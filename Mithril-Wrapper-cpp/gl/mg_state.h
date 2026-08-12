// Mithril-Wrapper - gl/mg_state.h
//
// GL core scalar state (gl_state_s). Mirrors MobileGlues gl/mg.h's gl_state_s:
// the small bag of per-context scalars that don't belong to any object table —
// current program / texture unit / draw fbo, the proxy-texture query slots, and
// the six pixel-store parameters desktop GL has and GLES does not.
//
// Subview pattern (Task 3.1): the actual values still live in the centralized
// mithril::GLState (currentProgram / activeTextureUnit / currentDrawFBO /
// proxyTexture2D / pixelStore). This struct reproduces MobileGlues' field shape
// (forward-looking: these become the source of truth once the data is migrated
// out of GLState) AND holds a non-owning GLState* back-pointer so the
// distributed per-context table + accessor API is in place now, without
// disturbing existing g_state->currentProgram access points.
//
// On bind (mg_gl_bind_context) only the back-pointer is initialised; the scalar
// fields are left at their defaults. Readers that need the live value today go
// through gl_state()->...  (the back-pointer). When the data moves out of
// GLState, the bind hook will copy into the scalar fields and they become
// authoritative — matching the MobileGlues path exactly.
#ifndef MITHRIL_GL_MG_STATE_H
#define MITHRIL_GL_MG_STATE_H

#include <GL/gl.h>
#include "../MG_State/State.h"   // mithril::GLState

#ifdef __cplusplus
extern "C" {
#endif

    struct gl_state_s {
        // ---- MobileGlues-shape scalar fields (forward-looking) ----
        GLsizei proxy_width;
        GLsizei proxy_height;
        GLenum  proxy_intformat;

        GLuint  current_program;
        GLuint  current_tex_unit;
        GLuint  current_draw_fbo;

        // The pixel-store parameters desktop GL has and GLES does not. See the
        // matching comment in MobileGlues gl/mg.h: only the two SWAP_BYTES are
        // acted on today; the rest are tracked so a value that was set can be
        // read back.
        GLint   unpack_swap_bytes;
        GLint   unpack_lsb_first;
        GLint   pack_swap_bytes;
        GLint   pack_lsb_first;
        GLint   pack_image_height;
        GLint   pack_skip_images;

        // ---- Subview back-pointer (authoritative today) ----
        // Non-owning pointer to the per-context GLState that still owns the live
        // values. Set by mg_gl_bind_context(). Readers today go through this;
        // the scalar fields above become authoritative once the data migrates.
        mithril::GLState* state;

        // Convenience: the whole GLState.
        mithril::GLState* gl_state() { return state; }
    };

#ifdef __cplusplus
}
#endif

#endif // MITHRIL_GL_MG_STATE_H
