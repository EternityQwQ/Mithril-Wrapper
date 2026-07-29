// Mithril-Wrapper - MG_Backend/DirectVulkan/Device.cpp
// Vulkan 1.2 instance / physical-device / device / queue / command pool init.
// MoltenVK is statically linked, so vkCreateInstance etc. resolve at link time
// (no loader, no VK_ICD_FILENAMES).

// VK_EXT_metal_surface extension-name macro. The canonical definition lives in
// vulkan_metal.h, which vulkan.h only pulls in when VK_USE_PLATFORM_METAL_EXT
// is defined BEFORE the include. We intentionally do NOT define that macro in
// this .cpp (it drags in <Metal/Metal.h>, Objective-C only — would break the
// plain-C++ compile). SwapchainMetal.mm is the one TU that defines it. Here we
// supply the spec-mandated string literal directly so Device.cpp can request
// the instance extension by name. The value is fixed by the Vulkan spec and
// will never diverge; guarding with #ifndef keeps this a no-op if a future
// Vulkan header exposes the macro without the platform define.
#ifndef VK_EXT_METAL_SURFACE_EXTENSION_NAME
#define VK_EXT_METAL_SURFACE_EXTENSION_NAME "VK_EXT_metal_surface"
#endif

#include "Device.h"
#include "Resources.h"
#include "CommandStream.h"  // end_render_pass, ensure_command_buffer_recording, render_pass_active
#include "Pipeline.h"  // clear_all_pipeline_caches() for deviceLost recovery
#include "../../MG_Impl/Log.h"

#include <cstring>
#include <cstdlib>
#include <vector>

namespace mithril {
namespace vk {

Backend* backend() {
    static Backend b;
    return &b;
}

// FIX (SIGBUS 根因 - vkDeviceWaitIdle 触发 deferred encoding):
// 见 Device.h 中的详细注释。此函数在 vkDeviceWaitIdle 前安全结束当前录制
// 的 command buffer，避免 MoltenVK 对未完成 command buffer 做 deferred
// encoding 时访问未对齐的命令池内存。
//
// 透明语义：如果调用时 command buffer 正在录制，函数会：
//   1. 结束 render pass（如有）— vkEndCommandBuffer 在 render pass 内会失败
//   2. vkEndCommandBuffer + vkQueueSubmit 提交当前录制的命令
//   3. vkDeviceWaitIdle 等待 GPU 完成
//   4. 清除所有 fencePending（wait 后所有 fence 已 signaled）
//   5. 重新 begin command buffer，让调用方可以继续录制（透明）
// 如果调用时 command buffer 未在录制，仅执行 vkDeviceWaitIdle（无状态变化）。
void safe_device_wait_idle() {
    Backend* b = backend();
    if (!b->initialized || !b->device) return;

    const bool wasRecording = b->commandBufferRecording && b->commandBuffer != VK_NULL_HANDLE;

    // 1. 如果 render pass 处于活动状态，先结束它。
    //    vkEndCommandBuffer 在 render pass 实例内调用会返回 VK_ERROR_UNKNOWN。
    //    纹理上传（stage_and_copy_image）通常在 render pass 外调用，
    //    但 draw 路径触发的分配可能在 render pass 内，需要安全处理。
    if (wasRecording && render_pass_active()) {
        end_render_pass();
        static int rpWarnCount = 0;
        rpWarnCount++;
        if (rpWarnCount <= 3) {
            MITHRIL_LOG_WARN("vk", "safe_device_wait_idle: ended active render "
                              "pass before vkDeviceWaitIdle (occurrence #%d) — "
                              "render pass will be re-established by next "
                              "begin_render_pass call", rpWarnCount);
        }
    }

    // 2. 结束并提交当前录制的 command buffer。
    //    vkEndCommandBuffer 让 MoltenVK 在安全上下文中完成 command buffer 的
    //    编码，然后 vkQueueSubmit 提交到 GPU。这样 vkDeviceWaitIdle 就不会
    //    触发对未完成 command buffer 的 deferred encoding。
    if (wasRecording) {
        VkResult endRc = vkEndCommandBuffer(b->commandBuffer);
        b->commandBufferRecording = false;
        if (endRc == VK_SUCCESS) {
            // 提交到当前 slot 的 fence，让 GPU 执行这个 command buffer。
            // 不等待 renderFinished semaphore（这是 GC 路径，不是正常帧提交）。
            VkSubmitInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &b->commandBuffer;
            VkFence fence = b->frameFences[b->currentFrame];
            // FIX: fence 可能在 signaled 状态（ensure_command_buffer_recording
            // 的 vkWaitForFences 不 reset fence，或上一次 safe_device_wait_idle
            // 的 vkDeviceWaitIdle 已 signal 它）。Vulkan spec 要求 vkQueueSubmit
            // 的 fence 必须是 unsignaled，否则 UB。commit_frame 也这样做。
            vkResetFences(b->device, 1, &fence);
            VkResult submitRc = vkQueueSubmit(b->graphicsQueue, 1, &si, fence);
            if (submitRc == VK_SUCCESS) {
                b->fencePending[b->currentFrame] = true;
            }
            // 提交失败（OOM/deviceLost）时不设置 fencePending，后续的
            // vkDeviceWaitIdle 仍会等待其他 pending 工作。
        }
        // vkEndCommandBuffer 失败时（设备已挂起），command buffer 状态未定义，
        // 但 vkDeviceWaitIdle 仍可安全调用（它会等待其他 pending 工作）。
    }

    // 3. 等待 GPU 完成。
    VkResult waitRc = vkDeviceWaitIdle(b->device);
    (void)waitRc;

    // 4. vkDeviceWaitIdle 后所有 fence 已 signaled。清除 fencePending，
    //    让后续 ensure_command_buffer_recording 跳过无意义的 vkWaitForFences。
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        b->fencePending[i] = false;
    }

