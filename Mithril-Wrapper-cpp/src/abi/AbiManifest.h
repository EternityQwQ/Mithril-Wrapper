#pragma once

#include "core/Error.h"
#include "core/Expected.h"

#include <string>
#include <string_view>
#include <vector>

namespace mithril::abi {

enum class SymbolStatus { implemented, provisional, unsupported };

struct SymbolContract {
    std::string name;
    std::string signature;
    std::string signatureHash;
    std::string api;
    std::string since;
    SymbolStatus status{SymbolStatus::provisional};
    std::string errorBehavior;
    std::string evidence;
};

class AbiManifest {
public:
    static core::Expected<AbiManifest, core::Error> parse(std::string_view json);

    [[nodiscard]] std::string_view formatVersion() const noexcept;
    [[nodiscard]] const std::vector<SymbolContract>& symbols() const noexcept;
    [[nodiscard]] const SymbolContract* find(std::string_view name) const noexcept;

private:
    std::string formatVersion_;
    std::vector<SymbolContract> symbols_;
};

[[nodiscard]] std::string_view toString(SymbolStatus status) noexcept;

} // namespace mithril::abi
