#pragma once

#include "core/Expected.h"
#include "shader/ShaderError.h"
#include "shader/ShaderTypes.h"

#include <string>

namespace mithril::shader {

core::Expected<std::string, ShaderDiagnostic> preprocessGlsl(
    const ShaderSource&, const PreprocessOptions&);

} // namespace mithril::shader
