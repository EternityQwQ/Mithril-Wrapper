// Mithril-Wrapper - MG_Impl/Framebuffer.cpp
// Framebuffer objects and attachment resolution.
//
// This is the Vulkan/MoltenVK rewrite of the former gl/framebuffer.cpp. The
// attachment resolution (collect_draw_fbo_attachments) now returns Vulkan
// VkImageView handles — the swapchain image views for the EGL default
// framebuffer (FBO 0) and backend_get_texture_view() for user FBO color/depth
// attachments — so the drawing path can build the dynamic-rendering attachment
// list.
//
// Migrated to the modular GLContext API: framebuffer state lives in
// mithril::glstate::FramebufferState (owned by g_state), per-name records are
// mithril::glstate::FramebufferObject (SharedPtr-owned), and texture lookups
// during attachment resolution go through mithril::glstate::TextureState. The
// Vulkan backend C API (backend_get_texture_view / backend_get_texture_image /
// backend_blit_images) is unchanged.
#include "includes.h"
#include "Framebuffer.h"

#include <vector>

extern "C" {

void glGenFramebuffers(GLsizei n, GLuint* framebuffers) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !framebuffers) return;
    std::vector<uint32_t> names;
    g_state->GetFramebufferState().GenFramebufferNames(static_cast<uint32_t>(n), names);
    for (GLsizei i = 0; i < n; ++i) {
        framebuffers[i] = names[static_cast<size_t>(i)];
    }
}

void glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !framebuffers) return;
    for (GLsizei i = 0; i < n; ++i) {
        GLuint name = framebuffers[i];
        if (name == 0) continue;
        // GL name-layer deletion: detaches the name from the object table and
        // falls back the current draw/read binding to the default framebuffer
        // (0) if it was holding this name. The underlying Vulkan resources are
        // released by the backend disposal queue once in-flight GPU work
        // referencing them completes.
        g_state->GetFramebufferState().MarkFramebufferForDeletion(name);
    }
}

void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    MITHRIL_ENSURE_INIT();
    // Detect first materialisation so we can apply the legacy default
    // draw/read-buffer configuration (the former glBindFramebuffer set
    // drawBuffers[0]=GL_COLOR_ATTACHMENT0, drawBufferCount=1,
    // readBuffer=GL_COLOR_ATTACHMENT0 on every newly created FBO). The default
    // framebuffer (name 0) is pre-populated with this config by
    // FramebufferState's constructor, so only non-zero names need it.
    bool firstMaterialisation = false;
    if (framebuffer != 0) {
        firstMaterialisation = !g_state->GetFramebufferState().GetFramebufferObject(framebuffer);
    }
    g_state->GetFramebufferState().BindFramebuffer(target, framebuffer);
    if (firstMaterialisation) {
        const auto& fbo = g_state->GetFramebufferState().GetFramebufferObject(framebuffer);
        if (fbo) {
            fbo->drawBuffers[0] = GL_COLOR_ATTACHMENT0;
            fbo->drawBufferCount = 1;
            fbo->readBuffer = GL_COLOR_ATTACHMENT0;
        }
    }
}

void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget,
                            GLuint texture, GLint level) {
    MITHRIL_ENSURE_INIT();
    const auto& fbo = (target == GL_READ_FRAMEBUFFER)
        ? g_state->GetFramebufferState().GetCurrentReadFramebuffer()
        : g_state->GetFramebufferState().GetCurrentDrawFramebuffer();
    if (!fbo) return;
    mithril::glstate::FBOAttachment a{};
    a.texture = texture;
    a.textarget = textarget;
    a.level = level;
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment <= GL_COLOR_ATTACHMENT7) {
        fbo->colors[attachment - GL_COLOR_ATTACHMENT0] = a;
    } else if (attachment == GL_DEPTH_ATTACHMENT) {
        fbo->depth = a;
    } else if (attachment == GL_STENCIL_ATTACHMENT) {
        fbo->stencil = a;
    } else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        fbo->depth = a; fbo->stencil = a;
    }
}

void glFramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture,
                               GLint level, GLint layer) {
    MITHRIL_ENSURE_INIT();
    const auto& fbo = (target == GL_READ_FRAMEBUFFER)
        ? g_state->GetFramebufferState().GetCurrentReadFramebuffer()
        : g_state->GetFramebufferState().GetCurrentDrawFramebuffer();
    if (!fbo) return;
    mithril::glstate::FBOAttachment a{};
    a.texture = texture;
    a.level = level;
    a.layer = layer;
    a.layered = true;
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment <= GL_COLOR_ATTACHMENT7) {
        fbo->colors[attachment - GL_COLOR_ATTACHMENT0] = a;
    } else if (attachment == GL_DEPTH_ATTACHMENT) {
        fbo->depth = a;
    } else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        fbo->depth = a; fbo->stencil = a;
    }
}

void glFramebufferTexture(GLenum target, GLenum attachment, GLuint texture, GLint level) {
    MITHRIL_ENSURE_INIT();
    const auto& fbo = (target == GL_READ_FRAMEBUFFER)
        ? g_state->GetFramebufferState().GetCurrentReadFramebuffer()
        : g_state->GetFramebufferState().GetCurrentDrawFramebuffer();
    if (!fbo) return;
    mithril::glstate::FBOAttachment a{};
    a.texture = texture;
    a.level = level;
    a.layered = true;
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment <= GL_COLOR_ATTACHMENT7) {
        fbo->colors[attachment - GL_COLOR_ATTACHMENT0] = a;
    } else if (attachment == GL_DEPTH_ATTACHMENT) {
        fbo->depth = a;
    } else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        fbo->depth = a; fbo->stencil = a;
    }
}

void glDrawBuffers(GLsizei n, const GLenum* bufs) {
    MITHRIL_ENSURE_INIT();
    const auto& fbo = g_state->GetFramebufferState().GetCurrentDrawFramebuffer();
    if (!fbo || n <= 0 || !bufs) return;
    for (int i = 0; i < 8; ++i) fbo->drawBuffers[i] = GL_NONE;
    fbo->drawBufferCount = 0;
    for (GLsizei i = 0; i < n && i < 8; ++i) {
        fbo->drawBuffers[i] = bufs[i];
        if (bufs[i] != GL_NONE) fbo->drawBufferCount = i + 1;
    }
}

void glDrawBuffer(GLenum mode) {
    MITHRIL_ENSURE_INIT();
    const auto& fbo = g_state->GetFramebufferState().GetCurrentDrawFramebuffer();
    if (!fbo) return;
    for (int i = 0; i < 8; ++i) fbo->drawBuffers[i] = GL_NONE;
    if (mode == GL_FRONT || mode == GL_BACK || mode == GL_NONE) {
        fbo->drawBuffers[0] = (mode == GL_NONE) ? GL_NONE : GL_COLOR_ATTACHMENT0;
        fbo->drawBufferCount = (mode == GL_NONE) ? 0 : 1;
    } else if (mode >= GL_COLOR_ATTACHMENT0 && mode <= GL_COLOR_ATTACHMENT7) {
        fbo->drawBuffers[0] = mode;
        fbo->drawBufferCount = 1;
    }
}

void glReadBuffer(GLenum mode) {
    MITHRIL_ENSURE_INIT();
    const auto& fbo = g_state->GetFramebufferState().GetCurrentReadFramebuffer();
    if (fbo) fbo->readBuffer = mode;
}

GLenum glCheckFramebufferStatus(GLenum target) {
    MITHRIL_ENSURE_INIT();
    return GL_FRAMEBUFFER_COMPLETE;
}

