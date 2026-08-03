// Mithril-Wrapper - MG_Impl/Stubs.cpp
// Legacy fixed-function entry points provided as no-op stubs so that
// applications dlsym-ing GL 1.x/2.x symbols resolve cleanly. The Core Profile
// path does not call into any of these; they exist only for symbol presence.
//
// Pattern mirrors MobileGlues' STUB_FUNCTION_* macros: a single shared
// definition body that records a debug log line and returns a sensible default.
//
// This is the Vulkan/MoltenVK rewrite of the former gl/stubs.cpp; the bodies
// are identical because the stubs do not depend on the backend.
// Constants missing from our minimal glcorearb.h but used as default field
// values in State.h. Guarded so a future header update won't conflict.
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif
#ifndef GL_INTERLEAVED_ATTRIBS
#define GL_INTERLEAVED_ATTRIBS       0x8C8C
#endif

// ---- GL enums for Sampler / Query / Transform Feedback (absent from minimal glcorearb.h) ----
#ifndef GL_SAMPLER_BINDING
#define GL_SAMPLER_BINDING           0x8919
#endif
#ifndef GL_SAMPLES_PASSED
#define GL_SAMPLES_PASSED            0x8914
#endif
#ifndef GL_ANY_SAMPLES_PASSED
#define GL_ANY_SAMPLES_PASSED        0x8C2F
#endif
#ifndef GL_PRIMITIVES_GENERATED
#define GL_PRIMITIVES_GENERATED      0x8C87
#endif
#ifndef GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN
#define GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN 0x8C88
#endif
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED              0x88BF
#endif
#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP                 0x8E28
#endif
#ifndef GL_QUERY_COUNTER_BITS
#define GL_QUERY_COUNTER_BITS        0x8864
#endif
#ifndef GL_CURRENT_QUERY
#define GL_CURRENT_QUERY             0x8865
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT              0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE    0x8867
#endif
#ifndef GL_TRANSFORM_FEEDBACK
#define GL_TRANSFORM_FEEDBACK        0x8E22
#endif
#ifndef GL_TRANSFORM_FEEDBACK_BINDING
#define GL_TRANSFORM_FEEDBACK_BINDING 0x8E25
#endif
#ifndef GL_TEXTURE_BORDER_COLOR
#define GL_TEXTURE_BORDER_COLOR      0x1004
#endif
#ifndef GL_TEXTURE_LOD_BIAS
#define GL_TEXTURE_LOD_BIAS          0x8501
#endif
#ifndef GL_TEXTURE_MIN_LOD
#define GL_TEXTURE_MIN_LOD           0x813A
#endif
#ifndef GL_TEXTURE_MAX_LOD
#define GL_TEXTURE_MAX_LOD           0x813B
#endif
#ifndef GL_TEXTURE_COMPARE_MODE
#define GL_TEXTURE_COMPARE_MODE      0x884C
#endif
#ifndef GL_TEXTURE_COMPARE_FUNC
#define GL_TEXTURE_COMPARE_FUNC      0x884D
#endif

#include "includes.h"

#include <cstring>

extern "C" {

/* ---- Display lists ---- */
GLboolean glIsList(GLuint) { return GL_FALSE; }
GLuint glGenLists(GLsizei) { return 0; }

/* ---- Selection / feedback ---- */
GLint glRenderMode(GLenum) { return 0; }

/* ---- Misc legacy getters/queries ---- */
void glGetPointerv(GLenum pname, void** params) {
    (void)pname;
    if (params) *params = nullptr;
}

void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (params) *params = 0;
    if (level != 0) return; // only level 0 supported

    // Proxy texture queries: return the dimensions recorded by the last
    // glTexImage2D(GL_PROXY_TEXTURE_2D, ...) call. If the combo was
    // unsupported, valid=false and width/height are 0.
    if (target == GL_PROXY_TEXTURE_2D) {
        switch (pname) {
            case GL_TEXTURE_WIDTH:
                *params = g_state->proxyTexture2D.valid
                        ? g_state->proxyTexture2D.width : 0;
                break;
            case GL_TEXTURE_HEIGHT:
                *params = g_state->proxyTexture2D.valid
                        ? g_state->proxyTexture2D.height : 0;
                break;
            case GL_TEXTURE_INTERNAL_FORMAT:
                *params = g_state->proxyTexture2D.valid
                        ? g_state->proxyTexture2D.internalFormat : 0;
                break;
            default:
                *params = 0;
                break;
        }
        return;
    }

    // Real texture queries: return the tracked dimensions for level 0.
    mithril::Texture* t = nullptr;
    GLuint unit = g_state->activeTextureUnit;
    if (unit < mithril::kMaxTextureUnits) {
        t = mithril::state_get_texture(g_state->boundTextureForUnit(unit));
    }
    if (!t) return;
    switch (pname) {
        case GL_TEXTURE_WIDTH:           *params = t->width; break;
        case GL_TEXTURE_HEIGHT:          *params = t->height; break;
        case GL_TEXTURE_DEPTH:           *params = t->depth; break;
        case GL_TEXTURE_INTERNAL_FORMAT: *params = t->internalFormat; break;
        default:                         *params = 0; break;
    }
}

