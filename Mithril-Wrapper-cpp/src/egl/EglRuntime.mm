#import <EGL/egl.h>

#include "egl/EglConfig.h"
#include "egl/EglBridge.h"
#include "frontend/gl/DirectGlContext.h"
#include "frontend/gl/DirectGlApi.h"
#include "gl/Context.h"
#include "metal/MetalDeviceSession.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string_view>
#include <unordered_set>

#if defined(__GNUC__)
#define MITHRIL_EXPORT __attribute__((visibility("default")))
#else
#define MITHRIL_EXPORT
#endif

namespace mithril::egl {

struct Display final {
    std::mutex mutex;
    std::shared_ptr<metal::MetalDeviceSession> session;
    std::size_t initializeCount{};
};

struct Context final {
    Context(std::shared_ptr<gl::ShareGroup> share, const gl::CapabilityManifest& capabilities,
            std::shared_ptr<frontend::gl::DirectGlShareGroup> directShare,
            std::shared_ptr<metal::MetalDeviceSession> session)
        : glContext(std::move(share), capabilities),
          directContext(std::move(directShare), std::move(session)) {}
    gl::Context glContext;
    frontend::gl::DirectGlContext directContext;
};

struct Surface final {
    core::SurfaceHandle handle;
    backend::SurfaceDesc desc;
    bool window{};
    std::mutex frameMutex;
    std::optional<backend::Frame> frame;
};

Display g_display;
std::mutex g_objectsMutex;
std::unordered_set<Context*> g_contexts;
std::unordered_set<Surface*> g_surfaces;
thread_local EGLint g_error = EGL_SUCCESS;
thread_local EGLenum g_api = EGL_OPENGL_API;
thread_local Context* g_context = nullptr;
thread_local Surface* g_draw = nullptr;
thread_local Surface* g_read = nullptr;

template <typename T>
T fail(EGLint error, T value) {
    g_error = error;
    return value;
}

bool validDisplay(EGLDisplay display) { return display == &g_display; }

bool initialized(EGLDisplay display) {
    if (!validDisplay(display)) return false;
    std::lock_guard lock(g_display.mutex);
    return g_display.initializeCount != 0 && g_display.session != nullptr;
}

Context* contextFrom(EGLContext handle) {
    auto* context = static_cast<Context*>(handle);
    std::lock_guard lock(g_objectsMutex);
    return g_contexts.contains(context) ? context : nullptr;
}

Surface* surfaceFrom(EGLSurface handle) {
    auto* surface = static_cast<Surface*>(handle);
    std::lock_guard lock(g_objectsMutex);
    return g_surfaces.contains(surface) ? surface : nullptr;
}

bool parsePbuffer(const EGLint* attributes, backend::SurfaceDesc& desc) {
    desc.width = 1;
    desc.height = 1;
    if (attributes == nullptr) return true;
    for (const EGLint* item = attributes; *item != EGL_NONE; item += 2) {
        if (item[0] == EGL_WIDTH && item[1] > 0) desc.width = static_cast<std::uint32_t>(item[1]);
        else if (item[0] == EGL_HEIGHT && item[1] > 0) desc.height = static_cast<std::uint32_t>(item[1]);
        else if (item[0] != EGL_LARGEST_PBUFFER && item[0] != EGL_TEXTURE_FORMAT &&
                 item[0] != EGL_TEXTURE_TARGET && item[0] != EGL_MIPMAP_TEXTURE) return false;
    }
    return true;
}

EGLSurface createSurface(EGLDisplay display, EGLConfig config, void* nativeWindow,
                         const EGLint* attributes, bool window) {
    if (!initialized(display)) return fail(EGL_NOT_INITIALIZED, EGL_NO_SURFACE);
    if (fromHandle(config) == nullptr) return fail(EGL_BAD_CONFIG, EGL_NO_SURFACE);
    if (window && nativeWindow == nullptr) return fail(EGL_BAD_NATIVE_WINDOW, EGL_NO_SURFACE);
    backend::SurfaceDesc desc;
    desc.nativeWindow = nativeWindow;
    if (window) {
        desc.width = 0;
        desc.height = 0;
    } else if (!parsePbuffer(attributes, desc)) {
        return fail(EGL_BAD_ATTRIBUTE, EGL_NO_SURFACE);
    }
    auto surfaceHandle = g_display.session->createSurface(desc);
    if (!surfaceHandle) return fail(EGL_BAD_ALLOC, EGL_NO_SURFACE);
    auto stored = g_display.session->describeSurface(surfaceHandle.value());
    if (!stored) {
        (void)g_display.session->release(surfaceHandle.value());
        return fail(EGL_BAD_NATIVE_WINDOW, EGL_NO_SURFACE);
    }
    desc.width = stored.value().width;
    desc.height = stored.value().height;
    auto* surface = new (std::nothrow) Surface{surfaceHandle.value(), desc, window};
    if (surface == nullptr) {
        (void)g_display.session->release(surfaceHandle.value());
        return fail(EGL_BAD_ALLOC, EGL_NO_SURFACE);
    }
    std::lock_guard lock(g_objectsMutex);
    g_surfaces.insert(surface);
    return surface;
}

using Proc = void (*)(void);
struct ProcEntry { std::string_view name; Proc address; };

} // namespace mithril::egl

