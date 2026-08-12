// Mithril-Wrapper - gl/enable_state.h
//
// Virtual enable state (mirrors MobileGlues gl/enable.h).
//
// This is the ONLY subsystem state struct that is STANDALONE — it owns its own
// data fields rather than holding a mithril::GLState* back-pointer. Enable
// (capability) state is per-context by definition (GL scopes glEnable/glDisable
// to the current context), so it is the natural first piece to distribute out
// of the centralized GLState.
//
// Every GL 4.6 enable capability lives in one table here. Queries read it, the
// enable/disable entry points write it, and whether the driver is also told is
// a per-capability property rather than an accident of which enum the driver
// happens to know.
//
// Task 3.1: structure definition only. The bind hook (mg_enable_bind_context)
// copies the current GLState capability bools into this struct; the accessor
// (mg_enable_current) returns the per-context entry. Both live in
// egl/context.{h,cpp}. The driver-sync path (mg_enable_sync_driver) is reserved
// for a future task and is intentionally not wired here.
#ifndef MITHRIL_GL_ENABLE_STATE_H
#define MITHRIL_GL_ENABLE_STATE_H

#include <GL/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

    // Scalar capabilities. GL_CLIP_DISTANCEi is a contiguous enum range rather
    // than a single value and is kept as a bitmask instead of occupying slots.
    enum mg_cap_index : int {
        MGC_BLEND = 0,
        MGC_COLOR_LOGIC_OP,
        MGC_CULL_FACE,
        MGC_DEBUG_OUTPUT,
        MGC_DEBUG_OUTPUT_SYNCHRONOUS,
        MGC_DEPTH_CLAMP,
        MGC_DEPTH_TEST,
        MGC_DITHER,
        MGC_FRAMEBUFFER_SRGB,
        MGC_LINE_SMOOTH,
        MGC_MULTISAMPLE,
        MGC_POLYGON_OFFSET_FILL,
        MGC_POLYGON_OFFSET_LINE,
        MGC_POLYGON_OFFSET_POINT,
        MGC_POLYGON_SMOOTH,
        MGC_PRIMITIVE_RESTART,
        MGC_PRIMITIVE_RESTART_FIXED_INDEX,
        MGC_PROGRAM_POINT_SIZE,
        MGC_RASTERIZER_DISCARD,
        MGC_SAMPLE_ALPHA_TO_COVERAGE,
        MGC_SAMPLE_ALPHA_TO_ONE,
        MGC_SAMPLE_COVERAGE,
        MGC_SAMPLE_MASK,
        MGC_SAMPLE_SHADING,
        MGC_SCISSOR_TEST,
        MGC_STENCIL_TEST,
        MGC_TEXTURE_CUBE_MAP_SEAMLESS,
        MGC_COUNT
    };

    // GL 4.6 requires at least 8 clip distances and at least 8 draw buffers.
    // Anything the driver reports above these is clamped; the layer never claims
    // more than it can store.
    enum { MG_MAX_CLIP_DISTANCES = 8, MG_MAX_DRAW_BUFFERS = 16, MG_MAX_VIEWPORTS = 16 };

    struct mg_enable_state_t {
        GLboolean scalar[MGC_COUNT];
        GLboolean blend_indexed[MG_MAX_DRAW_BUFFERS];   // GL_BLEND per draw buffer
        GLboolean scissor_indexed[MG_MAX_VIEWPORTS];    // GL_SCISSOR_TEST per viewport
        GLuint   clip_distance_mask;                    // GL_CLIP_DISTANCE0..7
        GLuint   primitive_restart_index;               // glPrimitiveRestartIndex
        bool     initialised;
        bool     driver_synced;  // mg_enable_sync_driver() has run for this context
    };

#ifdef __cplusplus
}
#endif

#endif // MITHRIL_GL_ENABLE_STATE_H
