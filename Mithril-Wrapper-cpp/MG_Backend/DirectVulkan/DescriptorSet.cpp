// Mithril-Wrapper - MG_Backend/DirectVulkan/DescriptorSet.cpp
// SPIR-V reflection (SPIRV-Cross) + VkDescriptorSetLayout / VkPipelineLayout /
// VkDescriptorPool construction + per-frame VkDescriptorSet allocation/write/bind.
//
// Reflection strategy: glslang compiles desktop GLSL with setAutoMapBindings(true),
// so each `uniform` global / `uniform sampler*` gets an auto-assigned Vulkan
// binding. We reflect both VS and FS, merge by (set,binding) OR-ing the stage
// masks, build one VkDescriptorSetLayout, and a VkPipelineLayout referencing it.
//
// UBO data sourcing: a reflected UBO is matched against Program.uniforms. If the
// UBO name matches a uniform directly (glslang emits one UBO per loose uniform)
// we upload that uniform's value. Otherwise (glslang aggregates loose uniforms
// into a single `$Global` block) we pack the block's members by name using the
// member offsets reported by SPIRV-Cross's get_active_buffer_ranges().
//
// Texture sourcing: a reflected combined-image-sampler at binding B is fed from
// g_state->boundTextureForUnit(B) (the GL texture bound to unit B), matching how
// the GL frontend binds textures by unit index.
#include "DescriptorSet.h"
#include "Device.h"
#include "Pipeline.h"
#include "CommandStream.h"  // ensure_command_buffer_recording
#include "Reflect.h"  // reflect_stage / merge_bindings (pure-logic, unit-tested)
#include "Resources.h"  // texture_table() — for TextureEntry::format lookup (root cause AH)
#include "FormatMap.h"  // sampled_layout_for_format (root cause AH)
#include "../Backend.h"
#include "../../MG_State/State.h"
#include "../../MG_Impl/Log.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace mithril {
namespace vk {

void ensure_program_layouts(GLuint program,
                            const uint32_t* vs, int vs_words,
                            const uint32_t* fs, int fs_words) {
    Backend* b = backend();
    if (!b->initialized || program == 0) return;

    auto& tbl = program_table();
    ProgramResources& pr = tbl[program];
    if (pr.layoutsBuilt) return;

    // Reflect + merge both stages.
    pr.bindings.clear();
    pr.bindings = reflect_stage(vs, vs_words, VK_SHADER_STAGE_VERTEX_BIT);
    merge_bindings(pr.bindings, reflect_stage(fs, fs_words, VK_SHADER_STAGE_FRAGMENT_BIT));

    // Deterministic ordering for stable set-layout construction.
    std::sort(pr.bindings.begin(), pr.bindings.end(),
              [](const DescriptorBinding& a, const DescriptorBinding& c) {
                  if (a.set != c.set) return a.set < c.set;
                  return a.binding < c.binding;
              });

    pr.layoutsBuilt = true;  // set early so a reflection failure doesn't retry forever
    if (pr.bindings.empty()) {
        // No descriptors: caller falls back to the process-wide empty layout.
        return;
    }

    // ---- VkDescriptorSetLayout ----
    std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
    layoutBindings.reserve(pr.bindings.size());
    for (const auto& db : pr.bindings) {
        VkDescriptorSetLayoutBinding lb{};
        lb.binding = db.binding;
        lb.descriptorType = db.type;
        lb.descriptorCount = db.descriptorCount;
        lb.stageFlags = db.stageMask;
        lb.pImmutableSamplers = nullptr;
        layoutBindings.push_back(lb);
    }
    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    dslci.pBindings = layoutBindings.data();
    if (vkCreateDescriptorSetLayout(b->device, &dslci, nullptr, &pr.descriptorSetLayout) != VK_SUCCESS) {
        MITHRIL_LOG_WARN("vk", "vkCreateDescriptorSetLayout failed (program %u)", program);
        pr.bindings.clear();
        return;
    }

    // ---- VkPipelineLayout (single set) ----
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &pr.descriptorSetLayout;
    if (vkCreatePipelineLayout(b->device, &plci, nullptr, &pr.pipelineLayout) != VK_SUCCESS) {
        MITHRIL_LOG_WARN("vk", "vkCreatePipelineLayout failed (program %u)", program);
        vkDestroyDescriptorSetLayout(b->device, pr.descriptorSetLayout, nullptr);
        pr.descriptorSetLayout = VK_NULL_HANDLE;
        pr.bindings.clear();
        return;
    }

    // ---- VkDescriptorPool (one per frame-in-flight slot) ----
    // maxSets=256, 256 descriptors per type, PER SLOT. Created with
    // FREE_DESCRIPTOR_SET_BIT so individual sets CAN be freed (useful if a
    // layout is ever destroyed while cached sets survive); in practice we
    // never free individual sets — the pool is destroyed wholesale on program
    // deletion. Per-slot pools prevent the UAF where a shared pool reset
    // invalidated descriptor sets still referenced by an in-flight command
    // buffer on another slot.
    //
    // We do NOT call vkResetDescriptorPool. Instead, bind_program_descriptors
    // rewinds a per-slot cursor at the start of each frame (after the slot's
    // fence wait) and reuses previously allocated VkDescriptorSets. This
    // avoids VUID-vkResetDescriptorPool-descriptorPool-00313 entirely and
    // mirrors MobileGL's UniformManager (UniformManager.cpp:183,1026).
    bool hasUBO = false, hasImg = false;
    for (const auto& db : pr.bindings) {
        if (db.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) hasUBO = true;
        else if (db.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) hasImg = true;
    }
    std::vector<VkDescriptorPoolSize> poolSizes;
    if (hasUBO) {
        VkDescriptorPoolSize ps{};
        ps.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ps.descriptorCount = 1024;
        poolSizes.push_back(ps);
    }
    if (hasImg) {
        VkDescriptorPoolSize ps{};
        ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps.descriptorCount = 1024;
        poolSizes.push_back(ps);
    }
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    // FREE_DESCRIPTOR_SET_BIT lets a destroyed layout's cached sets be freed
    // back to the pool (MobileGL UniformManager.cpp:952 does the same). Even
    // though we currently rely on pool destruction to reclaim all sets, the
    // flag is cheap and keeps the option open for future layout-eviction
    // paths.
    dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    // FIX (descriptor pool exhausted): maxSets 从 256 增大到 1024。Minecraft 1.21.1
    // 每帧可能绑定超过 256 个不同描述符集（多种着色器 + 多个纹理单元），
    // 256 不够用导致 "descriptor pool exhausted" 日志刷屏并跳过 draw。
    // 1024 足够覆盖最复杂的场景，且内存开销可忽略（每个 set 约 100 字节）。
    dpci.maxSets = 1024;
    dpci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    dpci.pPoolSizes = poolSizes.data();
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        if (vkCreateDescriptorPool(b->device, &dpci, nullptr, &pr.descriptorPools[i]) != VK_SUCCESS) {
            MITHRIL_LOG_WARN("vk", "vkCreateDescriptorPool failed (program %u, slot %d)", program, i);
            pr.descriptorPools[i] = VK_NULL_HANDLE;
            // Layout is still valid; bind_program_descriptors skips slots with a null pool.
        }
    }
}

