#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "backend/Pipeline.h"
#include "egl/EglBridge.h"
#include "MG_Backend/DirectMetal/MetalDeviceSession.h"
#include "MG_State/DirectGlContext.h"
#include "shader/GlslangCompiler.h"
#include "shader/SpirvCrossMslCompiler.h"
#include "fixtures/triangle_fixture.h"

#include <EGL/egl.h>
#include <GL/gl.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>

namespace {


struct Vertex {
    float x;
    float y;
    float r;
    float g;
    float b;
};

template <typename Result>
bool require(const Result& result, const char* operation) {
    if (result) return true;
    std::cerr << operation << ": " << result.error().text() << '\n';
    return false;
}

bool testGlFrontend() {
    const auto fail = [](const char* stage) {
        std::cerr << "GL frontend stage failed: " << stage << " glError=0x"
                  << std::hex << glGetError() << std::dec << '\n';
        return false;
    };
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0;
    EGLint minor = 0;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor)) return fail("eglInitialize");
    EGLConfig config = nullptr;
    EGLint configCount = 0;
    const EGLint choose[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
    if (!eglChooseConfig(display, choose, &config, 1, &configCount) || configCount != 1) return fail("eglChooseConfig");
    const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 3, EGL_CONTEXT_OPENGL_PROFILE_MASK,
        EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    const EGLint surfaceAttributes[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttributes);
    if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(display, surface, surface, context)) return fail("eglMakeCurrent");

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return fail("default framebuffer status");
    GLuint framebuffer = 0;
    GLuint colorTexture = 0;
    GLuint depthRenderbuffer = 0;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT) return fail("empty framebuffer status");
    glGenTextures(1, &colorTexture);
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 32, 32);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, colorTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT)
        return fail("color texture rejected as depth attachment");
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    glGenRenderbuffers(1, &depthRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 32, 32);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER, depthRenderbuffer);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return fail("complete framebuffer status");
    glClearColor(0.25F, 0.5F, 0.75F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    if (glGetError() != GL_NO_ERROR) return fail("framebuffer clear");

    GLuint minecraftFramebuffer = 0;
    GLuint minecraftColorTexture = 0;
    GLuint minecraftDepthTexture = 0;
    glGenFramebuffers(1, &minecraftFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, minecraftFramebuffer);
    glGenTextures(1, &minecraftColorTexture);
    glBindTexture(GL_TEXTURE_2D, minecraftColorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 32, 32, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, minecraftColorTexture, 0);
    glGenTextures(1, &minecraftDepthTexture);
    glBindTexture(GL_TEXTURE_2D, minecraftDepthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 32, 32, 0,
        GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D, minecraftDepthTexture, 0);
    if (glGetError() != GL_NO_ERROR) return fail("Minecraft framebuffer allocation");
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return fail("Minecraft depth texture framebuffer status");
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (glGetError() != GL_NO_ERROR) return fail("Minecraft depth texture framebuffer clear");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    const auto vertex = mithril::tests::triangleVertexShader();
    const auto fragment = mithril::tests::triangleFragmentShader();
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    const char* vertexSource = vertex.source.c_str();
    glShaderSource(vertexShader, 1, &vertexSource, nullptr);
    glCompileShader(vertexShader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) return fail("vertex compile");
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    const char* fragmentSource = fragment.source.c_str();
    glShaderSource(fragmentShader, 1, &fragmentSource, nullptr);
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) return fail("fragment compile");
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) return fail("program link");
    glUseProgram(program);

    if (glGetUniformLocation(program, "MissingUniform") != -1)
        return fail("missing uniform location");
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    const char* uniformVertexSource =
        "#version 330 core\nlayout(location=0) in vec2 position; out vec2 uv; "
        "uniform mat4 Transform; void main(){ gl_Position=Transform*vec4(position,0,1); uv=position*0.5+0.5; }";
    const char* uniformFragmentSource =
        "#version 330 core\nin vec2 uv; out vec4 color; uniform sampler2D Sampler0; "
        "uniform sampler2D Sampler1; uniform vec4 ColorModulator; "
        "void main(){ color=(texture(Sampler0,uv)+texture(Sampler1,uv))*0.5*ColorModulator; }";
    GLuint uniformVertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(uniformVertex, 1, &uniformVertexSource, nullptr);
    glCompileShader(uniformVertex);
    GLuint uniformFragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(uniformFragment, 1, &uniformFragmentSource, nullptr);
    glCompileShader(uniformFragment);
    GLuint uniformProgram = glCreateProgram();
    glAttachShader(uniformProgram, uniformVertex);
    glAttachShader(uniformProgram, uniformFragment);
    glLinkProgram(uniformProgram);
    glGetProgramiv(uniformProgram, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) return fail("uniform texture program link");
    glUseProgram(uniformProgram);
    constexpr GLfloat identity[] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    glUniformMatrix4fv(glGetUniformLocation(uniformProgram, "Transform"), 1, GL_FALSE, identity);
    glUniform4f(glGetUniformLocation(uniformProgram, "ColorModulator"), 1, 1, 1, 1);
    glUniform1i(glGetUniformLocation(uniformProgram, "Sampler0"), 0);
    glUniform1i(glGetUniformLocation(uniformProgram, "Sampler1"), 1);
    GLuint sampledTextures[2]{};
    constexpr std::array<GLubyte, 4> red{255, 0, 0, 255};
    constexpr std::array<GLubyte, 4> green{0, 255, 0, 255};
    glGenTextures(2, sampledTextures);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sampledTextures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, red.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, sampledTextures[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, green.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    constexpr std::array vertices{
        Vertex{-0.8F, -0.8F, 1.0F, 0.0F, 0.0F},
        Vertex{ 0.8F, -0.8F, 0.0F, 1.0F, 0.0F},
        Vertex{ 0.0F,  0.8F, 0.0F, 0.0F, 1.0F},
    };
    GLuint buffer = 0;
    GLuint vao = 0;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), nullptr, GL_DYNAMIC_DRAW);
    void* mapped = glMapBufferRange(GL_ARRAY_BUFFER, 0, sizeof(vertices),
        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    if (mapped == nullptr) return fail("map vertex buffer");
    std::memcpy(mapped, vertices.data(), sizeof(vertices));
    if (glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY) != nullptr ||
        glGetError() != GL_INVALID_OPERATION) return fail("reject double map");
    if (glUnmapBuffer(GL_ARRAY_BUFFER) != GL_TRUE) return fail("unmap vertex buffer");
    GLint mappedState = GL_TRUE;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAPPED, &mappedState);
    if (mappedState != GL_FALSE) return fail("buffer mapping query");
    mapped = glMapBufferRange(GL_ARRAY_BUFFER, 4, 8,
        GL_MAP_WRITE_BIT | GL_MAP_FLUSH_EXPLICIT_BIT);
    if (mapped == nullptr) return fail("map explicit flush range");
    glFlushMappedBufferRange(GL_ARRAY_BUFFER, 0, 8);
    if (glGetError() != GL_NO_ERROR || glUnmapBuffer(GL_ARRAY_BUFFER) != GL_TRUE)
        return fail("relative explicit flush range");
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<const void*>(2 * sizeof(float)));
    glViewport(0, 0, 64, 64);
    glDisableVertexAttribArray(1);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (glGetError() != GL_NO_ERROR) return fail("default framebuffer draw");
    glUseProgram(program);
    glEnableVertexAttribArray(1);

    glBindFramebuffer(GL_FRAMEBUFFER, minecraftFramebuffer);
    glViewport(0, 0, 32, 32);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
    glScissor(0, 0, 32, 32);
    glEnable(GL_SCISSOR_TEST);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (glGetError() != GL_NO_ERROR) return fail("Minecraft framebuffer draw");
    if (!mithril::egl::bridge::currentSession()) return fail("current Metal session");
    auto fboPixels = mithril::egl::bridge::currentGlContext()->readbackDrawFramebuffer();
    if (!fboPixels) return fail("Minecraft framebuffer readback");
    std::size_t fboColored = 0;
    for (std::size_t index = 0; index < fboPixels.value().size(); ++index) {
        if (index % 4 != 3 && std::to_integer<unsigned char>(fboPixels.value()[index]) > 16) ++fboColored;
    }
    if (fboColored < 200) return fail("Minecraft framebuffer pixels");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, 64, 64);
    auto pixels = mithril::egl::bridge::readbackDrawFrame();
    if (!pixels) return fail("default framebuffer readback");
    std::size_t colored = 0;
    for (std::size_t index = 0; index < pixels.value().size(); ++index) {
        if (index % 4 != 3 && std::to_integer<unsigned char>(pixels.value()[index]) > 16) ++colored;
    }
    const bool rendered = colored > 1000;
    const bool swapped = eglSwapBuffers(display, surface) == EGL_TRUE;
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &buffer);
    glDeleteProgram(program);
    glDeleteProgram(uniformProgram);
    glDeleteShader(uniformVertex);
    glDeleteShader(uniformFragment);
    glDeleteRenderbuffers(1, &depthRenderbuffer);
    const GLuint textures[] = {colorTexture, minecraftColorTexture, minecraftDepthTexture,
        sampledTextures[0], sampledTextures[1]};
    glDeleteTextures(5, textures);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteFramebuffers(1, &minecraftFramebuffer);
    const bool released = eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT) == EGL_TRUE;
    const bool destroyed = eglDestroySurface(display, surface) == EGL_TRUE &&
        eglDestroyContext(display, context) == EGL_TRUE;
    const bool terminated = eglTerminate(display) == EGL_TRUE;
    if (!rendered) return fail("triangle pixels");
    if (!swapped) return fail("eglSwapBuffers");
    if (!released) return fail("egl release current");
    if (!destroyed) return fail("egl destroy objects");
    if (!terminated) return fail("eglTerminate");
    return true;
}

} // namespace

