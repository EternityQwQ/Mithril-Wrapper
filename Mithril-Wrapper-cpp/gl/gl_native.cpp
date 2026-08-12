// Mithril-Wrapper - gl/gl_native.cpp
// 1:1 GL→backend passthrough entry points for the MobileGlues-architecture
// refactor.
//
// This translation unit will host the GL entry points whose Core-Profile
// semantics are a thin forward to a single backend function (e.g. glBindBuffer
// -> backend_bind_buffer, glDeleteBuffers -> backend_delete_buffers). Each
// entry point is wrapped in NATIVE_FUNCTION_HEAD / NATIVE_FUNCTION_END (from
// gl.h) so it exports both the glFoo symbol and the glFooARB alias, mirroring
// MobileGlues' gl/gl_native.cpp.
//
// STATUS (Task 2a): scaffolding only. The real entry-point bodies are still
// implemented in their original MG_Impl translation units (now relocated under
// gl/) using plain extern "C". A later pass will migrate the 1:1 passthroughs
// here and wrap each in NATIVE_FUNCTION_HEAD, dispatching through the vk_func_t
// function-pointer table (MG_Backend/backend_func.h) instead of calling
// backend_* directly. No functions are defined here yet.
#include "gl.h"

#include "../MG_Backend/backend_func.h"  // vk_func_t (populated in backend_init)

// Placeholder for the 1:1 passthrough entry points. Intentionally empty until
// the migration pass moves real bodies here; kept as a compilable TU so the
// CMake source list (which now references this file) configures cleanly.
namespace mithril::gl_native {
// intentionally empty
}  // namespace mithril::gl_native