namespace mithril::egl::bridge {

std::shared_ptr<metal::MetalDeviceSession> currentSession() {
    std::lock_guard lock(g_display.mutex);
    return g_display.session;
}

frontend::gl::DirectGlContext* currentGlContext() noexcept {
    return g_context != nullptr ? &g_context->directContext : nullptr;
}

core::ValueResult<backend::Frame> acquireDrawFrame() {
    if (g_draw == nullptr) return core::ValueResult<backend::Frame>::failure(core::Error::make(
        core::ErrorDomain::surface, core::ErrorCode::invalid_state, "no current EGL draw surface"));
    std::lock_guard lock(g_draw->frameMutex);
    if (g_draw->frame) return *g_draw->frame;
    auto session = currentSession();
    if (!session) return core::ValueResult<backend::Frame>::failure(core::Error::make(
        core::ErrorDomain::device, core::ErrorCode::unavailable, "EGL display has no Metal session"));
    auto frame = session->acquire(g_draw->handle);
    if (!frame) return frame;
    g_draw->frame = frame.value();
    return frame.value();
}

core::Result presentDrawFrame() {
    if (g_draw == nullptr) return core::Result::failure(core::Error::make(
        core::ErrorDomain::surface, core::ErrorCode::invalid_state, "no current EGL draw surface"));
    std::optional<backend::Frame> frame;
    {
        std::lock_guard lock(g_draw->frameMutex);
        frame = std::move(g_draw->frame);
        g_draw->frame.reset();
    }
    if (!frame) return {};
    auto session = currentSession();
    return session ? session->present(*frame) : core::Result::failure(core::Error::make(
        core::ErrorDomain::device, core::ErrorCode::unavailable, "EGL display has no Metal session"));
}

} // namespace mithril::egl::bridge

