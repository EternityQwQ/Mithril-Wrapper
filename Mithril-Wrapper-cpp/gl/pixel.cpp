// Mithril-Wrapper - gl/pixel.cpp
// MobileGlues-style split of gl.cpp: pixel store state.
//
// Holds the glPixelStorei / glPixelStoref entry points extracted from the
// former monolithic gl/gl.cpp so the file mirrors the MobileGlues layout
// (pixel.cpp = pixel store state). Bodies are unchanged — only the
// enclosing translation unit moved.
#include "includes.h"
#include "gl.h"

extern "C" {

/* ---- Pixel store ----
 * Pnames map onto the PixelStoreState sub-struct (pack + unpack state was
 * moved off the flat GLState fields in the rewrite).
 */
NATIVE_FUNCTION_HEAD(void, glPixelStorei, GLenum pname, GLint param)
    MITHRIL_ENSURE_INIT();
    switch (pname) {
        case GL_UNPACK_ALIGNMENT:      g_state->pixelStore.unpackAlignment     = param; break;
        case GL_PACK_ALIGNMENT:        g_state->pixelStore.packAlignment       = param; break;
        case GL_UNPACK_ROW_LENGTH:     g_state->pixelStore.unpackRowLength     = param; break;
        case GL_UNPACK_IMAGE_HEIGHT:   g_state->pixelStore.unpackImageHeight   = param; break;
        case GL_UNPACK_SKIP_ROWS:      g_state->pixelStore.unpackSkipRows      = param; break;
        case GL_UNPACK_SKIP_PIXELS:    g_state->pixelStore.unpackSkipPixels    = param; break;
        case GL_UNPACK_SKIP_IMAGES:    g_state->pixelStore.unpackSkipImages    = param; break;
        case GL_PACK_ROW_LENGTH:       g_state->pixelStore.packRowLength        = param; break;
        case GL_PACK_IMAGE_HEIGHT:     g_state->pixelStore.packImageHeight     = param; break;
        case GL_PACK_SKIP_ROWS:        g_state->pixelStore.packSkipRows        = param; break;
        case GL_PACK_SKIP_PIXELS:      g_state->pixelStore.packSkipPixels      = param; break;
        case GL_PACK_SKIP_IMAGES:      g_state->pixelStore.packSkipImages      = param; break;
        default: break;
    }
NATIVE_FUNCTION_END

NATIVE_FUNCTION_HEAD(void, glPixelStoref, GLenum pname, GLfloat param)
    glPixelStorei(pname, (GLint)param);
NATIVE_FUNCTION_END

} // extern "C"
