// Mithril-Wrapper - gl/texture_state.h
//
// Texture subsystem state (subview). Mirrors the role of MobileGlues
// gl/texture.h, but the texture object table + per-unit/per-target binding
// slots still live in the centralized mithril::GLState. This struct holds a
// non-owning back-pointer so the distributed per-context table + accessor API
// is in place without disturbing existing g_state->textures /
// g_state->textureBindings access points.
//
// Subview pattern (Task 3.1): data stays in GLState; this wrapper only gives
// the texture subsystem its own per-context identity + thread_local current
// pointer. Access-point migration is a later task.
#ifndef MITHRIL_GL_TEXTURE_STATE_H
#define MITHRIL_GL_TEXTURE_STATE_H

#include "../MG_State/State.h"   // mithril::GLState

#ifdef __cplusplus
extern "C" {
#endif

    struct mg_texture_state_t {
        // Non-owning back-pointer to the per-context GLState that still owns the
        // texture object table + per-unit/per-target binding slots. Set by
        // mg_texture_bind_context().
        mithril::GLState* state;

        // Convenience: the whole GLState, for paths that still reach the object
        // table (state->textures[id]) or binding slots (state->textureBindings)
        // through this subview.
        mithril::GLState* gl_state() { return state; }
    };

#ifdef __cplusplus
}
#endif

#endif // MITHRIL_GL_TEXTURE_STATE_H
