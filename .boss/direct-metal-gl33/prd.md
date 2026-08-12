# 产品需求文档（PRD）

## 摘要

> 下游 Agent 请优先阅读本节，需要细节时再查阅完整文档。

- **核心目标**：从零重建 Mithril-Wrapper 的运行时架构，对宿主保持 OpenGL 3.3 Core Profile 与 EGL 兼容入口，内部直接使用 Metal 2+，完全移除 Vulkan/MoltenVK，使 Minecraft Java Edition 1.17.1+ 在定义的平台与版本验收矩阵中稳定进入并运行游戏。
- **目标用户**：在 iPhone、iPad、Mac 上通过 Amethyst-iOS 或兼容 LWJGL 3 宿主运行 Minecraft Java Edition 的玩家，以及维护渲染兼容层的开发者。
- **关键功能**：GL 3.3 Core 状态与对象模型、GLSL 到 Metal 着色器链路、Direct Metal 资源/管线/命令后端、EGL/CAMetalLayer 生命周期、Minecraft 自动化与真机回归矩阵。
- **技术约束**：C++20 + Objective-C++；Apple Metal 2+；运行时、构建系统、状态层与公开/内部接口均不得依赖 Vulkan 或 MoltenVK；GL/EGL 错误语义、对象生命周期与线程上下文必须可测试；默认宿主为 Amethyst-iOS/LWJGL 3。
- **优先级**：P0 硬基线为 Minecraft Java 1.21.1 原版在 iPhone X（A11 / Metal 2、iOS 16.7.15）上完整进入世界并稳定渲染；其余 iPhone/iPad/Mac 与 1.17.1+ 版本作为扩展矩阵。P1 补齐声明支持的 GL 3.3 Core 一致性与性能；不实现 Compatibility Profile 固定管线。
- **“100%”口径**：仅表示发布时本文定义的必测组合、必测场景和自动化门禁 **100% 通过**。在缺少启动器版本锁定、游戏资源、签名/JIT 条件及真机设备的当前环境下，不得宣称“所有设备、所有模组、所有未来 Minecraft 版本均保证成功”。

---

## 文档信息

- **功能名称**：Mithril Direct Metal GL 3.3 全量重构
- **版本**：1.0
- **创建日期**：2026-08-12
- **作者**：PM Agent
- **状态**：评审中
- **代码基线**：`refactor/gl33-core-metal2`，基于 `e53d7a6`
- **产品形态**：无独立界面的动态渲染兼容库（`libmithril.dylib`）

---

## 1. 概述

### 1.1 背景与现状证据

现有 Mithril-Wrapper 对外导出 OpenGL 3.3 Core/EGL 入口，内部路径为 `OpenGL → Vulkan 1.2 → MoltenVK → Metal`。本次工作不是在现有 Vulkan 后端上继续打补丁，而是建立不含 Vulkan/MoltenVK 的 Direct Metal 产品实现。

仓库调查得到以下事实，构成本 PRD 的范围依据：

1. `CMakeLists.txt` 当前查找 MoltenVK、编译 `MG_Backend/DirectVulkan/*`，并链接 glslang 与 SPIRV-Cross；CI 也验证 Vulkan 后端及 MoltenVK 链接痕迹。
2. `MG_Backend/Backend.h` 的抽象契约暴露 `VkImageView`、`VkPipeline`、`VkBuffer`、`VkSampler`、`VkFormat`、`VkImageLayout` 等 Vulkan 类型，无法作为 Direct Metal 的干净边界继续沿用。
3. `MG_State/State.h` 的 GL 状态对象保存 `VkImageView`、`VkImage`、`VkFormat`，说明前端状态模型与具体后端耦合，必须重建而非只替换实现目录。
4. `egl/egl.cpp` 已覆盖宿主需要的 EGL 入口，但 Sync/Image 为影子实现；`eglSwapBuffers`、默认帧缓冲与交换链均按 Vulkan 语义组织。
5. `MG_Impl/Stubs.cpp` 中大量固定管线入口为空实现。固定管线属于 Compatibility Profile，不是 GL 3.3 Core 的交付目标；但 query、sync、sampler、transform feedback 等 Core 能力必须按 Minecraft 实际调用与 GL 3.3 声明重新审计，不能以“有符号”代替“已实现”。
6. 当前 GitHub Actions 仅构建 iOS arm64 dylib并检查导出符号，没有 macOS 双架构构建、GL 行为测试、着色器回归、渲染 golden、启动器集成或真机进入游戏测试。
7. README 声称可运行 Minecraft，但仓库内没有可复现的版本/设备/场景证据。重构后的兼容结论必须由测试产物建立，不能继承文档宣称。

### 1.2 真实目标

用户要求“100% 重构”的真实目标有三层：

- **用户结果**：选择 Mithril 渲染器后，Minecraft 1.17.1+ 不因 GL 能力探测、着色器编译、资源上传、FBO、首帧呈现或上下文切换而退出，能够进入世界并持续正确渲染。
- **技术结果**：运行时从 OpenGL 3.3 Core 语义直接落到 Metal 2+，彻底移除 Vulkan/MoltenVK 中间层及其泄漏到状态、资源和 EGL 的类型。
- **工程结果**：形成清晰分层、强类型资源句柄、明确所有权、可重复测试与可观测诊断的代码库，不把旧实现中的全局耦合、影子对象和空桩原样搬迁。

### 1.3 产品目标

- 以 Minecraft Java 1.21.1 原版、iPhone X（A11 / Metal 2）、iOS 16.7.15 作为不可降低的 P0 最低功能/性能基线。
- 在定义的 Minecraft 版本、宿主和真机矩阵中，所有 P0 组合均成功完成“进入游戏”流程。
- 对外稳定提供宿主所需的 GL 3.3 Core 与 EGL 动态符号和查询结果。
- 对内完全使用 Metal 设备、资源、命令缓冲、渲染/计算编码器、同步和呈现能力。
- 在错误语义、对象生命周期、线程隔离、资源回收和设备/Surface 变化方面具备自动化验证。
- 建立从 GL 调用测试、shader 测试、离屏图像对比到真机 Minecraft 回归的分层质量体系。

