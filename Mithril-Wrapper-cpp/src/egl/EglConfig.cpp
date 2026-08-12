#include "egl/EglConfig.h"

namespace mithril::egl {

const std::array<Config, 4>& configs() noexcept {
    static const std::array values{
        Config{1, 8, 8, 8, 8, 24, 8},
        Config{2, 8, 8, 8, 8, 24, 0},
        Config{3, 8, 8, 8, 8, 0, 8},
        Config{4, 8, 8, 8, 8, 0, 0},
    };
    return values;
}

bool attribute(const Config& config, EGLint name, EGLint& value) noexcept {
    switch (name) {
        case EGL_CONFIG_ID: value = config.id; break;
        case EGL_RED_SIZE: value = config.red; break;
        case EGL_GREEN_SIZE: value = config.green; break;
        case EGL_BLUE_SIZE: value = config.blue; break;
        case EGL_ALPHA_SIZE: value = config.alpha; break;
        case EGL_DEPTH_SIZE: value = config.depth; break;
        case EGL_STENCIL_SIZE: value = config.stencil; break;
        case EGL_BUFFER_SIZE: value = config.red + config.green + config.blue + config.alpha; break;
        case EGL_SURFACE_TYPE: value = EGL_WINDOW_BIT | EGL_PBUFFER_BIT; break;
        case EGL_RENDERABLE_TYPE:
        case EGL_CONFORMANT: value = EGL_OPENGL_BIT | EGL_OPENGL_ES3_BIT; break;
        case EGL_COLOR_BUFFER_TYPE: value = EGL_RGB_BUFFER; break;
        case EGL_CONFIG_CAVEAT:
        case EGL_TRANSPARENT_TYPE: value = EGL_NONE; break;
        case EGL_LEVEL:
        case EGL_SAMPLES:
        case EGL_SAMPLE_BUFFERS:
        case EGL_LUMINANCE_SIZE:
        case EGL_ALPHA_MASK_SIZE:
        case EGL_NATIVE_VISUAL_ID:
        case EGL_NATIVE_RENDERABLE: value = 0; break;
        case EGL_MIN_SWAP_INTERVAL: value = 0; break;
        case EGL_MAX_SWAP_INTERVAL: value = 1; break;
        case EGL_MAX_PBUFFER_WIDTH:
        case EGL_MAX_PBUFFER_HEIGHT: value = 8192; break;
        case EGL_MAX_PBUFFER_PIXELS: value = 8192 * 8192; break;
        case EGL_BIND_TO_TEXTURE_RGB:
        case EGL_BIND_TO_TEXTURE_RGBA: value = EGL_FALSE; break;
        default: return false;
    }
    return true;
}

bool matches(const Config& config, const EGLint* attributes) noexcept {
    if (attributes == nullptr) return true;
    for (const EGLint* item = attributes; *item != EGL_NONE; item += 2) {
        if (item[1] == EGL_DONT_CARE) continue;
        EGLint actual = 0;
        if (!attribute(config, item[0], actual)) return false;
        switch (item[0]) {
            case EGL_SURFACE_TYPE:
            case EGL_RENDERABLE_TYPE:
            case EGL_CONFORMANT:
                if ((actual & item[1]) != item[1]) return false;
                break;
            case EGL_CONFIG_ID:
            case EGL_COLOR_BUFFER_TYPE:
            case EGL_TRANSPARENT_TYPE:
                if (actual != item[1]) return false;
                break;
            default:
                if (actual < item[1]) return false;
                break;
        }
    }
    return true;
}

const Config* fromHandle(EGLConfig handle) noexcept {
    const auto* candidate = static_cast<const Config*>(handle);
    for (const auto& config : configs()) {
        if (&config == candidate) return candidate;
    }
    return nullptr;
}

} // namespace mithril::egl