/*
 * Process-wide default 1x1 opaque-black texture used to fill unbound sampler
 * bindings so the descriptor set stays complete. Without this, a shader that
 * declares a sampler but whose GL texture unit is unbound would leave the
 * corresponding descriptor binding unwritten — MoltenVK then samples
 * undefined memory and the geometry renders pure black (root cause L).
 *
 * MobileGL keeps a similar default texture for the same reason. Lazily
 * created on first use; lifetime is the process (never destroyed).
 */
struct DefaultTexture {
    VkImage    image  = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view   = VK_NULL_HANDLE;
    VkSampler  sampler = VK_NULL_HANDLE;
};

DefaultTexture& default_texture() {
    static DefaultTexture dt;
    if (dt.view != VK_NULL_HANDLE && dt.sampler != VK_NULL_HANDLE) return dt;
    Backend* b = backend();
    if (!b->device) return dt;

    // 1x1 R8G8B8A8_UNORM, opaque black (0,0,0,1).
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {1, 1, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(b->device, &ici, nullptr, &dt.image) != VK_SUCCESS) return dt;

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(b->device, dt.image, &req);
    // find_memory_type lives in Resources.cpp.
    uint32_t mt = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < 32; ++i) {
        if ((req.memoryTypeBits >> i) & 1) { mt = i; break; }
    }
    if (mt == 0xFFFFFFFFu) { vkDestroyImage(b->device, dt.image, nullptr); dt.image = VK_NULL_HANDLE; return dt; }

    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = mt;
    if (vkAllocateMemory(b->device, &ai, nullptr, &dt.memory) != VK_SUCCESS) {
        vkDestroyImage(b->device, dt.image, nullptr); dt.image = VK_NULL_HANDLE; return dt;
    }
    vkBindImageMemory(b->device, dt.image, dt.memory, 0);

    // Transition UNDEFINED -> SHADER_READ_ONLY_OPTIMAL and clear to black.
    // Use a one-shot command buffer on the graphics queue.
    VkCommandBuffer cb = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = b->commandPool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(b->device, &cai, &cb) != VK_SUCCESS) return dt;
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    VkImageMemoryBarrier b2{};
    b2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b2.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b2.image = dt.image;
    b2.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b2.subresourceRange.baseMipLevel = 0;
    b2.subresourceRange.levelCount = 1;
    b2.subresourceRange.baseArrayLayer = 0;
    b2.subresourceRange.layerCount = 1;
    b2.srcAccessMask = 0;
    b2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &b2);
    vkEndCommandBuffer(cb);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    VkFence fence;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(b->device, &fci, nullptr, &fence);
    vkQueueSubmit(b->graphicsQueue, 1, &si, fence);
    vkWaitForFences(b->device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(b->device, fence, nullptr);
    vkFreeCommandBuffers(b->device, b->commandPool, 1, &cb);

    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = dt.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.baseMipLevel = 0;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount = 1;
    if (vkCreateImageView(b->device, &vci, nullptr, &dt.view) != VK_SUCCESS) {
        // FIX (显存泄漏): view 创建失败时必须销毁已分配的 image+memory，
        // 否则下次重入 default_texture() 会被 vkCreateImage/vkAllocateMemory
        // 覆盖句柄，导致旧 VkImage+VkDeviceMemory 永久泄漏。每次重试都会
        // 累积一组泄漏，在显存紧张时加剧 OOM。
        vkDestroyImage(b->device, dt.image, nullptr); dt.image = VK_NULL_HANDLE;
        vkFreeMemory(b->device, dt.memory, nullptr); dt.memory = VK_NULL_HANDLE;
        return dt;
    }

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_NEAREST;
    sci.minFilter = VK_FILTER_NEAREST;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.minLod = 0.0f;
    sci.maxLod = 0.0f;
    if (vkCreateSampler(b->device, &sci, nullptr, &dt.sampler) != VK_SUCCESS) {
        // FIX (显存泄漏): sampler 创建失败时同样必须销毁 image+memory+view。
        vkDestroyImageView(b->device, dt.view, nullptr); dt.view = VK_NULL_HANDLE;
        vkDestroyImage(b->device, dt.image, nullptr); dt.image = VK_NULL_HANDLE;
        vkFreeMemory(b->device, dt.memory, nullptr); dt.memory = VK_NULL_HANDLE;
        return dt;
    }
    return dt;
}