### 1.4 非目标

- 不实现 OpenGL Compatibility Profile、立即模式、矩阵栈、display list、固定管线光照/雾化等遗留 API。
- 不承诺 OptiFine、Iris、Sodium、第三方 shader pack 或任意模组兼容；它们需建立独立矩阵后才能纳入。
- 不将 Metal“转换成 OpenGL”；方向固定为 **对外接收 OpenGL 3.3 Core 调用，内部翻译为 Metal**。
- 不提供 Windows、Linux、Android 或非 Apple GPU 后端。
- 不提供启动器 UI、账号登录、Java 运行时、JIT/签名和游戏资源下载能力。

### 1.5 成功指标

- [ ] 发布门禁中的自动化 P0 用例通过率为 100%，无跳过、无预期失败。
- [ ] 定义的 P0 真机/版本组合进入游戏通过率为 100%，每个组合留存日志、设备信息、截图或录像与结果记录。
- [ ] iPhone X（A11 / Metal 2、iOS 16.7.15）运行 Minecraft Java 1.21.1 原版时，依次通过 dylib 加载、EGL/GL context、Mojang/加载界面、可交互主菜单、创建或加载世界、进入世界与持续稳定渲染。
- [ ] 产物及源码扫描中不存在 Vulkan/MoltenVK 头文件、链接依赖、符号、类型或运行时加载路径。
- [ ] `glGetString`、`glGetIntegerv`、扩展枚举与真实实现一致，不通过虚报能力绕过 LWJGL/Minecraft 检查。
- [ ] 30 分钟基础生存世界稳定性测试中无崩溃、Metal validation error、持续性资源增长或不可恢复黑屏。
- [ ] iOS/iPadOS 与 macOS Release 构建均可复现；GL/EGL 导出 ABI 检查全部通过。

---

## 2. 需求穿透分析

### 2.1 用户原始需求

> “帮我新开个分支，进行高端重构一下，metal转opengl3.3 core profile【拒绝难修的屎山代码，拥抱高端代码】，目标：兼容mc java版1.17+，iPhone、ipad、mac（metal2及以上），100%能成功进入游戏”

> 补充确认：“100%重构”

### 2.2 显性需求

| 需求 | 解读 |
|------|------|
| 100% 重构 | 重建前端状态、后端契约、Metal 后端、EGL 生命周期、shader 链路和测试体系；不是保留 Vulkan 数据模型后改名 |
| OpenGL 3.3 Core → Metal 2+ | 对宿主保持 GL 3.3 Core 行为，内部 Direct Metal；完全移除 Vulkan/MoltenVK |
| Minecraft 1.17+ | 以 1.17.1 为最低游戏版本，覆盖代表性跨代版本和最新稳定版；实际支持结论由矩阵测试产生 |
| iPhone/iPad/Mac | 同一核心实现覆盖 iOS、iPadOS、macOS，并处理不同架构、Surface、尺寸、前后台与 GPU 能力 |
| 100% 进入游戏 | 所有声明支持的 P0 验收组合必须完整通过启动、主菜单、加载世界、首帧交互，而非只完成编译或显示主菜单 |
| 高端代码 | 强调模块边界、所有权、错误模型、测试、诊断、性能与长期可维护性 |

### 2.3 隐性需求

| 需求 | 推断依据 | 为什么重要 |
|------|----------|------------|
| 宿主 ABI 不破坏 | Amethyst/LWJGL 依赖 `dlopen`/`dlsym` | 内部可全量重构，但宿主解析失败会在 GL 初始化前退出 |
| 能力报告必须诚实 | Minecraft/LWJGL 会依据版本和扩展选择代码路径 | 虚报扩展会把游戏引向未实现路径，产生随机崩溃 |
| Shader 兼容是首要链路 | 1.17+ 使用现代 GLSL、UBO、sampler 和多套 program | 只实现 draw API 无法通过资源加载阶段 |
| 资源与上下文可恢复 | Apple 应用存在前后台、旋转、resize、drawable 暂不可用 | 移动端正常生命周期不能被当成异常退出 |
| 诊断必须面向问题定位 | 跨启动器、JVM、JNI、GL 与 GPU，失败链路长 | 必须能分辨 GL error、shader error、OOM、drawable 缺失和设备错误 |
| 冷启动与卡顿受控 | Minecraft 首次加载会创建大量 shader/pipeline/texture | 无缓存和无界同步会造成超时、卡死或系统杀进程 |

### 2.4 潜在与惊喜需求

| 需求 | 类型 | 预期价值 |
|------|------|----------|
| 可序列化的 GL 调用轨迹与离线重放 | 潜在 | 无需每次启动完整游戏即可复现首帧、特定 shader/FBO 或资源生命周期问题 |
| Shader/pipeline 持久缓存 | 潜在 | 二次启动减少卡顿，同时以版本化 key 防止旧缓存污染 |
| 一键导出兼容诊断包 | 惊喜 | 输出设备、系统、GPU family、游戏版本、能力表、shader 日志和最近错误，降低远程排障成本 |
| Capability Manifest | 惊喜 | 由测试生成对外能力清单，确保实现、`glGet*` 报告、文档和测试矩阵一致 |

### 2.5 优先级矩阵

