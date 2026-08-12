// Mithril-Wrapper - MG_Backend/backend_func.h
// Backend function-pointer table (Task 1 of the MobileGlues-architecture refactor).
//
// vk_func_t is the Vulkan analog of MobileGlues' g_gles_func (gles/gles.h). It
// holds one function-pointer field per backend_* function declared in Backend.h,
// named after the function with the backend_ prefix stripped. The table is
// populated once in backend_init() (DirectVulkan/Device.cpp) and the GL frontend
// will dispatch through it (Task 2.3) instead of calling backend_* directly,
// so the gl/ layer contains no direct vk*/vkCmd* calls.
//
// Only the backend_* functions become function-pointer fields. The structs
// (MGVertexAttrib, MGUnpackParams) and #define MITHRIL_LIMIT_* constants from
// Backend.h stay as-is and are transitively visible because this header
// includes Backend.h.
//
// The VK_FUNC_TYPEDEF / VK_FUNC_DECL macro pair mirrors MobileGlues'
// GL_FUNC_TYPEDEF / GL_FUNC_DECL pattern (gles/gles.h:17,482): one typedef line
// per function builds the matching name##_PTR type, and one VK_FUNC_DECL line
// per function inside struct vk_func_t declares the field. Adding a new
// backend_* function therefore requires exactly two matching lines here, and
// one assignment in Device.cpp's backend_init.
#ifndef MITHRIL_BACKEND_FUNC_H_
#define MITHRIL_BACKEND_FUNC_H_

#include "Backend.h"  // backend_* signatures, MGVertexAttrib, MGUnpackParams, MITHRIL_LIMIT_*

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Function-pointer typedef for each backend_* entry. The calling convention is
 * left implicit (no GLAPIENTRY) because the backend_* functions in Backend.h
 * are declared as plain C functions without a calling-convention modifier —
 * the typedefs must match exactly so &backend_X assigns cleanly into vk_func_t.
 *
 * Note on `const float color[4]` in backend_clear_buffer_indexed: array
 * parameters decay to pointers in C function declarations, so the typedef uses
 * `const float*` to match the declared type after decay (taking the address of
 * the function and assigning it would otherwise trigger a -Wincompatible-pointer-
 * types warning under strict mode).
 */
#define VK_FUNC_TYPEDEF(type, name, ...) typedef type (*name##_PTR)(__VA_ARGS__);

/* ---- Lifecycle ---- */
VK_FUNC_TYPEDEF(void, init, void)
VK_FUNC_TYPEDEF(void, shutdown, void)
VK_FUNC_TYPEDEF(int, available, void)
VK_FUNC_TYPEDEF(const char*, physical_device_name, void)
VK_FUNC_TYPEDEF(uint64_t, vram_bytes, void)

/* ---- Clear / load op ---- */
VK_FUNC_TYPEDEF(void, set_clear_color, float, float, float, float)
VK_FUNC_TYPEDEF(void, set_clear_depth, double)
VK_FUNC_TYPEDEF(void, set_clear_stencil, int)
VK_FUNC_TYPEDEF(void, set_load_clear, void)
VK_FUNC_TYPEDEF(void, set_load_load, void)
VK_FUNC_TYPEDEF(void, clear_attachments, GLbitfield, int, int, int, int)
VK_FUNC_TYPEDEF(void, clear_buffer_indexed, GLenum, GLint, const float*, float, GLuint)

/* ---- Render pass ---- */
VK_FUNC_TYPEDEF(void, begin_render_pass, VkImageView*, int, VkImageView, int, int, int)
VK_FUNC_TYPEDEF(void, set_fbo_attachment_tex_ids, GLuint*, int, GLuint)
VK_FUNC_TYPEDEF(void, set_invalidate_attachments, uint32_t, bool, bool)
VK_FUNC_TYPEDEF(void, end_render_pass, void)
VK_FUNC_TYPEDEF(void, commit, void)

/* ---- Swapchain (EGL-owned; backend registers/queries) ---- */
VK_FUNC_TYPEDEF(void, set_active_swapchain, void*)
VK_FUNC_TYPEDEF(void, swapchain_set_drawable_size, void*, int, int)
VK_FUNC_TYPEDEF(void, swapchain_mark_rebuild, void*)
VK_FUNC_TYPEDEF(void, drain_and_detach_swapchain, void)
VK_FUNC_TYPEDEF(int, swapchain_needs_rebuild, void*)

