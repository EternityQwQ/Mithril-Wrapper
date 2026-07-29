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

## 五、剩余待修复项

### 5.1 长期改进：引入 VMA（Vulkan Memory Allocator）— 低优先级

- 替代独立 `vkAllocateMemory`，减少 allocation 数量
- 子分配（sub-allocation）将一个 `VkDeviceMemory` 切分给多个 buffer/image
- 需要完整重构 Resources.cpp 的分配路径
- 当前钳制到 4096 + staging buffer 延迟释放已足够稳定

## 六、验证标准

- [ ] iPhone SE 3 能正常进入游戏主界面
- [ ] 长时间运行（>30分钟）显存占用稳定不增长
- [ ] 无 VK_ERROR_OUT_OF_DEVICE_MEMORY
- [ ] 无 VK_ERROR_DEVICE_LOST
- [ ] 无红屏/黑屏（有声音无画面）
- [ ] 日志无循环刷屏（每帧日志数 < 10 条）
- [ ] allocation 数量 < maxMemoryAllocationCount 的 80%
- [ ] deviceLost 恢复后着色器能正常编译（无 "invalid type 'main0_in'"）
- [ ] 游戏不因渲染失败 exit(0)