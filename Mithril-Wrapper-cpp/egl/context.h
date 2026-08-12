// Mithril-Wrapper - egl/context.h
//
// MGContext: the per-EGLContext tracking model (mirrors MobileGlues'
// egl/context.h). This is an ADDITIVE layer that sits alongside the existing
// EglContext (EglInternal.h) — it does NOT replace it. The EglContext still
// owns the per-context mithril::GLState and the share-group refcount that
// egl.cpp has always managed; MGContext only records metadata (id, display,
// granted version, share-group pointer) and gives the rest of the layer a
// stable identity (a monotonic id, never the EGLContext pointer, which is a
// driver heap address that gets reused after free).
//
// Why a second record at all? EGL allows destroying a context that is still
// current on another thread, and a driver is free to hand out the same
// EGLContext address twice. The monotonic id lets subsystems key per-context
// state tables safely, and the thread-local g_current_ctx gives them a cheap
// "which context am I on" lookup that doesn't have to call back into EGL.
//
// Per-subsystem bind/forget hooks are declared here so each subsystem
// (buffer / texture / framebuffer / ...) can keep its own per-context state
// table and swap a thread_local current pointer on context switches. They are
// no-op stubs for now (the subsystem state still lives in the centralized
// GLState); they will be filled in as subsystems are distributed.
//
// Per-subsystem state accessors (mg_gl_state / mg_buffer_current / ...) return
// the current context's GLState. Initially they all delegate to
// mithril::g_state (the centralized GLState that eglMakeCurrent installs); as
// subsystems are distributed they will return typed subsystem state.
#ifndef MITHRIL_EGL_CONTEXT_H
#define MITHRIL_EGL_CONTEXT_H

#include <cstdint>
#include <memory>
#include <atomic>

#include <EGL/egl.h>
#include "../MG_State/State.h"   // mithril::GLState

// Per-subsystem state struct definitions (Task 3.1). Each subsystem owns a
// state struct in gl/<subsystem>_state.h; the hooks below + the accessors
// further down give each its own per-context table + thread_local current
// pointer. mg_enable_state_t is standalone (own data); the rest are subviews
// holding a mithril::GLState* back-pointer until their data migrates out of
// the centralized GLState.
#include "../gl/enable_state.h"
#include "../gl/buffer_state.h"
#include "../gl/texture_state.h"
#include "../gl/framebuffer_state.h"
#include "../gl/pixel_state.h"
#include "../gl/mg_state.h"
#include "../gl/program_state.h"
#include "../gl/shader_state.h"
#include "../gl/vertexattrib_state.h"
#include "../gl/sync_state.h"
#include "../gl/query_state.h"
#include "../gl/transformfeedback_state.h"

// ---------------------------------------------------------------------------
// Share group record. Objects GL shares across a share group (buffers,
// textures, renderbuffers, samplers, shaders, programs, syncs) belong here.
// They are still process-global today (living in the centralized GLState);
// moving them is the last step of the distributed-state work.
// ---------------------------------------------------------------------------
struct MGShareGroup {
    unsigned long long id;
    explicit MGShareGroup(unsigned long long gid) : id(gid) {}
};

// ---------------------------------------------------------------------------
// MGContext: per-EGLContext tracking record.
//
// `state` is NOT owned by MGContext — it is owned and freed by egl.cpp's
// existing EglContext refcount logic (state_destroy + delete in
// eglDestroyContext). MGContext only holds a non-owning pointer so subsystems
// can reach the GLState through the current context if they need to.
// ---------------------------------------------------------------------------
struct MGContext {
    unsigned long long id;            // monotonic, never reused (NOT the EGLContext handle)
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext handle = EGL_NO_CONTEXT;  // the EGLContext (EglContext* cast)
    EGLenum client_type = EGL_OPENGL_API; // EGL_OPENGL_API / EGL_OPENGL_ES_API
    EGLint granted_major = 0;         // what the app was told it got
    EGLint granted_minor = 0;
    EGLint profile_mask = 0;
    EGLint context_flags = 0;
    std::shared_ptr<MGShareGroup> share_group;
    mithril::GLState* state = nullptr; // per-context GL state (NOT owned by MGContext)
    EGLSurface draw = EGL_NO_SURFACE;
    EGLSurface read = EGL_NO_SURFACE;
    std::atomic<int> current_count{0}; // how many threads have this current
    bool destroy_pending{false};       // eglDestroyContext called while still current
};

// The context current on THIS thread, or nullptr. Thread-local because EGL
// scopes current-context per thread. Readers are safe while the thread keeps
// the context current: the thread holds a shared_ptr ref alongside this
// pointer (g_current_ref in context.cpp), so the MGContext record stays alive
// even if another thread erases it from the map.
extern thread_local MGContext* g_current_ctx;

