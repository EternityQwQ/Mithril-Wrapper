// Mithril-Wrapper - MG_Impl/Texture.cpp
// Texture object management: storage, upload, parameters, mipmap generation.
//
// Migrated to the modular GLContext API: texture state lives in
// mithril::glstate::TextureState (owned by g_state), per-name records are
// mithril::glstate::TextureObject (SharedPtr-owned), the active texture unit
// and per-unit bindings live on TextureState, the GL_PROXY_TEXTURE_2D query
// state lives in ProxyTextureState, and the unpack pixel-store parameters live
// on RenderState. The Vulkan backend C API (backend_get_or_create_texture /
// backend_texture_upload / backend_texture_set_params / backend_delete_texture
// / backend_transition_texture_layout / backend_generate_mipmaps) is unchanged.
#include "includes.h"

#include <vector>

extern "C" {

// Helper: the current GL_UNPACK_ALIGNMENT (used by backend_texture_upload to
// interpret the host pixel rows). Migrated from the flat g_state->unpackAlignment
// field to RenderState's typed PixelStoreParam slot.
static int unpack_alignment() {
    return g_state->GetRenderState().GetPixelStoreParam(
        mithril::glstate::GLToPixelStoreParam(GL_UNPACK_ALIGNMENT));
}

void glGenTextures(GLsizei n, GLuint* textures) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !textures) return;
    std::vector<uint32_t> names;
    g_state->GetTextureState().GenTextureNames(static_cast<uint32_t>(n), names);
    for (GLsizei i = 0; i < n; ++i) {
        textures[i] = names[static_cast<size_t>(i)];
    }
}

void glDeleteTextures(GLsizei n, const GLuint* textures) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !textures) return;
    for (GLsizei i = 0; i < n; ++i) {
        GLuint name = textures[i];
        if (name == 0) continue;
        // GL name-layer deletion: detaches the name from the object table and
        // unbinds it from every texture unit (TextureState). The underlying
        // VkImage + device memory is released by the backend disposal queue
        // (backend_delete_texture) once in-flight GPU work completes.
        g_state->GetTextureState().MarkTextureForDeletion(name);
        backend_delete_texture(name);
    }
}

void glBindTexture(GLenum target, GLuint texture) {
    MITHRIL_ENSURE_INIT();
    g_state->GetTextureState().BindTexture(
        mithril::glstate::GLToTextureTarget(target), texture);
}

// Resolve the texture object bound to the active texture unit. Returns a null
// SharedPtr when no texture is bound.
static mithril::glstate::SharedPtr<mithril::glstate::TextureObject>
bound_texture_for_unit() {
    uint32_t unit = g_state->GetTextureState().GetActiveTextureUnit();
    return g_state->GetTextureState().GetBoundTexture(unit);
}

void glTexImage2D(GLenum target, GLint level, GLint internalFormat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const void* pixels) {
    MITHRIL_ENSURE_INIT();
    if (border != 0) {
        g_state->RecordError(mithril::glstate::ErrorState::GLToErrorCode(GL_INVALID_VALUE));
        return;
    }

    // GL_PROXY_TEXTURE_2D: no real texture is created. Just record the
    // requested dimensions so glGetTexLevelParameteriv can report them.
    // Minecraft probes max texture size this way (GL_PROXY_TEXTURE_2D with
    // progressively larger sizes until the query returns 0).
    if (target == GL_PROXY_TEXTURE_2D) {
        // Accept the size if it's within our reported GL_MAX_TEXTURE_SIZE.
        // A size of 0 means "unsupported" per the GL spec.
        GLint maxSize = 16384; // matches GL_MAX_TEXTURE_SIZE in Getter.cpp
        mithril::glstate::ProxyTextureState& proxy =
            g_state->GetTextureState().GetProxyTexture2D();
        if (width > 0 && height > 0 && width <= maxSize && height <= maxSize) {
            proxy.width  = width;
            proxy.height = height;
            proxy.internalFormat = internalFormat;
            proxy.valid = true;
        } else {
            proxy.valid = false;
            proxy.width = 0;
            proxy.height = 0;
        }
        return;
    }

    auto t = bound_texture_for_unit();
    if (!t) return;
    if (level == 0) {
        t->internalFormat = internalFormat;
        t->width  = width;
        t->height = height;
        t->depth  = 1;
    }
    if (t->levels < level + 1) t->levels = level + 1;

    backend_get_or_create_texture(t->id, width, height, 1, t->levels,
                                  internalFormat, target, 1);
    if (pixels) {
        backend_texture_upload(t->id, level, 0, 0, 0, width, height, 1,
                               format, type, pixels, unpack_alignment());
    }
}

