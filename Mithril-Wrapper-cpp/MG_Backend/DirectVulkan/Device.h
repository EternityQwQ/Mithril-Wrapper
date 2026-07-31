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
// while MoltenVK's command encoding still held references to the corresponding
// MTLBuffer wrappers. With deferred destruction, the VkBuffer (and its MTLBuffer)
// survives until the GPU finishes all in-flight command buffers that reference it.
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

    // GPU 故障状态。只有 VK_ERROR_DEVICE_LOST 才设置 deviceLost；
    // OOM 和其他错误不再设置 deviceLost（改为触发 OOM GC 后跳过当前帧）。
    // deviceLost 为真时 commit_frame / present / acquire / eglSwapBuffers 全部
    // 跳过实际 GPU 操作，由 EGL 恢复路径（10 帧间隔重建 swapchain）清除。
    bool             deviceLost = false;
    int              consecutiveSubmitFailures = 0;  // 统计用，不再触发 deviceLost

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

    // FIX (Invalid Resource 根因 - per-frame transient staging arena):
    // 深度参考 MobileGL 的 transient staging arena 模式：每个 frame slot 持有
    // 一个大的 host-visible VkBuffer，纹理上传时从中 sub-allocate（bump offset）。
    // 在 ensure_command_buffer_recording 的 fence wait 之后 rewind offset 到 0。
    //
    // 这消除了 per-texture staging buffer 的创建/销毁循环：
    //   - 不再为每次 stage_and_copy_image 调用 vkCreateBuffer + vkAllocateMemory
    //   - 不再将 staging buffer 推入 disposalQueue
    //   - 不再因 disposal queue 生命周期管理不当导致 Metal Invalid Resource (code 9)
    //
    // arena 在 init_device 中创建（一次），在 shutdown_device 中销毁。
    // persistently mapped（vkMapMemory 一次，保持映射）。
    // overflow（单次上传超过剩余空间）回退到临时 staging buffer + deferred destroy。
    static constexpr VkDeviceSize kFrameStagingSize = 16 * 1024 * 1024;  // 16 MB per slot
    VkBuffer       frameStagingBuffer[kMaxFramesInFlight] = {};
    VkDeviceMemory frameStagingMemory[kMaxFramesInFlight] = {};
    VkDeviceSize   frameStagingOffset[kMaxFramesInFlight] = {};
    void*          frameStagingMapped[kMaxFramesInFlight] = {};
    bool           frameStagingReady = false;

    // ---- Per-frame UBO arena (eliminates cross-frame host-write/GPU-read race) ----
    // Each frame slot has its own 1MB host-visible VkBuffer with UNIFORM_BUFFER
    // usage, persistently mapped. UBO data is sub-allocated (bump offset) from the
    // arena. The offset is rewound to 0 after the fence wait in
    // ensure_command_buffer_recording() — guaranteeing the slot's GPU work is
    // complete, so overwriting is safe. This mirrors the per-frame staging arena
    // design and follows MobileGL's UniformManager per-frame ring buffer pattern.
    static constexpr VkDeviceSize kFrameUboSize = 1 * 1024 * 1024;  // 1 MB per slot
    VkBuffer       frameUboBuffer[kMaxFramesInFlight] = {};
    VkDeviceMemory frameUboMemory[kMaxFramesInFlight] = {};
    VkDeviceSize   frameUboOffset[kMaxFramesInFlight] = {};
    void*          frameUboMapped[kMaxFramesInFlight] = {};
    bool           frameUboReady = false;
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
//
// INVARIANT (MVKImageView UAF - 销毁顺序): 任何调用方在调用本函数前，必须先
// 调用 reset_descriptor_caches_for_slot(slot)（或对全部 slot 调
// reset_all_descriptor_caches / clear_all_pipeline_caches）销毁并重建该 slot
// 的描述符池，以触发 MoltenVK 析构链 (~MVKDescriptorPool → ... → release())
// 释放所有 retained MVKImageView。否则 drain 销毁的 VkImageView 仍被
// allocatedSets[slot] 中的陈旧 set 引用，bind_program_descriptors 复用该 set
// 时 vkUpdateDescriptorSets 会解引用已释放的 MVKImageView → SIGSEGV
// (si_addr=0x108)。参考实现：ensure_command_buffer_recording()
// (CommandStream.cpp) 在 fence wait 后先 reset_descriptor_caches_for_slot
// 再 drain_disposal_queue。
void drain_disposal_queue(int slot);

