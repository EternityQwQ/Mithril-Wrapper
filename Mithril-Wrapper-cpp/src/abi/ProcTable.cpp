#include "abi/ProcTable.h"

namespace mithril::abi {

ProcAddress ProcTable::lookup(std::string_view name) const noexcept {
    if (name.empty()) return nullptr;
    for (const ProcEntry& entry : entries_) {
        if (entry.name == name) {
            return entry.status == SymbolStatus::implemented ? entry.address : nullptr;
        }
    }
    return nullptr;
}

} // namespace mithril::abi
