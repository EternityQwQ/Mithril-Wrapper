#include "frontend/gl/DirectGlContext.h"

#include "egl/EglBridge.h"
#include "metal/MetalDeviceSession.h"
#include "shader/GlslangCompiler.h"
#include "shader/SpirvCrossMslCompiler.h"

#include <algorithm>
#include <cstring>
#include <span>

namespace mithril::frontend::gl {
namespace {

constexpr std::size_t kMaxShaderSource = 16U * 1024U * 1024U;

bool bufferTarget(GLenum target) {
    return target == GL_ARRAY_BUFFER || target == GL_ELEMENT_ARRAY_BUFFER;
}

bool bufferUsage(GLenum usage) {
    return usage == GL_STREAM_DRAW || usage == GL_STATIC_DRAW || usage == GL_DYNAMIC_DRAW;
}

shader::ShaderStage stage(GLenum type) {
    return type == GL_VERTEX_SHADER ? shader::ShaderStage::vertex : shader::ShaderStage::fragment;
}

} // namespace

DirectGlShareGroup::DirectGlShareGroup(std::shared_ptr<metal::MetalDeviceSession> session)
    : session_(std::move(session)) {}

DirectGlShareGroup::~DirectGlShareGroup() {
    if (!session_) return;
    for (auto& [name, buffer] : buffers_) {
        (void)name;
        if (buffer.handle.valid()) (void)session_->release(buffer.handle);
    }
    for (auto& [name, shader] : shaders_) {
        (void)name;
        if (shader.handle.valid()) (void)session_->release(shader.handle);
    }
}

GLuint DirectGlShareGroup::allocateName() {
    while (nextName_ == 0 || buffers_.contains(nextName_) || shaders_.contains(nextName_) ||
           programs_.contains(nextName_)) ++nextName_;
    return nextName_++;
}

DirectGlContext::DirectGlContext(std::shared_ptr<DirectGlShareGroup> share,
                                 std::shared_ptr<metal::MetalDeviceSession> session)
    : share_(std::move(share)), session_(std::move(session)) {
    vaos_.emplace(0, VertexArray{});
}

DirectGlContext::~DirectGlContext() {
    if (session_ && depthTexture_.valid()) (void)session_->release(depthTexture_);
}

const std::shared_ptr<DirectGlShareGroup>& DirectGlContext::shareGroup() const noexcept { return share_; }

void DirectGlContext::setError(GLenum error) noexcept {
    if (error_ == GL_NO_ERROR) error_ = error;
}

GLenum DirectGlContext::takeError() noexcept {
    const GLenum result = error_;
    error_ = GL_NO_ERROR;
    return result;
}

void DirectGlContext::backendError(const core::Error& error) {
    setError(error.code == core::ErrorCode::out_of_memory ? GL_OUT_OF_MEMORY : GL_INVALID_OPERATION);
}

GLuint DirectGlContext::boundBuffer(GLenum target) const {
    if (target == GL_ARRAY_BUFFER) return arrayBuffer_;
    if (target == GL_ELEMENT_ARRAY_BUFFER) {
        auto found = vaos_.find(currentVao_);
        return found != vaos_.end() ? found->second.elementBuffer : 0;
    }
    return 0;
}

DirectGlContext::VertexArray* DirectGlContext::currentVao() {
    auto found = vaos_.find(currentVao_);
    return found != vaos_.end() ? &found->second : nullptr;
}

void DirectGlContext::genBuffers(GLsizei count, GLuint* output) {
    if (count < 0 || (count != 0 && output == nullptr)) { setError(GL_INVALID_VALUE); return; }
    std::lock_guard lock(share_->mutex_);
    for (GLsizei index = 0; index < count; ++index) {
        const GLuint name = share_->allocateName();
        share_->buffers_.emplace(name, BufferObject{});
        output[index] = name;
    }
}

void DirectGlContext::deleteBuffers(GLsizei count, const GLuint* names) {
    if (count < 0 || (count != 0 && names == nullptr)) { setError(GL_INVALID_VALUE); return; }
    std::lock_guard lock(share_->mutex_);
    for (GLsizei index = 0; index < count; ++index) {
        auto found = share_->buffers_.find(names[index]);
        if (found == share_->buffers_.end()) continue;
        if (found->second.handle.valid()) (void)session_->release(found->second.handle);
        share_->buffers_.erase(found);
        if (arrayBuffer_ == names[index]) arrayBuffer_ = 0;
        for (auto& [vaoName, vao] : vaos_) {
            (void)vaoName;
            if (vao.elementBuffer == names[index]) vao.elementBuffer = 0;
            for (auto& attribute : vao.attributes) if (attribute.buffer == names[index]) attribute.buffer = 0;
        }
    }
}

void DirectGlContext::bindBuffer(GLenum target, GLuint name) {
    if (!bufferTarget(target)) { setError(GL_INVALID_ENUM); return; }
    std::lock_guard lock(share_->mutex_);
    if (name != 0 && !share_->buffers_.contains(name)) share_->buffers_.emplace(name, BufferObject{});
    if (target == GL_ARRAY_BUFFER) arrayBuffer_ = name;
    else if (auto* vao = currentVao()) vao->elementBuffer = name;
}

void DirectGlContext::bufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    if (!bufferTarget(target) || !bufferUsage(usage)) { setError(GL_INVALID_ENUM); return; }
    if (size < 0) { setError(GL_INVALID_VALUE); return; }
    const GLuint name = boundBuffer(target);
    if (name == 0) { setError(GL_INVALID_OPERATION); return; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->buffers_.find(name);
    if (found == share_->buffers_.end()) { setError(GL_INVALID_OPERATION); return; }
    auto created = session_->createBuffer({static_cast<std::size_t>(std::max<GLsizeiptr>(size, 1)),
        target == GL_ELEMENT_ARRAY_BUFFER ? backend::BufferUsage::index : backend::BufferUsage::vertex});
    if (!created) { backendError(created.error()); return; }
    if (found->second.handle.valid()) (void)session_->release(found->second.handle);
    found->second = {created.value(), size, usage};
    if (data != nullptr && size != 0) {
        auto uploaded = session_->upload(created.value(), 0,
            {static_cast<const std::byte*>(data), static_cast<std::size_t>(size)});
        if (!uploaded) backendError(uploaded.error());
    }
}

void DirectGlContext::bufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    if (!bufferTarget(target)) { setError(GL_INVALID_ENUM); return; }
    if (offset < 0 || size < 0 || (size != 0 && data == nullptr)) { setError(GL_INVALID_VALUE); return; }
    const GLuint name = boundBuffer(target);
    std::lock_guard lock(share_->mutex_);
    auto found = share_->buffers_.find(name);
    if (name == 0 || found == share_->buffers_.end() || !found->second.handle.valid()) {
        setError(GL_INVALID_OPERATION); return;
    }
    if (offset > found->second.size || size > found->second.size - offset) { setError(GL_INVALID_VALUE); return; }
    auto result = session_->upload(found->second.handle, static_cast<std::size_t>(offset),
        {static_cast<const std::byte*>(data), static_cast<std::size_t>(size)});
    if (!result) backendError(result.error());
}

