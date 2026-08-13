#pragma once

#include "backend/CommandEncoder.h"
#include "backend/Pipeline.h"
#include "backend/Presenter.h"
#include "core/Handles.h"
#include "shader/ShaderTypes.h"

#include <GL/gl.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mithril::metal { class MetalDeviceSession; }

namespace mithril::frontend::gl {

struct BufferObject final {
    core::BufferHandle handle;
    GLsizeiptr size{};
    GLenum usage{GL_STATIC_DRAW};
    GLintptr mappedOffset{};
    GLsizeiptr mappedLength{};
    GLbitfield mappedAccess{};
    bool mapped{};
};

struct ShaderObject final {
    GLenum type{};
    std::string source;
    std::string log;
    core::ShaderHandle handle;
    std::vector<shader::ReflectedBinding> bindings;
    bool compiled{};
    bool deletePending{};
};

struct ProgramAttribute final {
    std::string name;
    shader::ScalarKind scalar{shader::ScalarKind::floating};
    std::uint32_t vectorSize{4};
    std::uint32_t columns{1};
    std::uint32_t arraySize{1};
    GLuint glLocation{};
    GLuint metalLocation{};
};

struct ProgramUniformBinding final {
    shader::ShaderStage stage{shader::ShaderStage::vertex};
    std::uint32_t mslBuffer{};
    std::uint32_t mslTexture{};
    std::uint32_t mslSampler{};
};

struct ProgramUniform final {
    std::string name;
    shader::ScalarKind scalar{shader::ScalarKind::floating};
    std::uint32_t vectorSize{1};
    std::uint32_t columns{1};
    std::uint32_t arraySize{1};
    std::uint32_t elementBytes{};
    std::vector<std::byte> value;
    std::vector<ProgramUniformBinding> bindings;
};

struct ProgramUniformBlock final {
    std::string name;
    GLuint binding{};
    std::uint32_t byteSize{};
    std::vector<ProgramUniformBinding> stages;
};

struct ProgramObject final {
    std::vector<GLuint> attachedShaders;
    std::vector<GLuint> linkedShaders;
    std::string log;
    std::vector<ProgramUniform> uniforms;
    std::vector<std::pair<std::size_t, std::uint32_t>> locations;
    std::unordered_map<std::string, GLint> uniformLocations;
    std::unordered_map<std::string, GLuint> requestedAttributeLocations;
    std::unordered_map<std::string, GLint> attributeLocations;
    std::unordered_map<GLuint, GLuint> metalAttributeLocations;
    std::vector<ProgramAttribute> attributes;
    std::vector<ProgramUniformBlock> uniformBlocks;
    std::unordered_map<std::string, GLuint> uniformBlockIndices;
    bool linked{};
};

struct TextureObject final {
    core::TextureHandle handle;
    GLsizei width{};
    GLsizei height{};
    GLint internalFormat{GL_RGBA8};
    GLsizei levels{1};
    GLint minFilter{GL_NEAREST_MIPMAP_LINEAR};
    GLint magFilter{GL_LINEAR};
    GLint wrapS{GL_REPEAT};
    GLint wrapT{GL_REPEAT};
    GLint baseLevel{};
    GLint maxLevel{1000};
    core::SamplerHandle sampler;
};

struct RenderbufferObject final {
    core::TextureHandle handle;
    GLsizei width{};
    GLsizei height{};
    GLenum internalFormat{GL_RGBA8};
};

class DirectGlShareGroup final {
public:
    explicit DirectGlShareGroup(std::shared_ptr<metal::MetalDeviceSession>);
    ~DirectGlShareGroup();
    DirectGlShareGroup(const DirectGlShareGroup&) = delete;
    DirectGlShareGroup& operator=(const DirectGlShareGroup&) = delete;

private:
    GLuint allocateName();
    std::shared_ptr<metal::MetalDeviceSession> session_;
    std::mutex mutex_;
    GLuint nextName_{1};
    std::unordered_map<GLuint, BufferObject> buffers_;
    std::unordered_map<GLuint, ShaderObject> shaders_;
    std::unordered_map<GLuint, ProgramObject> programs_;
    std::unordered_map<GLuint, TextureObject> textures_;
    std::unordered_map<GLuint, RenderbufferObject> renderbuffers_;
    friend class DirectGlContext;
};

class DirectGlContext final {
public:
    DirectGlContext(std::shared_ptr<DirectGlShareGroup>,
                    std::shared_ptr<metal::MetalDeviceSession>);
    ~DirectGlContext();
    DirectGlContext(const DirectGlContext&) = delete;
    DirectGlContext& operator=(const DirectGlContext&) = delete;

