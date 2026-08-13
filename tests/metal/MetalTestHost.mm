#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "backend/Pipeline.h"
#include "egl/EglBridge.h"
#include "MG_Backend/DirectMetal/MetalDeviceSession.h"
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
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0;
    EGLint minor = 0;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor)) return false;
    EGLConfig config = nullptr;
    EGLint configCount = 0;
    const EGLint choose[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
    if (!eglChooseConfig(display, choose, &config, 1, &configCount) || configCount != 1) return false;
    const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 3, EGL_CONTEXT_OPENGL_PROFILE_MASK,
        EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    const EGLint surfaceAttributes[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttributes);
    if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(display, surface, surface, context)) return false;

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    GLuint framebuffer = 0;
    GLuint colorTexture = 0;
    GLuint depthRenderbuffer = 0;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT) return false;
    glGenTextures(1, &colorTexture);
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 32, 32);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
    glGenRenderbuffers(1, &depthRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 32, 32);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER, depthRenderbuffer);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    glClearColor(0.25F, 0.5F, 0.75F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    if (glGetError() != GL_NO_ERROR) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    const auto vertex = mithril::tests::triangleVertexShader();
    const auto fragment = mithril::tests::triangleFragmentShader();
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    const char* vertexSource = vertex.source.c_str();
    glShaderSource(vertexShader, 1, &vertexSource, nullptr);
    glCompileShader(vertexShader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) return false;
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    const char* fragmentSource = fragment.source.c_str();
    glShaderSource(fragmentShader, 1, &fragmentSource, nullptr);
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) return false;
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) return false;
    glUseProgram(program);

    constexpr std::array vertices{
        Vertex{-0.8F, -0.8F, 1.0F, 0.0F, 0.0F},
        Vertex{ 0.8F, -0.8F, 0.0F, 1.0F, 0.0F},
        Vertex{ 0.0F,  0.8F, 0.0F, 0.0F, 1.0F},
    };
    GLuint buffer = 0;
    GLuint vao = 0;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices.data(), GL_STATIC_DRAW);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<const void*>(2 * sizeof(float)));
    glViewport(0, 0, 64, 64);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (glGetError() != GL_NO_ERROR) return false;
    auto pixels = mithril::egl::bridge::readbackDrawFrame();
    if (!pixels) return false;
    std::size_t colored = 0;
    for (std::size_t index = 0; index < pixels.value().size(); ++index) {
        if (index % 4 != 3 && std::to_integer<unsigned char>(pixels.value()[index]) > 16) ++colored;
    }
    const bool rendered = colored > 1000;
    const bool swapped = eglSwapBuffers(display, surface) == EGL_TRUE;
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &buffer);
    glDeleteProgram(program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    glDeleteRenderbuffers(1, &depthRenderbuffer);
    glDeleteTextures(1, &colorTexture);
    glDeleteFramebuffers(1, &framebuffer);
    const bool released = eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT) == EGL_TRUE;
    const bool destroyed = eglDestroySurface(display, surface) == EGL_TRUE &&
        eglDestroyContext(display, context) == EGL_TRUE;
    const bool terminated = eglTerminate(display) == EGL_TRUE;
    return rendered && swapped && released && destroyed && terminated;
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
            {0, 0, 0, sizeof(Vertex), mithril::backend::VertexScalar::float32, 2, false},
            {1, 0, 2 * sizeof(float), sizeof(Vertex), mithril::backend::VertexScalar::float32, 3, false},
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
