#pragma once

#include "abi/AbiManifest.h"

#include <span>
#include <string_view>

namespace mithril::abi {

using ProcAddress = void (*)();

struct ProcEntry {
    std::string_view name;
    SymbolStatus status{SymbolStatus::unsupported};
    ProcAddress address{};
};

class ProcTable {
public:
    explicit ProcTable(std::span<const ProcEntry> entries) : entries_(entries) {}
    [[nodiscard]] ProcAddress lookup(std::string_view name) const noexcept;

private:
    std::span<const ProcEntry> entries_;
};

} // namespace mithril::abi