    [[nodiscard]] const std::shared_ptr<DirectGlShareGroup>& shareGroup() const noexcept;
    void setError(GLenum) noexcept;
    [[nodiscard]] GLenum takeError() noexcept;

    void genBuffers(GLsizei, GLuint*);
    void deleteBuffers(GLsizei, const GLuint*);
    void bindBuffer(GLenum, GLuint);
    void bindBufferBase(GLenum, GLuint, GLuint);
    void bindBufferRange(GLenum, GLuint, GLuint, GLintptr, GLsizeiptr);
    void bufferData(GLenum, GLsizeiptr, const void*, GLenum);
    void bufferSubData(GLenum, GLintptr, GLsizeiptr, const void*);
    [[nodiscard]] void* mapBuffer(GLenum, GLenum);
    [[nodiscard]] void* mapBufferRange(GLenum, GLintptr, GLsizeiptr, GLbitfield);
    [[nodiscard]] GLboolean unmapBuffer(GLenum);
    void flushMappedBufferRange(GLenum, GLintptr, GLsizeiptr);
    void getBufferParameteriv(GLenum, GLenum, GLint*);

    void genVertexArrays(GLsizei, GLuint*);
    void deleteVertexArrays(GLsizei, const GLuint*);
    void bindVertexArray(GLuint);
    void enableVertexAttrib(GLuint, bool);
    void vertexAttribPointer(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*, bool integer);
    void vertexAttribDivisor(GLuint, GLuint);

    [[nodiscard]] GLuint createShader(GLenum);
    void deleteShader(GLuint);
    void shaderSource(GLuint, GLsizei, const GLchar* const*, const GLint*);
    void compileShader(GLuint);
    void getShaderiv(GLuint, GLenum, GLint*);
    void getShaderInfoLog(GLuint, GLsizei, GLsizei*, GLchar*);
    void getShaderSource(GLuint, GLsizei, GLsizei*, GLchar*);

    [[nodiscard]] GLuint createProgram();
    void deleteProgram(GLuint);
    void attachShader(GLuint, GLuint);
    void detachShader(GLuint, GLuint);
    void linkProgram(GLuint);
    void useProgram(GLuint);
    void getProgramiv(GLuint, GLenum, GLint*);
    void getProgramInfoLog(GLuint, GLsizei, GLsizei*, GLchar*);
    void getAttachedShaders(GLuint, GLsizei, GLsizei*, GLuint*);
    void getActiveUniform(GLuint, GLuint, GLsizei, GLsizei*, GLint*, GLenum*, GLchar*);
    void getActiveAttrib(GLuint, GLuint, GLsizei, GLsizei*, GLint*, GLenum*, GLchar*);
    void getUniform(GLuint, GLint, shader::ScalarKind, void*);
    [[nodiscard]] GLuint getUniformBlockIndex(GLuint, const GLchar*);
    void getActiveUniformBlockiv(GLuint, GLuint, GLenum, GLint*);
    void uniformBlockBinding(GLuint, GLuint, GLuint);
    void bindAttribLocation(GLuint, GLuint, const GLchar*);
    [[nodiscard]] GLint getAttribLocation(GLuint, const GLchar*);
    [[nodiscard]] GLint getUniformLocation(GLuint, const GLchar*);
    void setUniform(GLint, GLsizei, shader::ScalarKind, std::uint32_t columns,
                    std::uint32_t rows, GLboolean transpose, const void*);