| 优先级 | 需求 | 价值 | 成本 | 决策 |
|--------|------|------|------|------|
| P0 | 干净的 GL Core 前端、对象/状态/错误模型 | 高 | 高 | 必须做 |
| P0 | Direct Metal 设备、资源、管线、命令、呈现 | 高 | 高 | 必须做 |
| P0 | Minecraft 必需 shader、texture、FBO、draw、sync/query 子集 | 高 | 高 | 必须做 |
| P0 | EGL/CAMetalLayer、线程上下文与移动端生命周期 | 高 | 高 | 必须做 |
| P0 | iOS/iPadOS/macOS 构建、ABI 与分层测试门禁 | 高 | 中 | 必须做 |
| P0 | Amethyst-iOS/LWJGL 3 + Vanilla Minecraft 验收矩阵 | 高 | 高 | 必须做 |
| P1 | GL 3.3 Core 全量一致性补齐 | 高 | 高 | 进入游戏后继续完成，声明 3.3 前必须通过 |
| P1 | 持久 shader/pipeline 缓存与轨迹重放 | 高 | 中 | 优先做 |
| P2 | 诊断包与性能仪表 | 中 | 中 | 可以做 |
| P3 | 模组、shader pack、Compatibility Profile | 低/不确定 | 极高 | 本期不做 |

---

## 3. 目标用户与关键旅程

### 3.1 用户画像：移动端 Minecraft 玩家

| 属性 | 描述 |
|------|------|
| 角色 | 使用 Amethyst-iOS 类启动器运行 Java 版 Minecraft 的 iPhone/iPad 用户 |
| 特征 | 关注能否启动、画面正确、卡顿、发热和崩溃；不了解图形后端内部结构 |
| 核心需求 | 选择 Mithril 后无需额外配置即可进入世界 |
| 痛点 | 黑屏、shader 编译失败、加载世界崩溃、切后台后无法恢复、设备差异导致结果不可预测 |
| 期望 | 明确支持范围、失败时有可导出的诊断、更新后不回归 |

### 3.2 用户画像：Mac 玩家与兼容层维护者

| 属性 | 描述 |
|------|------|
| 角色 | 在 Intel/Apple Silicon Mac 验证渲染器的玩家和项目维护者 |
| 特征 | 需要可复现构建、日志、GPU validation、自动化测试和性能基线 |
| 核心需求 | 同一 GL 语义在 macOS 与移动端保持一致，Mac 可作为高效率调试平台 |
| 痛点 | 平台专用分支漂移；只在某台设备上复现；符号存在但行为为空 |
| 期望 | 小而清晰的模块、确定性测试、平台差异被封装、问题可定位到具体层 |

### 3.3 核心用户旅程

1. 用户在启动器中选择 Mithril 渲染器。
2. 宿主成功加载 `libmithril.dylib`，解析 GL/EGL 入口并创建上下文与窗口 Surface。
3. Minecraft/LWJGL 获取真实 GL 版本、限制和扩展，创建资源并编译/链接 shader。
4. 游戏显示主菜单，用户创建或载入 Vanilla 世界。
5. 世界首帧正确显示，用户可移动、打开菜单、调整窗口/方向。
6. 用户切到后台再返回或改变窗口尺寸后，游戏恢复渲染。
7. 若失败，诊断输出指出失败层级和原因，不以无信息黑屏结束。

---

## 4. 功能需求

### FR-001：稳定的 OpenGL 3.3 Core / EGL 宿主 ABI

- **描述**：当 Amethyst-iOS/LWJGL 3 宿主加载动态库时，系统必须导出并解析其所需的 GL/EGL 入口，返回与实现相符的版本、限制与扩展，并维持既有调用约定。
- **用户价值**：无需改造 Minecraft 即可选择渲染器启动。
- **优先级**：P0
- **依赖**：宿主符号清单与调用轨迹。
- **验收标准**：
  - [ ] AC-1：CI 对 iOS arm64、macOS arm64、macOS x86_64 产物执行导出符号清单检查，必需符号 100% 存在。
  - [ ] AC-2：`eglGetProcAddress`/兼容 lookup 对支持入口返回稳定地址，对不支持入口返回空或规范要求的结果，不返回空桩伪实现。
  - [ ] AC-3：GL 版本报告为 3.3 Core 的前提是声明范围内的 Core 行为测试通过；扩展字符串仅包含已实现且已测试扩展。
  - [ ] AC-4：无当前上下文、非法 enum/value/operation、对象删除后使用等路径产生规范要求的 GL/EGL 错误。
- **边界情况**：重复初始化/终止、多次查询、缺失 Metal 设备、错误 native window、无 current context。

### FR-002：后端无关的 GL Core 状态与对象模型

- **描述**：系统必须以 GL 语义建模 buffer、texture、sampler、VAO、shader、program、uniform/UBO、FBO/renderbuffer、query、sync 与 context；模型不得包含 Metal/Vulkan 原生类型。
- **用户价值**：获得一致、可预测的 GL 行为，减少跨设备随机错误。
- **优先级**：P0
- **验收标准**：
  - [ ] AC-1：前端/状态层公开头文件不包含 `<vulkan/*>` 或 `<Metal/*>`，对象通过类型安全、不透明的后端句柄关联。
  - [ ] AC-2：对象生成、绑定、孤儿化、删除延迟、引用解除、context/share group 生命周期均有单元测试。
  - [ ] AC-3：每线程 current context 隔离，切换 context 后状态不泄漏；共享对象与非共享对象边界有测试。
  - [ ] AC-4：状态变更采用可审计 dirty tracking；同值重复设置不会错误改变资源或 draw 结果。
- **边界情况**：删除仍被绑定的对象、零尺寸资源、buffer 重分配、FBO 附件被删除、跨线程 context 迁移。

### FR-003：Direct Metal 2+ 设备与能力管理

