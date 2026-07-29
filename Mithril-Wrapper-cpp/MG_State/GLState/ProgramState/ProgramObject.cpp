// Mithril-Wrapper - MG_State/GLState/ProgramState/ProgramObject.cpp
//
// Implementation of the ProgramObject constructors. All other state is left to
// default member initializers (declared in ProgramObject.h), which carry the
// OpenGL 3.3 Core defaults inherited from the legacy `struct Program` in
// MG_State/State.h (no attached shaders, not linked, empty info log, empty
// reflection tables, empty SPIR-V).
#include "ProgramObject.h"

namespace mithril::glstate {

ProgramObject::ProgramObject() = default;

ProgramObject::ProgramObject(uint32_t id_) : id(id_) {}

} // namespace mithril::glstate
