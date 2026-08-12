// Mithril-Wrapper - egl/context.cpp
//
// Implementation of the MGContext tracking model (see context.h). This is an
// ADDITIVE layer alongside the existing EglContext; it does NOT own GLState
// and does NOT free EglContext. It only tracks per-context metadata and gives
// the rest of the layer a stable monotonic id + thread-local current pointer.
#include "context.h"

#include <unordered_map>
#include <mutex>
#include <utility>

// ---------------------------------------------------------------------------
// Monotonic id counters. Context ids and share-group ids are never reused:
// EGLContext is a driver heap allocation, so destroying one and creating
// another frequently yields the same address; keying state tables by address
// would report "same context" for a context that never owned the cached
// objects. The monotonic id avoids that.
// ---------------------------------------------------------------------------
static std::atomic<unsigned long long> g_next_ctx_id{1};
static std::atomic<unsigned long long> g_next_share_group_id{1};

// ---------------------------------------------------------------------------
// Per-context map. Keyed by EGLContext handle (the EglContext* cast). Holds
// MGContext through shared_ptr so a thread that has the context current can
// keep its record alive (g_current_ref) even if another thread erases it from
// the map (e.g. eglDestroyContext on a context still current elsewhere).
// ---------------------------------------------------------------------------
static std::unordered_map<EGLContext, std::shared_ptr<MGContext>> g_contexts;
static std::mutex g_context_mutex;

// The MGContext current on THIS thread, plus the matching shared_ptr that
// keeps it alive while it is current. g_current_ctx is the raw pointer
// (declared extern in context.h); g_current_ref is the owning ref private to
// this TU. Both are written together by mg_context_make_current.
thread_local MGContext* g_current_ctx = nullptr;
static thread_local std::shared_ptr<MGContext> g_current_ref;

// ---------------------------------------------------------------------------
// Per-display eglInitialize accounting. Two holders (probe / app) per
// display, not a count, because eglInitialize is idempotent. mg_display_release
// returns true only when BOTH holders are now false (this was the last one).
// ---------------------------------------------------------------------------
static std::mutex g_display_mutex;
static std::unordered_map<EGLDisplay, std::pair<bool, bool>> g_display_refs; // {probe, app}

// ---------------------------------------------------------------------------
// mg_context_create
//
// Allocate a new MGContext, assign a monotonic id, fill in the metadata from
// the EGL wrapper's args. For share_group: if share_handle resolves to an
// existing MGContext, reuse its share_group (so sibling contexts share the
// same group id); otherwise create a fresh MGShareGroup with a new id.
//
// Stores the new record in the map under `handle` and returns a raw pointer
// (the map retains the shared_ptr). The caller (egl.cpp) does NOT need to
// free the result — mg_context_destroy handles map teardown.
// ---------------------------------------------------------------------------
MGContext* mg_context_create(EGLDisplay dpy, EGLContext handle, EGLContext share_handle,
                             EGLenum client_type, EGLint major, EGLint minor,
                             EGLint profile_mask, EGLint context_flags,
                             mithril::GLState* state) {
    if (handle == EGL_NO_CONTEXT) return nullptr;

    auto ctx = std::make_shared<MGContext>();
    ctx->id = g_next_ctx_id.fetch_add(1);
    ctx->display = dpy;
    ctx->handle = handle;
    ctx->client_type = client_type;
    ctx->granted_major = major;
    ctx->granted_minor = minor;
    ctx->profile_mask = profile_mask;
    ctx->context_flags = context_flags;
    ctx->state = state;

    // Resolve the share group: reuse the share context's group if it exists,
    // otherwise allocate a new group id. Sibling contexts (sharing a parent)
    // all reference the same MGShareGroup and thus share the same group id.
    if (share_handle != EGL_NO_CONTEXT) {
        std::lock_guard<std::mutex> lk(g_context_mutex);
        auto it = g_contexts.find(share_handle);
        if (it != g_contexts.end() && it->second->share_group) {
            ctx->share_group = it->second->share_group;
        }
    }
    if (!ctx->share_group) {
        ctx->share_group = std::make_shared<MGShareGroup>(
            g_next_share_group_id.fetch_add(1));
    }

    MGContext* raw = ctx.get();
    {
        std::lock_guard<std::mutex> lk(g_context_mutex);
        g_contexts[handle] = std::move(ctx);
    }
    return raw;
}