- **描述**：系统必须直接创建和管理 `MTLDevice`、command queue、command buffer、encoder、heap/resource 与同步对象，并基于真实 GPU family/feature 能力选择实现路径。
- **用户价值**：去除额外翻译层，减少不确定性并适配 Apple 平台。
- **优先级**：P0
- **验收标准**：
  - [ ] AC-1：支持设备初始化成功并产生可用 Metal command queue；不支持设备返回明确错误而非崩溃。
  - [ ] AC-2：源码、构建配置、动态依赖、符号表和产物字符串扫描均无 Vulkan/MoltenVK 依赖。
  - [ ] AC-3：能力探测结果驱动 pixel format、depth/stencil、argument binding、同步与资源选项选择，不依赖机型硬编码作为唯一判据。
  - [ ] AC-4：启用 Metal API Validation 的测试场景无 validation error。
- **边界情况**：drawable 暂不可用、command buffer error、内存压力、低功耗/独显选择、Intel 与 Apple GPU 差异。

### FR-004：GLSL 3.30 到 Metal shader 的正确编译与链接

- **描述**：系统必须接受 Minecraft 1.17.1+ 所用 GLSL，完成预处理、编译、链接、反射、属性/fragment output 位置、uniform/UBO/sampler 映射和 MSL/Metal library 生成，并保留 GL 可读的错误日志。
- **用户价值**：资源加载阶段不因 shader 不兼容而退出或黑屏。
- **优先级**：P0
- **验收标准**：
  - [ ] AC-1：建立来自各验收 Minecraft 版本的去版权化/最小化 shader corpus，编译与链接通过率 100%。
  - [ ] AC-2：合法 shader 的顶点输入、varying、fragment output、矩阵布局、整数/浮点类型、数组、struct、UBO 与 sampler 反射结果符合预期。
  - [ ] AC-3：非法 shader 返回 `GL_COMPILE_STATUS/GL_LINK_STATUS = false` 与可定位日志，不崩溃、不产生可使用 program。
  - [ ] AC-4：GL 与 Metal 的坐标系、深度范围、屏幕/FBO Y 方向和 front-face 差异由一致策略修正，并通过图像 golden 测试。
  - [ ] AC-5：shader/pipeline cache key 包含源码、编译选项、资源布局、渲染目标格式、GPU/系统与缓存格式版本；损坏或过期缓存可安全回退。
- **边界情况**：显式/隐式 location、未使用 uniform、sampler array、宏/`#line`、编译并发、长日志、缓存失效。

### FR-005：Metal 资源映射与数据传输

- **描述**：系统必须正确映射 GL buffer、texture、renderbuffer 与 sampler 的创建、更新、copy、map、mipmap、pixel store、format/type 和生命周期。
- **用户价值**：方块、实体、字体、纹理和 UI 数据正确显示且内存稳定。
- **优先级**：P0
- **验收标准**：
  - [ ] AC-1：Minecraft 路径涉及的 buffer target/usage、subdata、map/unmap、copy 与 index 类型有单元/集成测试。
  - [ ] AC-2：颜色、sRGB、深度、模板、整数、压缩格式的支持表显式定义；支持项 round-trip/copy/readback 结果正确，不支持项返回规范错误。
  - [ ] AC-3：`GL_UNPACK_*`/`GL_PACK_*` 对齐、row length 和 skip 语义通过非紧密布局测试。
  - [ ] AC-4：CPU/GPU 并发访问由 staging/ring buffer/fence 保护，无 use-after-free 或覆盖仍在飞行资源。
  - [ ] AC-5：延迟销毁以 GPU 完成状态为准，30 分钟场景不存在无界资源积累。
- **边界情况**：NPOT、1×1、零尺寸、mip 不完整、纹理重定义、读取默认 FBO、内存分配失败。

### FR-006：渲染管线、FBO 与绘制语义

- **描述**：系统必须把 GL 3.3 Core 的 VAO/attribute、primitive、indexed/instanced draw、viewport/scissor、blend、depth、stencil、cull、color mask、clear、FBO 与 blit 映射到正确 Metal render pipeline/encoder。
- **用户价值**：世界、透明材质、天空、实体和界面正确渲染。
- **优先级**：P0
- **验收标准**：
  - [ ] AC-1：管线 key 覆盖所有会改变 `MTLRenderPipelineState`/depth-stencil state/vertex descriptor 的 GL 状态，且相同 key 可安全复用。
  - [ ] AC-2：三角形、strip、line、indexed、base vertex、instanced 等 Minecraft 使用路径与参考图像一致。
  - [ ] AC-3：默认 FBO 与离屏多附件 FBO 的 completeness、load/store、clear、resolve、blit 与 invalidate 行为通过测试。
  - [ ] AC-4：透明混合、深度遮挡、模板、裁剪、面剔除和颜色写掩码均有独立 golden。
  - [ ] AC-5：不支持的 primitive/format/state 组合返回可诊断错误，不静默绘制错误画面。
- **边界情况**：空 draw、负 base vertex、附件格式变化、反馈回路、resize 过程中 FBO 0 变化、primitive restart。

### FR-007：EGL、CAMetalLayer 与平台生命周期

- **描述**：系统必须提供宿主所需的 EGL display/config/context/surface/current/swap/sync/image 能力，以 `CAMetalLayer` 和 Metal drawable 完成 iPhone、iPad、Mac 呈现。
- **用户价值**：启动、旋转/缩放、切后台和恢复期间不黑屏、不崩溃。
- **优先级**：P0
- **验收标准**：
  - [ ] AC-1：创建 display/config/context/window surface、make current、swap、unbind、destroy、terminate 的完整生命周期测试通过。
  - [ ] AC-2：EGL Sync/Image 若对外声明支持，必须由真实 Metal 资源/同步语义支撑；否则不暴露对应版本/扩展，禁止沿用始终 signaled 的影子实现。
  - [ ] AC-3：drawable size、scale、方向和 macOS 窗口 resize 变化在下一安全帧生效，无拉伸、越界或永久黑屏。
  - [ ] AC-4：iOS/iPadOS 前后台往返 10 次，恢复后均可继续渲染和交互。
  - [ ] AC-5：`nextDrawable` 暂时返回空时跳过/重试策略有界，不提交无效 present，不把暂态当成设备永久失败。
