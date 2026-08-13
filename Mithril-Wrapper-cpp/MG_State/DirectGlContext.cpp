#include "MG_State/DirectGlContext.h"

#include "egl/EglBridge.h"
#include "MG_Backend/DirectMetal/MetalDeviceSession.h"
#include "shader/GlslangCompiler.h"
#include "shader/SpirvCrossMslCompiler.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <span>

namespace mithril::frontend::gl {
namespace {

constexpr std::size_t kMaxShaderSource = 16U * 1024U * 1024U;
constexpr std::uint32_t kMaxTextureUnits = 32U;

bool bufferTarget(GLenum target) {
    return target == GL_ARRAY_BUFFER || target == GL_ELEMENT_ARRAY_BUFFER || target == GL_UNIFORM_BUFFER;
}

bool bufferUsage(GLenum usage) {
    return usage == GL_STREAM_DRAW || usage == GL_STATIC_DRAW || usage == GL_DYNAMIC_DRAW;
}

shader::ShaderStage stage(GLenum type) {
    return type == GL_VERTEX_SHADER ? shader::ShaderStage::vertex : shader::ShaderStage::fragment;
}

bool colorFormat(backend::PixelFormat format) {
    return format == backend::PixelFormat::rgba8Unorm ||
        format == backend::PixelFormat::bgra8Unorm;
}

bool depthFormat(backend::PixelFormat format) {
    return format == backend::PixelFormat::depth32Float ||
        format == backend::PixelFormat::depth24Stencil8 ||
        format == backend::PixelFormat::depth32FloatStencil8;
}

bool stencilFormat(backend::PixelFormat format) {
    return format == backend::PixelFormat::depth24Stencil8 ||
        format == backend::PixelFormat::depth32FloatStencil8;
}

bool textureTransferFormat(backend::PixelFormat internalFormat, GLenum format, GLenum type) {
    if (colorFormat(internalFormat)) {
        return (format == GL_RGB || format == GL_RGBA) && type == GL_UNSIGNED_BYTE;
    }
    if (internalFormat == backend::PixelFormat::depth32Float) {
        return format == GL_DEPTH_COMPONENT &&
            (type == GL_FLOAT || type == GL_UNSIGNED_INT || type == GL_UNSIGNED_SHORT ||
             type == GL_UNSIGNED_BYTE);
    }
    if (stencilFormat(internalFormat)) {
        return format == GL_DEPTH_STENCIL &&
            (type == GL_UNSIGNED_INT_24_8 || type == GL_FLOAT_32_UNSIGNED_INT_24_8_REV);
    }
    return false;
}

bool compareFunction(GLenum function, ir::Compare& output) {
    switch (function) {
        case GL_NEVER: output = ir::Compare::never; return true;
        case GL_LESS: output = ir::Compare::less; return true;
        case GL_LEQUAL: output = ir::Compare::lessEqual; return true;
        case GL_EQUAL: output = ir::Compare::equal; return true;
        case GL_NOTEQUAL: output = ir::Compare::notEqual; return true;
        case GL_GREATER: output = ir::Compare::greater; return true;
        case GL_GEQUAL: output = ir::Compare::greaterEqual; return true;
        case GL_ALWAYS: output = ir::Compare::always; return true;
        default: return false;
    }
}

bool blendFactor(GLenum factor, ir::BlendFactor& output) {
    switch (factor) {
        case GL_ZERO: output = ir::BlendFactor::zero; return true;
        case GL_ONE: output = ir::BlendFactor::one; return true;
        case GL_SRC_COLOR: output = ir::BlendFactor::sourceColor; return true;
        case GL_ONE_MINUS_SRC_COLOR: output = ir::BlendFactor::oneMinusSourceColor; return true;
        case GL_SRC_ALPHA: output = ir::BlendFactor::sourceAlpha; return true;
        case GL_ONE_MINUS_SRC_ALPHA: output = ir::BlendFactor::oneMinusSourceAlpha; return true;
        case GL_DST_COLOR: output = ir::BlendFactor::destinationColor; return true;
        case GL_ONE_MINUS_DST_COLOR: output = ir::BlendFactor::oneMinusDestinationColor; return true;
        case GL_DST_ALPHA: output = ir::BlendFactor::destinationAlpha; return true;
        case GL_ONE_MINUS_DST_ALPHA: output = ir::BlendFactor::oneMinusDestinationAlpha; return true;
        case GL_SRC_ALPHA_SATURATE: output = ir::BlendFactor::sourceAlphaSaturated; return true;
        case GL_CONSTANT_COLOR: output = ir::BlendFactor::blendColor; return true;
        case GL_ONE_MINUS_CONSTANT_COLOR: output = ir::BlendFactor::oneMinusBlendColor; return true;
        case GL_CONSTANT_ALPHA: output = ir::BlendFactor::blendAlpha; return true;
        case GL_ONE_MINUS_CONSTANT_ALPHA: output = ir::BlendFactor::oneMinusBlendAlpha; return true;
        default: return false;
    }
}

bool blendOperation(GLenum operation, ir::BlendOperation& output) {
    switch (operation) {
        case GL_FUNC_ADD: output = ir::BlendOperation::add; return true;
        case GL_FUNC_SUBTRACT: output = ir::BlendOperation::subtract; return true;
        case GL_FUNC_REVERSE_SUBTRACT: output = ir::BlendOperation::reverseSubtract; return true;
        case GL_MIN: output = ir::BlendOperation::minimum; return true;
        case GL_MAX: output = ir::BlendOperation::maximum; return true;
        default: return false;
    }
}

backend::RenderStage renderStage(shader::ShaderStage stage) {
    return stage == shader::ShaderStage::vertex
        ? backend::RenderStage::vertex : backend::RenderStage::fragment;
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
    for (auto& [name, texture] : textures_) {
        (void)name;
        if (texture.sampler.valid()) (void)session_->release(texture.sampler);
        if (texture.handle.valid()) (void)session_->release(texture.handle);
    }
    for (auto& [name, renderbuffer] : renderbuffers_) {
        (void)name;
        if (renderbuffer.handle.valid()) (void)session_->release(renderbuffer.handle);
    }
}

GLuint DirectGlShareGroup::allocateName() {
    while (nextName_ == 0 || buffers_.contains(nextName_) || shaders_.contains(nextName_) ||
           programs_.contains(nextName_) || textures_.contains(nextName_) ||
           renderbuffers_.contains(nextName_)) ++nextName_;
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
    if (target == GL_UNIFORM_BUFFER) return uniformBuffer_;
    if (target == GL_ELEMENT_ARRAY_BUFFER) {
        auto found = vaos_.find(currentVao_);
        return found != vaos_.end() ? found->second.elementBuffer : 0;
    }
    return 0;
}

GLuint DirectGlContext::boundTexture2D() const noexcept {
    return texture2D_[activeTextureUnit_];
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
        if (uniformBuffer_ == names[index]) uniformBuffer_ = 0;
        for (auto& binding : uniformBuffers_) if (binding.name == names[index]) binding = {};
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
    else if (target == GL_UNIFORM_BUFFER) uniformBuffer_ = name;
    else if (auto* vao = currentVao()) vao->elementBuffer = name;
}

void DirectGlContext::bindBufferRange(GLenum target, GLuint index, GLuint name,
                                      GLintptr offset, GLsizeiptr size) {
    if (target != GL_UNIFORM_BUFFER) { setError(GL_INVALID_ENUM); return; }
    if (index >= uniformBuffers_.size()) { setError(GL_INVALID_VALUE); return; }
    if (offset < 0 || size <= 0) { setError(GL_INVALID_VALUE); return; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->buffers_.find(name);
    if (name != 0 && (found == share_->buffers_.end() || offset > found->second.size ||
        size > found->second.size - offset)) { setError(GL_INVALID_VALUE); return; }
    uniformBuffer_ = name;
    uniformBuffers_[index] = {name, offset, size};
}

void DirectGlContext::bindBufferBase(GLenum target, GLuint index, GLuint name) {
    GLsizeiptr size = 1;
    {
        std::lock_guard lock(share_->mutex_);
        auto found = share_->buffers_.find(name);
        if (name != 0) {
            if (found == share_->buffers_.end() || !found->second.handle.valid()) {
                setError(GL_INVALID_OPERATION); return;
            }
            size = std::max<GLsizeiptr>(1, found->second.size);
        }
    }
    bindBufferRange(target, index, name, 0, size);
}

void DirectGlContext::bufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    if (!bufferTarget(target) || !bufferUsage(usage)) { setError(GL_INVALID_ENUM); return; }
    if (size < 0) { setError(GL_INVALID_VALUE); return; }
    const GLuint name = boundBuffer(target);
    if (name == 0) { setError(GL_INVALID_OPERATION); return; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->buffers_.find(name);
    if (found == share_->buffers_.end()) { setError(GL_INVALID_OPERATION); return; }
    if (found->second.mapped) { setError(GL_INVALID_OPERATION); return; }
    auto created = session_->createBuffer({static_cast<std::size_t>(std::max<GLsizeiptr>(size, 1)),
        target == GL_ELEMENT_ARRAY_BUFFER ? backend::BufferUsage::index :
        target == GL_UNIFORM_BUFFER ? backend::BufferUsage::uniform : backend::BufferUsage::vertex});
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
    if (found->second.mapped) { setError(GL_INVALID_OPERATION); return; }
    if (offset > found->second.size || size > found->second.size - offset) { setError(GL_INVALID_VALUE); return; }
    auto result = session_->upload(found->second.handle, static_cast<std::size_t>(offset),
        {static_cast<const std::byte*>(data), static_cast<std::size_t>(size)});
    if (!result) backendError(result.error());
}

void* DirectGlContext::mapBuffer(GLenum target, GLenum access) {
    GLbitfield flags = 0;
    if (access == GL_READ_ONLY) flags = GL_MAP_READ_BIT;
    else if (access == GL_WRITE_ONLY) flags = GL_MAP_WRITE_BIT;
    else if (access == GL_READ_WRITE) flags = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;
    else { setError(GL_INVALID_ENUM); return nullptr; }
    const GLuint name = boundBuffer(target);
    if (!bufferTarget(target)) { setError(GL_INVALID_ENUM); return nullptr; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->buffers_.find(name);
    if (name == 0 || found == share_->buffers_.end() || !found->second.handle.valid() ||
        found->second.size == 0 || found->second.mapped) {
        setError(GL_INVALID_OPERATION); return nullptr;
    }
    auto mapped = session_->mapBuffer(found->second.handle, 0,
        static_cast<std::size_t>(found->second.size));
    if (!mapped) { backendError(mapped.error()); return nullptr; }
    found->second.mappedOffset = 0;
    found->second.mappedLength = found->second.size;
    found->second.mappedAccess = flags;
    found->second.mapped = true;
    return mapped.value();
}

void* DirectGlContext::mapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length,
                                      GLbitfield access) {
    constexpr GLbitfield allowed = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT |
        GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
        GL_MAP_FLUSH_EXPLICIT_BIT | GL_MAP_UNSYNCHRONIZED_BIT;
    if (!bufferTarget(target)) { setError(GL_INVALID_ENUM); return nullptr; }
    if ((access & ~allowed) != 0 || (access & (GL_MAP_READ_BIT | GL_MAP_WRITE_BIT)) == 0 ||
        ((access & GL_MAP_READ_BIT) != 0 &&
         (access & (GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
                    GL_MAP_UNSYNCHRONIZED_BIT)) != 0) ||
        ((access & GL_MAP_FLUSH_EXPLICIT_BIT) != 0 && (access & GL_MAP_WRITE_BIT) == 0)) {
        setError(GL_INVALID_VALUE); return nullptr;
    }
    if (offset < 0 || length <= 0) { setError(GL_INVALID_VALUE); return nullptr; }
    const GLuint name = boundBuffer(target);
    std::lock_guard lock(share_->mutex_);
    auto found = share_->buffers_.find(name);
    if (name == 0 || found == share_->buffers_.end() || !found->second.handle.valid() ||
        found->second.mapped) {
        setError(GL_INVALID_OPERATION); return nullptr;
    }
    if (offset > found->second.size || length > found->second.size - offset) {
        setError(GL_INVALID_VALUE); return nullptr;
    }
    auto mapped = session_->mapBuffer(found->second.handle, static_cast<std::size_t>(offset),
        static_cast<std::size_t>(length));
    if (!mapped) { backendError(mapped.error()); return nullptr; }
    found->second.mappedOffset = offset;
    found->second.mappedLength = length;
    found->second.mappedAccess = access;
    found->second.mapped = true;
    return mapped.value();
}

GLboolean DirectGlContext::unmapBuffer(GLenum target) {
    if (!bufferTarget(target)) { setError(GL_INVALID_ENUM); return GL_FALSE; }
    const GLuint name = boundBuffer(target);
    std::lock_guard lock(share_->mutex_);
    auto found = share_->buffers_.find(name);
    if (name == 0 || found == share_->buffers_.end() || !found->second.mapped) {
        setError(GL_INVALID_OPERATION); return GL_FALSE;
    }
    found->second.mappedOffset = 0;
    found->second.mappedLength = 0;
    found->second.mappedAccess = 0;
    found->second.mapped = false;
    return GL_TRUE;
}

void DirectGlContext::flushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length) {
    if (!bufferTarget(target)) { setError(GL_INVALID_ENUM); return; }
    if (offset < 0 || length < 0) { setError(GL_INVALID_VALUE); return; }
    const GLuint name = boundBuffer(target);
    std::lock_guard lock(share_->mutex_);
    auto found = share_->buffers_.find(name);
    if (name == 0 || found == share_->buffers_.end() || !found->second.mapped ||
        (found->second.mappedAccess & GL_MAP_FLUSH_EXPLICIT_BIT) == 0) {
        setError(GL_INVALID_OPERATION); return;
    }
    if (offset > found->second.mappedLength || length > found->second.mappedLength - offset)
        setError(GL_INVALID_VALUE);
}

void DirectGlContext::getBufferParameteriv(GLenum target, GLenum pname, GLint* output) {
    if (output == nullptr) { setError(GL_INVALID_VALUE); return; }
    if (!bufferTarget(target)) { setError(GL_INVALID_ENUM); *output = 0; return; }
    const GLuint name = boundBuffer(target);
    std::lock_guard lock(share_->mutex_);
    auto found = share_->buffers_.find(name);
    if (name == 0 || found == share_->buffers_.end()) {
        setError(GL_INVALID_OPERATION); *output = 0; return;
    }
    if (pname == GL_BUFFER_SIZE) *output = static_cast<GLint>(found->second.size);
    else if (pname == GL_BUFFER_USAGE) *output = static_cast<GLint>(found->second.usage);
    else if (pname == GL_BUFFER_MAPPED) *output = found->second.mapped ? GL_TRUE : GL_FALSE;
    else if (pname == GL_BUFFER_ACCESS_FLAGS) *output = static_cast<GLint>(found->second.mappedAccess);
    else if (pname == GL_BUFFER_MAP_OFFSET) *output = static_cast<GLint>(found->second.mappedOffset);
    else if (pname == GL_BUFFER_MAP_LENGTH) *output = static_cast<GLint>(found->second.mappedLength);
    else { setError(GL_INVALID_ENUM); *output = 0; }
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
        vao->attributes[index].enabled, vao->attributes[index].divisor};
}

void DirectGlContext::vertexAttribDivisor(GLuint index, GLuint divisor) {
    auto* vao = currentVao();
    if (vao == nullptr || index >= vao->attributes.size()) { setError(GL_INVALID_VALUE); return; }
    vao->attributes[index].divisor = divisor;
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
    found->second.deletePending = true;
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
    object.bindings = std::move(msl.value().bindings);
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

void DirectGlContext::getShaderSource(GLuint name, GLsizei capacity, GLsizei* length, GLchar* output) {
    std::lock_guard lock(share_->mutex_);
    auto found = share_->shaders_.find(name);
    if (found == share_->shaders_.end()) { setError(GL_INVALID_VALUE); return; }
    copyLog(found->second.source, capacity, length, output);
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
    found->second.uniforms.clear();
    found->second.locations.clear();
    found->second.uniformLocations.clear();
    found->second.attributeLocations.clear();
    found->second.metalAttributeLocations.clear();
    found->second.attributes.clear();
    found->second.uniformBlocks.clear();
    found->second.uniformBlockIndices.clear();
    for (GLuint shaderName : found->second.attachedShaders) {
        auto shaderIt = share_->shaders_.find(shaderName);
        if (shaderIt == share_->shaders_.end() || !shaderIt->second.compiled) continue;
        vertex |= shaderIt->second.type == GL_VERTEX_SHADER;
        fragment |= shaderIt->second.type == GL_FRAGMENT_SHADER;
        for (const auto& binding : shaderIt->second.bindings) {
            if (binding.kind == shader::BindingKind::stageInput) {
                const auto requested = found->second.requestedAttributeLocations.find(binding.name);
                const GLuint glLocation = requested != found->second.requestedAttributeLocations.end()
                    ? requested->second : binding.binding;
                if (glLocation >= 16U || found->second.metalAttributeLocations.contains(glLocation)) {
                    found->second.linked = false;
                    found->second.log = "vertex attribute location collision: " + binding.name;
                    return;
                }
                found->second.attributeLocations[binding.name] = static_cast<GLint>(glLocation);
                found->second.metalAttributeLocations[glLocation] = binding.binding;
                found->second.attributes.push_back({binding.name, binding.scalar, binding.vectorSize,
                    binding.columns, binding.arraySize, glLocation, binding.binding});
                continue;
            }
            if (binding.kind == shader::BindingKind::uniformBuffer) {
                auto block = std::find_if(found->second.uniformBlocks.begin(), found->second.uniformBlocks.end(),
                    [&](const ProgramUniformBlock& candidate) { return candidate.name == binding.name; });
                if (block == found->second.uniformBlocks.end()) {
                    found->second.uniformBlocks.push_back({binding.name, 0, binding.byteSize, {}});
                    block = std::prev(found->second.uniformBlocks.end());
                } else block->byteSize = std::max(block->byteSize, binding.byteSize);
                block->stages.push_back({binding.stage, binding.mslBuffer,
                    binding.mslTexture, binding.mslSampler});
                continue;
            }
            auto uniform = std::find_if(found->second.uniforms.begin(), found->second.uniforms.end(),
                [&](const ProgramUniform& candidate) { return candidate.name == binding.name; });
            if (uniform == found->second.uniforms.end()) {
                ProgramUniform created;
                created.name = binding.name;
                created.scalar = binding.scalar;
                created.vectorSize = binding.vectorSize;
                created.columns = binding.columns;
                created.arraySize = binding.arraySize;
                created.elementBytes = binding.kind == shader::BindingKind::sampledTexture
                    ? sizeof(GLint) : binding.byteSize / std::max(1U, binding.arraySize);
                created.value.resize(created.elementBytes * created.arraySize);
                found->second.uniforms.push_back(std::move(created));
                uniform = std::prev(found->second.uniforms.end());
            } else if (uniform->scalar != binding.scalar ||
                       uniform->vectorSize != binding.vectorSize ||
                       uniform->columns != binding.columns ||
                       uniform->arraySize != binding.arraySize) {
                found->second.linked = false;
                found->second.log = "uniform type mismatch across shader stages: " + binding.name;
                return;
            }
            uniform->bindings.push_back({binding.stage, binding.mslBuffer,
                binding.mslTexture, binding.mslSampler});
        }
    }
    found->second.linked = vertex && fragment;
    found->second.log = found->second.linked ? "" : "program requires compiled vertex and fragment shaders";
    if (found->second.linked) found->second.linkedShaders = found->second.attachedShaders;
    if (found->second.linked) {
        for (std::size_t uniformIndex = 0; uniformIndex < found->second.uniforms.size(); ++uniformIndex) {
            auto& uniform = found->second.uniforms[uniformIndex];
            const GLint baseLocation = static_cast<GLint>(found->second.locations.size());
            found->second.uniformLocations.emplace(uniform.name, baseLocation);
            if (uniform.arraySize != 1U) found->second.uniformLocations.emplace(uniform.name + "[0]", baseLocation);
            for (std::uint32_t element = 0; element < uniform.arraySize; ++element) {
                const GLint location = static_cast<GLint>(found->second.locations.size());
                found->second.locations.emplace_back(uniformIndex, element);
                if (element != 0U) found->second.uniformLocations.emplace(
                    uniform.name + "[" + std::to_string(element) + "]", location);
            }
        }
        for (std::size_t index = 0; index < found->second.uniformBlocks.size(); ++index)
            found->second.uniformBlockIndices.emplace(found->second.uniformBlocks[index].name,
                static_cast<GLuint>(index));
    }
}

GLuint DirectGlContext::getUniformBlockIndex(GLuint program, const GLchar* name) {
    if (name == nullptr) { setError(GL_INVALID_VALUE); return GL_INVALID_INDEX; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->programs_.find(program);
    if (found == share_->programs_.end()) { setError(GL_INVALID_VALUE); return GL_INVALID_INDEX; }
    auto index = found->second.uniformBlockIndices.find(name);
    return index != found->second.uniformBlockIndices.end() ? index->second : GL_INVALID_INDEX;
}

void DirectGlContext::getActiveUniformBlockiv(GLuint program, GLuint index, GLenum pname, GLint* output) {
    if (output == nullptr) { setError(GL_INVALID_VALUE); return; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->programs_.find(program);
    if (found == share_->programs_.end()) { setError(GL_INVALID_VALUE); return; }
    if (index >= found->second.uniformBlocks.size()) { setError(GL_INVALID_VALUE); return; }
    if (pname == GL_UNIFORM_BLOCK_BINDING) *output = static_cast<GLint>(found->second.uniformBlocks[index].binding);
    else if (pname == GL_UNIFORM_BLOCK_DATA_SIZE)
        *output = static_cast<GLint>(found->second.uniformBlocks[index].byteSize);
    else if (pname == GL_UNIFORM_BLOCK_NAME_LENGTH)
        *output = static_cast<GLint>(found->second.uniformBlocks[index].name.size() + 1U);
    else { setError(GL_INVALID_ENUM); *output = 0; }
}

void DirectGlContext::uniformBlockBinding(GLuint program, GLuint index, GLuint binding) {
    if (binding >= uniformBuffers_.size()) { setError(GL_INVALID_VALUE); return; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->programs_.find(program);
    if (found == share_->programs_.end()) { setError(GL_INVALID_VALUE); return; }
    if (index >= found->second.uniformBlocks.size()) { setError(GL_INVALID_VALUE); return; }
    found->second.uniformBlocks[index].binding = binding;
}

void DirectGlContext::bindAttribLocation(GLuint program, GLuint index, const GLchar* name) {
    if (name == nullptr) { setError(GL_INVALID_VALUE); return; }
    if (index >= 16U) { setError(GL_INVALID_VALUE); return; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->programs_.find(program);
    if (found == share_->programs_.end()) { setError(GL_INVALID_VALUE); return; }
    found->second.requestedAttributeLocations[name] = index;
}

GLint DirectGlContext::getAttribLocation(GLuint program, const GLchar* name) {
    if (name == nullptr) { setError(GL_INVALID_VALUE); return -1; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->programs_.find(program);
    if (found == share_->programs_.end()) { setError(GL_INVALID_VALUE); return -1; }
    if (!found->second.linked) { setError(GL_INVALID_OPERATION); return -1; }
    auto location = found->second.attributeLocations.find(name);
    return location != found->second.attributeLocations.end() ? location->second : -1;
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
    else if (pname == GL_ACTIVE_UNIFORMS) *output = static_cast<GLint>(found->second.uniforms.size());
    else if (pname == GL_ACTIVE_UNIFORM_MAX_LENGTH) {
        std::size_t length = 0;
        for (const auto& uniform : found->second.uniforms) length = std::max(length, uniform.name.size() + 1U);
        *output = static_cast<GLint>(length);
    } else if (pname == GL_ACTIVE_ATTRIBUTES) *output = static_cast<GLint>(found->second.attributeLocations.size());
    else if (pname == GL_ACTIVE_ATTRIBUTE_MAX_LENGTH) {
        std::size_t length = 0;
        for (const auto& [attribute, location] : found->second.attributeLocations) {
            (void)location; length = std::max(length, attribute.size() + 1U);
        }
        *output = static_cast<GLint>(length);
    }
    else { setError(GL_INVALID_ENUM); *output = 0; }
}

void DirectGlContext::getProgramInfoLog(GLuint name, GLsizei capacity, GLsizei* length, GLchar* output) {
    std::lock_guard lock(share_->mutex_);
    auto found = share_->programs_.find(name);
    if (found == share_->programs_.end()) { setError(GL_INVALID_VALUE); return; }
    copyLog(found->second.log, capacity, length, output);
}

void DirectGlContext::getAttachedShaders(GLuint name, GLsizei capacity, GLsizei* count, GLuint* output) {
    if (capacity < 0 || (capacity != 0 && output == nullptr)) { setError(GL_INVALID_VALUE); return; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->programs_.find(name);
    if (found == share_->programs_.end()) { setError(GL_INVALID_VALUE); return; }
    const GLsizei written = std::min(capacity, static_cast<GLsizei>(found->second.attachedShaders.size()));
    std::copy_n(found->second.attachedShaders.begin(), written, output);
    if (count != nullptr) *count = written;
}

GLenum DirectGlContext::reflectedGlType(shader::ScalarKind scalar, std::uint32_t columns,
                                        std::uint32_t rows) noexcept {
    if (scalar == shader::ScalarKind::sampler) return GL_SAMPLER_2D;
    if (columns > 1U) {
        if (columns == 2U && rows == 2U) return GL_FLOAT_MAT2;
        if (columns == 3U && rows == 3U) return GL_FLOAT_MAT3;
        if (columns == 4U && rows == 4U) return GL_FLOAT_MAT4;
        if (columns == 2U && rows == 3U) return GL_FLOAT_MAT2x3;
        if (columns == 2U && rows == 4U) return GL_FLOAT_MAT2x4;
        if (columns == 3U && rows == 2U) return GL_FLOAT_MAT3x2;
        if (columns == 3U && rows == 4U) return GL_FLOAT_MAT3x4;
        if (columns == 4U && rows == 2U) return GL_FLOAT_MAT4x2;
        if (columns == 4U && rows == 3U) return GL_FLOAT_MAT4x3;
    }
    if (scalar == shader::ScalarKind::floating)
        return rows == 1U ? GL_FLOAT : rows == 2U ? GL_FLOAT_VEC2 : rows == 3U ? GL_FLOAT_VEC3 : GL_FLOAT_VEC4;
    if (scalar == shader::ScalarKind::signedInteger)
        return rows == 1U ? GL_INT : rows == 2U ? GL_INT_VEC2 : rows == 3U ? GL_INT_VEC3 : GL_INT_VEC4;
    if (scalar == shader::ScalarKind::unsignedInteger)
        return rows == 1U ? GL_UNSIGNED_INT : rows == 2U ? GL_UNSIGNED_INT_VEC2 :
            rows == 3U ? GL_UNSIGNED_INT_VEC3 : GL_UNSIGNED_INT_VEC4;
    return rows == 1U ? GL_BOOL : rows == 2U ? GL_BOOL_VEC2 : rows == 3U ? GL_BOOL_VEC3 : GL_BOOL_VEC4;
}

void DirectGlContext::getActiveUniform(GLuint program, GLuint index, GLsizei capacity,
                                       GLsizei* length, GLint* size, GLenum* type, GLchar* output) {
    if (capacity < 0 || size == nullptr || type == nullptr) { setError(GL_INVALID_VALUE); return; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->programs_.find(program);
    if (found == share_->programs_.end()) { setError(GL_INVALID_VALUE); return; }
    if (index >= found->second.uniforms.size()) { setError(GL_INVALID_VALUE); return; }
    const auto& uniform = found->second.uniforms[index];
    *size = static_cast<GLint>(uniform.arraySize);
    *type = reflectedGlType(uniform.scalar, uniform.columns, uniform.vectorSize);
    copyLog(uniform.name, capacity, length, output);
}

void DirectGlContext::getActiveAttrib(GLuint program, GLuint index, GLsizei capacity,
                                      GLsizei* length, GLint* size, GLenum* type, GLchar* output) {
    if (capacity < 0 || size == nullptr || type == nullptr) { setError(GL_INVALID_VALUE); return; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->programs_.find(program);
    if (found == share_->programs_.end()) { setError(GL_INVALID_VALUE); return; }
    if (index >= found->second.attributes.size()) { setError(GL_INVALID_VALUE); return; }
    const auto& attribute = found->second.attributes[index];
    *size = static_cast<GLint>(attribute.arraySize);
    *type = reflectedGlType(attribute.scalar, attribute.columns, attribute.vectorSize);
    copyLog(attribute.name, capacity, length, output);
}

void DirectGlContext::getUniform(GLuint program, GLint location, shader::ScalarKind scalar,
                                 void* output) {
    if (output == nullptr) { setError(GL_INVALID_VALUE); return; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->programs_.find(program);
    if (found == share_->programs_.end()) { setError(GL_INVALID_VALUE); return; }
    if (location < 0 || static_cast<std::size_t>(location) >= found->second.locations.size()) {
        setError(GL_INVALID_OPERATION); return;
    }
    const auto [uniformIndex, element] = found->second.locations[static_cast<std::size_t>(location)];
    const auto& uniform = found->second.uniforms[uniformIndex];
    if (uniform.scalar == scalar || (uniform.scalar == shader::ScalarKind::sampler &&
        scalar == shader::ScalarKind::signedInteger)) {
        std::memcpy(output, uniform.value.data() + element * uniform.elementBytes, uniform.elementBytes);
        return;
    }
    setError(GL_INVALID_OPERATION);
}

GLint DirectGlContext::getUniformLocation(GLuint program, const GLchar* name) {
    if (name == nullptr) { setError(GL_INVALID_VALUE); return -1; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->programs_.find(program);
    if (found == share_->programs_.end()) { setError(GL_INVALID_VALUE); return -1; }
    if (!found->second.linked) { setError(GL_INVALID_OPERATION); return -1; }
    auto location = found->second.uniformLocations.find(name);
    return location != found->second.uniformLocations.end() ? location->second : -1;
}

void DirectGlContext::setUniform(GLint location, GLsizei count, shader::ScalarKind scalar,
                                 std::uint32_t columns, std::uint32_t rows,
                                 GLboolean transpose, const void* values) {
    if (location == -1) return;
    if (count < 0 || (count != 0 && values == nullptr)) { setError(GL_INVALID_VALUE); return; }
    if (transpose != GL_FALSE && columns > 1U) { setError(GL_INVALID_VALUE); return; }
    std::lock_guard lock(share_->mutex_);
    auto program = share_->programs_.find(currentProgram_);
    if (program == share_->programs_.end() || !program->second.linked) {
        setError(GL_INVALID_OPERATION); return;
    }
    if (location < 0 || static_cast<std::size_t>(location) >= program->second.locations.size()) {
        setError(GL_INVALID_OPERATION); return;
    }
    const auto [uniformIndex, firstElement] = program->second.locations[static_cast<std::size_t>(location)];
    ProgramUniform& uniform = program->second.uniforms[uniformIndex];
    const bool samplerCompatible = uniform.scalar == shader::ScalarKind::sampler &&
        scalar == shader::ScalarKind::signedInteger && columns == 1U && rows == 1U;
    const bool booleanCompatible = uniform.scalar == shader::ScalarKind::boolean &&
        scalar == shader::ScalarKind::signedInteger && columns == 1U;
    if (!samplerCompatible && !booleanCompatible && (uniform.scalar != scalar || uniform.columns != columns ||
        uniform.vectorSize != rows)) {
        setError(GL_INVALID_OPERATION); return;
    }
    if (static_cast<std::uint32_t>(count) > uniform.arraySize - firstElement) {
        setError(GL_INVALID_OPERATION); return;
    }
    const std::size_t sourceElementBytes = sizeof(std::uint32_t) * columns * rows;
    for (GLsizei element = 0; element < count; ++element) {
        std::byte* destination = uniform.value.data() +
            (firstElement + static_cast<std::uint32_t>(element)) * uniform.elementBytes;
        const std::byte* source = static_cast<const std::byte*>(values) +
            static_cast<std::size_t>(element) * sourceElementBytes;
        if (booleanCompatible) {
            const auto* integers = reinterpret_cast<const GLint*>(source);
            for (std::uint32_t row = 0; row < rows; ++row)
                destination[row] = integers[row] == 0 ? std::byte{0} : std::byte{1};
        } else if (rows == 3U) {
            const std::size_t sourceColumnBytes = sizeof(std::uint32_t) * rows;
            const std::size_t destinationColumnBytes = sizeof(std::uint32_t) * 4U;
            for (std::uint32_t column = 0; column < columns; ++column) {
                std::byte* destinationColumn = destination + column * destinationColumnBytes;
                const std::byte* sourceColumn = source + column * sourceColumnBytes;
                std::memcpy(destinationColumn, sourceColumn, sourceColumnBytes);
                std::memset(destinationColumn + sourceColumnBytes, 0,
                    destinationColumnBytes - sourceColumnBytes);
            }
        } else std::memcpy(destination, source, sourceElementBytes);
    }
}

bool DirectGlContext::textureFormat(GLint internalFormat, backend::PixelFormat& output) noexcept {
    if (internalFormat == GL_RGBA || internalFormat == GL_RGBA8 || internalFormat == GL_RGB ||
        internalFormat == GL_RGB8) output = backend::PixelFormat::rgba8Unorm;
    else if (internalFormat == GL_DEPTH_COMPONENT || internalFormat == GL_DEPTH_COMPONENT16 ||
             internalFormat == GL_DEPTH_COMPONENT24 || internalFormat == GL_DEPTH_COMPONENT32 ||
             internalFormat == GL_DEPTH_COMPONENT32F) output = backend::PixelFormat::depth32Float;
    else if (internalFormat == GL_DEPTH24_STENCIL8 || internalFormat == GL_DEPTH32F_STENCIL8)
        output = backend::PixelFormat::depth32FloatStencil8;
    else return false;
    return true;
}

void DirectGlContext::genTextures(GLsizei count, GLuint* output) {
    if (count < 0 || (count != 0 && output == nullptr)) { setError(GL_INVALID_VALUE); return; }
    std::lock_guard lock(share_->mutex_);
    for (GLsizei index = 0; index < count; ++index) {
        const GLuint name = share_->allocateName();
        share_->textures_.emplace(name, TextureObject{});
        output[index] = name;
    }
}

void DirectGlContext::deleteTextures(GLsizei count, const GLuint* names) {
    if (count < 0 || (count != 0 && names == nullptr)) { setError(GL_INVALID_VALUE); return; }
    std::lock_guard lock(share_->mutex_);
    for (GLsizei index = 0; index < count; ++index) {
        auto found = share_->textures_.find(names[index]);
        if (found == share_->textures_.end()) continue;
        if (found->second.sampler.valid()) (void)session_->release(found->second.sampler);
        if (found->second.handle.valid()) (void)session_->release(found->second.handle);
        share_->textures_.erase(found);
        for (GLuint& binding : texture2D_) if (binding == names[index]) binding = 0;
    }
}

void DirectGlContext::activeTexture(GLenum texture) {
    if (texture < GL_TEXTURE0 || texture >= GL_TEXTURE0 + kMaxTextureUnits) {
        setError(GL_INVALID_ENUM); return;
    }
    activeTextureUnit_ = texture - GL_TEXTURE0;
}

void DirectGlContext::bindTexture(GLenum target, GLuint name) {
    if (target != GL_TEXTURE_2D) { setError(GL_INVALID_ENUM); return; }
    std::lock_guard lock(share_->mutex_);
    if (name != 0 && !share_->textures_.contains(name)) share_->textures_.emplace(name, TextureObject{});
    texture2D_[activeTextureUnit_] = name;
}

void DirectGlContext::texImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width,
                                 GLsizei height, GLint border, GLenum format, GLenum type,
                                 const void* pixels) {
    if (target != GL_TEXTURE_2D) { setError(GL_INVALID_ENUM); return; }
    if (level < 0 || border != 0 || width < 0 || height < 0) { setError(GL_INVALID_VALUE); return; }
    const GLuint textureName = boundTexture2D();
    if (textureName == 0) { setError(GL_INVALID_OPERATION); return; }
    backend::PixelFormat pixelFormat;
    if (!textureFormat(internalFormat, pixelFormat) ||
        !textureTransferFormat(pixelFormat, format, type)) {
        setError(GL_INVALID_ENUM); return;
    }
    if (pixels != nullptr && stencilFormat(pixelFormat)) {
        setError(GL_INVALID_OPERATION); return;
    }
    if (level != 0) {
        std::lock_guard lock(share_->mutex_);
        auto found = share_->textures_.find(textureName);
        if (found == share_->textures_.end() || !found->second.handle.valid() ||
            found->second.internalFormat != internalFormat || level >= found->second.levels ||
            width != std::max(1, found->second.width >> level) ||
            height != std::max(1, found->second.height >> level)) {
            setError(GL_INVALID_OPERATION); return;
        }
        if (pixels == nullptr) return;
    }
    if (level != 0) {
        texSubImage2D(target, level, 0, 0, width, height, format, type, pixels);
        return;
    }
    if (width == 0 || height == 0) {
        core::TextureHandle previous;
        {
            std::lock_guard lock(share_->mutex_);
            auto& texture = share_->textures_[textureName];
            previous = texture.handle;
            texture.handle = {};
            texture.width = width;
            texture.height = height;
            texture.internalFormat = internalFormat;
            texture.levels = 1;
        }
        if (previous.valid()) (void)session_->release(previous);
        return;
    }
    std::uint16_t mipLevels = 1;
    for (GLsizei extent = std::max(width, height); extent > 1; extent >>= 1) ++mipLevels;
    auto created = session_->createTexture({static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height), mipLevels, pixelFormat});
    if (!created) { backendError(created.error()); return; }

    if (pixels != nullptr) {
        const std::size_t pixelCount = static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height);
        std::vector<std::byte> rgbaPixels;
        std::vector<float> depthPixels;
        std::span<const std::byte> uploadBytes;
        if (pixelFormat == backend::PixelFormat::rgba8Unorm && format == GL_RGB) {
            const auto* source = static_cast<const std::byte*>(pixels);
            rgbaPixels.resize(pixelCount * 4U);
            for (std::size_t index = 0; index < pixelCount; ++index) {
                rgbaPixels[index * 4U] = source[index * 3U];
                rgbaPixels[index * 4U + 1U] = source[index * 3U + 1U];
                rgbaPixels[index * 4U + 2U] = source[index * 3U + 2U];
                rgbaPixels[index * 4U + 3U] = std::byte{0xff};
            }
            uploadBytes = rgbaPixels;
        } else if (pixelFormat == backend::PixelFormat::depth32Float && type != GL_FLOAT) {
            depthPixels.resize(pixelCount);
            if (type == GL_UNSIGNED_BYTE) {
                const auto* source = static_cast<const GLubyte*>(pixels);
                const float scale = 1.0F / std::numeric_limits<GLubyte>::max();
                for (std::size_t index = 0; index < pixelCount; ++index)
                    depthPixels[index] = static_cast<float>(source[index]) * scale;
            } else if (type == GL_UNSIGNED_SHORT) {
                const auto* source = static_cast<const GLushort*>(pixels);
                const float scale = 1.0F / std::numeric_limits<GLushort>::max();
                for (std::size_t index = 0; index < pixelCount; ++index)
                    depthPixels[index] = static_cast<float>(source[index]) * scale;
            } else {
                const auto* source = static_cast<const GLuint*>(pixels);
                const double scale = 1.0 / std::numeric_limits<GLuint>::max();
                for (std::size_t index = 0; index < pixelCount; ++index)
                    depthPixels[index] = static_cast<float>(static_cast<double>(source[index]) * scale);
            }
            uploadBytes = std::as_bytes(std::span<const float>(depthPixels));
        } else {
            uploadBytes = {static_cast<const std::byte*>(pixels), pixelCount * 4U};
        }
        auto uploaded = session_->upload(created.value(), 0, 0, 0,
            static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
            uploadBytes);
        if (!uploaded) {
            (void)session_->release(created.value());
            backendError(uploaded.error());
            return;
        }
    }

    core::TextureHandle previous;
    {
        std::lock_guard lock(share_->mutex_);
        auto& texture = share_->textures_[textureName];
        previous = texture.handle;
        texture.handle = created.value();
        texture.width = width;
        texture.height = height;
        texture.internalFormat = internalFormat;
        texture.levels = mipLevels;
    }
    if (previous.valid()) (void)session_->release(previous);
}

void DirectGlContext::texSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width,
                                    GLsizei height, GLenum format, GLenum type, const void* pixels) {
    if (target != GL_TEXTURE_2D) { setError(GL_INVALID_ENUM); return; }
    if (level < 0 || x < 0 || y < 0 || width < 0 || height < 0 ||
        ((width != 0 && height != 0) && pixels == nullptr)) { setError(GL_INVALID_VALUE); return; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->textures_.find(boundTexture2D());
    if (found == share_->textures_.end() || !found->second.handle.valid()) {
        setError(GL_INVALID_OPERATION); return;
    }
    backend::PixelFormat pixelFormat;
    if (!textureFormat(found->second.internalFormat, pixelFormat) ||
        !textureTransferFormat(pixelFormat, format, type) || stencilFormat(pixelFormat)) {
        setError(GL_INVALID_ENUM); return;
    }
    const std::size_t pixelCount = static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height);
    std::vector<std::byte> rgbaPixels;
    std::vector<float> depthPixels;
    std::span<const std::byte> uploadBytes;
    if (pixelFormat == backend::PixelFormat::rgba8Unorm && format == GL_RGB) {
        const auto* source = static_cast<const std::byte*>(pixels);
        rgbaPixels.resize(pixelCount * 4U);
        for (std::size_t index = 0; index < pixelCount; ++index) {
            rgbaPixels[index * 4U] = source[index * 3U];
            rgbaPixels[index * 4U + 1U] = source[index * 3U + 1U];
            rgbaPixels[index * 4U + 2U] = source[index * 3U + 2U];
            rgbaPixels[index * 4U + 3U] = std::byte{0xff};
        }
        uploadBytes = rgbaPixels;
    } else if (pixelFormat == backend::PixelFormat::depth32Float && type != GL_FLOAT) {
        depthPixels.resize(pixelCount);
        if (type == GL_UNSIGNED_BYTE) {
            const auto* source = static_cast<const GLubyte*>(pixels);
            const float scale = 1.0F / std::numeric_limits<GLubyte>::max();
            for (std::size_t index = 0; index < pixelCount; ++index)
                depthPixels[index] = static_cast<float>(source[index]) * scale;
        } else if (type == GL_UNSIGNED_SHORT) {
            const auto* source = static_cast<const GLushort*>(pixels);
            const float scale = 1.0F / std::numeric_limits<GLushort>::max();
            for (std::size_t index = 0; index < pixelCount; ++index)
                depthPixels[index] = static_cast<float>(source[index]) * scale;
        } else {
            const auto* source = static_cast<const GLuint*>(pixels);
            const double scale = 1.0 / std::numeric_limits<GLuint>::max();
            for (std::size_t index = 0; index < pixelCount; ++index)
                depthPixels[index] = static_cast<float>(static_cast<double>(source[index]) * scale);
        }
        uploadBytes = std::as_bytes(std::span<const float>(depthPixels));
    } else {
        uploadBytes = {static_cast<const std::byte*>(pixels), pixelCount * 4U};
    }
    auto uploaded = session_->upload(found->second.handle, static_cast<std::uint32_t>(level),
        static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
        static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
        uploadBytes);
    if (!uploaded) backendError(uploaded.error());
}

void DirectGlContext::texStorage2D(GLenum target, GLsizei levels, GLenum internalFormat,
                                   GLsizei width, GLsizei height) {
    if (target != GL_TEXTURE_2D) { setError(GL_INVALID_ENUM); return; }
    if (levels <= 0 || width <= 0 || height <= 0) { setError(GL_INVALID_VALUE); return; }
    const GLuint textureName = boundTexture2D();
    if (textureName == 0) { setError(GL_INVALID_OPERATION); return; }
    backend::PixelFormat format;
    if (!textureFormat(static_cast<GLint>(internalFormat), format)) { setError(GL_INVALID_ENUM); return; }
    auto created = session_->createTexture({static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height), static_cast<std::uint16_t>(levels), format});
    if (!created) { backendError(created.error()); return; }
    std::lock_guard lock(share_->mutex_);
    auto& texture = share_->textures_[textureName];
    if (texture.handle.valid()) (void)session_->release(texture.handle);
    texture.handle = created.value();
    texture.width = width;
    texture.height = height;
    texture.internalFormat = static_cast<GLint>(internalFormat);
    texture.levels = levels;
}

void DirectGlContext::texParameteri(GLenum target, GLenum pname, GLint param) {
    if (target != GL_TEXTURE_2D) { setError(GL_INVALID_ENUM); return; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->textures_.find(boundTexture2D());
    if (found == share_->textures_.end()) { setError(GL_INVALID_OPERATION); return; }
    bool samplerChanged = false;
    if (pname == GL_TEXTURE_MIN_FILTER) {
        if (param != GL_NEAREST && param != GL_LINEAR && param != GL_NEAREST_MIPMAP_NEAREST &&
            param != GL_LINEAR_MIPMAP_NEAREST && param != GL_NEAREST_MIPMAP_LINEAR &&
            param != GL_LINEAR_MIPMAP_LINEAR) { setError(GL_INVALID_ENUM); return; }
        found->second.minFilter = param; samplerChanged = true;
    } else if (pname == GL_TEXTURE_MAG_FILTER) {
        if (param != GL_NEAREST && param != GL_LINEAR) { setError(GL_INVALID_ENUM); return; }
        found->second.magFilter = param; samplerChanged = true;
    } else if (pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T) {
        if (param != GL_CLAMP_TO_EDGE && param != GL_REPEAT && param != GL_MIRRORED_REPEAT) {
            setError(GL_INVALID_ENUM); return;
        }
        if (pname == GL_TEXTURE_WRAP_S) found->second.wrapS = param;
        else found->second.wrapT = param;
        samplerChanged = true;
    } else if (pname == GL_TEXTURE_BASE_LEVEL || pname == GL_TEXTURE_MAX_LEVEL) {
        if (param < 0) { setError(GL_INVALID_VALUE); return; }
        if (pname == GL_TEXTURE_BASE_LEVEL) found->second.baseLevel = param;
        else found->second.maxLevel = param;
    } else { setError(GL_INVALID_ENUM); return; }
    if (samplerChanged && found->second.sampler.valid()) {
        (void)session_->release(found->second.sampler);
        found->second.sampler = {};
    }
}

void DirectGlContext::generateMipmap(GLenum target) {
    if (target != GL_TEXTURE_2D) { setError(GL_INVALID_ENUM); return; }
    std::lock_guard lock(share_->mutex_);
    auto found = share_->textures_.find(boundTexture2D());
    if (found == share_->textures_.end() || !found->second.handle.valid()) {
        setError(GL_INVALID_OPERATION); return;
    }
    auto result = session_->generateMipmaps(found->second.handle);
    if (!result) backendError(result.error());
}

DirectGlContext::Framebuffer* DirectGlContext::framebufferForTarget(GLenum target) {
    GLuint name = 0;
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) name = drawFramebuffer_;
    else if (target == GL_READ_FRAMEBUFFER) name = readFramebuffer_;
    else return nullptr;
    auto found = framebuffers_.find(name);
    return found != framebuffers_.end() ? &found->second : nullptr;
}

const DirectGlContext::Framebuffer* DirectGlContext::framebufferForTarget(GLenum target) const {
    return const_cast<DirectGlContext*>(this)->framebufferForTarget(target);
}

core::TextureHandle DirectGlContext::attachmentHandle(const Attachment& attachment) const {
    std::lock_guard lock(share_->mutex_);
    if (attachment.kind == AttachmentKind::texture) {
        auto found = share_->textures_.find(attachment.name);
        return found != share_->textures_.end() ? found->second.handle : core::TextureHandle{};
    }
    if (attachment.kind == AttachmentKind::renderbuffer) {
        auto found = share_->renderbuffers_.find(attachment.name);
        return found != share_->renderbuffers_.end() ? found->second.handle : core::TextureHandle{};
    }
    return {};
}

bool DirectGlContext::attachmentExtent(const Attachment& attachment, GLsizei& width,
                                       GLsizei& height) const {
    std::lock_guard lock(share_->mutex_);
    if (attachment.kind == AttachmentKind::texture) {
        auto found = share_->textures_.find(attachment.name);
        if (found == share_->textures_.end() || !found->second.handle.valid()) return false;
        if (attachment.level < 0 || attachment.level >= found->second.levels) return false;
        width = std::max(1, found->second.width >> attachment.level);
        height = std::max(1, found->second.height >> attachment.level);
        return true;
    }
    if (attachment.kind == AttachmentKind::renderbuffer) {
        auto found = share_->renderbuffers_.find(attachment.name);
        if (found == share_->renderbuffers_.end() || !found->second.handle.valid()) return false;
        width = found->second.width; height = found->second.height; return true;
    }
    return false;
}

backend::PixelFormat DirectGlContext::attachmentFormat(const Attachment& attachment) const {
    std::lock_guard lock(share_->mutex_);
    GLint internalFormat = 0;
    if (attachment.kind == AttachmentKind::texture) {
        auto found = share_->textures_.find(attachment.name);
        if (found != share_->textures_.end()) internalFormat = found->second.internalFormat;
    } else if (attachment.kind == AttachmentKind::renderbuffer) {
        auto found = share_->renderbuffers_.find(attachment.name);
        if (found != share_->renderbuffers_.end()) internalFormat = static_cast<GLint>(found->second.internalFormat);
    }
    backend::PixelFormat result = backend::PixelFormat::none;
    (void)textureFormat(internalFormat, result);
    return result;
}

void DirectGlContext::genFramebuffers(GLsizei count, GLuint* output) {
    if (count < 0 || (count != 0 && output == nullptr)) { setError(GL_INVALID_VALUE); return; }
    for (GLsizei index = 0; index < count; ++index) {
        while (nextFramebuffer_ == 0 || framebuffers_.contains(nextFramebuffer_)) ++nextFramebuffer_;
        output[index] = nextFramebuffer_;
        framebuffers_.emplace(nextFramebuffer_++, Framebuffer{});
    }
}

void DirectGlContext::deleteFramebuffers(GLsizei count, const GLuint* names) {
    if (count < 0 || (count != 0 && names == nullptr)) { setError(GL_INVALID_VALUE); return; }
    for (GLsizei index = 0; index < count; ++index) {
        framebuffers_.erase(names[index]);
        if (drawFramebuffer_ == names[index]) drawFramebuffer_ = 0;
        if (readFramebuffer_ == names[index]) readFramebuffer_ = 0;
    }
}

void DirectGlContext::bindFramebuffer(GLenum target, GLuint name) {
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER) {
        setError(GL_INVALID_ENUM); return;
    }
    if (name != 0 && !framebuffers_.contains(name)) framebuffers_.emplace(name, Framebuffer{});
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) drawFramebuffer_ = name;
    if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER) readFramebuffer_ = name;
}

void DirectGlContext::framebufferTexture2D(GLenum target, GLenum attachment, GLenum textureTarget,
                                           GLuint texture, GLint level) {
    if (textureTarget != GL_TEXTURE_2D) { setError(GL_INVALID_ENUM); return; }
    Framebuffer* framebuffer = framebufferForTarget(target);
    if (framebuffer == nullptr || (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER &&
        target != GL_READ_FRAMEBUFFER)) { setError(target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER ||
        target == GL_READ_FRAMEBUFFER ? GL_INVALID_OPERATION : GL_INVALID_ENUM); return; }
    if (level < 0) { setError(GL_INVALID_VALUE); return; }
    {
        std::lock_guard lock(share_->mutex_);
        if (texture != 0 && !share_->textures_.contains(texture)) { setError(GL_INVALID_OPERATION); return; }
    }
    Attachment value{texture == 0 ? AttachmentKind::none : AttachmentKind::texture, texture, level};
    if (attachment == GL_COLOR_ATTACHMENT0) framebuffer->color = value;
    else if (attachment == GL_DEPTH_ATTACHMENT) framebuffer->depth = value;
    else if (attachment == GL_STENCIL_ATTACHMENT) framebuffer->stencil = value;
    else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) framebuffer->depth = framebuffer->stencil = value;
    else setError(GL_INVALID_ENUM);
}

void DirectGlContext::framebufferRenderbuffer(GLenum target, GLenum attachment,
                                              GLenum renderbufferTarget, GLuint renderbuffer) {
    if (renderbufferTarget != GL_RENDERBUFFER) { setError(GL_INVALID_ENUM); return; }
    Framebuffer* framebuffer = framebufferForTarget(target);
    if (framebuffer == nullptr) { setError(target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER ||
        target == GL_READ_FRAMEBUFFER ? GL_INVALID_OPERATION : GL_INVALID_ENUM); return; }
    {
        std::lock_guard lock(share_->mutex_);
        if (renderbuffer != 0 && !share_->renderbuffers_.contains(renderbuffer)) {
            setError(GL_INVALID_OPERATION); return;
        }
    }
    Attachment value{renderbuffer == 0 ? AttachmentKind::none : AttachmentKind::renderbuffer,
        renderbuffer, 0};
    if (attachment == GL_COLOR_ATTACHMENT0) framebuffer->color = value;
    else if (attachment == GL_DEPTH_ATTACHMENT) framebuffer->depth = value;
    else if (attachment == GL_STENCIL_ATTACHMENT) framebuffer->stencil = value;
    else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) framebuffer->depth = framebuffer->stencil = value;
    else setError(GL_INVALID_ENUM);
}

GLenum DirectGlContext::checkFramebufferStatus(GLenum target) {
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER) {
        setError(GL_INVALID_ENUM); return 0;
    }
    const GLuint name = target == GL_READ_FRAMEBUFFER ? readFramebuffer_ : drawFramebuffer_;
    if (name == 0) return GL_FRAMEBUFFER_COMPLETE;
    const Framebuffer* framebuffer = framebufferForTarget(target);
    if (framebuffer == nullptr) { setError(GL_INVALID_OPERATION); return 0; }
    struct AttachmentRequirement {
        const Attachment* attachment;
        bool (*accepts)(backend::PixelFormat);
    };
    const AttachmentRequirement attachments[] = {
        {&framebuffer->color, colorFormat},
        {&framebuffer->depth, depthFormat},
        {&framebuffer->stencil, stencilFormat},
    };
    GLsizei expectedWidth = 0;
    GLsizei expectedHeight = 0;
    bool attached = false;
    for (const auto& requirement : attachments) {
        const Attachment& attachment = *requirement.attachment;
        if (attachment.kind == AttachmentKind::none) continue;
        GLsizei width = 0;
        GLsizei height = 0;
        if (!attachmentExtent(attachment, width, height) ||
            !requirement.accepts(attachmentFormat(attachment)))
            return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        if (!attached) { expectedWidth = width; expectedHeight = height; attached = true; }
        else if (width != expectedWidth || height != expectedHeight) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    }
    if (!attached) return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    if (framebuffer->depth.kind != AttachmentKind::none &&
        framebuffer->stencil.kind != AttachmentKind::none &&
        (framebuffer->depth.kind != framebuffer->stencil.kind ||
         framebuffer->depth.name != framebuffer->stencil.name ||
         framebuffer->depth.level != framebuffer->stencil.level))
        return GL_FRAMEBUFFER_UNSUPPORTED;
    if (framebuffer->color.kind == AttachmentKind::none &&
        framebuffer->drawBuffer == GL_COLOR_ATTACHMENT0)
        return GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER;
    if (framebuffer->color.kind == AttachmentKind::none &&
        framebuffer->readBuffer == GL_COLOR_ATTACHMENT0)
        return GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER;
    if (framebuffer->drawBuffer != GL_NONE && framebuffer->drawBuffer != GL_COLOR_ATTACHMENT0)
        return GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER;
    if (framebuffer->readBuffer != GL_NONE && framebuffer->readBuffer != GL_COLOR_ATTACHMENT0)
        return GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER;
    return GL_FRAMEBUFFER_COMPLETE;
}

void DirectGlContext::drawBuffer(GLenum mode) {
    Framebuffer* framebuffer = framebufferForTarget(GL_DRAW_FRAMEBUFFER);
    if (drawFramebuffer_ == 0) { if (mode != GL_BACK && mode != GL_NONE) setError(GL_INVALID_ENUM); return; }
    if (framebuffer == nullptr || (mode != GL_COLOR_ATTACHMENT0 && mode != GL_NONE)) {
        setError(GL_INVALID_ENUM); return;
    }
    framebuffer->drawBuffer = mode;
}

void DirectGlContext::readBuffer(GLenum mode) {
    Framebuffer* framebuffer = framebufferForTarget(GL_READ_FRAMEBUFFER);
    if (readFramebuffer_ == 0) { if (mode != GL_BACK && mode != GL_NONE) setError(GL_INVALID_ENUM); return; }
    if (framebuffer == nullptr || (mode != GL_COLOR_ATTACHMENT0 && mode != GL_NONE)) {
        setError(GL_INVALID_ENUM); return;
    }
    framebuffer->readBuffer = mode;
}

void DirectGlContext::drawBuffers(GLsizei count, const GLenum* buffers) {
    if (count < 0 || (count != 0 && buffers == nullptr)) { setError(GL_INVALID_VALUE); return; }
    if (count > 1) { setError(GL_INVALID_VALUE); return; }
    if (count == 1) drawBuffer(buffers[0]);
}

void DirectGlContext::genRenderbuffers(GLsizei count, GLuint* output) {
    if (count < 0 || (count != 0 && output == nullptr)) { setError(GL_INVALID_VALUE); return; }
    std::lock_guard lock(share_->mutex_);
    for (GLsizei index = 0; index < count; ++index) {
        const GLuint name = share_->allocateName();
        share_->renderbuffers_.emplace(name, RenderbufferObject{});
        output[index] = name;
    }
}

void DirectGlContext::deleteRenderbuffers(GLsizei count, const GLuint* names) {
    if (count < 0 || (count != 0 && names == nullptr)) { setError(GL_INVALID_VALUE); return; }
    std::lock_guard lock(share_->mutex_);
    for (GLsizei index = 0; index < count; ++index) {
        auto found = share_->renderbuffers_.find(names[index]);
        if (found == share_->renderbuffers_.end()) continue;
        if (found->second.handle.valid()) (void)session_->release(found->second.handle);
        share_->renderbuffers_.erase(found);
        if (renderbuffer_ == names[index]) renderbuffer_ = 0;
    }
}

void DirectGlContext::bindRenderbuffer(GLenum target, GLuint name) {
    if (target != GL_RENDERBUFFER) { setError(GL_INVALID_ENUM); return; }
    std::lock_guard lock(share_->mutex_);
    if (name != 0 && !share_->renderbuffers_.contains(name)) share_->renderbuffers_.emplace(name, RenderbufferObject{});
    renderbuffer_ = name;
}

void DirectGlContext::renderbufferStorage(GLenum target, GLenum internalFormat,
                                          GLsizei width, GLsizei height) {
    if (target != GL_RENDERBUFFER) { setError(GL_INVALID_ENUM); return; }
    if (width <= 0 || height <= 0) { setError(GL_INVALID_VALUE); return; }
    if (renderbuffer_ == 0) { setError(GL_INVALID_OPERATION); return; }
    backend::PixelFormat format;
    if (!textureFormat(static_cast<GLint>(internalFormat), format)) { setError(GL_INVALID_ENUM); return; }
    auto created = session_->createTexture({static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height), 1, format});
    if (!created) { backendError(created.error()); return; }
    std::lock_guard lock(share_->mutex_);
    auto& renderbuffer = share_->renderbuffers_[renderbuffer_];
    if (renderbuffer.handle.valid()) (void)session_->release(renderbuffer.handle);
    renderbuffer = {created.value(), width, height, internalFormat};
}

