// Mithril-Wrapper - MG_State/GLState/FramebufferState/FramebufferObject.cpp
//
// Implementation of the FramebufferObject constructors. All other state is
// left to default member initializers (declared in FramebufferObject.h),
// which carry the OpenGL 3.3 Core defaults inherited from the legacy
// `struct Framebuffer` in MG_State/State.h (empty attachments, all draw
// buffers GL_NONE, drawBufferCount 0, readBuffer GL_NONE).
#include "FramebufferObject.h"

namespace mithril::glstate {

FramebufferObject::FramebufferObject() = default;

FramebufferObject::FramebufferObject(uint32_t id_) : id(id_) {}

} // namespace mithril::glstate
