// Mithril-Wrapper - MG_Backend/DirectVulkan/Resources.cpp
// VkBuffer / VkImage / VkImageView / VkSampler lifecycle + GL internalFormat
// -> VkFormat mapping + staging upload. Implements the backend_get_or_create_*
// family declared in MG_Backend/Backend.h.
#include "Resources.h"
#include "Device.h"
#include "CommandStream.h"  // ensure_command_buffer_recording
#include "../Backend.h"
#include "../../MG_Impl/Log.h"

#include <cstring>
#include <unordered_set>
#include <vector>

namespace mithril {
namespace vk {

std::unordered_map<GLuint, BufferEntry>&  buffer_table()  { static std::unordered_map<GLuint, BufferEntry>  t; return t; }
std::unordered_map<GLuint, TextureEntry>& texture_table() { static std::unordered_map<GLuint, TextureEntry> t; return t; }
std::unordered_map<GLuint, SamplerEntry>& sampler_table() { static std::unordered_map<GLuint, SamplerEntry> t; return t; }

uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags props) {
    Backend* b = backend();
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(b->physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return 0xFFFFFFFFu;
}

// FIX (显存耗尽根因 - OOM 主动 GC):
// 封装 vkAllocateMemory。首次分配失败（VK_ERROR_OUT_OF_DEVICE_MEMORY）时
// 触发一次强制 GC：vkDeviceWaitIdle 等待所有 GPU 工作完成，然后
// drain_all_disposal_queues 释放所有延迟销毁队列中的资源（staging buffer、
// 旧纹理、orphaned buffer），腾出显存后重试一次。
//
// 参考 MobileGL TryDrainFrameTransients（VulkanRenderer.cpp:7239-7313）：
// present-suspend 期间主动 drain + rewind transient arena 防止资源累积。
// 我们在分配失败时做同样的事，避免 OOM 导致渲染永久失败。
VkResult try_allocate_memory_with_gc(VkDevice device, const VkMemoryAllocateInfo* info,
                                     const VkAllocationCallbacks* allocator,
                                     VkDeviceMemory* memory) {
    // FIX (显存耗尽根因 - 主动式 GC，深度参考 MobileGL):
    // 在尝试分配之前，先检查内存压力。当 allocationCount 接近上限时，
    // 主动触发 GC（vkDeviceWaitIdle + drain_all_disposal_queues），
    // 在 GPU 进入降级状态（timeout/device lost）之前释放显存。
    //
    // 这是 PREVENTIVE 措施，与下面失败后重试的 REACTIVE 措施配合：
    // 1. proactive_gc_if_needed: 防止 OOM 发生（70% 阈值触发）
    // 2. 失败后 GC 重试: 兜底，处理 proactive 没覆盖的情况
    //
    // 参考 MobileGL：它每帧 TryDrainFrameTransients 主动 drain，根本不会
    // 触发 vkAllocateMemory 失败。我们没有 MobileGL 的分层 manager，所以
    // 在分配路径加这个压力检查。
    backend_proactive_gc_if_needed();

    VkResult r = vkAllocateMemory(device, info, allocator, memory);
    if (r == VK_SUCCESS) return r;
    if (r != VK_ERROR_OUT_OF_DEVICE_MEMORY) return r;

    // OOM：触发一次强制 GC
    static int gcTriggerCount = 0;
    gcTriggerCount++;
    // 限流日志：首次 + 每 50 次
    if (gcTriggerCount <= 3 || gcTriggerCount % 50 == 0) {
        MITHRIL_LOG_WARN("vk", "OOM detected (vkAllocateMemory failed), "
                          "triggering forced GC (attempt #%d): safe_device_wait_idle "
                          "+ drain_all_disposal_queues",
                          gcTriggerCount);
    }
    Backend* b = backend();
    // FIX (SIGBUS): 使用 safe_device_wait_idle 而非直接 vkDeviceWaitIdle。
    // try_allocate_memory_with_gc 被 create_buffer 调用，后者被 stage_and_copy_image
    // 调用。此时 command buffer 可能正在录制（包含之前帧内已记录的
    // vkCmdCopyBufferToImage），直接 vkDeviceWaitIdle 会触发 MoltenVK 的
    // deferred encoding → MVKCmdBufferImageCopy::encode → SIGBUS。
    // safe_device_wait_idle 先安全结束+提交当前 command buffer，wait 后重新 begin。
    safe_device_wait_idle();
    drain_all_disposal_queues();

    // GC 后重试一次
    r = vkAllocateMemory(device, info, allocator, memory);
    if (r == VK_SUCCESS) {
        if (gcTriggerCount <= 3 || gcTriggerCount % 50 == 0) {
            MITHRIL_LOG_WARN("vk", "OOM recovery: vkAllocateMemory succeeded "
                              "after GC (freed delayed resources)");
        }
    }
    // GC 后仍失败则返回原错误码，由调用方处理（可能触发 deviceLost 恢复路径）
    return r;
}

bool create_buffer(BufferEntry& out, VkDeviceSize size,
                   VkBufferUsageFlags usage, const void* data) {
    Backend* b = backend();
    VkBufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = size;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(b->device, &ci, nullptr, &out.buffer) != VK_SUCCESS) return false;

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(b->device, out.buffer, &req);
    uint32_t mt = find_memory_type(req.memoryTypeBits,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == 0xFFFFFFFFu) { vkDestroyBuffer(b->device, out.buffer, nullptr); out.buffer = VK_NULL_HANDLE; return false; }
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = mt;
    // FIX (OOM 主动 GC): 使用带 GC 的分配函数，OOM 时先排空延迟队列重试
    if (try_allocate_memory_with_gc(b->device, &ai, nullptr, &out.memory) != VK_SUCCESS) {
        vkDestroyBuffer(b->device, out.buffer, nullptr);
        out.buffer = VK_NULL_HANDLE;
        return false;
    }
    // FIX (P1): 递增分配计数器，接近上限时警告
    b->currentAllocationCount++;
    if (b->maxMemoryAllocationCount > 0 &&
        b->currentAllocationCount >= b->maxMemoryAllocationCount * 4 / 5) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            MITHRIL_LOG_WARN("vk", "Memory allocation count %u/%u (>=80%%) — "
                              "consider freeing resources",
                              (unsigned)b->currentAllocationCount,
                              (unsigned)b->maxMemoryAllocationCount);
        }
    }
    vkBindBufferMemory(b->device, out.buffer, out.memory, 0);
    if (data) {
        void* dst = nullptr;
        vkMapMemory(b->device, out.memory, 0, size, 0, &dst);
        if (dst) { std::memcpy(dst, data, (size_t)size); vkUnmapMemory(b->device, out.memory); }
    }
    out.size = size;
    return true;
}

