#pragma once

#include "core/Result.h"
#include "gl/Capability.h"

namespace mithril::platform::apple {

class AppleCapabilities final {
public:
    [[nodiscard]] static core::ValueResult<gl::CapabilityManifest> queryDefaultDevice();
};

} // namespace mithril::platform::apple
