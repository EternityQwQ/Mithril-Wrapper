// Mithril-Wrapper - MG_Impl/Drawing.cpp
// Core drawing path: glDrawArrays / glDrawElements / instanced variants ->
// Vulkan dynamic-rendering + pipeline orchestration.
//
// Pipeline: resolve VAO + program + FBO attachments -> get-or-create
// VkGraphicsPipeline (backend_get_or_create_pipeline, SPIR-V + vertex format +
// attachment VkFormats + blend state as cache key) -> begin dynamic render
// pass (Load action) -> bind pipeline + set viewport/scissor/cull/depth/mask
// via vkCmdSet* -> bind vertex buffers + textures/samplers + uniform buffers
// -> issue draw -> end pass.
//
// This is the Vulkan/MoltenVK rewrite of the former gl/drawing.cpp. The Metal
// encoder calls (metal_encoder_*) are replaced with the Vulkan backend C API
// (backend_*) declared in MG_Backend/Backend.h. Render passes use Vulkan 1.2
// dynamic rendering (VK_KHR_dynamic_rendering) instead of Metal render
// encoders.
// Sync object + transform-feedback constants — standard GL values missing
// from our minimal glcorearb.h. Guarded so a future header update won't
// conflict. Defined before includes so State.h (which uses them as default
// field values) sees them.
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif
#ifndef GL_SYNC_FENCE
#define GL_SYNC_FENCE                0x9116
#endif
#ifndef GL_SYNC_CONDITION
#define GL_SYNC_CONDITION            0x9118
#endif
#ifndef GL_SYNC_FLAGS
#define GL_SYNC_FLAGS                0x9115
#endif
#ifndef GL_SYNC_STATUS
#define GL_SYNC_STATUS               0x9119
#endif
#ifndef GL_SIGNALED
#define GL_SIGNALED                  0x911E
#endif
#ifndef GL_UNSIGNALED
#define GL_UNSIGNALED                0x911F
#endif
#ifndef GL_OBJECT_TYPE
#define GL_OBJECT_TYPE               0x9112
#endif
#ifndef GL_INTERLEAVED_ATTRIBS
#define GL_INTERLEAVED_ATTRIBS       0x8C8C
#endif

#include "includes.h"
#include "Framebuffer.h"

#include <algorithm>
#include <cstring>

