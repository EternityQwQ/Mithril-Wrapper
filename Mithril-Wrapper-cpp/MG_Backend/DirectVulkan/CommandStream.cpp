// Mithril-Wrapper - MG_Backend/DirectVulkan/CommandStream.cpp
// Render-pass orchestration via VK_KHR_dynamic_rendering (vkCmdBeginRendering)
// + encoder dynamic-state setters + draw recording + per-frame submit.
#include "CommandStream.h"
#include "Device.h"
#include "Swapchain.h"
#include "../Backend.h"
#include "../../MG_Impl/Log.h"
#include "../../MG_State/State.h"  // g_state (for scissorTest in clear_attachments)

#include <cstring>
#include <vector>

namespace mithril {
namespace vk {

namespace {

// Encoder state carried between begin_render_pass() and the draw calls.
struct EncoderState {
    bool passActive = false;
    VkPipeline boundPipeline = VK_NULL_HANDLE;

    // Pending clear values (applied to the load op of the next pass).
    float clearColor[4] = {0, 0, 0, 0};
    double clearDepth = 1.0;
    GLint clearStencil = 0;
    bool loadClear = false;   // true = CLEAR (glClear), false = LOAD (draw pass)

    // Color/depth attachment views for the active pass.
    VkImageView colorViews[8] = {};
    int colorCount = 0;
    VkImageView depthView = VK_NULL_HANDLE;
    int width = 0;
    int height = 0;

    // The swapchain whose currently-acquired image backs framebuffer 0.
    // nullptr when no EGLSurface is current (headless) or the active FBO is
    // a user-created framebuffer object. Set by EGL after each acquire; read
    // by begin_render_pass() / commit_frame() to record layout barriers and
    // signal the per-image renderFinished semaphore.
    Swapchain* activeSwapchain = nullptr;

