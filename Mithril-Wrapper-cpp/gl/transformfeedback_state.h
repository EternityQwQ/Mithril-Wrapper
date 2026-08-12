// Mithril-Wrapper - gl/transformfeedback_state.h
//
// Transform feedback subsystem state (reserved). Mirrors the slot MobileGlues
// reserves for GL transform feedback objects (glGenTransformFeedbacks /
// glBeginTransformFeedback / glEndTransformFeedback / glPauseTransformFeedback).
// The transform-feedback object table still lives in the centralized
// mithril::GLState (transformFeedbacks map).
//
// Task 3.1: empty placeholder so the per-context table + bind/forget hook shape
// is uniform across all subsystems. mg_transformfeedback_bind_context /
// mg_transformfeedback_forget_context are no-op hooks (declared in
// egl/context.h, implemented in egl/context.cpp). The struct will be filled in
// when the transform-feedback subsystem is distributed.
#ifndef MITHRIL_GL_TRANSFORMFEEDBACK_STATE_H
#define MITHRIL_GL_TRANSFORMFEEDBACK_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

    struct mg_transformfeedback_state_t {
        // Reserved: no fields yet. The transform-feedback object table still
        // lives in GLState.
    };

#ifdef __cplusplus
}
#endif

#endif // MITHRIL_GL_TRANSFORMFEEDBACK_STATE_H
