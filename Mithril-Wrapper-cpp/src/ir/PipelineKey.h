#pragma once

#include "backend/ResourceTypes.h"

#include <array>
#include <compare>
#include <cstdint>

namespace mithril::ir {

enum class Primitive : std::uint8_t { point, line, lineStrip, triangle, triangleStrip };
enum class Compare : std::uint8_t { never, less, lessEqual, equal, notEqual, greater, greaterEqual, always };
enum class BlendFactor : std::uint8_t {
    zero, one, sourceColor, oneMinusSourceColor, sourceAlpha, oneMinusSourceAlpha,
    destinationColor, oneMinusDestinationColor, destinationAlpha, oneMinusDestinationAlpha,
    sourceAlphaSaturated, blendColor, oneMinusBlendColor, blendAlpha, oneMinusBlendAlpha
};
enum class BlendOperation : std::uint8_t { add, subtract, reverseSubtract, minimum, maximum };

struct PipelineKey {
    std::uint64_t vertexShaderHash{};
    std::uint64_t fragmentShaderHash{};
    std::uint64_t vertexLayoutHash{};
    std::array<backend::PixelFormat, 4> colorFormats{};
    backend::PixelFormat depthStencilFormat{backend::PixelFormat::none};
    Primitive primitive{Primitive::triangle};
    Compare depthCompare{Compare::less};
    BlendFactor sourceRgb{BlendFactor::one};
    BlendFactor destinationRgb{BlendFactor::zero};
    BlendFactor sourceAlpha{BlendFactor::one};
    BlendFactor destinationAlpha{BlendFactor::zero};
    BlendOperation rgbOperation{BlendOperation::add};
    BlendOperation alphaOperation{BlendOperation::add};
    std::uint8_t colorAttachmentCount{1};
    std::uint8_t sampleCount{1};
    std::uint8_t colorWriteMask{0x0f};
    bool blending{};
    bool depthWrite{true};
    bool flipY{};

    [[nodiscard]] PipelineKey normalized() const noexcept;
    auto operator<=>(const PipelineKey&) const = default;
};

[[nodiscard]] std::uint64_t hashPipelineKey(const PipelineKey&) noexcept;

} // namespace mithril::ir
