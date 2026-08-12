#pragma once

#include "backend/Presenter.h"
#include "core/Result.h"

#include <memory>

namespace mithril::metal { class MetalDeviceSession; }
namespace mithril::frontend::gl { class DirectGlContext; }

namespace mithril::egl::bridge {

[[nodiscard]] std::shared_ptr<metal::MetalDeviceSession> currentSession();
[[nodiscard]] frontend::gl::DirectGlContext* currentGlContext() noexcept;
[[nodiscard]] core::ValueResult<backend::Frame> acquireDrawFrame();
[[nodiscard]] core::Result presentDrawFrame();

} // namespace mithril::egl::bridge
