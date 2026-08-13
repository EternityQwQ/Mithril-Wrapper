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
};

struct ShaderObject final {
    GLenum type{};
    std::string source;
    std::string log;
    core::ShaderHandle handle;
    bool compiled{};
};

struct ProgramObject final {
    std::vector<GLuint> attachedShaders;
    std::string log;
    bool linked{};
};

struct TextureObject final {
    core::TextureHandle handle;
    GLsizei width{};
    GLsizei height{};
    GLint internalFormat{GL_RGBA8};
    GLsizei levels{1};
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
    void bufferData(GLenum, GLsizeiptr, const void*, GLenum);
    void bufferSubData(GLenum, GLintptr, GLsizeiptr, const void*);

    void genVertexArrays(GLsizei, GLuint*);
    void deleteVertexArrays(GLsizei, const GLuint*);
    void bindVertexArray(GLuint);
    void enableVertexAttrib(GLuint, bool);
    void vertexAttribPointer(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*, bool integer);

    [[nodiscard]] GLuint createShader(GLenum);
    void deleteShader(GLuint);
    void shaderSource(GLuint, GLsizei, const GLchar* const*, const GLint*);
    void compileShader(GLuint);
    void getShaderiv(GLuint, GLenum, GLint*);
    void getShaderInfoLog(GLuint, GLsizei, GLsizei*, GLchar*);

    [[nodiscard]] GLuint createProgram();
    void deleteProgram(GLuint);
    void attachShader(GLuint, GLuint);
    void detachShader(GLuint, GLuint);
    void linkProgram(GLuint);
    void useProgram(GLuint);
    void getProgramiv(GLuint, GLenum, GLint*);
    void getProgramInfoLog(GLuint, GLsizei, GLsizei*, GLchar*);

    void genTextures(GLsizei, GLuint*);
    void deleteTextures(GLsizei, const GLuint*);
    void bindTexture(GLenum, GLuint);
    void texImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void texSubImage2D(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
    void texStorage2D(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
    void texParameteri(GLenum, GLenum, GLint);

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
    void clearColor(GLfloat, GLfloat, GLfloat, GLfloat) noexcept;
    void clearDepth(GLdouble) noexcept;
    void clearStencil(GLint) noexcept;
    void clear(GLbitfield);
    void drawArrays(GLenum, GLint, GLsizei, GLsizei instances = 1);
    void drawElements(GLenum, GLsizei, GLenum, const void*, GLsizei instances = 1,
                      GLint baseVertex = 0, GLuint baseInstance = 0);
    void finish();

    void getIntegerv(GLenum, GLint*);

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
    };
    struct VertexArray {
        std::array<VertexAttribute, 16> attributes;
        GLuint elementBuffer{};
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
    [[nodiscard]] VertexArray* currentVao();
    [[nodiscard]] core::ValueResult<core::TextureHandle> depthTarget(std::uint32_t, std::uint32_t);
    [[nodiscard]] core::ValueResult<core::PipelineHandle> pipelineFor(GLenum, const VertexArray&);
    [[nodiscard]] Framebuffer* framebufferForTarget(GLenum);
    [[nodiscard]] const Framebuffer* framebufferForTarget(GLenum) const;
    [[nodiscard]] core::TextureHandle attachmentHandle(const Attachment&) const;
    [[nodiscard]] backend::PixelFormat attachmentFormat(const Attachment&) const;
    [[nodiscard]] bool attachmentExtent(const Attachment&, GLsizei&, GLsizei&) const;
    [[nodiscard]] core::ValueResult<backend::Frame> acquireRenderFrame();
    [[nodiscard]] static bool textureFormat(GLint, backend::PixelFormat&) noexcept;
    [[nodiscard]] static bool primitive(GLenum, ir::Primitive&) noexcept;
    [[nodiscard]] static bool vertexScalar(GLenum, backend::VertexScalar&) noexcept;
    void copyLog(std::string_view, GLsizei, GLsizei*, GLchar*);
    void backendError(const core::Error&);

    std::shared_ptr<DirectGlShareGroup> share_;
    std::shared_ptr<metal::MetalDeviceSession> session_;
    GLenum error_{GL_NO_ERROR};
    GLuint arrayBuffer_{};
    GLuint currentVao_{};
    GLuint currentProgram_{};
    GLuint nextVao_{1};
    GLuint nextFramebuffer_{1};
    std::unordered_map<GLuint, VertexArray> vaos_;
    std::unordered_map<GLuint, Framebuffer> framebuffers_;
    GLuint texture2D_{};
    GLuint renderbuffer_{};
    GLuint drawFramebuffer_{};
    GLuint readFramebuffer_{};
    backend::Viewport viewport_{};
    std::array<float, 4> clearColor_{};
    double clearDepth_{1.0};
    std::uint32_t clearStencil_{};
    core::TextureHandle depthTexture_;
    std::uint32_t depthWidth_{};
    std::uint32_t depthHeight_{};
};

} // namespace mithril::frontend::gl
