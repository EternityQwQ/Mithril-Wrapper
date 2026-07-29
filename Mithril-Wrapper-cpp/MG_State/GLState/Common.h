// Mithril-Wrapper - MG_State/GLState/Common.h
//
// Common types, constants and small helpers shared by the new modular
// OpenGL state machine. This header is intentionally GL/Vulkan-agnostic:
// it depends only on the C++ standard library, so every per-component state
// header (RenderState, TextureState, ...) can include it without pulling in
// GL/gl.h or vulkan.h.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace mithril::glstate {

// ---- Limits (migrated from the flat MG_State/State.h, same values) ----
inline constexpr int kMaxVertexAttribs = 16;
inline constexpr int kMaxColorAttachments = 8;
// The render layer historically used MAX_DRAW_BUFFERS; keep both names so the
// old call sites and the new state machine agree on the same bound.
inline constexpr int kMaxDrawBuffers = 8;
inline constexpr int kMaxTextureUnits = 32;

// Shared pointer alias used across the state-machine components.
template <typename T>
using SharedPtr = std::shared_ptr<T>;

// Common size type for indexed binding ranges.
using SizeType = std::size_t;

// ---- BindingSlot ----
// A single bound object reference. `touched` is set whenever the binding
// changes (bind/unbind) and is consumed by the backend to enumerate the set
// of bindings that changed during the current frame, so it can rebuild only
// the affected descriptor sets / pipeline state instead of rehashing every
// binding on each draw. ResetTouched() is called once the backend has flushed
// the per-frame diff.
template <typename T>
struct BindingSlot {
    SharedPtr<T> object;
    bool touched = false;

    bool IsBound() const { return object != nullptr; }

    void Bind(const SharedPtr<T>& o) {
        object = o;
        touched = true;
    }

    void Unbind() {
        object.reset();
        touched = true;
    }

    void ResetTouched() { touched = false; }
};

// ---- BindingSlotRange1D ----
// A fixed-size array of BindingSlot<T> (e.g. the per-texture-unit bindings,
// the per-color-attachment framebuffer bindings). The `touched` bits let the
// backend cheaply walk only the dirty indices via GetTouchedCount().
template <typename T, SizeType N>
struct BindingSlotRange1D {
    static constexpr SizeType Count = N;

    std::array<BindingSlot<T>, N> slots{};

    BindingSlot<T>& Get(SizeType index) { return slots[index]; }
    const BindingSlot<T>& Get(SizeType index) const { return slots[index]; }

    void Bind(SizeType index, const SharedPtr<T>& o) { slots[index].Bind(o); }

    void Unbind(SizeType index) { slots[index].Unbind(); }

    bool IsBound(SizeType index) const { return slots[index].IsBound(); }

    void ResetTouched() {
        for (SizeType i = 0; i < N; ++i) {
            slots[i].ResetTouched();
        }
    }

    SizeType GetTouchedCount() const {
        SizeType count = 0;
        for (SizeType i = 0; i < N; ++i) {
            if (slots[i].touched) {
                ++count;
            }
        }
        return count;
    }
};

// ---- BoolVec4 ----
// Convenience 4-component boolean vector, used by RenderState for the color
// mask (GL_COLOR_WRITEMASK) and similar per-channel toggles.
struct BoolVec4 {
    bool v[4] = {true, true, true, true};

    bool AllTrue() const {
        return v[0] && v[1] && v[2] && v[3];
    }

    bool AnyTrue() const {
        return v[0] || v[1] || v[2] || v[3];
    }

    bool operator==(const BoolVec4& other) const {
        return v[0] == other.v[0] && v[1] == other.v[1] &&
               v[2] == other.v[2] && v[3] == other.v[3];
    }
};

} // namespace mithril::glstate
