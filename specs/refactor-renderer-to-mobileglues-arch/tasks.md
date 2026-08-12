# Tasks

- [x] Task 1: 搭建 gl/ 三层结构与符号抢占派发骨架
  - [ ] SubTask 1.1: 创建 `Mithril-Wrapper-cpp/gl/` 目录；在 `gl/gl.h`（或 `gl/loader.h`）引入 `NATIVE_FUNCTION_HEAD` / `NATIVE_FUNCTION_END` 宏（`extern "C"` 导出 `glFoo` + `glFooARB` 别名 `__attribute__((alias))`）
  - [ ] SubTask 1.2: 定义后端函数指针表 `vk_func_t`（`MG_Backend/backend_func.h`），将 `MG_Backend/Backend.h` 的 `backend_*` C API 收敛为函数指针表字段；`DirectVulkan/` 在 `backend_init` 填充全局 `g_vk_func`
  - [ ] SubTask 1.3: `CMakeLists.txt` 添加 `-Wl,-Bsymbolic-functions` 链接选项；更新源路径 `MG_Impl`→`gl`；新增 `gl/glsl/` 源
- [x] Task 2: MG_Impl/ → gl/ 文件重组（1:1 透传 + 翻译入口拆分）
  - [x] SubTask 2.1: 拆分 `gl.cpp` 为 `gl/gl.cpp`（核心状态切换）、`gl/gl_native.cpp`（1:1 透传骨架）、`gl/gl_stub.cpp`（deprecated no-op）；提取 `gl/enable.cpp`（enable/disable）与 `gl/pixel.cpp`（pixel store）；57 个入口已用 `NATIVE_FUNCTION_HEAD`/`END` 包裹导出 `glFoo`+`glFooARB` 别名
  - [x] SubTask 2.2: 按职责拆分文件（buffer/drawing/enable/framebuffer/getter/program/shader/texture/vertexattrib/pixel/gl_native/gl_stub 已就位，对应 MobileGlues 文件命名）
  - [x] SubTask 2.3: 205 个 `backend_*` 调用点已转为 `g_vk_func.foo(...)` 函数指针表派发；`gl/` 层无直接 `vk*`/`vkCmd*` 调用（仅注释中提及）；`init.cpp` 引导与 `MITHRIL_ENSURE_INIT` 的 `backend_available()` 保留
- [ ] Task 3: 分布式 per-context 状态（替换中心化 GLState）
  - [x] SubTask 3.1: 定义各子系统状态结构与 per-context 表：`gl/buffer_state.h`、`gl/texture_state.h`、`gl/framebuffer_state.h`、`gl/enable_state.h`（`mg_enable_state_t`，独立结构）、`gl/pixel_state.h`、`gl/mg_state.h`（含 `gl_state_s`）、`gl/program_state.h`、`gl/shader_state.h`、`gl/vertexattrib_state.h`（VAO）、`gl/sync_state.h`、`gl/query_state.h`、`gl/transformfeedback_state.h`；每个子系统 `unordered_map<ctx_id, unique_ptr<state>>` + `thread_local` 当前指针（`_state.h` 后缀避免与现有 `gl/Framebuffer.h`/`gl/Shader.h` 大小写冲突）
  - [x] SubTask 3.2: 实现 `mg_<subsystem>_bind_context(ctx_id, group_id)` / `mg_<subsystem>_forget_context(ctx_id)` 钩子（12 个子系统，含 sync/query/tf no-op 桩）；`mg_context_make_current` / `mg_context_destroy` 统一分发全部 12 个钩子
  - [ ] SubTask 3.3: 提供状态访问器（`mg_buffer_current()` / `mg_texture_current()` / `mg_enable_current()` / `mg_framebuffer_current()` / `mg_program_current()` / `mg_vertexattrib_current()` / `mg_pixel_current()` / `mg_gl_current()`）—— 访问器已就位（subview 回退到 `g_state`）；**待迁移**：原 `g_state->` 全部访问点（`gl/*.cpp` 与 `MG_Backend/DirectVulkan/*.cpp`）改读访问器，这是后续独立任务
  - [ ] SubTask 3.4: `DirectVulkan/*`（Device/Resources/Pipeline/CommandStream/DescriptorSet）改读分布式状态访问器；EGL 默认帧缓冲 `VkImageView` 字段迁移到 framebuffer 子系统默认 FBO 状态