/* ---- Encoder dynamic state (vkCmdSet* under dynamic rendering) ---- */
VK_FUNC_TYPEDEF(void, bind_pipeline, VkPipeline)
VK_FUNC_TYPEDEF(void, set_viewport, int, int, int, int, double, double)
VK_FUNC_TYPEDEF(void, set_scissor, int, int, int, int)
VK_FUNC_TYPEDEF(void, set_vertex_buffer, int, VkBuffer, VkDeviceSize)
VK_FUNC_TYPEDEF(void, set_fragment_buffer, int, VkBuffer, VkDeviceSize)
VK_FUNC_TYPEDEF(void, set_vertex_texture, int, VkImageView, VkSampler)
VK_FUNC_TYPEDEF(void, set_fragment_texture, int, VkImageView, VkSampler)
VK_FUNC_TYPEDEF(void, set_blend_color, float, float, float, float)
VK_FUNC_TYPEDEF(void, set_depth_bias, float, float)
VK_FUNC_TYPEDEF(void, set_cull_mode, int)
VK_FUNC_TYPEDEF(void, set_front_face, int)
VK_FUNC_TYPEDEF(void, set_depth_test, int, int, int)
VK_FUNC_TYPEDEF(void, set_color_write_mask, int, int, int, int)
VK_FUNC_TYPEDEF(void, set_stencil_state, int, int, int, int, int, int, int)

/* ---- Draw calls ---- */
VK_FUNC_TYPEDEF(void, draw_arrays, int, int, int)
VK_FUNC_TYPEDEF(void, draw_indexed, int, int, int, VkBuffer, VkDeviceSize)
VK_FUNC_TYPEDEF(void, draw_arrays_instanced, int, int, int, int)
VK_FUNC_TYPEDEF(void, draw_indexed_instanced, int, int, int, VkBuffer, VkDeviceSize, int)
VK_FUNC_TYPEDEF(void, push_constants, GLuint, uint32_t, uint32_t, const void*)
VK_FUNC_TYPEDEF(void, draw_indirect, int, VkBuffer, VkDeviceSize, int, int)
VK_FUNC_TYPEDEF(void, draw_indexed_indirect, int, int, VkBuffer, VkDeviceSize, VkBuffer, VkDeviceSize, int, int)
VK_FUNC_TYPEDEF(void, draw_indirect_count, int, VkBuffer, VkDeviceSize, VkBuffer, VkDeviceSize, int, int)
VK_FUNC_TYPEDEF(void, draw_indexed_indirect_count, int, int, VkBuffer, VkDeviceSize, VkBuffer, VkDeviceSize, VkBuffer, VkDeviceSize, int, int)

/* ---- Buffers ---- */
VK_FUNC_TYPEDEF(VkBuffer, get_or_create_buffer, GLuint, const void*, size_t)
VK_FUNC_TYPEDEF(VkBuffer, create_buffer_storage, GLuint, VkDeviceSize, VkBufferUsageFlags, bool, bool)
VK_FUNC_TYPEDEF(void, buffer_upload, GLuint, GLintptr, const void*, size_t)
VK_FUNC_TYPEDEF(void*, get_buffer_mapped_pointer, GLuint)
VK_FUNC_TYPEDEF(VkBuffer, get_buffer, GLuint)
VK_FUNC_TYPEDEF(void, delete_buffer, GLuint)
VK_FUNC_TYPEDEF(VkBuffer, get_zero_buffer, void)
VK_FUNC_TYPEDEF(void, update_generic_attribs, const float*, int)
VK_FUNC_TYPEDEF(VkBuffer, get_generic_attrib_buffer, void)

/* ---- Textures ---- */
VK_FUNC_TYPEDEF(VkImage, get_or_create_texture, GLuint, int, int, int, int, GLenum, GLenum, int)
VK_FUNC_TYPEDEF(void, texture_upload, GLuint, int, int, int, int, int, int, int, GLenum, GLenum, const void*, const MGUnpackParams*, int)
VK_FUNC_TYPEDEF(void, texture_upload_compressed, GLuint, int, int, int, int, int, int, int, GLenum, GLsizei, const void*, int)
VK_FUNC_TYPEDEF(void, texture_set_params, GLuint, GLint, GLint, GLint, GLint, GLint, const float*)
VK_FUNC_TYPEDEF(VkImageView, get_texture_view, GLuint)
VK_FUNC_TYPEDEF(VkImage, get_texture_image, GLuint)
VK_FUNC_TYPEDEF(void, delete_texture, GLuint)
VK_FUNC_TYPEDEF(void, invalidate_sampler_cache, GLuint)
VK_FUNC_TYPEDEF(void, transition_texture_layout, GLuint, VkImageLayout)
VK_FUNC_TYPEDEF(void, generate_mipmaps, GLuint)
VK_FUNC_TYPEDEF(int, read_pixels, int, int, int, int, GLenum, GLenum, void*)
VK_FUNC_TYPEDEF(void, blit_texture, GLuint, GLuint, int, int, int, int, int, int, int, int, GLbitfield, GLenum)
VK_FUNC_TYPEDEF(void, blit_images, VkImage, VkFormat, VkImage, VkFormat, int, int, int, int, int, int, int, int, GLbitfield, GLenum, int, int)