// ---------------------------------------------------------------------------
// mg_context_make_current
//
// Update this thread's current MGContext pointer + the keeping-it-alive ref.
// The actual g_state swap + t_currentCtx update is done by egl.cpp's
// eglMakeCurrent; this function only tracks the MGContext side and dispatches
// the per-subsystem bind hooks so each subsystem can swap its own thread_local
// current pointer.
//
// On the detach case (handle == EGL_NO_CONTEXT) we drop the thread's current
// ref and clear g_current_ctx; egl.cpp has already cleared g_state /
// t_currentCtx.
// ---------------------------------------------------------------------------
void mg_context_make_current(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                             EGLContext handle) {
    (void)dpy; // display is recorded on the MGContext at create time

    if (handle == EGL_NO_CONTEXT) {
        g_current_ctx = nullptr;
        g_current_ref.reset();
        return;
    }

    std::shared_ptr<MGContext> ctx;
    {
        std::lock_guard<std::mutex> lk(g_context_mutex);
        auto it = g_contexts.find(handle);
        if (it == g_contexts.end()) return;
        ctx = it->second;
    }

    ctx->draw = draw;
    ctx->read = read;
    ctx->current_count.fetch_add(1, std::memory_order_relaxed);

    // Publish the thread-local current pointer + the keeping-it-alive ref.
    // Order matters: store g_current_ref first so the record is kept alive
    // before any reader observes g_current_ctx pointing at it.
    g_current_ref = ctx;
    g_current_ctx = ctx.get();

    // Dispatch per-subsystem bind hooks. Each subsystem swaps its own
    // thread_local current pointer (keyed by MGContext::id); the share-group
    // id lets buffer/texture tables fall back to the shared group table.
    const unsigned long long group_id =
        ctx->share_group ? ctx->share_group->id : 0;
    mg_buffer_bind_context(ctx->id, group_id);
    mg_texture_bind_context(ctx->id, group_id);
    mg_framebuffer_bind_context(ctx->id);
    mg_enable_bind_context(ctx->id);
    mg_pixel_bind_context(ctx->id);
    mg_gl_bind_context(ctx->id);
    mg_program_bind_context(ctx->id);
    mg_shader_bind_context(ctx->id);
    mg_vertexattrib_bind_context(ctx->id);
    mg_sync_bind_context(ctx->id);
    mg_query_bind_context(ctx->id);
    mg_transformfeedback_bind_context(ctx->id);
}

// ---------------------------------------------------------------------------
// mg_context_destroy
//
// Mark the record destroy-pending and erase it from the map. Dispatch the
// per-subsystem forget hooks so each subsystem can drop its per-context state
// table entry.
//
// IMPORTANT: this does NOT call state_destroy or delete the EglContext — the
// EglContext (and its GLState) is owned and freed by egl.cpp's existing
// refcount logic (state_destroy + delete c in eglDestroyContext). MGContext
// only tracks metadata.
//
// For simplicity (and because egl.cpp's eglDestroyContext already detaches
// the context if it is current on THIS thread and frees the EglContext when
// refcount hits 0), we erase the MGContext unconditionally here. The
// current_count tracking is retained on the record for the benefit of a
// future MobileGlues-style deferred-destruction path; we do not block on it
// today.
// ---------------------------------------------------------------------------
void mg_context_destroy(EGLContext handle) {
    if (handle == EGL_NO_CONTEXT) return;

    std::shared_ptr<MGContext> ctx;
    {
        std::lock_guard<std::mutex> lk(g_context_mutex);
        auto it = g_contexts.find(handle);
        if (it == g_contexts.end()) return;
        ctx = it->second;
        g_contexts.erase(it);
    }
    if (!ctx) return;

    ctx->destroy_pending = true;
    const unsigned long long id = ctx->id;

    // Dispatch forget hooks so subsystems can drop their per-context entries.
    mg_buffer_forget_context(id);
    mg_texture_forget_context(id);
    mg_framebuffer_forget_context(id);
    mg_enable_forget_context(id);
    mg_pixel_forget_context(id);
    mg_gl_forget_context(id);
    mg_program_forget_context(id);
    mg_shader_forget_context(id);
    mg_vertexattrib_forget_context(id);
    mg_sync_forget_context(id);
    mg_query_forget_context(id);
    mg_transformfeedback_forget_context(id);

    // `ctx` (the shared_ptr) drops here. If this thread still has it current
    // (g_current_ref holds a copy), the record stays alive until this thread
    // later calls mg_context_make_current(EGL_NO_CONTEXT) — at which point
    // g_current_ref.reset() releases the last ref and the record is freed.
}

