#include "shader/ShaderTypes.h"

#include <algorithm>

namespace mithril::shader {

ShaderDiagnostic sanitizeDiagnostic(ShaderDiagnostic diagnostic, std::size_t maxLength) {
    std::replace(diagnostic.message.begin(), diagnostic.message.end(), '\r', ' ');
    if (diagnostic.message.size() > maxLength) {
        diagnostic.message.resize(maxLength);
        diagnostic.message += " [truncated]";
    }
    const std::size_t slash = diagnostic.sourceName.find_last_of("/\\");
    if (slash != std::string::npos) diagnostic.sourceName.erase(0, slash + 1U);
    return diagnostic;
}

ShaderCacheKey makeShaderCacheKey(const ShaderSource& source,
                                  const PreprocessOptions& options) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char value : source.source) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    for (const unsigned char value : source.entryPoint) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    return ShaderCacheKey{hash, 1, options.targetVersion, source.stage, options.flipY};
}

} // namespace mithril::shader