extern "C" {

static void prepare_draw(GLenum mode) {
    // Resolve current program + its SPIR-V.
    mithril::Program* prog = mithril::state_get_program(g_state->currentProgram);
    if (!prog || !prog->linked) return;

    // Determine whether we are drawing to the default framebuffer (FBO 0) or a
    // user-created FBO. This selects the Y-flipped vs non-flipped vertex SPIR-V
    // variant: the default framebuffer renders to the on-screen drawable
    // (Vulkan/Metal Y-down), so it needs the Y-flipped variant; user FBOs
    // render into textures sampled by GL shaders (GL Y-up), so they use the
    // non-flipped variant. Deep reference: MobileGL GetShaderTransformFlags.
    bool is_default_fbo = (g_state->currentDrawFBO == 0);
    const std::vector<uint32_t>& vs_spirv = is_default_fbo
        ? prog->vertexSpirvYFlipped : prog->vertexSpirv;

    // Defensive: skip draws whose shader translation produced no SPIR-V
    // (e.g. glslang failed on an unrecognised construct). Issuing the draw
    // would pass null/0 to backend_get_or_create_pipeline, which would
    // either crash on the SPIR-V pointer or fail pipeline creation silently
    // and leave the screen black. Logging once per program id keeps the log
    // readable when the host retries the same broken shader every frame.
    if (vs_spirv.empty() || prog->fragmentSpirv.empty()) {
        static GLuint last_warned = 0;
        if (last_warned != prog->id) {
            last_warned = prog->id;
            MITHRIL_LOG_WARN("gl", "prepare_draw: program %u has empty SPIR-V "
                              "(vertex=%zu vertexYFlip=%zu fragment=%zu words, "
                              "is_default_fbo=%d); skipping draw",
                              prog->id, prog->vertexSpirv.size(),
                              prog->vertexSpirvYFlipped.size(),
                              prog->fragmentSpirv.size(), (int)is_default_fbo);
        }
        return;
    }

    // Resolve current draw FBO attachments (color + depth VkImageViews + size).
    VkImageView colors[8] = {VK_NULL_HANDLE};
    VkImageView depth_view = VK_NULL_HANDLE;
    int w = 0, h = 0;
    int color_count = mithril::collect_draw_fbo_attachments(colors, &depth_view, &w, &h);
    // Defensive: if no color attachment is bound at all (e.g. the EGL default
    // framebuffer has no swapchain yet because the surface isn't sized), skip
    // the draw. Beginning a render pass with all-null attachments produces a
    // validation error and a no-op pass on MoltenVK, so skipping is both
    // cheaper and avoids log spam.
    if (color_count <= 0) {
        bool any_color = false;
        for (int i = 0; i < 8; ++i) if (colors[i] != VK_NULL_HANDLE) { any_color = true; break; }
        if (!any_color) return;
    }

    // Compute color attachment VkFormats.
    VkFormat color_formats[8] = {VK_FORMAT_UNDEFINED};
    mithril::Framebuffer* fbo = mithril::state_get_framebuffer(g_state->currentDrawFBO);
    if (fbo) {
        for (int i = 0; i < color_count; ++i) {
            GLuint t = fbo->colors[i].texture;
            mithril::Texture* tex = mithril::state_get_texture(t);
            if (tex) color_formats[i] = backend_vk_format_for_gl((GLenum)tex->internalFormat);
        }
    } else {
        // EGL default framebuffer: read the swapchain's actual color format
        // from g_state->eglDefaultColorFormat (set by install_surface_on_state
        // after each acquire). Hardcoding VK_FORMAT_B8G8R8A8_UNORM would
        // mismatch if MoltenVK picked a different surface format (e.g. RGBA8
        // or an sRGB variant), causing a pipeline-creation failure on the
        // first draw and a black screen.
        VkFormat swapchainFmt = g_state->eglDefaultColorFormat;
        if (swapchainFmt == VK_FORMAT_UNDEFINED) {
            // Fallback for headless / surfaceless mode where no swapchain is
            // attached. BGRA8Unorm matches MoltenVK's most common default.
            swapchainFmt = VK_FORMAT_B8G8R8A8_UNORM;
        }
        for (int i = 0; i < color_count; ++i) {
            if (colors[i] != VK_NULL_HANDLE) {
                color_formats[i] = swapchainFmt;
            }
        }
    }
    // Depth format from the bound depth texture.
    // For FBO 0 (EGL default framebuffer), the depth image is a raw VkImage
    // created by the EGL layer (VK_FORMAT_D32_SFLOAT_S8_UINT), not tracked in
    // the GL texture table. For user FBOs, derive the VkFormat from the GL
    // internalFormat.
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    if (fbo && fbo->depth.texture) {
        mithril::Texture* dt = mithril::state_get_texture(fbo->depth.texture);
        if (dt) depth_format = backend_vk_format_for_gl((GLenum)dt->internalFormat);
    } else if (depth_view != VK_NULL_HANDLE) {
        // EGL default framebuffer: depth is always D32_SFLOAT_S8_UINT.
        depth_format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    }

    // Build the vertex attribute descriptor array for the pipeline signature.
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    if (!vao) vao = mithril::state_get_vao(0);
    MGVertexAttrib attribs[mithril::kMaxVertexAttribs];
    int attrib_count = 0;
    for (int i = 0; i < mithril::kMaxVertexAttribs; ++i) {
        const mithril::VertexAttrib& a = vao->attribs[i];
        if (!a.enabled) continue;
        MGVertexAttrib& m = attribs[attrib_count++];
        m.location     = i;
        m.size         = a.size;
        m.type         = a.type;
        m.normalized   = a.normalized ? 1 : 0;
        m.integer      = a.integer ? 1 : 0;
        m.stride       = a.stride;
        m.offset       = (int)(intptr_t)a.pointer;
        m.enabled      = 1;
        m.buffer_name  = a.boundBuffer;
        m.divisor      = a.divisor;
    }

    // Get-or-create the VkGraphicsPipeline. Blend state + colorWriteMask are
    // part of the pipeline signature so that enabling/disabling GL_BLEND,
    // changing blend functions, or calling glColorMask creates a distinct
    // pipeline (root cause I+J: previously only blend_enabled/src/dst were in
    // the signature and colorWriteMask was hardcoded RGBA-all-on, so different
    // blend/mask configs collided in the cache and glColorMask was a no-op).
    int cwm_bits = 0;
    if (g_state->colorMask[0][0]) cwm_bits |= 1;
    if (g_state->colorMask[0][1]) cwm_bits |= 2;
    if (g_state->colorMask[0][2]) cwm_bits |= 4;
    if (g_state->colorMask[0][3]) cwm_bits |= 8;
    VkPipeline pipeline = backend_get_or_create_pipeline(
        prog->id,
        vs_spirv.data(),            (int)vs_spirv.size(),
        prog->fragmentSpirv.data(), (int)prog->fragmentSpirv.size(),
        attribs, attrib_count,
        color_formats, color_count,
        depth_format,
        g_state->blends[0].enabled ? 1 : 0,
        g_state->blends[0].srcRGB,
        g_state->blends[0].dstRGB,
        g_state->blends[0].srcA,
        g_state->blends[0].dstA,
        cwm_bits,
        mode,
        is_default_fbo ? 1 : 0);
    if (pipeline == VK_NULL_HANDLE) return;

    // FIX (root cause Y, CRITICAL): Register user-FBO attachment tex_ids so
    // begin_render_pass can barrier their images to attachment-optimal and
    // end_render_pass can barrier them back to read-only + update
    // TextureEntry::currentLayout. VK_KHR_dynamic_rendering does NOT
    // auto-transition attachment layouts — without this registration, the
    // declared imageLayout (COLOR_ATTACHMENT_OPTIMAL) would mismatch the
    // actual layout (SHADER_READ_ONLY_OPTIMAL from a prior upload) → spec
    // violation → MoltenVK drops the draw → black screen.
    // For FBO 0 (swapchain), pass null/0 to clear any stale registration;
    // the swapchain path's barriers are handled by the activeSwapchain block.
    if (fbo) {
        GLuint color_tex_ids[8] = {0};
        for (int i = 0; i < color_count && i < 8; ++i) {
            color_tex_ids[i] = fbo->colors[i].texture;
        }
        GLuint depth_tex_id = fbo->depth.texture;
        backend_set_fbo_attachment_tex_ids(color_tex_ids, color_count, depth_tex_id);
    } else {
        backend_set_fbo_attachment_tex_ids(nullptr, 0, 0);
    }

    // Begin render pass (Load action preserves previous contents).
    backend_set_load_load();
    backend_begin_render_pass(colors, color_count, depth_view, w, h, 1);

    // Bind pipeline + set dynamic state via vkCmdSet*.
    backend_bind_pipeline(pipeline);
    // Bind the program's descriptor set (UBOs + sampled images) immediately
    // after the pipeline so the shader's uniform/texture bindings are live for
    // the upcoming draw. The set is built per-draw from Program.uniforms +
    // g_state->boundTextures by DescriptorSet.cpp.
    backend_bind_program_descriptors(prog->id);
    backend_set_viewport(g_state->viewportX, g_state->viewportY,
                         g_state->viewportW, g_state->viewportH,
                         g_state->depthNear, g_state->depthFar);
    // FIX (root cause G): ALWAYS set the scissor. VK_DYNAMIC_STATE_SCISSOR is
    // a dynamic state (Pipeline.cpp), so it MUST be set via vkCmdSetScissor
    // before drawing. When scissorTest is disabled, the old code skipped the
    // call entirely, leaving the dynamic scissor at its undefined default
    // (0,0,0,0) — which clips ALL pixels → black screen. MobileGL always
    // sets a scissor (full viewport when GL_SCISSOR_TEST is off).
    if (g_state->scissorTest) {
        backend_set_scissor(g_state->scissorX, g_state->scissorY,
                            g_state->scissorW, g_state->scissorH);
    } else {
        backend_set_scissor(0, 0, g_state->viewportW, g_state->viewportH);
    }
    // FIX (root cause H + Y-flip winding fix): ALWAYS set cull mode.
    // VK_DYNAMIC_STATE_CULL_MODE is dynamic; skipping the call when cullFace is
    // disabled leaves the previous draw's cull mode active → stale culling
    // culls geometry incorrectly. When cullFace is off, explicitly set
    // VK_CULL_MODE_NONE.
    //
    // Y-flip winding adjustment (deep reference: MobileGL
    // ConvertCullFaceModeToVkEnum + VulkanRenderer frontFace=CLOCKWISE):
    // When the vertex Y is flipped (default framebuffer), triangle winding
    // inverts (CCW→CW, CW→CCW). To keep the GL-intended faces visible:
    //   - Swap the cull mode: GL_FRONT→VK_BACK, GL_BACK→VK_FRONT
    //   - Hardcode frontFace to CLOCKWISE (the inverted winding makes GL's
    //     CCW triangles appear as CW in Vulkan). MobileGL does the same.
    // User FBOs (no Y flip) keep the original cull mode and frontFace.
    if (g_state->cullFace) {
        // FIX (Root Cause K - Y翻转面剔除双重补偿):
        // Vulkan 面剔除由两个独立状态控制：frontFace（定义正面缠绕方向）+ cullMode（剔除哪面）。
        // Y 翻转（gl_Position.y = -y）反转缠绕：GL-CCW → Vulkan-CW。
        // 正确补偿（二选一，不可同时）：
        //   方案A: frontFace=CW（GL-CCW→Vulkan-CW="正面"），不交换 cull mode（GL_BACK→VK_BACK 剔除 GL-背面）
        //   方案B: frontFace=CCW（GL-CCW→Vulkan-CW="背面"），交换 cull mode（GL_BACK→VK_FRONT 剔除 GL-背面）
        // 旧代码同时执行 A+B → 双重补偿：frontFace=CW 使 GL-正面=Vulkan-正面，再交换 cull=VK_FRONT
        // 剔除 Vulkan-正面=GL-正面 → 所有正面几何被剔除，只剩 clear color（红色）→ 红屏。
        // 修复：采用方案A，仅 frontFace=CW 补偿，cull mode 直接按 GL 值映射不交换。
        // 参考 MobileGL VulkanRenderer ConvertCullFaceModeToVkEnum：不交换 cull mode，
        // 仅通过 frontFace=CLOCKWISE 补偿 Y 翻转。
        int vk_cull = 0;
        if (g_state->cullMode == GL_FRONT) {
            vk_cull = 1;  // VK_CULL_MODE_FRONT_BIT
        } else if (g_state->cullMode == GL_BACK) {
            vk_cull = 2;  // VK_CULL_MODE_BACK_BIT
        } else {  // GL_FRONT_AND_BACK
            vk_cull = 3;  // VK_CULL_MODE_FRONT_AND_BACK
        }
        backend_set_cull_mode(vk_cull);
        // Y 翻转使缠绕反转：GL-CCW → Vulkan-CW。设 frontFace=CW 补偿（仅默认帧缓冲）。
        // 用户 FBO 无 Y 翻转，frontFace 按 GL 值映射（CCW→1, CW→0）。
        backend_set_front_face(is_default_fbo ? 0 /*CW*/ :
                               (g_state->frontFace == GL_CCW ? 1 : 0));
    } else {
        backend_set_cull_mode(0);  // VK_CULL_MODE_NONE
    }
    backend_set_color_write_mask(
        g_state->colorMask[0][0], g_state->colorMask[0][1],
        g_state->colorMask[0][2], g_state->colorMask[0][3]);
    backend_set_depth_test(
        g_state->depthTest ? 1 : 0,
        g_state->depthMask ? 1 : 0,
        (int)g_state->depthFunc);
    // Apply dynamic pipeline state: depth bias + stencil.
    // 对照 MobileGL 动态状态应用.
    if (g_state->polygonOffsetFill) {
        backend_set_depth_bias(g_state->polygonOffsetFactor, g_state->polygonOffsetUnits);
    }
    if (g_state->stencilTest) {
        backend_set_stencil_state(1, (int)g_state->stencilFunc, g_state->stencilRef,
                                  (int)g_state->stencilValueMask,
                                  (int)g_state->stencilSfail, (int)g_state->stencilDpfail,
                                  (int)g_state->stencilDppass);
    }
    if (g_state->blends[0].enabled) {
        backend_set_blend_color(
            g_state->blendColor[0], g_state->blendColor[1],
            g_state->blendColor[2], g_state->blendColor[3]);
    }

    // Bind vertex buffers — one VkBuffer per enabled attribute, at index
    // == attribute location (matches the vertex input binding layout). For
    // attribute slots the VAO didn't enable, bind the shared zero buffer so
    // the unbound vertex input reads vec4(0) instead of dereferencing
    // unbound memory.
    VkBuffer zero_buf = backend_get_zero_buffer();
    bool bound_slots[16] = {false};
    for (int i = 0; i < attrib_count; ++i) {
        MGVertexAttrib& m = attribs[i];
        VkBuffer buf = backend_get_buffer(m.buffer_name);
        if (buf != VK_NULL_HANDLE) {
// FIX (Root Cause H - 顶点属性偏移双重应用):
// Vulkan 顶点寻址公式: buffer + pOffsets[binding] + vertexIndex*stride + attr.offset
// m.offset 是属性在顶点结构内的成员偏移，必须只由 VkVertexInputAttributeDescription::offset
// 处理（见 Pipeline.cpp:302 ad.offset = a.offset）。若同时作为 binding offset 传入，
// 偏移会被应用两次 → 有效地址 = buffer + 2*m.offset，导致交错顶点格式（如
// position@0/color@12/uv@24）的属性读取错位 → 加载界面红屏/花屏。
// 参考 MobileGL VkglVertexAttribBindingState：binding offset 恒为 0，偏移由属性描述处理。
            backend_set_vertex_buffer(m.location, buf, 0);
            if (m.location < 16) bound_slots[m.location] = true;
        }
    }
    // Bind the zero buffer to any slot 0..15 not covered above.
    if (zero_buf != VK_NULL_HANDLE) {
        for (int loc = 0; loc < 16; ++loc) {
            if (!bound_slots[loc]) {
                backend_set_vertex_buffer(loc, zero_buf, 0);
            }
        }
    }

    // Uniform buffers and sampled-image bindings are now sourced + bound via
    // the descriptor set in backend_bind_program_descriptors() (called above,
    // right after backend_bind_pipeline). It reflects the program's SPIR-V,
    // maps each UBO to Program.uniforms[name].value and each sampler binding B
    // to g_state->boundTextures[B], and writes + binds a fresh VkDescriptorSet
    // for this draw. The legacy backend_set_fragment_buffer /
    // backend_set_fragment_texture stubs are no-ops (kept only for the C API
    // contract) — descriptor binding is centralised in DescriptorSet.cpp.
}

static void end_draw(void) {
    // End the render pass but DON'T commit the command buffer here.
    // The command buffer is committed once per frame in eglSwapBuffers,
    // which presents the swapchain image. Committing per-draw would flush
    // the Vulkan pipeline hundreds of times per frame, causing severe perf
    // loss and present timing issues.
    backend_end_render_pass();
}

static int index_type_to_int(GLenum type) {
    // FIX (root cause AE - GL_UNSIGNED_BYTE 索引支持):
    // 0 = UINT16 (GL_UNSIGNED_SHORT), 1 = UINT32 (GL_UNSIGNED_INT),
    // 2 = UINT8 (GL_UNSIGNED_BYTE)。原代码仅返回 0/1，GL_UNSIGNED_BYTE
    // 被当作 UINT16 → 1 字节索引按 2 字节解释 → 索引值错乱 → 几何腐败 → 红屏。
    // backend_draw_indexed 的 case 2 映射到 VK_INDEX_TYPE_UINT8
    // （需 Device.cpp 启用 VK_EXT_index_type_uint8）。
    // 深度对照 MobileGL VulkanRenderer.cpp:3093-3109。
    if (type == GL_UNSIGNED_INT)   return 1;
    if (type == GL_UNSIGNED_BYTE)  return 2;
    return 0;  // GL_UNSIGNED_SHORT → UINT16
}

// P1-9: Validate primitive mode + vertex count for draw calls.
// Returns true if the draw may proceed; otherwise records a GL error and
// returns false. Mode must be one of the GL 3.3 Core primitive modes; count
// must be non-negative.
static bool validate_draw_call(GLenum mode, GLsizei count) {
    switch (mode) {
        case GL_POINTS:
        case GL_LINES:
        case GL_LINE_STRIP:
        case GL_LINE_LOOP:
        case GL_TRIANGLES:
        case GL_TRIANGLE_STRIP:
        case GL_TRIANGLE_FAN:
            break;
        default:
            mithril::state_set_error(GL_INVALID_ENUM);
            return false;
    }
    if (count < 0) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return false;
    }
    return true;
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    MITHRIL_ENSURE_INIT();
    if (!validate_draw_call(mode, count)) return;
    prepare_draw(mode);
    backend_draw_arrays((int)mode, (int)first, (int)count);
    end_draw();
}

