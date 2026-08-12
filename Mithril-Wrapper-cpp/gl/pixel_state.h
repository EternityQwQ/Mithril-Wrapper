// Mithril-Wrapper - gl/pixel_state.h
//
// Pixel-store subsystem state (subview). Mirrors the role of MobileGlues
// gl/pixel.h, but the pack/unpack pixel-store block still lives in the
// centralized mithril::GLState (PixelStoreState). This struct holds a
// non-owning back-pointer so the distributed per-context table + accessor API
// is in place without disturbing existing g_state->pixelStore access points.
//
// Subview pattern (Task 3.1): data stays in GLState; this wrapper only gives
// the pixel-store subsystem its own per-context identity + thread_local
// current pointer. Access-point migration is a later task.
#ifndef MITHRIL_GL_PIXEL_STATE_H
#define MITHRIL_GL_PIXEL_STATE_H

#include "../MG_State/State.h"   // mithril::GLState, PixelStoreState

#ifdef __cplusplus
extern "C" {
#endif

    struct mg_pixel_state_t {
        // Non-owning back-pointer to the per-context GLState that still owns the
        // pack/unpack pixel-store block. Set by mg_pixel_bind_context().
        mithril::GLState* state;

        // Access the pixel-store block (pack + unpack alignment / row length /
        // swap bytes / ...). Mirrors the mg_pixel_store_set / mg_pixel_store_query
        // helpers in MobileGlues gl/pixel.h, reached through this subview.
        mithril::PixelStoreState& pixel_store() { return state->pixelStore; }

        // Convenience: the whole GLState.
        mithril::GLState* gl_state() { return state; }
    };

#ifdef __cplusplus
}
#endif

#endif // MITHRIL_GL_PIXEL_STATE_H
