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
    // One pool per frame-in-flight slot. bind_program_descriptors() resets
    // descriptorPools[b->currentFrame] on the first draw of a new frameGeneration.
    // This is safe because ensure_command_buffer_recording() has already waited
    // on frameFences[currentFrame] — guaranteeing the GPU work (and MoltenVK's
    // Metal encoding of descriptor references) submitted to this slot
    // kMaxFramesInFlight frames ago is complete. A single shared pool reset
    // across all slots was a UAF: it invalidated descriptor sets still
    // referenced by the just-submitted slot's in-flight command buffer,
    // crashing MoltenVK's Metal encoder in
    // MVKGraphicsResourcesCommandEncoderState::encodeImpl at the next
    // vkEndCommandBuffer (SIGSEGV in objc_msgSend on a zombie
    // MTLBuffer/MTLTexture).
    VkDescriptorPool      descriptorPools[kMaxFramesInFlight] = {};
    std::vector<DescriptorBinding> bindings;  // reflected VS+FS binding set
    bool layoutsBuilt = false;
    // Per-slot monotonic frame-generation value at which each slot's pool was
    // last reset. A program drawn only on alternate frames still resets the
    // correct slot's pool on its next use (the per-slot gen lags the global
    // gen until the slot is actually revisited).
    uint64_t lastResetGen[kMaxFramesInFlight] = {};
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