void DirectGlContext::viewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (width < 0 || height < 0) { setError(GL_INVALID_VALUE); return; }
    viewport_ = {static_cast<double>(x), static_cast<double>(y), static_cast<double>(width),
        static_cast<double>(height), 0.0, 1.0};
}
void DirectGlContext::depthRange(GLdouble nearValue, GLdouble farValue) noexcept {
    viewport_.nearDepth = std::clamp(nearValue, 0.0, 1.0);
    viewport_.farDepth = std::clamp(farValue, 0.0, 1.0);
}

void DirectGlContext::scissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (width < 0 || height < 0) { setError(GL_INVALID_VALUE); return; }
    scissor_ = {x, y, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
}

void DirectGlContext::enable(GLenum capability, bool enabled) {
    if (capability == GL_DEPTH_TEST) depthTestEnabled_ = enabled;
    else if (capability == GL_BLEND) blendEnabled_ = enabled;
    else if (capability == GL_CULL_FACE) cullEnabled_ = enabled;
    else if (capability == GL_SCISSOR_TEST) scissorEnabled_ = enabled;
    else { setError(GL_INVALID_ENUM); }
}

GLboolean DirectGlContext::isEnabled(GLenum capability) const {
    if (capability == GL_DEPTH_TEST) return depthTestEnabled_ ? GL_TRUE : GL_FALSE;
    if (capability == GL_BLEND) return blendEnabled_ ? GL_TRUE : GL_FALSE;
    if (capability == GL_CULL_FACE) return cullEnabled_ ? GL_TRUE : GL_FALSE;
    if (capability == GL_SCISSOR_TEST) return scissorEnabled_ ? GL_TRUE : GL_FALSE;
    const_cast<DirectGlContext*>(this)->setError(GL_INVALID_ENUM);
    return GL_FALSE;
}

