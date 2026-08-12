#include "ir/PipelineKey.h"

#include <algorithm>
#include <bit>

namespace mithril::ir {

PipelineKey PipelineKey::normalized() const noexcept {
    PipelineKey result = *this;
    result.colorAttachmentCount = std::min<std::uint8_t>(result.colorAttachmentCount, 4);
    result.sampleCount = std::max<std::uint8_t>(result.sampleCount, 1);
    for (std::size_t i = result.colorAttachmentCount; i < result.colorFormats.size(); ++i) {
        result.colorFormats[i] = backend::PixelFormat::rgba8Unorm;
    }
    return result;
}

std::uint64_t hashPipelineKey(const PipelineKey& input) noexcept {
    const PipelineKey key = input.normalized();
    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    mix(key.vertexShaderHash);
    mix(key.fragmentShaderHash);
    mix(key.vertexLayoutHash);
    for (const auto format : key.colorFormats) mix(static_cast<std::uint8_t>(format));
    mix(static_cast<std::uint8_t>(key.depthStencilFormat));
    mix(static_cast<std::uint8_t>(key.primitive));
    mix(static_cast<std::uint8_t>(key.depthCompare));
    mix(key.colorAttachmentCount);
    mix(key.sampleCount);
    mix(key.blending);
    mix(key.depthWrite);
    mix(key.flipY);
    return hash;
}

} // namespace mithril::ir