    // 5. 如果之前在录制，重新 begin command buffer，让调用方可以透明地
    //    继续录制。ensure_command_buffer_recording 看到
    //    commandBufferRecording==false，跳过 fence wait（fencePending 已清除），
    //    reset + begin 当前 slot 的 buffer。
    if (wasRecording && !b->deviceLost) {
        ensure_command_buffer_recording();
    }
}

bool backend_is_device_lost() {
    return backend()->deviceLost;
}

void backend_reset_device_lost() {
    Backend* b = backend();
    // FIX (OOM 反馈循环): deviceLost 期间 disposalQueue 不会被排空
    // （ensure_command_buffer_recording 在 deviceLost 时早退），导致延迟
    // 释放的资源无限累积，显存占用不降反升。重置 deviceLost 时主动排空
    // 所有 slot 的 disposalQueue，释放被延迟销毁的 VkBuffer/VkImage/
    // VkDeviceMemory，给设备恢复腾出显存空间。
    if (b->device) {
        vkDeviceWaitIdle(b->device);
    }
    drain_all_disposal_queues();
    // FIX (红屏根因 - deviceLost 恢复后清除着色器负缓存):
    // deviceLost 期间 vkCreateGraphicsPipelines 可能因设备状态异常而失败，
    // 这些失败被缓存在 failedSignatures 中。即使设备恢复后着色器可以正常
    // 编译，draw 也会被永久跳过，导致物体消失/红屏。
    // 同时销毁已创建的 VkPipeline（可能引用了损坏的 MoltenVK 着色器缓存），
    // 让 get_or_create_pipeline 在下次 draw 时从干净状态重新创建。
    // 参考 MobileGL RecreateSwapchain（VulkanRenderer.cpp:8579）：
    // pipelineFactory->DestroyAll() 在 swapchain 重建时销毁全部 pipeline。
    clear_all_pipeline_caches();
    b->deviceLost = false;
    b->consecutiveSubmitFailures = 0;
}

void backend_reset_device_lost_pending_resources() {
    Backend* b = backend();
    if (!b->device) return;
    // vkDeviceWaitIdle 在 deviceLost 时可能返回 VK_ERROR_DEVICE_LOST，
    // 但仍会尝试等待已提交的 command buffer 完成或超时。之后 drain
    // 是安全的：即使 GPU 挂起，被引用的 Metal 资源也不会再被访问。
    VkResult waitResult = vkDeviceWaitIdle(b->device);
    if (waitResult != VK_SUCCESS) {
        // 设备挂起时 vkDeviceWaitIdle 失败是正常的，仍继续 drain
        // （资源已被 command buffer 引用过，GPU 挂起不会再次访问它们）
        static int drainFailLogCount = 0;
        drainFailLogCount++;
        if (drainFailLogCount <= 3) {
            MITHRIL_LOG_WARN("vk", "backend_reset_device_lost_pending_resources: "
                              "vkDeviceWaitIdle returned %d (expected during "
                              "deviceLost), draining disposal queues anyway",
                              (int)waitResult);
        }
    }
    drain_all_disposal_queues();
}

void drain_disposal_queue(int slot) {
    Backend* b = backend();
    if (!b->device || slot < 0 || slot >= kMaxFramesInFlight) return;
    auto& q = b->disposalQueue[slot];
    if (q.empty()) return;
    for (auto& d : q) {
        if (d.buffer)  vkDestroyBuffer(b->device, d.buffer, nullptr);
        if (d.image)   vkDestroyImage(b->device, d.image, nullptr);
        if (d.view)    vkDestroyImageView(b->device, d.view, nullptr);
        if (d.memory)  {
            vkFreeMemory(b->device, d.memory, nullptr);
            // FIX (P1): 递减分配计数器
            if (b->currentAllocationCount > 0) b->currentAllocationCount--;
        }
        if (d.sampler) vkDestroySampler(b->device, d.sampler, nullptr);
    }
    q.clear();
}

void drain_all_disposal_queues() {
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        drain_disposal_queue(i);
    }
}