void DirectGlContext::depthFunc(GLenum function) {
    ir::Compare ignored;
    if (!compareFunction(function, ignored)) { setError(GL_INVALID_ENUM); return; }
    depthFunction_ = function;
}

void DirectGlContext::depthMask(GLboolean enabled) {
    if (enabled != GL_FALSE && enabled != GL_TRUE) { setError(GL_INVALID_VALUE); return; }
    depthWrite_ = enabled == GL_TRUE;
}

void DirectGlContext::blendFuncSeparate(GLenum sourceRgb, GLenum destinationRgb,
                                        GLenum sourceAlpha, GLenum destinationAlpha) {
    ir::BlendFactor ignored;
    if (!blendFactor(sourceRgb, ignored) || !blendFactor(destinationRgb, ignored) ||
        !blendFactor(sourceAlpha, ignored) || !blendFactor(destinationAlpha, ignored)) {
        setError(GL_INVALID_ENUM); return;
    }
    sourceRgb_ = sourceRgb; destinationRgb_ = destinationRgb;
    sourceAlpha_ = sourceAlpha; destinationAlpha_ = destinationAlpha;
}

void DirectGlContext::blendEquationSeparate(GLenum rgb, GLenum alpha) {
    ir::BlendOperation ignored;
    if (!blendOperation(rgb, ignored) || !blendOperation(alpha, ignored)) {
        setError(GL_INVALID_ENUM); return;
    }
    rgbEquation_ = rgb; alphaEquation_ = alpha;
}

