#pragma once

#include "backend/CommandEncoder.h"
#include "backend/Pipeline.h"
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

    [[nodiscard]] GLuint boundBuffer(GLenum) const;
    [[nodiscard]] VertexArray* currentVao();
    [[nodiscard]] core::ValueResult<core::TextureHandle> depthTarget(std::uint32_t, std::uint32_t);
    [[nodiscard]] core::ValueResult<core::PipelineHandle> pipelineFor(GLenum, const VertexArray&);
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
    std::unordered_map<GLuint, VertexArray> vaos_;
    backend::Viewport viewport_{};
    std::array<float, 4> clearColor_{};
    double clearDepth_{1.0};
    std::uint32_t clearStencil_{};
    core::TextureHandle depthTexture_;
    std::uint32_t depthWidth_{};
    std::uint32_t depthHeight_{};
};

} // namespace mithril::frontend::gl
