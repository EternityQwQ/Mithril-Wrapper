#include "core/Error.h"

#include <algorithm>

namespace mithril::core {

Error Error::make(ErrorDomain domainValue, ErrorCode codeValue, std::string_view value) noexcept {
    Error result;
    result.domain = domainValue;
    result.code = codeValue;
    const std::size_t count = std::min(value.size(), result.message.size() - 1U);
    std::copy_n(value.data(), count, result.message.data());
    result.message[count] = '\0';
    return result;
}

std::string_view Error::text() const noexcept {
    const auto end = std::find(message.begin(), message.end(), '\0');
    return {message.data(), static_cast<std::size_t>(end - message.begin())};
}

} // namespace mithril::core