    // True once any command has been recorded into the current command buffer
    // since the last commit_frame() / vkBeginCommandBuffer. Used by
    // commit_frame() to skip empty submits (eglWaitClient + eglSwapBuffers
    // both call backend_commit; the second call would otherwise submit an
    // empty command buffer, which is wasteful and — under resize/destruction
    // races — can submit against a destroyed swapchain's semaphore, triggering
    // MoltenVK / IOSurface UAF crashes).
    bool hasCommands = false;
};

EncoderState& encoder() {
    static EncoderState s;
    return s;
}

/*
 * Record an image-memory barrier transitioning `image` from `oldLayout` to
 * `newLayout` on the active command buffer. Used by begin_render_pass() /
 * commit_frame() to put swapchain images into COLOR_ATTACHMENT_OPTIMAL before
 * dynamic rendering and back into PRESENT_SRC_KHR before present. Without
 * these barriers MoltenVK sees a PRESENT_SRC image used as a colour
 * attachment, which is spec-illegal and produces a black screen.
 *
 *   isDepthStencil : selects the aspect mask (color vs depth+stencil) and
 *                    the destination pipeline stage.
 */
void record_layout_barrier(VkCommandBuffer cb, VkImage image, VkFormat format,
                           VkImageLayout oldLayout, VkImageLayout newLayout,
                           bool isDepthStencil) {
    if (image == VK_NULL_HANDLE || oldLayout == newLayout) return;

    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;

    if (isDepthStencil) {
        // D32_SFLOAT_S8_UINT has separate depth + stencil aspects.
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    } else {
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount = 1;
    (void)format;

    // Source stage mask: who wrote the image in oldLayout.
    VkPipelineStageFlags srcStage;
    VkAccessFlags srcAccess;
    switch (oldLayout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            // No prior writes; contents discarded.
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            srcAccess = 0;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            // Image came back from present; the presentation engine read it.
            srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            srcAccess = VK_ACCESS_MEMORY_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            srcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            srcStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            srcAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        default:
            srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            srcAccess = VK_ACCESS_MEMORY_WRITE_BIT;
            break;
    }

    // Destination stage mask: who will read/write the image in newLayout.
    VkPipelineStageFlags dstStage;
    VkAccessFlags dstAccess;
    switch (newLayout) {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dstAccess = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dstAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            // Present engine reads the image.
            // FIX (root cause R): Use COLOR_ATTACHMENT_OUTPUT instead of
            // BOTTOM_OF_PIPE. BOTTOM_OF_PIPE is legal per spec but MoltenVK
            // historically maps it to a no-op barrier, which can cause the
            // color-attachment writes from the render pass to not be fully
            // visible to the present engine — the present then reads stale/
            // incomplete pixels (black screen). COLOR_ATTACHMENT_OUTPUT is
            // the stage where the presentation engine reads the image on
            // MoltenVK/Metal, and is the stage MobileGL uses for this
            // transition. This guarantees the color attachment writes complete
            // before present.
            dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dstAccess = VK_ACCESS_MEMORY_READ_BIT;
            break;
        default:
            dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            dstAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            break;
    }

    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(cb, srcStage, dstStage, 0,
                         0, nullptr, 0, nullptr, 1, &b);
}

} // namespace

bool render_pass_active() { return encoder().passActive; }

void set_clear_color(float r, float g, float b, float a) {
    auto& e = encoder();
    e.clearColor[0] = r; e.clearColor[1] = g; e.clearColor[2] = b; e.clearColor[3] = a;
}
void set_clear_depth(double d) { encoder().clearDepth = d; }
void set_clear_stencil(int s)  { encoder().clearStencil = s; }
void set_load_clear(bool clear){ encoder().loadClear = clear; }

void set_active_swapchain(Swapchain* sc) {
    encoder().activeSwapchain = sc;
}

/*
 * Ensure the current frame slot's command buffer is in the RECORDING state.
 * See the header comment for the full rationale. In short: with per-slot
 * command buffers, after commit_frame() submits slot N and advances to
 * slot N+1, the next recording operation must switch the alias to slot N+1's
 * buffer, wait on its fence, reset it, and begin it. This function does all
 * of that lazily.
 *
 * This is the FIX for the black-screen-with-sound root cause: the old code
 * used a single shared command buffer and did vkResetCommandBuffer at the
 * end of commit_frame() — resetting a buffer the GPU was still executing,
 * which is Vulkan spec UB. The per-slot design + lazy ensure eliminates this
 * by never resetting a pending buffer.
 */
bool ensure_command_buffer_recording() {
    Backend* b = backend();
    if (!b->initialized) return false;
    if (b->commandBufferRecording) return true;  // fast path
    if (b->deviceLost) return false;
    if (!b->commandPool) return false;

    // Wait on the current slot's fence if a submit is pending on it. After
    // the wait, this slot's command buffer is no longer pending and can be
    // safely reset. The fence was created with VK_FENCE_CREATE_SIGNALED_BIT,
    // so the very first frame's wait (fencePending=false) is skipped.
    if (b->fencePending[b->currentFrame]) {
        VkFence fence = b->frameFences[b->currentFrame];
        VkResult wr = vkWaitForFences(b->device, 1, &fence, VK_TRUE, UINT64_MAX);
        if (wr != VK_SUCCESS) {
            MITHRIL_LOG_ERROR("vk", "ensure_command_buffer_recording: "
                              "vkWaitForFences(slot=%d) failed (rc=%d) — "
                              "possible device lost",
                              b->currentFrame, (int)wr);
            b->deviceLost = true;
            return false;
        }
        b->fencePending[b->currentFrame] = false;
        // The fence wait guarantees all GPU work submitted to this slot has
        // completed. Any resources deferred to this slot's disposal queue
        // (glDeleteBuffers / glBufferData orphan / glDeleteTextures from the
        // frame that last used this slot) are now safe to actually destroy.
        // Without this drain, the deferred VkBuffer/VkImage handles would
        // leak until the slot is reused again (kMaxFramesInFlight later),
        // and more critically, the drain would happen too late — the
        // disposal queue would accumulate unboundedly during rapid buffer
        // churn (e.g. MC's per-frame uniform buffer updates).
        drain_disposal_queue(b->currentFrame);
    }

    // Switch the alias to the current slot's buffer and reset+begin it.
    b->commandBuffer = b->commandBuffers[b->currentFrame];
    vkResetCommandBuffer(b->commandBuffer, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(b->commandBuffer, &bi) != VK_SUCCESS) {
        MITHRIL_LOG_ERROR("vk", "ensure_command_buffer_recording: "
                          "vkBeginCommandBuffer failed (slot=%d)",
                          b->currentFrame);
        b->commandBufferRecording = false;
        return false;
    }
    b->commandBufferRecording = true;
    encoder().hasCommands = false;  // fresh buffer, no commands yet
    return true;
}

void begin_render_pass(VkImageView* color_views, int color_count,
                       VkImageView depth_view, int width, int height, int samples) {
    (void)samples;
    Backend* b = backend();
    if (!b->initialized || !b->commandBuffer) return;
    EncoderState& e = encoder();
    if (e.passActive) return;  // coalesce draws into one pass

    // Ensure the current slot's command buffer is in the recording state.
    // After commit_frame() submits slot N and advances to slot N+1, the
    // alias b->commandBuffer is stale (points at the pending slot N buffer).
    // This call lazily switches to slot N+1's buffer, waits on its fence,
    // resets it, and begins it. On the very first frame, the buffer is
    // already recording (begun by init_device), so this is a no-op.
    if (!ensure_command_buffer_recording()) {
        return;  // device lost or begin failed — skip this pass
    }

    // The command buffer is already in the recording state — either begun by
    // init_device() (first frame) or by commit_frame() (subsequent frames).
    // Pre-frame commands (layout transitions, texture uploads, etc.) recorded
    // before this point are preserved and will be submitted with this pass.
    // Do NOT reset the command buffer here; that would discard those records.

    // Record the per-frame attachments so draw commands can reference them.
    e.colorCount = color_count > 8 ? 8 : color_count;
    for (int i = 0; i < e.colorCount; ++i) e.colorViews[i] = color_views ? color_views[i] : VK_NULL_HANDLE;
    e.depthView = depth_view;
    e.width = width;
    e.height = height;

    // ---- Layout barriers for the swapchain-backed default framebuffer ----
    // dynamic-rendering hard-codes imageLayout = COLOR_ATTACHMENT_OPTIMAL in
    // the VkRenderingAttachmentInfo below. The swapchain image comes back from
    // acquire in PRESENT_SRC_KHR (or UNDEFINED on first use); without an
    // explicit barrier transitioning it to COLOR_ATTACHMENT_OPTIMAL, MoltenVK
    // sees an illegal layout and renders nothing (black screen). The depth
    // image is created with initialLayout = UNDEFINED and needs the same
    // one-shot transition to DEPTH_STENCIL_ATTACHMENT_OPTIMAL on first use.
    //
    // swapchainColorWasUndefined: set when the colour image was transitioned
    // out of UNDEFINED this frame. Used below to pick DONT_CARE for the load
    // op (LOAD on an image whose contents were discarded is wasteful and
    // spec-discouraged; DONT_CARE matches the discard semantics).
    bool swapchainColorWasUndefined = false;
    bool swapchainDepthWasUndefined = false;
    if (e.activeSwapchain) {
        Swapchain* sc = e.activeSwapchain;
        if (sc->currentImage >= 0 && sc->currentImage < (int)sc->views.size()) {
            // Only barrier the image if one of the bound colour attachments is
            // the swapchain's current view (i.e. we're rendering to FBO 0).
            bool swapchainBound = false;
            for (int i = 0; i < e.colorCount; ++i) {
                if (e.colorViews[i] == sc->views[sc->currentImage]) {
                    swapchainBound = true;
                    break;
                }
            }
            if (swapchainBound && sc->currentColorLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
                swapchainColorWasUndefined = (sc->currentColorLayout == VK_IMAGE_LAYOUT_UNDEFINED);
                record_layout_barrier(b->commandBuffer,
                                      sc->images[sc->currentImage], sc->format,
                                      sc->currentColorLayout,
                                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                      /*isDepthStencil=*/false);
                sc->currentColorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
        }
        // Depth: one-shot UNDEFINED -> DEPTH_STENCIL_ATTACHMENT_OPTIMAL.
        if (sc->depthImage != VK_NULL_HANDLE && sc->depthView != VK_NULL_HANDLE &&
            e.depthView == sc->depthView && !sc->depthLayoutInitialized) {
            record_layout_barrier(b->commandBuffer,
                                  sc->depthImage, VK_FORMAT_D32_SFLOAT_S8_UINT,
                                  VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                  /*isDepthStencil=*/true);
            sc->depthLayoutInitialized = true;
            swapchainDepthWasUndefined = true;
        }
    }

    // Begin dynamic rendering.
    // loadOp selection — aligned with MobileGL's VkRenderPassManager
    // (VkRenderPassManager.cpp:711-784). Priority order (hasClear first):
    //   1. hasClear (e.loadClear)  -> CLEAR   (discard prior contents)
    //   2. trackedLayout == UNDEFINED -> DONT_CARE (no valid contents to load;
    //      LOAD on UNDEFINED is spec-illegal and wastes tile bandwidth)
    //   3. otherwise               -> LOAD    (preserve existing contents)
    // MobileGL sets initialLayout=UNDEFINED for cases 1 & 2; we achieve the
    // same via the acquire->attachment barrier using oldLayout=UNDEFINED
    // (swapchain_acquire_color deliberately resets currentColorLayout to
    // UNDEFINED on every acquire, since post-present contents are undefined).
    VkRenderingAttachmentInfoKHR colorAttachs[8] = {};
    for (int i = 0; i < e.colorCount; ++i) {
        colorAttachs[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
        colorAttachs[i].imageView = e.colorViews[i];
        colorAttachs[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        // Color loadOp (MobileGL VkRenderPassManager.cpp:713,776-780):
        //   hasClear -> CLEAR; !hasClear && UNDEFINED -> DONT_CARE; else LOAD.
        // swapchainColorWasUndefined covers the swapchain image on its first
        // pass of the frame (currentColorLayout was UNDEFINED before the
        // acquire->attachment barrier). Subsequent passes in the same frame
        // see COLOR_ATTACHMENT_OPTIMAL and correctly use LOAD to preserve the
        // first pass's output.
        if (e.loadClear) {
            colorAttachs[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        } else if (swapchainColorWasUndefined &&
                   e.activeSwapchain &&
                   e.colorViews[i] == e.activeSwapchain->views[e.activeSwapchain->currentImage]) {
            colorAttachs[i].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        } else {
            colorAttachs[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        }
        colorAttachs[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachs[i].clearValue.color.float32[0] = e.clearColor[0];
        colorAttachs[i].clearValue.color.float32[1] = e.clearColor[1];
        colorAttachs[i].clearValue.color.float32[2] = e.clearColor[2];
        colorAttachs[i].clearValue.color.float32[3] = e.clearColor[3];
    }
    // Depth/stencil loadOp (MobileGL ResolveDepthStencilAttachmentLoadInfo,
    // VkRenderPassManager.cpp:140-155). Same priority: hasClear -> CLEAR;
    // UNDEFINED (first use) -> DONT_CARE; else LOAD. The depth image is
    // persistent across frames (never presented), so after the one-shot
    // UNDEFINED->DEPTH_STENCIL_ATTACHMENT_OPTIMAL transition it stays valid
    // and subsequent passes use LOAD to preserve depth across passes.
    // NOTE: glClear now uses backend_clear_attachments (vkCmdClearAttachments)
    // to clear only the requested aspects, NOT loadOp=CLEAR. The loadClear
    // flag here only affects the initial loadOp of the pass, and glClear
    // sets it to LOAD (not CLEAR) before calling begin_render_pass.
    VkRenderingAttachmentInfoKHR depthAttach{};
    depthAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    depthAttach.imageView = e.depthView;
    depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    if (e.loadClear) {
        depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    } else if (swapchainDepthWasUndefined) {
        depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    } else {
        depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    }
    depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttach.clearValue.depthStencil.depth = (float)e.clearDepth;
    depthAttach.clearValue.depthStencil.stencil = (uint32_t)e.clearStencil;

    VkRenderingInfoKHR ri{};
    ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    ri.renderArea.offset.x = 0;
    ri.renderArea.offset.y = 0;
    ri.renderArea.extent.width = (uint32_t)width;
    ri.renderArea.extent.height = (uint32_t)height;
    ri.layerCount = 1;
    ri.colorAttachmentCount = (uint32_t)e.colorCount;
    ri.pColorAttachments = e.colorCount > 0 ? colorAttachs : nullptr;
    ri.pDepthAttachment = e.depthView ? &depthAttach : nullptr;
    ri.pStencilAttachment = e.depthView ? &depthAttach : nullptr;

    // Resolve the dynamic-rendering entry point (Vulkan 1.2 + extension).
    static PFN_vkCmdBeginRenderingKHR fn = nullptr;
    if (!fn) {
        fn = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(b->device, "vkCmdBeginRendering");
        if (!fn) fn = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(b->device, "vkCmdBeginRenderingKHR");
    }
    if (fn) fn(b->commandBuffer, &ri);

    e.passActive = true;
    e.hasCommands = true;  // begin_render_pass recorded real commands
    e.loadClear = false;  // subsequent passes within the frame use LOAD
}

void end_render_pass() {
    Backend* b = backend();
    EncoderState& e = encoder();
    if (!e.passActive || !b->commandBuffer) return;

    static PFN_vkCmdEndRenderingKHR fn = nullptr;
    if (!fn) {
        fn = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(b->device, "vkCmdEndRendering");
        if (!fn) fn = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(b->device, "vkCmdEndRenderingKHR");
    }
    if (fn) fn(b->commandBuffer);

    e.passActive = false;
}

/*
 * Clear specific aspects of the current framebuffer via vkCmdClearAttachments.
 * Must be called inside a render pass. This is the correct implementation of
 * glClear: it clears ONLY the buffers specified by `mask`, unlike the old
 * loadOp=CLEAR approach which cleared ALL attachments regardless of mask.
 *
 * MobileGL (VulkanRenderer.cpp:4230-4358) uses the same vkCmdClearAttachments
 * approach, respecting GL_SCISSOR_TEST for the clear rect.
 */
void clear_attachments(uint32_t mask, int x, int y, int w, int h) {
    Backend* b = backend();
    EncoderState& e = encoder();
    if (!b->commandBuffer || !e.passActive) return;
    if (mask == 0) return;

    // Build the VkClearAttachment array for the requested aspects.
    std::vector<VkClearAttachment> attaches;
    VkClearValue cv{};

    if (mask & GL_COLOR_BUFFER_BIT) {
        cv.color.float32[0] = e.clearColor[0];
        cv.color.float32[1] = e.clearColor[1];
        cv.color.float32[2] = e.clearColor[2];
        cv.color.float32[3] = e.clearColor[3];
        // One clear attachment per color attachment (VK_IMAGE_ASPECT_COLOR_BIT
        // covers all color aspects, but per-attachment is safer with MRT).
        for (int i = 0; i < e.colorCount; ++i) {
            VkClearAttachment a{};
            a.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            a.colorAttachment = (uint32_t)i;
            a.clearValue = cv;
            attaches.push_back(a);
        }
    }
    if (mask & GL_DEPTH_BUFFER_BIT) {
        VkClearAttachment a{};
        a.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        a.clearValue.depthStencil.depth = (float)e.clearDepth;
        attaches.push_back(a);
    }
    if (mask & GL_STENCIL_BUFFER_BIT) {
        VkClearAttachment a{};
        a.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
        a.clearValue.depthStencil.stencil = (uint32_t)e.clearStencil;
        attaches.push_back(a);
    }
    if (attaches.empty()) return;

    // Determine the clear rect. GL scissor test clips the clear region.
    // When scissor is disabled, clear the full framebuffer rect.
    VkClearRect rect{};
    if (mithril::g_state && mithril::g_state->scissorTest) {
        rect.rect.offset.x = mithril::g_state->scissorX;
        rect.rect.offset.y = mithril::g_state->scissorY;
        rect.rect.extent.width = (uint32_t)mithril::g_state->scissorW;
        rect.rect.extent.height = (uint32_t)mithril::g_state->scissorH;
    } else {
        rect.rect.offset.x = 0;
        rect.rect.offset.y = 0;
        rect.rect.extent.width = (uint32_t)w;
        rect.rect.extent.height = (uint32_t)h;
    }
    rect.baseArrayLayer = 0;
    rect.layerCount = 1;

    vkCmdClearAttachments(b->commandBuffer,
                          (uint32_t)attaches.size(), attaches.data(),
                          1, &rect);
    e.hasCommands = true;
}

void commit_frame() {
    Backend* b = backend();
    if (b->deviceLost) {
        // 持久性故障已挂起，跳过 submit 避免死循环刷屏
        return;
    }
    if (!b->initialized || !b->commandBuffer) return;
    EncoderState& e = encoder();
    if (e.passActive) end_render_pass();

    // ---- Ensure the current slot's command buffer is recording ----
    // This is the async-pipeline wait point (mirrors MobileGL's
    // FrameContext::WaitAndAcquireNextImage, FrameContext.cpp:220, which
    // calls vkWaitForFences on the slot's imageInFlightFence before acquire).
    //
    // With per-slot command buffers, after the previous commit_frame()
    // submitted slot N and advanced to slot N+1, the alias b->commandBuffer
    // still points at slot N's pending buffer. This call lazily switches to
    // the current slot's buffer, waits on its fence (submitted
    // kMaxFramesInFlight frames ago — the deferred async wait that gives the
    // GPU kMaxFramesInFlight-1 frames of latency), resets it, and begins it.
    //
    // If the buffer is already recording (e.g. this is the first commit of
    // the process, or a previous commit's shouldSubmit was false so the
    // buffer was never submitted), this is a fast no-op.
    //
    // This replaces the old single-buffer design's "wait at top + reset+begin
    // at bottom" pattern, which reset a pending buffer (spec UB) at the bottom.
    if (!ensure_command_buffer_recording()) {
        return;  // device lost or begin failed
    }

    // ---- Decide whether we need to submit at all ----
    // MobileGL's Present() logic (VulkanRenderer.cpp:6912):
    //   shouldSubmit = hasCommandBufferRecorded || needsLayoutTransitionForPresent
    //
    // We MUST submit (and signal renderFinished) whenever:
    //   (a) commands were recorded this frame (normal draw pass), OR
    //   (b) the swapchain image is NOT already in PRESENT_SRC_KHR (it is in
    //       COLOR_ATTACHMENT_OPTIMAL from a previous render pass and needs a
    //       layout-transition barrier before present), OR
    //   (c) imageAvailable has not been consumed yet this frame (the very
    //       first commit after acquire — even with no draw commands, we must
    //       wait on imageAvailable so the GPU does not race ahead of the
    //       presentation engine; this is the MobileGL TransitionToPresent
    //       fallback path).
    //
    // Without this, a frame where the app only calls glClear (no draws) or a
    // frame where eglWaitClient already flushed the draws would skip submit,
    // leave renderFinished unsignaled, and present would wait on a semaphore
    // that is never signaled -> MoltenVK hangs / black screen.
    Swapchain* sc = e.activeSwapchain;
    bool hasCommands = e.hasCommands;
    bool needsLayoutTransition = false;
    bool needsImageAvailableWait = false;
    if (sc && sc->currentImage >= 0 && sc->currentImage < (int)sc->images.size()) {
        // FIX (root cause N): present requires the image to be in
        // PRESENT_SRC_KHR layout. The old code EXCLUDED UNDEFINED from the
        // transition check, assuming begin_render_pass would always transition
        // UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL first. But when the swapchain
        // is created lazily (eglMakeCurrent failed because the window wasn't
        // sized yet, so eglSwapBuffers creates it), the first frame has NO
        // draw commands — the image stays UNDEFINED and is presented directly,
        // which is spec-illegal and crashes MoltenVK's IOSurfaceBindAccel
        // (SIGSEGV) because the IOSurface was never properly bound as a
        // render target. Now we transition to PRESENT_SRC_KHR whenever the
        // layout is not already PRESENT_SRC_KHR (UNDEFINED -> PRESENT_SRC is
        // a legal, content-discarding barrier).
        if (sc->currentColorLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
            needsLayoutTransition = true;
        }
        needsImageAvailableWait = !sc->imageAvailableConsumed;
    }
    bool shouldSubmit = hasCommands || needsLayoutTransition || needsImageAvailableWait;

    if (!shouldSubmit) {
        return;
    }

    // FIX (root cause S): If we are about to present but NO draw commands were
    // recorded this frame (hasCommands=false), the swapchain image was never
    // used as a render target. On MoltenVK/iOS, the IOSurface backing the
    // swapchain image is lazily bound the first time the image is used as a
    // color attachment in a render pass. If we present without ever rendering
    // to it, MoltenVK presents a drawable whose IOSurface was never bound →
    // the Metal driver's IOSurfaceBindAccel dereferences an uninitialized
    // IOSurface → SIGSEGV or silent black screen.
    //
    // This happens on the first frame when the swapchain is created lazily
    // (eglMakeCurrent failed because the window wasn't sized, so eglSwapBuffers
    // creates the swapchain — but the app already finished rendering with no
    // color attachment, so no draw commands were recorded).
    //
    // Fix: if we need to present (needsLayoutTransition or needsImageAvailableWait)
    // but have no draw commands, insert a minimal dynamic-rendering pass that
    // touches the swapchain color attachment. This forces MoltenVK to bind the
    // IOSurface as a render target, making the subsequent present safe.
    // MobileGL avoids this because its TransitionToPresent path still records
    // a real command buffer with a layout barrier, and traditional VkRenderPass
    // ensures attachment binding happens during render pass begin.
    if (sc && !hasCommands && (needsLayoutTransition || needsImageAvailableWait)) {
        // Insert a dummy render pass: BeginRendering with DONT_CARE loadOp,
        // no draws, EndRendering. This is enough to trigger IOSurface binding
        // in MoltenVK without clearing or modifying the image contents.
        if (sc->currentImage >= 0 && sc->currentImage < (int)sc->views.size() &&
            sc->views[sc->currentImage] != VK_NULL_HANDLE) {
            // Ensure the image is in COLOR_ATTACHMENT_OPTIMAL first (if it
            // isn't already). The layout transition below (needsLayoutTransition)
            // will handle COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR.
            if (sc->currentColorLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
                sc->currentColorLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
                record_layout_barrier(b->commandBuffer,
                                      sc->images[sc->currentImage], sc->format,
                                      sc->currentColorLayout,
                                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                      /*isDepthStencil=*/false);
                sc->currentColorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
            // Minimal render pass to trigger IOSurface binding.
            VkRenderingAttachmentInfoKHR dummyAttach{};
            dummyAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
            dummyAttach.imageView = sc->views[sc->currentImage];
            dummyAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            dummyAttach.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            dummyAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            VkRenderingInfoKHR dummyRI{};
            dummyRI.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
            dummyRI.renderArea.offset.x = 0;
            dummyRI.renderArea.offset.y = 0;
            dummyRI.renderArea.extent.width = sc->width;
            dummyRI.renderArea.extent.height = sc->height;
            dummyRI.layerCount = 1;
            dummyRI.colorAttachmentCount = 1;
            dummyRI.pColorAttachments = &dummyAttach;
            static PFN_vkCmdBeginRenderingKHR beginFn = nullptr;
            static PFN_vkCmdEndRenderingKHR endFn = nullptr;
            if (!beginFn) {
                beginFn = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(b->device, "vkCmdBeginRendering");
                if (!beginFn) beginFn = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(b->device, "vkCmdBeginRenderingKHR");
            }
            if (!endFn) {
                endFn = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(b->device, "vkCmdEndRendering");
                if (!endFn) endFn = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(b->device, "vkCmdEndRenderingKHR");
            }
            if (beginFn) beginFn(b->commandBuffer, &dummyRI);
            if (endFn) endFn(b->commandBuffer);
            // After the dummy pass, the image is in COLOR_ATTACHMENT_OPTIMAL.
            // The needsLayoutTransition block below will transition it to
            // PRESENT_SRC_KHR for present.
            needsLayoutTransition = true;
        }
    }

    // The command buffer is guaranteed to be in the recording state here —
    // ensure_command_buffer_recording() at the top of this function took care
    // of it (including the fence wait, reset, and begin). The old defensive
    // recovery block that was here has been removed because it reset the
    // buffer WITHOUT waiting on the fence first, which could reset a pending
    // buffer (spec UB) under the per-slot design.

    // Transition the swapchain color image back to PRESENT_SRC_KHR before
    // vkEndCommandBuffer so vkQueuePresentKHR sees a legal layout. Without
    // this, present is spec-illegal and MoltenVK drops the frame (black screen).
    if (sc && needsLayoutTransition) {
        record_layout_barrier(b->commandBuffer,
                              sc->images[sc->currentImage], sc->format,
                              sc->currentColorLayout,
                              VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                              /*isDepthStencil=*/false);
        sc->currentColorLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    VkResult r = vkEndCommandBuffer(b->commandBuffer);
    if (r != VK_SUCCESS) {
        MITHRIL_LOG_ERROR("vk", "vkEndCommandBuffer failed (rc=%d)", (int)r);
        // Buffer is now in an invalid state. Force reset+begin so the next
        // begin_render_pass has a recording buffer to write into; otherwise
        // the render thread spins forever issuing vkCmd* into a dead buffer.
        vkResetCommandBuffer(b->commandBuffer, 0);
        VkCommandBufferBeginInfo rbi{};
        rbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        rbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(b->commandBuffer, &rbi) == VK_SUCCESS) {
            b->commandBufferRecording = true;
        }
        e.hasCommands = false;
        if (sc) sc->needsRebuild = true;
        return;
    }
    b->commandBufferRecording = false;  // executable, not recording

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &b->commandBuffer;

    // ---- Wait on imageAvailable (acquire semaphore) if not yet consumed ----
    // This is the CRITICAL fix for the black screen: vkAcquireNextImageKHR
    // signals imageAvailable, and the FIRST vkQueueSubmit of the frame MUST
    // wait on it (at COLOR_ATTACHMENT_OUTPUT stage) so the GPU does not start
    // writing to the swapchain image before the presentation engine releases
    // it. Without this wait, the GPU renders into a stale/owned-by-presenter
    // image and the rendered contents never reach the display -> black screen.
    //
    // Only the first commit_frame() per frame waits on imageAvailable (mid-
    // frame flushes via eglWaitClient consume it on the first submit). This
    // mirrors MobileGL's imageAvailableSemaphoreConsumed flag
    // (FrameContext.cpp:191-193).
    VkSemaphore waitSemaphore = VK_NULL_HANDLE;
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (sc && sc->imageAvailable != VK_NULL_HANDLE && !sc->imageAvailableConsumed) {
        waitSemaphore = sc->imageAvailable;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &waitSemaphore;
        si.pWaitDstStageMask = &waitStage;
    }

    // ---- Signal renderFinished so present can wait on it ----
    // Signal the per-image render-finished semaphore for the image we just
    // rendered into. This mirrors MobileGL's GetSubmitInfo (FrameContext.cpp:196),
    // which signals m_swapchainImageRenderFinishedSemaphores[swapchainImageIndex].
    //
    // Per-image (not per-frame-slot or per-swapchain) signaling is essential:
    // present must wait on the exact semaphore signaled by the submit that
    // rendered into THIS image. A single shared semaphore races under triple
    // buffering (image A's submit signals it, then image B's submit re-signals
    // before present consumes A's signal → black screen or spec violation).
    //
    // Only signal if not already signaled for THIS image this frame (a binary
    // semaphore cannot be re-signaled while still signaled). renderFinished
    // SignaledPerImage[currentImage] is cleared by swapchain_present_and_acquire
    // after present consumes the signal.
    VkSemaphore signalSemaphore = VK_NULL_HANDLE;
    if (sc && sc->currentImage >= 0 &&
        (size_t)sc->currentImage < sc->renderFinishedPerImage.size() &&
        sc->renderFinishedPerImage[sc->currentImage] != VK_NULL_HANDLE &&
        (size_t)sc->currentImage < sc->renderFinishedSignaledPerImage.size() &&
        !sc->renderFinishedSignaledPerImage[sc->currentImage]) {
        signalSemaphore = sc->renderFinishedPerImage[sc->currentImage];
        sc->renderFinishedSignaledPerImage[sc->currentImage] = true;
    }
    if (signalSemaphore != VK_NULL_HANDLE) {
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &signalSemaphore;
    }

    VkFence fence = b->frameFences[b->currentFrame];
    vkResetFences(b->device, 1, &fence);
    r = vkQueueSubmit(b->graphicsQueue, 1, &si, fence);
    if (r != VK_SUCCESS) {
        // Classify the failure: VK_ERROR_OUT_OF_DATE_KHR / VK_SUBOPTIMAL_KHR
        // are swapchain-rebuild signals handled by the eglSwapBuffers path,
        // NOT fatal GPU faults. Every other non-success result (DEVICE_LOST,
        // OUT_OF_DEVICE_MEMORY, OUT_OF_HOST_MEMORY, INVALID_EXTERNAL_HANDLE,
        // and any other negative VkResult) indicates a persistent GPU fault
        // and is counted toward the deviceLost threshold.
        if (r != VK_ERROR_OUT_OF_DATE_KHR && r != VK_SUBOPTIMAL_KHR) {
            b->consecutiveSubmitFailures++;
            if (b->consecutiveSubmitFailures >= 3 && !b->deviceLost) {
                b->deviceLost = true;
                MITHRIL_LOG_ERROR("vk", "Persistent GPU fault detected after %d "
                                  "consecutive submit failures — rendering "
                                  "suspended to prevent log flooding",
                                  b->consecutiveSubmitFailures);
            }
        }
        // Throttle the per-failure log: first failure, then every 100th.
        // Once deviceLost is set, commit_frame returns early above and this
        // path is no longer reached.
        if (b->consecutiveSubmitFailures == 1 ||
            b->consecutiveSubmitFailures % 100 == 0) {
            MITHRIL_LOG_ERROR("vk", "vkQueueSubmit failed (rc=%d) — marking "
                              "swapchain for rebuild", (int)r);
        }
        // vkQueueSubmit failure (e.g. VK_ERROR_OUT_OF_DEVICE_MEMORY /
        // VK_ERROR_DEVICE_LOST) means the command buffer was NOT consumed.
        // The fence will NOT be signaled, so we must NOT wait on it below
        // (vkWaitForFences would hang forever). Reset the fence manually and
        // mark the swapchain dead so EGL rebuilds it on the next swap.
        vkResetFences(b->device, 1, &fence);
        // Roll back the semaphore state: neither imageAvailable nor
        // renderFinished was actually consumed/signal'd (submit failed), so
        // the next commit must be allowed to wait/signal again. Without
        // these rollbacks, the state would be inconsistent (e.g. next submit
        // would skip the imageAvailable wait, racing the presenter again).
        if (sc) {
            // Roll back the per-image render-finished signal flag for the
            // image we tried (and failed) to render into, so the next
            // commit_frame() can signal it again.
            if (sc->currentImage >= 0 &&
                (size_t)sc->currentImage < sc->renderFinishedSignaledPerImage.size()) {
                sc->renderFinishedSignaledPerImage[sc->currentImage] = false;
            }
            // imageAvailableConsumed stays false (we never set it true on
            // failure) — correct, the next submit should still wait.
            sc->needsRebuild = true;
        }
        // Reset+begin so the next frame has a recording buffer.
        vkResetCommandBuffer(b->commandBuffer, 0);
        VkCommandBufferBeginInfo rbi{};
        rbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        rbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(b->commandBuffer, &rbi) == VK_SUCCESS) {
            b->commandBufferRecording = true;
        }
        e.hasCommands = false;
        return;
    }

    // Success: a clean submit clears the consecutive-failure counter (only
    // persistent faults should keep it climbing toward the deviceLost threshold).
    b->consecutiveSubmitFailures = 0;

    // Submit succeeded: imageAvailable is now consumed (the wait was honored).
    if (sc) {
        sc->imageAvailableConsumed = true;
    }

    // Mark this slot's fence as pending. The NEXT commit_frame() that cycles
    // back to this slot (kMaxFramesInFlight frames later) will wait on
    // frameFences[currentFrame] via ensure_command_buffer_recording() before
    // reusing this slot's command buffer. This must be set BEFORE the
    // currentFrame advance below, so the flag lands on the slot we just
    // submitted (not the next one). Without this, the deferred wait in
    // ensure_command_buffer_recording() is a no-op (fencePending is always
    // false) and the next reset of this slot's buffer would reset a buffer
    // the GPU is still executing -> spec violation / UAF crash. The fences
    // start signaled (VK_FENCE_CREATE_SIGNALED_BIT), so the first frame's
    // wait is correctly skipped (flag starts false).
    b->fencePending[b->currentFrame] = true;

    // CRITICAL FIX: do NOT vkResetCommandBuffer here.
    //
    // The old single-buffer code reset+begin the command buffer at this point
    // so that inter-frame texture uploads (stage_and_copy_image) had a
    // recording buffer to write into. But with a single shared buffer, this
    // reset hit a buffer the GPU was still executing (spec UB) — the root
    // cause of the black-screen-with-sound issue.
    //
    // With per-slot command buffers, we advance to the NEXT slot's buffer and
    // leave the just-submitted slot's buffer alone (pending on the GPU). The
    // next slot's buffer is lazily reset+begin by ensure_command_buffer_recording()
    // when the next begin_render_pass / stage_and_copy_image / commit_frame
    // needs it. The lazy ensure waits on the next slot's fence (submitted
    // kMaxFramesInFlight frames ago) before resetting, so no pending buffer is
    // ever reset.
    //
    // The alias b->commandBuffer is NOT updated here — it still points at the
    // just-submitted (pending) buffer. ensure_command_buffer_recording() will
    // update it to point at the new slot's buffer on the next call. Code that
    // records into b->commandBuffer between now and the next ensure call MUST
    // call ensure_command_buffer_recording() first (stage_and_copy_image,
    // transition_image_layout, bind_program_descriptors).
    b->commandBufferRecording = false;  // just-submitted buffer is no longer recording
    b->currentFrame = (b->currentFrame + 1) % kMaxFramesInFlight;
    // Monotonic generation bump: descriptor pools are reset on first draw of
    // each generation (see DescriptorSet.cpp), so this must advance every frame
    // regardless of the cycling currentFrame value.
    b->frameGeneration++;

    e.hasCommands = false;  // fresh command buffer, no commands yet
}

/*
 * Drain GPU work that references the active swapchain, then detach the
 * swapchain from the encoder. Called by EGL BEFORE backend_destroy_swapchain()
 * (eglDestroySurface / ensure_swapchain resize path).
 *
 * Sequence:
 *   1. end_render_pass() + commit_frame() — flush any pending commands that
 *      reference the swapchain's images into the GPU. This submit is safe
 *      because ensure_command_buffer_recording() at the top of commit_frame
 *      waits on the current slot's fence before resetting its buffer.
 *   2. vkDeviceWaitIdle() — block until the GPU has finished executing those
 *      commands (and any prior in-flight submits on other slots), so the
 *      swapchain's IOSurface-backed images are no longer referenced by the
 *      driver. Without this wait, vkDestroySwapchainKHR / vkDestroyImageView
 *      would free IOSurfaces that the GPU is still reading, and the next
 *      IOSurfaceBindAccel call in the Metal driver would crash with SIGSEGV
 *      (UAF).
 *   3. Clear fencePending[] — after vkDeviceWaitIdle, ALL fences are signaled.
 *      Clearing the flags ensures the next commit_frame doesn't waste a
 *      vkWaitForFences call on an already-signaled fence, and more importantly
 *      ensures consistent state for the new swapchain's first frame.
 *   4. Reset commandBufferRecording — the just-submitted buffer is no longer
 *      recording. The next ensure_command_buffer_recording() will lazily
 *      reset+begin the current slot's buffer (now safe, fence signaled).
 *   5. set_active_swapchain(nullptr) — clear the encoder's raw pointer to the
 *      swapchain so begin_render_pass() / commit_frame() cannot record layout
 *      barriers against its (soon-to-be-freed) images.
 */
void drain_and_detach_swapchain() {
    Backend* b = backend();
    if (!b->initialized || !b->device) {
        set_active_swapchain(nullptr);
        return;
    }
    end_render_pass();
    commit_frame();
    vkDeviceWaitIdle(b->device);

    // After vkDeviceWaitIdle, all in-flight command buffers across ALL slots
    // have completed. Clear fencePending so the next ensure_command_buffer_recording()
    // on any slot doesn't wait on an already-signaled fence (benign but wasteful,
    // and the state should be clean for the new swapchain's first frame).
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        b->fencePending[i] = false;
    }
    // vkDeviceWaitIdle also guarantees all deferred resources are safe to
    // destroy now — drain every slot's disposal queue before detaching the
    // swapchain so no stale Vulkan handles outlive the swapchain they were
    // created for.
    drain_all_disposal_queues();
    // The command buffer alias still points at whatever slot was current when
    // commit_frame advanced. Mark it not-recording so the next
    // ensure_command_buffer_recording() will reset+begin it (safe now, since
    // vkDeviceWaitIdle guarantees no buffer is pending).
    b->commandBufferRecording = false;

    set_active_swapchain(nullptr);
}

} // namespace vk
} // namespace mithril

// ===========================================================================
// Public C API (declared in MG_Backend/Backend.h)
// ===========================================================================
extern "C" {

void backend_set_clear_color(float r, float g, float b, float a) {
    mithril::vk::set_clear_color(r, g, b, a);
}
void backend_set_clear_depth(double d) { mithril::vk::set_clear_depth(d); }
void backend_set_clear_stencil(int s)  { mithril::vk::set_clear_stencil(s); }
void backend_set_load_clear(void)      { mithril::vk::set_load_clear(true); }
void backend_set_load_load(void)       { mithril::vk::set_load_clear(false); }

void backend_clear_attachments(GLbitfield mask, int x, int y, int w, int h) {
    mithril::vk::clear_attachments(mask, x, y, w, h);
}

void backend_begin_render_pass(VkImageView* color_views, int color_count,
                               VkImageView depth_view, int width, int height, int samples) {
    mithril::vk::begin_render_pass(color_views, color_count, depth_view, width, height, samples);
}

void backend_end_render_pass(void) { mithril::vk::end_render_pass(); }
void backend_commit(void)          { mithril::vk::commit_frame(); }

void backend_set_active_swapchain(void* swapchain_state) {
    mithril::vk::set_active_swapchain((mithril::vk::Swapchain*)swapchain_state);
}

void backend_drain_and_detach_swapchain(void) {
    mithril::vk::drain_and_detach_swapchain();
}

void backend_bind_pipeline(VkPipeline pipeline) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (b->commandBuffer && pipeline) {
        vkCmdBindPipeline(b->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    }
}

void backend_set_viewport(int x, int y, int w, int h, double znear, double zfar) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer) return;
    // Use the GL viewport directly. GL's viewport is bottom-left origin while
    // Vulkan's is top-left, but MoltenVK flips the Y axis when translating to
    // Metal, so passing the GL values through unchanged matches the on-screen
    // behaviour of the previous Metal backend (and what host apps expect).
    VkViewport vp{};
    vp.x        = (float)x;
    vp.y        = (float)y;
    vp.width    = (float)w;
    vp.height   = (float)h;
    vp.minDepth = (float)znear;
    vp.maxDepth = (float)zfar;
    vkCmdSetViewport(b->commandBuffer, 0, 1, &vp);
}

void backend_set_scissor(int x, int y, int w, int h) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer) return;
    VkRect2D sc{};
    sc.offset.x = x; sc.offset.y = y;
    sc.extent.width = (uint32_t)w; sc.extent.height = (uint32_t)h;
    vkCmdSetScissor(b->commandBuffer, 0, 1, &sc);
}

void backend_set_vertex_buffer(int slot, VkBuffer buffer, VkDeviceSize offset) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer || !buffer) return;
    VkDeviceSize offsets[1] = { offset };
    vkCmdBindVertexBuffers(b->commandBuffer, (uint32_t)slot, 1, &buffer, offsets);
}

void backend_set_fragment_buffer(int slot, VkBuffer buffer, VkDeviceSize offset) {
    // No-op: fragment-stage UBO binding is handled by descriptor sets built in
    // DescriptorSet.cpp (backend_bind_program_descriptors). There is no Vulkan
    // "bind buffer to fragment stage slot" command outside of descriptor sets,
    // so this entry point exists only to satisfy the C API contract.
    (void)slot; (void)buffer; (void)offset;
}

void backend_set_vertex_texture(int slot, VkImageView view, VkSampler sampler) {
    // No-op: see backend_set_fragment_buffer — descriptor binding is centralised
    // in DescriptorSet.cpp (backend_bind_program_descriptors).
    (void)slot; (void)view; (void)sampler;
}

void backend_set_fragment_texture(int slot, VkImageView view, VkSampler sampler) {
    // No-op: see backend_set_fragment_buffer — descriptor binding is centralised
    // in DescriptorSet.cpp (backend_bind_program_descriptors).
    (void)slot; (void)view; (void)sampler;
}

void backend_set_blend_color(float r, float g, float b, float a) {
    // NOTE: parameter `b` is the blue blend constant (float); the backend ptr
    // is renamed to avoid shadowing it.
    mithril::vk::Backend* bk = mithril::vk::backend();
    if (!bk->commandBuffer) return;
    float bc[4] = { r, g, b, a };
    vkCmdSetBlendConstants(bk->commandBuffer, bc);
}

void backend_set_depth_bias(float slope, float clamp) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer) return;
    vkCmdSetDepthBias(b->commandBuffer, slope, clamp, 0.0f);
}

