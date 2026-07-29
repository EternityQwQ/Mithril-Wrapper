// Mithril-Wrapper - MG_Impl/gl.cpp
// Core state-control GL entry points: clear, enable/disable, viewport, blend,
// depth, stencil, rasterizer, pixel store, active texture, flush/finish.
//
// This is the Vulkan/MoltenVK rewrite of the former gl/gl.cpp. The Metal
// clear/render-pass/commit calls (metal_set_clear_color / metal_begin_render_pass
// / metal_commit / ...) are replaced with the Vulkan backend C API
// (backend_set_clear_color / backend_begin_render_pass / backend_commit / ...)
// declared in MG_Backend/Backend.h.
#include "includes.h"
#include "Framebuffer.h"

extern "C" {

/* ---- Clear ---- */
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
    MITHRIL_ENSURE_INIT();
    g_state->clearColor[0] = r;
    g_state->clearColor[1] = g;
    g_state->clearColor[2] = b;
    g_state->clearColor[3] = a;
    backend_set_clear_color(r, g, b, a);
}

void glClearDepth(GLclampd d) {
    MITHRIL_ENSURE_INIT();
    g_state->clearDepth = d;
    backend_set_clear_depth(d);
}

void glClearDepthf(GLclampf d) {
    glClearDepth((GLclampd)d);
}

void glClearStencil(GLint s) {
    MITHRIL_ENSURE_INIT();
    g_state->clearStencil = s;
    backend_set_clear_stencil(s);
}

void glClear(GLbitfield mask) {
    MITHRIL_ENSURE_INIT();
    // Resolve current draw framebuffer attachments.
    VkImageView colors[8] = {VK_NULL_HANDLE};
    VkImageView depth = VK_NULL_HANDLE;
    int w = 0, h = 0;
    int n = mithril::collect_draw_fbo_attachments(colors, &depth, &w, &h);

    // FIX: Use LOAD (not CLEAR) for the render pass, then vkCmdClearAttachments
    // to clear ONLY the aspects specified by `mask`. The old code used
    // loadOp=CLEAR which cleared ALL attachments regardless of mask — so
    // glClear(GL_DEPTH_BUFFER_BIT) would wipe the color buffer too, causing
    // black screen. This matches MobileGL's Clear() implementation
    // (VulkanRenderer.cpp:4230-4358) which uses vkCmdClearAttachments.
    backend_set_load_load();
    backend_begin_render_pass(colors, n, depth, w, h, 1);
    backend_clear_attachments(mask, 0, 0, w, h);
    backend_end_render_pass();
    backend_commit();
}

/* ---- Enable / Disable ----
 * P0-6: capabilities are now managed solely by GLState::setCapability /
 * isCapabilityEnabled (bool fields). The old enabledCaps set is gone, so
 * there is a single source of truth — no possibility of the set and the
 * fields drifting out of sync.
 */
void glEnable(GLenum cap) {
    MITHRIL_ENSURE_INIT();
    g_state->setCapability(cap, true);
}

void glDisable(GLenum cap) {
    MITHRIL_ENSURE_INIT();
    g_state->setCapability(cap, false);
}

GLboolean glIsEnabled(GLenum cap) {
    if (!g_state) return GL_FALSE;
    return g_state->isCapabilityEnabled(cap) ? GL_TRUE : GL_FALSE;
}

/* Indexed enable/disable: GL_BLEND is the only capability that is per-draw-
 * buffer. For all other caps the index is ignored (GL_INVALID_OPERATION is
 * not required by the spec for caps that are not per-buffer). */
void glEnablei(GLenum cap, GLuint index) {
    MITHRIL_ENSURE_INIT();
    if (cap == GL_BLEND && index < mithril::kMaxColorAttachments) {
        g_state->blends[index].enabled = true;
        g_state->bumpRenderVersion();
        return;
    }
    g_state->setCapability(cap, true);
}

void glDisablei(GLenum cap, GLuint index) {
    MITHRIL_ENSURE_INIT();
    if (cap == GL_BLEND && index < mithril::kMaxColorAttachments) {
        g_state->blends[index].enabled = false;
        g_state->bumpRenderVersion();
        return;
    }
    g_state->setCapability(cap, false);
}

GLboolean glIsEnabledi(GLenum cap, GLuint index) {
    if (!g_state) return GL_FALSE;
    if (cap == GL_BLEND && index < mithril::kMaxColorAttachments) {
        return g_state->blends[index].enabled ? GL_TRUE : GL_FALSE;
    }
    return g_state->isCapabilityEnabled(cap) ? GL_TRUE : GL_FALSE;
}

/* ---- Viewport / scissor / depth range ---- */
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    MITHRIL_ENSURE_INIT();
    g_state->viewportX = x;
    g_state->viewportY = y;
    g_state->viewportW = w;
    g_state->viewportH = h;
}

