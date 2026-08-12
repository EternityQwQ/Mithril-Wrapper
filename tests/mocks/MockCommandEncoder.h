#pragma once

#include "backend/CommandEncoder.h"

#include <string>
#include <vector>

namespace mithril::tests {

class MockCommandEncoder final : public backend::CommandEncoder {
public:
    std::vector<std::string> calls;
    core::Result beginRenderPass(const backend::RenderPassDesc&) override { calls.emplace_back("begin"); return {}; }
    core::Result bindPipeline(core::PipelineHandle) override { calls.emplace_back("pipeline"); return {}; }
    core::Result setViewport(const backend::Viewport&) override { calls.emplace_back("viewport"); return {}; }
    core::Result bindVertexBuffer(std::uint32_t, core::BufferHandle, std::size_t) override { calls.emplace_back("buffer"); return {}; }
    core::Result bindIndexBuffer(core::BufferHandle, std::size_t, backend::IndexType) override { calls.emplace_back("index"); return {}; }
    core::Result bindTexture(std::uint32_t, core::TextureHandle, core::SamplerHandle) override { calls.emplace_back("texture"); return {}; }
    core::Result draw(const backend::DrawCommand&) override { calls.emplace_back("draw"); return {}; }
    core::Result drawIndexed(const backend::DrawIndexedCommand&) override { calls.emplace_back("drawIndexed"); return {}; }
    core::Result endRenderPass() override { calls.emplace_back("end"); return {}; }
    core::Result commit() override { calls.emplace_back("commit"); return {}; }
};

} // namespace mithril::tests
