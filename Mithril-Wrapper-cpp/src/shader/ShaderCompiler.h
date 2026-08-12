#pragma once

#include "core/Expected.h"
#include "shader/ShaderError.h"
#include "shader/ShaderTypes.h"

namespace mithril::shader {

class ShaderCompiler {
public:
    virtual ~ShaderCompiler() = default;
    virtual core::Expected<SpirvArtifact, ShaderDiagnostic> compile(
        const ShaderSource&, const PreprocessOptions&) = 0;
    virtual core::Expected<MslArtifact, ShaderDiagnostic> translate(
        const SpirvArtifact&) = 0;
};

} // namespace mithril::shader