void glDepthRange(GLclampd n, GLclampd f) {
    MITHRIL_ENSURE_INIT();
    g_state->depthNear = n;
    g_state->depthFar  = f;
}

void glDepthRangef(GLclampf n, GLclampf f) { glDepthRange((GLclampd)n, (GLclampd)f); }

void glScissor(GLint x, GLint y, GLsizei w, GLsizei h) {
    MITHRIL_ENSURE_INIT();
    g_state->scissorX = x;
    g_state->scissorY = y;
    g_state->scissorW = w;
    g_state->scissorH = h;
}

/* ---- Blend (per-draw-buffer) ----
 * The non-indexed variants set the blend state for ALL draw buffers (GL spec).
 * The *i variants set the state for a single draw buffer `buf`.
 */
void glBlendFunc(GLenum sf, GLenum df) {
    MITHRIL_ENSURE_INIT();
    for (int i = 0; i < mithril::kMaxColorAttachments; ++i) {
        g_state->blends[i].srcRGB = sf; g_state->blends[i].srcA = sf;
        g_state->blends[i].dstRGB = df; g_state->blends[i].dstA = df;
    }
    g_state->bumpRenderVersion();
}

void glBlendFuncSeparate(GLenum sRGB, GLenum dRGB, GLenum sA, GLenum dA) {
    MITHRIL_ENSURE_INIT();
    for (int i = 0; i < mithril::kMaxColorAttachments; ++i) {
        g_state->blends[i].srcRGB = sRGB; g_state->blends[i].dstRGB = dRGB;
        g_state->blends[i].srcA   = sA;   g_state->blends[i].dstA   = dA;
    }
    g_state->bumpRenderVersion();
}

void glBlendEquation(GLenum mode) {
    MITHRIL_ENSURE_INIT();
    for (int i = 0; i < mithril::kMaxColorAttachments; ++i) {
        g_state->blends[i].eqRGB = mode; g_state->blends[i].eqA = mode;
    }
    g_state->bumpRenderVersion();
}

void glBlendEquationSeparate(GLenum mRGB, GLenum mA) {
    MITHRIL_ENSURE_INIT();
    for (int i = 0; i < mithril::kMaxColorAttachments; ++i) {
        g_state->blends[i].eqRGB = mRGB;
        g_state->blends[i].eqA   = mA;
    }
    g_state->bumpRenderVersion();
}

void glBlendColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
    MITHRIL_ENSURE_INIT();
    g_state->blendColor[0] = r;
    g_state->blendColor[1] = g;
    g_state->blendColor[2] = b;
    g_state->blendColor[3] = a;
}

void glBlendFunci(GLuint buf, GLenum src, GLenum dst) {
    MITHRIL_ENSURE_INIT();
    if (buf >= mithril::kMaxColorAttachments) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    g_state->blends[buf].srcRGB = src; g_state->blends[buf].srcA = src;
    g_state->blends[buf].dstRGB = dst; g_state->blends[buf].dstA = dst;
    g_state->bumpRenderVersion();
}
void glBlendFuncSeparatei(GLuint buf, GLenum sR, GLenum dR, GLenum sA, GLenum dA) {
    MITHRIL_ENSURE_INIT();
    if (buf >= mithril::kMaxColorAttachments) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    g_state->blends[buf].srcRGB = sR; g_state->blends[buf].dstRGB = dR;
    g_state->blends[buf].srcA   = sA; g_state->blends[buf].dstA   = dA;
    g_state->bumpRenderVersion();
}
void glBlendEquationi(GLuint buf, GLenum mode) {
    MITHRIL_ENSURE_INIT();
    if (buf >= mithril::kMaxColorAttachments) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    g_state->blends[buf].eqRGB = mode; g_state->blends[buf].eqA = mode;
    g_state->bumpRenderVersion();
}
void glBlendEquationSeparatei(GLuint buf, GLenum mRGB, GLenum mA) {
    MITHRIL_ENSURE_INIT();
    if (buf >= mithril::kMaxColorAttachments) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    g_state->blends[buf].eqRGB = mRGB; g_state->blends[buf].eqA = mA;
    g_state->bumpRenderVersion();
}

/* ---- Depth / stencil / color mask ---- */
void glDepthFunc(GLenum func) {
    MITHRIL_ENSURE_INIT();
    g_state->depthFunc = func;
}

void glDepthMask(GLboolean flag) {
    MITHRIL_ENSURE_INIT();
    g_state->depthMask = (flag != 0);
}

void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    MITHRIL_ENSURE_INIT();
    // GL spec: glColorMask sets the mask for ALL draw buffers.
    for (int i = 0; i < mithril::kMaxColorAttachments; ++i) {
        g_state->colorMask[i][0] = (r != 0);
        g_state->colorMask[i][1] = (g != 0);
        g_state->colorMask[i][2] = (b != 0);
        g_state->colorMask[i][3] = (a != 0);
    }
    g_state->bumpRenderVersion();
}

