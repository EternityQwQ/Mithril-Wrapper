# 渲染器重构为 MobileGlues 转译架构 Spec

## Why
Mithril-Wrapper 当前渲染器采用中心化 `mithril::GLState` 大结构 + `MG_Impl/` 扁平入口，与 MobileGlues 的转译架构差异显著（MobileGlues 用三层 `gl/` → 后端函数指针表 → `egl/`、分布式 per-context 状态、符号抢占派发、glslang→SPIR-V→spirv_cross shader 管线 + 持久化缓存）。为统一转译层设计范式、提升可维护性与跨子系统一致性，需将渲染器重构为与 MobileGlues 一模一样的转译架构，同时**保留 Vulkan 1.2 + MoltenVK 后端身份**（参考已克隆到 `.refs/MobileGL` 的 MobileGL Vulkan 后端）。

> 架构选型（已与用户确认）：**混合架构** — 采用 MobileGlues 三层结构 + 分布式状态 + 符号抢占派发 + shader 翻译管线，但后端函数指针表指向自研 Vulkan 后端（而非 native GLES）。**核心范围** — 前端文件重组 + 分布式 per-context 状态 + shader 翻译管线 + 符号抢占派发；DSAWrapper / MultiBindWrapper / multidraw / FSR1 / transfer / config 留作 Phase 2。

## What Changes
- **BREAKING** 目录重组：`Mithril-Wrapper-cpp/MG_Impl/` → `Mithril-Wrapper-cpp/gl/`（MobileGlues 风格文件拆分：`buffer/drawing/enable/framebuffer/getter/program/shader/texture/transfer/vertexattrib/pixel/mg/gl_native/gl_stub/log`）
- **BREAKING** 状态管理重构：移除中心化 `mithril::GLState` 大结构，改为 MobileGlues 风格分布式 per-context 状态（每个子系统 `flat_hash_map<ctx_id, unique_ptr<state>>` + `thread_local` 当前指针 + `mg_<subsystem>_bind_context` / `mg_<subsystem>_forget_context` 钩子）
- 新增后端函数指针表 `vk_func_t`（类比 MobileGlues `g_gles_func`）：`MG_Backend/Backend.h` 的 `backend_*` C API 收敛为函数指针表派发，`DirectVulkan/*.cpp` 在 `backend_init` 填充该表；前端仅通过 `vk_func.foo(...)` 调用后端
- 新增符号抢占派发：CMake 链接 `-Wl,-Bsymbolic-functions`；引入 `NATIVE_FUNCTION_HEAD` / `NATIVE_FUNCTION_END` 宏，导出 `glFoo` + `glFooARB` 别名（`__attribute__((alias))`）
- 着色器翻译模块重组：`MG_Impl/Shader.cpp` → `gl/glsl/`（`glsl_for_vk.cpp` + `cache.cpp`），采用 MobileGlues 风格 SHA-256 LRU **持久化**缓存（磁盘 load/save + 增量 flush：累计 16 条或 5 秒触发）
- EGL 上下文模型对齐 MobileGlues `MGContext`：单调递增 `id`（不复用 EGLContext 句柄，避免驱动句柄回收悬垂）、`shared_ptr<MGContext>` + `current_count`、share group、display 引用计数
- 保留 Vulkan 1.2 + MoltenVK 后端与**所有已修复**的崩溃/OOM/红黑屏补丁（见 `spec.md` 第七节与 `specs/fix-*`）
- **不在本 spec 范围**（Phase 2 后续）：DSAWrapper、MultiBindWrapper、multidraw、FSR1、transfer 格式转换、config/settings/stats