void glTexImage3D(GLenum target, GLint level, GLint internalFormat,
                  GLsizei width, GLsizei height, GLsizei depth, GLint border,
                  GLenum format, GLenum type, const void* pixels) {
    MITHRIL_ENSURE_INIT();
    if (border != 0) {
        g_state->RecordError(mithril::glstate::ErrorState::GLToErrorCode(GL_INVALID_VALUE));
        return;
    }
    auto t = bound_texture_for_unit();
    if (!t) return;
    if (level == 0) {
        t->internalFormat = internalFormat;
        t->width  = width;
        t->height = height;
        t->depth  = depth;
    }
    if (t->levels < level + 1) t->levels = level + 1;

    backend_get_or_create_texture(t->id, width, height, depth, t->levels,
                                  internalFormat, target, 1);
    if (pixels) {
        backend_texture_upload(t->id, level, 0, 0, 0, width, height, depth,
                               format, type, pixels, unpack_alignment());
    }
}

/*
 * glTexStorage2D / glTexStorage3D: allocate immutable storage for a texture.
 * Minecraft 1.21 uses these (rather than glTexImage2D) to create framebuffer
 * attachments, especially depth/stencil textures. We set the GL-level metadata
 * (internalFormat, dimensions, levels) and create the Vulkan texture with the
 * correct VkFormat up front. No pixel data is uploaded (immutable storage
 * starts uninitialised, like glTexImage2D with pixels=NULL).
 *
 * The Vulkan image is created with initialLayout = UNDEFINED. We immediately
 * transition it to SHADER_READ_ONLY_OPTIMAL so that:
 *   - If the texture is sampled before being rendered into, the layout is valid.
 *   - If the texture is attached to an FBO and rendered into, dynamic rendering
 *     will transition it to COLOR/DEPTH_STENCIL_ATTACHMENT_OPTIMAL (and the
 *     tracked layout lets our barrier code emit the correct oldLayout).
 * Without this transition, the texture sits in UNDEFINED and a subsequent
 * vkCmdBindDescriptorSets + draw that samples it would be a validation error.
 */