extern "C" {

MITHRIL_EXPORT EGLint eglGetError() {
    const EGLint result = mithril::egl::g_error;
    mithril::egl::g_error = EGL_SUCCESS;
    return result;
}

MITHRIL_EXPORT EGLDisplay eglGetDisplay(EGLNativeDisplayType display) {
    if (display != EGL_DEFAULT_DISPLAY) return mithril::egl::fail(EGL_BAD_PARAMETER, EGL_NO_DISPLAY);
    return &mithril::egl::g_display;
}

MITHRIL_EXPORT EGLDisplay eglGetPlatformDisplay(EGLenum, void* display, const EGLint*) {
    return eglGetDisplay(display);
}

MITHRIL_EXPORT EGLBoolean eglInitialize(EGLDisplay display, EGLint* major, EGLint* minor) {
    using namespace mithril::egl;
    if (!validDisplay(display)) return fail(EGL_BAD_DISPLAY, EGL_FALSE);
    std::lock_guard lock(g_display.mutex);
    if (!g_display.session) {
        auto session = mithril::metal::MetalDeviceSession::create();
        if (!session) return fail(EGL_NOT_INITIALIZED, EGL_FALSE);
        g_display.session = std::move(session.value());
    }
    ++g_display.initializeCount;
    if (major) *major = 1;
    if (minor) *minor = 5;
    return EGL_TRUE;
}

MITHRIL_EXPORT EGLBoolean eglTerminate(EGLDisplay display) {
    using namespace mithril::egl;
    if (!validDisplay(display)) return fail(EGL_BAD_DISPLAY, EGL_FALSE);
    std::lock_guard lock(g_display.mutex);
    if (g_display.initializeCount == 0) return fail(EGL_NOT_INITIALIZED, EGL_FALSE);
    if (--g_display.initializeCount == 0) {
        {
            std::lock_guard objectsLock(g_objectsMutex);
            if (!g_contexts.empty() || !g_surfaces.empty()) {
                ++g_display.initializeCount;
                return fail(EGL_BAD_ACCESS, EGL_FALSE);
            }
        }
        (void)g_display.session->waitIdle(5'000'000'000ULL);
        g_display.session.reset();
    }
    return EGL_TRUE;
}

MITHRIL_EXPORT const char* eglQueryString(EGLDisplay display, EGLint name) {
    if (display != EGL_NO_DISPLAY && !mithril::egl::validDisplay(display)) {
        return mithril::egl::fail(EGL_BAD_DISPLAY, static_cast<const char*>(nullptr));
    }
    switch (name) {
        case EGL_VENDOR: return "Mithril Direct Metal";
        case EGL_VERSION: return "1.5 Mithril";
        case EGL_CLIENT_APIS: return "OpenGL OpenGL_ES";
        case EGL_EXTENSIONS: return "EGL_KHR_create_context EGL_KHR_surfaceless_context EGL_EXT_platform_base";
        default: return mithril::egl::fail(EGL_BAD_PARAMETER, static_cast<const char*>(nullptr));
    }
}

MITHRIL_EXPORT EGLBoolean eglBindAPI(EGLenum api) {
    if (api != EGL_OPENGL_API && api != EGL_OPENGL_ES_API) return mithril::egl::fail(EGL_BAD_PARAMETER, EGL_FALSE);
    mithril::egl::g_api = api;
    return EGL_TRUE;
}

MITHRIL_EXPORT EGLenum eglQueryAPI() { return mithril::egl::g_api; }

MITHRIL_EXPORT EGLBoolean eglGetConfigs(EGLDisplay display, EGLConfig* output,
                                        EGLint capacity, EGLint* count) {
    using namespace mithril::egl;
    if (!initialized(display)) return fail(EGL_NOT_INITIALIZED, EGL_FALSE);
    if (count == nullptr || capacity < 0) return fail(EGL_BAD_PARAMETER, EGL_FALSE);
    *count = static_cast<EGLint>(configs().size());
    if (output != nullptr) {
        const auto copied = std::min<std::size_t>(configs().size(), static_cast<std::size_t>(capacity));
        for (std::size_t index = 0; index < copied; ++index) output[index] = const_cast<Config*>(&configs()[index]);
        *count = static_cast<EGLint>(copied);
    }
    return EGL_TRUE;
}

MITHRIL_EXPORT EGLBoolean eglChooseConfig(EGLDisplay display, const EGLint* attributes,
                                          EGLConfig* output, EGLint capacity, EGLint* count) {
    using namespace mithril::egl;
    if (!initialized(display)) return fail(EGL_NOT_INITIALIZED, EGL_FALSE);
    if (count == nullptr || capacity < 0) return fail(EGL_BAD_PARAMETER, EGL_FALSE);
    EGLint matchesFound = 0;
    for (const auto& config : configs()) {
        if (!matches(config, attributes)) continue;
        if (output != nullptr && matchesFound < capacity) output[matchesFound] = const_cast<Config*>(&config);
        ++matchesFound;
    }
    *count = output != nullptr ? std::min(matchesFound, capacity) : matchesFound;
    return EGL_TRUE;
}

MITHRIL_EXPORT EGLBoolean eglGetConfigAttrib(EGLDisplay display, EGLConfig handle,
                                             EGLint name, EGLint* value) {
    using namespace mithril::egl;
    if (!initialized(display)) return fail(EGL_NOT_INITIALIZED, EGL_FALSE);
    const Config* config = fromHandle(handle);
    if (config == nullptr) return fail(EGL_BAD_CONFIG, EGL_FALSE);
    if (value == nullptr) return fail(EGL_BAD_PARAMETER, EGL_FALSE);
    if (!attribute(*config, name, *value)) return fail(EGL_BAD_ATTRIBUTE, EGL_FALSE);
    return EGL_TRUE;
}

MITHRIL_EXPORT EGLContext eglCreateContext(EGLDisplay display, EGLConfig config,
                                           EGLContext shared, const EGLint* attributes) {
    using namespace mithril::egl;
    if (!initialized(display)) return fail(EGL_NOT_INITIALIZED, EGL_NO_CONTEXT);
    if (fromHandle(config) == nullptr) return fail(EGL_BAD_CONFIG, EGL_NO_CONTEXT);
    int major = 3;
    int minor = 3;
    int profile = EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT;
    for (const EGLint* item = attributes; item != nullptr && *item != EGL_NONE; item += 2) {
        if (item[0] == EGL_CONTEXT_MAJOR_VERSION) major = item[1];
        else if (item[0] == EGL_CONTEXT_MINOR_VERSION) minor = item[1];
        else if (item[0] == EGL_CONTEXT_OPENGL_PROFILE_MASK) profile = item[1];
        else if (item[0] != EGL_CONTEXT_FLAGS_KHR) return fail(EGL_BAD_ATTRIBUTE, EGL_NO_CONTEXT);
    }
    if (major != 3 || minor > 3 || (g_api == EGL_OPENGL_API &&
        (profile & EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT) == 0)) return fail(EGL_BAD_MATCH, EGL_NO_CONTEXT);
    std::shared_ptr<mithril::gl::ShareGroup> shareGroup;
    std::shared_ptr<mithril::frontend::gl::DirectGlShareGroup> directShare;
    if (shared != EGL_NO_CONTEXT) {
        auto* parent = contextFrom(shared);
        if (parent == nullptr) return fail(EGL_BAD_CONTEXT, EGL_NO_CONTEXT);
        shareGroup = parent->glContext.shareGroup();
        directShare = parent->directContext.shareGroup();
    } else {
        shareGroup = std::make_shared<mithril::gl::ShareGroup>();
        directShare = std::make_shared<mithril::frontend::gl::DirectGlShareGroup>(g_display.session);
    }
    auto* context = new (std::nothrow) Context(std::move(shareGroup), g_display.session->capabilities(),
        std::move(directShare), g_display.session);
    if (context == nullptr) return fail(EGL_BAD_ALLOC, EGL_NO_CONTEXT);
    std::lock_guard lock(g_objectsMutex);
    g_contexts.insert(context);
    return context;
}

MITHRIL_EXPORT EGLBoolean eglDestroyContext(EGLDisplay display, EGLContext handle) {
    using namespace mithril::egl;
    if (!initialized(display)) return fail(EGL_NOT_INITIALIZED, EGL_FALSE);
    auto* context = contextFrom(handle);
    if (context == nullptr) return fail(EGL_BAD_CONTEXT, EGL_FALSE);
    if (context == g_context || context->glContext.isCurrent()) return fail(EGL_BAD_ACCESS, EGL_FALSE);
    { std::lock_guard lock(g_objectsMutex); g_contexts.erase(context); }
    delete context;
    return EGL_TRUE;
}

MITHRIL_EXPORT EGLSurface eglCreateWindowSurface(EGLDisplay display, EGLConfig config,
                                                 EGLNativeWindowType window, const EGLint* attributes) {
    return mithril::egl::createSurface(display, config, window, attributes, true);
}

MITHRIL_EXPORT EGLSurface eglCreatePbufferSurface(EGLDisplay display, EGLConfig config,
                                                  const EGLint* attributes) {
    return mithril::egl::createSurface(display, config, nullptr, attributes, false);
}

MITHRIL_EXPORT EGLSurface eglCreatePlatformWindowSurface(EGLDisplay display, EGLConfig config,
                                                         void* window, const EGLAttrib*) {
    return mithril::egl::createSurface(display, config, window, nullptr, true);
}

MITHRIL_EXPORT EGLBoolean eglDestroySurface(EGLDisplay display, EGLSurface handle) {
    using namespace mithril::egl;
    if (!initialized(display)) return fail(EGL_NOT_INITIALIZED, EGL_FALSE);
    auto* surface = surfaceFrom(handle);
    if (surface == nullptr) return fail(EGL_BAD_SURFACE, EGL_FALSE);
    if (surface == g_draw || surface == g_read) return fail(EGL_BAD_ACCESS, EGL_FALSE);
    auto result = g_display.session->release(surface->handle);
    if (!result) return fail(EGL_BAD_SURFACE, EGL_FALSE);
    { std::lock_guard lock(g_objectsMutex); g_surfaces.erase(surface); }
    delete surface;
    return EGL_TRUE;
}

MITHRIL_EXPORT EGLBoolean eglMakeCurrent(EGLDisplay display, EGLSurface drawHandle,
                                         EGLSurface readHandle, EGLContext contextHandle) {
    using namespace mithril::egl;
    if (!initialized(display)) return fail(EGL_NOT_INITIALIZED, EGL_FALSE);
    if (contextHandle == EGL_NO_CONTEXT && drawHandle == EGL_NO_SURFACE && readHandle == EGL_NO_SURFACE) {
        if (g_context && !g_context->glContext.releaseCurrent()) return fail(EGL_BAD_ACCESS, EGL_FALSE);
        g_context = nullptr; g_draw = nullptr; g_read = nullptr;
        return EGL_TRUE;
    }
    auto* context = contextFrom(contextHandle);
    auto* draw = drawHandle == EGL_NO_SURFACE ? nullptr : surfaceFrom(drawHandle);
    auto* read = readHandle == EGL_NO_SURFACE ? nullptr : surfaceFrom(readHandle);
    if (context == nullptr) return fail(EGL_BAD_CONTEXT, EGL_FALSE);
    if ((drawHandle != EGL_NO_SURFACE && draw == nullptr) || (readHandle != EGL_NO_SURFACE && read == nullptr)) {
        return fail(EGL_BAD_SURFACE, EGL_FALSE);
    }
    if (!context->glContext.makeCurrent()) return fail(EGL_BAD_ACCESS, EGL_FALSE);
    g_context = context; g_draw = draw; g_read = read;
    return EGL_TRUE;
}

MITHRIL_EXPORT EGLContext eglGetCurrentContext() { return mithril::egl::g_context; }
MITHRIL_EXPORT EGLDisplay eglGetCurrentDisplay() {
    return mithril::egl::g_context ? static_cast<EGLDisplay>(&mithril::egl::g_display) : EGL_NO_DISPLAY;
}
MITHRIL_EXPORT EGLSurface eglGetCurrentSurface(EGLenum target) {
    if (target == EGL_DRAW) return mithril::egl::g_draw;
    if (target == EGL_READ) return mithril::egl::g_read;
    return mithril::egl::fail(EGL_BAD_PARAMETER, EGL_NO_SURFACE);
}

MITHRIL_EXPORT EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface handle) {
    using namespace mithril::egl;
    if (!initialized(display)) return fail(EGL_NOT_INITIALIZED, EGL_FALSE);
    auto* surface = surfaceFrom(handle);
    if (surface == nullptr) return fail(EGL_BAD_SURFACE, EGL_FALSE);
    if (!surface->window) return EGL_TRUE;
    if (surface != g_draw) return fail(EGL_BAD_SURFACE, EGL_FALSE);
    auto frame = bridge::acquireDrawFrame();
    if (!frame) return fail(EGL_BAD_SURFACE, EGL_FALSE);
    return bridge::presentDrawFrame() ? EGL_TRUE : fail(EGL_BAD_SURFACE, EGL_FALSE);
}

MITHRIL_EXPORT EGLBoolean eglSwapInterval(EGLDisplay display, EGLint interval) {
    if (!mithril::egl::initialized(display)) return mithril::egl::fail(EGL_NOT_INITIALIZED, EGL_FALSE);
    return interval >= 0 && interval <= 1 ? EGL_TRUE : mithril::egl::fail(EGL_BAD_PARAMETER, EGL_FALSE);
}

MITHRIL_EXPORT EGLBoolean eglQuerySurface(EGLDisplay display, EGLSurface handle,
                                          EGLint name, EGLint* value) {
    using namespace mithril::egl;
    if (!initialized(display)) return fail(EGL_NOT_INITIALIZED, EGL_FALSE);
    auto* surface = surfaceFrom(handle);
    if (surface == nullptr) return fail(EGL_BAD_SURFACE, EGL_FALSE);
    if (value == nullptr) return fail(EGL_BAD_PARAMETER, EGL_FALSE);
    if (name == EGL_WIDTH) *value = static_cast<EGLint>(surface->desc.width);
    else if (name == EGL_HEIGHT) *value = static_cast<EGLint>(surface->desc.height);
    else if (name == EGL_CONFIG_ID) *value = 1;
    else if (name == EGL_RENDER_BUFFER) *value = EGL_BACK_BUFFER;
    else if (name == EGL_SWAP_BEHAVIOR) *value = EGL_BUFFER_DESTROYED;
    else return fail(EGL_BAD_ATTRIBUTE, EGL_FALSE);
    return EGL_TRUE;
}

MITHRIL_EXPORT EGLBoolean eglQueryContext(EGLDisplay display, EGLContext handle,
                                          EGLint name, EGLint* value) {
    using namespace mithril::egl;
    if (!initialized(display)) return fail(EGL_NOT_INITIALIZED, EGL_FALSE);
    if (contextFrom(handle) == nullptr) return fail(EGL_BAD_CONTEXT, EGL_FALSE);
    if (value == nullptr) return fail(EGL_BAD_PARAMETER, EGL_FALSE);
    if (name == EGL_CONTEXT_CLIENT_TYPE) *value = static_cast<EGLint>(g_api);
    else if (name == EGL_CONTEXT_MAJOR_VERSION) *value = 3;
    else if (name == EGL_CONTEXT_MINOR_VERSION) *value = 3;
    else if (name == EGL_CONTEXT_OPENGL_PROFILE_MASK) *value = EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT;
    else return fail(EGL_BAD_ATTRIBUTE, EGL_FALSE);
    return EGL_TRUE;
}

MITHRIL_EXPORT EGLBoolean eglReleaseThread() {
    if (mithril::egl::g_context) (void)mithril::egl::g_context->glContext.releaseCurrent();
    mithril::egl::g_context = nullptr; mithril::egl::g_draw = nullptr; mithril::egl::g_read = nullptr;
    mithril::egl::g_error = EGL_SUCCESS; mithril::egl::g_api = EGL_OPENGL_API;
    return EGL_TRUE;
}

MITHRIL_EXPORT EGLBoolean eglWaitClient() { return EGL_TRUE; }
MITHRIL_EXPORT EGLBoolean eglWaitGL() { return EGL_TRUE; }
MITHRIL_EXPORT EGLBoolean eglWaitNative(EGLint) { return EGL_TRUE; }

MITHRIL_EXPORT void (*eglGetProcAddress(const char* name))(void) {
    using namespace mithril::egl;
    if (name == nullptr) return nullptr;
#define EGL_PROC(symbol) ProcEntry{#symbol, reinterpret_cast<Proc>(&symbol)}
    static const ProcEntry entries[] = {
        EGL_PROC(eglGetError), EGL_PROC(eglGetDisplay), EGL_PROC(eglGetPlatformDisplay),
        EGL_PROC(eglInitialize), EGL_PROC(eglTerminate), EGL_PROC(eglQueryString),
        EGL_PROC(eglBindAPI), EGL_PROC(eglQueryAPI), EGL_PROC(eglGetConfigs),
        EGL_PROC(eglChooseConfig), EGL_PROC(eglGetConfigAttrib), EGL_PROC(eglCreateContext),
        EGL_PROC(eglDestroyContext), EGL_PROC(eglCreateWindowSurface), EGL_PROC(eglCreatePbufferSurface),
        EGL_PROC(eglCreatePlatformWindowSurface), EGL_PROC(eglDestroySurface), EGL_PROC(eglMakeCurrent),
        EGL_PROC(eglGetCurrentContext), EGL_PROC(eglGetCurrentDisplay), EGL_PROC(eglGetCurrentSurface),
        EGL_PROC(eglSwapBuffers), EGL_PROC(eglSwapInterval), EGL_PROC(eglQuerySurface),
        EGL_PROC(eglQueryContext), EGL_PROC(eglReleaseThread), EGL_PROC(eglWaitClient),
        EGL_PROC(eglWaitGL), EGL_PROC(eglWaitNative), EGL_PROC(eglGetProcAddress),
    };
#undef EGL_PROC
    for (const auto& entry : entries) if (entry.name == name) return entry.address;
    return reinterpret_cast<Proc>(mithril::frontend::gl::lookupDirectGlProc(name));
}

} // extern "C"