void backend_set_cull_mode(int mode) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer) return;
    VkCullModeFlags cull = VK_CULL_MODE_NONE;
    if (mode == 1) cull = VK_CULL_MODE_FRONT_BIT;
    else if (mode == 2) cull = VK_CULL_MODE_BACK_BIT;
    vkCmdSetCullMode(b->commandBuffer, cull);
}

void backend_set_front_face(int ccw) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer) return;
    vkCmdSetFrontFace(b->commandBuffer, ccw ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE);
}

void backend_set_depth_test(int enabled, int write_mask, int compare_func) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer) return;
    vkCmdSetDepthTestEnable(b->commandBuffer, enabled ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthWriteEnable(b->commandBuffer, write_mask ? VK_TRUE : VK_FALSE);
    VkCompareOp op = VK_COMPARE_OP_LESS;
    switch (compare_func) {
        case 0x200: op = VK_COMPARE_OP_NEVER; break;    // GL_NEVER
        case 0x201: op = VK_COMPARE_OP_LESS; break;     // GL_LESS
        case 0x202: op = VK_COMPARE_OP_EQUAL; break;    // GL_EQUAL
        case 0x203: op = VK_COMPARE_OP_LESS_OR_EQUAL; break;
        case 0x204: op = VK_COMPARE_OP_GREATER; break;
        case 0x205: op = VK_COMPARE_OP_NOT_EQUAL; break;
        case 0x206: op = VK_COMPARE_OP_GREATER_OR_EQUAL; break;
        case 0x207: op = VK_COMPARE_OP_ALWAYS; break;
        default: op = VK_COMPARE_OP_LESS; break;
    }
    vkCmdSetDepthCompareOp(b->commandBuffer, op);
}