// ---------------------------------------------------------------------------
// mg_context_find
//
// Look up an MGContext by EGLContext handle. Returns a raw pointer (valid
// only while the caller holds g_context_mutex, OR while the caller knows the
// record cannot be erased — e.g. the calling thread has it current). Returns
// nullptr if no such handle is registered.
// ---------------------------------------------------------------------------
MGContext* mg_context_find(EGLContext handle) {
    if (handle == EGL_NO_CONTEXT) return nullptr;
    std::lock_guard<std::mutex> lk(g_context_mutex);
    auto it = g_contexts.find(handle);
    if (it == g_contexts.end()) return nullptr;
    return it->second.get();
}

// ---------------------------------------------------------------------------
// Per-subsystem per-context state tables + bind/forget hooks.
//
// Each subsystem keeps a per-context table keyed by MGContext::id, holding its
// state struct through unique_ptr (the unordered_map is open addressing and
// moves its elements when it grows, so the record itself must stay put — the
// same reason MobileGlues holds its records through unique_ptr). A thread_local
// current pointer is swapped by bind_context; forget_context erases the entry.
//
// Subview subsystems (buffer/texture/framebuffer/pixel/gl/program/shader/
// vertexattrib) hold a mithril::GLState* back-pointer. bind_context sets it to
// the current context's GLState (g_current_ctx->state, the same pointer egl.cpp
// installed as mithril::g_state). The actual data still lives in GLState — this
// only puts the distributed accessor API in place so access-point migration is
// a later, mechanical task.
//
// mg_enable_state_t is STANDALONE (no back-pointer): bind_context copies the
// current GLState capability bools into it. The projection is one-way for now
// (the enable/disable entry points still write GLState); making it the source
// of truth is a later task.
//
// sync/query/transformfeedback are no-op stubs (their tables still live in
// GLState); the hooks exist so mg_context_make_current / mg_context_destroy
// can dispatch uniformly across all subsystems.
// ---------------------------------------------------------------------------

// --- buffer ---
static std::unordered_map<unsigned long long, std::unique_ptr<mg_buffer_state_t>> g_buffer_table;
static thread_local mg_buffer_state_t* g_buffer_current = nullptr;
void mg_buffer_bind_context(unsigned long long ctx_id, unsigned long long group_id) {
    (void)group_id; // share-group-scoped object table migration is a later task
    auto& slot = g_buffer_table[ctx_id];
    if (!slot) slot = std::make_unique<mg_buffer_state_t>();
    slot->state = g_current_ctx ? g_current_ctx->state : nullptr;
    g_buffer_current = slot.get();
}
void mg_buffer_forget_context(unsigned long long ctx_id) { g_buffer_table.erase(ctx_id); }

// --- texture ---
static std::unordered_map<unsigned long long, std::unique_ptr<mg_texture_state_t>> g_texture_table;
static thread_local mg_texture_state_t* g_texture_current = nullptr;
void mg_texture_bind_context(unsigned long long ctx_id, unsigned long long group_id) {
    (void)group_id;
    auto& slot = g_texture_table[ctx_id];
    if (!slot) slot = std::make_unique<mg_texture_state_t>();
    slot->state = g_current_ctx ? g_current_ctx->state : nullptr;
    g_texture_current = slot.get();
}
void mg_texture_forget_context(unsigned long long ctx_id) { g_texture_table.erase(ctx_id); }

// --- framebuffer ---
static std::unordered_map<unsigned long long, std::unique_ptr<mg_framebuffer_state_t>> g_framebuffer_table;
static thread_local mg_framebuffer_state_t* g_framebuffer_current = nullptr;
void mg_framebuffer_bind_context(unsigned long long ctx_id) {
    auto& slot = g_framebuffer_table[ctx_id];
    if (!slot) slot = std::make_unique<mg_framebuffer_state_t>();
    slot->state = g_current_ctx ? g_current_ctx->state : nullptr;
    g_framebuffer_current = slot.get();
}
void mg_framebuffer_forget_context(unsigned long long ctx_id) { g_framebuffer_table.erase(ctx_id); }

