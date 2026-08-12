#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace mithril::core {

enum class ErrorDomain : std::uint8_t {
    gl,
    egl,
    shader,
    device,
    surface,
    resource,
    contract,
};

enum class ErrorCode : std::uint16_t {
    invalid_argument,
    invalid_state,
    not_found,
    stale_handle,
    unsupported,
    unavailable,
    compile_failed,
    out_of_memory,
};

struct Error {
    ErrorDomain domain{ErrorDomain::contract};
    ErrorCode code{ErrorCode::invalid_state};
    std::array<char, 160> message{};

    static Error make(ErrorDomain domain, ErrorCode code, std::string_view message) noexcept;
    [[nodiscard]] std::string_view text() const noexcept;
};

} // namespace mithril::core
