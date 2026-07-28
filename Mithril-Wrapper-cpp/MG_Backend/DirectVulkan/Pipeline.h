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
    // VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT. bind_program_descriptors()
    // reuses cached sets via cursor rewind (see allocatedSets/setCursor below)
    // instead of vkResetDescriptorPool — the latter requires all in-flight sets
    // to be complete (VUID-vkResetDescriptorPool-descriptorPool-00313), and even
    // though ensure_command_buffer_recording() waits on the slot's fence before
    // we touch the pool, avoiding reset entirely is more robust against any
    // future code path that might allocate mid-frame. This mirrors MobileGL's
    // UniformManager (UniformManager.cpp:183,1026), which also avoids
    // vkResetDescriptorPool in favor of cursor rewind + set reuse.
    VkDescriptorPool      descriptorPools[kMaxFramesInFlight] = {};
    std::vector<DescriptorBinding> bindings;  // reflected VS+FS binding set
    bool layoutsBuilt = false;
    // Per-slot cached descriptor sets + rewind cursor. At the start of each
    // frame (detected via lastFrameGen != b->frameGeneration), the cursor is
    // rewound to 0. Subsequent draws in that frame reuse
    // allocatedSets[slot][cursor++] if available, or vkAllocateDescriptorSets
    // a new set and appends it. The pool's FREE_DESCRIPTOR_SET_BIT flag keeps
    // the option open to vkFreeDescriptorSets a destroyed layout's cached
    // sets, though in practice we never free individual sets — the pool is
    // destroyed wholesale on program deletion (which implicitly frees all
    // sets allocated from it). Rewind is safe because it runs after the
    // slot's fence wait in ensure_command_buffer_recording(), so no in-flight
    // command buffer references the reused sets.
    std::vector<VkDescriptorSet> allocatedSets[kMaxFramesInFlight];
    size_t    setCursor[kMaxFramesInFlight] = {};
    uint64_t  lastFrameGen[kMaxFramesInFlight] = {};
};

// Accessor for the per-program resource table (keyed by GL program name).
std::unordered_map<GLuint, ProgramResources>& program_table();

// Build (or fetch from cache) a VkPipeline for the given configuration.
// All arguments mirror backend_get_or_create_pipeline() in Backend.h.
//   color_write_mask : 4-bit RGBA mask (bit0=R, bit1=G, bit2=B, bit3=A) from
//                      g_state->colorMask; part of the pipeline signature so
//                      glColorMask changes create distinct pipelines.
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

} // namespace vk
} // namespace mithril

#endif // MITHRIL_DIRECTVULKAN_PIPELINE_H
