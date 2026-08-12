// Mithril-Wrapper - gl/program_state.h
//
// Program/shader subsystem state (subview). Mirrors the role of MobileGlues
// gl/program.h, but the program/shader object tables still live in the
// centralized mithril::GLState. This struct holds a non-owning back-pointer so
// the distributed per-context table + accessor API is in place without
// disturbing existing g_state->programs / g_state->shaders /
// g_state->currentProgram access points.
//
// Subview pattern (Task 3.1): data stays in GLState; this wrapper only gives
// the program/shader subsystem its own per-context identity + thread_local
// current pointer. Access-point migration is a later task.
#ifndef MITHRIL_GL_PROGRAM_STATE_H
#define MITHRIL_GL_PROGRAM_STATE_H

#include "../MG_State/State.h"   // mithril::GLState

#ifdef __cplusplus
extern "C" {
#endif

    struct mg_program_state_t {
        // Non-owning back-pointer to the per-context GLState that still owns the
        // program/shader object tables + current program binding. Set by
        // mg_program_bind_context().
        mithril::GLState* state;

        // Convenience: the whole GLState, for paths that still reach the object
        // tables (state->programs[id] / state->shaders[id]) through this subview.
        mithril::GLState* gl_state() { return state; }
    };

#ifdef __cplusplus
}
#endif

#endif // MITHRIL_GL_PROGRAM_STATE_H