void backend_set_color_write_mask(int r, int g, int b, int a) {
    (void)r; (void)g; (void)b; (void)a;
    // No-op by design: colorWriteMask is a STATIC pipeline state (part of
    // VkPipelineColorBlendAttachmentState), not a dynamic state. It is read
    // from g_state->colorMask at pipeline-creation time (Drawing.cpp packs it
    // into the pipeline signature), so changing glColorMask creates a new
    // pipeline rather than updating the bound one. VK_DYNAMIC_STATE_COLOR_WRITE
    // _ENABLE_EXT would allow dynamic toggling but requires an extension we do
    // not enable; the static approach is correct and matches MobileGL.
}

void backend_set_stencil_state(int enabled, int func, int ref, int mask,
                               int sfail, int dpfail, int dppass) {
    (void)enabled; (void)func; (void)ref; (void)mask;
    (void)sfail; (void)dpfail; (void)dppass;
    // Stencil dynamic state deferred (bring-up).
}

void backend_draw_arrays(int primitive, int first, int count) {
    (void)primitive;
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer) return;
    vkCmdDraw(b->commandBuffer, (uint32_t)count, 1, (uint32_t)first, 0);
}

void backend_draw_indexed(int primitive, int count, int index_type,
                          VkBuffer index_buffer, VkDeviceSize index_offset) {
    (void)primitive;
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer || !index_buffer) return;
    VkIndexType t = (index_type == 1) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
    vkCmdBindIndexBuffer(b->commandBuffer, index_buffer, index_offset, t);
    vkCmdDrawIndexed(b->commandBuffer, (uint32_t)count, 1, 0, 0, 0);
}

void backend_draw_arrays_instanced(int primitive, int first, int count, int primcount) {
    (void)primitive;
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer) return;
    vkCmdDraw(b->commandBuffer, (uint32_t)count, (uint32_t)primcount, (uint32_t)first, 0);
}

void backend_draw_indexed_instanced(int primitive, int count, int index_type,
                                    VkBuffer index_buffer, VkDeviceSize index_offset,
                                    int primcount) {
    (void)primitive;
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->commandBuffer || !index_buffer) return;
    VkIndexType t = (index_type == 1) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
    vkCmdBindIndexBuffer(b->commandBuffer, index_buffer, index_offset, t);
    vkCmdDrawIndexed(b->commandBuffer, (uint32_t)count, (uint32_t)primcount, 0, 0, 0);
}

} // extern "C"