- **边界情况**：零尺寸 layer、surface 先于 context 销毁、多个 surface/context、swap interval、应用挂起时存在 in-flight frame。

### FR-008：Minecraft 1.17.1+ 兼容路径

- **描述**：系统必须以 Vanilla Minecraft + LWJGL 3 的真实调用行为驱动 P0 能力实现和回归，完成启动至世界可交互的全过程。
- **用户价值**：用户真正进入游戏，而非仅通过合成 demo。
- **优先级**：P0
- **验收标准**：
  - [ ] AC-0：硬基线组合为 Minecraft Java 1.21.1 原版 + iPhone X（A11 / Metal 2）+ iOS 16.7.15；默认使用 Amethyst-iOS/LWJGL 3（准确启动器版本仍为开放项）。该组合必须成功加载 dylib、创建 EGL/GL context、显示 Mojang/加载界面、进入可交互主菜单、创建或加载世界并进入、持续稳定渲染。
  - [ ] AC-1：每个 P0 版本均通过 GL 能力检测、资源重载、主菜单渲染、创建/载入世界、世界首帧与 10 分钟交互。
  - [ ] AC-2：测试期间无 native/JVM crash、未捕获 Metal command error、持续黑屏、致命 shader/link error 或资源加载失败。
  - [ ] AC-3：F3/日志报告的 renderer/version/vendor 与实际 Direct Metal 实现一致，不冒充 Apple OpenGL/Vulkan。
  - [ ] AC-4：调整图形选项、切换全屏/窗口（Mac）或方向/尺寸（移动端）、资源重载后继续渲染。
- **边界情况**：首次无缓存启动、缓存热启动、新建世界、已有世界、语言/资源重载、不同 UI scale。

### FR-009：Query、Sync 与读回能力

- **描述**：对 Minecraft 实际调用的 occlusion/timer query、fence sync、client wait、read pixels 等能力，系统必须提供真实语义或诚实降级，并确保调用者不会进入伪实现路径。
- **用户价值**：避免“能启动但随机等待、错误判断或截图失败”。
- **优先级**：P0（实际调用子集），P1（其余 GL 3.3 Core）
- **验收标准**：
  - [ ] AC-1：调用轨迹中出现的 query/sync API 均有行为测试，不允许仅维护名称和固定返回值。
  - [ ] AC-2：等待操作有明确超时和线程语义，不进行无界 CPU busy-wait。
  - [ ] AC-3：GPU 结果在规范允许的时间点可见，删除 in-flight 对象安全。

### FR-010：可观测性与可复现诊断

- **描述**：系统必须提供分级、可关闭、低开销的诊断，关联 context、thread、frame、program/pipeline 与 Metal command error。
- **用户价值**：失败时知道原因，维护者能复现和修复。
- **优先级**：P1
- **验收标准**：
  - [ ] AC-1：Release 默认不输出高频逐调用日志；错误包含稳定错误码、层级和必要对象标识。
  - [ ] AC-2：诊断模式可输出能力清单、shader 编译日志、pipeline key、最近 GL error 和 Metal command buffer 状态。
  - [ ] AC-3：日志不包含账号 token、用户路径中的敏感信息或完整受版权保护游戏资源。

---

## 5. 非功能需求

### NFR-001：架构与可维护性

- 前端 API、GL 语义/状态、命令归一化、shader 工具链、Metal 后端、EGL/平台适配和诊断必须有单向依赖边界。
- 所有 GPU 资源使用 RAII 或等价的明确所有权；禁止裸全局设备资源、重复销毁和隐式跨 context 共享。
- 后端接口使用领域类型或不透明句柄，不暴露 `id<MTL*>` 到 GL 语义层，也不暴露 GL 可变状态到 Metal 底层。
- 禁止在声明支持的 Core API 中保留无日志空桩；未支持入口必须不声明或返回规范错误。
- 关键模块须有设计注释解释不变量和 Apple/GL 语义差异，避免逐行复述代码。
- 新增功能必须带测试；静态分析、格式检查、警告作为错误和 sanitizers（可用平台）纳入 CI。

### NFR-002：性能与流畅性

- 每帧不得调用 `waitUntilCompleted` 等全队列硬同步，除非 GL 语义明确要求读回/等待。
- 正常渲染采用有界 in-flight frame（建议 2–3 帧）和可复用上传分配器，避免每 draw 分配 Metal 对象。
- pipeline/shader 不得在相同 key 下重复编译；热缓存命中可测。
- 在指定基准场景与同一设备上，相对首次可运行基线，P95 frame time 不得回退超过 10%；若尚无基线，首个通过版本固化为基线而非虚构绝对 FPS。
- 游戏世界稳定运行 30 分钟后，wrapper 可归因内存不得持续单调增长；稳态 10 分钟区间增长不超过 5% 或 64 MiB（取更严格者，游戏主动资源重载窗口除外）。

### NFR-003：可靠性

- 所有外部输入（GL enum、尺寸、offset、长度、shader 源、native handle）在进入后端前验证，整数运算检查溢出。
- OOM、shader 失败、drawable 缺失、Metal command error 必须受控失败并记录；不得 use-after-free 或 silent corruption。
- 前后台/resize 不丢失可恢复的 context 状态；无法恢复时必须返回明确 EGL 错误。
- Debug 配置在 Metal API Validation 下无错误；适用测试在 ASan/UBSan/TSan 下无缺陷。

### NFR-004：兼容性

