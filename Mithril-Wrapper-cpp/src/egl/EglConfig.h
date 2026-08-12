#pragma once

#include <EGL/egl.h>

#include <array>

namespace mithril::egl {

struct Config final {
    EGLint id;
    EGLint red;
    EGLint green;
    EGLint blue;
    EGLint alpha;
    EGLint depth;
    EGLint stencil;
};

[[nodiscard]] const std::array<Config, 4>& configs() noexcept;
[[nodiscard]] bool matches(const Config&, const EGLint* attributes) noexcept;
[[nodiscard]] bool attribute(const Config&, EGLint name, EGLint& value) noexcept;
[[nodiscard]] const Config* fromHandle(EGLConfig) noexcept;

} // namespace mithril::egl
