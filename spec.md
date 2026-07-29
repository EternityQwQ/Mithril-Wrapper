# Mithril-Wrapper 渲染崩溃修复规范（深度参考 MobileGL）

## 一、架构概述

### 1.1 渲染器分层

```
┌─────────────────────────────────┐
│  Minecraft Java (1.21.1)        │  ← 调用 GL/EGL API
├─────────────────────────────────┤
│  EGL 层 (egl/egl.cpp)          │  ← eglSwapBuffers / eglMakeCurrent
│  - swapchain 生命周期管理        │     窗口事件 / deviceLost 恢复
├─────────────────────────────────┤
│  GL 实现层 (MG_Impl/)           │  ← glDrawArrays / glClear / glBindTexture
│  - Drawing.cpp / Framebuffer.cpp │     GL 状态 → VkCmd 转换
├─────────────────────────────────┤
│  Vulkan 后端 (MG_Backend/)      │  ← 直接调用 Vulkan 1.2 API
│  - DirectVulkan/                │     MoltenVK 将 SPIR-V → MSL → Metal
│    Pipeline.cpp (着色器)         │
│    CommandStream.cpp (命令流)    │
│    Resources.cpp (资源)          │
│    SwapchainCommon.cpp (交换链)  │
│    Device.cpp (设备)             │
├─────────────────────────────────┤
│  MoltenVK (静态链接)             │  ← Vulkan → Metal 翻译层
│  - SPIR-V → MSL 着色器编译       │     IOSurface 管理
│  - VkImage → MTLTexture         │
├─────────────────────────────────┤
│  Metal 2 / iOS GPU Driver       │  ← Apple A15 GPU
└─────────────────────────────────┘
```

### 1.2 帧渲染流程

```
eglSwapBuffers()
  ├─ install_surface_on_state()      // 获取 swapchain 图像
  │   └─ backend_swapchain_acquire_color()  // vkAcquireNextImageKHR
  │       └─ 记录 layout barrier (PRESENT → COLOR_ATTACHMENT)
  ├─ backend_end_render_pass()       // 结束动态渲染 pass
  ├─ backend_commit()                // 提交命令缓冲区
  │   └─ commit_frame()
  │       ├─ vkEndCommandBuffer()
  │       ├─ layout barrier (COLOR_ATTACHMENT → PRESENT)
  │       ├─ vkQueueSubmit()          // 提交到 GPU 队列
  │       └─ advance to next frame slot
  └─ backend_present_and_acquire()
      ├─ vkQueuePresentKHR()          // 呈现到屏幕
      └─ vkAcquireNextImageKHR()     // 获取下一帧图像
```

### 1.3 关键数据结构

| 结构 | 文件 | 作用 |
|------|------|------|
| `Backend` | Device.h:51-147 | 全局 Vulkan 设备状态、命令缓冲、栅栏、deviceLost 标记 |
| `Swapchain` | Swapchain.h:6-80 | 交换链状态、图像视图、深度缓冲、信号量 |
| `EncoderState` | CommandStream.cpp:20-57 | 编码器状态：活动管线、clear 值、attachments |
| `ProgramResources` | Pipeline.h:14-45 | 着色器模块、管线缓存、负缓存、descriptor layout |
| `TextureEntry` | Resources.h:24-45 | 纹理图像、内存、staging buffer、采样器 |

## 二、问题诊断（来自用户日志分析）

### 2.1 崩溃链

```
1. 显存不足 (VK_ERROR_OUT_OF_DEVICE_MEMORY)
   ├─ MoltenVK 报告 maxMemoryAllocationCount = 1073741824 (2^30，不可信)
   ├─ 真实限制：iOS 设备物理内存 4GB，Jetsam 限制 2092MB
   ├─ 每张纹理 2 次 vkAllocateMemory (image + staging buffer 永久持有)
   └─ MTLCommandBuffer 执行失败 (code 9): Invalid Resource

2. GPU 超时 (VK_TIMEOUT)
   ├─ 显存耗尽 → GPU 无法正常处理任务
   └─ kIOGPUCommandBufferCallbackErrorTimeout

3. 设备丢失 (VK_ERROR_DEVICE_LOST)
   ├─ 连续错误 → GPU 进入不可恢复状态
   └─ MTLCommandBuffer Ignored (for causing prior/excessive GPU errors)

4. 着色器编译失败 (红屏)
   ├─ deviceLost 后 MoltenVK 内部 MSL 编译器状态异常
   ├─ 错误: "invalid type 'main0_in'"
   └─ 失败签名被永久缓存 → 所有 draw 跳过 → 红屏

5. exit(0) 退出
   └─ 恢复间隔过长 → Java 层检测到渲染失败 → 退出
```