// FIX (显存耗尽根因 - 主动式 GC，深度参考 MobileGL):
// 实现 backend_poll_completed_frames：非阻塞轮询所有帧槽位的 fence，
// 对已完成的 slot 立即 drain 其 disposalQueue。
//
// MobileGL 的 RefreshCompletedSubmits（VulkanRenderer.cpp:7199-7237）用
// vkGetFenceStatus 做 prefix-only scan，回收已完成的 pooled fence。
// 我们做类似的事：遍历所有 slot，对 fencePending[s]==true 的 slot 调用
// vkGetFenceStatus，若 VK_SUCCESS 则 drain 并清除 fencePending 标志。
//
// 关键：这是非阻塞的。vkGetFenceStatus 立即返回，不会 stall 渲染线程。
// 只有当 GPU 真正完成该 slot 的工作时才 drain，避免在 GPU 还在执行时
// 释放被引用的资源。
//
// 注意：drain 后 fencePending[s] 被清除，但 ensure_command_buffer_recording
// 复用该 slot 时仍会检查 fencePending——为 false 时跳过 vkWaitForFences，
// 这是正确的（fence 已 signaled，无需等待）。
int backend_poll_completed_frames() {
    Backend* b = backend();
    if (!b->initialized || !b->device) return 0;

    int drainedSlots = 0;
    for (int s = 0; s < kMaxFramesInFlight; ++s) {
        if (!b->fencePending[s]) continue;  // 无 pending submit，跳过
        if (b->disposalQueue[s].empty()) {
            // 队列为空：但 fence 可能仍 pending。检查 fence 状态以清除
            // fencePending 标志（让后续 ensure_command_buffer_recording 跳过等待）。
            // 不计入 drainedSlots（没有实际释放资源）。
            VkResult fr = vkGetFenceStatus(b->device, b->frameFences[s]);
            if (fr == VK_SUCCESS) {
                b->fencePending[s] = false;
            }
            continue;
        }
        // 有 pending submit 且 disposalQueue 非空：检查 fence
        VkResult fr = vkGetFenceStatus(b->device, b->frameFences[s]);
        if (fr == VK_SUCCESS) {
            // GPU 已完成该 slot 的所有工作，安全 drain
            drain_disposal_queue(s);
            b->fencePending[s] = false;
            drainedSlots++;
        }
        // VK_NOT_READY：GPU 还在执行，不 drain（避免 UAF）
        // 其他错误码（如 VK_ERROR_DEVICE_LOST）：不 drain，交给 deviceLost 恢复路径
    }

    if (drainedSlots > 0) {
        // 限流日志：仅在释放了大量资源时打印
        static int pollDrainLogCount = 0;
        pollDrainLogCount++;
        if (pollDrainLogCount <= 5 || pollDrainLogCount % 200 == 0) {
            MITHRIL_LOG_INFO("vk", "proactive poll: drained %d completed frame "
                              "slot(s) (occurrence #%d) — freed deferred "
                              "resources before new allocations",
                              drainedSlots, pollDrainLogCount);
        }
    }
    return drainedSlots;
}

// FIX (显存耗尽根因 - 内存压力主动 GC):
// 当 allocationCount 接近上限时，主动触发 GC。
// 阈值：70% of maxMemoryAllocationCount。在达到硬限制之前释放资源。
bool backend_proactive_gc_if_needed() {
    Backend* b = backend();
    if (!b->initialized || !b->device) return false;
    if (b->maxMemoryAllocationCount == 0) return false;  // 未知限制，不触发

    // 70% 阈值：在硬限制之前主动释放
    uint32_t threshold = (b->maxMemoryAllocationCount * 7) / 10;
    if (b->currentAllocationCount < threshold) return false;

    // 检查是否有可释放的资源（任一 slot 的 disposalQueue 非空）
    bool hasDeferred = false;
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        if (!b->disposalQueue[i].empty()) { hasDeferred = true; break; }
    }
    if (!hasDeferred) return false;  // 没有可释放的，GC 无意义

    static int proactiveGcCount = 0;
    proactiveGcCount++;
    // 限流日志：首次 + 每 20 次
    if (proactiveGcCount <= 3 || proactiveGcCount % 20 == 0) {
        MITHRIL_LOG_WARN("vk", "proactive GC triggered: allocationCount "
                          "%u/%u (>=70%% threshold %u) — draining before "
                          "OOM (attempt #%d)",
                          (unsigned)b->currentAllocationCount,
                          (unsigned)b->maxMemoryAllocationCount,
                          (unsigned)threshold, proactiveGcCount);
    }

    // 先非阻塞 poll（可能已经完成，无需 vkDeviceWaitIdle 阻塞）
    backend_poll_completed_frames();

    // 如果 poll 后仍超阈值，且仍有 deferred 资源，才阻塞等待
    bool stillHasDeferred = false;
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        if (!b->disposalQueue[i].empty()) { stillHasDeferred = true; break; }
    }
    if (stillHasDeferred && b->currentAllocationCount >= threshold) {
        // FIX (SIGBUS): 使用 safe_device_wait_idle 而非直接 vkDeviceWaitIdle。
        // proactive GC 可能在 stage_and_copy_image 录制期间触发（create_buffer →
        // try_allocate_memory_with_gc → 此函数）。此时 command buffer 正在录制，
        // 直接 vkDeviceWaitIdle 会触发 MoltenVK 的 deferred encoding →
        // MVKCmdBufferImageCopy::encode 访问未对齐内存 → SIGBUS。
        // safe_device_wait_idle 先结束+提交当前 command buffer，wait 后重新 begin，
        // 让 stage_and_copy_image 可以透明地继续录制。
        safe_device_wait_idle();
        drain_all_disposal_queues();
        // safe_device_wait_idle 已清除 fencePending，但重复清除无害（防御性）
        for (int i = 0; i < kMaxFramesInFlight; ++i) {
            b->fencePending[i] = false;
        }
        if (proactiveGcCount <= 3 || proactiveGcCount % 20 == 0) {
            MITHRIL_LOG_WARN("vk", "proactive GC: safe_device_wait_idle + "
                              "drain_all completed, allocationCount now %u/%u",
                              (unsigned)b->currentAllocationCount,
                              (unsigned)b->maxMemoryAllocationCount);
        }
    }
    return true;
}

