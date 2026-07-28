// Mithril-Wrapper - MG_Backend/DirectVulkan/Device.h
// Global Vulkan 1.2 device state (VkInstance / VkPhysicalDevice / VkDevice /
// VkQueue / VkCommandPool / VkCommandBuffer). Accessed by the other
// DirectVulkan translation units via vk_backend().
//
// iOS surface creation uses VK_EXT_metal_surface (vkCreateMetalSurfaceEXT) —
// NOT the deprecated VK_MVK_metal_surface. Portability is required:
//   * VK_KHR_portability_enumeration instance extension +
//     VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
//   * VK_KHR_portability_subset device extension (always enabled alongside
//     VK_KHR_swapchain)
// MoltenVK is statically linked, so there is no Vulkan loader / ICD file.
#ifndef MITHRIL_DIRECTVULKAN_DEVICE_H
#define MITHRIL_DIRECTVULKAN_DEVICE_H

#include <vulkan/vulkan.h>
#include <vector>

namespace mithril {
namespace vk {

// Minimum/maximum swapchain images in-flight (default 2; allows up to 3).
constexpr int kMaxFramesInFlight = 2;

// Deferred Vulkan resource destruction entry. When a GL buffer/texture/sampler
// is deleted or orphaned (glBufferData rename), its underlying VkBuffer/VkImage/
// VkDeviceMemory/VkImageView/VkSampler handles are pushed into a per-frame-slot
// disposal queue instead of being destroyed immediately. The queue for slot S
// is drained when ensure_command_buffer_recording() waits on fence[S] — at that
// point all command buffers submitted to slot S have completed on the GPU, so
// any Metal resources referenced by those command buffers are no longer in use.
//
// This fixes the Metal resource Use-After-Free crash (objc_retain of a zombie
// IOGPUMetalResource in MVKGraphicsResourcesCommandEncoderState::encodeBindings
// during vkEndCommandBuffer → MVKCmdDrawIndexed::encode). The crash occurred
// because glDeleteBuffers / glBufferData orphan destroyed VkBuffers immediately
// while MoltenVK's deferred encoding (MVK_CONFIG_PREFILL_METAL_COMMAND_BUFFERS=1)
// still held references to the corresponding MTLBuffer wrappers. With deferred
// destruction, the VkBuffer (and its MTLBuffer) survives until the GPU finishes
// all in-flight command buffers that reference it.
struct DeferredDestroy {
    VkBuffer       buffer = VK_NULL_HANDLE;
    VkImage        image = VK_NULL_HANDLE;
    VkImageView    view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkSampler      sampler = VK_NULL_HANDLE;
};

// Global Vulkan backend state. A single instance lives for the process; it is
// created by backend_init() and torn down by backend_shutdown().
struct Backend {
    bool initialized = false;

    VkInstance       instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice         device = VK_NULL_HANDLE;
    VkQueue          graphicsQueue = VK_NULL_HANDLE;
    uint32_t         graphicsFamily = 0xFFFFFFFFu;

    VkCommandPool    commandPool = VK_NULL_HANDLE;
    // Per-frame-slot primary command buffers (mirrors MobileGL's
    // FrameContext::FrameData::commandBuffer). Each slot has its own buffer so
    // that submitting slot N's buffer and then recording into slot N+1's
    // buffer can happen concurrently without resetting a pending buffer
    // (which is Vulkan spec UB). With a single shared buffer, the reset+begin
    // at the end of commit_frame would reset a buffer the GPU is still
    // executing — the root cause of the black-screen-with-sound issue.
    VkCommandBuffer  commandBuffers[kMaxFramesInFlight] = {};
    // Alias pointing to commandBuffers[currentFrame]. Updated by
    // ensure_command_buffer_recording() whenever the current slot changes or
    // the buffer is reset+begin. All existing code that references
    // b->commandBuffer continues to work unchanged.
    VkCommandBuffer  commandBuffer = VK_NULL_HANDLE;

    // Tracks whether commandBuffer (= commandBuffers[currentFrame]) is in the
    // RECORDING state. Set false after vkEndCommandBuffer (buffer is now
    // executable/pending). Set true by ensure_command_buffer_recording().
    // Code that records into b->commandBuffer outside of begin_render_pass
    // (e.g. stage_and_copy_image for texture uploads) MUST call
    // ensure_command_buffer_recording() first.
    bool             commandBufferRecording = false;

    VkPipelineCache  pipelineCache = VK_NULL_HANDLE;

    // Physical-device properties (cached on init for the GPU name string).
    VkPhysicalDeviceProperties props{};

    // vkCreateMetalSurfaceEXT function pointer (resolved from the instance).
    // Stored as PFN_vkVoidFunction (always declared by vulkan_core.h) so this
    // header does NOT need VK_USE_PLATFORM_METAL_EXT — that macro pulls in
    // <Metal/Metal.h> (Objective-C only) and would break every .cpp that
    // includes this header. SwapchainMetal.mm casts to PFN_vkCreateMetalSurfaceEXT
    // at the call site, where the metal platform define is active.
    PFN_vkVoidFunction createMetalSurfaceEXT = nullptr;

    // Per-frame sync: a fence per in-flight frame so we can wait on the GPU
    // before reusing the command buffer.
    VkFence frameFences[kMaxFramesInFlight] = {};
    int     currentFrame = 0;