void DirectGlContext::blendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) noexcept {
    blendColor_ = {std::clamp(red, 0.0F, 1.0F), std::clamp(green, 0.0F, 1.0F),
        std::clamp(blue, 0.0F, 1.0F), std::clamp(alpha, 0.0F, 1.0F)};
}

void DirectGlContext::colorMask(GLboolean red, GLboolean green, GLboolean blue,
                                GLboolean alpha) noexcept {
    colorWriteMask_ = static_cast<std::uint8_t>((red ? 1U : 0U) | (green ? 2U : 0U) |
        (blue ? 4U : 0U) | (alpha ? 8U : 0U));
}

void DirectGlContext::cullFace(GLenum mode) {
    if (mode != GL_FRONT && mode != GL_BACK && mode != GL_FRONT_AND_BACK) {
        setError(GL_INVALID_ENUM); return;
    }
    cullMode_ = mode;
}

void DirectGlContext::frontFace(GLenum winding) {
    if (winding != GL_CW && winding != GL_CCW) { setError(GL_INVALID_ENUM); return; }
    frontWinding_ = winding;
}

void DirectGlContext::clearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) noexcept {
    clearColor_ = {red, green, blue, alpha};
}
void DirectGlContext::clearDepth(GLdouble value) noexcept { clearDepth_ = std::clamp(value, 0.0, 1.0); }
void DirectGlContext::clearStencil(GLint value) noexcept { clearStencil_ = static_cast<std::uint32_t>(value); }

