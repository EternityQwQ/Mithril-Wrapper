#pragma once

#include "backend/ResourceTypes.h"

#include <array>
#include <compare>
#include <cstdint>

namespace mithril::ir {

enum class Primitive : std::uint8_t { point, line, triangle };
enum class Compare : std::uint8_t { always, less, lessEqual, equal, greater };

struct PipelineKey {
    std::uint64_t vertexShaderHash{};
    std::uint64_t fragmentShaderHash{};
    std::uint64_t vertexLayoutHash{};
    std::array<backend::PixelFormat, 4> colorFormats{};
    backend::PixelFormat depthStencilFormat{backend::PixelFormat::none};
    Primitive primitive{Primitive::triangle};
    Compare depthCompare{Compare::less};
    std::uint8_t colorAttachmentCount{1};
    std::uint8_t sampleCount{1};
    bool blending{};
    bool depthWrite{true};
    bool flipY{};

    [[nodiscard]] PipelineKey normalized() const noexcept;
    auto operator<=>(const PipelineKey&) const = default;
};

[[nodiscard]] std::uint64_t hashPipelineKey(const PipelineKey&) noexcept;

} // namespace mithril::ir
