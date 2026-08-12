// Mithril-Wrapper - MG_Impl/Getter_gpu.mm
// Builds the GL_RENDERER string from the live Vulkan physical device so
// Minecraft's F3 screen and crash reports show real GPU info instead of a
// static placeholder.
//
// This is the Vulkan/MoltenVK rewrite of the former gl/getter_gpu.mm. The
// Metal queries (MTLDevice / supportsFamily / recommendedMaxWorkingSetSize)
// are gone; we instead read the VkPhysicalDeviceProperties.deviceName and
// VkPhysicalDeviceMemoryProperties heap sizes through the backend_* C API
// declared in MG_Backend/Backend.h. MoltenVK exposes the Apple GPU name
// ("Apple A14 GPU", "Apple A17 Pro GPU", ...) via deviceName, so the F3
// string still reports the real SoC.
//
// Provides (consumed by Getter.cpp):
//   mithril_get_gpu_renderer_string() — full F3 GL_RENDERER string
//   mithril_get_vulkan_device_name()  — raw VkPhysicalDeviceProperties.deviceName
//   mithril_get_vulkan_api_string()   — "Vulkan 1.2 (MoltenVK)"
//   mithril_get_vram_bytes()          — sum of device-local heap sizes
//   mithril_get_settings_dump()       — multi-line config dump for F3 screen
#include <Foundation/Foundation.h>

#include "includes.h"
#include <string>
#include <sstream>
#include <cstdio>

// The build's git commit id (CMake compile-definition). Guarded so standalone
// / verify compiles that omit the -D still build.
#ifndef MITHRIL_COMMIT_ID
#define MITHRIL_COMMIT_ID "unknown"
#endif

/*
 * Note: this translation unit no longer touches Metal directly — the only
 * reason it stays .mm is that the rest of the Apple build (egl.mm) is .mm and
 * the project links Foundation anyway. Compiling as OBJCXX lets us use
 * NSString / NSLog for the rare diagnostic without an #ifdef dance.
 */

// Map MoltenVK's reported deviceName to a friendlier SoC label for the F3
// string. MoltenVK reports e.g. "Apple A14 GPU" verbatim, so this is mostly
// an identity map; the prefix-stripping is purely cosmetic.
static std::string friendly_gpu_name(const char* vk_name) {
    if (!vk_name || !*vk_name) return "Apple GPU";
    return std::string(vk_name);
}

extern "C" const char* mithril_get_vulkan_device_name(void) {
    static std::string name;
    if (!name.empty()) return name.c_str();
    const char* raw = backend_physical_device_name();
    name = friendly_gpu_name(raw);
    return name.c_str();
}

extern "C" const char* mithril_get_vulkan_api_string(void) {
    return "Vulkan 1.2 (MoltenVK)";
}

extern "C" uint64_t mithril_get_vram_bytes(void) {
    return backend_vram_bytes();
}

extern "C" const char* mithril_get_gpu_renderer_string(void) {
    static std::string cached;
    if (!cached.empty()) return cached.c_str();

    if (!backend_available()) {
        cached = "Mithril-Wrapper (Mithril-Wrapper Core) (Vulkan backend, no device)";
        return cached.c_str();
    }

    std::string gpuName = friendly_gpu_name(backend_physical_device_name());
    std::string api     = mithril_get_vulkan_api_string();

    // MobileGL-style three-part GL_RENDERER:
    //   {RendererName} ({CoreName}) ({backendApiVersionString})
    // (see MobileGL GL_Getter.cpp case GL_RENDERER, and DirectVulkan
    // FormatBackendAPIVersionString = "{device}, Vulkan {ver}, Driver {driver}").
    // The leading RendererName is the friendly GPU label; the parenthesised
    // backend token carries the full device + API stack for the F3 screen.
    cached = gpuName + " (Mithril-Wrapper Core) (" + gpuName + ", " + api + ")";
    return cached.c_str();
}

/*
 * Multi-line config dump displayed on Minecraft's F3 debug screen when a mod
 * queries glGetString(MITHRIL_SETTINGS) (= glGetString(0x0402)).
 * Mirrors MobileGlues' dump_settings_string() output, adapted for Vulkan.
 */
extern "C" const char* mithril_get_settings_dump(void) {
    static std::string dump;
    if (!dump.empty()) return dump.c_str();

    std::ostringstream ss;

    ss << "Mithril-Wrapper 1.0 (OpenGL 3.3 -> Vulkan 1.2 / MoltenVK)\n";
    ss << "  Backend: Vulkan 1.2 (MoltenVK static link)\n";
    ss << "  Build: GIT@" MITHRIL_COMMIT_ID "\n";

    if (backend_available()) {
        ss << "  Renderer: " << mithril_get_gpu_renderer_string() << "\n";
        ss << "  GPU: " << backend_physical_device_name() << "\n";
        ss << "  API: " << mithril_get_vulkan_api_string() << "\n";
        uint64_t vram = backend_vram_bytes();
        if (vram > 0) {
            ss << "  VRAM: " << (vram / (1024ULL * 1024ULL))
               << " MB (device-local heaps, unified memory)\n";
        }
    } else {
        ss << "  GPU: (no Vulkan device)\n";
    }

    ss << "  Shader pipeline: GLSL -> SPIR-V (glslang) -> [MoltenVK SPIR-V->MSL]\n";
    ss << "  Depth/stencil: VK_FORMAT_D32_SFLOAT_S8_UINT\n";
    ss << "  Color format: VK_FORMAT_B8G8R8A8_UNORM (CAMetalLayer)\n";
    ss << "  Surface: VK_EXT_metal_surface (vkCreateMetalSurfaceEXT)\n";
    ss << "  Portability: VK_KHR_portability_enumeration + VK_KHR_portability_subset\n";
    ss << "  EGL: 1.5 (Vulkan-backed)\n";
    ss << "  GL version: 3.3 Core Profile\n";
    ss << "  GLSL version: 3.30\n";

    dump = ss.str();
    return dump.c_str();
}