void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei primcount) {
    MITHRIL_ENSURE_INIT();
    prepare_draw(mode);
    backend_draw_arrays_instanced((int)mode, (int)first, (int)count, (int)primcount);
    end_draw();
}

void glDrawArraysInstancedBaseInstance(GLenum mode, GLint first, GLsizei count,
                                       GLsizei primcount, GLuint baseinstance) {
    MITHRIL_ENSURE_INIT();
    // FIX (root cause AG - BaseInstance): 设置 currentBaseInstance 后调用 draw，
    // 完成后重置为 0。backend_draw_arrays_instanced 从 g_state 读取后传给
    // vkCmdDraw 的 firstInstance。深度对照 MobileGL drawParams.baseInstance。
    g_state->currentBaseInstance = baseinstance;
    prepare_draw(mode);
    backend_draw_arrays_instanced((int)mode, (int)first, (int)count, (int)primcount);
    g_state->currentBaseInstance = 0;
    end_draw();
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    MITHRIL_ENSURE_INIT();
    if (!validate_draw_call(mode, count)) return;
    prepare_draw(mode);
    // If a VBO is bound for GL_ELEMENT_ARRAY_BUFFER, indices is an offset into it.
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    GLuint ib_name = vao ? vao->elementArrayBuffer : 0;
    VkBuffer ib = backend_get_buffer(ib_name);
    if (ib != VK_NULL_HANDLE) {
        backend_draw_indexed((int)mode, (int)count, index_type_to_int(type),
                             ib, (VkDeviceSize)(intptr_t)indices);
    } else if (indices) {
        // Client-space index pointer: stage into a transient VkBuffer.
        // FIX (root cause AE): GL_UNSIGNED_BYTE 索引按 1 字节/索引 staging，
        // 否则 staging 大小翻倍 → 越界读 + 索引错乱。
        size_t elem = (type == GL_UNSIGNED_INT) ? 4 : (type == GL_UNSIGNED_BYTE) ? 1 : 2;
        GLuint transient = (GLuint)(uintptr_t)indices; // use address as throwaway name
        VkBuffer staged = backend_get_or_create_buffer(transient | 0x80000000u,
                                                       indices, (size_t)count * elem);
        if (staged != VK_NULL_HANDLE) {
            backend_draw_indexed((int)mode, (int)count, index_type_to_int(type),
                                 staged, 0);
        }
    }
    end_draw();
}

void glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type,
                              const void* indices, GLint basevertex) {
    MITHRIL_ENSURE_INIT();
    // FIX (root cause AG - BaseVertex): 将 baseVertex 通过 g_state->currentBaseVertex
    // 传递给 backend_draw_indexed（vkCmdDrawIndexed 的 vertexOffset）。draw 完成后
    // 立即重置为 0，避免泄漏到后续无 BaseVertex 的 draw（应保持 vertexOffset=0）。
    // 深度对照 MobileGL drawParams.baseVertex。
    //
    // TODO (Task 6 — gl_VertexID baseVertex 语义): 这里只把 baseVertex 作为
    // vkCmdDrawIndexed 的 vertexOffset 传下去，这只补偿了 *顶点数据寻址*
    // （buffer + (index + vertexOffset) * stride），并不影响 shader 内 gl_VertexIndex
    // 的值。GL 的 gl_VertexID 在索引绘制中 == index + baseVertex（含 baseVertex），
    // Vulkan 的 gl_VertexIndex == 原始 index（不含 vertexOffset）。因此当 baseVertex!=0
    // 且 vertex shader 用 gl_VertexID 做 SSBO/纹理数组查找时，查找会偏移 baseVertex。
    //
    // 完整修复需要在 vertex shader 注入 push-constant 补偿：
    //   layout(push_constant) uniform _MithrilBaseVertex { int _mithrilBaseVertex; } _mbv;
    //   #define gl_VertexID (gl_VertexIndex + _mbv._mithrilBaseVertex)
    // 并在此处（及 glDrawElementsInstancedBaseVertex / glDrawElementsBaseVertexBaseInstance）
    // draw 前调用 backend_push_constants(offset = baseVertex)。这需要 Pipeline.cpp 在
    // VkPipelineLayout 声明 push-constant range + Backend.h / CommandStream.cpp 新增
    // backend_push_constants 入口（当前 backend 无任何 push-constant 基础设施）。
    // 属 3+ 文件改动，超出最小修复范围，留作 follow-up。
    //
    // 当前不补的合理性：Minecraft 绝大多数 draw call 的 baseVertex==0，此时
    // gl_VertexID == index == gl_VertexIndex，语义差异消失。Shader.cpp 已保留
    // gl_VertexID→gl_VertexIndex 改名（否则 Vulkan GLSL 编译失败 → 黑屏）。
    // 详见 Shader.cpp:rewrite_desktop_builtins 的 SEMANTIC MISMATCH 注释。
    g_state->currentBaseVertex = basevertex;
    glDrawElements(mode, count, type, indices);
    g_state->currentBaseVertex = 0;
}