void bind_program_descriptors(GLuint program) {
    Backend* b = backend();
    if (!b->initialized || !b->commandBuffer || program == 0) return;
    // Ensure the current slot's command buffer is recording before we issue
    // vkCmdBindDescriptorSets. This is normally a no-op (bind_program_descriptors
    // is called during a render pass, when the buffer is already recording),
    // but the ensure call makes this function safe if called outside a pass.
    if (!ensure_command_buffer_recording()) return;

    auto& tbl = program_table();
    auto it = tbl.find(program);
    if (it == tbl.end()) return;
    ProgramResources& pr = it->second;
    int slot = b->currentFrame;
    if (!pr.layoutsBuilt || pr.bindings.empty() ||
        pr.pipelineLayout == VK_NULL_HANDLE ||
        pr.descriptorPools[slot] == VK_NULL_HANDLE) {
        return;
    }

    mithril::Program* prog = mithril::state_get_program(program);
    if (!prog) return;

    // Per-slot cursor rewind at the start of each frame. ensure_command_buffer
    // _recording() (called above) has ALREADY waited on frameFences[currentFrame]
    // before this point — so the command buffer submitted to this slot
    // kMaxFramesInFlight frames ago, and all of MoltenVK's Metal encoding of
    // its descriptor references, is guaranteed complete on the GPU. Rewinding
    // the cursor is therefore safe: no in-flight command buffer references
    // the sets we are about to reuse.
    //
    // We do NOT call vkResetDescriptorPool (which would invalidate all sets
    // and require re-allocation). Instead, previously allocated sets are
    // reused in order — vkUpdateDescriptorSets below rewrites their contents
    // with the current frame's uniform/texture bindings. This mirrors
    // MobileGL's UniformManager (UniformManager.cpp:183,1026), which also
    // avoids vkResetDescriptorPool in favor of cursor rewind + set reuse,
    // eliminating any VUID-vkResetDescriptorPool-descriptorPool-00313 hazard.
    if (pr.lastFrameGen[slot] != b->frameGeneration) {
        pr.setCursor[slot] = 0;
        pr.lastFrameGen[slot] = b->frameGeneration;
    }

    // Reuse a cached set if available; otherwise allocate a new one and
    // append it to the per-slot cache.
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (pr.setCursor[slot] < pr.allocatedSets[slot].size()) {
        set = pr.allocatedSets[slot][pr.setCursor[slot]++];
    } else {
        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = pr.descriptorPools[slot];
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &pr.descriptorSetLayout;
        if (vkAllocateDescriptorSets(b->device, &dsai, &set) != VK_SUCCESS) {
            // Pool exhausted mid-frame (>1024 distinct sets this frame for this
            // program). FIX: 重置描述符池并重试，而不是跳过 draw。
            // 原实现跳过 draw 会导致渲染残缺，且每帧都会刷屏日志。
            // 重置池会失效已分配的 set，但由于我们在帧开头已经 rewind 了 cursor，
            // 当前帧的 set 都是在本次 rewind 后分配的，重置后重新分配是安全的
            // （前提：当前帧已绑定的 set 不再被后续 draw 使用，因为 pipeline
            // bind 会覆盖之前的 descriptor bind）。
            static int poolExhaustedLogCount = 0;
            poolExhaustedLogCount++;
            if (poolExhaustedLogCount <= 3 || poolExhaustedLogCount % 100 == 0) {
                MITHRIL_LOG_WARN("vk", "vkAllocateDescriptorSets failed (program %u, slot %d, "
                                  "gen %llu) — descriptor pool exhausted; resetting pool "
                                  "(log %d)",
                                  program, slot, (unsigned long long)b->frameGeneration,
                                  poolExhaustedLogCount);
            }
            vkResetDescriptorPool(b->device, pr.descriptorPools[slot], 0);
            pr.allocatedSets[slot].clear();
            pr.setCursor[slot] = 0;
            // 重试分配
            if (vkAllocateDescriptorSets(b->device, &dsai, &set) != VK_SUCCESS) {
                return;  // 重试仍失败，放弃本次 bind
            }
        }
        pr.allocatedSets[slot].push_back(set);
        pr.setCursor[slot]++;
    }

    // Gather descriptor writes. The info vectors are reserved to the binding
    // count so they never reallocate (the VkWriteDescriptorSet structs hold
    // pointers into them).
    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorBufferInfo> bufInfos;
    std::vector<VkDescriptorImageInfo> imgInfos;
    bufInfos.reserve(pr.bindings.size());
    imgInfos.reserve(pr.bindings.size());
    writes.reserve(pr.bindings.size());

    for (const auto& db : pr.bindings) {
        if (db.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            uint32_t sz = db.bufferSize ? db.bufferSize : 16u;
            std::vector<uint8_t> payload(sz, 0);

            // (1) Direct name match: one UBO per loose uniform.
            auto uit = prog->uniforms.find(db.name);
            if (uit != prog->uniforms.end() && !uit->second.value.empty()) {
                size_t bytes = uit->second.value.size() * sizeof(float);
                std::memcpy(payload.data(), uit->second.value.data(),
                            std::min(bytes, static_cast<size_t>(sz)));
            } else {
                // (2) Aggregated block ($Global): pack members by name/offset.
                for (const auto& m : db.members) {
                    auto mit = prog->uniforms.find(m.name);
                    if (mit != prog->uniforms.end() && !mit->second.value.empty()) {
                        size_t bytes = mit->second.value.size() * sizeof(float);
                        size_t n = std::min(bytes, static_cast<size_t>(m.size));
                        if (static_cast<size_t>(m.offset) + n <= payload.size()) {
                            std::memcpy(payload.data() + m.offset,
                                        mit->second.value.data(), n);
                        }
                    }
                }
            }

            // Stable per-(program,binding) GL name so the buffer is reused and
            // its contents updated each frame rather than reallocated.
            GLuint uname = program * 1000000u + db.binding + 1u;
            VkBuffer ubuf = backend_get_or_create_buffer(uname, payload.data(), payload.size());
            if (ubuf != VK_NULL_HANDLE) {
                VkDescriptorBufferInfo bi{};
                bi.buffer = ubuf;
                bi.offset = 0;
                bi.range = VK_WHOLE_SIZE;
                bufInfos.push_back(bi);
                VkWriteDescriptorSet w{};
                w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.dstSet = set;
                w.dstBinding = db.binding;
                w.dstArrayElement = 0;
                w.descriptorCount = 1;
                w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                w.pBufferInfo = &bufInfos.back();
                writes.push_back(w);
            }
        } else if (db.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
            // Sampler binding B maps to GL texture unit B.
            GLuint tex_id = 0;
            if (db.binding < mithril::kMaxTextureUnits) {
                tex_id = mithril::g_state->boundTextureForUnit(db.binding);
            }
            if (!tex_id) {
                // Fallback: first bound texture, so the descriptor stays valid
                // (an unbound sampler binding would leave the set incomplete).
                for (int i = 0; i < mithril::kMaxTextureUnits; ++i) {
                    GLuint t = mithril::g_state->boundTextureForUnit(i);
                    if (t) {
                        tex_id = t;
                        break;
                    }
                }
            }
            VkImageView view = VK_NULL_HANDLE;
            VkSampler samp = VK_NULL_HANDLE;
            if (tex_id) {
                view = backend_get_texture_view(tex_id);
                // FIX (Root Cause M - 采样器参数硬编码): 旧代码硬编码 GL_LINEAR/GL_REPEAT，
                // 完全忽略纹理通过 glTexParameteri 设置的真实 sampler 参数。
                // Minecraft 像素风纹理（GL_NEAREST）被双线性插值，图集纹理
                // （GL_CLAMP_TO_EDGE）被 REPEAT → 采样到错误纹素 → 颜色偏红/花屏。
                // 修复：从 Texture 结构体读取真实 minFilter/magFilter/wrapS/wrapT/wrapR。
                // 采样器按纹理名缓存（Resources.cpp:793-830），首次绑定读取的参数
                // 即为最终参数。Minecraft 通常在纹理创建后立即设置参数并不再修改，
                // 因此无需额外缓存失效逻辑。
                mithril::Texture* tex = mithril::state_get_texture(tex_id);
                GLenum minF = tex ? (GLenum)tex->minFilter : GL_NEAREST_MIPMAP_LINEAR;
                GLenum magF = tex ? (GLenum)tex->magFilter : GL_LINEAR;
                GLenum wrapS = tex ? (GLenum)tex->wrapS : GL_REPEAT;
                GLenum wrapT = tex ? (GLenum)tex->wrapT : GL_REPEAT;
                GLenum wrapR = tex ? (GLenum)tex->wrapR : GL_REPEAT;
                samp = backend_get_or_create_sampler(
                    tex_id, minF, magF, wrapS, wrapT, wrapR, nullptr);
            }
            // FIX (root cause L): if no texture is bound (or the bound texture
            // has no view/sampler), use the process-wide default 1x1 black
            // texture. Without this, the descriptor binding would be left
            // unwritten — MoltenVK then samples undefined memory and the
            // geometry renders pure black. A complete descriptor set with a
            // valid (black) texture is always preferable to an incomplete one.
            if (view == VK_NULL_HANDLE || samp == VK_NULL_HANDLE) {
                DefaultTexture& dt = default_texture();
                if (dt.view != VK_NULL_HANDLE && dt.sampler != VK_NULL_HANDLE) {
                    view = dt.view;
                    samp = dt.sampler;
                }
            }
            if (view != VK_NULL_HANDLE && samp != VK_NULL_HANDLE) {
                // FIX (Root Cause AH - depth-stencil descriptor layout):
                // 旧代码硬编码 imageLayout = SHADER_READ_ONLY_OPTIMAL，对 depth-stencil
                // 纹理不匹配。descriptor 声明的 imageLayout 必须与 image 实际布局一致，
                // 否则 MoltenVK 验证错误或静默丢 draw → 黑屏。
                // 通过 TextureEntry.format 查找纹理的 VkFormat，用 sampled_layout_for_format
                // 选择正确的 read-only 布局（depth-stencil -> DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                // depth-only -> DEPTH_READ_ONLY_OPTIMAL, color -> SHADER_READ_ONLY_OPTIMAL）。
                // 对照 MobileGL ResolveSampledReadOnlyLayout (VkTextureManager.cpp:177)。
                //
                // 注意：当 view 是 default fallback texture (R8G8B8A8_UNORM, color) 时，
                // tex_it->second.view != view，保持 SHADER_READ_ONLY_OPTIMAL（default 是 color 格式）。
                VkImageLayout sampledLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                if (tex_id) {
                    auto& tex_tbl = mithril::vk::texture_table();
                    auto tex_it = tex_tbl.find(tex_id);
                    if (tex_it != tex_tbl.end() && tex_it->second.view == view) {
                        sampledLayout = mithril::vk::sampled_layout_for_format(tex_it->second.format);
                    }
                }
                VkDescriptorImageInfo ii{};
                ii.sampler = samp;
                ii.imageView = view;
                ii.imageLayout = sampledLayout;
                imgInfos.push_back(ii);
                VkWriteDescriptorSet w{};
                w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.dstSet = set;
                w.dstBinding = db.binding;
                w.dstArrayElement = 0;
                w.descriptorCount = 1;
                w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w.pImageInfo = &imgInfos.back();
                writes.push_back(w);
            }
        }
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(b->device, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
    vkCmdBindDescriptorSets(b->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pr.pipelineLayout, 0, 1, &set, 0, nullptr);
}

} // namespace vk
} // namespace mithril

// ===========================================================================
// Public C API wrappers (declared in MG_Backend/Backend.h)
// ===========================================================================
extern "C" {

void backend_ensure_program_layouts(GLuint program,
                                    const uint32_t* vs, int vs_words,
                                    const uint32_t* fs, int fs_words) {
    mithril::vk::ensure_program_layouts(program, vs, vs_words, fs, fs_words);
}

void backend_bind_program_descriptors(GLuint program) {
    mithril::vk::bind_program_descriptors(program);
}

} // extern "C"