// --- pixel ---
static std::unordered_map<unsigned long long, std::unique_ptr<mg_pixel_state_t>> g_pixel_table;
static thread_local mg_pixel_state_t* g_pixel_current = nullptr;
void mg_pixel_bind_context(unsigned long long ctx_id) {
    auto& slot = g_pixel_table[ctx_id];
    if (!slot) slot = std::make_unique<mg_pixel_state_t>();
    slot->state = g_current_ctx ? g_current_ctx->state : nullptr;
    g_pixel_current = slot.get();
}
void mg_pixel_forget_context(unsigned long long ctx_id) { g_pixel_table.erase(ctx_id); }

// --- gl (core scalars: current program / tex unit / draw fbo / proxy / pixel-store) ---
static std::unordered_map<unsigned long long, std::unique_ptr<gl_state_s>> g_gl_table;
static thread_local gl_state_s* g_gl_current = nullptr;
void mg_gl_bind_context(unsigned long long ctx_id) {
    auto& slot = g_gl_table[ctx_id];
    if (!slot) slot = std::make_unique<gl_state_s>();
    slot->state = g_current_ctx ? g_current_ctx->state : nullptr;
    g_gl_current = slot.get();
}
void mg_gl_forget_context(unsigned long long ctx_id) { g_gl_table.erase(ctx_id); }

// --- program ---
static std::unordered_map<unsigned long long, std::unique_ptr<mg_program_state_t>> g_program_table;
static thread_local mg_program_state_t* g_program_current = nullptr;
void mg_program_bind_context(unsigned long long ctx_id) {
    auto& slot = g_program_table[ctx_id];
    if (!slot) slot = std::make_unique<mg_program_state_t>();
    slot->state = g_current_ctx ? g_current_ctx->state : nullptr;
    g_program_current = slot.get();
}
void mg_program_forget_context(unsigned long long ctx_id) { g_program_table.erase(ctx_id); }

// --- shader ---
static std::unordered_map<unsigned long long, std::unique_ptr<mg_shader_state_t>> g_shader_table;
static thread_local mg_shader_state_t* g_shader_current = nullptr;
void mg_shader_bind_context(unsigned long long ctx_id) {
    auto& slot = g_shader_table[ctx_id];
    if (!slot) slot = std::make_unique<mg_shader_state_t>();
    slot->state = g_current_ctx ? g_current_ctx->state : nullptr;
    g_shader_current = slot.get();
}
void mg_shader_forget_context(unsigned long long ctx_id) { g_shader_table.erase(ctx_id); }

// --- vertexattrib (VAO) ---
static std::unordered_map<unsigned long long, std::unique_ptr<mg_vertexattrib_state_t>> g_vertexattrib_table;
static thread_local mg_vertexattrib_state_t* g_vertexattrib_current = nullptr;
void mg_vertexattrib_bind_context(unsigned long long ctx_id) {
    auto& slot = g_vertexattrib_table[ctx_id];
    if (!slot) slot = std::make_unique<mg_vertexattrib_state_t>();
    slot->state = g_current_ctx ? g_current_ctx->state : nullptr;
    g_vertexattrib_current = slot.get();
}
void mg_vertexattrib_forget_context(unsigned long long ctx_id) { g_vertexattrib_table.erase(ctx_id); }

// --- enable (standalone: own data, no GLState* back-pointer) ---
static std::unordered_map<unsigned long long, std::unique_ptr<mg_enable_state_t>> g_enable_table;
static thread_local mg_enable_state_t* g_enable_current = nullptr;