void destroy_buffer_entry(BufferEntry& e) {
    Backend* b = backend();
    if (!b->device) return;
    if (e.mapped) { vkUnmapMemory(b->device, e.memory); e.mapped = nullptr; }
    if (e.buffer) { vkDestroyBuffer(b->device, e.buffer, nullptr); e.buffer = VK_NULL_HANDLE; }
    if (e.memory) { vkFreeMemory(b->device, e.memory, nullptr); e.memory = VK_NULL_HANDLE; }
    e.size = 0;
}

void destroy_texture_entry(TextureEntry& e) {
    Backend* b = backend();
    if (!b->device) return;
    if (e.view)          { vkDestroyImageView(b->device, e.view, nullptr); e.view = VK_NULL_HANDLE; }
    if (e.image)         { vkDestroyImage(b->device, e.image, nullptr); e.image = VK_NULL_HANDLE; }
    if (e.memory)        { vkFreeMemory(b->device, e.memory, nullptr); e.memory = VK_NULL_HANDLE; }
    if (e.stagingBuffer) { vkDestroyBuffer(b->device, e.stagingBuffer, nullptr); e.stagingBuffer = VK_NULL_HANDLE; }
    if (e.stagingMemory) { vkFreeMemory(b->device, e.stagingMemory, nullptr); e.stagingMemory = VK_NULL_HANDLE; }
    e.stagingSize = 0;
}

