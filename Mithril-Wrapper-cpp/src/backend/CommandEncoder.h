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

enum class IndexType : std::uint8_t { uint16, uint32 };

struct Viewport {
    double x{};
    double y{};
    double width{};
    double height{};
    double nearDepth{};
    double farDepth{1.0};
};

struct DrawIndexedCommand {
    std::uint32_t indexCount{};
    std::uint32_t instanceCount{1};
    std::int32_t baseVertex{};
    std::uint32_t baseInstance{};
};

class CommandEncoder {
public:
    virtual ~CommandEncoder() = default;
    virtual core::Result beginRenderPass(const RenderPassDesc&) = 0;
    virtual core::Result bindPipeline(core::PipelineHandle) = 0;
    virtual core::Result setViewport(const Viewport&) = 0;
    virtual core::Result bindVertexBuffer(std::uint32_t slot, core::BufferHandle, std::size_t offset) = 0;
    virtual core::Result bindIndexBuffer(core::BufferHandle, std::size_t offset, IndexType) = 0;
    virtual core::Result bindTexture(std::uint32_t slot, core::TextureHandle, core::SamplerHandle) = 0;
    virtual core::Result draw(const DrawCommand&) = 0;
    virtual core::Result drawIndexed(const DrawIndexedCommand&) = 0;
    virtual core::Result endRenderPass() = 0;
    virtual core::Result commit() = 0;
};

} // namespace mithril::backend