// glGetTexParameteriv / glGetTexParameterfv / glGetTexImage are implemented
// (returning real tracked values) in MG_Impl/Texture.cpp — see P1-4 fix.

GLboolean glAreTexturesResident(GLsizei, const GLuint*, GLboolean* residences) {
    if (residences) {
        // caller is responsible for sizing residences; mark all resident.
    }
    return GL_TRUE;
}

/* =========================================================================
 * Sampler objects (P1-1 / P2-1)
 * Samplers override the embedded sampler state in Texture objects.  When a
 * sampler is bound to a unit, its params take precedence over the texture's
 * own min/mag/wrap/lod params for that unit.
 * ========================================================================= */
void glGenSamplers(GLsizei n, GLuint* samplers) {
    MITHRIL_ENSURE_INIT();
    mithril::state_gen_names("sampler", n, samplers);
    for (GLsizei i = 0; i < n; ++i) {
        mithril::Sampler s{};
        s.id = samplers[i];
        s.lifetimeId = g_state->nextSamplerLifetimeId++;
        g_state->samplers[samplers[i]] = s;
    }
}

void glDeleteSamplers(GLsizei n, const GLuint* samplers) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !samplers) return;
    for (GLsizei i = 0; i < n; ++i) {
        GLuint name = samplers[i];
        if (name == 0) continue;
        // Unbind from every texture unit.
        for (int u = 0; u < mithril::kMaxTextureUnits; ++u) {
            if (g_state->samplerBindings[u] == name)
                g_state->samplerBindings[u] = 0;
        }
        g_state->samplers.erase(name);
        g_state->samplerNames.release(name);
    }
}

void glBindSampler(GLuint unit, GLuint sampler) {
    MITHRIL_ENSURE_INIT();
    if (unit >= mithril::kMaxTextureUnits) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    if (sampler != 0 && !mithril::state_get_sampler(sampler)) {
        mithril::state_set_error(GL_INVALID_OPERATION);
        return;
    }
    g_state->samplerBindings[unit] = sampler;
}

GLboolean glIsSampler(GLuint sampler) {
    if (!g_state) return GL_FALSE;
    return g_state->samplerNames.valid(sampler) ? GL_TRUE : GL_FALSE;
}

void glSamplerParameteri(GLuint sampler, GLenum pname, GLint param) {
    MITHRIL_ENSURE_INIT();
    mithril::Sampler* s = mithril::state_get_sampler(sampler);
    if (!s) return;
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER:     s->minFilter = param; break;
        case GL_TEXTURE_MAG_FILTER:     s->magFilter = param; break;
        case GL_TEXTURE_WRAP_S:         s->wrapS = param; break;
        case GL_TEXTURE_WRAP_T:         s->wrapT = param; break;
        case GL_TEXTURE_WRAP_R:         s->wrapR = param; break;
        case GL_TEXTURE_MIN_LOD:        s->minLod = (GLfloat)param; break;
        case GL_TEXTURE_MAX_LOD:        s->maxLod = (GLfloat)param; break;
        case GL_TEXTURE_LOD_BIAS:       s->lodBias = (GLfloat)param; break;
        case GL_TEXTURE_COMPARE_MODE:   s->compareMode = param; break;
        case GL_TEXTURE_COMPARE_FUNC:   s->compareFunc = param; break;
        default: mithril::state_set_error(GL_INVALID_ENUM); return;
    }
    s->version++;
}

void glSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param) {
    MITHRIL_ENSURE_INIT();
    mithril::Sampler* s = mithril::state_get_sampler(sampler);
    if (!s) return;
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER:     s->minFilter = (GLint)param; break;
        case GL_TEXTURE_MAG_FILTER:     s->magFilter = (GLint)param; break;
        case GL_TEXTURE_WRAP_S:         s->wrapS = (GLint)param; break;
        case GL_TEXTURE_WRAP_T:         s->wrapT = (GLint)param; break;
        case GL_TEXTURE_WRAP_R:         s->wrapR = (GLint)param; break;
        case GL_TEXTURE_MIN_LOD:        s->minLod = param; break;
        case GL_TEXTURE_MAX_LOD:        s->maxLod = param; break;
        case GL_TEXTURE_LOD_BIAS:       s->lodBias = param; break;
        case GL_TEXTURE_COMPARE_MODE:   s->compareMode = (GLint)param; break;
        case GL_TEXTURE_COMPARE_FUNC:   s->compareFunc = (GLint)param; break;
        case GL_TEXTURE_BORDER_COLOR:   s->borderColor[0] = param; break;
        default: mithril::state_set_error(GL_INVALID_ENUM); return;
    }
    s->version++;
}

void glSamplerParameteriv(GLuint sampler, GLenum pname, const GLint* params) {
    MITHRIL_ENSURE_INIT();
    mithril::Sampler* s = mithril::state_get_sampler(sampler);
    if (!s || !params) return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        s->borderColorI[0] = params[0]; s->borderColorI[1] = params[1];
        s->borderColorI[2] = params[2]; s->borderColorI[3] = params[3];
        s->version++;
        return;
    }
    glSamplerParameteri(sampler, pname, params[0]);
}

void glSamplerParameterfv(GLuint sampler, GLenum pname, const GLfloat* params) {
    MITHRIL_ENSURE_INIT();
    mithril::Sampler* s = mithril::state_get_sampler(sampler);
    if (!s || !params) return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        s->borderColor[0] = params[0]; s->borderColor[1] = params[1];
        s->borderColor[2] = params[2]; s->borderColor[3] = params[3];
        s->version++;
        return;
    }
    glSamplerParameterf(sampler, pname, params[0]);
}

/* =========================================================================
 * Query objects (P1-1 / P2-1)
 * Occlusion / primitives-generated / timer queries.  State is tracked; the
 * backend result retrieval is deferred (returns 0 / GL_FALSE until wired).
 * ========================================================================= */
static mithril::QueryTarget query_target_from_gl(GLenum target) {
    switch (target) {
        case GL_SAMPLES_PASSED:             return mithril::QueryTarget::SamplesPassed;
        case GL_ANY_SAMPLES_PASSED:         return mithril::QueryTarget::AnySamplesPassed;
        case GL_PRIMITIVES_GENERATED:       return mithril::QueryTarget::PrimitivesGenerated;
        case GL_TIME_ELAPSED:               return mithril::QueryTarget::TimeElapsed;
        default:                            return mithril::QueryTarget::Count;
    }
}

void glGenQueries(GLsizei n, GLuint* ids) {
    MITHRIL_ENSURE_INIT();
    mithril::state_gen_names("query", n, ids);
    for (GLsizei i = 0; i < n; ++i) {
        mithril::Query q{};
        q.id = ids[i];
        g_state->queries[ids[i]] = q;
    }
}

void glDeleteQueries(GLsizei n, const GLuint* ids) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !ids) return;
    for (GLsizei i = 0; i < n; ++i) {
        GLuint name = ids[i];
        if (name == 0) continue;
        g_state->queries.erase(name);
        g_state->queryNames.release(name);
    }
}

GLboolean glIsQuery(GLuint id) {
    if (!g_state) return GL_FALSE;
    if (!g_state->queryNames.valid(id)) return GL_FALSE;
    mithril::Query* q = mithril::state_get_query(id);
    return (q && q->ended) ? GL_TRUE : GL_FALSE;
}

void glBeginQuery(GLenum target, GLuint id) {
    MITHRIL_ENSURE_INIT();
    mithril::QueryTarget qt = query_target_from_gl(target);
    if (qt == mithril::QueryTarget::Count) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    mithril::Query* q = mithril::state_get_query(id);
    if (!q) { mithril::state_set_error(GL_INVALID_OPERATION); return; }
    q->target = qt;
    q->active = true;
    q->ended = false;
    q->resultCached = false;
}

void glEndQuery(GLenum target) {
    MITHRIL_ENSURE_INIT();
    mithril::QueryTarget qt = query_target_from_gl(target);
    if (qt == mithril::QueryTarget::Count) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    // Find the active query for this target and end it.
    for (auto& [id, q] : g_state->queries) {
        if (q.active && q.target == qt) {
            q.active = false;
            q.ended = true;
            break;
        }
    }
}