// ---- Deferred destruction (root cause U fix) ----
// Push the entry's Vulkan handles into disposalQueue[currentFrame] and null
// out the entry. The actual vkDestroy* / vkFreeMemory calls happen in
// drain_disposal_queue() after the slot's fence is waited — by then the GPU
// has finished all command buffers that might reference these resources.
// This prevents the Metal resource UAF crash where MoltenVK's command
// encoding retains MTLBuffer/MTLTexture wrappers that were already freed
// by an immediate vkDestroy*.
void defer_destroy_buffer_entry(BufferEntry& e) {
    Backend* b = backend();
    if (!b->device) return;
    if (e.mapped) { vkUnmapMemory(b->device, e.memory); e.mapped = nullptr; }
    if (e.buffer == VK_NULL_HANDLE && e.memory == VK_NULL_HANDLE) return;
    DeferredDestroy d;
    d.buffer = e.buffer;
    d.memory = e.memory;
    b->disposalQueue[b->currentFrame].push_back(d);
    e.buffer = VK_NULL_HANDLE;
    e.memory = VK_NULL_HANDLE;
    e.size = 0;
}

void defer_destroy_texture_entry(TextureEntry& e) {
    Backend* b = backend();
    if (!b->device) return;
    if (e.view == VK_NULL_HANDLE && e.image == VK_NULL_HANDLE &&
        e.memory == VK_NULL_HANDLE && e.stagingBuffer == VK_NULL_HANDLE &&
        e.stagingMemory == VK_NULL_HANDLE) return;
    DeferredDestroy d;
    d.image = e.image;
    d.view  = e.view;
    d.memory = e.memory;
    b->disposalQueue[b->currentFrame].push_back(d);
    // Staging buffer/memory go in a separate entry (they may be independently
    // non-null while the main image is null, e.g. during staging resize).
    if (e.stagingBuffer != VK_NULL_HANDLE || e.stagingMemory != VK_NULL_HANDLE) {
        DeferredDestroy ds;
        ds.buffer = e.stagingBuffer;
        ds.memory = e.stagingMemory;
        b->disposalQueue[b->currentFrame].push_back(ds);
    }
    e.view = VK_NULL_HANDLE;
    e.image = VK_NULL_HANDLE;
    e.memory = VK_NULL_HANDLE;
    e.stagingBuffer = VK_NULL_HANDLE;
    e.stagingMemory = VK_NULL_HANDLE;
    e.stagingSize = 0;
}

void defer_destroy_sampler_entry(SamplerEntry& e) {
    Backend* b = backend();
    if (!b->device || e.sampler == VK_NULL_HANDLE) return;
    DeferredDestroy d;
    d.sampler = e.sampler;
    b->disposalQueue[b->currentFrame].push_back(d);
    e.sampler = VK_NULL_HANDLE;
}

// ---- GL internalFormat -> VkFormat / host texel bytes / aspect mask ----
// (Moved to FormatMap.{h,cpp} so they can be unit-tested without linking the
// rest of the Vulkan backend. See FormatMap.h for the declarations.)

