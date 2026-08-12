#pragma once

#include <cstdint>
#include <string>

namespace mithril::shader {

enum class ShaderErrorCode : std::uint8_t {
    invalidStage,
    invalidSource,
    preprocessFailed,
    compileFailed,
    reflectionFailed,
    translationFailed,
};

struct ShaderDiagnostic {
    ShaderErrorCode code{ShaderErrorCode::compileFailed};
    std::string message;
    std::string sourceName;
    std::uint32_t line{};
    std::uint32_t column{};
};

[[nodiscard]] ShaderDiagnostic sanitizeDiagnostic(ShaderDiagnostic diagnostic,
                                                  std::size_t maxLength = 4096);

} // namespace mithril::shader