core::ValueResult<backend::Frame> DirectGlContext::acquireRenderFrame() {
    if (drawFramebuffer_ == 0) return egl::bridge::acquireDrawFrame();
    if (checkFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        return core::ValueResult<backend::Frame>::failure(core::Error::make(
            core::ErrorDomain::gl, core::ErrorCode::invalid_state, "draw framebuffer is incomplete"));
    }
    const Framebuffer* framebuffer = framebufferForTarget(GL_DRAW_FRAMEBUFFER);
    GLsizei width = 0;
    GLsizei height = 0;
    if (framebuffer == nullptr || !attachmentExtent(framebuffer->color, width, height)) {
        return core::ValueResult<backend::Frame>::failure(core::Error::make(
            core::ErrorDomain::gl, core::ErrorCode::unsupported,
            "depth-only draw framebuffers are not implemented"));
    }
    return backend::Frame{0, 0, attachmentHandle(framebuffer->color),
        static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
}

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
    auto frame = acquireRenderFrame();
    if (!frame) { backendError(frame.error()); return; }
    auto commands = session_->createCommandEncoder();
    if (!commands) { backendError(commands.error()); return; }
    backend::RenderPassDesc pass;
    pass.color = frame.value().drawable;
    if (drawFramebuffer_ != 0) {
        const Framebuffer* framebuffer = framebufferForTarget(GL_DRAW_FRAMEBUFFER);
        if (framebuffer != nullptr) pass.colorLevel = static_cast<std::uint32_t>(framebuffer->color.level);
    }
    pass.clearColor = (mask & GL_COLOR_BUFFER_BIT) != 0;
    pass.clearRed = clearColor_[0]; pass.clearGreen = clearColor_[1];
    pass.clearBlue = clearColor_[2]; pass.clearAlpha = clearColor_[3];
    if ((mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) != 0) {
        core::ValueResult<core::TextureHandle> depth = core::ValueResult<core::TextureHandle>::failure(
            core::Error::make(core::ErrorDomain::gl, core::ErrorCode::invalid_state,
                "framebuffer has no depth attachment"));
        bool clearDepth = (mask & GL_DEPTH_BUFFER_BIT) != 0;
        bool clearStencil = (mask & GL_STENCIL_BUFFER_BIT) != 0;
        if (drawFramebuffer_ != 0) {
            const Framebuffer* framebuffer = framebufferForTarget(GL_DRAW_FRAMEBUFFER);
            if (framebuffer != nullptr) {
                const Attachment& attachment = framebuffer->depth.kind != AttachmentKind::none
                    ? framebuffer->depth : framebuffer->stencil;
                if (attachment.kind != AttachmentKind::none) depth = attachmentHandle(attachment);
                pass.depthStencilLevel = static_cast<std::uint32_t>(attachment.level);
                clearDepth &= framebuffer->depth.kind != AttachmentKind::none;
                clearStencil &= framebuffer->stencil.kind != AttachmentKind::none;
            }
        } else depth = depthTarget(frame.value().width, frame.value().height);
        if (clearDepth || clearStencil) {
            if (!depth) { backendError(depth.error()); return; }
            pass.depthStencil = depth.value();
        }
        pass.clearDepth = clearDepth;
        pass.clearDepthValue = clearDepth_;
        pass.clearStencil = clearStencil;
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
    backend::PipelineDesc desc;
    desc.key.colorFormats[0] = backend::PixelFormat::bgra8Unorm;
    desc.key.depthStencilFormat = backend::PixelFormat::depth32FloatStencil8;
    if (drawFramebuffer_ != 0) {
        desc.key.depthStencilFormat = backend::PixelFormat::none;
        const Framebuffer* framebuffer = framebufferForTarget(GL_DRAW_FRAMEBUFFER);
        if (framebuffer != nullptr) {
            desc.key.colorFormats[0] = attachmentFormat(framebuffer->color);
            desc.key.colorAttachmentCount = desc.key.colorFormats[0] == backend::PixelFormat::none ? 0 : 1;
            const Attachment& depthStencil = framebuffer->depth.kind != AttachmentKind::none
                ? framebuffer->depth : framebuffer->stencil;
            if (depthStencil.kind != AttachmentKind::none)
                desc.key.depthStencilFormat = attachmentFormat(depthStencil);
        }
    }
    std::vector<core::ShaderHandle> shaders;
    {
        std::lock_guard lock(share_->mutex_);
        auto program = share_->programs_.find(currentProgram_);
        if (program == share_->programs_.end() || !program->second.linked) {
            return core::ValueResult<core::PipelineHandle>::failure(core::Error::make(
                core::ErrorDomain::gl, core::ErrorCode::invalid_state,
                "no linked GL program is active"));
        }
        for (GLuint name : program->second.linkedShaders) {
            auto found = share_->shaders_.find(name);
            if (found != share_->shaders_.end() && found->second.compiled)
                shaders.push_back(found->second.handle);
        }
    }
    desc.key.primitive = primitiveMode;
    if (!compareFunction(depthTestEnabled_ ? depthFunction_ : GL_ALWAYS, desc.key.depthCompare) ||
        !blendFactor(sourceRgb_, desc.key.sourceRgb) ||
        !blendFactor(destinationRgb_, desc.key.destinationRgb) ||
        !blendFactor(sourceAlpha_, desc.key.sourceAlpha) ||
        !blendFactor(destinationAlpha_, desc.key.destinationAlpha) ||
        !blendOperation(rgbEquation_, desc.key.rgbOperation) ||
        !blendOperation(alphaEquation_, desc.key.alphaOperation)) {
        return core::ValueResult<core::PipelineHandle>::failure(core::Error::make(
            core::ErrorDomain::gl, core::ErrorCode::invalid_state, "invalid GL pipeline state"));
    }
    desc.key.depthWrite = depthTestEnabled_ && depthWrite_;
    desc.key.blending = blendEnabled_;
    desc.key.colorWriteMask = colorWriteMask_;
    for (std::uint32_t location = 0; location < vao.attributes.size(); ++location) {
        const auto& attribute = vao.attributes[location];
        if (!attribute.enabled) continue;
        backend::VertexScalar scalar;
        if (!vertexScalar(attribute.type, scalar)) return core::ValueResult<core::PipelineHandle>::failure(
            core::Error::make(core::ErrorDomain::gl, core::ErrorCode::unsupported, "unsupported GL vertex type"));
        const std::uint32_t scalarSize = attribute.type == GL_BYTE || attribute.type == GL_UNSIGNED_BYTE ? 1U :
            attribute.type == GL_SHORT || attribute.type == GL_UNSIGNED_SHORT ? 2U : 4U;
        std::uint32_t metalLocation = location;
        {
            std::lock_guard lock(share_->mutex_);
            auto program = share_->programs_.find(currentProgram_);
            if (program != share_->programs_.end()) {
                auto mapped = program->second.metalAttributeLocations.find(location);
                if (mapped != program->second.metalAttributeLocations.end()) metalLocation = mapped->second;
            }
        }
        desc.vertexAttributes.push_back({metalLocation, location, 0,
            static_cast<std::uint32_t>(attribute.stride != 0 ? attribute.stride : attribute.size * scalarSize),
            scalar, static_cast<std::uint8_t>(attribute.size), attribute.divisor, attribute.normalized});
    }
    return session_->createPipeline(shaders, desc);
}

core::ValueResult<core::SamplerHandle> DirectGlContext::samplerFor(TextureObject& texture) {
    if (texture.sampler.valid()) return texture.sampler;
    backend::SamplerDesc desc;
    desc.minFilter = texture.minFilter == GL_NEAREST ||
        texture.minFilter == GL_NEAREST_MIPMAP_NEAREST ||
        texture.minFilter == GL_NEAREST_MIPMAP_LINEAR
        ? backend::Filter::nearest : backend::Filter::linear;
    desc.magFilter = texture.magFilter == GL_NEAREST ? backend::Filter::nearest : backend::Filter::linear;
    if (texture.minFilter == GL_NEAREST_MIPMAP_NEAREST || texture.minFilter == GL_LINEAR_MIPMAP_NEAREST)
        desc.mipFilter = backend::MipFilter::nearest;
    else if (texture.minFilter == GL_NEAREST_MIPMAP_LINEAR || texture.minFilter == GL_LINEAR_MIPMAP_LINEAR)
        desc.mipFilter = backend::MipFilter::linear;
    desc.lodMinClamp = static_cast<float>(texture.baseLevel);
    desc.lodMaxClamp = static_cast<float>(std::min(texture.maxLevel, std::max(0, texture.levels - 1)));
    const auto address = [](GLint mode) {
        if (mode == GL_REPEAT) return backend::AddressMode::repeat;
        if (mode == GL_MIRRORED_REPEAT) return backend::AddressMode::mirroredRepeat;
        return backend::AddressMode::clampToEdge;
    };
    desc.addressU = address(texture.wrapS);
    desc.addressV = address(texture.wrapT);
    auto created = session_->createSampler(desc);
    if (created) texture.sampler = created.value();
    return created;
}

core::Result DirectGlContext::bindProgramResources(backend::CommandEncoder& commands,
                                                   ProgramObject& program) {
    for (const auto& block : program.uniformBlocks) {
        const IndexedBufferBinding& indexed = uniformBuffers_[block.binding];
        auto buffer = share_->buffers_.find(indexed.name);
        if (indexed.name == 0 || buffer == share_->buffers_.end() || !buffer->second.handle.valid()) {
            return core::Result::failure(core::Error::make(core::ErrorDomain::gl,
                core::ErrorCode::invalid_state, "uniform block has no buffer binding"));
        }
        for (const auto& stage : block.stages) {
            auto result = commands.bindBuffer(renderStage(stage.stage), stage.mslBuffer,
                buffer->second.handle, static_cast<std::size_t>(indexed.offset));
            if (!result) return result;
        }
    }
    for (auto& uniform : program.uniforms) {
        for (const auto& binding : uniform.bindings) {
            if (uniform.scalar == shader::ScalarKind::sampler) {
                for (std::uint32_t element = 0; element < uniform.arraySize; ++element) {
                    GLint unit = 0;
                    std::memcpy(&unit, uniform.value.data() + element * uniform.elementBytes, sizeof(unit));
                    if (unit < 0 || unit >= static_cast<GLint>(texture2D_.size())) {
                        return core::Result::failure(core::Error::make(core::ErrorDomain::gl,
                            core::ErrorCode::invalid_argument, "sampler uniform selects an invalid texture unit"));
                    }
                    auto texture = share_->textures_.find(texture2D_[static_cast<std::size_t>(unit)]);
                    if (texture == share_->textures_.end() || !texture->second.handle.valid()) {
                        return core::Result::failure(core::Error::make(core::ErrorDomain::gl,
                            core::ErrorCode::invalid_state, "sampler uniform has no complete texture"));
                    }
                    auto sampler = samplerFor(texture->second);
                    if (!sampler) return core::Result::failure(sampler.error());
                    auto result = commands.bindTexture(renderStage(binding.stage),
                        binding.mslTexture + element, binding.mslSampler + element,
                        texture->second.handle, sampler.value());
                    if (!result) return result;
                }
            } else {
                auto result = commands.bindBytes(renderStage(binding.stage), binding.mslBuffer,
                    std::span<const std::byte>(uniform.value));
                if (!result) return result;
            }
        }
    }
    return {};
}

core::Result DirectGlContext::applyDynamicState(backend::CommandEncoder& commands,
                                                std::uint32_t width, std::uint32_t height) {
    backend::ScissorRect scissor = scissorEnabled_ ? scissor_ :
        backend::ScissorRect{0, 0, width, height};
    const std::uint32_t glX = std::min(width, static_cast<std::uint32_t>(std::max(0, scissor.x)));
    const std::uint32_t glY = std::min(height, static_cast<std::uint32_t>(std::max(0, scissor.y)));
    scissor.width = std::min(scissor.width, width - glX);
    scissor.height = std::min(scissor.height, height - glY);
    scissor.x = static_cast<std::int32_t>(glX);
    scissor.y = static_cast<std::int32_t>(height - glY - scissor.height);
    core::Result result = commands.setScissor(scissor);
    backend::CullMode cull = backend::CullMode::none;
    if (cullEnabled_ && cullMode_ == GL_FRONT) cull = backend::CullMode::front;
    else if (cullEnabled_ && cullMode_ == GL_BACK) cull = backend::CullMode::back;
    else if (cullEnabled_ && cullMode_ == GL_FRONT_AND_BACK) cull = backend::CullMode::frontAndBack;
    if (result) result = commands.setCullState(cull, frontWinding_ == GL_CCW
        ? backend::FrontFace::clockwise : backend::FrontFace::counterClockwise);
    if (result) result = commands.setBlendColor(
        blendColor_[0], blendColor_[1], blendColor_[2], blendColor_[3]);
    return result;
}

void DirectGlContext::drawArrays(GLenum mode, GLint first, GLsizei count, GLsizei instances) {
    drawArrays(mode, first, count, instances, 0);
}

void DirectGlContext::drawArrays(GLenum mode, GLint first, GLsizei count, GLsizei instances,
                                 GLuint baseInstance) {
    if (first < 0 || count < 0 || instances < 0) { setError(GL_INVALID_VALUE); return; }
    if (count == 0 || instances == 0) return;
    auto* vao = currentVao();
    if (vao == nullptr || currentProgram_ == 0) { setError(GL_INVALID_OPERATION); return; }
    auto frame = acquireRenderFrame();
    if (!frame) { backendError(frame.error()); return; }
    if (drawFramebuffer_ == 0) {
        auto depth = depthTarget(frame.value().width, frame.value().height);
        if (!depth) { backendError(depth.error()); return; }
    }
    auto pipeline = pipelineFor(mode, *vao);
    if (!pipeline) { backendError(pipeline.error()); return; }
    auto commands = session_->createCommandEncoder();
    if (!commands) { backendError(commands.error()); return; }
    backend::RenderPassDesc pass{frame.value().drawable, depthTexture_};
    if (drawFramebuffer_ != 0) {
        const Framebuffer* framebuffer = framebufferForTarget(GL_DRAW_FRAMEBUFFER);
        const Attachment& depthStencil = framebuffer != nullptr &&
            framebuffer->depth.kind != AttachmentKind::none ? framebuffer->depth : framebuffer->stencil;
        pass.depthStencil = framebuffer != nullptr
            ? attachmentHandle(depthStencil) : core::TextureHandle{};
        if (framebuffer != nullptr) {
            pass.colorLevel = static_cast<std::uint32_t>(framebuffer->color.level);
            pass.depthStencilLevel = static_cast<std::uint32_t>(depthStencil.level);
        }
    }
    core::Result result = commands.value()->beginRenderPass(pass);
    if (result) result = commands.value()->bindPipeline(pipeline.value());
    backend::Viewport viewport = viewport_;
    if (viewport.width == 0 || viewport.height == 0) {
        viewport.width = frame.value().width; viewport.height = frame.value().height;
    }
    viewport.y = static_cast<double>(frame.value().height) - viewport.y - viewport.height;
    if (result) result = commands.value()->setViewport(viewport);
    if (result) result = applyDynamicState(*commands.value(), frame.value().width, frame.value().height);
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
        auto program = share_->programs_.find(currentProgram_);
        if (result && program != share_->programs_.end())
            result = bindProgramResources(*commands.value(), program->second);
    }
    if (result) result = commands.value()->draw({static_cast<std::uint32_t>(first),
        static_cast<std::uint32_t>(count), static_cast<std::uint32_t>(instances), baseInstance});
    if (result) result = commands.value()->endRenderPass();
    if (result) result = commands.value()->commit();
    if (!result) backendError(result.error());
    (void)session_->release(pipeline.value());
}

