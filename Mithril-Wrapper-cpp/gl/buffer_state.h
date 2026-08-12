// Mithril-Wrapper - gl/buffer_state.h
//
// Buffer subsystem state (subview). Mirrors the role of MobileGlues gl/buffer.h,
// but unlike MobileGlues the actual buffer object table + binding slots still
// live in the centralized mithril::GLState. This struct holds a non-owning
// back-pointer to the current context's GLState so the distributed per-context
// table + accessor API is in place without disturbing existing g_state->buffers
// / g_state->bufferBindings access points.
//
// Subview pattern (Task 3.1): the data stays in GLState; this wrapper only
// gives the buffer subsystem its own per-context identity + thread_local
// current pointer so that bind/forget hooks and mg_buffer_current() can be
// routed through it. Access-point migration (g_state->...  ->  mg_buffer_current()
// ->state->...) is a later, independent task and is explicitly out of scope here.
#ifndef MITHRIL_GL_BUFFER_STATE_H
#define MITHRIL_GL_BUFFER_STATE_H

#include "../MG_State/State.h"   // mithril::GLState, BindingSlot, BufferTarget

#ifdef __cplusplus
extern "C" {
#endif

    struct mg_buffer_state_t {
        // Non-owning back-pointer to the per-context GLState that still owns the
        // buffer object table + binding slots. Set by mg_buffer_bind_context().
        mithril::GLState* state;

        // Access the non-indexed per-target buffer binding table.
        // Indexed by mithril::BufferTarget. (GL_ELEMENT_ARRAY_BUFFER is NOT here;
        // it lives in the current VAO.)
        mithril::BindingSlot& binding(mithril::BufferTarget t) {
            return state->bufferBindings[static_cast<int>(t)];
        }
        // Convenience: the whole GLState, for the paths that still reach the
        // object table (state->buffers[id]) through this subview.
        mithril::GLState* gl_state() { return state; }
    };

#ifdef __cplusplus
}
#endif

#endif // MITHRIL_GL_BUFFER_STATE_H