// One-way projection of the centralized GLState capability bools into the
// standalone enable table. Called from mg_enable_bind_context. Reads GLState
// only (does not modify it), so it does not touch any existing g_state-> access
// point — access-point migration is a later, independent task.
static void mg_enable_sync_from_glstate(mg_enable_state_t* s, const mithril::GLState* g) {
    if (!s) return;
    for (int i = 0; i < MGC_COUNT; ++i) s->scalar[i] = GL_FALSE;
    for (int i = 0; i < MG_MAX_DRAW_BUFFERS; ++i) s->blend_indexed[i] = GL_FALSE;
    for (int i = 0; i < MG_MAX_VIEWPORTS; ++i) s->scissor_indexed[i] = GL_FALSE;
    s->clip_distance_mask = 0;
    s->primitive_restart_index = 0;

    if (!g) { s->initialised = true; s->driver_synced = false; return; }

    // Scalar capabilities GLState tracks as named bool fields.
    s->scalar[MGC_BLEND]                     = g->blends[0].enabled ? GL_TRUE : GL_FALSE;
    s->scalar[MGC_CULL_FACE]                 = g->cullFace ? GL_TRUE : GL_FALSE;
    s->scalar[MGC_DEPTH_CLAMP]               = g->depthClamp ? GL_TRUE : GL_FALSE;
    s->scalar[MGC_DEPTH_TEST]                = g->depthTest ? GL_TRUE : GL_FALSE;
    s->scalar[MGC_DITHER]                    = g->dither ? GL_TRUE : GL_FALSE;
    s->scalar[MGC_FRAMEBUFFER_SRGB]          = g->framebufferSRGB ? GL_TRUE : GL_FALSE;
    s->scalar[MGC_MULTISAMPLE]               = g->multisample ? GL_TRUE : GL_FALSE;
    s->scalar[MGC_POLYGON_OFFSET_FILL]       = g->polygonOffsetFill ? GL_TRUE : GL_FALSE;
    s->scalar[MGC_PRIMITIVE_RESTART]         = g->primitiveRestart ? GL_TRUE : GL_FALSE;
    s->scalar[MGC_PRIMITIVE_RESTART_FIXED_INDEX] = g->primitiveRestartFixedIndex ? GL_TRUE : GL_FALSE;
    s->scalar[MGC_PROGRAM_POINT_SIZE]        = g->programPointSize ? GL_TRUE : GL_FALSE;
    s->scalar[MGC_RASTERIZER_DISCARD]        = g->rasterizerDiscard ? GL_TRUE : GL_FALSE;
    s->scalar[MGC_SAMPLE_ALPHA_TO_COVERAGE]  = g->sampleAlphaToCoverage ? GL_TRUE : GL_FALSE;
    s->scalar[MGC_SAMPLE_COVERAGE]           = g->sampleCoverage ? GL_TRUE : GL_FALSE;
    s->scalar[MGC_SAMPLE_MASK]               = g->sampleMask ? GL_TRUE : GL_FALSE;
    s->scalar[MGC_SAMPLE_SHADING]            = g->sampleShadingEnabled ? GL_TRUE : GL_FALSE;
    s->scalar[MGC_SCISSOR_TEST]              = g->scissorTest ? GL_TRUE : GL_FALSE;
    s->scalar[MGC_STENCIL_TEST]              = g->stencilTest ? GL_TRUE : GL_FALSE;
    s->scalar[MGC_TEXTURE_CUBE_MAP_SEAMLESS] = g->textureCubeMapSeamless ? GL_TRUE : GL_FALSE;
    // (COLOR_LOGIC_OP, DEBUG_OUTPUT*, LINE_SMOOTH, POLYGON_OFFSET_LINE/POINT,
    //  POLYGON_SMOOTH, SAMPLE_ALPHA_TO_ONE are not tracked by GLState today;
    //  they stay GL_FALSE — matching the GL default for the ones GLState omits.)

    // Per-draw-buffer GL_BLEND. GLState tracks up to kMaxColorAttachments.
    for (int i = 0; i < MG_MAX_DRAW_BUFFERS && i < mithril::kMaxColorAttachments; ++i)
        s->blend_indexed[i] = g->blends[i].enabled ? GL_TRUE : GL_FALSE;

    // Per-viewport GL_SCISSOR_TEST. GLState tracks a single scissor today, so
    // mirror it to slot 0; the rest stay GL_FALSE (GL 3.3 core guarantees one
    // viewport anyway).
    s->scissor_indexed[0] = g->scissorTest ? GL_TRUE : GL_FALSE;

    // GL_CLIP_DISTANCE0..7 as a bitmask.
    for (int i = 0; i < mithril::kMaxClipDistances && i < MG_MAX_CLIP_DISTANCES; ++i)
        if (g->clipDistance[i]) s->clip_distance_mask |= (1u << i);

    s->primitive_restart_index = g->primitiveRestartIndex;
    s->initialised = true;
    s->driver_synced = false; // driver sync not wired yet (future task)
}