## Impact
- **Affected specs**：`specs/rewrite-gl-state-machine`（中心化 GLState 被替换为分布式状态）、`specs/implement-gl33-core-renderer`（入口层重组）、`specs/fix-red-black-screen*` 与 `specs/fix-red-screen-root-causes-*`（所有已修复补丁须在重构后保留并回归验证）
- **Affected code**：
  - 目录重组：`Mithril-Wrapper-cpp/MG_Impl/` → `Mithril-Wrapper-cpp/gl/`（全部 `.cpp/.h`）
  - 状态机：`Mithril-Wrapper-cpp/MG_State/State.{h,cpp}` 拆分为 `gl/buffer.{h,cpp}`、`gl/texture.{h,cpp}`、`gl/framebuffer.{h,cpp}`、`gl/enable.{h,cpp}`、`gl/pixel.{h,cpp}`、`gl/mg.{h,cpp}`（含 `gl_state_s`）+ per-context 状态表
  - 后端：`Mithril-Wrapper-cpp/MG_Backend/Backend.h`（重构为 `vk_func_t` 函数指针表）；`DirectVulkan/*.cpp` 填充表 + 改读分布式状态访问器（替代直接读 `g_state`）
  - 着色器：`Mithril-Wrapper-cpp/gl/glsl/{glsl_for_vk.cpp,cache.cpp,cache.h,glsl_for_vk.h}`
  - EGL：`Mithril-Wrapper-cpp/egl/{egl.cpp,EglInternal.h,context.h,context.cpp}`（MGContext 模型）
  - 构建：`CMakeLists.txt`（源路径 `MG_Impl`→`gl`、`-Wl,-Bsymbolic-functions`、`gl/glsl/` 源）
  - CI：`.github/workflows/build.yml`（校验导出符号含 `glFooARB` 别名）

## ADDED Requirements

### Requirement: 三层转译结构（gl/ → 后端函数指针表 → egl/）
系统 SHALL 采用 MobileGlues 三层转译架构：`gl/` 前端导出 `glFoo` 并做 GL 3.3 Core → 后端翻译；后端函数指针表 `vk_func_t`（类比 `g_gles_func`）持有所有后端入口指针；`egl/` 翻译 EGL 上下文请求并跟踪上下文。前端 SHALL 仅通过 `vk_func_t` 函数指针表调用后端，**不直接调用任何 `vk*` / `vkCmd*` API**。

#### Scenario: 前端调用经函数指针表派发
- **WHEN** GL 前端执行一次绘制（如 `glDrawArrays`）
- **THEN** 调用经 `vk_func.draw_arrays(...)`（或等价函数指针）进入 `DirectVulkan` 实现，`gl/` 层无直接 `vkCmd*` 调用

### Requirement: 分布式 per-context 状态
系统 SHALL 移除中心化 `GLState`，改为每个子系统（buffer/texture/framebuffer/enable/pixel/program/shader/vao/sync/query/tf）维护 `flat_hash_map<ctx_id, unique_ptr<state>>` + `thread_local` 当前指针（可沿用 `std::unordered_map`）。`mg_context_make_current` SHALL 在上下文切换时统一调度各子系统的 `bind_context` 钩子；上下文销毁 SHALL 调用 `forget_context` 钩子清理 per-context 状态。

#### Scenario: 上下文切换正确切换子系统状态
- **WHEN** `eglMakeCurrent` 切换到上下文 B
- **THEN** 所有子系统的 `thread_local` 当前指针指向 B 的状态；后续 `gl*` 调用操作 B 的对象表与渲染状态

#### Scenario: 上下文销毁不泄漏
- **WHEN** 销毁上下文 B（所有线程释放后）
- **THEN** 各子系统 `forget_context(B)` 移除 B 的状态条目并释放其 Vulkan 资源

### Requirement: 符号抢占派发
系统 SHALL 通过 `extern "C"` 导出 `glFoo`，并为每个入口提供 `glFooARB` 别名（`__attribute__((alias))`）。构建 SHALL 链接 `-Wl,-Bsymbolic-functions`，确保库内部 TU 间的 `gl*` 调用不被宿主进程全局作用域中的系统 GL 库抢占。

#### Scenario: 内部调用不被系统库抢占
- **WHEN** 库内部 TU 调用 `glBindBuffer`
- **THEN** 调用解析到本库的 mithril 实现，而非宿主进程中的系统 `libGLESv2` / `libGL`

### Requirement: 着色器翻译模块（glslang→SPIR-V + spirv_cross 反射 + SHA-256 LRU 持久化缓存）
系统 SHALL 将着色器翻译重组到 `gl/glsl/` 模块，采用 MobileGlues 风格管线：预处理（注入 `MG_MITHRIL` / `MG_MITHRIL_VERSION` 宏、GLSL 版本归一、`layout(packed/shared)`→`std140` / SSBO→`std430` 归一、`gl_FragColor`→合成命名输出、`glBindAttribLocation` → `layout(location=N)` 注入）→ glslang GLSL→SPIR-V（`EShClientOpenGL` + `EShTargetSpv_1_5` + `EShMsgVulkanRules`）→ spirv_cross **反射**（构建描述符布局）→ SPIR-V 交由 MoltenVK 在 `vkCreateShaderModule` 内部翻译为 MSL。系统 SHALL 提供 SHA-256 LRU **持久化**缓存（磁盘 load/save，键 = SHA-256(源码 + MG 版本)，累计 16 条或距上次 save 5 秒触发增量 flush，LRU 超限淘汰）。