/* ---- Samplers ---- */
VK_FUNC_TYPEDEF(VkSampler, get_or_create_sampler, GLuint, GLint, GLint, GLint, GLint, GLint, const float*)

/* ---- Format helpers ---- */
VK_FUNC_TYPEDEF(VkFormat, vk_format_for_gl, GLenum)

/* ---- Pipeline cache ---- */
VK_FUNC_TYPEDEF(VkPipeline, get_or_create_pipeline, GLuint, const uint32_t*, int, const uint32_t*, int, const MGVertexAttrib*, int, const VkFormat*, int, VkFormat, int, GLenum, GLenum, GLenum, GLenum, int, GLenum, int)
VK_FUNC_TYPEDEF(VkPipeline, get_or_create_compute_pipeline, GLuint)

/* ---- Compute / memory barrier ---- */
VK_FUNC_TYPEDEF(void, dispatch_compute, uint32_t, uint32_t, uint32_t)
VK_FUNC_TYPEDEF(void, dispatch_compute_indirect, VkBuffer, VkDeviceSize)
VK_FUNC_TYPEDEF(void, memory_barrier, GLbitfield)
VK_FUNC_TYPEDEF(void, delete_program_resources, GLuint)

/* ---- GL sync object backing (glFenceSync / glClientWaitSync) ---- */
VK_FUNC_TYPEDEF(uint64_t, last_completed_serial, void)
VK_FUNC_TYPEDEF(uint64_t, current_submit_serial, void)
VK_FUNC_TYPEDEF(bool, wait_serial, uint64_t, uint64_t)

/* ---- Program layouts / descriptors ---- */
VK_FUNC_TYPEDEF(void, ensure_program_layouts, GLuint, const uint32_t*, int, const uint32_t*, int)
VK_FUNC_TYPEDEF(void, bind_program_descriptors, GLuint)

/* ---- Present / swapchain lifecycle (EGL-owned) ---- */
VK_FUNC_TYPEDEF(void, present_and_acquire, void*)
VK_FUNC_TYPEDEF(void*, create_swapchain, void*, int, int, int, int)
VK_FUNC_TYPEDEF(void, destroy_swapchain, void*)
VK_FUNC_TYPEDEF(VkImageView, swapchain_acquire_color, void*)
VK_FUNC_TYPEDEF(VkImageView, swapchain_acquire_depth, void*)
VK_FUNC_TYPEDEF(int, swapchain_width, void*)
VK_FUNC_TYPEDEF(int, swapchain_height, void*)
VK_FUNC_TYPEDEF(VkImage, swapchain_current_color_image, void*)
VK_FUNC_TYPEDEF(VkFormat, swapchain_color_format, void*)
VK_FUNC_TYPEDEF(VkImage, swapchain_current_depth_image, void*)
VK_FUNC_TYPEDEF(VkFormat, swapchain_depth_format, void*)

/* ---- Device limits (GL_MAX_* queries) ---- */
VK_FUNC_TYPEDEF(int, device_limit, int, int)

/*
 * vk_func_t — the backend function-pointer table. One field per backend_*
 * function in Backend.h. Mirrors MobileGlues' gles_func_t (gles/gles.h:484).
 *
 * The global instance g_vk_func is defined in DirectVulkan/Device.cpp and
 * populated in backend_init(). Frontend dispatch will be switched to
 * g_vk_func.* in Task 2.3.
 */
#define VK_FUNC_DECL(name) name##_PTR name;

struct vk_func_t {
    /* Lifecycle */
    VK_FUNC_DECL(init)
    VK_FUNC_DECL(shutdown)
    VK_FUNC_DECL(available)
    VK_FUNC_DECL(physical_device_name)
    VK_FUNC_DECL(vram_bytes)

    /* Clear / load op */
    VK_FUNC_DECL(set_clear_color)
    VK_FUNC_DECL(set_clear_depth)
    VK_FUNC_DECL(set_clear_stencil)
    VK_FUNC_DECL(set_load_clear)
    VK_FUNC_DECL(set_load_load)
    VK_FUNC_DECL(clear_attachments)
    VK_FUNC_DECL(clear_buffer_indexed)

    /* Render pass */
    VK_FUNC_DECL(begin_render_pass)
    VK_FUNC_DECL(set_fbo_attachment_tex_ids)
    VK_FUNC_DECL(set_invalidate_attachments)
    VK_FUNC_DECL(end_render_pass)
    VK_FUNC_DECL(commit)