- [x] Task 4: 着色器翻译模块 gl/glsl/
  - [x] SubTask 4.1: 创建 `gl/glsl/glsl_for_vk.{cpp,h}`：迁移 `gl/Shader.cpp` 全部翻译逻辑（预处理步骤 1-8 + glslang `EShClientOpenGL` + `EShMsgVulkanRules` + 两级 strict 回退 wrapped→unwrapped + 线程安全 `std::mutex`）；spirv_cross 反射保留在 `MG_Backend/DirectVulkan/{Reflect,DescriptorSet}.cpp`（本模块不重复）
  - [x] SubTask 4.2: 创建 `gl/glsl/cache.{cpp,h}`：SHA-256 LRU 持久化缓存（内联 FIPS 180-4 SHA-256、`std::list`+`unordered_map` LRU、`.new`+`rename` 原子磁盘 load/save、增量 flush `kPendingEntriesBeforeSave=16`/`kSaveIntervalNs=5e9`、LRU 256 条淘汰、`thread_local` digest 复用、`std::mutex` 线程安全）；`MITHRIL_GLSL_CACHE` 环境变量自动 load
  - [x] SubTask 4.3: `gl/Shader.cpp` 改为薄转发层（保留 `mithril::shader_translate`/`mithril::get_fallback_spirv` 公开 API，转发到 `mithril::glsl::` 对应函数）；翻译入口接入缓存（命中直接返回 SPIR-V，未命中翻译后 `cache_put`）；`CMakeLists.txt` 添加新源；fallback shader 不进持久化缓存（保留原行为）
- [x] Task 5: MGContext 上下文模型
  - [x] SubTask 5.1: 创建 `egl/context.{h,cpp}`，定义 `MGContext`（单调 `id`、`display`、`handle`、`client_type`、`granted_major/minor`、`profile_mask`、`share_group`、`state`、`current_count`、`destroy_pending`）+ 全局 `unordered_map<EGLContext, shared_ptr<MGContext>>` + `thread_local MGContext* g_current_ctx` + `thread_local shared_ptr<MGContext> g_current_ref`
  - [x] SubTask 5.2: `eglMakeCurrent` 调度各子系统 `bind_context`；`mg_context_destroy` 调 `forget_context`；display 引用计数（probe/app 双 bool，`mg_display_initialised` / `mg_display_release`）
  - [x] SubTask 5.3: `MGContext` 作为 `EglContext` 的附加追踪层（additive layer）—— `EglContext` 仍拥有 `GLState` 与 refcount；`MGContext` 仅追踪元数据 + 单调 id + share group。EGL 1.5 Sync/Image 影子实现保留在 `egl.cpp` 未受影响
- [x] Task 6: 回归验证（保留所有已修复补丁）
  - [x] SubTask 6.1: 校验 OOM 主动 GC（`backend_proactive_gc_if_needed` + `backend_poll_completed_frames`，Device.cpp:334/400/429）、per-frame transient staging arena（Device.cpp:1252 + UniformArena.cpp）、`safe_device_wait_idle`（ImageOps.cpp:228/439 + Device.h:137）、swapchain 三级降级（SwapchainCommon.cpp:154-183）、deviceLost 恢复（CommandStream.h:96-110 + Device.h:247-249）、pipeline 负缓存清除（Pipeline.h:130-141 `failedSignatures`） 仍生效
  - [ ] SubTask 6.2: `nm -gU` 校验导出符号含 `glFoo` + `glFooARB` 别名（**需 macOS CI**：`__attribute__((alias))` 在 ELF 生效；Mach-O 按 MobileGlues 惯例仅导出 `glFoo`；`NATIVE_FUNCTION_HEAD` 宏机制 + `-Wl,-Bsymbolic-functions` 链接标志已验证就位）
  - [ ] SubTask 6.3: 构建 iOS arm64 dylib + 现有测试（`tests/`、`verify/`）通过（**iOS arm64 dylib 需 macOS CI**：Linux 沙箱无 MoltenVK 与 iOS 工具链；`syntax_check.sh` 36/36 通过 + `link_check.sh` 无未定义 mithril:: 符号 — Linux 上可验证的部分已通过）

# Task Dependencies
- Task 2 依赖 Task 1（骨架与 `NATIVE_FUNCTION_HEAD` 宏、`vk_func_t`）
- Task 3 与 Task 2 可部分并行（子系统状态结构定义不依赖入口拆分；SubTask 3.3 迁移访问点须在 Task 2 拆分后）
- Task 4 依赖 Task 1（模块位置与 CMake 源）
- Task 5 依赖 Task 3（`bind_context` / `forget_context` 钩子须先就位）
- Task 6 依赖 Task 1-5 全部完成
