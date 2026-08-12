#pragma once

#include <cstdint>

namespace mithril::backend {

struct SurfaceDesc {
    std::uint32_t width{};
    std::uint32_t height{};
    float scale{1.0F};
    std::uint64_t generation{1};
    // Opaque platform object supplied by the host. Apple accepts a
    // CAMetalLayer, CALayer, UIView or NSView. Null creates an offscreen
    // surface suitable for pbuffers and tests.
    void* nativeWindow{};
    bool framebufferOnly{true};
};

} // namespace mithril::backend