void DirectGlContext::genVertexArrays(GLsizei count, GLuint* output) {
    if (count < 0 || (count != 0 && output == nullptr)) { setError(GL_INVALID_VALUE); return; }
    for (GLsizei index = 0; index < count; ++index) {
        while (nextVao_ == 0 || vaos_.contains(nextVao_)) ++nextVao_;
        output[index] = nextVao_;
        vaos_.emplace(nextVao_++, VertexArray{});
    }
}

void DirectGlContext::deleteVertexArrays(GLsizei count, const GLuint* names) {
    if (count < 0 || (count != 0 && names == nullptr)) { setError(GL_INVALID_VALUE); return; }
    for (GLsizei index = 0; index < count; ++index) {
        if (names[index] == 0) continue;
        vaos_.erase(names[index]);
        if (currentVao_ == names[index]) currentVao_ = 0;
    }
}

void DirectGlContext::bindVertexArray(GLuint name) {
    if (!vaos_.contains(name)) { setError(GL_INVALID_OPERATION); return; }
    currentVao_ = name;
}

void DirectGlContext::enableVertexAttrib(GLuint index, bool enabled) {
    auto* vao = currentVao();
    if (vao == nullptr || index >= vao->attributes.size()) { setError(GL_INVALID_VALUE); return; }
    vao->attributes[index].enabled = enabled;
}

