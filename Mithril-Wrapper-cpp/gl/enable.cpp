// Mithril-Wrapper - gl/enable.cpp
// MobileGlues-style split of gl.cpp: capability enable/disable state.
//
// Holds the glEnable / glDisable / glIsEnabled entry points (plus the
// indexed glEnablei / glDisablei / glIsEnabledi variants) extracted from the
// former monolithic gl/gl.cpp so the file mirrors the MobileGlues layout
// (enable.cpp = capability enable/disable state). Bodies are unchanged —
// only the enclosing translation unit moved.
#include "includes.h"
#include "gl.h"

extern "C" {

/* ---- Enable / Disable ----
 * P0-6: capabilities are now managed solely by GLState::setCapability /
 * isCapabilityEnabled (bool fields). The old enabledCaps set is gone, so
 * there is a single source of truth — no possibility of the set and the
 * fields drifting out of sync.
 *
 * FIX (root cause AF - Primitive Restart): GL_PRIMITIVE_RESTART_FIXED_INDEX
 * 在 State.cpp 的 setCapability 中未列项（GL_PRIMITIVE_RESTART 已列项）。
 * 这里在 glEnable/glDisable 入口直接维护 g_state->primitiveRestartFixedIndex，
 * 让 Pipeline.cpp 的 ia.primitiveRestartEnable 能在两者任一启用时为 VK_TRUE。
 * 深度对照 MobileGL VulkanRenderer.cpp:3861-3877。
 */
NATIVE_FUNCTION_HEAD(void, glEnable, GLenum cap)
    MITHRIL_ENSURE_INIT();
    if (cap == GL_PRIMITIVE_RESTART_FIXED_INDEX) {
        g_state->primitiveRestartFixedIndex = true;
        g_state->bumpRenderVersion();
        return;
    }
    // GL spec: enabling an unrecognised capability raises GL_INVALID_ENUM.
    if (!g_state->isKnownCapability(cap)) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    g_state->setCapability(cap, true);
NATIVE_FUNCTION_END

NATIVE_FUNCTION_HEAD(void, glDisable, GLenum cap)
    MITHRIL_ENSURE_INIT();
    if (cap == GL_PRIMITIVE_RESTART_FIXED_INDEX) {
        g_state->primitiveRestartFixedIndex = false;
        g_state->bumpRenderVersion();
        return;
    }
    // GL spec: disabling an unrecognised capability raises GL_INVALID_ENUM.
    if (!g_state->isKnownCapability(cap)) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    g_state->setCapability(cap, false);
NATIVE_FUNCTION_END

NATIVE_FUNCTION_HEAD(GLboolean, glIsEnabled, GLenum cap)
    if (!g_state) return GL_FALSE;
    // FIX (root cause AF): GL_PRIMITIVE_RESTART_FIXED_INDEX 未在
    // State.cpp isCapabilityEnabled 中列项，这里直接读 g_state 字段。
    if (cap == GL_PRIMITIVE_RESTART_FIXED_INDEX) {
        return g_state->primitiveRestartFixedIndex ? GL_TRUE : GL_FALSE;
    }
    return g_state->isCapabilityEnabled(cap) ? GL_TRUE : GL_FALSE;
NATIVE_FUNCTION_END

/* Indexed enable/disable: GL_BLEND is the only capability that is per-draw-
 * buffer. For all other caps the index is ignored (GL_INVALID_OPERATION is
 * not required by the spec for caps that are not per-buffer). */
NATIVE_FUNCTION_HEAD(void, glEnablei, GLenum cap, GLuint index)
    MITHRIL_ENSURE_INIT();
    if (cap == GL_BLEND && index < mithril::kMaxColorAttachments) {
        g_state->blends[index].enabled = true;
        g_state->bumpRenderVersion();
        return;
    }
    g_state->setCapability(cap, true);
NATIVE_FUNCTION_END

NATIVE_FUNCTION_HEAD(void, glDisablei, GLenum cap, GLuint index)
    MITHRIL_ENSURE_INIT();
    if (cap == GL_BLEND && index < mithril::kMaxColorAttachments) {
        g_state->blends[index].enabled = false;
        g_state->bumpRenderVersion();
        return;
    }
    g_state->setCapability(cap, false);
NATIVE_FUNCTION_END

NATIVE_FUNCTION_HEAD(GLboolean, glIsEnabledi, GLenum cap, GLuint index)
    if (!g_state) return GL_FALSE;
    if (cap == GL_BLEND && index < mithril::kMaxColorAttachments) {
        return g_state->blends[index].enabled ? GL_TRUE : GL_FALSE;
    }
    return g_state->isCapabilityEnabled(cap) ? GL_TRUE : GL_FALSE;
NATIVE_FUNCTION_END

} // extern "C"
