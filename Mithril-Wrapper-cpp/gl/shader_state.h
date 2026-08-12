// Mithril-Wrapper - gl/shader_state.h
//
// Shader subsystem state (subview). Mirrors the role of MobileGlues
// gl/shader.h, but the shader object table still lives in the centralized
// mithril::GLState. This struct holds a non-owning back-pointer so the
// distributed per-context table + accessor API is in place without disturbing
// existing g_state->shaders access points.
//
// Subview pattern (Task 3.1): data stays in GLState; this wrapper only gives
// the shader subsystem its own per-context identity + thread_local current
// pointer. Access-point migration is a later task.
#ifndef MITHRIL_GL_SHADER_STATE_H
#define MITHRIL_GL_SHADER_STATE_H

#include "../MG_State/State.h"   // mithril::GLState

#ifdef __cplusplus
extern "C" {
#endif

    struct mg_shader_state_t {
        // Non-owning back-pointer to the per-context GLState that still owns the
        // shader object table. Set by mg_shader_bind_context().
        mithril::GLState* state;

        // Convenience: the whole GLState, for paths that still reach the object
        // table (state->shaders[id]) through this subview.
        mithril::GLState* gl_state() { return state; }
    };

#ifdef __cplusplus
}
#endif

#endif // MITHRIL_GL_SHADER_STATE_H