namespace {

bool has_extension(const std::vector<VkExtensionProperties>& props, const char* name) {
    for (const auto& p : props) {
        if (std::strcmp(p.extensionName, name) == 0) return true;
    }
    return false;
}

bool has_layer(const std::vector<VkLayerProperties>& props, const char* name) {
    for (const auto& p : props) {
        if (std::strcmp(p.layerName, name) == 0) return true;
    }
    return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT,
                                              VkDebugUtilsMessageTypeFlagsEXT,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data,
                                              void*) {
    if (data && data->pMessage) {
        // FIX (日志刷屏根因): MoltenVK 在 OOM/deviceLost 时会通过 debug callback
        // 每帧输出大量重复的 MTLCommandBuffer/VK_ERROR 错误（日志中观察到的
        // ~3000 行循环就是来自这里）。对相同消息做去重限流：
        // - 首次：完整输出
        // - 后续相同消息：每 500 次输出一次（含计数）
        // - 不同消息：各自独立计数
        //
        // 使用简单的消息哈希（前 128 字节）做去重 key，避免存储完整消息。
        static thread_local struct {
            uint64_t hash;      // 上次消息的哈希
            int count;          // 相同消息的累计次数
        } dedup = { 0, 0 };

        // 简单 FNV-1a 哈希，只取前 128 字节（足以区分 MoltenVK 的不同错误类型）
        const char* msg = data->pMessage;
        uint64_t h = 1469598103934665603ULL;
        for (int i = 0; i < 128 && msg[i]; ++i) {
            h ^= (uint8_t)msg[i];
            h *= 1099511628211ULL;
        }

        if (h == dedup.hash) {
            dedup.count++;
            // 相同消息：首次后每 500 次才输出一次
            if (dedup.count % 500 == 0) {
                MITHRIL_LOG_WARN("vk", "%s (repeated %d times, suppressed)",
                                  msg, dedup.count);
            }
        } else {
            // 新消息：输出并重置计数
            dedup.hash = h;
            dedup.count = 1;
            MITHRIL_LOG_WARN("vk", "%s", msg);
        }
    }
    return VK_FALSE;
}

} // namespace

