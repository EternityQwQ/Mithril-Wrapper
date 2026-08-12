// Mithril-Wrapper - gl/sync_state.h
//
// Sync object subsystem state (reserved). Mirrors the slot MobileGlues reserves
// for GL sync objects (glFenceSync / glWaitSync / glDeleteSync). The sync object
// table still lives in the centralized mithril::GLState (syncObjects map).
//
// Task 3.1: empty placeholder so the per-context table + bind/forget hook shape
// is uniform across all subsystems. mg_sync_bind_context / mg_sync_forget_context
// are no-op hooks (declared in egl/context.h, implemented in egl/context.cpp).
// The struct will be filled in when the sync subsystem is distributed.
#ifndef MITHRIL_GL_SYNC_STATE_H
#define MITHRIL_GL_SYNC_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

    struct mg_sync_state_t {
        // Reserved: no fields yet. The sync object table still lives in GLState.
    };

#ifdef __cplusplus
}
#endif

#endif // MITHRIL_GL_SYNC_STATE_H
