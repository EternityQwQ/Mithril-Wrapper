// Mithril-Wrapper - MG_Impl/gl.cpp
// Core state-control GL entry points: clear, enable/disable, viewport, blend,
// depth, stencil, rasterizer, pixel store, active texture, flush/finish.
//
// This is the Vulkan/MoltenVK rewrite of the former gl/gl.cpp. The Metal
// clear/render-pass/commit calls (metal_set_clear_color / metal_begin_render_pass
// / metal_commit / ...) are replaced with the Vulkan backend C API
// (backend_set_clear_color / backend_begin_render_pass / backend_commit / ...)
// declared in MG_Backend/Backend.h.
//
// State access goes through the modular GLContext domain accessors
// (GetRenderState / GetTextureState) instead of the former flat GLState fields.
#include "includes.h"
#include "Framebuffer.h"

extern "C" {

/* ---- Clear ---- */
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
    MITHRIL_ENSURE_INIT();
    float c[4] = {r, g, b, a};
    g_state->GetRenderState().SetClearColor(c);
    backend_set_clear_color(r, g, b, a);
}

void glClearDepth(GLclampd d) {
    MITHRIL_ENSURE_INIT();
    g_state->GetRenderState().SetClearDepth(static_cast<float>(d));
    backend_set_clear_depth(d);
}

void glClearDepthf(GLclampf d) {
    glClearDepth((GLclampd)d);
}

void glClearStencil(GLint s) {
    MITHRIL_ENSURE_INIT();
    g_state->GetRenderState().SetClearStencil(static_cast<uint32_t>(s));
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

/* ---- Enable / Disable ---- */
void glEnable(GLenum cap) {
    MITHRIL_ENSURE_INIT();
    g_state->GetRenderState().SetCapability(
        mithril::glstate::GLToCapabilityInput(cap), true);
}

void glDisable(GLenum cap) {
    MITHRIL_ENSURE_INIT();
    g_state->GetRenderState().SetCapability(
        mithril::glstate::GLToCapabilityInput(cap), false);
}

GLboolean glIsEnabled(GLenum cap) {
    MITHRIL_ENSURE_INIT();
    return g_state->GetRenderState().IsCapabilityEnabled(
        mithril::glstate::GLToCapabilityInput(cap)) ? GL_TRUE : GL_FALSE;
}

// Indexed capability toggles are kept as the historical simplified broadcast
// (glEnable(cap) ignores the index). Specialising GL_BLEND to a per-buffer
// enable would require splitting the main blend switch and is left for a
// later pass; the broadcast matches the previous behaviour exactly.
void glEnablei(GLenum cap, GLuint index) { (void)index; glEnable(cap); }
void glDisablei(GLenum cap, GLuint index) { (void)index; glDisable(cap); }
GLboolean glIsEnabledi(GLenum cap, GLuint index) { (void)index; return glIsEnabled(cap); }

/* ---- Viewport / scissor / depth range ---- */
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    MITHRIL_ENSURE_INIT();
    g_state->GetRenderState().SetViewport(x, y, w, h);
}

void glDepthRange(GLclampd n, GLclampd f) {
    MITHRIL_ENSURE_INIT();
    g_state->GetRenderState().SetDepthRange(static_cast<float>(n),
                                            static_cast<float>(f));
}

void glDepthRangef(GLclampf n, GLclampf f) { glDepthRange((GLclampd)n, (GLclampd)f); }

void glScissor(GLint x, GLint y, GLsizei w, GLsizei h) {
    MITHRIL_ENSURE_INIT();
    g_state->GetRenderState().SetScissorBox(x, y, w, h);
}

/* ---- Blend ---- */
void glBlendFunc(GLenum sf, GLenum df) {
    MITHRIL_ENSURE_INIT();
    mithril::glstate::BlendFactor s = mithril::glstate::GLToBlendFactor(sf);
    mithril::glstate::BlendFactor d = mithril::glstate::GLToBlendFactor(df);
    g_state->GetRenderState().SetBlendFunc(s, d, s, d);
}

void glBlendFuncSeparate(GLenum sRGB, GLenum dRGB, GLenum sA, GLenum dA) {
    MITHRIL_ENSURE_INIT();
    g_state->GetRenderState().SetBlendFunc(
        mithril::glstate::GLToBlendFactor(sRGB),
        mithril::glstate::GLToBlendFactor(dRGB),
        mithril::glstate::GLToBlendFactor(sA),
        mithril::glstate::GLToBlendFactor(dA));
}

