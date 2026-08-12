#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mithril::backend {

enum class BufferUsage : std::uint8_t { vertex, index, uniform, staging };
enum class PixelFormat : std::uint8_t {
    none,
    rgba8Unorm,
    bgra8Unorm,
    depth32Float,
    depth24Stencil8,
    depth32FloatStencil8,
};
enum class Filter : std::uint8_t { nearest, linear };
enum class AddressMode : std::uint8_t { clampToEdge, repeat, mirroredRepeat };
enum class VertexScalar : std::uint8_t { float32, sint32, uint32, sint16, uint16, sint8, uint8 };

struct BufferDesc {
    std::size_t size{};
    BufferUsage usage{BufferUsage::vertex};
};

struct TextureDesc {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint16_t mipLevels{1};
    PixelFormat format{PixelFormat::rgba8Unorm};
};

struct SamplerDesc {
    Filter minFilter{Filter::nearest};
    Filter magFilter{Filter::nearest};
    AddressMode addressU{AddressMode::clampToEdge};
    AddressMode addressV{AddressMode::clampToEdge};
};

struct VertexAttributeDesc {
    std::uint32_t location{};
    std::uint32_t bufferSlot{};
    std::uint32_t offset{};
    std::uint32_t stride{};
    VertexScalar scalar{VertexScalar::float32};
    std::uint8_t components{4};
    bool normalized{};
};

} // namespace mithril::backend