bool init_device() {
    Backend* b = backend();
    if (b->initialized) return true;

    // ---- MoltenVK runtime configuration (root cause T) ----
    // Set critical MoltenVK environment variables BEFORE vkCreateInstance.
    // MoltenVK reads these once during instance creation; setting them after
    // has no effect. Amethyst-iOS sets these explicitly for stability.
    //
    // MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=1: vkQueueSubmit blocks until
    //   encoding is complete. Prevents submit races on iOS. (MoltenVK default
    //   is 1, but we set it explicitly for determinism.)
    //
    // MVK_CONFIG_PREFILL_METAL_COMMAND_BUFFERS=0 (SIGBUS 对齐崩溃根因修复):
    //   MoltenVK prefill 模式：
    //     0 = 提交时编码（vkQueueSubmit 时创建 Metal command buffer）— 默认
    //     1 = vkEndCommandBuffer() 时编码（deferred encoding）
    //     2 = 每条命令立即编码
    //
    //   原设为 1 是为了"避免 present 时 IOSurface 未绑定竞态"，但这是个
    //   误解：prefill=1 实际在 vkEndCommandBuffer() 时触发 deferred encoding，
    //   而 MoltenVK 命令池中的 MVKCmdBufferImageCopy 对象分配可能未满足
    //   8 字节对齐（ARMv8 ldr x9,[x9,#0xE0] 要求 8 字节对齐），导致 SIGBUS
    //   (BUS_ADRALN) 崩溃。崩溃栈：
    //     MVKCmdBufferImageCopy::encode → checkDeferredEncoding →
    //     MVKCommandBuffer::end() → vkEndCommandBuffer → commit_frame
    //
    //   改为 0（提交时编码）规避了 vkEndCommandBuffer 时的 deferred
    //   encoding 路径，从而避免命令池对齐崩溃。IOSurface 绑定竞态已通过
    //   commit_frame 中的 dummy render pass（CommandStream.cpp:706-768）
    //   和 imageAvailable semaphore 等待解决，不再依赖 prefill。
    //
    // MVK_CONFIG_RESUME_LOST_DEVICE=1: Automatically attempt to recover from
    //   VK_ERROR_DEVICE_LOST by re-creating the VkDevice. Without this, a
    //   single GPU error permanently kills rendering (black screen forever).
    //
    // MVK_CONFIG_SHADER_CONVERSION_FLIP_VERTEX_Y=0: MoltenVK's global vertex Y
    //   flip is DISABLED. Y flipping is now handled at the shader-translation
    //   layer (Shader.cpp inject_position_fixup with flip_y=true) ONLY for the
    //   default framebuffer (FBO 0). User-created FBOs use the non-flipped
    //   variant so their textures stay in GL Y-up orientation for correct
    //   sampling by GL shaders. The global MoltenVK flip applied Y inversion to
    //   ALL vertex shaders indiscriminately, which turned user-FBO texture
    //   content upside-down and was the root cause of the red/black screen.
    //   Deep reference: MobileGL GetShaderTransformFlags applies PositionYFlip
    //   only when currentDrawFBO->IsDefaultFramebuffer(), never globally.
    //
    // MVK_CONFIG_SUBMIT_COMMAND_BUFFERS_PER_QUEUE=2 (深度参考 MobileGL 缺口):
    //   限制每个 queue 同时未完成的 command buffer 数量。MoltenVK 默认
    //   是 64（即允许 64 个 command buffer 并发编码），每个 command buffer
    //   都会预分配 Metal 资源（编码器、IOSurface 引用等）。在 iPhone SE 3
    //   这种显存受限的设备上，64 个并发 command buffer 的 Metal 资源占用
    //   会加剧显存压力，导致 OOM。
    //   设为 kMaxFramesInFlight(2) 后，MoltenVK 最多维护 2 个活跃 command
    //   buffer（与我们的 per-slot 设计匹配），从默认 64 降到 2，显著降低
    //   峰值显存占用。
    //   注意：这个值必须 >= kMaxFramesInFlight（2），否则 submit 会被
    //   MoltenVK 阻塞等待前一个 command buffer 完成，可能导致死锁
    //   （若前一个的 fence 还没被 ensure_command_buffer_recording 等待）。
    setenv("MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS", "1", 1);
    setenv("MVK_CONFIG_PREFILL_METAL_COMMAND_BUFFERS", "0", 1);
    setenv("MVK_CONFIG_RESUME_LOST_DEVICE", "1", 1);
    setenv("MVK_CONFIG_SHADER_CONVERSION_FLIP_VERTEX_Y", "0", 1);
    setenv("MVK_CONFIG_SUBMIT_COMMAND_BUFFERS_PER_QUEUE", "2", 1);

    // ---- Instance ----
    std::vector<VkExtensionProperties> instExtProps;
    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    instExtProps.resize(extCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, instExtProps.data());

    std::vector<const char*> instExts;
    // Portability enumeration is mandatory for MoltenVK-backed Vulkan.
    if (has_extension(instExtProps, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        instExts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    }
    if (has_extension(instExtProps, VK_EXT_METAL_SURFACE_EXTENSION_NAME)) {
        instExts.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
    }
    if (has_extension(instExtProps, VK_KHR_SURFACE_EXTENSION_NAME)) {
        instExts.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    }
    // Debug utils optional.
    bool wantDebugUtils = has_extension(instExtProps, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (wantDebugUtils) instExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Mithril-Wrapper";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Mithril-Wrapper";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo instCI{};
    instCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instCI.pApplicationInfo = &appInfo;
    instCI.enabledExtensionCount = (uint32_t)instExts.size();
    instCI.ppEnabledExtensionNames = instExts.data();
    // VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR is REQUIRED so
    // vkEnumeratePhysicalDevices returns the MoltenVK ICD.
    instCI.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

    if (vkCreateInstance(&instCI, nullptr, &b->instance) != VK_SUCCESS) {
        MITHRIL_LOG_ERROR("vk", "vkCreateInstance failed");
        return false;
    }

    if (wantDebugUtils) {
        // Best-effort debug messenger (optional, never fatal).
        auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(b->instance, "vkCreateDebugUtilsMessengerEXT");
        if (fn) {
            VkDebugUtilsMessengerCreateInfoEXT mic{};
            mic.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            mic.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            mic.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            mic.pfnUserCallback = debug_callback;
            VkDebugUtilsMessengerEXT messenger;
            fn(b->instance, &mic, nullptr, &messenger);
            // Messenger intentionally leaked (process lifetime); the callback
            // stays armed for the whole session.
        }
    }

    // Resolve vkCreateMetalSurfaceEXT (used by SwapchainMetal.mm). Stored as
    // PFN_vkVoidFunction to avoid needing VK_USE_PLATFORM_METAL_EXT here
    // (that macro pulls in <Metal/Metal.h>, which is ObjC-only). The .mm
    // translation unit casts it to PFN_vkCreateMetalSurfaceEXT at the call.
    b->createMetalSurfaceEXT =
        vkGetInstanceProcAddr(b->instance, "vkCreateMetalSurfaceEXT");
    if (!b->createMetalSurfaceEXT) {
        // Diagnostic: this function is only exported when VK_EXT_metal_surface
        // is enabled at instance creation. Dump the enumerated instance
        // extensions so a null resolution is self-explaining on-device.
        MITHRIL_LOG_WARN("vk", "vkCreateMetalSurfaceEXT not resolved; "
                              "enumerated instance extensions:");
        for (const auto& e : instExtProps) {
            MITHRIL_LOG_WARN("vk", "  %s", e.extensionName);
        }
    }

    // ---- Physical device ----
    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(b->instance, &gpuCount, nullptr);
    if (gpuCount == 0) {
        MITHRIL_LOG_ERROR("vk", "No Vulkan physical devices (MoltenVK not linked?)");
        return false;
    }
    std::vector<VkPhysicalDevice> gpus(gpuCount);
    vkEnumeratePhysicalDevices(b->instance, &gpuCount, gpus.data());
    b->physicalDevice = gpus[0];  // iOS has exactly one Metal device.

    vkGetPhysicalDeviceProperties(b->physicalDevice, &b->props);
    MITHRIL_LOG_INFO("vk", "Physical device: %s (api 0x%x, driver 0x%x)",
                     b->props.deviceName, b->props.apiVersion, b->props.driverVersion);

    // FIX (P1): 查询 maxMemoryAllocationCount。注意 MoltenVK 报告的值
    // 不可信（可能返回 1073741824 = 2^30），实际限制是物理显存。
    // 真正的 OOM 是物理显存耗尽，不是 allocation count。
    // 这里仅记录用于诊断，不做阈值警告（阈值检查改为基于 allocation 数量
    // 的合理估计）。
    b->maxMemoryAllocationCount = b->props.limits.maxMemoryAllocationCount;
    // MoltenVK 的 maxMemoryAllocationCount 不可信时，用一个保守的估计值
    // 替代（MoltenVK 实际限制取决于 IOSurface 池大小，通常 < 4096）
    if (b->maxMemoryAllocationCount > 100000) {
        MITHRIL_LOG_WARN("vk", "maxMemoryAllocationCount = %u (MoltenVK reports "
                          "unrealistic value, clamping to 4096 for safety)",
                          (unsigned)b->maxMemoryAllocationCount);
        b->maxMemoryAllocationCount = 4096;
    }
    MITHRIL_LOG_INFO("vk", "maxMemoryAllocationCount = %u (effective)",
                     (unsigned)b->maxMemoryAllocationCount);

    // ---- Queue family ----
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(b->physicalDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfProps(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(b->physicalDevice, &qfCount, qfProps.data());
    b->graphicsFamily = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < qfCount; ++i) {
        if (qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            b->graphicsFamily = i;
            break;
        }
    }
    if (b->graphicsFamily == 0xFFFFFFFFu) {
        MITHRIL_LOG_ERROR("vk", "No graphics queue family");
        return false;
    }

    // ---- Device ----
    // Verify the portability-subset + swapchain extensions are available.
    std::vector<VkExtensionProperties> devExtProps;
    uint32_t devExtCount = 0;
    vkEnumerateDeviceExtensionProperties(b->physicalDevice, nullptr, &devExtCount, nullptr);
    devExtProps.resize(devExtCount);
    vkEnumerateDeviceExtensionProperties(b->physicalDevice, nullptr, &devExtCount, devExtProps.data());

    std::vector<const char*> devExts;
    if (has_extension(devExtProps, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        devExts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    // VK_KHR_portability_subset MUST be enabled if present (MoltenVK always
    // advertises it).
    if (has_extension(devExtProps, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
        devExts.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
    }
    // VK_KHR_dynamic_rendering: lets us create pipelines + record render passes
    // without a VkRenderPass object (simpler than managing compat render passes).
    if (has_extension(devExtProps, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
        devExts.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    }
    // VK_EXT_extended_dynamic_state: vkCmdSetCullMode/FrontFace/DepthTestEnable/
    // DepthWriteEnable/DepthCompareOp etc. without rebuilding pipelines.
    if (has_extension(devExtProps, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME)) {
        devExts.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCI{};
    queueCI.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCI.queueFamilyIndex = b->graphicsFamily;
    queueCI.queueCount = 1;
    queueCI.pQueuePriorities = &queuePriority;

    // Feature chain: enable dynamic rendering + extended dynamic state so the
    // vkCmdBeginRendering / vkCmdSetCullMode etc. calls in CommandStream.cpp
    // are valid. Without these features a strict driver rejects pipeline
    // creation that carries VkPipelineRenderingCreateInfo and rejects the
    // dynamic-state vkCmdSet* calls. (MoltenVK is lenient but the spec
    // requires the features to be enabled.)
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extDynStateFeat{};
    extDynStateFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
    extDynStateFeat.extendedDynamicState = VK_TRUE;

    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynRenderFeat{};
    dynRenderFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    dynRenderFeat.dynamicRendering = VK_TRUE;
    dynRenderFeat.pNext = &extDynStateFeat;

    // FIX (root cause Q): When VK_KHR_portability_subset is enabled (it is, on
    // MoltenVK — Device.cpp:197-199), the Vulkan spec REQUIRES
    // VkPhysicalDevicePortabilitySubsetFeaturesKHR to be chained into
    // VkDeviceCreateInfo::pNext. Without it, all portability-subset features
    // default to VK_FALSE, which may cause pipelines using portability-
    // constrained features (triangleFans, separateStencilTextureFilter, etc.)
    // to silently fail on MoltenVK → black screen. MobileGL chains this struct.
    // Query the supported features first; only enable what the device supports.
    VkPhysicalDevicePortabilitySubsetFeaturesKHR portSubsetFeat{};
    portSubsetFeat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR;
    portSubsetFeat.pNext = nullptr;
    if (has_extension(devExtProps, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
        // Query the supported portability-subset features. vkGetPhysicalDevice-
        // Features2 fills portSubsetFeat with VK_TRUE for features the device
        // supports; chaining the struct into VkDeviceCreateInfo::pNext enables
        // exactly those supported features. We do NOT force any feature on,
        // so unsupported features stay VK_FALSE (spec-safe).
        VkPhysicalDeviceFeatures2 feat2{};
        feat2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        feat2.pNext = &portSubsetFeat;
        vkGetPhysicalDeviceFeatures2(b->physicalDevice, &feat2);
        // Chain portability-subset at the END of the feature chain
        // (after extended-dynamic-state). The order does not matter for
        // correctness; we just need all feature structs in the pNext chain.
        extDynStateFeat.pNext = &portSubsetFeat;
    }

    // ---- Core VkPhysicalDeviceFeatures ----
    // Without pEnabledFeatures, all core features default to VK_FALSE, which
    // means ANY pipeline/shader using them will fail vkCreateGraphicsPipelines
    // and get negatively cached in Pipeline.cpp's failedSignatures — the draw
    // is then permanently skipped, manifesting as black-screen-with-sound.
    // MobileGL (VulkanRenderer.cpp:7291-7348) explicitly enables ~17 features
    // here; we mirror that set, gated by what the physical device actually
    // supports so we never request an unsupported feature (which would cause
    // vkCreateDevice to fail).
    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(b->physicalDevice, &supported);

    VkPhysicalDeviceFeatures enabled{};
    enabled.robustBufferAccess            = supported.robustBufferAccess;
    enabled.independentBlend              = supported.independentBlend;
    enabled.fillModeNonSolid              = supported.fillModeNonSolid;
    enabled.dualSrcBlend                  = supported.dualSrcBlend;
    enabled.logicOp                       = supported.logicOp;
    enabled.shaderClipDistance            = supported.shaderClipDistance;
    enabled.shaderCullDistance            = supported.shaderCullDistance;
    enabled.wideLines                     = supported.wideLines;
    enabled.largePoints                   = supported.largePoints;
    enabled.shaderInt64                   = supported.shaderInt64;
    enabled.vertexPipelineStoresAndAtomics= supported.vertexPipelineStoresAndAtomics;
    enabled.fragmentStoresAndAtomics      = supported.fragmentStoresAndAtomics;
    enabled.shaderStorageImageExtendedFormats    = supported.shaderStorageImageExtendedFormats;
    enabled.shaderStorageImageReadWithoutFormat  = supported.shaderStorageImageReadWithoutFormat;
    enabled.shaderStorageImageWriteWithoutFormat = supported.shaderStorageImageWriteWithoutFormat;
    enabled.drawIndirectFirstInstance     = supported.drawIndirectFirstInstance;
    enabled.multiDrawIndirect             = supported.multiDrawIndirect;
    enabled.samplerAnisotropy             = supported.samplerAnisotropy;
    enabled.depthBounds                   = supported.depthBounds;
    enabled.occlusionQueryPrecise         = supported.occlusionQueryPrecise;
    enabled.pipelineStatisticsQuery       = supported.pipelineStatisticsQuery;
    enabled.multiViewport                 = supported.multiViewport;
    enabled.imageCubeArray                = supported.imageCubeArray;
    enabled.alphaToOne                    = supported.alphaToOne;

    VkDeviceCreateInfo devCI{};
    devCI.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devCI.pNext = &dynRenderFeat;
    devCI.queueCreateInfoCount = 1;
    devCI.pQueueCreateInfos = &queueCI;
    devCI.enabledExtensionCount = (uint32_t)devExts.size();
    devCI.ppEnabledExtensionNames = devExts.data();
    devCI.pEnabledFeatures = &enabled;

    if (vkCreateDevice(b->physicalDevice, &devCI, nullptr, &b->device) != VK_SUCCESS) {
        MITHRIL_LOG_ERROR("vk", "vkCreateDevice failed");
        return false;
    }
    vkGetDeviceQueue(b->device, b->graphicsFamily, 0, &b->graphicsQueue);

    // ---- Command pool + primary command buffer ----
    VkCommandPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolCI.queueFamilyIndex = b->graphicsFamily;
    if (vkCreateCommandPool(b->device, &poolCI, nullptr, &b->commandPool) != VK_SUCCESS) {
        MITHRIL_LOG_ERROR("vk", "vkCreateCommandPool failed");
        return false;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = b->commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kMaxFramesInFlight;
    if (vkAllocateCommandBuffers(b->device, &allocInfo, b->commandBuffers) != VK_SUCCESS) {
        MITHRIL_LOG_ERROR("vk", "vkAllocateCommandBuffers failed");
        return false;
    }
    // Alias points to the current slot's buffer (slot 0 at init).
    b->commandBuffer = b->commandBuffers[b->currentFrame];

    // ---- Per-frame fences ----
    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // start signalled so first wait is a no-op
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        vkCreateFence(b->device, &fenceCI, nullptr, &b->frameFences[i]);
    }

    // Begin slot 0's command buffer in the recording state so that pre-frame
    // commands (texture uploads, layout transitions from glTexStorage2D) have
    // somewhere to record before the first begin_render_pass. The other slots
    // are lazily begun by ensure_command_buffer_recording() when their turn
    // comes. This mirrors MobileGL's FrameContext::Initialize
    // (FrameContext.cpp:29-31) which begins each slot's buffer at init.
    VkCommandBufferBeginInfo cbBegin{};
    cbBegin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbBegin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(b->commandBuffer, &cbBegin) == VK_SUCCESS) {
        b->commandBufferRecording = true;
    } else {
        MITHRIL_LOG_ERROR("vk", "initial vkBeginCommandBuffer failed");
        return false;
    }

    // ---- Pipeline cache ----
    VkPipelineCacheCreateInfo cacheCI{};
    cacheCI.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(b->device, &cacheCI, nullptr, &b->pipelineCache);

    // ---- Dummy vertex buffer ----
    // 16-byte zero buffer for vertex attributes the shader declares but GL
    // has not enabled (see Pipeline.cpp's get_or_create_pipeline). Provides
    // valid backing for dummy attribute descriptions so SPIRV-Cross emits
    // [[attribute(N)]] for every stage_in field, avoiding the Metal
    // "invalid type ... stage_in" compile error.
    {
        static const uint8_t zeros[16] = {0};
        BufferEntry tmp{};
        if (create_buffer(tmp, 16, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, zeros)) {
            b->dummyVertexBuffer = tmp.buffer;
            b->dummyVertexMemory = tmp.memory;
        } else {
            MITHRIL_LOG_WARN("vk", "failed to allocate dummy vertex buffer");
        }
    }

    // ---- Per-frame transient staging arena (MobileGL pattern) ----
    // 每个 frame slot 一个大的 host-visible VkBuffer，用于纹理上传的 staging。
    // 在 ensure_command_buffer_recording 的 fence wait 后 rewind offset 到 0。
    // 这消除了 per-texture staging buffer 的分配/销毁循环，根治 Metal
    // Invalid Resource (code 9) UAF 崩溃。
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        VkBufferCreateInfo sbci{};
        sbci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        sbci.size = Backend::kFrameStagingSize;
        sbci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        sbci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(b->device, &sbci, nullptr, &b->frameStagingBuffer[i]) != VK_SUCCESS) {
            MITHRIL_LOG_ERROR("vk", "failed to create frame staging buffer "
                              "(slot %d, size=%zu)", i, (size_t)Backend::kFrameStagingSize);
            break;
        }
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(b->device, b->frameStagingBuffer[i], &req);
        uint32_t mt = find_memory_type(req.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (mt == 0xFFFFFFFFu) {
            vkDestroyBuffer(b->device, b->frameStagingBuffer[i], nullptr);
            b->frameStagingBuffer[i] = VK_NULL_HANDLE;
            MITHRIL_LOG_ERROR("vk", "no host-visible memory type for frame staging "
                              "buffer (slot %d)", i);
            break;
        }
        VkMemoryAllocateInfo smai{};
        smai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        smai.allocationSize = req.size;
        smai.memoryTypeIndex = mt;
        // 使用 try_allocate_memory_with_gc（虽然首次分配不应 OOM，但保持一致性）
        if (try_allocate_memory_with_gc(b->device, &smai, nullptr,
                                         &b->frameStagingMemory[i]) != VK_SUCCESS) {
            vkDestroyBuffer(b->device, b->frameStagingBuffer[i], nullptr);
            b->frameStagingBuffer[i] = VK_NULL_HANDLE;
            MITHRIL_LOG_ERROR("vk", "failed to allocate memory for frame staging "
                              "buffer (slot %d)", i);
            break;
        }
        vkBindBufferMemory(b->device, b->frameStagingBuffer[i],
                           b->frameStagingMemory[i], 0);
        // Persistently map — keep mapped for the lifetime of the buffer.
        // Host-coherent memory: writes are automatically visible to GPU.
        if (vkMapMemory(b->device, b->frameStagingMemory[i], 0,
                        Backend::kFrameStagingSize, 0,
                        &b->frameStagingMapped[i]) != VK_SUCCESS) {
            MITHRIL_LOG_WARN("vk", "failed to persistently map frame staging "
                             "buffer (slot %d) — falling back to per-upload map",
                             i);
            b->frameStagingMapped[i] = nullptr;
        }
        b->frameStagingOffset[i] = 0;
    }
    // Check if all slots were created successfully
    b->frameStagingReady = true;
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        if (b->frameStagingBuffer[i] == VK_NULL_HANDLE) {
            b->frameStagingReady = false;
            break;
        }
    }
    if (b->frameStagingReady) {
        MITHRIL_LOG_INFO("vk", "Per-frame transient staging arena initialised "
                          "(%d slots x %zu MB, persistently mapped)",
                          kMaxFramesInFlight,
                          (size_t)Backend::kFrameStagingSize / (1024 * 1024));
    } else {
        MITHRIL_LOG_WARN("vk", "Per-frame staging arena NOT ready — falling "
                          "back to per-texture staging buffers (higher "
                          "allocation count, potential Invalid Resource risk)");
    }

    b->initialized = true;
    MITHRIL_LOG_INFO("vk", "Vulkan 1.2 backend initialised (MoltenVK static link)");
    return true;
}

void shutdown_device() {
    Backend* b = backend();
    if (!b->initialized) return;
    if (b->device) vkDeviceWaitIdle(b->device);
    // After vkDeviceWaitIdle, all GPU work is complete — safe to destroy any
    // deferred resources still sitting in the disposal queues.
    drain_all_disposal_queues();
    if (b->pipelineCache) { vkDestroyPipelineCache(b->device, b->pipelineCache, nullptr); b->pipelineCache = VK_NULL_HANDLE; }
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        if (b->frameFences[i]) { vkDestroyFence(b->device, b->frameFences[i], nullptr); b->frameFences[i] = VK_NULL_HANDLE; }
    }
    if (b->commandPool && b->commandBuffers[0]) {
        vkFreeCommandBuffers(b->device, b->commandPool, kMaxFramesInFlight, b->commandBuffers);
        for (int i = 0; i < kMaxFramesInFlight; ++i) b->commandBuffers[i] = VK_NULL_HANDLE;
        b->commandBuffer = VK_NULL_HANDLE;
    }
    if (b->commandPool) { vkDestroyCommandPool(b->device, b->commandPool, nullptr); b->commandPool = VK_NULL_HANDLE; }
    if (b->dummyVertexBuffer) { vkDestroyBuffer(b->device, b->dummyVertexBuffer, nullptr); b->dummyVertexBuffer = VK_NULL_HANDLE; }
    if (b->dummyVertexMemory) { vkFreeMemory(b->device, b->dummyVertexMemory, nullptr); b->dummyVertexMemory = VK_NULL_HANDLE; }
    // Destroy per-frame transient staging arena
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        if (b->frameStagingMapped[i]) { vkUnmapMemory(b->device, b->frameStagingMemory[i]); b->frameStagingMapped[i] = nullptr; }
        if (b->frameStagingBuffer[i]) { vkDestroyBuffer(b->device, b->frameStagingBuffer[i], nullptr); b->frameStagingBuffer[i] = VK_NULL_HANDLE; }
        if (b->frameStagingMemory[i]) { vkFreeMemory(b->device, b->frameStagingMemory[i], nullptr); b->frameStagingMemory[i] = VK_NULL_HANDLE; }
        b->frameStagingOffset[i] = 0;
    }
    b->frameStagingReady = false;
    if (b->device) { vkDestroyDevice(b->device, nullptr); b->device = VK_NULL_HANDLE; }
    if (b->instance) { vkDestroyInstance(b->instance, nullptr); b->instance = VK_NULL_HANDLE; }
    b->initialized = false;
}

} // namespace vk
} // namespace mithril

// ===========================================================================
// Public C API lifecycle functions (declared in MG_Backend/Backend.h)
// ===========================================================================
extern "C" {

void backend_init(void) {
    mithril::vk::init_device();
}

void backend_shutdown(void) {
    mithril::vk::shutdown_device();
}

int backend_available(void) {
    return mithril::vk::backend()->initialized ? 1 : 0;
}

const char* backend_physical_device_name(void) {
    mithril::vk::Backend* b = mithril::vk::backend();
    return b->initialized ? b->props.deviceName : "Vulkan (MoltenVK)";
}

uint64_t backend_vram_bytes(void) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->initialized) return 0;
    // MoltenVK reports maxMemoryAllocationCount but not total VRAM reliably;
    // approximate using the heap sizes from memory properties.
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(b->physicalDevice, &mp);
    uint64_t total = 0;
    for (uint32_t i = 0; i < mp.memoryHeapCount; ++i) {
        if (mp.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            total += mp.memoryHeaps[i].size;
        }
    }
    return total;
}

} // extern "C"
