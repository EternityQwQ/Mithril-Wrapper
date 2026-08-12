#pragma once

#include <compare>
#include <cstdint>
#include <limits>

namespace mithril::core {

enum class ObjectKind : std::uint8_t {
    buffer,
    texture,
    sampler,
    shader,
    pipeline,
    surface,
};

namespace detail {
inline constexpr std::uint32_t nextGeneration(std::uint32_t generation) noexcept {
    return generation == std::numeric_limits<std::uint32_t>::max() ? 1U : generation + 1U;
}
} // namespace detail

template <ObjectKind KindValue>
struct Handle {
    static constexpr ObjectKind kind = KindValue;
    std::uint32_t slot{};
    std::uint32_t generation{};

    [[nodiscard]] constexpr bool valid() const noexcept { return generation != 0; }
    auto operator<=>(const Handle&) const = default;
};

using BufferHandle = Handle<ObjectKind::buffer>;
using TextureHandle = Handle<ObjectKind::texture>;
using SamplerHandle = Handle<ObjectKind::sampler>;
using ShaderHandle = Handle<ObjectKind::shader>;
using PipelineHandle = Handle<ObjectKind::pipeline>;
using SurfaceHandle = Handle<ObjectKind::surface>;

} // namespace mithril::core