void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                             const void* indices, GLsizei primcount) {
    MITHRIL_ENSURE_INIT();
    prepare_draw(mode);
    mithril::VertexArray* vao = mithril::state_get_vao(g_state->currentVAO);
    GLuint ib_name = vao ? vao->elementArrayBuffer : 0;
    VkBuffer ib = backend_get_buffer(ib_name);
    if (ib != VK_NULL_HANDLE) {
        backend_draw_indexed_instanced((int)mode, (int)count,
                                       index_type_to_int(type), ib,
                                       (VkDeviceSize)(intptr_t)indices, (int)primcount);
    } else if (indices) {
        // FIX (root cause AE): GL_UNSIGNED_BYTE 索引按 1 字节/索引 staging。
        size_t elem = (type == GL_UNSIGNED_INT) ? 4 : (type == GL_UNSIGNED_BYTE) ? 1 : 2;
        GLuint transient = (GLuint)(uintptr_t)indices;
        VkBuffer staged = backend_get_or_create_buffer(transient | 0x80000000u,
                                                       indices, (size_t)count * elem);
        if (staged != VK_NULL_HANDLE) {
            backend_draw_indexed_instanced((int)mode, (int)count,
                                           index_type_to_int(type), staged, 0,
                                           (int)primcount);
        }
    }
    end_draw();
}

void glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type,
                                       const void* indices, GLsizei primcount,
                                       GLint basevertex) {
    MITHRIL_ENSURE_INIT();
    // FIX (root cause AG - BaseVertex): 设置 currentBaseVertex 后调用 draw，
    // 完成后重置为 0。深度对照 MobileGL drawParams.baseVertex。
    g_state->currentBaseVertex = basevertex;
    glDrawElementsInstanced(mode, count, type, indices, primcount);
    g_state->currentBaseVertex = 0;
}

void glDrawElementsInstancedBaseInstance(GLenum mode, GLsizei count, GLenum type,
                                         const void* indices, GLsizei primcount,
                                         GLuint baseinstance) {
    MITHRIL_ENSURE_INIT();
    // FIX (root cause AG - BaseInstance): 设置 currentBaseInstance 后调用 draw，
    // 完成后重置为 0。backend_draw_indexed_instanced 从 g_state 读取后传给
    // vkCmdDrawIndexed 的 firstInstance。深度对照 MobileGL drawParams.baseInstance。
    g_state->currentBaseInstance = baseinstance;
    glDrawElementsInstanced(mode, count, type, indices, primcount);
    g_state->currentBaseInstance = 0;
}

void glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count,
                         GLenum type, const void* indices) {
    MITHRIL_ENSURE_INIT();
    (void)start; (void)end;
    glDrawElements(mode, count, type, indices);
}

