// Mithril-Wrapper - MG_Backend/DirectVulkan/Pipeline.h
// VkShaderModule creation (from SPIR-V) + VkGraphicsPipeline caching keyed by
// a hash signature built from (program, vertex format, attachment formats,
// blend state, primitive mode). Implements backend_get_or_create_pipeline()
// and backend_delete_program_resources() declared in MG_Backend/Backend.h.
#ifndef MITHRIL_DIRECTVULKAN_PIPELINE_H
#define MITHRIL_DIRECTVULKAN_PIPELINE_H

#include <vulkan/vulkan.h>
#include <GL/gl.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "DescriptorSet.h"
#include "Device.h"       // kMaxFramesInFlight
#include "../Backend.h"   // defines ::MGVertexAttrib (extern "C", global scope)

namespace mithril {
namespace vk {

// Per-program owned Vulkan resources. Each Program GL object gets one of
// these; it holds the shader modules built from the linked SPIR-V, the
// descriptor set layout / pipeline layout / descriptor pool built from
// SPIRV-Cross reflection (see DescriptorSet.cpp), and the cache of pipelines
// derived from that program.
struct ProgramResources {
    VkShaderModule vertexModule = VK_NULL_HANDLE;
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    // Cached pipelines keyed by a 64-bit signature (see Pipeline.cpp).
    std::unordered_map<uint64_t, VkPipeline> pipelines;

    // Negative cache: signatures for which vkCreateGraphicsPipelines has
    // failed. Without this, every draw call with a failing signature re-runs
    // vkCreateGraphicsPipelines (the success-cache lookup at line ~245 only
    // stores VK_NULL_HANDLE if we explicitly insert it, which we don't). On a
    // persistent failure (e.g. shader/format mismatch), this means N draw
    // calls/frame × M frames of redundant pipeline-creation attempts plus
    // log spam. The set is cleared when the program's shader modules are
    // rebuilt (delete_program_resources / re-link) so a fixed shader can
    // recover. Draws against a negatively-cached signature skip the creation
    // attempt and return VK_NULL_HANDLE immediately (draw is skipped, but
    // without the CPU cost and log spam of a real attempt).
    std::unordered_set<uint64_t> failedSignatures;