void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                       GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                       GLbitfield mask, GLenum filter) {
    MITHRIL_ENSURE_INIT();

    // Only colour blits are implemented (depth/stencil blits are rare in MC
    // Java's modern pipeline and require NEAREST filtering + aspect masks).
    if (!(mask & GL_COLOR_BUFFER_BIT)) return;

    // Flush any pending rendering into the source/destination so the blit
    // sees the latest pixels and subsequent draws see the blit's result.
    backend_end_render_pass();
    backend_commit();

    auto& fbState = g_state->GetFramebufferState();

    // Resolve the source FBO's colour attachment. The read FBO is the source.
    //   - FBO 0 (EGL default): use the swapchain image installed on g_state.
    //   - User FBO: use the texture attached to GL_COLOR_ATTACHMENT0 (or the
    //     buffer selected by glReadBuffer, but MC Java only uses attachment 0).
    VkImage src_image = VK_NULL_HANDLE;
    VkFormat src_format = VK_FORMAT_UNDEFINED;
    if (fbState.GetCurrentReadFBO() == 0) {
        src_image  = fbState.eglDefaultColorImage;
        src_format = fbState.eglDefaultColorFormat;
    } else {
        const auto& fbo = fbState.GetCurrentReadFramebuffer();
        if (fbo) {
            GLuint tex = 0;
            // glReadBuffer selects which color attachment is the read source.
            // Default is GL_COLOR_ATTACHMENT0. MC Java doesn't change this.
            if (fbState.GetCurrentReadFBO() == fbState.GetCurrentDrawFBO() &&
                fbo->readBuffer >= GL_COLOR_ATTACHMENT0 && fbo->readBuffer <= GL_COLOR_ATTACHMENT7) {
                tex = fbo->colors[fbo->readBuffer - GL_COLOR_ATTACHMENT0].texture;
            } else if (fbo->readBuffer >= GL_COLOR_ATTACHMENT0 && fbo->readBuffer <= GL_COLOR_ATTACHMENT7) {
                tex = fbo->colors[fbo->readBuffer - GL_COLOR_ATTACHMENT0].texture;
            } else {
                tex = fbo->colors[0].texture;
            }
            if (tex) {
                src_image = backend_get_texture_image(tex);
                const auto& t = g_state->GetTextureState().GetTextureObject(tex);
                if (t) src_format = backend_vk_format_for_gl((GLenum)t->internalFormat);
            }
        }
    }

    // Resolve the destination FBO's colour attachment. The draw FBO is the
    // destination.
    VkImage dst_image = VK_NULL_HANDLE;
    VkFormat dst_format = VK_FORMAT_UNDEFINED;
    if (fbState.GetCurrentDrawFBO() == 0) {
        dst_image  = fbState.eglDefaultColorImage;
        dst_format = fbState.eglDefaultColorFormat;
    } else {
        const auto& fbo = fbState.GetCurrentDrawFramebuffer();
        if (fbo) {
            GLuint tex = fbo->colors[0].texture;
            if (tex) {
                dst_image = backend_get_texture_image(tex);
                const auto& t = g_state->GetTextureState().GetTextureObject(tex);
                if (t) dst_format = backend_vk_format_for_gl((GLenum)t->internalFormat);
            }
        }
    }

    if (src_image == VK_NULL_HANDLE || dst_image == VK_NULL_HANDLE) return;
    if (src_format == VK_FORMAT_UNDEFINED) src_format = VK_FORMAT_R8G8B8A8_UNORM;
    if (dst_format == VK_FORMAT_UNDEFINED) dst_format = VK_FORMAT_R8G8B8A8_UNORM;

    // GL's framebuffer origin is bottom-left; Vulkan's is top-left. MoltenVK
    // flips the Y axis when translating to Metal, so passing the GL coordinates
    // through unchanged matches the on-screen behaviour expected by the host
    // app (this mirrors how backend_set_viewport handles the Y flip).
    backend_blit_images(src_image, src_format,
                        dst_image, dst_format,
                        srcX0, srcY0, srcX1, srcY1,
                        dstX0, dstY0, dstX1, dstY1,
                        mask, filter);
}