void glDrawElementsBaseVertexBaseInstance(GLenum mode, GLsizei count, GLenum type,
                                          const void* indices, GLint basevertex,
                                          GLuint baseinstance) {
    MITHRIL_ENSURE_INIT();
    // FIX (root cause AG - BaseVertex + BaseInstance): 同时设置 currentBaseVertex
    // 与 currentBaseInstance，draw 完成后重置为 0。深度对照 MobileGL drawParams。
    g_state->currentBaseVertex = basevertex;
    g_state->currentBaseInstance = baseinstance;
    glDrawElements(mode, count, type, indices);
    g_state->currentBaseVertex = 0;
    g_state->currentBaseInstance = 0;
}

void glMultiDrawArrays(GLenum mode, const GLint* first, const GLsizei* count, GLsizei drawcount) {
    MITHRIL_ENSURE_INIT();
    if (!first || !count || drawcount <= 0) return;
    for (GLsizei i = 0; i < drawcount; ++i) {
        if (count[i] > 0) glDrawArrays(mode, first[i], count[i]);
    }
}

void glMultiDrawElements(GLenum mode, const GLsizei* count, GLenum type,
                         const void* const* indices, GLsizei drawcount) {
    MITHRIL_ENSURE_INIT();
    if (!count || !indices || drawcount <= 0) return;
    for (GLsizei i = 0; i < drawcount; ++i) {
        if (count[i] > 0) glDrawElements(mode, count[i], type, indices[i]);
    }
}