## 三、已修复问题（全部 commit）

### 3.1 显存管理修复

| 修复 | 文件 | 行号 | 说明 |
|------|------|------|------|
| maxMemoryAllocationCount 钳制 | Device.cpp | 305-315 | MoltenVK 报告 1073741824 → 钳制到 4096 |
| 分配计数器和阈值警告 | Resources.cpp | 60-71 | 超过 80% 时警告 |
| staging buffer 延迟释放 | Resources.cpp | 258-271 | 纹理上传后进 disposalQueue，不再永久持有 |
| depth buffer OOM 降级禁用 | SwapchainCommon.cpp | 170-177 | swapchain 走降级路径时禁用 depth，节省 ~8-43MB |
| deviceLost 期间主动 drain | Device.cpp | 64-84 | 恢复前先 vkDeviceWaitIdle + drain，释放显存 |
| **OOM 主动 GC** | **Resources.cpp** | **44-78** | **vkAllocateMemory 失败时 vkDeviceWaitIdle + drain_all_disposal_queues 后重试** |
| **default_texture 泄漏修复** | **DescriptorSet.cpp** | **266-292** | **view/sampler 创建失败时销毁已分配的 image+memory，避免重试覆盖句柄泄漏** |

### 3.2 设备丢失恢复修复

| 修复 | 文件 | 行号 | 说明 |
|------|------|------|------|
| 恢复间隔 60→10 帧 | egl.cpp | 813-816 | 0.17 秒一次，避免 Java 层检测到失败 |
| 恢复前先释放显存 | egl.cpp | 829 | 调用 backend_reset_device_lost_pending_resources() |
| 恢复失败限流日志 | egl.cpp | 853-860 | 首次+每30次打印一条 |
| 恢复成功打印尝试次数 | egl.cpp | 842-850 | 便于诊断恢复周期 |

### 3.3 红屏修复（着色器缓存）

| 修复 | 文件 | 行号 | 说明 |
|------|------|------|------|
| 瞬态失败不缓存 | Pipeline.cpp | 500-551 | OOM/deviceLost/初始化失败不加入负缓存 |
| 恢复时清除 pipeline 缓存 | Pipeline.cpp | 571-591 | clear_all_pipeline_caches() 清除负缓存+销毁 pipeline |
| 恢复时调用清除 | Device.cpp | 47-63 | backend_reset_device_lost() 内调用 |

### 3.4 日志刷屏修复

| 修复 | 文件 | 行号 | 说明 |
|------|------|------|------|
| MoltenVK debug callback 去重 | Device.cpp | 127-168 | FNV-1a 哈希去重，相同消息每500次一条 |
| vkWaitForFences 失败限流 | CommandStream.cpp | 211-222 | 首次+每100次 |
| vkBeginCommandBuffer 失败限流 | CommandStream.cpp | 238-253 | 首次+每100次 + 标记 deviceLost |
| vkEndCommandBuffer 失败限流 | CommandStream.cpp | 784-808 | 首次+每100次 + 标记 deviceLost |
| vkAcquireNextImageKHR 失败限流 | SwapchainCommon.cpp | 337-356 | 首次+每100次 |
| unsupported internalFormat 去重 | Resources.cpp | 529-541 | unordered_set 去重，每种格式只打印一次 |
| generate_mipmaps 失败限流 | ImageOps.cpp | 105-114 | 首次+每100次 |

### 3.5 其他修复

| 修复 | 文件 | 说明 |
|------|------|------|
| swapchain 创建降级重试 | SwapchainCommon.cpp | 3 级降级：imgCount 减少 → usage 减少 → 最小 usage |
| 退避防死循环 | egl.cpp | ensure_swapchain 失败后 30 帧重试 |
| 描述符池扩容 | DescriptorSet.cpp | 256→1024，池耗尽时重置重试 |
| renderArea clamp | CommandStream.cpp | IOSurfaceBindAccel SIGSEGV 修复 |
| VK_KHR_portability_subset 特性 | Device.cpp | 384-403 | 必需结构体链入 VkDeviceCreateInfo |
| MVK_CONFIG_RESUME_LOST_DEVICE=1 | Device.cpp | 201 | MoltenVK 自动恢复 deviceLost |