// Per-subsystem bind/forget hooks. Each subsystem keeps its own per-context
// state table keyed by MGContext::id; bind_context swaps its thread_local
// current pointer, forget_context erases the entry. buffer/texture also take
// the share-group id (their object tables are share-group-scoped); the rest
// are per-context only. sync/query/transformfeedback are no-op stubs for now
// (their tables still live in GLState); the others install a subview entry
// whose GLState* back-pointer points at the current context's GLState, so the
// distributed accessor API is live without disturbing existing g_state->...
// access points.
void mg_buffer_bind_context(unsigned long long ctx_id, unsigned long long group_id);
void mg_texture_bind_context(unsigned long long ctx_id, unsigned long long group_id);
void mg_framebuffer_bind_context(unsigned long long ctx_id);
void mg_enable_bind_context(unsigned long long ctx_id);
void mg_pixel_bind_context(unsigned long long ctx_id);
void mg_gl_bind_context(unsigned long long ctx_id);
void mg_program_bind_context(unsigned long long ctx_id);
void mg_shader_bind_context(unsigned long long ctx_id);
void mg_vertexattrib_bind_context(unsigned long long ctx_id);
void mg_sync_bind_context(unsigned long long ctx_id);
void mg_query_bind_context(unsigned long long ctx_id);
void mg_transformfeedback_bind_context(unsigned long long ctx_id);

void mg_buffer_forget_context(unsigned long long ctx_id);
void mg_texture_forget_context(unsigned long long ctx_id);
void mg_framebuffer_forget_context(unsigned long long ctx_id);
void mg_enable_forget_context(unsigned long long ctx_id);
void mg_pixel_forget_context(unsigned long long ctx_id);
void mg_gl_forget_context(unsigned long long ctx_id);
void mg_program_forget_context(unsigned long long ctx_id);
void mg_shader_forget_context(unsigned long long ctx_id);
void mg_vertexattrib_forget_context(unsigned long long ctx_id);
void mg_sync_forget_context(unsigned long long ctx_id);
void mg_query_forget_context(unsigned long long ctx_id);
void mg_transformfeedback_forget_context(unsigned long long ctx_id);

// Called from the EGL wrappers (eglCreateContext / eglMakeCurrent /
// eglDestroyContext). These manipulate the MGContext tracking map only; they
// do NOT touch the EglContext or its GLState (egl.cpp keeps owning those).
MGContext* mg_context_create(EGLDisplay dpy, EGLContext handle, EGLContext share_handle,
                             EGLenum client_type, EGLint major, EGLint minor,
                             EGLint profile_mask, EGLint context_flags,
                             mithril::GLState* state);
void mg_context_make_current(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext handle);
void mg_context_destroy(EGLContext handle);
MGContext* mg_context_find(EGLContext handle);

// Per-display eglInitialize accounting (probe/app dual-holder).
//
// EGL does not reference-count initialisation per caller: whoever calls
// eglTerminate marks EVERY resource on the display for destruction. Two
// holders (probe / app), not a count, because eglInitialize is idempotent:
// an app may call it any number of times and is only obliged to call
// eglTerminate once. Counting calls meant two inits + one terminate left the
// display up for good.
void mg_display_initialised(EGLDisplay dpy, bool probe);
bool mg_display_release(EGLDisplay dpy, bool probe); // true when this was the last holder

// Per-subsystem state accessors. Each returns the current context's typed
// subsystem state pointer (the thread_local current pointer swapped by the
// matching mg_<subsystem>_bind_context hook). mg_gl_state() keeps returning
// the whole mithril::GLState* for backward compatibility.
//
// Subview fallback: for the subview subsystems (buffer/texture/framebuffer/
// pixel/gl/program/shader/vertexattrib), if no MGContext is bound on this
// thread (e.g. headless unit tests that set g_state directly without EGL),
// the accessor returns a thread_local fallback entry whose GLState* back-pointer
// is the centralized g_state — so future access-point migration works in the
// headless path too. mg_enable_current() returns nullptr in that case, because
// enable state is standalone (no back-pointer) and cannot be cheaply projected
// from GLState without the full capability mapping.
namespace mithril {

struct GLState;
GLState*              mg_gl_state();            // whole GLState (backward compat)
mg_buffer_state_t*    mg_buffer_current();      // buffer subsystem state (subview)
mg_texture_state_t*   mg_texture_current();     // texture subsystem state (subview)
mg_enable_state_t*    mg_enable_current();      // enable/capability state (standalone)
mg_framebuffer_state_t* mg_framebuffer_current();// framebuffer subsystem state (subview)
mg_program_state_t*   mg_program_current();     // program/shader state (subview)
mg_vertexattrib_state_t* mg_vertexattrib_current();// VAO / vertex attrib state (subview)
mg_pixel_state_t*     mg_pixel_current();       // pixel store state (subview)
gl_state_s*           mg_gl_current();          // gl core scalar state (subview)

} // namespace mithril

#endif // MITHRIL_EGL_CONTEXT_H