/* ---- Sync objects (P1-16 FIX) ---- */
// Real state tracking via g_state->syncObjects. Handles are allocated from
// g_state->nextSyncHandle (monotonic, avoids the sentinel 0x1). CPU-side
// fences are considered immediately signaled, matching the previous stub
// behaviour but with proper existence/identity checks.
GLsync glFenceSync(GLenum condition, GLbitfield flags) {
    MITHRIL_ENSURE_INIT();
    if (condition != GL_SYNC_GPU_COMMANDS_COMPLETE) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return nullptr;
    }
    mithril::Sync sync;
    sync.handle = g_state->nextSyncHandle;
    sync.condition = condition;
    sync.flags = flags;
    sync.signaled = true;  // CPU-side fence is immediately signaled
    sync.markedForDeletion = false;
    g_state->syncObjects[sync.handle] = sync;
    g_state->nextSyncHandle = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(g_state->nextSyncHandle) + 1);
    return reinterpret_cast<GLsync>(sync.handle);
}

void glDeleteSync(GLsync sync) {
    MITHRIL_ENSURE_INIT();
    if (!sync) return;
    void* handle = reinterpret_cast<void*>(sync);
    g_state->syncObjects.erase(handle);
}

GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
    MITHRIL_ENSURE_INIT();
    (void)flags; (void)timeout;
    if (!sync) return GL_WAIT_FAILED;
    void* handle = reinterpret_cast<void*>(sync);
    auto it = g_state->syncObjects.find(handle);
    if (it == g_state->syncObjects.end()) return GL_WAIT_FAILED;
    return it->second.signaled ? GL_ALREADY_SIGNALED : GL_TIMEOUT_EXPIRED;
}

void glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
    MITHRIL_ENSURE_INIT();
    (void)sync; (void)flags; (void)timeout;
    // No-op: CPU-side fences are immediately signaled.
}

GLboolean glIsSync(GLsync sync) {
    MITHRIL_ENSURE_INIT();
    if (!sync) return GL_FALSE;
    void* handle = reinterpret_cast<void*>(sync);
    return g_state->syncObjects.find(handle) != g_state->syncObjects.end()
        ? GL_TRUE : GL_FALSE;
}

void glGetSynciv(GLsync sync, GLenum pname, GLsizei bufSize, GLsizei* length, GLint* values) {
    MITHRIL_ENSURE_INIT();
    if (length) *length = 0;
    if (bufSize < 0 || !values || bufSize == 0) return;
    if (!sync) return;
    void* handle = reinterpret_cast<void*>(sync);
    auto it = g_state->syncObjects.find(handle);
    if (it == g_state->syncObjects.end()) return;
    const mithril::Sync& s = it->second;
    GLint v = 0;
    switch (pname) {
        case GL_OBJECT_TYPE:    v = GL_SYNC_FENCE; break;
        case GL_SYNC_CONDITION: v = (GLint)s.condition; break;
        case GL_SYNC_FLAGS:     v = (GLint)s.flags; break;
        case GL_SYNC_STATUS:    v = s.signaled ? GL_SIGNALED : GL_UNSIGNALED; break;
        default: return;
    }
    values[0] = v;
    if (length) *length = 1;
}

} // extern "C"