- **移动端候选范围**：iOS/iPadOS 15.0+、arm64、系统报告支持 Metal 2 所需能力的 iPhone/iPad。具体最低 GPU family 在设备能力验证后由架构与测试结果固化，不沿用 README 中未经本项目验证的 A11 结论。
- **Mac 候选范围**：macOS 12.0+；Apple Silicon arm64，以及配备 Metal 2+ GPU 的 Intel x86_64 Mac。
- 不以设备型号字符串替代能力探测；可以使用经过记录的设备规避表处理已知驱动缺陷。
- 最新稳定 Minecraft 版本是滚动目标：每次发布冻结准确版本号与 Java/LWJGL 依赖，未来版本默认“待验证”。

### NFR-005：构建与交付

- CI 至少生成 iOS device arm64、macOS arm64、macOS x86_64（可合并 universal）产物。
- 产物只链接必要 Apple frameworks 和明确列出的第三方 shader 编译依赖，不加载 Vulkan ICD 或 MoltenVK dylib。
- 依赖版本锁定，保留许可证与来源；构建过程可从干净 checkout 重现。
- Release 产物完成符号、架构、deployment target、install name/rpath 与签名兼容检查。

### NFR-006：安全与隐私

- shader 缓存和诊断文件使用应用授权目录，文件名与长度经过验证，不执行缓存内容。
- 不上传日志或游戏数据；任何远程上传能力不在本期范围。
- 对不可信 shader 和尺寸输入设置资源/编译边界，避免崩溃、路径注入和无界内存消耗。

---

## 6. 用户故事

### US-001：首次进入 Vanilla 世界

- **作为** iPhone/iPad Minecraft 玩家
- **我想要** 选择 Mithril 后启动 1.17.1+ 并进入世界
- **以便** 在没有桌面 OpenGL 的设备上玩 Java 版 Minecraft
- **验收标准**：宿主加载、资源初始化、主菜单、世界加载、首帧和 10 分钟交互全部成功，无黑屏或崩溃。
- **优先级**：P0

### US-002：切后台后恢复

- **作为** 移动端玩家
- **我想要** 临时切换应用后返回游戏
- **以便** 正常处理消息或系统中断而不必重启世界
- **验收标准**：往返后台 10 次后画面和输入均恢复，无不可恢复 context/surface 错误。
- **优先级**：P0

### US-003：Mac 窗口变化

- **作为** Mac 玩家
- **我想要** 调整窗口和全屏状态
- **以便** 使用合适分辨率游玩
- **验收标准**：连续 resize、最小化/恢复、全屏切换后默认 FBO 尺寸正确并持续呈现。
- **优先级**：P0

### US-004：定位兼容失败

- **作为** 维护者
- **我想要** 获得可关联到 shader、资源、draw 或 present 阶段的诊断
- **以便** 不依赖猜测修复某个设备/版本问题
- **验收标准**：一次故障可导出脱敏诊断，包含重现所需版本/能力/错误上下文。
- **优先级**：P1

---

## 7. 验收与测试策略

### 7.1 “进入游戏”统一定义

一个组合只有同时满足以下条件才记为通过：

1. 启动器成功加载 wrapper，创建 EGL context/surface，LWJGL 能力初始化无致命缺失。
2. 使用全新缓存冷启动进入主菜单，画面非黑屏且 UI 可交互。
3. 创建或载入固定种子的 Vanilla 单人世界，完成世界首帧。
4. 连续 10 分钟执行移动、转向、打开背包/菜单、切换视距或图形选项、资源重载等脚本。
5. 日志无 native crash、Metal validation error、致命 shader/pipeline error、GPU command failure。
6. 留存版本、设备、系统、GPU、wrapper commit、Java/LWJGL、冷/热缓存、日志和画面证据。

P0 硬基线还要求 Minecraft Java 1.21.1 原版在 iPhone X（A11 / Metal 2、iOS 16.7.15）上逐项留证：启动器加载 dylib、EGL/GL context 成功、Mojang/加载界面可见、主菜单可交互、创建或加载世界并进入、持续稳定渲染。启动器未另行指定前默认 Amethyst-iOS/LWJGL 3，其准确 commit 仍需冻结。

### 7.2 Minecraft 版本矩阵

| 层级 | 游戏版本 | 目的 | 发布要求 |
|------|----------|------|----------|
| P0 硬基线 | **1.21.1 原版** | 用户明确指定的最低验收版本 | 必须在 iPhone X / iOS 16.7.15 通过完整进入游戏门禁 |
| P0 | 1.17.1 | 最低目标，Java 16 世代 | 必须通过 |
| P0 | 1.18.2 | 世界高度/渲染负载变化代表 | 必须通过 |
| P0 | 1.19.4 | 中期 Blaze3D/LWJGL 行为代表 | 必须通过 |
| P0 | 1.20.1 | 广泛使用的稳定版本代表 | 必须通过 |
| P0 | 1.20.6 | Java 21 与较新资源链路代表 | 必须通过 |
| P0 | 发布时最新稳定版（冻结准确 patch） | 当前版本兼容；若不是 1.21.1，则作为额外组合 | 必须通过 |
| P1 | 其余 1.17.1+ patch/minor | 扩展覆盖 | 有设备与资源后纳入；未测不得宣称 |

所有 P0 测试默认 **Vanilla、无模组、默认资源包**。可另加性能场景，但不得用模组结果替代 Vanilla 基线。

### 7.3 平台与设备矩阵

用户已明确 iPhone X 基线参数，但当前未确认设备是否已提供给自动化/人工测试。以下是发布前必须补齐的最小角色矩阵，不代表当前已验证：