## 四、与 MobileGL 的对比

| 方面 | MobileGL | Mithril（修复后） |
|------|----------|-------------------|
| 内存分配 | VMA sub-allocation，几百个 allocation 占少数配额 | 独立 vkAllocateMemory，配额钳制到 4096 |
| staging buffer | 同步销毁，upload 后立即释放 | 延迟释放进 disposalQueue |
| deviceLost 恢复 | VK_VERIFY 硬断言→abort（无恢复） | 10 帧间隔重试 + drain + 清除 pipeline 缓存 |
| pipeline 缓存 | RecreateSwapchain 时 DestroyAll() | deviceLost 恢复时 clear_all_pipeline_caches() |
| 着色器失败 | 不做负缓存，每帧重试 | 区分瞬态/永久失败，瞬态不缓存 |
| 日志控制 | 无去重，硬断言即 abort | 全链路去重限流 |
| present 黑屏 | 有 presentSuspended 路径 | 有 deviceLost 恢复路径 |

## 五、关键架构修复（rendering suspended 根因）

### 5.1 移除过早的 deviceLost 挂起机制

**问题**：原实现在 `vkQueueSubmit` / `vkQueuePresentKHR` 连续失败 3 次后就设置 `deviceLost=true`，导致 "Persistent GPU fault detected — rendering suspended"。这让 OOM 变成永久性挂起，即使显存后续释放也无法恢复。

**根因分析**（深度参考 MobileGL）：
- MobileGL 用 `VK_VERIFY(vkQueueSubmit(...))` 在失败时直接 abort 进程
- MobileGL **不会** OOM，因为它在每帧 present 前调用 `TryDrainFrameTransients()` 主动释放资源
- Mithril 的 `consecutiveSubmitFailures >= 3` 机制是我自己加的有害逻辑

**修复**：
- `vkQueueSubmit` 失败：按错误类型分类处理
  - `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR` → 标记 swapchain 重建，不挂起
  - `VK_ERROR_DEVICE_LOST` → 设置 deviceLost（真正的设备丢失），EGL 恢复路径处理
  - `VK_ERROR_OUT_OF_DEVICE_MEMORY` / 其他 → **触发 OOM GC**（vkDeviceWaitIdle + drain），跳过当前帧，**不挂起**
- `vkQueuePresentKHR` 失败：同样的分类策略
- OOM 不再导致永久挂起，每帧 OOM 时触发 GC 释放资源，下一帧重试

### 5.2 OOM 主动 GC（已在 3.1 实现）

`try_allocate_memory_with_gc()` 在 `vkAllocateMemory` 失败时触发 GC 重试。现在 submit/present 失败时也触发同样的 GC。

## 六、关键架构修复 v2（主动式 GC — 彻底解决显存耗尽）

### 6.1 问题：反应式 GC 不够，OOM 仍发生

经过 5.1/5.2 的修复，"rendering suspended" 不再出现，但用户反馈 GPU OOM / Device Lost 仍发生。

**深度分析**：5.1/5.2 的 GC 都是**反应式**（reactive）—— 在 `vkAllocateMemory` 失败**之后**才触发 GC。但此时：
1. GPU 可能已进入降级状态（timeout 错误开始级联）
2. MoltenVK 可能已返回 `VK_ERROR_DEVICE_LOST`
3. 即使 GC 释放了显存，设备状态已不可恢复

**MobileGL 的真正秘诀**：MobileGL 是**主动式**（proactive）—— 每帧 `Present()` 末尾调用：
```cpp
m_textureManager->BeginFrame(frameIndex);   // CollectAllDeferredReleases
m_bufferManager.BeginFrame(frameIndex);      // CollectAllDeferredReleases
m_uniformManager->BeginFrame(frameIndex);    // rewind descriptor cursors
```
这些 `BeginFrame` 在**新帧开始前**释放上一帧已完成的延迟资源，**永远不让显存累积到 OOM**。MobileGL 的 `vkAllocateMemory` 根本不会失败。

### 6.2 修复：三层主动式 GC 策略

#### 6.2.1 `backend_poll_completed_frames()` — 非阻塞轮询 drain

**文件**: `Device.cpp:127-168`, `Device.h:189-209`

用 `vkGetFenceStatus`（非阻塞）轮询所有 frame slot 的 fence。对已 signal 的 slot 立即 drain 其 `disposalQueue`。

参考 MobileGL `RefreshCompletedSubmits`（VulkanRenderer.cpp:7199-7237）+ `CollectAllDeferredReleases`。