void glTexStorage2D(GLenum target, GLsizei levels, GLenum internalFormat,
                    GLsizei width, GLsizei height) {
    MITHRIL_ENSURE_INIT();
    auto t = bound_texture_for_unit();
    if (!t || levels <= 0) return;
    t->internalFormat = internalFormat;
    t->width  = width;
    t->height = height;
    t->depth  = 1;
    t->levels = levels;

    backend_get_or_create_texture(t->id, width, height, 1, levels,
                                  internalFormat, target, 1);
    // Transition UNDEFINED -> SHADER_READ_ONLY_OPTIMAL so the texture is in a
    // valid sampling layout before any draw references it.
    backend_transition_texture_layout(t->id, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void glTexStorage3D(GLenum target, GLsizei levels, GLenum internalFormat,
                    GLsizei width, GLsizei height, GLsizei depth) {
    MITHRIL_ENSURE_INIT();
    auto t = bound_texture_for_unit();
    if (!t || levels <= 0) return;
    t->internalFormat = internalFormat;
    t->width  = width;
    t->height = height;
    t->depth  = depth;
    t->levels = levels;

    backend_get_or_create_texture(t->id, width, height, depth, levels,
                                  internalFormat, target, 1);
    backend_transition_texture_layout(t->id, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height,
                     GLenum format, GLenum type, const void* pixels) {
    MITHRIL_ENSURE_INIT();
    (void)target;
    auto t = bound_texture_for_unit();
    if (!t || !pixels) return;
    backend_texture_upload(t->id, level, xoffset, yoffset, 0,
                           width, height, 1, format, type, pixels,
                           unpack_alignment());
}

void glTexSubImage3D(GLenum target, GLint level,
                     GLint xoffset, GLint yoffset, GLint zoffset,
                     GLsizei width, GLsizei height, GLsizei depth,
                     GLenum format, GLenum type, const void* pixels) {
    MITHRIL_ENSURE_INIT();
    (void)target;
    auto t = bound_texture_for_unit();
    if (!t || !pixels) return;
    backend_texture_upload(t->id, level, xoffset, yoffset, zoffset,
                           width, height, depth, format, type, pixels,
                           unpack_alignment());
}

void glTexImage2DMultisample(GLenum target, GLsizei samples, GLenum internalformat,
                             GLsizei width, GLsizei height,
                             GLboolean fixedsamplelocations) {
    MITHRIL_ENSURE_INIT();
    (void)fixedsamplelocations;
    auto t = bound_texture_for_unit();
    if (!t) return;
    t->internalFormat = internalformat;
    t->width  = width;
    t->height = height;
    t->depth  = 1;
    backend_get_or_create_texture(t->id, width, height, 1, 1,
                                  internalformat, target, samples > 1 ? samples : 1);
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    MITHRIL_ENSURE_INIT();
    (void)target;
    auto t = bound_texture_for_unit();
    if (!t) return;
    GLint p = (GLint)param;
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER: t->minFilter = p; break;
        case GL_TEXTURE_MAG_FILTER: t->magFilter = p; break;
        case GL_TEXTURE_WRAP_S:     t->wrapS = p; break;
        case GL_TEXTURE_WRAP_T:     t->wrapT = p; break;
        case GL_TEXTURE_WRAP_R:     t->wrapR = p; break;
        default: break;
    }
    backend_texture_set_params(t->id, t->minFilter, t->magFilter,
                               t->wrapS, t->wrapT, t->wrapR, t->borderColor);
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    glTexParameterf(target, pname, (GLfloat)param);
}

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        auto t = bound_texture_for_unit();
        if (!t) return;
        for (int i = 0; i < 4; ++i) t->borderColor[i] = params[i];
        backend_texture_set_params(t->id, t->minFilter, t->magFilter,
                                   t->wrapS, t->wrapT, t->wrapR, t->borderColor);
        return;
    }
    glTexParameterf(target, pname, params[0]);
}

void glTexParameteriv(GLenum target, GLenum pname, const GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    glTexParameterf(target, pname, (GLfloat)params[0]);
}

void glGenerateMipmap(GLenum target) {
    MITHRIL_ENSURE_INIT();
    (void)target;
    auto t = bound_texture_for_unit();
    if (!t) return;
    // Compute mip level count if the app never called glTexStorage*(levels=N).
    // glGenerateMipmap is the legacy way to request a full mip chain: the
    // driver allocates log2(max(w,h))+1 levels. Our Vulkan texture was
    // created with whatever level count was last set on the GL object; if
    // it's still 1, the backend's generate_mipmaps becomes a no-op, so the
    // app sees the base level only. This is acceptable for MC Java (its
    // modern pipeline uses glTexStorage2D for mipmapped textures).
    t->generateMipmaps = true;
    backend_generate_mipmaps(t->id);
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, void* pixels) {
    MITHRIL_ENSURE_INIT();
    if (!pixels || width <= 0 || height <= 0) return;
    // Delegate to the backend: it resolves the current colour attachment
    // (EGL default framebuffer or the user FBO's GL_COLOR_ATTACHMENT0),
    // transitions it to TRANSFER_SRC_OPTIMAL, copies into a host-visible
    // staging buffer via vkCmdCopyImageToBuffer, and synchronously maps +
    // memcpy's into the caller's buffer. Returns 0 if readback isn't
    // possible (e.g. no FBO bound to the default framebuffer).
    (void)backend_read_pixels((int)x, (int)y, (int)width, (int)height,
                              format, type, pixels);
}

void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
                      GLint x, GLint y, GLsizei width, GLsizei height, GLint border) {
    MITHRIL_ENSURE_INIT();
    (void)target; (void)level; (void)internalformat;
    (void)x; (void)y; (void)width; (void)height; (void)border;
}

void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                         GLint x, GLint y, GLsizei width, GLsizei height) {
    MITHRIL_ENSURE_INIT();
    (void)target; (void)level; (void)xoffset; (void)yoffset;
    (void)x; (void)y; (void)width; (void)height;
}

} // extern "C"
