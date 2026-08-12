#pragma once

#include "core/Handles.h"
#include "core/Result.h"

#include <cstdint>
#include <cstddef>

namespace mithril::backend {

struct RenderPassDesc {
    core::TextureHandle color;
    core::TextureHandle depthStencil;
    bool clearColor{};
    float clearRed{};
    float clearGreen{};
    float clearBlue{};
    float clearAlpha{1.0F};
    bool clearDepth{};
    double clearDepthValue{1.0};
    bool clearStencil{};
    std::uint32_t clearStencilValue{};
};

struct DrawCommand {
    std::uint32_t vertexStart{};
    std::uint32_t vertexCount{};
    std::uint32_t instanceCount{1};
};

class CommandEncoder {
public:
    virtual ~CommandEncoder() = default;
    virtual core::Result beginRenderPass(const RenderPassDesc&) = 0;
    virtual core::Result bindPipeline(core::PipelineHandle) = 0;
    virtual core::Result bindVertexBuffer(std::uint32_t slot, core::BufferHandle, std::size_t offset) = 0;
    virtual core::Result bindTexture(std::uint32_t slot, core::TextureHandle, core::SamplerHandle) = 0;
    virtual core::Result draw(const DrawCommand&) = 0;
    virtual core::Result endRenderPass() = 0;
    virtual core::Result commit() = 0;
};

} // namespace mithril::backend