/* Renderbuffers are minimally supported (used rarely by MC Java). */
void glGenRenderbuffers(GLsizei n, GLuint* rbs) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !rbs) return;
    // Renderbuffers share the framebuffer name allocator (the former flat
    // GLState had a single nextName counter shared by every object type).
    std::vector<uint32_t> names;
    g_state->GetFramebufferState().GenFramebufferNames(static_cast<uint32_t>(n), names);
    for (GLsizei i = 0; i < n; ++i) {
        rbs[i] = names[static_cast<size_t>(i)];
    }
}
void glDeleteRenderbuffers(GLsizei, const GLuint*) { MITHRIL_ENSURE_INIT(); }
void glBindRenderbuffer(GLenum, GLuint) { MITHRIL_ENSURE_INIT(); }
void glRenderbufferStorage(GLenum, GLenum, GLsizei, GLsizei) { MITHRIL_ENSURE_INIT(); }
void glRenderbufferStorageMultisample(GLenum, GLsizei, GLenum, GLsizei, GLsizei) { MITHRIL_ENSURE_INIT(); }
void glFramebufferRenderbuffer(GLenum, GLenum, GLenum, GLuint) { MITHRIL_ENSURE_INIT(); }

} // extern "C"

namespace mithril {

int collect_draw_fbo_attachments(VkImageView out_color[8], VkImageView* out_depth,
                                 int* out_w, int* out_h) {
    for (int i = 0; i < 8; ++i) out_color[i] = VK_NULL_HANDLE;
    *out_depth = VK_NULL_HANDLE;
    auto& fbState = g_state->GetFramebufferState();
    auto vp = g_state->GetRenderState().GetViewport();
    if (out_w) *out_w = vp.w;
    if (out_h) *out_h = vp.h;

    /*
     * EGL-backed default framebuffer: when an EGLSurface is current, the
     * swapchain image's VkImageView is installed on the FramebufferState. GL
     * commands against framebuffer 0 render straight into the on-screen
     * drawable. EGL swaps the image view per-frame (eglSwapBuffers acquires
     * the next swapchain image and replaces this handle).
     */
    if (fbState.GetCurrentDrawFBO() == 0 && fbState.eglDefaultColor != VK_NULL_HANDLE) {
        out_color[0] = fbState.eglDefaultColor;
        if (fbState.eglDefaultDepth != VK_NULL_HANDLE) *out_depth = fbState.eglDefaultDepth;
        if (fbState.eglDefaultWidth > 0 && out_w) *out_w = fbState.eglDefaultWidth;
        if (fbState.eglDefaultHeight > 0 && out_h) *out_h = fbState.eglDefaultHeight;
        return 1;
    }

    const auto& fbo = fbState.GetCurrentDrawFramebuffer();
    if (!fbo) return 0;

    int count = 0;
    int w = 0, h = 0;
    for (int i = 0; i < 8; ++i) {
        if (i >= fbo->drawBufferCount) break;
        if (fbo->drawBuffers[i] == GL_NONE) break;
        GLuint tex = fbo->colors[i].texture;
        if (tex == 0) { out_color[i] = VK_NULL_HANDLE; continue; }
        VkImageView view = backend_get_texture_view(tex);
        out_color[i] = view;
        if (view != VK_NULL_HANDLE) { count = i + 1; }
        if (w == 0) {
            const auto& t = g_state->GetTextureState().GetTextureObject(tex);
            if (t) { w = t->width; h = t->height; }
        }
    }
    if (fbo->depth.texture) {
        *out_depth = backend_get_texture_view(fbo->depth.texture);
        if (w == 0) {
            const auto& t = g_state->GetTextureState().GetTextureObject(fbo->depth.texture);
            if (t) { w = t->width; h = t->height; }
        }
    }
    if (w > 0 && out_w) *out_w = w;
    if (h > 0 && out_h) *out_h = h;
    return count;
}

} // namespace mithril
