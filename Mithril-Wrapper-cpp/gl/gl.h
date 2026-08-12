// Mithril-Wrapper - gl/gl.h
// Frontend dispatch scaffolding (Task 1 of the MobileGlues-architecture refactor).
//
// This header defines the NATIVE_FUNCTION_HEAD / NATIVE_FUNCTION_END macros used
// to export each glFoo entry point together with a glFooARB alias, mirroring
// MobileGlues gles/loader.h:135-141. The alias is created with
// __attribute__((alias)) so both symbols resolve to the same implementation
// body without duplicating it.
//
// Combined with the -Wl,-Bsymbolic-functions linker flag (added in the root
// CMakeLists.txt), this guarantees that internal TU-to-TU calls to glFoo bind
// to THIS library's implementation instead of going through a preemptible PLT
// slot that a host process's system GL library could win — the rationale
// documented in MobileGlues CMakeLists.txt:142-152. The symbols stay exported
// for dlsym / eglGetProcAddress; only who internal calls bind to changes.
//
// STATUS (Task 1): scaffolding only. The macros are defined here but NOT yet
// applied to any existing entry point — the MG_Impl/ files still use plain
// extern "C". Task 2 moves the entry points under gl/ and wraps each body in
// NATIVE_FUNCTION_HEAD / NATIVE_FUNCTION_END. No gl* functions are declared
// in this header yet.
#ifndef MITHRIL_GL_GL_H_
#define MITHRIL_GL_GL_H_

#include <GL/gl.h>  // GLAPI / GLAPIENTRY (defined via KHR/khrplatform.h)

#ifdef __cplusplus
extern "C" {
#endif

/*
 * NATIVE_FUNCTION_HEAD(type, name, ...)
 *
 *   type : return type of the entry point (void, GLenum, GLuint, ...)
 *   name : bare GL symbol name WITHOUT the gl prefix is NOT how this is used —
 *          pass the full symbol (e.g. glBindBuffer). The macro pastes ARB onto
 *          it to form glBindBufferARB.
 *   ...  : the parameter list (types only, matching the GL prototype).
 *
 * Emits two declarations:
 *   1. An extern "C" glFooARB alias attribute that points at glFoo. The alias
 *      is resolved at link time, so glFooARB shares glFoo's body exactly.
 *   2. The glFoo function definition header (open brace). The caller writes
 *      the body immediately after and closes it with NATIVE_FUNCTION_END.
 *
 * On Apple (Mach-O / ld64) __attribute__((alias)) is not supported the way ELF
 * linkers support it, so only the primary glFoo symbol is exported there —
 * mirroring MobileGlues' __APPLE__ guard in gles/loader.h:135-141. The
 * function body is identical either way; only the ARB alias is suppressed.
 *
 * The non-Apple branch also guards on __GNUC__ / __clang__ for portability,
 * since __attribute__((alias)) is a GCC/Clang extension. The project targets
 * clang, so this branch is always taken on the iOS / Linux build paths.
 *
 * NOTE on GLAPI: Mithril's KHR/khrplatform.h defines GLAPI as `extern`, so
 * `extern "C" GLAPI type ...` would expand to `extern "C" extern type ...`,
 * which GCC/Clang reject ("invalid use of 'extern' in linkage specification").
 * The `extern "C"` prefix already supplies extern storage + C linkage, so
 * GLAPI is intentionally omitted from the macro — only GLAPIENTRY (the calling
 * convention, empty on non-Win32) is kept, matching the rest of Mithril's
 * GL declarations which use `GLAPI type GLAPIENTRY name` inside an
 * `extern "C" {}` block.
 *
 * Usage (mirrors MobileGlues gles/loader.h):
 *
 *     NATIVE_FUNCTION_HEAD(void, glBindBuffer, GLenum target, GLuint buffer)
 *         // ... body: translate GL call into vk_func.bind_buffer(...) ...
 *     NATIVE_FUNCTION_END
 */
#if !defined(__APPLE__) && (defined(__GNUC__) || defined(__clang__))
#define NATIVE_FUNCTION_HEAD(type, name, ...) \
    extern "C" type GLAPIENTRY name##ARB(__VA_ARGS__) __attribute__((alias(#name))); \
    extern "C" type GLAPIENTRY name(__VA_ARGS__) {
#else
#define NATIVE_FUNCTION_HEAD(type, name, ...) \
    extern "C" type GLAPIENTRY name(__VA_ARGS__) {
#endif

/*
 * NATIVE_FUNCTION_END
 *
 * Closes the function body opened by NATIVE_FUNCTION_HEAD. Kept as a separate
 * macro (rather than just a literal '}') so call sites are symmetric and so a
 * future variant can inject post-call work (e.g. error checks) in one place,
 * exactly as MobileGlues' NATIVE_FUNCTION_END / *_NO_RETURN family does.
 */
#define NATIVE_FUNCTION_END }

#ifdef __cplusplus
}
#endif

#endif // MITHRIL_GL_GL_H_
