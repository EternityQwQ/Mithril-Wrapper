// Mithril-Wrapper - MG_State/GLState/FramebufferState/FramebufferState.h
//
// FramebufferState: the framebuffer-object table + draw/read binding owner of
// the modular OpenGL state machine. It owns the GL name -> SharedPtr<FramebufferObject>
// table, the name allocator (m_nextName), the current draw/read FBO names and a
// version counter that is bumped whenever a binding or the EGL default
// framebuffer changes, so a backend can cheaply tell whether the cached
// renderpass / framebuffer / descriptor state needs rebuilding.
//
// Shared API contract (mirrors BufferState / TextureState):
//   * namespace mithril::glstate, #pragma once, C++20.
//   * Version counter: uint16_t m_version = 0; GetVersion(); ++m_version on
//     every binding change (bind / mark-for-deletion) and on EGL default
//     framebuffer injection.
//   * Object table entry points use the unified names GenFramebufferNames /
//     GetFramebufferObject / CreateFramebufferObject /
//     MarkFramebufferForDeletion / ValidateFramebufferName /
//     ValidateFramebufferObject.
//   * Objects are owned via SharedPtr (mithril::glstate::SharedPtr from
//     Common.h). Get/Create return a const reference to the stored SharedPtr
//     (or to a static null SharedPtr when absent), so callers can hold the
//     reference without copying the refcount.
//
// Binding model note:
//   Unlike the BufferState / TextureState components, framebuffer binding is
//   split into draw vs read targets (glBindFramebuffer accepts GL_FRAMEBUFFER
//   / GL_DRAW_FRAMEBUFFER / GL_READ_FRAMEBUFFER). A full BindingSlot with
//   per-slot touched bits is not needed here — the backend reads the
//   currentDrawFBO / currentReadFBO names directly — so two simple uint32_t
//   fields plus the version counter suffice, matching the legacy GLState
//   currentDrawFBO / currentReadFBO fields.
//
// EGL default framebuffer:
//   The EGL layer (egl/egl.mm) installs the current swapchain image's
//   VkImageView / VkImage handles onto FramebufferState so that GL commands
//   issued against framebuffer 0 render directly into the on-screen drawable.
//   These handles are OWNED BY the EGLSurface (swapchain), not by the GL
//   object table — the GL texture/sampler paths never touch them. The backend
//   reads these fields when FBO 0 is the render target.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <GL/gl.h>
#include <vulkan/vulkan.h>

#include "../Common.h"
#include "FramebufferObject.h"

namespace mithril::glstate {

class FramebufferState {
public:
    FramebufferState();

    // Allocate `n` fresh framebuffer names from m_nextName. Names are reserved
    // only; no FramebufferObject is created until BindFramebuffer /
    // CreateFramebufferObject is called. Appends to `out` (does not clear it,
    // matching glGenFramebuffers).
    void GenFramebufferNames(uint32_t n, std::vector<uint32_t>& out);

    // Look up a framebuffer object by name. Name 0 returns the pre-populated
    // default framebuffer. An absent name returns a static null SharedPtr.
    const SharedPtr<FramebufferObject>& GetFramebufferObject(uint32_t index);

    // Look up a framebuffer object by name, creating it (with the given id) if
    // the name is allocated but has no object yet. Name 0 returns the existing
    // default framebuffer (never replaces it).
    const SharedPtr<FramebufferObject>& CreateFramebufferObject(uint32_t index);

    // GL name-layer deletion. Erases the name from the object table; the
    // underlying Vulkan resources (renderpass / VkFramebuffer / image views)
    // are released asynchronously by the backend disposal queue once in-flight
    // GPU work referencing them completes, so no backend handle is freed here.
    // Name 0 (the default framebuffer) is never deleted — it is owned by the
    // EGL surface / context, not the GL name layer. If the deleted name is
    // the current draw/read binding, that binding falls back to 0. Bumps
    // m_version.
    void MarkFramebufferForDeletion(uint32_t index);

    // True if `index` is an allocated name (present in the object table). Name
    // 0 is always valid (pre-populated in the constructor).
    bool ValidateFramebufferName(uint32_t index) const;

    // True if `index` is an allocated name AND has a live FramebufferObject.
    bool ValidateFramebufferObject(uint32_t index) const;

    // Current draw/read framebuffer names (0 == default framebuffer).
    uint32_t GetCurrentDrawFBO() const;
    uint32_t GetCurrentReadFBO() const;

    // Bind a framebuffer name to a target. GL_FRAMEBUFFER sets both draw and
    // read; GL_DRAW_FRAMEBUFFER sets only draw; GL_READ_FRAMEBUFFER sets only
    // read. index == 0 selects the default framebuffer (the EGL surface). A
    // non-zero name is materialised on demand. Bumps m_version on every call.
    void BindFramebuffer(GLenum target, uint32_t index);

    const SharedPtr<FramebufferObject>& GetCurrentDrawFramebuffer() const;
    const SharedPtr<FramebufferObject>& GetCurrentReadFramebuffer() const;

    // ---- EGL default-framebuffer injection ----
    // Installed by the EGL layer (egl/egl.mm) when a surface is made current.
    // The VkImageView/VkImage handles are owned by the EGLSurface (swapchain),
    // NOT by the GL object table; the GL texture/sampler paths never touch
    // them. The backend reads these fields when FBO 0 is the render target.
    // Bumps m_version so the backend rebuilds its FBO-0 renderpass / framebuffer.
    void SetEglDefaultFramebuffer(VkImageView color, VkImageView depth,
                                  VkImage colorImg, VkImage depthImg,
                                  VkFormat colorFmt, VkFormat depthFmt,
                                  int w, int h);

    // EGL default framebuffer handles. Public so the backend reads them
    // directly when FBO 0 is bound, matching the legacy GLState layout.
    VkImageView eglDefaultColor = VK_NULL_HANDLE;
    VkImageView eglDefaultDepth = VK_NULL_HANDLE;
    VkImage eglDefaultColorImage = VK_NULL_HANDLE;
    VkImage eglDefaultDepthImage = VK_NULL_HANDLE;
    VkFormat eglDefaultColorFormat = VK_FORMAT_UNDEFINED;
    VkFormat eglDefaultDepthFormat = VK_FORMAT_UNDEFINED;
    int eglDefaultWidth = 0;
    int eglDefaultHeight = 0;

    uint16_t GetVersion() const;

private:
    std::unordered_map<uint32_t, SharedPtr<FramebufferObject>> m_objects;
    uint32_t m_nextName = 1;
    uint16_t m_version = 0;
    uint32_t m_currentDrawFBO = 0;
    uint32_t m_currentReadFBO = 0;
};

} // namespace mithril::glstate