void glBlendEquation(GLenum mode) {
    MITHRIL_ENSURE_INIT();
    mithril::glstate::BlendEquation m = mithril::glstate::GLToBlendEquation(mode);
    g_state->GetRenderState().SetBlendEquation(m, m);
}

void glBlendEquationSeparate(GLenum mRGB, GLenum mA) {
    MITHRIL_ENSURE_INIT();
    g_state->GetRenderState().SetBlendEquation(
        mithril::glstate::GLToBlendEquation(mRGB),
        mithril::glstate::GLToBlendEquation(mA));
}

void glBlendColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
    MITHRIL_ENSURE_INIT();
    float c[4] = {r, g, b, a};
    g_state->GetRenderState().SetBlendColor(c);
}

void glBlendFunci(GLuint buf, GLenum src, GLenum dst) {
    MITHRIL_ENSURE_INIT();
    mithril::glstate::BlendFactor s = mithril::glstate::GLToBlendFactor(src);
    mithril::glstate::BlendFactor d = mithril::glstate::GLToBlendFactor(dst);
    g_state->GetRenderState().SetBlendFuncIndexed(buf, s, d, s, d);
}
void glBlendFuncSeparatei(GLuint buf, GLenum sR, GLenum dR, GLenum sA, GLenum dA) {
    MITHRIL_ENSURE_INIT();
    g_state->GetRenderState().SetBlendFuncIndexed(
        buf,
        mithril::glstate::GLToBlendFactor(sR),
        mithril::glstate::GLToBlendFactor(dR),
        mithril::glstate::GLToBlendFactor(sA),
        mithril::glstate::GLToBlendFactor(dA));
}
void glBlendEquationi(GLuint buf, GLenum mode) {
    MITHRIL_ENSURE_INIT();
    mithril::glstate::BlendEquation m = mithril::glstate::GLToBlendEquation(mode);
    g_state->GetRenderState().SetBlendEquationIndexed(buf, m, m);
}

/* ---- Depth / stencil / color mask ---- */
void glDepthFunc(GLenum func) {
    MITHRIL_ENSURE_INIT();
    g_state->GetRenderState().SetDepthFunc(mithril::glstate::GLToDepthTestFunc(func));
}

void glDepthMask(GLboolean flag) {
    MITHRIL_ENSURE_INIT();
    g_state->GetRenderState().SetDepthMask(flag != 0);
}

void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    MITHRIL_ENSURE_INIT();
    g_state->GetRenderState().SetColorMask(
        mithril::glstate::BoolVec4{r != 0, g != 0, b != 0, a != 0});
}

void glStencilMask(GLuint mask) {
    MITHRIL_ENSURE_INIT();
    g_state->GetRenderState().SetStencilMask(mithril::glstate::StencilFace::Front, mask);
    g_state->GetRenderState().SetStencilMask(mithril::glstate::StencilFace::Back, mask);
}

void glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    MITHRIL_ENSURE_INIT();
    mithril::glstate::DepthTestFunc f = mithril::glstate::GLToDepthTestFunc(func);
    g_state->GetRenderState().SetStencilFunc(mithril::glstate::StencilFace::Front, f, ref, mask);
    g_state->GetRenderState().SetStencilFunc(mithril::glstate::StencilFace::Back,  f, ref, mask);
}

void glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass) {
    MITHRIL_ENSURE_INIT();
    mithril::glstate::StencilOperation sf  = mithril::glstate::GLToStencilOperation(sfail);
    mithril::glstate::StencilOperation dpf = mithril::glstate::GLToStencilOperation(dpfail);
    mithril::glstate::StencilOperation dpp = mithril::glstate::GLToStencilOperation(dppass);
    g_state->GetRenderState().SetStencilOp(mithril::glstate::StencilFace::Front, sf, dpf, dpp);
    g_state->GetRenderState().SetStencilOp(mithril::glstate::StencilFace::Back,  sf, dpf, dpp);
}

