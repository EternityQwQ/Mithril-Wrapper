// Mithril-Wrapper - MG_State/State.cpp
//
// Implementation of the modular GLContext aggregate together with the
// state_init / state_create / state_destroy compatibility entry points used by
// the EGL layer (egl/egl.mm) and the implicit global-context bootstrap
// (MITHRIL_ENSURE_INIT / proc_init).
#include "State.h"

namespace mithril {

// Global current context pointer. The EGL layer swaps this inside
// eglMakeCurrent; the implicit global context is created lazily by state_init().
GLContext* g_state = nullptr;

// Each domain component's constructor pre-populates the GL 3.3 Core defaults
// (VertexArrayState installs the default VAO name 0, FramebufferState installs
// the default framebuffer name 0, RenderState seeds the spec defaults, ...), so
// GLContext needs no explicit initialisation beyond default-constructing its
// members.
GLContext::GLContext() = default;

bool state_init() {
    // Idempotent: a non-null g_state means the implicit global context is
    // already up. The legacy flat GLState carried an `initialized` flag; the
    // modular state machine replaces that with the g_state-non-null check.
    if (g_state) return true;
    g_state = state_create();
    return true;
}

GLContext* state_create() {
    // Heap-own a fresh, independent GLContext. Does NOT touch g_state — the EGL
    // layer is responsible for swapping g_state to point at the chosen
    // context's state inside eglMakeCurrent.
    return new GLContext();
}

void state_destroy(GLContext* s) {
    if (!s) return;
    // The EGL default color/depth VkImageViews are owned by the EGLSurface
    // (swapchain), not by the GLContext, so they are not released here. Clear
    // g_state if it was pointing at the context being destroyed so global
    // access does not dangle; the EGL layer otherwise manages its own g_state
    // swapping.
    if (s == g_state) {
        g_state = nullptr;
    }
    delete s;
}

} // namespace mithril
