// Mithril-Wrapper - gl/framebuffer_state.h
//
// Framebuffer subsystem state (subview). Mirrors the role of MobileGlues
// gl/framebuffer.h, but the framebuffer object table + draw/read FBO bindings
// still live in the centralized mithril::GLState. This struct holds a
// non-owning back-pointer so the distributed per-context table + accessor API
// is in place without disturbing existing g_state->framebuffers /
// g_state->currentDrawFBO access points.
//
// Subview pattern (Task 3.1): data stays in GLState; this wrapper only gives
// the framebuffer subsystem its own per-context identity + thread_local
// current pointer. Access-point migration is a later task.
#ifndef MITHRIL_GL_FRAMEBUFFER_STATE_H
#define MITHRIL_GL_FRAMEBUFFER_STATE_H

#include "../MG_State/State.h"   // mithril::GLState

#ifdef __cplusplus
extern "C" {
#endif

    struct mg_framebuffer_state_t {
        // Non-owning back-pointer to the per-context GLState that still owns the
        // framebuffer object table + draw/read bindings. Set by
        // mg_framebuffer_bind_context().
        mithril::GLState* state;

        // Convenience: the whole GLState, for paths that still reach the object
        // table (state->framebuffers[id]) or the current draw/read FBO through
        // this subview.
        mithril::GLState* gl_state() { return state; }
    };

#ifdef __cplusplus
}
#endif

#endif // MITHRIL_GL_FRAMEBUFFER_STATE_H