// Drain ALL disposal queue buckets. Called after vkDeviceWaitIdle (which
// guarantees all GPU work is complete) and during shutdown_device().
//
// INVARIANT (MVKImageView UAF - 销毁顺序): 任何调用方在调用本函数前，必须先
// 调用 reset_all_descriptor_caches() 或 clear_all_pipeline_caches() 销毁并
// 重建所有 slot 的描述符池（触发 MoltenVK 析构链释放 retained MVKImageView）。
// 已知例外（spec.md Out of scope）：shutdown_device / delete_program_resources
// （描述符池随 program 整体销毁，allocatedSets 不会被复用）、OOM GC 路径
// （safe_device_wait_idle 先提交+编码当前命令缓冲区，drain 时 MTLTexture 已被
// 编码器独立 retain）。参考实现：ensure_command_buffer_recording()
// (CommandStream.cpp:361)。
void drain_all_disposal_queues();

// FIX (SIGBUS 根因 - vkDeviceWaitIdle 触发 deferred encoding):
// MoltenVK 的 vkDeviceWaitIdle 会等待所有 pending command buffer 被编码完成。
// 如果当前有正在录制的 command buffer（commandBufferRecording=true），其中
// 包含 vkCmdCopyBufferToImage 等命令，vkDeviceWaitIdle 会触发 MoltenVK 对
// 未完成 command buffer 的 deferred encoding → MVKCmdBufferImageCopy::encode
// 访问命令池中未对齐的内存 → SIGBUS (BUS_ADRALN)。
//
// safe_device_wait_idle() 在调用 vkDeviceWaitIdle 前，先检查并安全结束当前
// 录制的 command buffer（vkEndCommandBuffer + 提交到对应 slot 的 fence），
// 确保没有未完成的 command buffer 时才调用 vkDeviceWaitIdle。
//
// 所有需要在 GC/drain 路径中调用 vkDeviceWaitIdle 的地方都应使用此函数。
void safe_device_wait_idle();

// FIX (显存耗尽根因 - 主动式 GC，深度参考 MobileGL):
//
// MobileGL 在每帧 Present 末尾、FlushPendingCommands、WaitForFrameSerial 等
// 多个点调用 TryDrainFrameTransients（VulkanRenderer.cpp:7239-7313），其中：
//   1. RefreshCompletedSubmits() 用 vkGetFenceStatus 非阻塞轮询所有 in-flight
//      submit 的 fence，回收已完成的 pooled fence
//   2. CollectAllDeferredReleases() 释放所有已完成帧的延迟资源
//   3. 每 8 次 drain rewind transient arena + age cache
//
// 我们的 disposalQueue 是 per-frame-slot 的，只在 ensure_command_buffer_recording
// 复用某个 slot 时才 drain 该 slot。问题：kMaxFramesInFlight=2 时，slot 0 的
// GPU 工作可能在 slot 1 录制期间就完成了，但 slot 0 的 disposalQueue 要等到
// 下次循环回 slot 0 才 drain。这导致显存占用峰值偏高（2 帧 staging buffer 累积），
// 在 MC 纹理批量加载时容易触发 OOM。
//
// 本函数用 vkGetFenceStatus 非阻塞轮询所有 slot 的 fence，对已 signal 的 slot
// 立即 drain 其 disposalQueue。不会阻塞渲染线程。由 eglSwapBuffers 在每帧
// 开头调用，在新帧分配资源前释放已完成帧的资源。
//
// 返回 drain 的 slot 数（仅用于诊断日志）。
int backend_poll_completed_frames();

// FIX (显存耗尽根因 - 内存压力主动 GC):
//
// 在 vkAllocateMemory 失败之前就主动触发 GC。当 currentAllocationCount
// 达到 maxMemoryAllocationCount 的 70% 时，调用 vkDeviceWaitIdle +
// drain_all_disposal_queues，释放所有延迟销毁的资源。
//
// 这比 try_allocate_memory_with_gc 的失败后重试更有效：在 GPU 进入降级状态
// （timeout/device lost）之前就释放显存，避免级联失败。
//
// 参考 MobileGL 的策略：MobileGL 不会 OOM，因为它每帧主动 drain，但我们
// 的 per-slot drain 不够及时，所以需要这个压力触发的主动 GC。
//
// 返回 true 表示触发了 GC（调用方可记录日志）。
bool backend_proactive_gc_if_needed();

} // namespace vk
} // namespace mithril

#endif // MITHRIL_DIRECTVULKAN_DEVICE_H