> **关键区别**：因后端为 Vulkan（消费 SPIR-V），spirv_cross 的角色是**反射**（构建 `VkDescriptorSetLayout`），**非** MobileGlues 的 SPIR-V→GLSL ES 反编译。本 spec 复刻 MobileGlues `gl/glsl/` 的**模块结构与缓存机制**，spirv_cross 用法保持反射。

#### Scenario: 着色器翻译命中缓存
- **WHEN** 同一 GLSL 源码再次编译
- **THEN** 命中 SHA-256 LRU 缓存，跳过 glslang / spirv_cross 重复工作

#### Scenario: 缓存持久化跨进程重启
- **WHEN** 进程重启后编译相同着色器
- **THEN** 从磁盘缓存加载命中

### Requirement: MGContext 上下文模型
系统 SHALL 采用 MobileGlues `MGContext` 模型：单调递增 `id`（不复用 EGLContext 句柄以避免驱动句柄回收悬垂）、`shared_ptr<MGContext>` + `current_count`（destroy 后存活到所有线程释放）、share group（跨上下文共享对象表）、display 引用计数（probe/app 双 bool 跟踪 `eglInitialize`/`eglTerminate` 归属）。

#### Scenario: 句柄回收不导致悬垂
- **WHEN** 驱动回收 EGLContext 句柄地址并分配给新上下文
- **THEN** 系统以单调 `id` 区分两者，不发生状态混淆

## MODIFIED Requirements

### Requirement: GL 3.3 Core Profile 入口层
入口层文件从 `MG_Impl/` 重组为 `gl/`（`buffer/drawing/enable/framebuffer/getter/program/shader/texture/transfer/vertexattrib/pixel/mg/gl_native/gl_stub/log`），每个入口通过 `NATIVE_FUNCTION_HEAD` 宏导出 `glFoo` + `glFooARB` 别名，翻译后调用经 `vk_func_t` 函数指针表进入 Vulkan 后端。`gl_native.cpp` 承载 1:1 透传入口（`vk_func.foo(...)` 直传），`gl_stub.cpp` 承载 deprecated GL 1.x/2.x no-op 入口。

### Requirement: Vulkan 1.2 + MoltenVK 后端
后端保留 Vulkan 1.2 + MoltenVK（静态链接）实现，`DirectVulkan/*.cpp` 在 `backend_init` 填充 `vk_func_t` 函数指针表；`Device`/`Resources`/`Pipeline`/`CommandStream`/`DescriptorSet`/`Swapchain` 改为从分布式状态访问器读取 GL 状态（替代直接读 `g_state`）。所有已修复补丁（OOM 主动 GC、per-frame transient staging arena、`safe_device_wait_idle`、swapchain 三级降级、deviceLost 恢复、pipeline 负缓存清除、MVK 配置等，见 `spec.md` 第七节与 `specs/fix-*`）SHALL 保留并回归通过。

### Requirement: EGL 1.5
EGL 保留 Vulkan 后端实现（EGLDisplay→单例 Vulkan 实例/设备，EGLSurface→swapchain，EGLContext→`MGContext`）。`eglCreateContext` 翻译 desktop GL context 请求为后端 context；上下文跟踪采用 `MGContext` 模型。EGL 1.5 Sync/Image 影子实现保留（不创建真实 `VkFence`/`VkImage`）。

## REMOVED Requirements

### Requirement: 中心化 GLState 大结构
**Reason**: 替换为 MobileGlues 风格分布式 per-context 状态，消除单一巨大状态结构与跨子系统耦合
**Migration**: 各子系统提供访问器（如 `mg_buffer_get(id)`、`mg_texture_current()`、`mg_enable_current()`），原 `g_state->buffers[id]` / `g_state->textures` / `g_state->blends` 等访问迁移至对应访问器；Vulkan 后端通过访问器读取状态。`thread_local GLState* g_state` 替换为各子系统的 `thread_local` 当前指针。原 `GLState` 中的 EGL 默认帧缓冲 `VkImageView` 字段迁移到 framebuffer 子系统的默认 FBO 状态。