void stage_and_copy_image(TextureEntry& tex, int level, int x, int y, int z,
                          int w, int h, int d, const void* pixels,
                          int unpack_alignment, GLenum format, GLenum type,
                          bool is_full_upload) {
    Backend* b = backend();
    if (!b->commandBuffer) return;
    // With per-slot command buffers, the alias b->commandBuffer may point at
    // a just-submitted (pending) buffer after commit_frame advanced the slot.
    // ensure_command_buffer_recording() lazily switches to the current slot's
    // buffer, waits on its fence, resets, and begins it. Without this, the
    // vkCmdPipelineBarrier / vkCmdCopyBufferToImage calls below would record
    // into a non-recording buffer (spec UB).
    if (!ensure_command_buffer_recording()) return;
    if (unpack_alignment <= 0) unpack_alignment = 4;  // GL default UNPACK_ALIGNMENT

    // Compute host-side bytes per pixel for this (format, type) pair and
    // honour GL_UNPACK_ALIGNMENT when computing the source row stride. The
    // staging buffer is repacked to be tightly packed, matching
    // VkBufferImageCopy.bufferRowLength == 0 below.
    int bpp = host_texel_bytes(format, type);
    if (bpp <= 0) bpp = 4;  // conservative fallback
    size_t tight_row = (size_t)w * (size_t)bpp;
    size_t mask = (size_t)unpack_alignment - 1;
    size_t src_stride = (tight_row + mask) & ~mask;
    size_t staging = tight_row * (size_t)h * (size_t)d;

    // ---- FIX (Invalid Resource 根因 - per-frame transient staging arena) ----
    // 深度参考 MobileGL 的 transient staging arena 模式：
    // 从当前 frame slot 的大 staging buffer 中 sub-allocate（bump offset），
    // 而不是为每张纹理创建/销毁独立的 staging buffer。
    //
    // 这消除了：
    //   1. per-texture vkCreateBuffer + vkAllocateMemory（降低 allocation count）
    //   2. staging buffer 的 disposalQueue 条目（消除 UAF 风险）
    //   3. vkMapMemory/vkUnmapMemory 的 per-upload 开销（arena persistently mapped）
    //
    // arena 在 ensure_command_buffer_recording 的 fence wait 后 rewind 到 0，
    // 保证 GPU 已完成对该 slot staging buffer 的引用。
    //
    // overflow（staging > 剩余空间）回退到临时 staging buffer + deferred destroy。

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize stagingOffset = 0;
    void* stagingMapped = nullptr;
    bool usedArena = false;

    if (b->frameStagingReady) {
        // 对齐到 256 字节（满足 VkBufferImageCopy.bufferOffset 的对齐要求，
        // 也满足 MoltenVK/Metal 的 MTLBuffer offset 对齐）
        VkDeviceSize alignedOffset = (b->frameStagingOffset[b->currentFrame] + 255) & ~255;
        if (alignedOffset + staging <= Backend::kFrameStagingSize) {
            // Fast path: sub-allocate from per-frame arena
            stagingBuffer = b->frameStagingBuffer[b->currentFrame];
            stagingOffset = alignedOffset;
            stagingMapped = b->frameStagingMapped[b->currentFrame];
            b->frameStagingOffset[b->currentFrame] = alignedOffset + staging;
            usedArena = true;
        }
        // else: overflow — fall through to temporary staging buffer path
    }

    if (!usedArena) {
        // Overflow path: arena 不存在或剩余空间不足。
        // 创建临时 staging buffer，上传后延迟销毁（disposalQueue）。
        // 这是罕见情况（单帧上传超过 16MB），不影响整体性能。
        // 对超大单张纹理（>16MB），也需要走此路径。
        if (tex.stagingSize < staging) {
            if (tex.stagingBuffer != VK_NULL_HANDLE || tex.stagingMemory != VK_NULL_HANDLE) {
                DeferredDestroy ds;
                ds.buffer = tex.stagingBuffer;
                ds.memory = tex.stagingMemory;
                b->disposalQueue[b->currentFrame].push_back(ds);
                tex.stagingBuffer = VK_NULL_HANDLE;
                tex.stagingMemory = VK_NULL_HANDLE;
            }
            BufferEntry tmp;
            if (create_buffer(tmp, staging,
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT, nullptr)) {
                tex.stagingBuffer = tmp.buffer;
                tex.stagingMemory = tmp.memory;
                tex.stagingSize = staging;
            } else {
                return;
            }
        }
        stagingBuffer = tex.stagingBuffer;
        stagingOffset = 0;
        // Per-upload map for overflow path (arena 的 persistently mapped 不可用)
        vkMapMemory(b->device, tex.stagingMemory, 0, staging, 0, &stagingMapped);
    }

    // Copy pixel data into staging buffer at stagingOffset
    if (stagingMapped && pixels) {
        char* dst = (char*)stagingMapped + stagingOffset;
        if (src_stride == tight_row) {
            // Source rows are already tightly packed — single memcpy.
            std::memcpy(dst, pixels, staging);
        } else {
            // Source rows carry GL_UNPACK_ALIGNMENT padding; repack to tight
            // so VkBufferImageCopy.bufferRowLength == 0 (== w) is valid.
            const char* s8 = (const char*)pixels;
            for (int layer = 0; layer < d; ++layer) {
                for (int row = 0; row < h; ++row) {
                    std::memcpy(dst, s8, tight_row);
                    dst += tight_row;
                    s8 += src_stride;
                }
            }
        }
    }

    if (!usedArena && stagingMapped) {
        // Unmap the per-upload mapping (overflow path only)
        vkUnmapMemory(b->device, tex.stagingMemory);
    }
    // Arena path: no unmap needed (persistently mapped)

    VkBufferImageCopy region{};
    region.bufferOffset = stagingOffset;  // 非 0 for arena sub-allocation
    region.bufferRowLength = 0;     // tightly packed
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (tex.format == VK_FORMAT_D16_UNORM || tex.format == VK_FORMAT_D32_SFLOAT)
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (tex.format == VK_FORMAT_S8_UINT)
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    if (tex.format == VK_FORMAT_D24_UNORM_S8_UINT || tex.format == VK_FORMAT_D32_SFLOAT_S8_UINT)
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;  // depth aspect only
    region.imageSubresource.mipLevel = level;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { x, y, z };
    region.imageExtent = { (uint32_t)w, (uint32_t)h, (uint32_t)d };

    // Transition the image layout to TRANSFER_DST for the copy.
    // 根因 F：部分上传（glTexSubImage*）必须用 tex.currentLayout 作为 oldLayout，
    // 保留未更新区域的既有内容；完整上传（glTexImage*）用 UNDEFINED 丢弃旧内容。
    // 无条件用 UNDEFINED 会导致 glTexSubImage2D 后纹理其余区域变 undefined →
    // 纹理损坏 → 物体黑色斑块。
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = is_full_upload ? VK_IMAGE_LAYOUT_UNDEFINED : tex.currentLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tex.image;
    barrier.subresourceRange.aspectMask = region.imageSubresource.aspectMask;
    barrier.subresourceRange.baseMipLevel = level;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(b->commandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);

    vkCmdCopyBufferToImage(b->commandBuffer, stagingBuffer, tex.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition to SHADER_READ_ONLY so the texture can be sampled afterwards.
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkPipelineStageFlagBits dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    vkCmdPipelineBarrier(b->commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         dstStage, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
    tex.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Arena path: no cleanup needed — staging buffer 是永久的，offset 在
    // 下次 ensure_command_buffer_recording 时 rewind。
    //
    // Overflow path: 延迟释放临时 staging buffer（与原实现相同）。
    if (!usedArena) {
        if (tex.stagingBuffer != VK_NULL_HANDLE || tex.stagingMemory != VK_NULL_HANDLE) {
            DeferredDestroy ds;
            ds.buffer = tex.stagingBuffer;
            ds.memory = tex.stagingMemory;
            b->disposalQueue[b->currentFrame].push_back(ds);
            tex.stagingBuffer = VK_NULL_HANDLE;
            tex.stagingMemory = VK_NULL_HANDLE;
            tex.stagingSize = 0;
        }
    }
}

// aspect_for_format() moved to FormatMap.{h,cpp} (pure-logic helper).

// Stage masks for the source side of an image-memory barrier, keyed on the
// old layout. Returns 0 when the old layout is UNDEFINED or PREINITIALIZED
// (no prior work needs to be visible).
static VkAccessFlags src_access_for_layout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_ACCESS_SHADER_READ_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_ACCESS_TRANSFER_READ_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_ACCESS_TRANSFER_WRITE_BIT;
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        default:
            return 0;  // UNDEFINED / PREINITIALIZED
    }
}

