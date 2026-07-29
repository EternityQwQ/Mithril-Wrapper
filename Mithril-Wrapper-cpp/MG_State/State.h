// Mithril-Wrapper - MG_State/State.h
//
// GLContext: the aggregate of the modular OpenGL state machine. It owns the
// eight domain components (ErrorState, RenderState, BufferState,
// VertexArrayState, TextureState, ProgramState, FramebufferState,
// SamplerState) directly as value members and exposes per-domain accessors so
// the GL entry-point layer (MG_Impl) and the backend (MG_Backend) can reach
// each component without going through a flat god-struct.
//
// This header replaces the former flat MG_State/State.h `struct GLState`. The
// legacy `mithril::VertexArray` / `Buffer` / `Texture` / `Shader` / `Program` /
// `Framebuffer` / `FBOAttachment` / `Uniform` / `Attrib` records and the
// `state_get_*` / `state_set_error` / `state_take_error` / `state_gen_names`
// free functions have been removed: their typed replacements live in the
// per-domain headers under MG_State/GLState/<Domain>/ (e.g.
// glstate::BufferObject, glstate::TextureObject). Call sites will be migrated
// to the new API during the cutover tasks.
//
// Shared API contract:
//   * namespace mithril (NOT mithril::glstate); the domain components
//     themselves live in mithril::glstate.
//   * #pragma once, C++20.
//   * GLContext is value-semantics composition: it holds each domain component
//     as a direct member (not a pointer). Each domain component's constructor
//     pre-populates the GL-required defaults (default VAO name 0 for
//     VertexArrayState, default framebuffer name 0 for FramebufferState), so
//     GLContext's own constructor is trivial.
#pragma once

#include <cstdint>
#include <utility>

#include <GL/gl.h>

#include "GLState/ErrorState/Error.h"
#include "GLState/RenderState/RenderState.h"
#include "GLState/BufferState/BufferState.h"
#include "GLState/VertexArrayState/VertexArrayState.h"
#include "GLState/TextureState/TextureState.h"
#include "GLState/ProgramState/ProgramState.h"
#include "GLState/FramebufferState/FramebufferState.h"
#include "GLState/SamplerState/SamplerState.h"

namespace mithril {

// GLContext: value-semantics aggregate of the modular OpenGL state machine.
// Each domain component is held directly (not via pointer) and is pre-populated
// with the GL 3.3 Core defaults by its own constructor — VertexArrayState
// installs the default VAO (name 0) and FramebufferState installs the default
// framebuffer (name 0) — so GLContext needs no explicit initialisation beyond
// default-constructing its members.
class GLContext {
public:
    // Constructs a context whose domain components already carry the GL 3.3
    // Core defaults (default VAO 0 bound, default FBO 0 bound, render state at
    // spec defaults, no error pending, empty object tables).
    GLContext();

    // ---- Domain accessors (mutable) ----
    glstate::ErrorState& GetErrorState() { return m_errorState; }
    glstate::RenderState& GetRenderState() { return m_renderState; }
    glstate::BufferState& GetBufferState() { return m_bufferState; }
    glstate::VertexArrayState& GetVertexArrayState() { return m_vertexArrayState; }
    glstate::TextureState& GetTextureState() { return m_textureState; }
    glstate::ProgramState& GetProgramState() { return m_programState; }
    glstate::FramebufferState& GetFramebufferState() { return m_framebufferState; }
    glstate::SamplerState& GetSamplerState() { return m_samplerState; }

    // ---- Domain accessors (const) ----
    const glstate::ErrorState& GetErrorState() const { return m_errorState; }
    const glstate::RenderState& GetRenderState() const { return m_renderState; }
    const glstate::BufferState& GetBufferState() const { return m_bufferState; }
    const glstate::VertexArrayState& GetVertexArrayState() const { return m_vertexArrayState; }
    const glstate::TextureState& GetTextureState() const { return m_textureState; }
    const glstate::ProgramState& GetProgramState() const { return m_programState; }
    const glstate::FramebufferState& GetFramebufferState() const { return m_framebufferState; }
    const glstate::SamplerState& GetSamplerState() const { return m_samplerState; }

    // ---- Convenience: error recording (used by the GL entry-point layer) ----
    // Record a typed GL error against the current context. Per GL semantics the
    // first recorded error is retained; later codes are dropped. `info` carries
    // optional diagnostic context (message / source location).
    void RecordError(glstate::ErrorCode code, glstate::ErrorInfo info = {}) {
        m_errorState.RecordError(code, std::move(info));
    }

    // Consume the pending GL error and translate it to the GLenum returned by
    // glGetError (GL_NO_ERROR when none was pending).
    GLenum PopGLErrorAsGLenum() {
        return m_errorState.ErrorCodeToGL(m_errorState.PopGLError());
    }

private:
    glstate::ErrorState m_errorState;
    glstate::RenderState m_renderState;
    glstate::BufferState m_bufferState;
    glstate::VertexArrayState m_vertexArrayState;
    glstate::TextureState m_textureState;
    glstate::ProgramState m_programState;
    glstate::FramebufferState m_framebufferState;
    glstate::SamplerState m_samplerState;
};

// Global current context pointer. The EGL layer (egl/egl.mm) swaps this inside
// eglMakeCurrent so each EGLContext's GLContext is current on its bound
// surface. The implicit global context is created lazily by state_init().
extern GLContext* g_state;

// Initialise the implicit global context (idempotent). If g_state is null a
// fresh GLContext is allocated and assigned to g_state. Returns true.
bool state_init();

// Allocate a fresh, independent GLContext (used by the EGL layer so each
// EGLContext owns its own state). The returned pointer is heap-owned and must
// be released with state_destroy(). Does NOT touch g_state — the EGL layer is
// responsible for swapping g_state to point at the chosen context's state
// inside eglMakeCurrent.
GLContext* state_create();

// Release a GLContext previously returned by state_create(). The EGL default
// color/depth VkImageViews are owned by the EGLSurface (swapchain), not by the
// GLContext, so they are not released here. If `s` is the current global
// context (== g_state), g_state is cleared to nullptr so stale global access is
// avoided; the caller is otherwise responsible for g_state bookkeeping.
void state_destroy(GLContext* s);

} // namespace mithril