void glGetQueryiv(GLenum target, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    switch (pname) {
        case GL_QUERY_COUNTER_BITS: *params = 64; break;
        case GL_CURRENT_QUERY:      *params = 0; break;  // no active query tracked per-target
        default:                     *params = 0; break;
    }
}

void glGetQueryObjectiv(GLuint id, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    mithril::Query* q = mithril::state_get_query(id);
    if (!q || !q->ended) { *params = 0; return; }
    switch (pname) {
        case GL_QUERY_RESULT_AVAILABLE: *params = q->resultCached ? GL_TRUE : GL_FALSE; break;
        case GL_QUERY_RESULT:           *params = (GLint)q->cachedResult; break;
        default:                         *params = 0; break;
    }
}

void glGetQueryObjectuiv(GLuint id, GLenum pname, GLuint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    mithril::Query* q = mithril::state_get_query(id);
    if (!q || !q->ended) { *params = 0; return; }
    switch (pname) {
        case GL_QUERY_RESULT_AVAILABLE: *params = q->resultCached ? GL_TRUE : GL_FALSE; break;
        case GL_QUERY_RESULT:           *params = (GLuint)q->cachedResult; break;
        default:                         *params = 0; break;
    }
}

void glQueryCounter(GLuint id, GLenum target) {
    MITHRIL_ENSURE_INIT();
    if (target != GL_TIMESTAMP) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    mithril::Query* q = mithril::state_get_query(id);
    if (!q) { mithril::state_set_error(GL_INVALID_OPERATION); return; }
    q->target = mithril::QueryTarget::Timestamp;
    q->ended = true;
}

/* =========================================================================
 * Transform Feedback objects (P1-1 / P2-1)
 * State tracking only — backend wiring deferred.
 * ========================================================================= */
void glGenTransformFeedbacks(GLsizei n, GLuint* ids) {
    MITHRIL_ENSURE_INIT();
    mithril::state_gen_names("tf", n, ids);
    for (GLsizei i = 0; i < n; ++i) {
        mithril::TransformFeedback tf{};
        tf.id = ids[i];
        g_state->transformFeedbacks[ids[i]] = tf;
    }
}

void glDeleteTransformFeedbacks(GLsizei n, const GLuint* ids) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !ids) return;
    for (GLsizei i = 0; i < n; ++i) {
        GLuint name = ids[i];
        if (name == 0) continue;
        if (g_state->currentTransformFeedback == name)
            g_state->currentTransformFeedback = 0;
        g_state->transformFeedbacks.erase(name);
        g_state->tfNames.release(name);
    }
}

void glBindTransformFeedback(GLenum target, GLuint id) {
    MITHRIL_ENSURE_INIT();
    if (target != GL_TRANSFORM_FEEDBACK) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    if (id != 0 && !mithril::state_get_transform_feedback(id)) {
        mithril::TransformFeedback tf{};
        tf.id = id;
        g_state->transformFeedbacks[id] = tf;
    }
    g_state->currentTransformFeedback = id;
}

GLboolean glIsTransformFeedback(GLuint id) {
    if (!g_state) return GL_FALSE;
    return g_state->tfNames.valid(id) ? GL_TRUE : GL_FALSE;
}

void glBeginTransformFeedback(GLenum primitiveMode) {
    MITHRIL_ENSURE_INIT();
    mithril::TransformFeedback* tf =
        mithril::state_get_transform_feedback(g_state->currentTransformFeedback);
    if (!tf) return;
    tf->active = true;
    tf->paused = false;
    tf->primitiveMode = primitiveMode;
}

void glEndTransformFeedback(void) {
    MITHRIL_ENSURE_INIT();
    mithril::TransformFeedback* tf =
        mithril::state_get_transform_feedback(g_state->currentTransformFeedback);
    if (!tf) return;
    tf->active = false;
    tf->paused = false;
}

void glPauseTransformFeedback(void) {
    MITHRIL_ENSURE_INIT();
    mithril::TransformFeedback* tf =
        mithril::state_get_transform_feedback(g_state->currentTransformFeedback);
    if (tf && tf->active) tf->paused = true;
}

void glResumeTransformFeedback(void) {
    MITHRIL_ENSURE_INIT();
    mithril::TransformFeedback* tf =
        mithril::state_get_transform_feedback(g_state->currentTransformFeedback);
    if (tf && tf->paused) tf->paused = false;
}

} // extern "C"