// Stage masks for the destination side of an image-memory barrier, keyed on
// the new layout. Returns 0 when the new layout is PREINITIALIZED (invalid).
static VkAccessFlags dst_access_for_layout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_ACCESS_SHADER_READ_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_ACCESS_TRANSFER_READ_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_ACCESS_TRANSFER_WRITE_BIT;
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        default:
            return 0;
    }
}

// Source pipeline stage for an image-memory barrier, keyed on the old layout.
static VkPipelineStageFlags src_stage_for_layout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            return VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_PIPELINE_STAGE_TRANSFER_BIT;
        default:
            return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
}

// Destination pipeline stage for an image-memory barrier, keyed on the new layout.
static VkPipelineStageFlags dst_stage_for_layout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_PIPELINE_STAGE_TRANSFER_BIT;
        default:
            return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
}

void transition_image_layout(TextureEntry& tex, VkImageLayout newLayout) {
    Backend* b = backend();
    if (!b->commandBuffer || tex.image == VK_NULL_HANDLE) return;
    if (tex.currentLayout == newLayout) return;  // already there
    // Ensure the current slot's command buffer is recording before we issue
    // vkCmdPipelineBarrier. See stage_and_copy_image for rationale.
    if (!ensure_command_buffer_recording()) return;

    VkImageAspectFlags aspect = aspect_for_format(tex.format);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = src_access_for_layout(tex.currentLayout);
    barrier.dstAccessMask = dst_access_for_layout(newLayout);
    barrier.oldLayout = tex.currentLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tex.image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = (uint32_t)tex.levels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(b->commandBuffer,
                         src_stage_for_layout(tex.currentLayout),
                         dst_stage_for_layout(newLayout),
                         0,
                         0, nullptr, 0, nullptr, 1, &barrier);
    tex.currentLayout = newLayout;
}