void mg_enable_bind_context(unsigned long long ctx_id) {
    auto& slot = g_enable_table[ctx_id];
    if (!slot) slot = std::make_unique<mg_enable_state_t>();
    mg_enable_sync_from_glstate(slot.get(), g_current_ctx ? g_current_ctx->state : nullptr);
    g_enable_current = slot.get();
}
void mg_enable_forget_context(unsigned long long ctx_id) { g_enable_table.erase(ctx_id); }

// --- sync / query / transformfeedback (no-op stubs; tables still in GLState) ---
void mg_sync_bind_context(unsigned long long) {}
void mg_query_bind_context(unsigned long long) {}
void mg_transformfeedback_bind_context(unsigned long long) {}
void mg_sync_forget_context(unsigned long long) {}
void mg_query_forget_context(unsigned long long) {}
void mg_transformfeedback_forget_context(unsigned long long) {}

// ---------------------------------------------------------------------------
// Per-display eglInitialize accounting.
// ---------------------------------------------------------------------------
void mg_display_initialised(EGLDisplay dpy, bool probe) {
    std::lock_guard<std::mutex> lk(g_display_mutex);
    auto& slot = g_display_refs[dpy];
    if (probe) slot.first = true;
    else       slot.second = true;
}

bool mg_display_release(EGLDisplay dpy, bool probe) {
    std::lock_guard<std::mutex> lk(g_display_mutex);
    auto it = g_display_refs.find(dpy);
    if (it == g_display_refs.end()) return true; // nothing held -> "last"
    auto& slot = it->second;
    if (probe) slot.first = false;
    else       slot.second = false;
    bool last = (!slot.first && !slot.second);
    if (last) g_display_refs.erase(it);
    return last;
}

// ---------------------------------------------------------------------------
// Per-subsystem state accessors.
//
// Each returns the thread_local current pointer swapped by the matching
// bind_context hook. mg_gl_state() keeps returning the whole mithril::GLState*
// for backward compatibility.
//
// Subview fallback: when no MGContext is bound on this thread (e.g. headless
// unit tests that set g_state directly without going through EGL), each subview
// accessor returns a thread_local fallback entry whose GLState* back-pointer is
// the centralized g_state — so future access-point migration works in the
// headless path too. Returns nullptr when g_state is also null (no context at
// all). mg_enable_current() has no fallback because enable state is standalone
// (no back-pointer); it returns nullptr in the headless case.
// ---------------------------------------------------------------------------
namespace mithril {

GLState* mg_gl_state() { return g_state; }

mg_buffer_state_t* mg_buffer_current() {
    if (g_buffer_current) return g_buffer_current;
    static thread_local mg_buffer_state_t fallback{};
    fallback.state = g_state;
    return g_state ? &fallback : nullptr;
}

mg_texture_state_t* mg_texture_current() {
    if (g_texture_current) return g_texture_current;
    static thread_local mg_texture_state_t fallback{};
    fallback.state = g_state;
    return g_state ? &fallback : nullptr;
}

mg_enable_state_t* mg_enable_current() {
    // Standalone: no back-pointer, so no g_state fallback. Returns nullptr in
    // the headless path (no MGContext bound). The enable entry points still
    // read/write the centralized GLState today; the standalone table only
    // becomes authoritative once those access points are migrated.
    return g_enable_current;
}

mg_framebuffer_state_t* mg_framebuffer_current() {
    if (g_framebuffer_current) return g_framebuffer_current;
    static thread_local mg_framebuffer_state_t fallback{};
    fallback.state = g_state;
    return g_state ? &fallback : nullptr;
}

mg_program_state_t* mg_program_current() {
    if (g_program_current) return g_program_current;
    static thread_local mg_program_state_t fallback{};
    fallback.state = g_state;
    return g_state ? &fallback : nullptr;
}

mg_vertexattrib_state_t* mg_vertexattrib_current() {
    if (g_vertexattrib_current) return g_vertexattrib_current;
    static thread_local mg_vertexattrib_state_t fallback{};
    fallback.state = g_state;
    return g_state ? &fallback : nullptr;
}

mg_pixel_state_t* mg_pixel_current() {
    if (g_pixel_current) return g_pixel_current;
    static thread_local mg_pixel_state_t fallback{};
    fallback.state = g_state;
    return g_state ? &fallback : nullptr;
}

gl_state_s* mg_gl_current() {
    if (g_gl_current) return g_gl_current;
    static thread_local gl_state_s fallback{};
    fallback.state = g_state;
    return g_state ? &fallback : nullptr;
}

} // namespace mithril
