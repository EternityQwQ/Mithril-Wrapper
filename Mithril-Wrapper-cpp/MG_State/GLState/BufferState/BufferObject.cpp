// Mithril-Wrapper - MG_State/GLState/BufferState/BufferObject.cpp
//
// Implementation of the BufferObject constructors. All other state is left to
// default member initializers (declared in BufferObject.h), which carry the
// OpenGL 3.3 Core defaults inherited from the legacy `struct Buffer` in
// MG_State/State.h (lastTarget = GL_ARRAY_BUFFER, usage = GL_STATIC_DRAW,
// empty data, no active map).
#include "BufferObject.h"

namespace mithril::glstate {

BufferObject::BufferObject() = default;

BufferObject::BufferObject(uint32_t id_) : id(id_) {}

} // namespace mithril::glstate