    // ---- Descriptor set management (built once by ensure_program_layouts) ----
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout      pipelineLayout = VK_NULL_HANDLE;
    // One pool per frame-in-flight slot, created with
    // VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT. The pool is DESTROYED
    // and RECREATED every frame via reset_descriptor_caches_for_slot() (called
    // from ensure_command_buffer_recording() AFTER the fence wait but BEFORE
    // drain_disposal_queue()), which frees every VkDescriptorSet allocated from
    // it so the next frame allocates fresh sets.
    //
    // WHY destroy+recreate instead of vkResetDescriptorPool: MoltenVK v1.2.9's
    // vkResetDescriptorPool does NOT release retained MVKImageView objects — it
    // only resets availability bits, deferring release to allocateDescriptor's
    // "clear before reusing" path. The retained views are only released when
    // allocateDescriptor() calls mvkDesc->reset() — which happens AFTER
    // drain_disposal_queue has already destroyed the views via vkDestroyImageView.
    // The previous approach (cursor-rewind + set reuse, and later
    // vkResetDescriptorPool) kept descriptor sets / retained views alive across
    // frames. Those cached sets referenced VkImageViews that
    // drain_disposal_queue() destroys — a Vulkan spec violation
    // (vkDestroyImageView while a descriptor set references the view). When
    // vkUpdateDescriptorSets rewrote a reused set, MoltenVK accessed the freed
    // view → SIGSEGV in MVKCombinedImageSamplerDescriptor::write (si_addr=0x108).
    //
    // vkDestroyDescriptorPool + vkCreateDescriptorPool fixes this: the destructor
    // chain (~MVKDescriptorPool → ~MVKDescriptorTypePool → ~DescriptorClass →
    // reset() → _mvkImageView->release()) properly releases ALL retained
    // MVKImageView objects BEFORE drain_disposal_queue destroys them. The pool
    // is recreated with the same configuration as the original creation (see
    // recreate_descriptor_pool_slot in Pipeline.cpp). Called after the fence
    // wait, so no in-flight command buffer references these sets.
    VkDescriptorPool      descriptorPools[kMaxFramesInFlight] = {};
    std::vector<DescriptorBinding> bindings;  // reflected VS+FS binding set
    bool layoutsBuilt = false;
    // Per-slot descriptor set cache + cursor, used WITHIN a single frame
    // only. reset_descriptor_caches_for_slot() clears allocatedSets[slot],
    // resets setCursor[slot] to 0, and zeroes lastFrameGen[slot] at the
    // start of each frame (after the fence wait). Within the frame,
    // bind_program_descriptors() reuses allocatedSets[slot][cursor++] if
    // available, or vkAllocateDescriptorSets a new set and appends it.
    // The pool's FREE_DESCRIPTOR_SET_BIT flag is retained for compatibility
    // but individual sets are never freed — the pool is destroyed and
    // recreated wholesale (per-frame) and destroyed wholesale on program deletion.
    std::vector<VkDescriptorSet> allocatedSets[kMaxFramesInFlight];
    size_t    setCursor[kMaxFramesInFlight] = {};
    uint64_t  lastFrameGen[kMaxFramesInFlight] = {};
};

// Accessor for the per-program resource table (keyed by GL program name).
std::unordered_map<GLuint, ProgramResources>& program_table();

// Build (or fetch from cache) a VkPipeline for the given configuration.
// All arguments mirror backend_get_or_create_pipeline() in Backend.h.
//   color_write_mask : 4-bit RGBA mask (bit0=R, bit1=G, bit2=B, bit3=A) from
//                      g_state->GetRenderState().GetColorMask(); part of the
//                      pipeline signature so glColorMask changes create
//                      distinct pipelines.
//   blend_src_alpha / blend_dst_alpha : GL blend factors for the alpha
//                      channel (independent from RGB); part of the signature.
VkPipeline get_or_create_pipeline(GLuint program,
                                  const uint32_t* vertex_spirv, int vertex_word_count,
                                  const uint32_t* fragment_spirv, int fragment_word_count,
                                  const ::MGVertexAttrib* attribs, int attrib_count,
                                  const VkFormat* color_formats, int color_count,
                                  VkFormat depth_format,
                                  int blend_enabled, GLenum blend_src, GLenum blend_dst,
                                  GLenum blend_src_alpha, GLenum blend_dst_alpha,
                                  int color_write_mask,
                                  GLenum gl_primitive_mode);

// Release all Vulkan resources owned by a program (shader modules + pipelines).
void delete_program_resources(GLuint program);

// FIX (红屏根因): 清除所有 program 的 failedSignatures 负缓存 + 销毁所有
// 已创建的 VkPipeline。在 backend_reset_device_lost() 时调用，让着色器在
// 设备恢复后有机会重新编译。deviceLost 期间 MoltenVK 着色器缓存可能损坏，
// 不清除会导致 draw 永久跳过 → 红屏/黑屏。
void clear_all_pipeline_caches();

// Per-frame per-slot descriptor pool destroy+recreate. Called from
// ensure_command_buffer_recording() AFTER the fence wait but BEFORE
// drain_disposal_queue(). Destroys and recreates the descriptor pool for the
// given slot across ALL programs, freeing every VkDescriptorSet allocated from
// it. This eliminates dangling VkImageView references in cached descriptor sets,
// so the subsequent drain_disposal_queue() can safely destroy views without
// triggering a use-after-free in MoltenVK's MVKDescriptorSet::write.
//
// WHY destroy+recreate instead of vkResetDescriptorPool: MoltenVK v1.2.9's
// vkResetDescriptorPool does NOT release retained MVKImageView objects — it
// only resets availability bits, deferring release to allocateDescriptor's
// "clear before reusing" path. The retained views are only released when
// allocateDescriptor() calls mvkDesc->reset() — which happens AFTER
// drain_disposal_queue has already destroyed the views via vkDestroyImageView.
// The previous cursor-rewind + set-reuse approach (see comment above
// descriptorPools[]) and later vkResetDescriptorPool kept descriptor sets /
// retained views alive across frames. Those sets referenced VkImageViews that
// drain_disposal_queue destroys — a Vulkan spec violation (vkDestroyImageView
// while a descriptor set references the view). vkUpdateDescriptorSets on a
// reused set then accessed the freed view → SIGSEGV in
// MVKCombinedImageSamplerDescriptor::write (si_addr=0x108).
//
// vkDestroyDescriptorPool + vkCreateDescriptorPool fixes this: the destructor
// chain (~MVKDescriptorPool → ~MVKDescriptorTypePool → ~DescriptorClass →
// reset() → _mvkImageView->release()) releases ALL retained MVKImageView
// objects BEFORE drain_disposal_queue destroys them. The fence wait guarantees
// no in-flight command buffer references these sets.
void reset_descriptor_caches_for_slot(int slot);

// FIX (MVKImageView UAF - 销毁顺序不变式):
// 重置所有 program × 所有 slot 的描述符池（销毁 + 重建），触发 MoltenVK
// 析构链释放所有 retained MVKImageView 引用。供 backend_reset_device_lost_pending_resources
// 等路径在 drain_all_disposal_queues() 之前调用，以满足 "drain 前先释放 retained
// 视图" 的销毁顺序不变式（详见 reset_descriptor_caches_for_slot 上方注释）。
// 与 clear_all_pipeline_caches() 区别：本函数仅重置描述符池，不动管线缓存。
void reset_all_descriptor_caches();

} // namespace vk
} // namespace mithril

#endif // MITHRIL_DIRECTVULKAN_PIPELINE_H
