#pragma once

#include "core/Expected.h"
#include "shader/ShaderError.h"
#include "shader/ShaderTypes.h"

namespace mithril::shader {

class SpirvCrossMslCompiler final {
public:
    core::Expected<MslArtifact, ShaderDiagnostic> translate(const SpirvArtifact&);
};

} // namespace mithril::shader
