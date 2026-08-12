#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "backend/Pipeline.h"
#include "metal/MetalDeviceSession.h"
#include "shader/GlslangCompiler.h"
#include "shader/SpirvCrossMslCompiler.h"
#include "fixtures/triangle_fixture.h"

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
        NSLog(@"Metal 2 triangle passed on %@; hash=%llu", @(
            session.value()->capabilities().deviceName.c_str()), hash);
        return 0;
    }
}