| 平台角色 | 最低样本 | 重点风险 | P0 要求 |
|----------|----------|----------|---------|
| **iPhone 硬基线** | **iPhone X（A11 / Metal 2）、iOS 16.7.15** | 最低性能/功能、内存、旧 GPU | **Minecraft Java 1.21.1 原版完整门禁必须通过** |
| iPhone 扩展设备 | 至少一台更现代 iPhone | 主流性能、现代 GPU family | 1.17.1、1.20.1、最新稳定必须通过 |
| iPhone 现代设备 | A15 或更新一代样本 | 主流性能、现代 family | 1.17.1、1.20.1、最新稳定必须通过 |
| iPad 边界设备 | 最老支持 iPad 样本 | 大 drawable、内存差异、方向 | 1.17.1 与最新稳定必须通过 |
| iPad 现代设备 | Apple Silicon iPad 样本 | 高分辨率、统一内存 | 1.20.1 与最新稳定必须通过 |
| Mac Apple Silicon | 至少一台基础 M 系列 | macOS Surface、arm64 | 全 P0 版本必须通过 |
| Mac Intel + Metal 2 GPU | 至少一台 | x86_64、Intel/AMD 驱动差异 | 1.17.1、1.20.1、最新稳定必须通过 |

若 iPhone X 硬基线没有真机证据，V1.0 不得发布；若其余扩展角色缺少真机，该角色必须从“已支持”降级为“实验性/待验证”，不得以模拟器或 Mac 结果代替。

### 7.4 可自动验证项

- C++ 纯逻辑：GL 状态、错误、对象生命周期、format/capability map、pipeline key、EGL config。
- macOS Metal 离屏集成：资源上传、shader、draw、FBO、query/sync、readback 与 golden。
- Shader corpus：各版本代表 shader 的编译、链接、反射与失败日志。
- GL trace replay：Minecraft 启动/首帧轨迹的确定性重放和图像/错误比对。
- ABI/build：GL/EGL 符号、架构、deployment target、动态依赖、Vulkan/MoltenVK 零痕迹。
- 质量：clang warnings-as-errors、clang-tidy（规则冻结）、ASan/UBSan，适用并发测试使用 TSan。
- 宿主冒烟：在可自动化 macOS 宿主中完成 context 创建与最小 LWJGL 渲染。

### 7.5 必须真机验证项

- Amethyst-iOS 的真实 `dlopen`/`dlsym`、签名/JIT/Java/LWJGL 集成。
- iPhone/iPad 的 `CAMetalLayer`、方向、scale、前后台、内存警告、热状态和真实 drawable 节奏。
- 边界 Metal 2 GPU 上的 format、shader、同步与性能行为。
- Mac Intel/AMD 与 Apple Silicon 的真实驱动差异。
- Minecraft 完整登录/离线启动（由测试账号与宿主条件决定）、资源加载、进入世界、长稳和画面正确性。

### 7.6 发布质量门禁

- 自动化 P0：100% 通过，0 skipped，0 known failure。
- 真机 P0：本文冻结的所有必测组合 100% 通过；失败组合不得通过文档措辞隐藏。
- 崩溃：进入游戏与 30 分钟稳定性场景为 0。
- Metal validation：0 error。
- 严重渲染差异：0；golden 容差须按格式预先定义，不得为通过结果临时放宽。
- Vulkan/MoltenVK：源码（业务/构建）、产物和运行时依赖均为 0；迁移说明中的历史文字可保留但不参与构建。
- 未完成 P1 GL 3.3 Core 能力时，不得笼统声称“完整 OpenGL 3.3 Core”；只能声明经测试的 Minecraft 兼容子集。

---

## 8. 范围定义

### 8.1 本期范围（In Scope）

- 全新 GL 3.3 Core 前端语义层、状态与对象系统。
- 全新后端契约与 Direct Metal 2+ 实现。
- GLSL 3.30 兼容、编译、反射、MSL/Metal pipeline 生成与缓存。
- Minecraft 路径需要的 buffer、texture、sampler、VAO、FBO、renderbuffer、draw、query、sync、readback。
- EGL 宿主入口、context/share group、CAMetalLayer surface、swap 与真实同步/图像语义。
- iOS/iPadOS/macOS 构建和打包；iOS arm64、macOS arm64/x86_64。
- 自动化 conformance 子集、shader corpus、golden、trace replay、ABI 和依赖扫描。
- Amethyst-iOS/LWJGL 3 + Vanilla Minecraft 版本/真机验收。
- 将旧 Vulkan/MoltenVK 实现、构建依赖和 CI 验证完全移除。

### 8.2 范围外（Out of Scope）

- OpenGL Compatibility Profile 与 `glBegin/glEnd`、矩阵栈、display list、固定管线功能。
- OpenGL 4.x、OpenGL ES 对外 profile、Vulkan 对外 API。
- OptiFine/Iris/Sodium、Forge/Fabric/NeoForge、shader pack 与第三方模组保证。
- Minecraft 1.16.5 及以下。
- 启动器 UI、账号、Java/JIT、输入、声音、网络和游戏自身 bug。
- App Store 发布、证书申请、外部测试账号与设备采购。
- 对未纳入冻结矩阵的未来 Minecraft/OS 版本作永久保证。

### 8.3 “全量重构”的保留边界

允许保留的内容：经审计的公共 GL/EGL 头与 ABI 清单、纯测试向量、许可证、项目元数据和可证明正确的无后端纯算法。任何保留都必须通过新架构边界和测试，而不是默认复制。

必须替换/移除的内容：DirectVulkan 目录、Vulkan 化 Backend 契约、状态层 `Vk*` 字段、MoltenVK/CMake/CI 路径、Vulkan swapchain/descriptor/pipeline/command 模型、影子 Sync/Image 语义和声明支持路径中的空桩。

---

## 9. 风险与依赖

### 9.1 风险登记