**关键区别**：原实现只在 `ensure_command_buffer_recording` 复用某个 slot 时才 drain **该 slot**。现在能 drain **任意已完成 slot**，即使当前不在复用它。`kMaxFramesInFlight=2` 时，slot 0 的 GPU 工作可能在 slot 1 录制期间就完成，原实现要等下次循环回 slot 0 才 drain，现在立即 drain。

**调用点**（3 处，镜像 MobileGL 的多 drain 点）：
1. `eglSwapBuffers` 开头（`egl.cpp:816`）— 新帧渲染前释放已完成帧资源
2. `commit_frame` 成功路径末尾（`CommandStream.cpp:1019`）— submit 后立即释放其他 slot
3. `backend_proactive_gc_if_needed` 内部（`Device.cpp:202`）— 压力 GC 先尝试非阻塞

#### 6.2.2 `backend_proactive_gc_if_needed()` — 内存压力阈值 GC

**文件**: `Device.cpp:173-226`, `Device.h:211-224`

当 `currentAllocationCount >= 70% * maxMemoryAllocationCount` 时，**在分配前**主动触发 GC：
1. 先非阻塞 `backend_poll_completed_frames()`（可能已足够）
2. 若仍超阈值且有 deferred 资源，才 `vkDeviceWaitIdle + drain_all_disposal_queues`

**调用点**: `try_allocate_memory_with_gc` 开头（`Resources.cpp:59`）— 每次 `vkAllocateMemory` 前检查压力。

这把 GC 从"失败后兜底"提升为"压力前预防"，在 GPU 进入降级状态前释放显存。

#### 6.2.3 调用流程图

```
eglSwapBuffers()
  ├─ backend_poll_completed_frames()     ← 6.2.1: 新帧前 drain 已完成 slot
  ├─ [deviceLost 恢复路径]
  ├─ backend_end_render_pass()
  ├─ backend_commit() → commit_frame()
  │   ├─ vkQueueSubmit()
  │   └─ backend_poll_completed_frames() ← 6.2.1: submit 后 drain 其他 slot
  ├─ backend_present_and_acquire()
  └─ [swapchain rebuild if needed]

try_allocate_memory_with_gc()           ← 每次纹理/buffer 分配
  ├─ backend_proactive_gc_if_needed()   ← 6.2.2: 70% 阈值主动 GC
  ├─ vkAllocateMemory()
  └─ [失败后 GC 重试 — 5.2 的兜底]
```

### 6.3 预期效果

- **显存峰值降低**：staging buffer 在 GPU 完成后立即释放（同帧或下一帧开头），不再累积 2 帧
- **OOM 发生前预防**：70% 阈值触发 GC，避免撞硬限制导致 GPU timeout/device lost 级联
- **非阻塞优先**：`vkGetFenceStatus` 立即返回，只在真正需要时才 `vkDeviceWaitIdle`
- **多 drain 点**：镜像 MobileGL 的 Present/Flush/Wait 多点 drain 策略

### 6.4 长期改进：引入 VMA（Vulkan Memory Allocator）— 低优先级

- 替代独立 `vkAllocateMemory`，减少 allocation 数量
- 子分配（sub-allocation）将一个 `VkDeviceMemory` 切分给多个 buffer/image
- 需要完整重构 Resources.cpp 的分配路径
- 当前三层主动式 GC + 钳制到 4096 + staging buffer 延迟释放已足够稳定

## 七、二次深度调查修复（MobileGL 全量对比）

基于对 MobileGL 完整源码（`.mobilegl_analysis/`）的 7 维度深度调查——Swapchain 重建、ImageLayout/Barrier、TextureManager deferred release、DescriptorSet/Uniform、MoltenVK 配置、deviceLost 处理、Pipeline 缓存——发现 Mithril 已对标或超越 MobileGL 的大部分机制，但仍存在 3 个关键缺口。

### 7.1 MobileGL 调查核心发现

**MobileGL 的设计哲学**：`VK_VERIFY` 失败即 abort（`VkIncludes.h:55-64`），无 deviceLost 恢复、无 OOM GC、无 swapchain 降级。它依赖"永远不失败"的前提——通过 VMA sub-allocation + 每帧 `BeginFrame` 主动 drain + 每 64 帧 `PruneDeadTextures` GC 来保证。

