// Mithril-Wrapper - MG_State/GLState/FramebufferState/FramebufferObject.h
//
// FramebufferObject: the per-name framebuffer record migrated from the flat
// MG_State/State.h `struct Framebuffer` (and the nested `struct FBOAttachment`).
// It carries the GL-side metadata for a single framebuffer object name: the
// per-color / depth / stencil attachment points, the draw-buffer mapping
// (glDrawBuffers) and the read buffer (glReadBuffer), plus a
// `markedForDeletion` flag.
//
// The Vulkan-side renderpass / VkFramebuffer / VkImageView handles for
// attached textures/renderbuffers are NOT owned here — the DirectVulkan
// backend resolves them at draw time from the texture/renderbuffer tables and
// its own framebuffer cache. FramebufferObject is purely the GL name-layer
// state, mirroring the historical split in State.h where `struct Framebuffer`
// held only GL fields.
//
// Shared API contract (mirrors BufferState / TextureState):
//   * namespace mithril::glstate, #pragma once, C++20.
//   * Fields are public so the state-machine components and the backend can
//     read and update them directly, matching the legacy `struct Framebuffer`
//     usage pattern.
#pragma once

#include <array>
#include <cstdint>

#include <GL/gl.h>

#include "../Common.h"

namespace mithril::glstate {

// Mirrors the legacy `struct FBOAttachment` in MG_State/State.h: a single
// framebuffer attachment point holding either a texture image (with its
// target / level / layer / layered flag) or a renderbuffer. The Vulkan-side
// VkImageView for an attached texture/renderbuffer is NOT stored here — it is
// resolved by the backend at draw time from the texture/renderbuffer tables.
struct FBOAttachment {
    uint32_t texture = 0;
    GLenum textarget = 0;
    GLint level = 0;
    GLint layer = 0;
    bool layered = false;
    uint32_t renderbuffer = 0;
};

// Mirrors the legacy `struct Framebuffer` in MG_State/State.h. `colors` and
// `drawBuffers` use std::array so they value-initialise cleanly and do not
// decay when the struct is copied/returned by value; `drawBuffers{GL_NONE}`
// matches the old `GLenum drawBuffers[kMaxColorAttachments] = {GL_NONE}` since
// GL_NONE == 0.
struct FramebufferObject {
    uint32_t id = 0;
    std::array<FBOAttachment, kMaxColorAttachments> colors{};
    FBOAttachment depth;
    FBOAttachment stencil;
    std::array<GLenum, kMaxColorAttachments> drawBuffers{GL_NONE};
    GLsizei drawBufferCount = 0;
    GLenum readBuffer = GL_NONE;
    bool markedForDeletion = false;

    FramebufferObject();
    explicit FramebufferObject(uint32_t id_);
};

} // namespace mithril::glstate