    void genTextures(GLsizei, GLuint*);
    void deleteTextures(GLsizei, const GLuint*);
    void bindTexture(GLenum, GLuint);
    void activeTexture(GLenum);
    void texImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void texSubImage2D(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
    void texStorage2D(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
    void texParameteri(GLenum, GLenum, GLint);
    void generateMipmap(GLenum);

    void genFramebuffers(GLsizei, GLuint*);
    void deleteFramebuffers(GLsizei, const GLuint*);
    void bindFramebuffer(GLenum, GLuint);
    void framebufferTexture2D(GLenum, GLenum, GLenum, GLuint, GLint);
    void framebufferRenderbuffer(GLenum, GLenum, GLenum, GLuint);
    [[nodiscard]] GLenum checkFramebufferStatus(GLenum);
    void drawBuffer(GLenum);
    void readBuffer(GLenum);
    void drawBuffers(GLsizei, const GLenum*);

    void genRenderbuffers(GLsizei, GLuint*);
    void deleteRenderbuffers(GLsizei, const GLuint*);
    void bindRenderbuffer(GLenum, GLuint);
    void renderbufferStorage(GLenum, GLenum, GLsizei, GLsizei);

    void viewport(GLint, GLint, GLsizei, GLsizei);
    void depthRange(GLdouble, GLdouble) noexcept;
    void scissor(GLint, GLint, GLsizei, GLsizei);
    void enable(GLenum, bool);
    [[nodiscard]] GLboolean isEnabled(GLenum) const;
    void depthFunc(GLenum);
    void depthMask(GLboolean);
    void blendFuncSeparate(GLenum, GLenum, GLenum, GLenum);
    void blendEquationSeparate(GLenum, GLenum);
    void blendColor(GLfloat, GLfloat, GLfloat, GLfloat) noexcept;
    void colorMask(GLboolean, GLboolean, GLboolean, GLboolean) noexcept;
    void cullFace(GLenum);
    void frontFace(GLenum);
    void clearColor(GLfloat, GLfloat, GLfloat, GLfloat) noexcept;
    void clearDepth(GLdouble) noexcept;
    void clearStencil(GLint) noexcept;
    void clear(GLbitfield);
    void drawArrays(GLenum, GLint, GLsizei, GLsizei instances = 1);
    void drawArrays(GLenum, GLint, GLsizei, GLsizei, GLuint baseInstance);
    void drawElements(GLenum, GLsizei, GLenum, const void*, GLsizei instances = 1,
                      GLint baseVertex = 0, GLuint baseInstance = 0);
    void finish();
    [[nodiscard]] core::ValueResult<std::vector<std::byte>> readbackDrawFramebuffer();

    void getIntegerv(GLenum, GLint*);
    void getFloatv(GLenum, GLfloat*);
    void getBooleanv(GLenum, GLboolean*);
    void getIntegeri(GLenum, GLuint, GLint*);
    void pixelStore(GLenum, GLint);

private:
    struct VertexAttribute {
        GLuint buffer{};
        GLint size{4};
        GLenum type{GL_FLOAT};
        GLsizei stride{};
        std::size_t offset{};
        bool normalized{};
        bool integer{};
        bool enabled{};
        GLuint divisor{};
    };
    struct VertexArray {
        std::array<VertexAttribute, 16> attributes;
        GLuint elementBuffer{};
    };
    struct IndexedBufferBinding {
        GLuint name{};
        GLintptr offset{};
        GLsizeiptr size{};
    };
    enum class AttachmentKind : std::uint8_t { none, texture, renderbuffer };
    struct Attachment {
        AttachmentKind kind{AttachmentKind::none};
        GLuint name{};
        GLint level{};
    };
    struct Framebuffer {
        Attachment color;
        Attachment depth;
        Attachment stencil;
        GLenum drawBuffer{GL_COLOR_ATTACHMENT0};
        GLenum readBuffer{GL_COLOR_ATTACHMENT0};
    };

    [[nodiscard]] GLuint boundBuffer(GLenum) const;
    [[nodiscard]] GLuint boundTexture2D() const noexcept;
    [[nodiscard]] VertexArray* currentVao();
    [[nodiscard]] core::ValueResult<core::TextureHandle> depthTarget(std::uint32_t, std::uint32_t);
    [[nodiscard]] core::ValueResult<core::PipelineHandle> pipelineFor(GLenum, const VertexArray&);
    [[nodiscard]] Framebuffer* framebufferForTarget(GLenum);
    [[nodiscard]] const Framebuffer* framebufferForTarget(GLenum) const;
    [[nodiscard]] core::TextureHandle attachmentHandle(const Attachment&) const;
    [[nodiscard]] backend::PixelFormat attachmentFormat(const Attachment&) const;
    [[nodiscard]] bool attachmentExtent(const Attachment&, GLsizei&, GLsizei&) const;
    [[nodiscard]] core::ValueResult<backend::Frame> acquireRenderFrame();
    [[nodiscard]] core::Result bindProgramResources(backend::CommandEncoder&, ProgramObject&);
    [[nodiscard]] core::Result applyDynamicState(backend::CommandEncoder&, std::uint32_t, std::uint32_t);
    [[nodiscard]] core::ValueResult<core::SamplerHandle> samplerFor(TextureObject&);
    [[nodiscard]] static bool textureFormat(GLint, backend::PixelFormat&) noexcept;
    [[nodiscard]] static bool primitive(GLenum, ir::Primitive&) noexcept;
    [[nodiscard]] static bool vertexScalar(GLenum, backend::VertexScalar&) noexcept;
    [[nodiscard]] static GLenum reflectedGlType(shader::ScalarKind, std::uint32_t, std::uint32_t) noexcept;
    void copyLog(std::string_view, GLsizei, GLsizei*, GLchar*);
    void backendError(const core::Error&);

    std::shared_ptr<DirectGlShareGroup> share_;
    std::shared_ptr<metal::MetalDeviceSession> session_;
    GLenum error_{GL_NO_ERROR};
    GLuint arrayBuffer_{};
    GLuint uniformBuffer_{};
    std::array<IndexedBufferBinding, 16> uniformBuffers_{};
    GLuint currentVao_{};
    GLuint currentProgram_{};
    GLuint nextVao_{1};
    GLuint nextFramebuffer_{1};
    std::unordered_map<GLuint, VertexArray> vaos_;
    std::unordered_map<GLuint, Framebuffer> framebuffers_;
    std::array<GLuint, 32> texture2D_{};
    std::uint32_t activeTextureUnit_{};
    GLuint renderbuffer_{};
    GLuint drawFramebuffer_{};
    GLuint readFramebuffer_{};
    backend::Viewport viewport_{};
    backend::ScissorRect scissor_{};
    bool scissorEnabled_{};
    bool depthTestEnabled_{};
    bool blendEnabled_{};
    bool cullEnabled_{};
    bool depthWrite_{true};
    GLenum depthFunction_{GL_LESS};
    GLenum sourceRgb_{GL_ONE};
    GLenum destinationRgb_{GL_ZERO};
    GLenum sourceAlpha_{GL_ONE};
    GLenum destinationAlpha_{GL_ZERO};
    GLenum rgbEquation_{GL_FUNC_ADD};
    GLenum alphaEquation_{GL_FUNC_ADD};
    GLenum cullMode_{GL_BACK};
    GLenum frontWinding_{GL_CCW};
    std::uint8_t colorWriteMask_{0x0f};
    std::array<float, 4> blendColor_{};
    std::array<float, 4> clearColor_{};
    GLint packAlignment_{4};
    GLint unpackAlignment_{4};
    GLint unpackRowLength_{};
    GLint unpackSkipRows_{};
    GLint unpackSkipPixels_{};
    double clearDepth_{1.0};
    std::uint32_t clearStencil_{};
    core::TextureHandle depthTexture_;
    std::uint32_t depthWidth_{};
    std::uint32_t depthHeight_{};
};

} // namespace mithril::frontend::gl
