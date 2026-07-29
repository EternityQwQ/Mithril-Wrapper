// Mithril-Wrapper - MG_State/GLState/VertexArrayState/VertexArrayObject.cpp
//
// Implementation of the VertexArrayObject constructors. All other state is
// left to default member initializers (declared in VertexArrayObject.h), which
// carry the OpenGL 3.3 Core defaults inherited from the legacy `struct
// VertexArray` / `struct VertexAttrib` in MG_State/State.h (disabled arrays,
// size 4, type GL_FLOAT, no bound element buffer).
#include "VertexArrayObject.h"

namespace mithril::glstate {

VertexArrayObject::VertexArrayObject() = default;

VertexArrayObject::VertexArrayObject(uint32_t id_) : id(id_) {}

} // namespace mithril::glstate