// ---- GL filter/wrap -> VkFilter / VkSamplerAddressMode ----
static VkFilter to_vk_filter(GLenum f) {
    if (f == GL_NEAREST || f == GL_NEAREST_MIPMAP_NEAREST || f == GL_NEAREST_MIPMAP_LINEAR) return VK_FILTER_NEAREST;
    return VK_FILTER_LINEAR;
}
static VkSamplerMipmapMode to_vk_mipmap(GLenum f) {
    if (f == GL_NEAREST_MIPMAP_NEAREST || f == GL_LINEAR_MIPMAP_NEAREST) return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    return VK_SAMPLER_MIPMAP_MODE_LINEAR;
}
static VkSamplerAddressMode to_vk_wrap(GLenum w) {
    switch (w) {
        case GL_CLAMP_TO_EDGE:        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case GL_CLAMP_TO_BORDER:      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case GL_MIRRORED_REPEAT:      return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case GL_REPEAT:
        default:                      return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

} // namespace vk
} // namespace mithril

// ===========================================================================
// Public C API (declared in MG_Backend/Backend.h). These thin wrappers map GL
// names to the vk::* tables above and create/destroy Vulkan resources.
// ===========================================================================
extern "C" {

VkBuffer backend_get_or_create_buffer(GLuint name, const void* data, size_t size) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->initialized || name == 0 || size == 0) return VK_NULL_HANDLE;
    auto& tbl = mithril::vk::buffer_table();
    auto it = tbl.find(name);
    // FIX (显存碎片化/持续增长): 当 buffer 已存在且大小足够时，即使有 data
    // 也原地更新（vkMapMemory + memcpy），而不是 orphan 旧 buffer 重新分配。
    // 原实现要求 !data 才复用，但 glBufferData 和 UBO 更新都传 data != nullptr，
    // 导致每次调用都走 orphan 路径（destroy + realloc），每帧产生上百次
    // vkCreateBuffer/vkAllocateMemory 和延迟销毁，在 iPhone SE 3 等显存紧张
    // 设备上导致 VK_ERROR_OUT_OF_DEVICE_MEMORY。
    if (it != tbl.end() && it->second.size >= (VkDeviceSize)size) {
        // Buffer 已存在且容量足够：原地更新数据（如果有）
        if (data && size > 0) {
            void* dst = nullptr;
            if (vkMapMemory(b->device, it->second.memory, 0, size, 0, &dst) == VK_SUCCESS && dst) {
                std::memcpy(dst, data, size);
                vkUnmapMemory(b->device, it->second.memory);
            }
        }
        return it->second.buffer;
    }
    // Buffer 不存在或容量不足：orphan 旧的，创建新的
    if (it != tbl.end()) mithril::vk::defer_destroy_buffer_entry(it->second);
    mithril::vk::BufferEntry e;
    if (!mithril::vk::create_buffer(e, size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            data)) {
        return VK_NULL_HANDLE;
    }
    tbl[name] = e;
    return e.buffer;
}

void backend_buffer_upload(GLuint name, GLintptr offset, const void* data, size_t size) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->initialized) return;
    auto& tbl = mithril::vk::buffer_table();
    auto it = tbl.find(name);
    if (it == tbl.end()) return;
    void* dst = nullptr;
    vkMapMemory(b->device, it->second.memory, offset, size, 0, &dst);
    if (dst) { std::memcpy(dst, data, size); vkUnmapMemory(b->device, it->second.memory); }
}

VkBuffer backend_get_buffer(GLuint name) {
    auto& tbl = mithril::vk::buffer_table();
    auto it = tbl.find(name);
    return it == tbl.end() ? VK_NULL_HANDLE : it->second.buffer;
}

void backend_delete_buffer(GLuint name) {
    auto& tbl = mithril::vk::buffer_table();
    auto it = tbl.find(name);
    if (it == tbl.end()) return;
    mithril::vk::defer_destroy_buffer_entry(it->second);
    tbl.erase(it);
}

