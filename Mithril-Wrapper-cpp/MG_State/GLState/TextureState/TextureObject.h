// Mithril-Wrapper - MG_State/GLState/TextureState/TextureObject.h
//
// TextureObject: the per-name texture record migrated from the flat
// MG_State/State.h `struct Texture`. It carries the GL-side metadata for a
// single texture object name (id, target, internal format, dimensions, mipmap
// level count, compressed flag, the filter/wrap sample-state, border colour,
// the generateMipmaps hint) plus a `markedForDeletion` flag.
//
// The filter/wrap fields are stored directly as GLenum-valued GLint values
// (i.e. the raw GL_* constants) rather than wrapped in our own strongly-typed
// enums, matching the legacy `struct Texture` and keeping the per-name record
// simple; the abstraction layer may introduce typed enums later.
//
// The Vulkan-side VkImage handle is NOT owned here: the DirectVulkan backend
// allocates/destroys the real device memory and releases it through its
// disposal queue once in-flight GPU work completes. TextureObject is purely
// the GL name-layer state, mirroring the historical split in State.h where
// `Texture` held only GL fields.
#pragma once

#include <cstdint>

#include <GL/gl.h>

#include "TextureEnum.h"

namespace mithril::glstate {

// Fields are public so the state-machine components and the backend can read
// and update them directly, matching the legacy `struct Texture` usage pattern.
// Defaults mirror the OpenGL 3.3 Core values inherited from State.h.
struct TextureObject {
    uint32_t id = 0;
    TextureTarget target = TextureTarget::Texture2D;
    GLint internalFormat = GL_RGBA8;
    GLsizei width = 0;
    GLsizei height = 0;
    GLsizei depth = 1;
    GLint levels = 1;
    bool isCompressed = false;

    // Sample state stored as raw GL enum values (see file header note).
    GLint minFilter = GL_NEAREST_MIPMAP_LINEAR;
    GLint magFilter = GL_LINEAR;
    GLint wrapS = GL_REPEAT;
    GLint wrapT = GL_REPEAT;
    GLint wrapR = GL_REPEAT;
    GLfloat borderColor[4] = {0, 0, 0, 0};

    bool generateMipmaps = false;
    bool markedForDeletion = false;

    TextureObject();
    explicit TextureObject(uint32_t id_);
};

} // namespace mithril::glstate