void glColorMaski(GLuint buf, GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    MITHRIL_ENSURE_INIT();
    if (buf >= mithril::kMaxColorAttachments) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    g_state->colorMask[buf][0] = (r != 0);
    g_state->colorMask[buf][1] = (g != 0);
    g_state->colorMask[buf][2] = (b != 0);
    g_state->colorMask[buf][3] = (a != 0);
    g_state->bumpRenderVersion();
}

void glStencilMask(GLuint mask) {
    MITHRIL_ENSURE_INIT();
    g_state->stencilMask = mask;
    g_state->stencilBackMask = mask;
}

void glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    MITHRIL_ENSURE_INIT();
    g_state->stencilFunc = func;
    g_state->stencilRef  = ref;
    g_state->stencilValueMask = mask;
    g_state->stencilBackFunc = func;
    g_state->stencilBackRef  = ref;
    g_state->stencilBackValueMask = mask;
}

void glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass) {
    MITHRIL_ENSURE_INIT();
    g_state->stencilSfail  = sfail;
    g_state->stencilDpfail = dpfail;
    g_state->stencilDppass = dppass;
    g_state->stencilBackSfail  = sfail;
    g_state->stencilBackDpfail = dpfail;
    g_state->stencilBackDppass = dppass;
}

void glStencilMaskSeparate(GLenum face, GLuint mask) {
    MITHRIL_ENSURE_INIT();
    if (face == GL_FRONT) g_state->stencilMask = mask;
    else if (face == GL_BACK) g_state->stencilBackMask = mask;
    else { g_state->stencilMask = mask; g_state->stencilBackMask = mask; }
}

void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) {
    MITHRIL_ENSURE_INIT();
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
        g_state->stencilFunc = func;
        g_state->stencilRef  = ref;
        g_state->stencilValueMask = mask;
    }
    if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
        g_state->stencilBackFunc = func;
        g_state->stencilBackRef  = ref;
        g_state->stencilBackValueMask = mask;
    }
}

void glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass) {
    MITHRIL_ENSURE_INIT();
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
        g_state->stencilSfail = sfail;
        g_state->stencilDpfail = dpfail;
        g_state->stencilDppass = dppass;
    }
    if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
        g_state->stencilBackSfail = sfail;
        g_state->stencilBackDpfail = dpfail;
        g_state->stencilBackDppass = dppass;
    }
}

/* ---- Rasterizer ---- */
void glCullFace(GLenum mode) {
    MITHRIL_ENSURE_INIT();
    g_state->cullMode = mode;
}

void glFrontFace(GLenum mode) {
    MITHRIL_ENSURE_INIT();
    g_state->frontFace = mode;
}

void glPolygonMode(GLenum face, GLenum mode) {
    MITHRIL_ENSURE_INIT();
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) g_state->polygonModeFront = mode;
    if (face == GL_BACK  || face == GL_FRONT_AND_BACK) g_state->polygonModeBack  = mode;
}

void glPolygonOffset(GLfloat factor, GLfloat units) {
    MITHRIL_ENSURE_INIT();
    g_state->polygonOffsetFactor = factor;
    g_state->polygonOffsetUnits  = units;
}

void glLineWidth(GLfloat w) { MITHRIL_ENSURE_INIT(); g_state->lineWidth = w; }
void glPointSize(GLfloat s) { MITHRIL_ENSURE_INIT(); g_state->pointSize = s; }
void glHint(GLenum, GLenum) { MITHRIL_ENSURE_INIT(); /* hints are advisory */ }

/* ---- Pixel store ----
 * Pnames map onto the PixelStoreState sub-struct (pack + unpack state was
 * moved off the flat GLState fields in the rewrite).
 */
void glPixelStorei(GLenum pname, GLint param) {
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
}

void glPixelStoref(GLenum pname, GLfloat param) { glPixelStorei(pname, (GLint)param); }

/* ---- Active texture ----
 * P1-13: validate the texture unit index. glActiveTexture accepts
 * GL_TEXTURE0..GL_TEXTUREi where i < GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS.
 * Out-of-range units must record GL_INVALID_ENUM (per the GL spec) instead of
 * being silently ignored.
 */
void glActiveTexture(GLenum texture) {
    MITHRIL_ENSURE_INIT();
    if (texture < GL_TEXTURE0 || texture >= GL_TEXTURE0 + mithril::kMaxTextureUnits) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    g_state->activeTextureUnit = texture - GL_TEXTURE0;
}

/* ---- Flush / finish ---- */
void glFlush(void) {
    MITHRIL_ENSURE_INIT();
    backend_end_render_pass();
    backend_commit();
}

void glFinish(void) {
    MITHRIL_ENSURE_INIT();
    backend_end_render_pass();
    backend_commit();
}

void glPrimitiveRestartIndex(GLuint index) {
    MITHRIL_ENSURE_INIT();
    g_state->primitiveRestartIndex = index;
}

} // extern "C"
