// Mithril-Wrapper - gl/vertexattrib_state.h
//
// Vertex-attrib / VAO subsystem state (subview). Mirrors the role of MobileGlues
// gl/vertexattrib.h, but the VAO object table + current VAO binding still live
// in the centralized mithril::GLState. This struct holds a non-owning
// back-pointer so the distributed per-context table + accessor API is in place
// without disturbing existing g_state->vaos / g_state->currentVAO access points.
//
// Subview pattern (Task 3.1): data stays in GLState; this wrapper only gives
// the VAO subsystem its own per-context identity + thread_local current
// pointer. Access-point migration is a later task.
#ifndef MITHRIL_GL_VERTEXATTRIB_STATE_H
#define MITHRIL_GL_VERTEXATTRIB_STATE_H

#include "../MG_State/State.h"   // mithril::GLState, VertexArray

#ifdef __cplusplus
extern "C" {
#endif

    struct mg_vertexattrib_state_t {
        // Non-owning back-pointer to the per-context GLState that still owns the
        // VAO object table + current VAO binding. Set by
        // mg_vertexattrib_bind_context().
        mithril::GLState* state;

        // Access the VAO object table. Returns nullptr for an unknown id,
        // matching the existing state_get_vao() contract.
        mithril::VertexArray* vao(GLuint id) {
            auto it = state->vaos.find(id);
            return it != state->vaos.end() ? &it->second : nullptr;
        }

        // Convenience: the whole GLState, for paths that still reach the current
        // VAO (state->currentVAO) through this subview.
        mithril::GLState* gl_state() { return state; }
    };

#ifdef __cplusplus
}
#endif

#endif // MITHRIL_GL_VERTEXATTRIB_STATE_H
