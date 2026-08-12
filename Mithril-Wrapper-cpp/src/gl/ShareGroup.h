#pragma once

#include "core/ObjectStore.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mithril::gl {

struct BufferObject {
    std::vector<std::byte> bytes;
};

struct TextureObject {
    std::uint32_t width{};
    std::uint32_t height{};
};

class ShareGroup {
public:
    [[nodiscard]] core::ObjectStore<BufferObject, core::ObjectKind::buffer>& buffers() noexcept {
        return buffers_;
    }
    [[nodiscard]] core::ObjectStore<TextureObject, core::ObjectKind::texture>& textures() noexcept {
        return textures_;
    }

private:
    core::ObjectStore<BufferObject, core::ObjectKind::buffer> buffers_;
    core::ObjectStore<TextureObject, core::ObjectKind::texture> textures_;
};

} // namespace mithril::gl