    /* Swapchain (EGL-owned; backend registers/queries) */
    VK_FUNC_DECL(set_active_swapchain)
    VK_FUNC_DECL(swapchain_set_drawable_size)
    VK_FUNC_DECL(swapchain_mark_rebuild)
    VK_FUNC_DECL(drain_and_detach_swapchain)
    VK_FUNC_DECL(swapchain_needs_rebuild)

    /* Encoder dynamic state */
    VK_FUNC_DECL(bind_pipeline)
    VK_FUNC_DECL(set_viewport)
    VK_FUNC_DECL(set_scissor)
    VK_FUNC_DECL(set_vertex_buffer)
    VK_FUNC_DECL(set_fragment_buffer)
    VK_FUNC_DECL(set_vertex_texture)
    VK_FUNC_DECL(set_fragment_texture)
    VK_FUNC_DECL(set_blend_color)
    VK_FUNC_DECL(set_depth_bias)
    VK_FUNC_DECL(set_cull_mode)
    VK_FUNC_DECL(set_front_face)
    VK_FUNC_DECL(set_depth_test)
    VK_FUNC_DECL(set_color_write_mask)
    VK_FUNC_DECL(set_stencil_state)

    /* Draw calls */
    VK_FUNC_DECL(draw_arrays)
    VK_FUNC_DECL(draw_indexed)
    VK_FUNC_DECL(draw_arrays_instanced)
    VK_FUNC_DECL(draw_indexed_instanced)
    VK_FUNC_DECL(push_constants)
    VK_FUNC_DECL(draw_indirect)
    VK_FUNC_DECL(draw_indexed_indirect)
    VK_FUNC_DECL(draw_indirect_count)
    VK_FUNC_DECL(draw_indexed_indirect_count)

    /* Buffers */
    VK_FUNC_DECL(get_or_create_buffer)
    VK_FUNC_DECL(create_buffer_storage)
    VK_FUNC_DECL(buffer_upload)
    VK_FUNC_DECL(get_buffer_mapped_pointer)
    VK_FUNC_DECL(get_buffer)
    VK_FUNC_DECL(delete_buffer)
    VK_FUNC_DECL(get_zero_buffer)
    VK_FUNC_DECL(update_generic_attribs)
    VK_FUNC_DECL(get_generic_attrib_buffer)

    /* Textures */
    VK_FUNC_DECL(get_or_create_texture)
    VK_FUNC_DECL(texture_upload)
    VK_FUNC_DECL(texture_upload_compressed)
    VK_FUNC_DECL(texture_set_params)
    VK_FUNC_DECL(get_texture_view)
    VK_FUNC_DECL(get_texture_image)
    VK_FUNC_DECL(delete_texture)
    VK_FUNC_DECL(invalidate_sampler_cache)
    VK_FUNC_DECL(transition_texture_layout)
    VK_FUNC_DECL(generate_mipmaps)
    VK_FUNC_DECL(read_pixels)
    VK_FUNC_DECL(blit_texture)
    VK_FUNC_DECL(blit_images)

    /* Samplers */
    VK_FUNC_DECL(get_or_create_sampler)

    /* Format helpers */
    VK_FUNC_DECL(vk_format_for_gl)

    /* Pipeline cache */
    VK_FUNC_DECL(get_or_create_pipeline)
    VK_FUNC_DECL(get_or_create_compute_pipeline)

    /* Compute / memory barrier */
    VK_FUNC_DECL(dispatch_compute)
    VK_FUNC_DECL(dispatch_compute_indirect)
    VK_FUNC_DECL(memory_barrier)
    VK_FUNC_DECL(delete_program_resources)

    /* GL sync object backing */
    VK_FUNC_DECL(last_completed_serial)
    VK_FUNC_DECL(current_submit_serial)
    VK_FUNC_DECL(wait_serial)

    /* Program layouts / descriptors */
    VK_FUNC_DECL(ensure_program_layouts)
    VK_FUNC_DECL(bind_program_descriptors)

    /* Present / swapchain lifecycle */
    VK_FUNC_DECL(present_and_acquire)
    VK_FUNC_DECL(create_swapchain)
    VK_FUNC_DECL(destroy_swapchain)
    VK_FUNC_DECL(swapchain_acquire_color)
    VK_FUNC_DECL(swapchain_acquire_depth)
    VK_FUNC_DECL(swapchain_width)
    VK_FUNC_DECL(swapchain_height)
    VK_FUNC_DECL(swapchain_current_color_image)
    VK_FUNC_DECL(swapchain_color_format)
    VK_FUNC_DECL(swapchain_current_depth_image)
    VK_FUNC_DECL(swapchain_depth_format)

    /* Device limits */
    VK_FUNC_DECL(device_limit)
};

/* Defined in DirectVulkan/Device.cpp; populated in backend_init(). */
extern vk_func_t g_vk_func;

#ifdef __cplusplus
}
#endif

#endif // MITHRIL_BACKEND_FUNC_H_