int main() {
    @autoreleasepool {
        auto session = mithril::metal::MetalDeviceSession::create();
        if (!require(session, "create Metal session")) return 1;

        mithril::shader::GlslangCompiler glslang;
        mithril::shader::SpirvCrossMslCompiler spirvCross;
        auto vertexSpirv = glslang.compile(mithril::tests::triangleVertexShader(), {});
        if (!vertexSpirv) { std::cerr << vertexSpirv.error().message << '\n'; return 2; }
        auto fragmentSpirv = glslang.compile(mithril::tests::triangleFragmentShader(), {});
        if (!fragmentSpirv) { std::cerr << fragmentSpirv.error().message << '\n'; return 3; }
        auto vertexMsl = spirvCross.translate(vertexSpirv.value());
        if (!vertexMsl) { std::cerr << vertexMsl.error().message << '\n'; return 4; }
        auto fragmentMsl = spirvCross.translate(fragmentSpirv.value());
        if (!fragmentMsl) { std::cerr << fragmentMsl.error().message << '\n'; return 5; }
        auto vertexShader = session.value()->createShader(vertexMsl.value());
        auto fragmentShader = session.value()->createShader(fragmentMsl.value());
        if (!require(vertexShader, "create vertex shader") || !require(fragmentShader, "create fragment shader")) return 6;

        mithril::backend::PipelineDesc pipelineDesc;
        pipelineDesc.key.colorFormats[0] = mithril::backend::PixelFormat::rgba8Unorm;
        pipelineDesc.key.depthStencilFormat = mithril::backend::PixelFormat::none;
        pipelineDesc.vertexAttributes = {
            {0, 0, 0, sizeof(Vertex), mithril::backend::VertexScalar::float32, 2, 0, false},
            {1, 0, 2 * sizeof(float), sizeof(Vertex), mithril::backend::VertexScalar::float32, 3, 0, false},
        };
        const std::array shaders{vertexShader.value(), fragmentShader.value()};
        auto pipeline = session.value()->createPipeline(shaders, pipelineDesc);
        if (!require(pipeline, "create Metal pipeline")) return 7;

        constexpr std::array vertices{
            Vertex{-0.8F, -0.8F, 1.0F, 0.0F, 0.0F},
            Vertex{ 0.8F, -0.8F, 0.0F, 1.0F, 0.0F},
            Vertex{ 0.0F,  0.8F, 0.0F, 0.0F, 1.0F},
        };
        auto vertexBuffer = session.value()->createBuffer({sizeof(vertices), mithril::backend::BufferUsage::vertex});
        if (!require(vertexBuffer, "create vertex buffer")) return 8;
        const auto vertexBytes = std::as_bytes(std::span(vertices));
        if (!require(session.value()->upload(vertexBuffer.value(), 0, vertexBytes), "upload vertices")) return 9;
        auto target = session.value()->createTexture({64, 64, 1, mithril::backend::PixelFormat::rgba8Unorm});
        if (!require(target, "create render target")) return 10;

        auto commands = session.value()->createCommandEncoder();
        if (!require(commands, "create command encoder")) return 11;
        mithril::backend::RenderPassDesc pass;
        pass.color = target.value();
        pass.clearColor = true;
        pass.clearAlpha = 1.0F;
        if (!require(commands.value()->beginRenderPass(pass), "begin render pass") ||
            !require(commands.value()->bindPipeline(pipeline.value()), "bind pipeline") ||
            !require(commands.value()->bindVertexBuffer(0, vertexBuffer.value(), 0), "bind vertices") ||
            !require(commands.value()->draw({0, 3, 1}), "draw triangle") ||
            !require(commands.value()->endRenderPass(), "end render pass") ||
            !require(commands.value()->commit(), "commit triangle")) return 12;
        if (!require(session.value()->waitIdle(5ULL * NSEC_PER_SEC), "wait for triangle")) return 13;
        auto pixels = session.value()->readbackRgba8(target.value());
        if (!require(pixels, "read back render target")) return 14;

        std::size_t colored = 0;
        std::uint64_t hash = 1469598103934665603ULL;
        for (std::size_t index = 0; index < pixels.value().size(); ++index) {
            const auto value = std::to_integer<unsigned char>(pixels.value()[index]);
            hash ^= value;
            hash *= 1099511628211ULL;
            if (index % 4 != 3 && value > 16) ++colored;
        }
        if (colored < 1000 || hash == 0 || hash == 1469598103934665603ULL) {
            std::cerr << "triangle readback invariant failed: colored=" << colored << " hash=" << hash << '\n';
            return 15;
        }
        if (!testGlFrontend()) {
            std::cerr << "EGL + GL 3.3 frontend triangle failed\n";
            return 16;
        }
        NSLog(@"Metal 2 triangle passed on %@; hash=%llu", @(
            session.value()->capabilities().deviceName.c_str()), hash);
        return 0;
    }
}