VkBuffer backend_get_zero_buffer(void) {
    static GLuint zero_name = 0x40000000u;  // sentinel name for the shared zero buffer
    static bool tried = false;
    if (!tried) {
        tried = true;
        static const uint8_t zeros[16] = {0};
        backend_get_or_create_buffer(zero_name, zeros, sizeof(zeros));
    }
    return backend_get_buffer(zero_name);
}

VkImage backend_get_or_create_texture(GLuint name, int width, int height, int depth,
                                      int levels, GLenum internal_format, GLenum target,
                                      int samples) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->initialized || name == 0 || width <= 0 || height <= 0) return VK_NULL_HANDLE;
    VkFormat fmt = mithril::vk::gl_internal_to_vk(internal_format);
    if (fmt == VK_FORMAT_UNDEFINED) {
        // FIX (日志刷屏): 同一不支持的格式会被反复打印。用 static set 去重，
        // 每种格式只打印一次。
        static std::unordered_set<GLenum> warnedFormats;
        if (warnedFormats.find(internal_format) == warnedFormats.end()) {
            warnedFormats.insert(internal_format);
            MITHRIL_LOG_WARN("vk", "backend_get_or_create_texture: unsupported "
                              "internalFormat 0x%x (falling back to RGBA8, "
                              "further occurrences of this format suppressed)",
                              internal_format);
        }
        fmt = VK_FORMAT_R8G8B8A8_UNORM;
    }
    auto& tbl = mithril::vk::texture_table();
    auto it = tbl.find(name);
    if (it != tbl.end() && it->second.image != VK_NULL_HANDLE &&
        it->second.format == fmt &&
        it->second.width == width && it->second.height == height &&
        it->second.depth == depth) {
        return it->second.image;
    }
    if (it != tbl.end()) mithril::vk::defer_destroy_texture_entry(it->second);

    mithril::vk::TextureEntry e;
    e.format = fmt;
    e.width = width; e.height = height; e.depth = depth;
    e.levels = levels > 0 ? levels : 1;
    e.target = target;

    VkImageType imgType = (target == GL_TEXTURE_3D) ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = imgType;
    ici.format = fmt;
    ici.extent = { (uint32_t)width, (uint32_t)height, (uint32_t)(imgType == VK_IMAGE_TYPE_3D ? depth : 1) };
    ici.mipLevels = e.levels;
    ici.arrayLayers = (imgType == VK_IMAGE_TYPE_3D) ? 1 : (target == GL_TEXTURE_CUBE_MAP ? 6 : 1);
    ici.samples = (VkSampleCountFlagBits)(samples > 1 ? samples : 1);
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    // Depth/stencil attachments also need to be renderable.
    if (fmt == VK_FORMAT_D16_UNORM || fmt == VK_FORMAT_D32_SFLOAT ||
        fmt == VK_FORMAT_D24_UNORM_S8_UINT || fmt == VK_FORMAT_D32_SFLOAT_S8_UINT) {
        ici.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(b->device, &ici, nullptr, &e.image) != VK_SUCCESS) return VK_NULL_HANDLE;

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(b->device, e.image, &req);
    uint32_t mt = mithril::vk::find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mt == 0xFFFFFFFFu) mt = mithril::vk::find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = mt;
    // FIX (OOM 主动 GC): 使用带 GC 的分配函数，OOM 时先排空延迟队列重试
    if (mithril::vk::try_allocate_memory_with_gc(b->device, &ai, nullptr, &e.memory) != VK_SUCCESS) {
        vkDestroyImage(b->device, e.image, nullptr);
        return VK_NULL_HANDLE;
    }
    // FIX (P1): 递增分配计数器
    b->currentAllocationCount++;
    vkBindImageMemory(b->device, e.image, e.memory, 0);

    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = e.image;
    vci.viewType = (target == GL_TEXTURE_3D) ? VK_IMAGE_VIEW_TYPE_3D :
                   (target == GL_TEXTURE_CUBE_MAP ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D);
    vci.format = fmt;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (fmt == VK_FORMAT_D16_UNORM || fmt == VK_FORMAT_D32_SFLOAT)
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    else if (fmt == VK_FORMAT_D24_UNORM_S8_UINT || fmt == VK_FORMAT_D32_SFLOAT_S8_UINT)
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    else if (fmt == VK_FORMAT_S8_UINT)
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    vci.subresourceRange.baseMipLevel = 0;
    vci.subresourceRange.levelCount = e.levels;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount = ici.arrayLayers;
    vci.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
    vkCreateImageView(b->device, &vci, nullptr, &e.view);

    tbl[name] = e;
    return e.image;
}