void DirectGlContext::vertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                                          GLsizei stride, const void* pointer, bool integer) {
    auto* vao = currentVao();
    backend::VertexScalar scalar;
    if (vao == nullptr || index >= vao->attributes.size() || size < 1 || size > 4 || stride < 0) {
        setError(GL_INVALID_VALUE); return;
    }
    if (!vertexScalar(type, scalar)) { setError(GL_INVALID_ENUM); return; }
    (void)scalar;
    if (arrayBuffer_ == 0) { setError(GL_INVALID_OPERATION); return; }
    vao->attributes[index] = {arrayBuffer_, size, type, stride,
        reinterpret_cast<std::uintptr_t>(pointer), normalized == GL_TRUE, integer,
        vao->attributes[index].enabled};
}

GLuint DirectGlContext::createShader(GLenum type) {
    if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER) { setError(GL_INVALID_ENUM); return 0; }
    std::lock_guard lock(share_->mutex_);
    const GLuint name = share_->allocateName();
    ShaderObject object;
    object.type = type;
    share_->shaders_.emplace(name, std::move(object));
    return name;
}

void DirectGlContext::deleteShader(GLuint name) {
    std::lock_guard lock(share_->mutex_);
    auto found = share_->shaders_.find(name);
    if (found == share_->shaders_.end()) return;
    if (found->second.handle.valid()) (void)session_->release(found->second.handle);
    share_->shaders_.erase(found);
}

