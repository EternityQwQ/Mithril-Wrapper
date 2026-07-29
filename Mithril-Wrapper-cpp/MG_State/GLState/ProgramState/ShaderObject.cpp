// Mithril-Wrapper - MG_State/GLState/ProgramState/ShaderObject.cpp
//
// Implementation of the ShaderObject constructors. All other state is left to
// default member initializers (declared in ShaderObject.h), which carry the
// OpenGL 3.3 Core defaults inherited from the legacy `struct Shader` in
// MG_State/State.h (type = GL_VERTEX_SHADER, empty source, not compiled,
// empty info log, empty SPIR-V).
#include "ShaderObject.h"

namespace mithril::glstate {

ShaderObject::ShaderObject() = default;

ShaderObject::ShaderObject(uint32_t id_) : id(id_) {}

ShaderObject::ShaderObject(uint32_t id_, GLenum type_) : id(id_), type(type_) {}

} // namespace mithril::glstate