    // Tracks whether a vkQueueSubmit has actually signaled each frame slot's
    // fence since the last wait on that slot. commit_frame() sets the flag
    // for the current slot after a successful submit; the NEXT commit_frame()
    // on the same slot waits on that fence (deferred async-pipeline wait,
    // mirroring MobileGL's FrameContext::WaitAndAcquireNextImage) and clears
    // the flag. Without this gate, commit_frame would wait on an
    // already-signaled or never-signaled fence. The fences are created with
    // VK_FENCE_CREATE_SIGNALED_BIT (Device.cpp), so the first frame's wait is
    // skipped — the flag starts false and is only set after a real submit.
    bool    fencePending[kMaxFramesInFlight] = {};

    // Monotonic frame generation counter, bumped once per commit_frame(). Used
    // by DescriptorSet.cpp to reset each program's descriptor pool exactly once
    // per frame (currentFrame cycles 0/1, so a program drawn only on every other
    // frame would never see a reset — the monotonic counter fixes that). The
    // value seen by every draw within a single frame is constant.
    uint64_t frameGeneration = 0;

    // 默认 16 字节 zero buffer，用于着色器声明但 GL 未 enable 的 vertex attribute
    // binding。Pipeline.cpp 的 get_or_create_pipeline 为这些 location 提供 dummy
    // attribute description 指向此 buffer，让 SPIRV-Cross 为每个 stage_in 字段生成
    // [[attribute(N)]] 限定符，避免 Metal 编译报错。
    VkBuffer         dummyVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory   dummyVertexMemory = VK_NULL_HANDLE;

    // 持久性 GPU 故障检测。vkQueueSubmit / vkQueuePresentKHR 致命失败时自增
    // consecutiveSubmitFailures，成功时清零。≥3 时置 deviceLost=true。
    // deviceLost 为真时 commit_frame / present / acquire / eglSwapBuffers 全部
    // 跳过实际 GPU 操作，避免日志死循环刷屏。
    bool             deviceLost = false;
    int              consecutiveSubmitFailures = 0;

    // FIX (P1): 内存分配计数器。MoltenVK 的 maxMemoryAllocationCount 默认很小
    // （通常 128-4096），每张纹理/buffer 独立 vkAllocateMemory 会耗尽配额。
    // 跟踪当前已分配数量，接近上限时触发警告和主动释放。
    uint32_t         maxMemoryAllocationCount = 0;  // 从 limits 读取
    uint32_t         currentAllocationCount = 0;    // 运行时计数

    // Deferred destruction queue: one bucket per frame slot. When a GL
    // buffer/texture/sampler is deleted or orphaned, its Vulkan handles are
    // pushed into disposalQueue[currentFrame] instead of being destroyed
    // immediately. The bucket is drained in ensure_command_buffer_recording()
    // after waiting on that slot's fence — at that point the GPU has finished
    // all work submitted to that slot, so the deferred resources are safe to
    // destroy. See DeferredDestroy above for the full rationale.
    std::vector<DeferredDestroy> disposalQueue[kMaxFramesInFlight];
};

// Access the singleton backend state. Allocated on first call.
Backend* backend();

// 返回 backend 是否进入持久性故障状态。一旦置位，渲染线程的 submit/present/
// acquire/swapchain-rebuild 全部跳过，避免死循环刷屏。
bool backend_is_device_lost();

// 重置 deviceLost 状态为 false，并清零 consecutiveSubmitFailures 计数器。
// 由 EGL 的 deviceLost 恢复路径调用：当 swapchain 重建成功后，给设备一次
// 恢复渲染的机会。GPU 超时通常是暂时的（资源压力释放后可恢复），不应永久
// 终止渲染（原实现一旦置位就永不恢复，导致进程永久卡死）。
void backend_reset_device_lost();

// FIX (黑屏根因): 在 deviceLost 期间（恢复尝试前）排空所有 disposalQueue，
// 释放被延迟销毁的 VkBuffer/VkImage/VkDeviceMemory。deviceLost 期间
// ensure_command_buffer_recording 早退，disposalQueue 不会被正常排空，
// 导致显存持续占用，swapchain 重建也因显存不足而失败。
// 此函数不重置 deviceLost 标志（仅释放资源），让 swapchain 重建有显存可用。
// 参考 MobileGL TryDrainFrameTransients（VulkanRenderer.cpp:7239-7313）。
void backend_reset_device_lost_pending_resources();

// One-time init of the instance/device/queue/command pool/pipeline cache.
// Idempotent; sets Backend::initialized on success.
bool init_device();

// Tear down everything created by init_device() (instance-level resources).
// Resource/pipeline/swapchain objects are owned by their respective modules.
void shutdown_device();

// Drain the disposal queue for `slot`: actually call vkDestroy* / vkFreeMemory
// for every deferred entry. MUST only be called after the GPU has finished all
// work submitted to that slot (i.e. after vkWaitForFences on frameFences[slot]
// or vkDeviceWaitIdle). Called by ensure_command_buffer_recording() after the
// per-slot fence wait, and by drain_all_disposal_queues() after vkDeviceWaitIdle.
void drain_disposal_queue(int slot);

// Drain ALL disposal queue buckets. Called after vkDeviceWaitIdle (which
// guarantees all GPU work is complete) and during shutdown_device().
void drain_all_disposal_queues();

} // namespace vk
} // namespace mithril

#endif // MITHRIL_DIRECTVULKAN_DEVICE_H