void DirectGlContext::shaderSource(GLuint name, GLsizei count, const GLchar* const* strings,
                                   const GLint* lengths) {
    if (count < 0 || (count != 0 && strings == nullptr)) { setError(GL_INVALID_VALUE); return; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->shaders_.find(name);
    if (found == share_->shaders_.end()) { setError(GL_INVALID_VALUE); return; }
    std::string combined;
    for (GLsizei index = 0; index < count; ++index) {
        if (strings[index] == nullptr) { setError(GL_INVALID_VALUE); return; }
        const std::size_t size = lengths != nullptr && lengths[index] >= 0
            ? static_cast<std::size_t>(lengths[index]) : std::strlen(strings[index]);
        if (size > kMaxShaderSource - combined.size()) { setError(GL_OUT_OF_MEMORY); return; }
        combined.append(strings[index], size);
    }
    found->second.source = std::move(combined);
    found->second.compiled = false;
    found->second.log.clear();
}

void DirectGlContext::compileShader(GLuint name) {
    std::lock_guard lock(share_->mutex_);
    auto found = share_->shaders_.find(name);
    if (found == share_->shaders_.end()) { setError(GL_INVALID_VALUE); return; }
    ShaderObject& object = found->second;
    shader::ShaderSource source{stage(object.type), object.source, "minecraft.glsl", "main"};
    shader::GlslangCompiler glslang;
    auto spirv = glslang.compile(source, {330, true});
    if (!spirv) { object.log = spirv.error().message; object.compiled = false; return; }
    shader::SpirvCrossMslCompiler spirvCross;
    auto msl = spirvCross.translate(spirv.value());
    if (!msl) { object.log = msl.error().message; object.compiled = false; return; }
    auto handle = session_->createShader(msl.value());
    if (!handle) { object.log = std::string(handle.error().text()); object.compiled = false; return; }
    if (object.handle.valid()) (void)session_->release(object.handle);
    object.handle = handle.value();
    object.log.clear();
    object.compiled = true;
}

void DirectGlContext::copyLog(std::string_view log, GLsizei capacity, GLsizei* length, GLchar* output) {
    if (capacity < 0) { setError(GL_INVALID_VALUE); return; }
    const std::size_t count = capacity > 0 ? std::min(log.size(), static_cast<std::size_t>(capacity - 1)) : 0;
    if (output != nullptr && capacity > 0) {
        std::memcpy(output, log.data(), count);
        output[count] = '\0';
    }
    if (length != nullptr) *length = static_cast<GLsizei>(count);
}

void DirectGlContext::getShaderiv(GLuint name, GLenum pname, GLint* output) {
    if (output == nullptr) { setError(GL_INVALID_VALUE); return; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->shaders_.find(name);
    if (found == share_->shaders_.end()) { setError(GL_INVALID_VALUE); return; }
    if (pname == GL_COMPILE_STATUS) *output = found->second.compiled ? GL_TRUE : GL_FALSE;
    else if (pname == GL_INFO_LOG_LENGTH) *output = static_cast<GLint>(found->second.log.size() + 1U);
    else if (pname == GL_SHADER_SOURCE_LENGTH) *output = static_cast<GLint>(found->second.source.size() + 1U);
    else if (pname == GL_SHADER_TYPE) *output = static_cast<GLint>(found->second.type);
    else { setError(GL_INVALID_ENUM); *output = 0; }
}

void DirectGlContext::getShaderInfoLog(GLuint name, GLsizei capacity, GLsizei* length, GLchar* output) {
    std::lock_guard lock(share_->mutex_);
    auto found = share_->shaders_.find(name);
    if (found == share_->shaders_.end()) { setError(GL_INVALID_VALUE); return; }
    copyLog(found->second.log, capacity, length, output);
}

GLuint DirectGlContext::createProgram() {
    std::lock_guard lock(share_->mutex_);
    const GLuint name = share_->allocateName();
    share_->programs_.emplace(name, ProgramObject{});
    return name;
}

void DirectGlContext::deleteProgram(GLuint name) {
    std::lock_guard lock(share_->mutex_);
    share_->programs_.erase(name);
    if (currentProgram_ == name) currentProgram_ = 0;
}

void DirectGlContext::attachShader(GLuint program, GLuint shaderName) {
    std::lock_guard lock(share_->mutex_);
    auto programIt = share_->programs_.find(program);
    if (programIt == share_->programs_.end() || !share_->shaders_.contains(shaderName)) {
        setError(GL_INVALID_VALUE); return;
    }
    auto& attached = programIt->second.attachedShaders;
    if (std::find(attached.begin(), attached.end(), shaderName) == attached.end()) attached.push_back(shaderName);
}

void DirectGlContext::detachShader(GLuint program, GLuint shaderName) {
    std::lock_guard lock(share_->mutex_);
    auto found = share_->programs_.find(program);
    if (found == share_->programs_.end()) { setError(GL_INVALID_VALUE); return; }
    std::erase(found->second.attachedShaders, shaderName);
}

void DirectGlContext::linkProgram(GLuint name) {
    std::lock_guard lock(share_->mutex_);
    auto found = share_->programs_.find(name);
    if (found == share_->programs_.end()) { setError(GL_INVALID_VALUE); return; }
    bool vertex = false;
    bool fragment = false;
    for (GLuint shaderName : found->second.attachedShaders) {
        auto shaderIt = share_->shaders_.find(shaderName);
        if (shaderIt == share_->shaders_.end() || !shaderIt->second.compiled) continue;
        vertex |= shaderIt->second.type == GL_VERTEX_SHADER;
        fragment |= shaderIt->second.type == GL_FRAGMENT_SHADER;
    }
    found->second.linked = vertex && fragment;
    found->second.log = found->second.linked ? "" : "program requires compiled vertex and fragment shaders";
}

void DirectGlContext::useProgram(GLuint name) {
    if (name == 0) { currentProgram_ = 0; return; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->programs_.find(name);
    if (found == share_->programs_.end() || !found->second.linked) { setError(GL_INVALID_OPERATION); return; }
    currentProgram_ = name;
}

void DirectGlContext::getProgramiv(GLuint name, GLenum pname, GLint* output) {
    if (output == nullptr) { setError(GL_INVALID_VALUE); return; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->programs_.find(name);
    if (found == share_->programs_.end()) { setError(GL_INVALID_VALUE); return; }
    if (pname == GL_LINK_STATUS || pname == GL_VALIDATE_STATUS) *output = found->second.linked ? GL_TRUE : GL_FALSE;
    else if (pname == GL_INFO_LOG_LENGTH) *output = static_cast<GLint>(found->second.log.size() + 1U);
    else if (pname == GL_ATTACHED_SHADERS) *output = static_cast<GLint>(found->second.attachedShaders.size());
    else { setError(GL_INVALID_ENUM); *output = 0; }
}

void DirectGlContext::getProgramInfoLog(GLuint name, GLsizei capacity, GLsizei* length, GLchar* output) {
    std::lock_guard lock(share_->mutex_);
    auto found = share_->programs_.find(name);
    if (found == share_->programs_.end()) { setError(GL_INVALID_VALUE); return; }
    copyLog(found->second.log, capacity, length, output);
}

void DirectGlContext::viewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (width < 0 || height < 0) { setError(GL_INVALID_VALUE); return; }
    viewport_ = {static_cast<double>(x), static_cast<double>(y), static_cast<double>(width),
        static_cast<double>(height), 0.0, 1.0};
}

void DirectGlContext::clearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) noexcept {
    clearColor_ = {red, green, blue, alpha};
}
void DirectGlContext::clearDepth(GLdouble value) noexcept { clearDepth_ = std::clamp(value, 0.0, 1.0); }
void DirectGlContext::clearStencil(GLint value) noexcept { clearStencil_ = static_cast<std::uint32_t>(value); }

core::ValueResult<core::TextureHandle> DirectGlContext::depthTarget(std::uint32_t width,
                                                                    std::uint32_t height) {
    if (depthTexture_.valid() && depthWidth_ == width && depthHeight_ == height) return depthTexture_;
    if (depthTexture_.valid()) (void)session_->release(depthTexture_);
    auto created = session_->createTexture({width, height, 1, backend::PixelFormat::depth32FloatStencil8});
    if (created) { depthTexture_ = created.value(); depthWidth_ = width; depthHeight_ = height; }
    return created;
}

void DirectGlContext::clear(GLbitfield mask) {
    if ((mask & ~(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) != 0) {
        setError(GL_INVALID_VALUE); return;
    }
    auto frame = egl::bridge::acquireDrawFrame();
    if (!frame) { backendError(frame.error()); return; }
    auto commands = session_->createCommandEncoder();
    if (!commands) { backendError(commands.error()); return; }
    backend::RenderPassDesc pass;
    pass.color = frame.value().drawable;
    pass.clearColor = (mask & GL_COLOR_BUFFER_BIT) != 0;
    pass.clearRed = clearColor_[0]; pass.clearGreen = clearColor_[1];
    pass.clearBlue = clearColor_[2]; pass.clearAlpha = clearColor_[3];
    if ((mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) != 0) {
        auto depth = depthTarget(frame.value().width, frame.value().height);
        if (!depth) { backendError(depth.error()); return; }
        pass.depthStencil = depth.value();
        pass.clearDepth = (mask & GL_DEPTH_BUFFER_BIT) != 0;
        pass.clearDepthValue = clearDepth_;
        pass.clearStencil = (mask & GL_STENCIL_BUFFER_BIT) != 0;
        pass.clearStencilValue = clearStencil_;
    }
    auto result = commands.value()->beginRenderPass(pass);
    if (result) result = commands.value()->endRenderPass();
    if (result) result = commands.value()->commit();
    if (!result) backendError(result.error());
}

bool DirectGlContext::primitive(GLenum mode, ir::Primitive& output) noexcept {
    if (mode == GL_POINTS) output = ir::Primitive::point;
    else if (mode == GL_LINES) output = ir::Primitive::line;
    else if (mode == GL_LINE_STRIP) output = ir::Primitive::lineStrip;
    else if (mode == GL_TRIANGLES) output = ir::Primitive::triangle;
    else if (mode == GL_TRIANGLE_STRIP) output = ir::Primitive::triangleStrip;
    else return false;
    return true;
}

bool DirectGlContext::vertexScalar(GLenum type, backend::VertexScalar& output) noexcept {
    switch (type) {
        case GL_FLOAT: output = backend::VertexScalar::float32; return true;
        case GL_INT: output = backend::VertexScalar::sint32; return true;
        case GL_UNSIGNED_INT: output = backend::VertexScalar::uint32; return true;
        case GL_SHORT: output = backend::VertexScalar::sint16; return true;
        case GL_UNSIGNED_SHORT: output = backend::VertexScalar::uint16; return true;
        case GL_BYTE: output = backend::VertexScalar::sint8; return true;
        case GL_UNSIGNED_BYTE: output = backend::VertexScalar::uint8; return true;
        default: return false;
    }
}

core::ValueResult<core::PipelineHandle> DirectGlContext::pipelineFor(GLenum mode,
                                                                     const VertexArray& vao) {
    ir::Primitive primitiveMode;
    if (!primitive(mode, primitiveMode)) return core::ValueResult<core::PipelineHandle>::failure(
        core::Error::make(core::ErrorDomain::gl, core::ErrorCode::unsupported, "unsupported GL primitive"));
    std::lock_guard lock(share_->mutex_);
    auto program = share_->programs_.find(currentProgram_);
    if (program == share_->programs_.end() || !program->second.linked) {
        return core::ValueResult<core::PipelineHandle>::failure(core::Error::make(
            core::ErrorDomain::gl, core::ErrorCode::invalid_state, "no linked GL program is active"));
    }
    std::vector<core::ShaderHandle> shaders;
    for (GLuint name : program->second.attachedShaders) {
        auto found = share_->shaders_.find(name);
        if (found != share_->shaders_.end() && found->second.compiled) shaders.push_back(found->second.handle);
    }
    backend::PipelineDesc desc;
    desc.key.colorFormats[0] = backend::PixelFormat::bgra8Unorm;
    desc.key.depthStencilFormat = depthTexture_.valid()
        ? backend::PixelFormat::depth32FloatStencil8 : backend::PixelFormat::none;
    desc.key.primitive = primitiveMode;
    for (std::uint32_t location = 0; location < vao.attributes.size(); ++location) {
        const auto& attribute = vao.attributes[location];
        if (!attribute.enabled) continue;
        backend::VertexScalar scalar;
        if (!vertexScalar(attribute.type, scalar)) return core::ValueResult<core::PipelineHandle>::failure(
            core::Error::make(core::ErrorDomain::gl, core::ErrorCode::unsupported, "unsupported GL vertex type"));
        const std::uint32_t scalarSize = attribute.type == GL_BYTE || attribute.type == GL_UNSIGNED_BYTE ? 1U :
            attribute.type == GL_SHORT || attribute.type == GL_UNSIGNED_SHORT ? 2U : 4U;
        desc.vertexAttributes.push_back({location, location, 0,
            static_cast<std::uint32_t>(attribute.stride != 0 ? attribute.stride : attribute.size * scalarSize),
            scalar, static_cast<std::uint8_t>(attribute.size), attribute.normalized});
    }
    return session_->createPipeline(shaders, desc);
}

void DirectGlContext::drawArrays(GLenum mode, GLint first, GLsizei count, GLsizei instances) {
    if (first < 0 || count < 0 || instances < 0) { setError(GL_INVALID_VALUE); return; }
    auto* vao = currentVao();
    if (vao == nullptr || currentProgram_ == 0) { setError(GL_INVALID_OPERATION); return; }
    auto frame = egl::bridge::acquireDrawFrame();
    auto pipeline = pipelineFor(mode, *vao);
    if (!frame) { backendError(frame.error()); return; }
    if (!pipeline) { backendError(pipeline.error()); return; }
    auto commands = session_->createCommandEncoder();
    if (!commands) { backendError(commands.error()); return; }
    backend::RenderPassDesc pass{frame.value().drawable, depthTexture_};
    core::Result result = commands.value()->beginRenderPass(pass);
    if (result) result = commands.value()->bindPipeline(pipeline.value());
    backend::Viewport viewport = viewport_;
    if (viewport.width == 0 || viewport.height == 0) {
        viewport.width = frame.value().width; viewport.height = frame.value().height;
    }
    if (result) result = commands.value()->setViewport(viewport);
    {
        std::lock_guard lock(share_->mutex_);
        for (std::uint32_t location = 0; result && location < vao->attributes.size(); ++location) {
            const auto& attribute = vao->attributes[location];
            if (!attribute.enabled) continue;
            auto buffer = share_->buffers_.find(attribute.buffer);
            if (buffer == share_->buffers_.end() || !buffer->second.handle.valid()) {
                result = core::Result::failure(core::Error::make(core::ErrorDomain::gl,
                    core::ErrorCode::invalid_state, "vertex attribute has no buffer")); break;
            }
            result = commands.value()->bindVertexBuffer(location, buffer->second.handle, attribute.offset);
        }
    }
    if (result) result = commands.value()->draw({static_cast<std::uint32_t>(first),
        static_cast<std::uint32_t>(count), static_cast<std::uint32_t>(instances)});
    if (result) result = commands.value()->endRenderPass();
    if (result) result = commands.value()->commit();
    if (!result) backendError(result.error());
    (void)session_->release(pipeline.value());
}

void DirectGlContext::drawElements(GLenum mode, GLsizei count, GLenum type, const void* indices,
                                   GLsizei instances, GLint baseVertex, GLuint baseInstance) {
    if (count < 0 || instances < 0) { setError(GL_INVALID_VALUE); return; }
    if (type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT) { setError(GL_INVALID_ENUM); return; }
    auto* vao = currentVao();
    if (vao == nullptr || vao->elementBuffer == 0 || currentProgram_ == 0) {
        setError(GL_INVALID_OPERATION); return;
    }
    auto frame = egl::bridge::acquireDrawFrame();
    auto pipeline = pipelineFor(mode, *vao);
    if (!frame) { backendError(frame.error()); return; }
    if (!pipeline) { backendError(pipeline.error()); return; }
    auto commands = session_->createCommandEncoder();
    if (!commands) { backendError(commands.error()); return; }
    backend::RenderPassDesc pass{frame.value().drawable, depthTexture_};
    core::Result result = commands.value()->beginRenderPass(pass);
    if (result) result = commands.value()->bindPipeline(pipeline.value());
    backend::Viewport viewport = viewport_;
    if (viewport.width == 0 || viewport.height == 0) {
        viewport.width = frame.value().width; viewport.height = frame.value().height;
    }
    if (result) result = commands.value()->setViewport(viewport);
    {
        std::lock_guard lock(share_->mutex_);
        for (std::uint32_t location = 0; result && location < vao->attributes.size(); ++location) {
            const auto& attribute = vao->attributes[location];
            if (!attribute.enabled) continue;
            auto buffer = share_->buffers_.find(attribute.buffer);
            if (buffer == share_->buffers_.end() || !buffer->second.handle.valid()) {
                result = core::Result::failure(core::Error::make(core::ErrorDomain::gl,
                    core::ErrorCode::invalid_state, "vertex attribute has no buffer")); break;
            }
            result = commands.value()->bindVertexBuffer(location, buffer->second.handle, attribute.offset);
        }
        auto element = share_->buffers_.find(vao->elementBuffer);
        if (result && (element == share_->buffers_.end() || !element->second.handle.valid())) {
            result = core::Result::failure(core::Error::make(core::ErrorDomain::gl,
                core::ErrorCode::invalid_state, "vertex array has no element buffer"));
        } else if (result) {
            result = commands.value()->bindIndexBuffer(element->second.handle,
                reinterpret_cast<std::uintptr_t>(indices),
                type == GL_UNSIGNED_SHORT ? backend::IndexType::uint16 : backend::IndexType::uint32);
        }
    }
    if (result) result = commands.value()->drawIndexed({static_cast<std::uint32_t>(count),
        static_cast<std::uint32_t>(instances), baseVertex, baseInstance});
    if (result) result = commands.value()->endRenderPass();
    if (result) result = commands.value()->commit();
    if (!result) backendError(result.error());
    (void)session_->release(pipeline.value());
}

void DirectGlContext::finish() {
    auto result = session_->waitIdle(5'000'000'000ULL);
    if (!result) backendError(result.error());
}

void DirectGlContext::getIntegerv(GLenum name, GLint* output) {
    if (output == nullptr) { setError(GL_INVALID_VALUE); return; }
    if (name == GL_ARRAY_BUFFER_BINDING) *output = static_cast<GLint>(arrayBuffer_);
    else if (name == GL_ELEMENT_ARRAY_BUFFER_BINDING) *output = static_cast<GLint>(boundBuffer(GL_ELEMENT_ARRAY_BUFFER));
    else if (name == GL_VERTEX_ARRAY_BINDING) *output = static_cast<GLint>(currentVao_);
    else if (name == GL_CURRENT_PROGRAM) *output = static_cast<GLint>(currentProgram_);
    else if (name == GL_MAX_VERTEX_ATTRIBS) *output = 16;
    else { setError(GL_INVALID_ENUM); *output = 0; }
}

} // namespace mithril::frontend::gl