void backend_texture_upload(GLuint name, int level, int x, int y, int z,
                            int w, int h, int d, GLenum format, GLenum type,
                            const void* pixels, int unpack_alignment,
                            int is_full_upload) {
    auto& tbl = mithril::vk::texture_table();
    auto it = tbl.find(name);
    if (it == tbl.end() || !pixels) return;
    mithril::vk::stage_and_copy_image(it->second, level, x, y, z, w, h, d,
                                      pixels, unpack_alignment, format, type,
                                      is_full_upload != 0);
}

void backend_texture_set_params(GLuint name, GLint min_filter, GLint mag_filter,
                                GLint wrap_s, GLint wrap_t, GLint wrap_r,
                                const float* border_color) {
    // Vulkan samplers are immutable; params are applied when the sampler is
    // fetched via backend_get_or_create_sampler (which caches per (name,param)).
    // Record nothing here — the sampler table is keyed by name and rebuilt on
    // demand. (See backend_get_or_create_sampler.)
    (void)name; (void)min_filter; (void)mag_filter;
    (void)wrap_s; (void)wrap_t; (void)wrap_r; (void)border_color;
}

VkImageView backend_get_texture_view(GLuint name) {
    auto& tbl = mithril::vk::texture_table();
    auto it = tbl.find(name);
    return it == tbl.end() ? VK_NULL_HANDLE : it->second.view;
}

VkImage backend_get_texture_image(GLuint name) {
    auto& tbl = mithril::vk::texture_table();
    auto it = tbl.find(name);
    return it == tbl.end() ? VK_NULL_HANDLE : it->second.image;
}

void backend_delete_texture(GLuint name) {
    auto& tbl = mithril::vk::texture_table();
    auto it = tbl.find(name);
    if (it == tbl.end()) return;
    mithril::vk::defer_destroy_texture_entry(it->second);
    tbl.erase(it);
}

void backend_transition_texture_layout(GLuint name, VkImageLayout target_layout) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->initialized) return;
    auto& tbl = mithril::vk::texture_table();
    auto it = tbl.find(name);
    if (it == tbl.end()) return;
    mithril::vk::transition_image_layout(it->second, target_layout);
}

VkSampler backend_get_or_create_sampler(GLuint name, GLint min_filter, GLint mag_filter,
                                        GLint wrap_s, GLint wrap_t, GLint wrap_r,
                                        const float* border_color) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!b->initialized) return VK_NULL_HANDLE;
    auto& tbl = mithril::vk::sampler_table();
    auto it = tbl.find(name);
    if (it != tbl.end() && it->second.sampler != VK_NULL_HANDLE) return it->second.sampler;

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = mithril::vk::to_vk_filter(mag_filter);
    sci.minFilter = mithril::vk::to_vk_filter(min_filter);
    sci.mipmapMode = mithril::vk::to_vk_mipmap(min_filter);
    sci.addressModeU = mithril::vk::to_vk_wrap(wrap_s);
    sci.addressModeV = mithril::vk::to_vk_wrap(wrap_t);
    sci.addressModeW = mithril::vk::to_vk_wrap(wrap_r);
    sci.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    if (border_color) {
        // Approximate border colour; only black/white matter for MoltenVK.
        bool white = border_color[0] >= 1.0f && border_color[1] >= 1.0f &&
                     border_color[2] >= 1.0f && border_color[3] >= 1.0f;
        sci.borderColor = white ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                                : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    }
    sci.anisotropyEnable = VK_FALSE;
    sci.maxAnisotropy = 1.0f;
    sci.compareEnable = VK_FALSE;
    sci.compareOp = VK_COMPARE_OP_ALWAYS;
    sci.minLod = 0.0f;
    sci.maxLod = 12.0f;
    sci.unnormalizedCoordinates = VK_FALSE;

    mithril::vk::SamplerEntry e;
    if (vkCreateSampler(b->device, &sci, nullptr, &e.sampler) != VK_SUCCESS) return VK_NULL_HANDLE;
    tbl[name] = e;
    return e.sampler;
}

VkFormat backend_vk_format_for_gl(GLenum internal_format) {
    return mithril::vk::gl_internal_to_vk(internal_format);
}

} // extern "C"
