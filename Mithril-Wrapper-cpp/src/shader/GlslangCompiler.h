#pragma once

#include "shader/ShaderCompiler.h"

namespace mithril::shader {

class GlslangCompiler final {
public:
    core::Expected<SpirvArtifact, ShaderDiagnostic> compile(
        const ShaderSource&, const PreprocessOptions&);
};

} // namespace mithril::shader