void DirectGlContext::drawElements(GLenum mode, GLsizei count, GLenum type, const void* indices,
                                   GLsizei instances, GLint baseVertex, GLuint baseInstance) {
    if (count < 0 || instances < 0) { setError(GL_INVALID_VALUE); return; }
    if (count == 0 || instances == 0) return;
    if (type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT) { setError(GL_INVALID_ENUM); return; }
    auto* vao = currentVao();
    if (vao == nullptr || vao->elementBuffer == 0 || currentProgram_ == 0) {
        setError(GL_INVALID_OPERATION); return;
    }
    auto frame = acquireRenderFrame();
    if (!frame) { backendError(frame.error()); return; }
    if (drawFramebuffer_ == 0) {
        auto depth = depthTarget(frame.value().width, frame.value().height);
        if (!depth) { backendError(depth.error()); return; }
    }
    auto pipeline = pipelineFor(mode, *vao);
    if (!pipeline) { backendError(pipeline.error()); return; }
    auto commands = session_->createCommandEncoder();
    if (!commands) { backendError(commands.error()); return; }
    backend::RenderPassDesc pass{frame.value().drawable, depthTexture_};
    if (drawFramebuffer_ != 0) {
        const Framebuffer* framebuffer = framebufferForTarget(GL_DRAW_FRAMEBUFFER);
        const Attachment& depthStencil = framebuffer != nullptr &&
            framebuffer->depth.kind != AttachmentKind::none ? framebuffer->depth : framebuffer->stencil;
        pass.depthStencil = framebuffer != nullptr
            ? attachmentHandle(depthStencil) : core::TextureHandle{};
        if (framebuffer != nullptr) {
            pass.colorLevel = static_cast<std::uint32_t>(framebuffer->color.level);
            pass.depthStencilLevel = static_cast<std::uint32_t>(depthStencil.level);
        }
    }
    core::Result result = commands.value()->beginRenderPass(pass);
    if (result) result = commands.value()->bindPipeline(pipeline.value());
    backend::Viewport viewport = viewport_;
    if (viewport.width == 0 || viewport.height == 0) {
        viewport.width = frame.value().width; viewport.height = frame.value().height;
    }
    viewport.y = static_cast<double>(frame.value().height) - viewport.y - viewport.height;
    if (result) result = commands.value()->setViewport(viewport);
    if (result) result = applyDynamicState(*commands.value(), frame.value().width, frame.value().height);
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
        auto program = share_->programs_.find(currentProgram_);
        if (result && program != share_->programs_.end())
            result = bindProgramResources(*commands.value(), program->second);
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

core::ValueResult<std::vector<std::byte>> DirectGlContext::readbackDrawFramebuffer() {
    if (drawFramebuffer_ == 0) return egl::bridge::readbackDrawFrame();
    const Framebuffer* framebuffer = framebufferForTarget(GL_DRAW_FRAMEBUFFER);
    if (framebuffer == nullptr || framebuffer->color.kind == AttachmentKind::none ||
        framebuffer->color.level != 0) {
        return core::ValueResult<std::vector<std::byte>>::failure(core::Error::make(
            core::ErrorDomain::gl, core::ErrorCode::unsupported,
            "framebuffer readback requires a level-zero color attachment"));
    }
    return session_->readbackRgba8(attachmentHandle(framebuffer->color));
}

void DirectGlContext::getIntegerv(GLenum name, GLint* output) {
    if (output == nullptr) { setError(GL_INVALID_VALUE); return; }
    if (name == GL_ARRAY_BUFFER_BINDING) *output = static_cast<GLint>(arrayBuffer_);
    else if (name == GL_ELEMENT_ARRAY_BUFFER_BINDING) *output = static_cast<GLint>(boundBuffer(GL_ELEMENT_ARRAY_BUFFER));
    else if (name == GL_UNIFORM_BUFFER_BINDING) *output = static_cast<GLint>(uniformBuffer_);
    else if (name == GL_VERTEX_ARRAY_BINDING) *output = static_cast<GLint>(currentVao_);
    else if (name == GL_CURRENT_PROGRAM) *output = static_cast<GLint>(currentProgram_);
    else if (name == GL_MAX_VERTEX_ATTRIBS) *output = 16;
    else if (name == GL_MAJOR_VERSION) *output = 3;
    else if (name == GL_MINOR_VERSION) *output = 3;
    else if (name == GL_CONTEXT_PROFILE_MASK) *output = GL_CONTEXT_CORE_PROFILE_BIT;
    else if (name == GL_CONTEXT_FLAGS || name == GL_NUM_EXTENSIONS) *output = 0;
    else if (name == GL_MAX_TEXTURE_SIZE || name == GL_MAX_RENDERBUFFER_SIZE) *output = 8192;
    else if (name == GL_MAX_TEXTURE_IMAGE_UNITS || name == GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS ||
             name == GL_MAX_TEXTURE_UNITS) *output = static_cast<GLint>(texture2D_.size());
    else if (name == GL_MAX_VERTEX_UNIFORM_COMPONENTS || name == GL_MAX_FRAGMENT_UNIFORM_COMPONENTS)
        *output = 1024;
    else if (name == GL_MAX_VERTEX_UNIFORM_BLOCKS || name == GL_MAX_FRAGMENT_UNIFORM_BLOCKS)
        *output = static_cast<GLint>(uniformBuffers_.size());
    else if (name == GL_MAX_UNIFORM_BLOCK_SIZE) *output = 65536;
    else if (name == GL_MAX_COLOR_ATTACHMENTS || name == GL_MAX_DRAW_BUFFERS) *output = 1;
    else if (name == GL_RED_BITS || name == GL_GREEN_BITS || name == GL_BLUE_BITS || name == GL_ALPHA_BITS)
        *output = 8;
    else if (name == GL_DEPTH_BITS) *output = 32;
    else if (name == GL_STENCIL_BITS) *output = 8;
    else if (name == GL_DOUBLEBUFFER) *output = GL_TRUE;
    else if (name == GL_STEREO) *output = GL_FALSE;
    else if (name == GL_MAX_VIEWPORT_DIMS) { output[0] = 16384; output[1] = 16384; }
    else if (name == GL_VIEWPORT) {
        output[0] = static_cast<GLint>(viewport_.x); output[1] = static_cast<GLint>(viewport_.y);
        output[2] = static_cast<GLint>(viewport_.width); output[3] = static_cast<GLint>(viewport_.height);
    } else if (name == GL_SCISSOR_BOX) {
        output[0] = scissor_.x; output[1] = scissor_.y;
        output[2] = static_cast<GLint>(scissor_.width); output[3] = static_cast<GLint>(scissor_.height);
    }
    else if (name == GL_TEXTURE_BINDING_2D) *output = static_cast<GLint>(boundTexture2D());
    else if (name == GL_ACTIVE_TEXTURE) *output = static_cast<GLint>(GL_TEXTURE0 + activeTextureUnit_);
    else if (name == GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS) *output = static_cast<GLint>(texture2D_.size());
    else if (name == GL_DEPTH_FUNC) *output = static_cast<GLint>(depthFunction_);
    else if (name == GL_BLEND_SRC_RGB) *output = static_cast<GLint>(sourceRgb_);
    else if (name == GL_BLEND_DST_RGB) *output = static_cast<GLint>(destinationRgb_);
    else if (name == GL_BLEND_SRC_ALPHA) *output = static_cast<GLint>(sourceAlpha_);
    else if (name == GL_BLEND_DST_ALPHA) *output = static_cast<GLint>(destinationAlpha_);
    else if (name == GL_BLEND_EQUATION_RGB) *output = static_cast<GLint>(rgbEquation_);
    else if (name == GL_BLEND_EQUATION_ALPHA) *output = static_cast<GLint>(alphaEquation_);
    else if (name == GL_CULL_FACE_MODE) *output = static_cast<GLint>(cullMode_);
    else if (name == GL_FRONT_FACE) *output = static_cast<GLint>(frontWinding_);
    else if (name == GL_RENDERBUFFER_BINDING) *output = static_cast<GLint>(renderbuffer_);
    else if (name == GL_FRAMEBUFFER_BINDING || name == GL_DRAW_FRAMEBUFFER_BINDING)
        *output = static_cast<GLint>(drawFramebuffer_);
    else if (name == GL_READ_FRAMEBUFFER_BINDING) *output = static_cast<GLint>(readFramebuffer_);
    else { setError(GL_INVALID_ENUM); *output = 0; }
}

void DirectGlContext::getFloatv(GLenum name, GLfloat* output) {
    if (output == nullptr) { setError(GL_INVALID_VALUE); return; }
    if (name == GL_VIEWPORT) {
        output[0] = static_cast<GLfloat>(viewport_.x); output[1] = static_cast<GLfloat>(viewport_.y);
        output[2] = static_cast<GLfloat>(viewport_.width); output[3] = static_cast<GLfloat>(viewport_.height);
    } else if (name == GL_COLOR_CLEAR_VALUE) std::copy(clearColor_.begin(), clearColor_.end(), output);
    else if (name == GL_BLEND_COLOR) std::copy(blendColor_.begin(), blendColor_.end(), output);
    else if (name == GL_DEPTH_RANGE) { output[0] = static_cast<GLfloat>(viewport_.nearDepth); output[1] = static_cast<GLfloat>(viewport_.farDepth); }
    else if (name == GL_DEPTH_CLEAR_VALUE) *output = static_cast<GLfloat>(clearDepth_);
    else { GLint integer = 0; getIntegerv(name, &integer); *output = static_cast<GLfloat>(integer); }
}

void DirectGlContext::getBooleanv(GLenum name, GLboolean* output) {
    if (output == nullptr) { setError(GL_INVALID_VALUE); return; }
    if (name == GL_COLOR_WRITEMASK) {
        output[0] = (colorWriteMask_ & 1U) != 0 ? GL_TRUE : GL_FALSE;
        output[1] = (colorWriteMask_ & 2U) != 0 ? GL_TRUE : GL_FALSE;
        output[2] = (colorWriteMask_ & 4U) != 0 ? GL_TRUE : GL_FALSE;
        output[3] = (colorWriteMask_ & 8U) != 0 ? GL_TRUE : GL_FALSE;
    } else if (name == GL_DEPTH_WRITEMASK) *output = depthWrite_ ? GL_TRUE : GL_FALSE;
    else if (name == GL_BLEND || name == GL_DEPTH_TEST || name == GL_CULL_FACE || name == GL_SCISSOR_TEST)
        *output = isEnabled(name);
    else { GLint integer = 0; getIntegerv(name, &integer); *output = integer != 0 ? GL_TRUE : GL_FALSE; }
}

void DirectGlContext::getIntegeri(GLenum name, GLuint index, GLint* output) {
    if (output == nullptr) { setError(GL_INVALID_VALUE); return; }
    if (name != GL_UNIFORM_BUFFER_BINDING) { setError(GL_INVALID_ENUM); *output = 0; return; }
    if (index >= uniformBuffers_.size()) { setError(GL_INVALID_VALUE); *output = 0; return; }
    *output = static_cast<GLint>(uniformBuffers_[index].name);
}

void DirectGlContext::pixelStore(GLenum name, GLint value) {
    if (name == GL_PACK_ALIGNMENT || name == GL_UNPACK_ALIGNMENT) {
        if (value != 1 && value != 2 && value != 4 && value != 8) { setError(GL_INVALID_VALUE); return; }
        if (name == GL_PACK_ALIGNMENT) packAlignment_ = value;
        else unpackAlignment_ = value;
    } else if (name == GL_UNPACK_ROW_LENGTH || name == GL_UNPACK_SKIP_ROWS ||
               name == GL_UNPACK_SKIP_PIXELS) {
        if (value < 0) { setError(GL_INVALID_VALUE); return; }
        if (name == GL_UNPACK_ROW_LENGTH) unpackRowLength_ = value;
        else if (name == GL_UNPACK_SKIP_ROWS) unpackSkipRows_ = value;
        else unpackSkipPixels_ = value;
    } else setError(GL_INVALID_ENUM);
}

} // namespace mithril::frontend::gl
