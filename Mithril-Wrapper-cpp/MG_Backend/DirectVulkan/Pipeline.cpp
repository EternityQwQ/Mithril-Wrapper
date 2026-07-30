// Mithril-Wrapper - MG_Backend/DirectVulkan/Pipeline.cpp
// VkShaderModule creation (from SPIR-V) + VkGraphicsPipeline caching keyed by
// a hash signature built from (program, vertex format, attachment formats,
// blend state, primitive mode). Uses VK_KHR_dynamic_rendering so pipelines
// are created against a VkPipelineRenderingCreateInfo instead of a VkRenderPass.
#include "Pipeline.h"
#include "Device.h"
#include "Resources.h"
#include "DescriptorSet.h"
#include "../Backend.h"
#include "../../MG_Impl/Log.h"

#include <cstring>
#include <vector>

#include <spirv_cross.hpp>
// spirv_cross.hpp transitively pulls in SPIRV-Cross's bundled spirv.hpp,
// which defines the spv:: namespace (spv::DecorationLocation, etc.) used by
// reflect_vertex_input_locations below.

namespace mithril {
namespace vk {

std::unordered_map<GLuint, ProgramResources>& program_table() {
    static std::unordered_map<GLuint, ProgramResources> t;
    return t;
}

namespace {

// ---- GL blend factor -> VkBlendFactor ----
VkBlendFactor gl_blend_to_vk(GLenum f) {
    switch (f) {
        case GL_ZERO:                     return VK_BLEND_FACTOR_ZERO;
        case GL_ONE:                      return VK_BLEND_FACTOR_ONE;
        case GL_SRC_COLOR:                return VK_BLEND_FACTOR_SRC_COLOR;
        case GL_ONE_MINUS_SRC_COLOR:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case GL_DST_COLOR:                return VK_BLEND_FACTOR_DST_COLOR;
        case GL_ONE_MINUS_DST_COLOR:      return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case GL_SRC_ALPHA:                return VK_BLEND_FACTOR_SRC_ALPHA;
        case GL_ONE_MINUS_SRC_ALPHA:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case GL_DST_ALPHA:                return VK_BLEND_FACTOR_DST_ALPHA;
        case GL_ONE_MINUS_DST_ALPHA:      return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case GL_CONSTANT_COLOR:           return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case GL_ONE_MINUS_CONSTANT_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case GL_CONSTANT_ALPHA:           return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case GL_ONE_MINUS_CONSTANT_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        case GL_SRC_ALPHA_SATURATE:       return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case GL_SRC1_COLOR:               return VK_BLEND_FACTOR_SRC1_COLOR;
        case GL_ONE_MINUS_SRC1_COLOR:     return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
        case GL_SRC1_ALPHA:               return VK_BLEND_FACTOR_SRC1_ALPHA;
        case GL_ONE_MINUS_SRC1_ALPHA:     return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
        default:                          return VK_BLEND_FACTOR_ONE;
    }
}

// ---- GL primitive mode -> VkPrimitiveTopology ----
VkPrimitiveTopology gl_prim_to_vk(GLenum m) {
    switch (m) {
        case GL_POINTS:         return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case GL_LINES:          return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case GL_LINE_STRIP:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case GL_LINE_LOOP:      return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; // approx
        case GL_TRIANGLES:      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case GL_TRIANGLE_STRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case GL_TRIANGLE_FAN:   return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        default:                return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

// ---- GL attribute type -> VkFormat ----
VkFormat attrib_type_to_vk_format(GLenum type, int size, bool normalized, bool integer) {
    if (integer) {
        switch (type) {
            case GL_UNSIGNED_BYTE:  switch (size) { case 1: return VK_FORMAT_R8_UINT;   case 2: return VK_FORMAT_R8G8_UINT;   case 3: return VK_FORMAT_R8G8B8_UINT;   case 4: return VK_FORMAT_R8G8B8A8_UINT; }
            case GL_INT:            switch (size) { case 1: return VK_FORMAT_R32_SINT;  case 2: return VK_FORMAT_R32G32_SINT; case 3: return VK_FORMAT_R32G32B32_SINT; case 4: return VK_FORMAT_R32G32B32A32_SINT; }
            case GL_UNSIGNED_INT:   switch (size) { case 1: return VK_FORMAT_R32_UINT;  case 2: return VK_FORMAT_R32G32_UINT; case 3: return VK_FORMAT_R32G32B32_UINT; case 4: return VK_FORMAT_R32G32B32A32_UINT; }
            case GL_SHORT:          switch (size) { case 1: return VK_FORMAT_R16_SINT;  case 2: return VK_FORMAT_R16G16_SINT; case 3: return VK_FORMAT_R16G16B16_SINT; case 4: return VK_FORMAT_R16G16B16A16_SINT; }
            case GL_UNSIGNED_SHORT: switch (size) { case 1: return VK_FORMAT_R16_UINT;  case 2: return VK_FORMAT_R16G16_UINT; case 3: return VK_FORMAT_R16G16B16_UINT; case 4: return VK_FORMAT_R16G16B16A16_UINT; }
            default: break;
        }
    }
    switch (type) {
        case GL_FLOAT:          switch (size) { case 1: return VK_FORMAT_R32_SFLOAT;   case 2: return VK_FORMAT_R32G32_SFLOAT;   case 3: return VK_FORMAT_R32G32B32_SFLOAT;   case 4: return VK_FORMAT_R32G32B32A32_SFLOAT; }
        case GL_HALF_FLOAT:     switch (size) { case 1: return VK_FORMAT_R16_SFLOAT;   case 2: return VK_FORMAT_R16G16_SFLOAT;   case 3: return VK_FORMAT_R16G16B16_SFLOAT;   case 4: return VK_FORMAT_R16G16B16A16_SFLOAT; }
        case GL_DOUBLE:         switch (size) { case 1: return VK_FORMAT_R64_SFLOAT;   case 2: return VK_FORMAT_R64G64_SFLOAT;  case 3: return VK_FORMAT_R64G64B64_SFLOAT;  case 4: return VK_FORMAT_R64G64B64A64_SFLOAT; }
        case GL_UNSIGNED_BYTE:
            if (normalized) switch (size) { case 1: return VK_FORMAT_R8_UNORM;  case 2: return VK_FORMAT_R8G8_UNORM;  case 3: return VK_FORMAT_R8G8B8_UNORM;  case 4: return VK_FORMAT_R8G8B8A8_UNORM; }
            else            switch (size) { case 1: return VK_FORMAT_R8_UINT;   case 2: return VK_FORMAT_R8G8_UINT;   case 3: return VK_FORMAT_R8G8B8_UINT;   case 4: return VK_FORMAT_R8G8B8A8_UINT; }
        case GL_BYTE:
            if (normalized) switch (size) { case 1: return VK_FORMAT_R8_SNORM;  case 2: return VK_FORMAT_R8G8_SNORM;  case 3: return VK_FORMAT_R8G8B8_SNORM;  case 4: return VK_FORMAT_R8G8B8A8_SNORM; }
            else            switch (size) { case 1: return VK_FORMAT_R8_SINT;   case 2: return VK_FORMAT_R8G8_SINT;   case 3: return VK_FORMAT_R8G8B8_SINT;   case 4: return VK_FORMAT_R8G8B8A8_SINT; }
        case GL_UNSIGNED_SHORT:
            if (normalized) switch (size) { case 1: return VK_FORMAT_R16_UNORM; case 2: return VK_FORMAT_R16G16_UNORM; case 3: return VK_FORMAT_R16G16B16_UNORM; case 4: return VK_FORMAT_R16G16B16A16_UNORM; }
            else            switch (size) { case 1: return VK_FORMAT_R16_UINT;  case 2: return VK_FORMAT_R16G16_UINT;  case 3: return VK_FORMAT_R16G16B16_UINT;  case 4: return VK_FORMAT_R16G16B16A16_UINT; }
        case GL_SHORT:
            if (normalized) switch (size) { case 1: return VK_FORMAT_R16_SNORM; case 2: return VK_FORMAT_R16G16_SNORM; case 3: return VK_FORMAT_R16G16B16_SNORM; case 4: return VK_FORMAT_R16G16B16A16_SNORM; }
            else            switch (size) { case 1: return VK_FORMAT_R16_SINT;  case 2: return VK_FORMAT_R16G16_SINT;  case 3: return VK_FORMAT_R16G16B16_SINT;  case 4: return VK_FORMAT_R16G16B16A16_SINT; }
        case GL_INT_2_10_10_10_REV:
            if (normalized) return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
            return VK_FORMAT_A2B10G10R10_UINT_PACK32;
        case GL_UNSIGNED_INT_2_10_10_10_REV:
            if (normalized) return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
            return VK_FORMAT_A2B10G10R10_UINT_PACK32;
        default: break;
    }
    return VK_FORMAT_R32G32B32A32_SFLOAT;
}

// GL compare func -> VkCompareOp
VkCompareOp gl_compare_to_vk(GLenum f) {
    switch (f) {
        case GL_NEVER:    return VK_COMPARE_OP_NEVER;
        case GL_LESS:     return VK_COMPARE_OP_LESS;
        case GL_EQUAL:    return VK_COMPARE_OP_EQUAL;
        case GL_LEQUAL:   return VK_COMPARE_OP_LESS_OR_EQUAL;
        case GL_GREATER:  return VK_COMPARE_OP_GREATER;
        case GL_NOTEQUAL: return VK_COMPARE_OP_NOT_EQUAL;
        case GL_GEQUAL:   return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case GL_ALWAYS:   return VK_COMPARE_OP_ALWAYS;
        default:          return VK_COMPARE_OP_LESS;
    }
}

// FNV-1a 64-bit hash over the pipeline signature.
uint64_t hash_signature(GLuint program, const MGVertexAttrib* attribs, int attrib_count,
                        const VkFormat* color_formats, int color_count,
                        VkFormat depth_format, int blend_enabled,
                        GLenum blend_src, GLenum blend_dst,
                        GLenum blend_src_alpha, GLenum blend_dst_alpha,
                        int color_write_mask, GLenum prim) {
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&](const void* p, size_t n) {
        const uint8_t* b = (const uint8_t*)p;
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; }
    };
    mix(&program, sizeof(program));
    mix(&attrib_count, sizeof(attrib_count));
    for (int i = 0; i < attrib_count; ++i) {
        mix(&attribs[i].location, sizeof(attribs[i].location));
        mix(&attribs[i].size, sizeof(attribs[i].size));
        mix(&attribs[i].type, sizeof(attribs[i].type));
        mix(&attribs[i].normalized, sizeof(attribs[i].normalized));
        mix(&attribs[i].integer, sizeof(attribs[i].integer));
        mix(&attribs[i].stride, sizeof(attribs[i].stride));
    }
    mix(&color_count, sizeof(color_count));
    for (int i = 0; i < color_count; ++i) mix(&color_formats[i], sizeof(color_formats[i]));
    mix(&depth_format, sizeof(depth_format));
    mix(&blend_enabled, sizeof(blend_enabled));
    mix(&blend_src, sizeof(blend_src));
    mix(&blend_dst, sizeof(blend_dst));
    mix(&blend_src_alpha, sizeof(blend_src_alpha));
    mix(&blend_dst_alpha, sizeof(blend_dst_alpha));
    mix(&color_write_mask, sizeof(color_write_mask));
    mix(&prim, sizeof(prim));
    return h;
}

VkShaderModule create_module(const uint32_t* spirv, int word_count) {
    if (!spirv || word_count <= 0) return VK_NULL_HANDLE;
    Backend* b = backend();
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = (size_t)word_count * sizeof(uint32_t);
    ci.pCode = spirv;
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(b->device, &ci, nullptr, &mod) != VK_SUCCESS) {
        // FIX (P2): 限流日志，防止显存不足时大量着色器编译失败刷屏
        static int shaderFailCount = 0;
        shaderFailCount++;
        if (shaderFailCount <= 3 || shaderFailCount % 50 == 0) {
            MITHRIL_LOG_WARN("vk", "vkCreateShaderModule failed (words=%d, fail #%d)",
                              word_count, shaderFailCount);
        }
        return VK_NULL_HANDLE;
    }
    return mod;
}

// Process-wide empty VkPipelineLayout used as a fallback for programs whose
// SPIR-V reflects no descriptor bindings (e.g. vertex-only / pass-through
// shaders). Created lazily on first use.
VkPipelineLayout empty_pipeline_layout() {
    Backend* b = backend();
    static VkPipelineLayout layout = VK_NULL_HANDLE;
    if (layout == VK_NULL_HANDLE) {
        VkPipelineLayoutCreateInfo plci{};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        vkCreatePipelineLayout(b->device, &plci, nullptr, &layout);
    }
    return layout;
}

// Reflect all vertex-shader input locations from SPIR-V via SPIRV-Cross.
// Returns the set of locations the vertex shader declares as stage inputs
// (whether or not GL has enabled a corresponding vertex attrib). Used by
// get_or_create_pipeline to emit dummy VkVertexInputAttributeDescription
// entries for un-enabled locations so SPIRV-Cross generates [[attribute(N)]]
// for every stage_in field — Metal requires this and the MSL compiler rejects
// a [[stage_in]] struct whose fields lack [[attribute(N)]] with
// "invalid type ... of input declaration with attribute 'stage_in'".
std::vector<uint32_t> reflect_vertex_input_locations(const uint32_t* spirv, int words) {
    std::vector<uint32_t> out;
    if (!spirv || words <= 0) return out;
    try {
        spirv_cross::Compiler compiler(spirv, static_cast<size_t>(words));
        spirv_cross::ShaderResources res = compiler.get_shader_resources();
        for (auto& r : res.stage_inputs) {
            uint32_t loc = compiler.get_decoration(r.id, spv::DecorationLocation);
            out.push_back(loc);
        }
    } catch (const std::exception&) {
        // Malformed SPIR-V — return empty; pipeline creation will fail later.
    }
    return out;
}

} // namespace

VkPipeline get_or_create_pipeline(GLuint program,
                                  const uint32_t* vertex_spirv, int vertex_word_count,
                                  const uint32_t* fragment_spirv, int fragment_word_count,
                                  const MGVertexAttrib* attribs, int attrib_count,
                                  const VkFormat* color_formats, int color_count,
                                  VkFormat depth_format,
                                  int blend_enabled, GLenum blend_src, GLenum blend_dst,
                                  GLenum blend_src_alpha, GLenum blend_dst_alpha,
                                  int color_write_mask,
                                  GLenum gl_primitive_mode) {
    Backend* b = backend();
    if (!b->initialized || program == 0) return VK_NULL_HANDLE;
    if (b->deviceLost) return VK_NULL_HANDLE;  // deviceLost 时 vkCreateGraphicsPipelines 必然失败,短路避免无效调用

    auto& tbl = program_table();
    ProgramResources& pr = tbl[program];

    // (Re)build shader modules if missing.
    if (pr.vertexModule == VK_NULL_HANDLE && vertex_spirv && vertex_word_count > 0) {
        pr.vertexModule = create_module(vertex_spirv, vertex_word_count);
    }
    if (pr.fragmentModule == VK_NULL_HANDLE && fragment_spirv && fragment_word_count > 0) {
        pr.fragmentModule = create_module(fragment_spirv, fragment_word_count);
    }
    if (pr.vertexModule == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    // Reflect SPIR-V + build VkDescriptorSetLayout / VkPipelineLayout /
    // VkDescriptorPool once per program (idempotent). The pipeline below binds
    // against pr.pipelineLayout (or the empty fallback for binding-less
    // shaders), and prepare_draw later calls backend_bind_program_descriptors
    // to populate the descriptor set from Program.uniforms + bound textures.
    ensure_program_layouts(program, vertex_spirv, vertex_word_count,
                           fragment_spirv, fragment_word_count);

    uint64_t sig = hash_signature(program, attribs, attrib_count, color_formats,
                                  color_count, depth_format, blend_enabled,
                                  blend_src, blend_dst,
                                  blend_src_alpha, blend_dst_alpha,
                                  color_write_mask, gl_primitive_mode);
    auto it = pr.pipelines.find(sig);
    if (it != pr.pipelines.end() && it->second != VK_NULL_HANDLE) return it->second;

    // Negative cache: skip the creation attempt entirely for signatures that
    // have already failed. This avoids re-running vkCreateGraphicsPipelines
    // (and re-logging the warning) on every draw call for a persistently-
    // broken shader/format combo. The set is cleared on program relink
    // (delete_program_resources), so a fixed shader recovers on the next
    // glLinkProgram. Without this, a single broken shader producing 100
    // draws/frame would generate 100 WARN lines/frame = 6000/sec at 60 FPS.
    if (pr.failedSignatures.count(sig)) {
        return VK_NULL_HANDLE;
    }

    // ---- Vertex input state ----
    std::vector<VkVertexInputBindingDescription> bindDescs;
    std::vector<VkVertexInputAttributeDescription> attrDescs;
    // Group attributes by their backing buffer name (one binding per VBO).
    // Simplified: one binding per attribute (binding index == location).
    for (int i = 0; i < attrib_count; ++i) {
        const MGVertexAttrib& a = attribs[i];
        if (!a.enabled) continue;
        VkVertexInputBindingDescription bd{};
        bd.binding = (uint32_t)a.location;
        bd.stride = a.stride > 0 ? (uint32_t)a.stride : 0;
        bd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        bindDescs.push_back(bd);

        VkVertexInputAttributeDescription ad{};
        ad.location = (uint32_t)a.location;
        ad.binding = (uint32_t)a.location;
        ad.format = attrib_type_to_vk_format(a.type, a.size, a.normalized != 0, a.integer != 0);
        ad.offset = (uint32_t)a.offset;
        attrDescs.push_back(ad);
    }

    // Reflect all vertex-shader input locations. SPIRV-Cross (via MoltenVK)
    // generates [[attribute(N)]] for every stage_in field based on the
    // VkVertexInputAttributeDescription array. Metal requires every field of
    // a [[stage_in]] struct to carry [[attribute(N)]] — if a shader-declared
    // location has no matching attribute description, Metal compilation fails.
    // For each shader location GL has NOT enabled, append a dummy binding +
    // attribute (stride 0, format R32G32B32A32_SFLOAT, offset 0) backed by
    // backend_get_zero_buffer() at draw time (see Drawing.cpp).
    std::vector<uint32_t> shaderLocations =
        reflect_vertex_input_locations(vertex_spirv, vertex_word_count);
    for (uint32_t loc : shaderLocations) {
        bool alreadyEnabled = false;
        for (int i = 0; i < attrib_count; ++i) {
            if (attribs[i].enabled && (uint32_t)attribs[i].location == loc) {
                alreadyEnabled = true;
                break;
            }
        }
        if (alreadyEnabled) continue;

        VkVertexInputBindingDescription bd{};
        bd.binding = loc;
        bd.stride = 0;
        bd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        bindDescs.push_back(bd);

        VkVertexInputAttributeDescription ad{};
        ad.location = loc;
        ad.binding = loc;
        ad.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        ad.offset = 0;
        attrDescs.push_back(ad);
    }

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = (uint32_t)bindDescs.size();
    vertexInput.pVertexBindingDescriptions = bindDescs.data();
    vertexInput.vertexAttributeDescriptionCount = (uint32_t)attrDescs.size();
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

    // ---- Input assembly ----
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = gl_prim_to_vk(gl_primitive_mode);
    ia.primitiveRestartEnable = VK_FALSE;

    // ---- Viewport / scissor (dynamic) ----
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.pViewports = nullptr;   // dynamic
    vp.scissorCount = 1;
    vp.pScissors = nullptr;    // dynamic

    // ---- Rasterizer ----
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.depthClampEnable = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;        // dynamic via vkCmdSetCullMode
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; // dynamic
    rs.depthBiasEnable = VK_FALSE;
    rs.lineWidth = 1.0f;

    // ---- Multisample ----
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    ms.minSampleShading = 1.0f;

    // ---- Depth / stencil ----
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;            // dynamic compare op + write mask
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;  // dynamic
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;

    // ---- Color blend ----
    // FIX (root cause I+J): use independent alpha blend factors (blend_src_alpha/
    // blend_dst_alpha) and apply the GL colorWriteMask (resolved by the caller
    // via g_state->GetRenderState().GetColorMask() and passed in as
    // color_write_mask) instead of hardcoding RGBA all-on. glColorMask now
    // takes effect via the pipeline signature (color_write_mask is hashed
    // above), so depth-only pre-passes that disable color writes get a distinct
    // pipeline that does not corrupt the color buffer.
    VkPipelineColorBlendAttachmentState cbAttach{};
    cbAttach.blendEnable = blend_enabled ? VK_TRUE : VK_FALSE;
    if (blend_enabled) {
        cbAttach.srcColorBlendFactor = gl_blend_to_vk(blend_src);
        cbAttach.dstColorBlendFactor = gl_blend_to_vk(blend_dst);
        cbAttach.colorBlendOp = VK_BLEND_OP_ADD;
        cbAttach.srcAlphaBlendFactor = gl_blend_to_vk(blend_src_alpha);
        cbAttach.dstAlphaBlendFactor = gl_blend_to_vk(blend_dst_alpha);
        cbAttach.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkColorComponentFlags cwm = 0;
    if (color_write_mask & 1) cwm |= VK_COLOR_COMPONENT_R_BIT;
    if (color_write_mask & 2) cwm |= VK_COLOR_COMPONENT_G_BIT;
    if (color_write_mask & 4) cwm |= VK_COLOR_COMPONENT_B_BIT;
    if (color_write_mask & 8) cwm |= VK_COLOR_COMPONENT_A_BIT;
    cbAttach.colorWriteMask = cwm;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.logicOpEnable = VK_FALSE;
    cb.attachmentCount = color_count > 0 ? (uint32_t)color_count : 1;
    cb.pAttachments = &cbAttach;

    // ---- Dynamic state ----
    // VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT requires the
    // VK_EXT_color_write_enable extension which we do not enable; colour
    // write is therefore part of the pipeline's blend attachment state
    // (cbAttach.colorWriteMask above). The extended-dynamic-state extension
    // we DO enable covers cull/front-face/depth-test/depth-write/depth-compare.
    VkDynamicState dynStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_CULL_MODE,
        VK_DYNAMIC_STATE_FRONT_FACE,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
        VK_DYNAMIC_STATE_BLEND_CONSTANTS,
    };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = (uint32_t)(sizeof(dynStates) / sizeof(dynStates[0]));
    dyn.pDynamicStates = dynStates;

    // ---- Shader stages ----
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    VkPipelineShaderStageCreateInfo vsStage{};
    vsStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vsStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vsStage.module = pr.vertexModule;
    vsStage.pName = "main";
    stages.push_back(vsStage);
    if (pr.fragmentModule != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo fsStage{};
        fsStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fsStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fsStage.module = pr.fragmentModule;
        fsStage.pName = "main";
        stages.push_back(fsStage);
    }

    // ---- Dynamic rendering attachment info (Vulkan 1.2 + VK_KHR_dynamic_rendering) ----
    VkFormat colorFmts[8] = {};
    for (int i = 0; i < color_count && i < 8; ++i) colorFmts[i] = color_formats[i];
    VkPipelineRenderingCreateInfo renderingCI{};
    renderingCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingCI.colorAttachmentCount = color_count > 0 ? (uint32_t)color_count : 1;
    renderingCI.pColorAttachmentFormats = colorFmts;
    renderingCI.depthAttachmentFormat = depth_format;
    // FIX (root cause O): For packed depth-stencil formats (D32_SFLOAT_S8_UINT,
    // D24_UNORM_S8_UINT), the stencil attachment format MUST match the depth
    // format. begin_render_pass() binds the SAME VkImageView (depthView, which
    // has aspect DEPTH|STENCIL) as both pDepthAttachment and pStencilAttachment.
    // If the pipeline declares stencilAttachmentFormat = VK_FORMAT_UNDEFINED
    // while the render pass provides a stencil attachment, this is a
    // pipeline/render-pass incompatibility — MoltenVK may silently drop the
    // entire draw or fail to compile the Metal stencil state, producing a
    // black screen. MobileGL sets stencilAttachmentFormat = depth_format for
    // packed D32S8/D24S8 formats. For depth-only formats (D32_SFLOAT,
    // D16_UNORM) there is no stencil aspect, so UNDEFINED is correct.
    if (depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
        depth_format == VK_FORMAT_D24_UNORM_S8_UINT ||
        depth_format == VK_FORMAT_D16_UNORM_S8_UINT ||
        depth_format == VK_FORMAT_S8_UINT) {
        renderingCI.stencilAttachmentFormat = depth_format;
    } else {
        renderingCI.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
    }

    // ---- Graphics pipeline ----
    VkGraphicsPipelineCreateInfo gi{};
    gi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gi.pNext = &renderingCI;
    gi.stageCount = (uint32_t)stages.size();
    gi.pStages = stages.data();
    gi.pVertexInputState = &vertexInput;
    gi.pInputAssemblyState = &ia;
    gi.pViewportState = &vp;
    gi.pRasterizationState = &rs;
    gi.pMultisampleState = &ms;
    gi.pDepthStencilState = &ds;
    gi.pColorBlendState = &cb;
    gi.pDynamicState = &dyn;
    gi.renderPass = VK_NULL_HANDLE;
    gi.subpass = 0;

    // Pipeline layout: use the program's reflected layout (built by
    // ensure_program_layouts above) so UBO / sampled-image bindings declared in
    // the SPIR-V are visible to the shader. Programs with no descriptor
    // bindings (reflection produced an empty set) fall back to the
    // process-wide empty layout so vkCreateGraphicsPipelines still succeeds.
    gi.layout = (pr.pipelineLayout != VK_NULL_HANDLE) ? pr.pipelineLayout
                                                      : empty_pipeline_layout();

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult r = vkCreateGraphicsPipelines(b->device, b->pipelineCache, 1, &gi,
                                           nullptr, &pipeline);
    if (r != VK_SUCCESS) {
        // FIX (红屏根因 - 瞬态失败不可永久缓存):
        // vkCreateGraphicsPipelines 可能在设备处于异常状态时因瞬态原因失败：
        //   VK_ERROR_OUT_OF_HOST_MEMORY       (-1) 主机内存不足
        //   VK_ERROR_OUT_OF_DEVICE_MEMORY     (-2) 显存不足，设备恢复后可成功
        //   VK_ERROR_INITIALIZATION_FAILED    (-3) MoltenVK 着色器库编译失败
        //         （deviceLost 后 MoltenVK 内部 MSL 编译器状态异常）
        //   VK_ERROR_DEVICE_LOST              (-4) 设备丢失，恢复后可成功
        //
        // 这些失败不是着色器本身的永久性缺陷。如果将其加入 failedSignatures
        // 负缓存，即使设备恢复后着色器可以正常编译，draw 也会被永久跳过，
        // 导致物体消失/红屏（只剩 clear color）。
        //
        // 仅当失败是永久性着色器/格式问题时才加入负缓存：
        //   VK_ERROR_INVALID_SHADER_NV        (-1000012000) 着色器本身有缺陷
        //
        // MobileGL 不做负缓存（VulkanRenderer.cpp:4183 直接返回 null），
        // 但那样会导致每帧重试。我们保留负缓存但仅用于永久性失败。
        // 注意：这里不再检查 b->deviceLost。若 b->deviceLost 为真，上方
        // line 233 的早期返回已先行短路返回 VK_NULL_HANDLE，不会执行到此
        // （即原 b->deviceLost 分支为死代码）。但 vkCreateGraphicsPipelines
        // 可能在 b->deviceLost 被 submit/fence 路径置位之前就直接返回
        // VK_ERROR_DEVICE_LOST（设备处于坏状态但 flag 尚未设置），此时必须
        // 显式识别为瞬态；否则 signature 会被永久缓存到 failedSignatures，
        // 导致设备恢复后 draw 仍被永久跳过 → 红屏。
        bool isTransient = (r == VK_ERROR_OUT_OF_DEVICE_MEMORY ||
                           r == VK_ERROR_OUT_OF_HOST_MEMORY ||
                           r == VK_ERROR_INITIALIZATION_FAILED ||
                           r == VK_ERROR_DEVICE_LOST);
        if (!isTransient) {
            // 永久性失败：加入负缓存避免每帧重试刷屏
            pr.failedSignatures.insert(sig);
        }
        // 限流日志：瞬态失败首次 + 每 50 次打印一条；永久性失败首次 + 每 100 次
        static int transientFailCount = 0;
        static int permanentFailCount = 0;
        if (isTransient) {
            transientFailCount++;
            if (transientFailCount <= 3 || transientFailCount % 50 == 0) {
                MITHRIL_LOG_WARN("vk", "vkCreateGraphicsPipelines transient "
                                  "failure (rc=%d, program=%u, deviceLost=%d, "
                                  "fail #%d) — NOT cached, will retry next draw",
                                  (int)r, program, (int)b->deviceLost,
                                  transientFailCount);
            }
        } else {
            permanentFailCount++;
            if (permanentFailCount <= 3 || permanentFailCount % 100 == 0) {
                MITHRIL_LOG_WARN("vk", "vkCreateGraphicsPipelines failed (rc=%d, "
                                  "program=%u, colorCount=%d, colorFmt0=%d, "
                                  "depthFmt=%d, attribs=%d, blend=%d) — draws "
                                  "with this signature will be skipped until "
                                  "program relink (fail #%d)",
                                  (int)r, program, color_count,
                                  color_count > 0 ? (int)color_formats[0] : 0,
                                  (int)depth_format, attrib_count, blend_enabled,
                                  permanentFailCount);
            }
        }
        return VK_NULL_HANDLE;
    }
    pr.pipelines[sig] = pipeline;
    return pipeline;
}

// FIX (红屏根因 - deviceLost 恢复后清除负缓存):
// deviceLost 期间 vkCreateGraphicsPipelines 可能因设备状态异常而失败。
// 如果这些失败被加入 failedSignatures 负缓存，即使设备恢复后着色器
// 可以正常编译，draw 也会被永久跳过，导致物体消失/红屏。
//
// 此函数在 backend_reset_device_lost() 时调用，清除所有 program 的
// failedSignatures，让着色器在设备恢复后有机会重新编译。
// 同时也清除已创建的 pipeline 缓存（pipeline 可能引用了损坏的着色器），
// 让 get_or_create_pipeline 在下次 draw 时重新创建。
//
// 参考 MobileGL RecreateSwapchain（VulkanRenderer.cpp:8579）：
// swapchain 重建时 pipelineFactory->DestroyAll() 销毁全部 pipeline，
// 确保设备恢复后着色器从干净状态重新编译。
void clear_all_pipeline_caches() {
    Backend* b = backend();
    if (!b->device) return;
    auto& tbl = program_table();
    for (auto& kv : tbl) {
        ProgramResources& pr = kv.second;
        // 清除负缓存：让之前因瞬态失败被跳过的签名有机会重试
        pr.failedSignatures.clear();
        // 销毁已创建的 pipeline：deviceLost 后 MoltenVK 的着色器缓存
        // 可能已损坏，重建 pipeline 确保从干净状态编译
        for (auto& pkv : pr.pipelines) {
            if (pkv.second) {
                vkDestroyPipeline(b->device, pkv.second, nullptr);
            }
        }
        pr.pipelines.clear();

        // FIX (UAF in MVKCombinedImageSamplerDescriptor::write):
        // device lost 恢复路径 (backend_reset_device_lost) 在 vkDeviceWaitIdle
        // 之后调用 drain_all_disposal_queues，会销毁 VkImageView。但
        // allocatedSets 里缓存的 descriptor set 仍引用这些 view —— 当下一帧
        // bind_program_descriptors 复用某个 cached set 并通过
        // vkUpdateDescriptorSets 重写它时，MoltenMVk 会解引用旧（已释放）的
        // view → SIGSEGV。
        // 这里 reset 每个 slot 的 descriptor pool（释放其中所有 set，但不
        // 销毁 pool 本身，pool 被复用）并清空缓存，使后续
        // bind_program_descriptors 走 vkAllocateDescriptorSets 分配全新 set，
        // 不再引用已释放的 view。注意：descriptorSetLayout / pipelineLayout
        // 跨恢复保持稳定，不销毁；descriptorPools 本身也不销毁，只 reset。
        // vkResetDescriptorPool 在 vkDeviceWaitIdle 之后调用是安全的（此时
        // 无 in-flight 命令引用这些 set）。
        for (int i = 0; i < kMaxFramesInFlight; ++i) {
            if (pr.descriptorPools[i] != VK_NULL_HANDLE) {
                VkResult rc = vkResetDescriptorPool(b->device,
                                                    pr.descriptorPools[i], 0);
                if (rc != VK_SUCCESS) {
                    MITHRIL_LOG_WARN("vk", "clear_all_pipeline_caches: "
                                     "vkResetDescriptorPool slot=%d failed "
                                     "(rc=%d), continuing", i, rc);
                }
            }
            pr.allocatedSets[i].clear();
            pr.setCursor[i] = 0;
            pr.lastFrameGen[i] = 0;
        }
    }
    MITHRIL_LOG_INFO("vk", "clear_all_pipeline_caches: cleared all "
                      "failedSignatures + destroyed all cached pipelines + "
                      "reset descriptor pools/caches (device recovery)");
}

void reset_descriptor_caches_for_slot(int slot) {
    Backend* b = backend();
    if (!b->device || slot < 0 || slot >= kMaxFramesInFlight) return;
    auto& tbl = program_table();
    for (auto& kv : tbl) {
        ProgramResources& pr = kv.second;
        if (pr.descriptorPools[slot] != VK_NULL_HANDLE) {
            VkResult rc = vkResetDescriptorPool(b->device, pr.descriptorPools[slot], 0);
            if (rc != VK_SUCCESS) {
                static int resetFailLog = 0;
                resetFailLog++;
                if (resetFailLog <= 3 || resetFailLog % 100 == 0) {
                    MITHRIL_LOG_WARN("vk", "vkResetDescriptorPool failed (slot=%d, "
                                      "rc=%d, log %d) — descriptor sets may be stale",
                                      slot, (int)rc, resetFailLog);
                }
            }
        }
        pr.allocatedSets[slot].clear();
        pr.setCursor[slot] = 0;
        pr.lastFrameGen[slot] = 0;
    }
}

void delete_program_resources(GLuint program) {
    Backend* b = backend();
    auto& tbl = program_table();
    auto it = tbl.find(program);
    if (it == tbl.end()) return;
    ProgramResources& pr = it->second;
    // FIX (SIGBUS): glDeleteProgram 可能在帧中间调用（command buffer 正在录制）。
    // 使用 safe_device_wait_idle 安全结束+提交当前 command buffer 后再 wait，
    // 避免 MoltenVK deferred encoding 触发 SIGBUS。
    safe_device_wait_idle();
    // safe_device_wait_idle 保证所有 GPU 工作已完成 — drain 任何之前帧延迟的
    // buffer/texture/sampler 销毁，确保没有过期的 Vulkan handle 比 program 的
    // shader module 存活更久。
    drain_all_disposal_queues();
    for (auto& kv : pr.pipelines) {
        if (kv.second) vkDestroyPipeline(b->device, kv.second, nullptr);
    }
    pr.pipelines.clear();
    if (pr.vertexModule)   { vkDestroyShaderModule(b->device, pr.vertexModule, nullptr);   pr.vertexModule = VK_NULL_HANDLE; }
    if (pr.fragmentModule) { vkDestroyShaderModule(b->device, pr.fragmentModule, nullptr); pr.fragmentModule = VK_NULL_HANDLE; }
    // Descriptor resources built by ensure_program_layouts. Pools must be
    // destroyed before the set layout they were created from (Vulkan ordering);
    // destroying a pool implicitly frees all sets allocated from it, so the
    // per-slot allocatedSets caches just need to be cleared (no per-set
    // vkFreeDescriptorSets — the pool destruction reclaims them wholesale).
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        pr.allocatedSets[i].clear();
        pr.setCursor[i] = 0;
        pr.lastFrameGen[i] = 0;
        if (pr.descriptorPools[i]) {
            vkDestroyDescriptorPool(b->device, pr.descriptorPools[i], nullptr);
            pr.descriptorPools[i] = VK_NULL_HANDLE;
        }
    }
    if (pr.pipelineLayout)      { vkDestroyPipelineLayout(b->device, pr.pipelineLayout, nullptr);      pr.pipelineLayout = VK_NULL_HANDLE; }
    if (pr.descriptorSetLayout) { vkDestroyDescriptorSetLayout(b->device, pr.descriptorSetLayout, nullptr); pr.descriptorSetLayout = VK_NULL_HANDLE; }
    pr.bindings.clear();
    pr.layoutsBuilt = false;
    tbl.erase(it);
}

} // namespace vk
} // namespace mithril

// ===========================================================================
// Public C API wrappers (declared in MG_Backend/Backend.h)
// ===========================================================================
extern "C" {

VkPipeline backend_get_or_create_pipeline(GLuint program,
                                          const uint32_t* vertex_spirv, int vertex_word_count,
                                          const uint32_t* fragment_spirv, int fragment_word_count,
                                          const MGVertexAttrib* attribs, int attrib_count,
                                          const VkFormat* color_formats, int color_count,
                                          VkFormat depth_format,
                                          int blend_enabled, GLenum blend_src, GLenum blend_dst,
                                          GLenum blend_src_alpha, GLenum blend_dst_alpha,
                                          int color_write_mask,
                                          GLenum gl_primitive_mode) {
    return mithril::vk::get_or_create_pipeline(program, vertex_spirv, vertex_word_count,
                                               fragment_spirv, fragment_word_count,
                                               attribs, attrib_count,
                                               color_formats, color_count, depth_format,
                                               blend_enabled, blend_src, blend_dst,
                                               blend_src_alpha, blend_dst_alpha,
                                               color_write_mask,
                                               gl_primitive_mode);
}

void backend_delete_program_resources(GLuint program) {
    mithril::vk::delete_program_resources(program);
}

} // extern "C"