| 风险 | 可能性 | 影响 | 缓解措施 |
|------|--------|------|----------|
| 用户要求的绝对“100%”超出可证明范围 | 高 | 高 | 用冻结矩阵、证据和发布门禁定义 100%；未测组合明确标记待验证 |
| 当前没有 iPhone/iPad/Mac 真机与启动器版本矩阵 | 高 | 高 | 先完成自动化；发布前必须提供/接入设备农场或人工真机，缺项则降级支持声明 |
| Minecraft/LWJGL 实际调用范围未知 | 高 | 高 | 在合法环境采集符号解析和 GL 调用轨迹，形成去数据化 capability manifest 与回放集 |
| GLSL→MSL 语义差异造成长尾画面错误 | 高 | 高 | shader corpus、反射测试、坐标/精度策略、离屏 golden 与真机对照 |
| GL 状态组合导致 pipeline key 爆炸或卡顿 | 中 | 高 | 状态归一化、分层缓存、异步预热、指标与上限监控 |
| Metal 2 边界设备能力/内存不足 | 高 | 高 | 能力驱动路径、资源预算、边界设备验证；不能达标时诚实收窄最低设备 |
| EGL 1.5 与宿主实际期望不一致 | 中 | 高 | 锁定 Amethyst 分支/commit，录制 dlsym 与生命周期契约测试 |
| “GL 3.3 Core”完整一致性工作量巨大 | 高 | 高 | Minecraft P0 子集优先；完整 3.3 声明必须由 P1 conformance 门禁解锁 |
| Intel Mac 缺少可用硬件 | 中 | 中 | 接入专用 runner/人工设备；未验证前列为实验性 |
| 第三方 shader 工具链体积、许可证或编译耗时 | 中 | 中 | 架构阶段比较路线，锁定版本/许可证，使用缓存与离线预编译能力 |
| 大规模重写长时间不可运行 | 中 | 高 | 以垂直切片推进：context→triangle→texture/FBO→Minecraft trace→真实游戏；每片有门禁 |

### 9.2 依赖项

| 依赖 | 类型 | 状态 | 负责人 |
|------|------|------|--------|
| Apple Xcode/Metal SDK 与 macOS CI runner | 外部技术 | 部分就绪（现 CI 仅 iOS） | DevOps |
| Amethyst-iOS 准确仓库、分支/commit 与集成契约 | 外部项目 | 待锁定；默认 Amethyst-iOS/LWJGL 3 | 产品/集成负责人 |
| 合法 Minecraft 1.17.1+ 安装、资源与测试账号/离线环境 | 外部业务 | 待提供 | 产品负责人 |
| iPhone X（A11 / Metal 2、iOS 16.7.15） | P0 硬件 | 参数已指定，测试接入状态待确认 | 产品负责人/QA |
| 其他 iPhone、iPad、Apple Silicon Mac、Intel Metal 2 Mac | 扩展硬件 | 未提供 | 产品负责人/QA |
| GLSL 前端与 MSL 生成/反射方案 | 第三方技术 | 待架构决策；可评估 glslang/SPIRV-Cross，但不得引入 Vulkan runtime | Architect |
| 图像 golden、GL trace 与 shader corpus | 测试资产 | 待建立 | Dev/QA |
| Apple 平台签名、JIT/Java 运行条件 | 外部平台 | 待确认 | Amethyst/DevOps |

---

## 10. 里程碑

| 里程碑 | 内容 | 完成定义 |
|--------|------|----------|
| M0：事实基线 | 宿主 ABI、Minecraft GL 轨迹、设备/版本矩阵冻结 | 能力清单和测试证据格式评审通过 |
| M1：Direct Metal 垂直切片 | EGL context + CAMetalLayer + shader + triangle + present | iOS/macOS 构建，真机显示 golden triangle，无 Vulkan/MoltenVK |
| M2：Core 资源与离屏渲染 | buffer/texture/VAO/FBO/state/draw/readback | 自动化 P0 GL/golden 100% 通过 |
| M3：Minecraft 首屏 | LWJGL 初始化、资源加载、主菜单 | 代表性 Mac 与一台 iOS 真机主菜单通过 |
| M4：进入世界 | 全 P0 游戏版本完成统一“进入游戏”流程 | 冻结矩阵 100% 通过 |
| M5：跨平台稳定性 | 前后台、resize、长稳、边界设备、性能 | 真机角色矩阵与 30 分钟门禁通过 |
| V1.0：可信发布 | 文档、产物、诊断、质量报告 | 第 7.6 节全部满足，无虚假兼容声明 |

里程碑不预设日期；需在设备、启动器、游戏资产和架构方案锁定后由 Scrum Master 估算。

---

## 11. 开放问题

- [ ] 锁定 Amethyst-iOS 的准确仓库、分支/commit、renderer 选择名和 GL/EGL `dlsym` 清单。
- [ ] 明确是否有合法的 Minecraft 测试账号/离线测试包，以及各版本 Java 运行时来源。
- [ ] 提供或接入哪些 iPhone、iPad、Apple Silicon Mac、Intel Metal 2 Mac 真机？
- [ ] 除已锁定的 iPhone X / iOS 16.7.15 硬基线外，iPadOS/macOS 的最低 OS 与 GPU family 经验证后分别是什么？
- [ ] “最新稳定 1.21.x”在发布冻结时的准确 patch 版本是什么？
- [ ] EGL 1.5 Sync/Image 是否是宿主硬依赖；若不是，是否可只声明实际需要且真实实现的 EGL 版本/扩展？
- [ ] P1 是否要求完整 GL 3.3 Core conformance，还是产品只对外声明 Minecraft 经验证子集？
- [ ] 是否将诊断轨迹和 shader corpus存入仓库；如何确保不包含 Mojang 版权资源或用户隐私？
- [ ] 是否要求 universal macOS dylib，还是分别交付 arm64/x86_64？

开放问题不阻塞架构和自动化垂直切片，但阻塞“iPhone/iPad/Mac + Minecraft 1.17.1+ 已 100% 验证”的最终发布结论。

---

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|----------|
| 1.0 | 2026-08-12 | PM Agent | 基于仓库事实建立 Direct Metal 全量重构范围、Minecraft 验收矩阵与可信发布门禁 |
