#include "abi/AbiManifest.h"

#include <algorithm>
#include <regex>
#include <unordered_set>

namespace mithril::abi {
namespace {

core::Error invalid(std::string_view text) {
    return core::Error::make(core::ErrorDomain::contract, core::ErrorCode::invalid_argument, text);
}

bool validHash(std::string_view hash) {
    return hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](char value) {
        return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
    });
}

} // namespace

core::Expected<AbiManifest, core::Error> AbiManifest::parse(std::string_view jsonView) {
    const std::string json(jsonView);
    const std::regex versionPattern(R"json("format_version"\s*:\s*"([^"]+)")json");
    std::smatch versionMatch;
    if (!std::regex_search(json, versionMatch, versionPattern)) {
        return core::Expected<AbiManifest, core::Error>::failure(invalid("missing format_version"));
    }

    const std::regex objectPattern(
        R"json(\{\s*"name"\s*:\s*"([^"]+)"\s*,\s*"signature"\s*:\s*"([^"]*)"\s*,\s*"signature_hash"\s*:\s*"([^"]*)"\s*,\s*"api"\s*:\s*"([^"]+)"\s*,\s*"since"\s*:\s*"([^"]+)"\s*,\s*"status"\s*:\s*"([^"]+)"\s*,\s*"error_behavior"\s*:\s*"([^"]*)"\s*,\s*"evidence"\s*:\s*"([^"]*)"\s*\})json");

    AbiManifest result;
    result.formatVersion_ = versionMatch[1].str();
    std::unordered_set<std::string> names;
    for (std::sregex_iterator it(json.begin(), json.end(), objectPattern), end; it != end; ++it) {
        SymbolContract symbol;
        symbol.name = (*it)[1].str();
        symbol.signature = (*it)[2].str();
        symbol.signatureHash = (*it)[3].str();
        symbol.api = (*it)[4].str();
        symbol.since = (*it)[5].str();
        const std::string status = (*it)[6].str();
        symbol.errorBehavior = (*it)[7].str();
        symbol.evidence = (*it)[8].str();

        if (status == "implemented") symbol.status = SymbolStatus::implemented;
        else if (status == "provisional") symbol.status = SymbolStatus::provisional;
        else if (status == "unsupported") symbol.status = SymbolStatus::unsupported;
        else return core::Expected<AbiManifest, core::Error>::failure(invalid("unknown symbol status"));

        if (symbol.name.empty() || symbol.signature.empty() || !validHash(symbol.signatureHash)) {
            return core::Expected<AbiManifest, core::Error>::failure(invalid("invalid symbol contract"));
        }
        if (symbol.api != "GL" && symbol.api != "EGL" && symbol.api != "GLX") {
            return core::Expected<AbiManifest, core::Error>::failure(invalid("unknown symbol api"));
        }
        if (!names.insert(symbol.name).second) {
            return core::Expected<AbiManifest, core::Error>::failure(invalid("duplicate symbol"));
        }
        result.symbols_.push_back(std::move(symbol));
    }

    const std::regex namePattern(R"json("name"\s*:)json");
    const std::size_t declaredNames = static_cast<std::size_t>(
        std::distance(std::sregex_iterator(json.begin(), json.end(), namePattern), std::sregex_iterator()));
    if (result.symbols_.empty() || declaredNames != result.symbols_.size()) {
        return core::Expected<AbiManifest, core::Error>::failure(invalid("malformed symbol entry"));
    }
    std::sort(result.symbols_.begin(), result.symbols_.end(),
              [](const SymbolContract& left, const SymbolContract& right) { return left.name < right.name; });
    return result;
}

std::string_view AbiManifest::formatVersion() const noexcept { return formatVersion_; }
const std::vector<SymbolContract>& AbiManifest::symbols() const noexcept { return symbols_; }

const SymbolContract* AbiManifest::find(std::string_view name) const noexcept {
    const auto it = std::lower_bound(symbols_.begin(), symbols_.end(), name,
        [](const SymbolContract& symbol, std::string_view candidate) { return symbol.name < candidate; });
    return it != symbols_.end() && it->name == name ? &*it : nullptr;
}

std::string_view toString(SymbolStatus status) noexcept {
    switch (status) {
        case SymbolStatus::implemented: return "implemented";
        case SymbolStatus::provisional: return "provisional";
        case SymbolStatus::unsupported: return "unsupported";
    }
    return "unsupported";
}

} // namespace mithril::abi
