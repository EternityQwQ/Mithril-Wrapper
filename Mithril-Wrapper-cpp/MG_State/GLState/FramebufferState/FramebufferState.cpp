// Mithril-Wrapper - MG_State/GLState/FramebufferState/FramebufferState.cpp
//
// Implementation of the FramebufferState domain component. See FramebufferState.h
// for the shared API contract (unified object-table names, SharedPtr ownership,
// single version bump per binding change / EGL injection).
#include "FramebufferState.h"

#include <memory>
#include <utility>

namespace mithril::glstate {

namespace {

// Stable empty SharedPtr returned by GetFramebufferObject /
// GetCurrent{Draw,Read}Framebuffer when a name is absent. Returning a const
// reference to it lets callers hold the result without copying the refcount
// and without risking a dangling reference.
const SharedPtr<FramebufferObject>& NullFramebuffer() {
    static const SharedPtr<FramebufferObject> null;
    return null;
}

} // namespace

FramebufferState::FramebufferState() {
    // Pre-populate the default framebuffer (name 0) so that GetFramebufferObject(0)
    // / GetCurrentDrawFramebuffer() return a real object with the GL default
    // draw/read buffer configuration. Per the GL spec the default framebuffer
    // has a single color draw buffer and reads from the same; in this wrapper
    // the backend maps GL_COLOR_ATTACHMENT0 on FBO 0 to the EGL surface, and
    // installs the actual on-screen VkImageViews via SetEglDefaultFramebuffer().
    auto fbo = std::make_shared<FramebufferObject>(0);
    fbo->drawBuffers[0] = GL_COLOR_ATTACHMENT0;
    fbo->drawBufferCount = 1;
    fbo->readBuffer = GL_COLOR_ATTACHMENT0;
    m_objects[0] = std::move(fbo);
}

void FramebufferState::GenFramebufferNames(uint32_t n, std::vector<uint32_t>& out) {
    out.reserve(out.size() + n);
    for (uint32_t i = 0; i < n; ++i) {
        out.push_back(m_nextName++);
    }
}

const SharedPtr<FramebufferObject>& FramebufferState::GetFramebufferObject(uint32_t index) {
    auto it = m_objects.find(index);
    if (it == m_objects.end()) {
        return NullFramebuffer();
    }
    return it->second;
}

const SharedPtr<FramebufferObject>& FramebufferState::CreateFramebufferObject(uint32_t index) {
    auto it = m_objects.find(index);
    if (it != m_objects.end()) {
        return it->second;
    }
    auto result = m_objects.emplace(index, std::make_shared<FramebufferObject>(index));
    return result.first->second;
}

void FramebufferState::MarkFramebufferForDeletion(uint32_t index) {
    // GL name-layer deletion only: drop the name from the object table. The
    // underlying Vulkan resources are released by the backend disposal queue
    // once in-flight GPU work referencing them completes, so this component
    // frees no backend handle here. A binding (currentDrawFBO / currentReadFBO)
    // still holding this name is reset to the default framebuffer (0).
    //
    // Name 0 (the default framebuffer) is never deleted — it is owned by the
    // EGL surface / context, not the GL name layer.
    if (index == 0) {
        return;
    }
    m_objects.erase(index);
    if (m_currentDrawFBO == index) {
        m_currentDrawFBO = 0;
    }
    if (m_currentReadFBO == index) {
        m_currentReadFBO = 0;
    }
    ++m_version;
}

bool FramebufferState::ValidateFramebufferName(uint32_t index) const {
    return m_objects.count(index) > 0;
}

bool FramebufferState::ValidateFramebufferObject(uint32_t index) const {
    auto it = m_objects.find(index);
    return it != m_objects.end() && it->second != nullptr;
}

uint32_t FramebufferState::GetCurrentDrawFBO() const {
    return m_currentDrawFBO;
}

uint32_t FramebufferState::GetCurrentReadFBO() const {
    return m_currentReadFBO;
}

void FramebufferState::BindFramebuffer(GLenum target, uint32_t index) {
    // GL_FRAMEBUFFER binds both draw and read; GL_DRAW_FRAMEBUFFER only draw;
    // GL_READ_FRAMEBUFFER only read. Binding name 0 selects the default
    // framebuffer (the EGL surface). A non-zero name is materialised on demand
    // (CreateFramebufferObject is idempotent), mirroring BufferState::BindBuffer.
    const bool bindDraw = (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER);
    const bool bindRead = (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER);

    if (index != 0 && (bindDraw || bindRead)) {
        CreateFramebufferObject(index);
    }
    if (bindDraw) {
        m_currentDrawFBO = index;
    }
    if (bindRead) {
        m_currentReadFBO = index;
    }
    ++m_version;
}

const SharedPtr<FramebufferObject>& FramebufferState::GetCurrentDrawFramebuffer() const {
    auto it = m_objects.find(m_currentDrawFBO);
    if (it == m_objects.end()) {
        return NullFramebuffer();
    }
    return it->second;
}

const SharedPtr<FramebufferObject>& FramebufferState::GetCurrentReadFramebuffer() const {
    auto it = m_objects.find(m_currentReadFBO);
    if (it == m_objects.end()) {
        return NullFramebuffer();
    }
    return it->second;
}

void FramebufferState::SetEglDefaultFramebuffer(VkImageView color, VkImageView depth,
                                                VkImage colorImg, VkImage depthImg,
                                                VkFormat colorFmt, VkFormat depthFmt,
                                                int w, int h) {
    eglDefaultColor = color;
    eglDefaultDepth = depth;
    eglDefaultColorImage = colorImg;
    eglDefaultDepthImage = depthImg;
    eglDefaultColorFormat = colorFmt;
    eglDefaultDepthFormat = depthFmt;
    eglDefaultWidth = w;
    eglDefaultHeight = h;
    ++m_version;
}

uint16_t FramebufferState::GetVersion() const {
    return m_version;
}

} // namespace mithril::glstate