**Mithril 已超越 MobileGL 的点**（无需再改）：
- ✅ MoltenVK 配置（`MVK_CONFIG_RESUME_LOST_DEVICE=1` 等，`Device.cpp:329-333`）— MobileGL 完全无配置
- ✅ `maxMemoryAllocationCount` 钳制到 4096（`Device.cpp:425-429`）— MobileGL 不查询
- ✅ 主动式 GC（`backend_proactive_gc_if_needed` + `backend_poll_completed_frames`）— MobileGL 无压力触发 GC
- ✅ deviceLost 恢复 + pipeline 负缓存清除（`Device.cpp:40-62`, `Pipeline.cpp:558-591`）— MobileGL 无恢复
- ✅ swapchain 三级降级（`SwapchainCommon.cpp:144-177`）— MobileGL 无降级
- ✅ TransitionToPresent 时序正确（barrier 在 vkEndCommandBuffer 前，`CommandStream.cpp:777-787`）— 对标 MobileGL `VulkanRenderer.cpp:7609`
- ✅ imageAvailable semaphore 等待（`CommandStream.cpp:821-840`）— 对标 MobileGL `FrameContext.cpp:234`
- ✅ 零尺寸窗口守卫（`egl.cpp:929-932`）— 对标 MobileGL commit 7ab8386
- ✅ DescriptorSet cursor rewind + 耗尽 reset+retry（`DescriptorSet.cpp:334-374`）— 对标 MobileGL UniformManager

### 7.2 修复缺口 1：VK_TIMEOUT acquire 处理（根因修复）

**文件**: `SwapchainCommon.cpp:338-385`

**问题**：原实现把 `VK_TIMEOUT`（GPU watchdog 触发）和 `VK_ERROR_OUT_OF_DEVICE_MEMORY` 一样标记 `needsRebuild=true`，导致 swapchain 被销毁重建。但重建 swapchain 不能解决 GPU 卡住的问题——新 swapchain 下次 acquire 还会 `VK_TIMEOUT`，形成"acquire timeout → rebuild → acquire timeout"死循环，日志中反复出现 `VK_TIMEOUT`。

**修复**：`VK_TIMEOUT` 时**不**标记 `needsRebuild`，而是：
1. `vkDeviceWaitIdle` + `drain_all_disposal_queues` 释放显存
2. 清除所有 `fencePending`（vkDeviceWaitIdle 后所有 fence 已 signaled）
3. 标记 `imageAvailableConsumed = true`（避免后续等待未定义状态的 semaphore）
4. 跳过本帧，swapchain 保留，下帧重试

### 7.3 修复缺口 2：MVK_CONFIG_SUBMIT_COMMAND_BUFFERS_PER_QUEUE

**文件**: `Device.cpp:317-333`

**问题**：MoltenVK 默认允许 64 个 command buffer 并发，每个都预分配 Metal 资源（编码器、IOSurface 引用）。在 iPhone SE 3 上这会加剧显存压力。

**修复**：设置 `MVK_CONFIG_SUBMIT_COMMAND_BUFFERS_PER_QUEUE=2`（匹配 `kMaxFramesInFlight`），从默认 64 降到 2，显著降低峰值显存占用。

### 7.4 修复缺口 3：acquire 失败后 imageAvailable semaphore 状态清理

**文件**: `SwapchainCommon.cpp:382, 406`

**问题**：`vkAcquireNextImageKHR` 失败时（VK_TIMEOUT / OOM / DEVICE_LOST），`imageAvailable` semaphore 的状态未定义（可能 signaled 也可能未 signaled）。后续 `commit_frame` 若等待它，可能死锁（等待永不 signal）或 UB（等待已 signal 但本帧未 acquire）。

**修复**：所有 acquire 失败路径都标记 `imageAvailableConsumed = true`，防止 `commit_frame` 等待不一致的 semaphore。下次成功 acquire 时 `swapchain_acquire_color` 会重置为 false。

## 八、验证标准

- [ ] iPhone SE 3 能正常进入游戏主界面
- [ ] 长时间运行（>30分钟）显存占用稳定不增长
- [ ] 无 VK_ERROR_OUT_OF_DEVICE_MEMORY
- [ ] 无 VK_ERROR_DEVICE_LOST
- [ ] 无红屏/黑屏（有声音无画面）
- [ ] 日志无循环刷屏（每帧日志数 < 10 条）
- [ ] allocation 数量 < maxMemoryAllocationCount 的 80%
- [ ] deviceLost 恢复后着色器能正常编译（无 "invalid type 'main0_in'"）
- [ ] 游戏不因渲染失败 exit(0)