void glStencilMaskSeparate(GLenum face, GLuint mask) {
    MITHRIL_ENSURE_INIT();
    if (face == GL_FRONT) {
        g_state->GetRenderState().SetStencilMask(mithril::glstate::StencilFace::Front, mask);
    } else if (face == GL_BACK) {
        g_state->GetRenderState().SetStencilMask(mithril::glstate::StencilFace::Back, mask);
    } else {
        // GL_FRONT_AND_BACK (or anything else): broadcast to both faces,
        // matching the previous flat-state behaviour.
        g_state->GetRenderState().SetStencilMask(mithril::glstate::StencilFace::Front, mask);
        g_state->GetRenderState().SetStencilMask(mithril::glstate::StencilFace::Back, mask);
    }
}

void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) {
    MITHRIL_ENSURE_INIT();
    mithril::glstate::DepthTestFunc f = mithril::glstate::GLToDepthTestFunc(func);
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
        g_state->GetRenderState().SetStencilFunc(mithril::glstate::StencilFace::Front, f, ref, mask);
    }
    if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
        g_state->GetRenderState().SetStencilFunc(mithril::glstate::StencilFace::Back, f, ref, mask);
    }
}

void glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass) {
    MITHRIL_ENSURE_INIT();
    mithril::glstate::StencilOperation sf  = mithril::glstate::GLToStencilOperation(sfail);
    mithril::glstate::StencilOperation dpf = mithril::glstate::GLToStencilOperation(dpfail);
    mithril::glstate::StencilOperation dpp = mithril::glstate::GLToStencilOperation(dppass);
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
        g_state->GetRenderState().SetStencilOp(mithril::glstate::StencilFace::Front, sf, dpf, dpp);
    }
    if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
        g_state->GetRenderState().SetStencilOp(mithril::glstate::StencilFace::Back, sf, dpf, dpp);
    }
}

/* ---- Rasterizer ---- */
void glCullFace(GLenum mode) {
    MITHRIL_ENSURE_INIT();
    g_state->GetRenderState().SetCullFaceMode(mithril::glstate::GLToCullFaceMode(mode));
}

void glFrontFace(GLenum mode) {
    MITHRIL_ENSURE_INIT();
    g_state->GetRenderState().SetFrontFaceMode(mithril::glstate::GLToFrontFaceMode(mode));
}

void glPolygonMode(GLenum face, GLenum mode) {
    MITHRIL_ENSURE_INIT();
    // SetPolygonMode takes (front, back) together; preserve the untouched
    // face by reading its current value and passing it through. This keeps
    // the per-face selectivity of the previous flat-state implementation.
    GLenum front = g_state->GetRenderState().GetPolygonModeFront();
    GLenum back  = g_state->GetRenderState().GetPolygonModeBack();
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) front = mode;
    if (face == GL_BACK  || face == GL_FRONT_AND_BACK) back  = mode;
    g_state->GetRenderState().SetPolygonMode(front, back);
}

void glPolygonOffset(GLfloat factor, GLfloat units) {
    MITHRIL_ENSURE_INIT();
    g_state->GetRenderState().SetPolygonOffset(factor, units);
}

void glLineWidth(GLfloat w) { MITHRIL_ENSURE_INIT(); g_state->GetRenderState().SetLineWidth(w); }
void glPointSize(GLfloat s) { MITHRIL_ENSURE_INIT(); g_state->GetRenderState().SetPointSize(s); }
void glHint(GLenum, GLenum) { MITHRIL_ENSURE_INIT(); /* hints are advisory */ }

/* ---- Pixel store ---- */
void glPixelStorei(GLenum pname, GLint param) {
    MITHRIL_ENSURE_INIT();
    g_state->GetRenderState().SetPixelStoreParam(
        mithril::glstate::GLToPixelStoreParam(pname), param);
}

void glPixelStoref(GLenum pname, GLfloat param) { glPixelStorei(pname, (GLint)param); }

/* ---- Active texture ---- */
void glActiveTexture(GLenum texture) {
    MITHRIL_ENSURE_INIT();
    if (texture >= GL_TEXTURE0 &&
        texture < GL_TEXTURE0 + mithril::glstate::kMaxTextureUnits) {
        g_state->GetTextureState().SetActiveTextureUnit(texture - GL_TEXTURE0);
    }
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
    g_state->GetRenderState().SetPrimitiveRestartIndex(index);
}

} // extern "C"
