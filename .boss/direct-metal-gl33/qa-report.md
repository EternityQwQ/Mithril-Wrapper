# QA 测试报告

## 报告信息

- **功能名称**：Direct Metal OpenGL 3.3 Core 重构 B1-B4
- **版本**：工作区 `refactor/gl33-core-metal2`
- **测试日期**：2026-08-12
- **测试者**：QA Agent（C++ 图形库专项审计）
- **测试环境**：Windows 10/11 x64；Visual Studio 17 2022/MSVC；`C:\Program Files\LLVM\bin\clang++.exe` 21.1.4

## 摘要

- **总体结论**：⚠️ B1-B4 门禁通过，但整体不可发布。
- **修复后纯 C++ 自动化**：通过；Clang 独立测试退出码 0，MSVC Debug/Release 串行 CTest 各 2/2。
- **ABI 语义比较**：通过；原始与重新生成 manifest 均 381 个符号，语义差异 0。JSON 字节差异仅格式化，不作为缺陷。
- **P0/P1 Bug**：原审计的 1 个 P0、3 个 P1 候选均已修复并复测；仍有 parser 结构化解析技术债务。
- **Metal/真机/Minecraft E2E**：未测试，禁止伪报通过。
- **发布建议**：否。shader toolchain 配置失败本身阻塞 M1 Apple 路径。

## 1. 测试范围

| 范围 | 状态 | 说明 |
|------|------|------|
| B1 ABI manifest/parser/lookup | 🟢 部分通过 | 可解析、可重生成、语义一致；尚无真实宿主 `dlsym` 轨迹 |
| B2 glslang/SPIRV-Cross MSL 构建 | 🟢 | legacy OFF + shader ON 已配置并实际构建 `mithril_shader_toolchain`、glslang、SPIRV-Cross GLSL/MSL |
| B3 后端窄接口/纯 C++ 核心 | 🟢 部分通过 | core target 可构建；接口静态审查见缺陷与建议 |
| B4 测试骨架/质量扫描 | 🟢 | unit、boundary artifact 和 34 个中性核心文件扫描通过 |
| Apple Metal、iPhone X、Minecraft 1.21.1 | ⚪ 未测试 | 当前 Windows 环境无 Apple SDK、真机、Amethyst commit 和合法游戏资产 |

## 2. 实际执行命令与结果

### 2.1 clang++ 纯 C++ 编译与运行

```powershell
& 'C:\Program Files\LLVM\bin\clang++.exe' -std=c++20 -Wall -Wextra -Wpedantic -Werror `
  '-DMITHRIL_SOURCE_ROOT="D:/mcios/Mithril-Wrapper"' -I Mithril-Wrapper-cpp/src -I tests `
  tests/unit/core_tests.cpp `
  Mithril-Wrapper-cpp/src/abi/AbiManifest.cpp `
  Mithril-Wrapper-cpp/src/abi/ProcTable.cpp `
  Mithril-Wrapper-cpp/src/core/Error.cpp `
  Mithril-Wrapper-cpp/src/gl/Capability.cpp `
  Mithril-Wrapper-cpp/src/gl/Context.cpp `
  Mithril-Wrapper-cpp/src/ir/PipelineKey.cpp `
  Mithril-Wrapper-cpp/src/shader/GlslPreprocessor.cpp `
  Mithril-Wrapper-cpp/src/shader/ShaderTypes.cpp `
  -o build-core/clang_core_tests.exe
& .\build-core\clang_core_tests.exe
```

结果：编译成功，程序退出码 `0`，未报告断言失败。

### 2.2 CMake/MSVC legacy OFF、shader OFF

配置：`cmake -S . -B build-core/cmake-vs-qa -G "Visual Studio 17 2022" -A x64 -DMITHRIL_BUILD_CORE=ON -DMITHRIL_BUILD_TESTS=ON -DMITHRIL_BUILD_LEGACY=OFF -DMITHRIL_ENABLE_SHADER_TOOLCHAIN=OFF`。

结果：配置、Debug/Release 构建成功；Debug 与 Release 串行 CTest 均为 **2/2 通过，0 失败，0 跳过**。

### 2.3 修复后质量扫描

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/quality/scan_for_vulkan.ps1 `
  -RepositoryRoot (Get-Location).Path -BuildDirectory (Join-Path (Get-Location).Path 'build-core')
```

结果：`mithril_core boundary scan passed (34 source files)`，且 boundary artifact 检查通过。扫描器现按 `core/abi/gl/ir/backend/shader` 中性目录工作，并可对 target artifact 做额外检查。

### 2.4 ABI manifest 重生成与语义比较

`tools/abi/gen_manifest.ps1` 重新生成 381 条目；逐字段比较 `name/signature/api/since/status/error_behavior/evidence`：`old=381 new=381 changed=0`。字节 hash/缩进差异不计为语义变化。

### 2.5 修复后 shader ON 配置与构建

配置 `MITHRIL_BUILD_LEGACY=OFF; MITHRIL_ENABLE_SHADER_TOOLCHAIN=ON` 成功；`mithril_shader_toolchain` 连同 glslang、SPIRV-Cross GLSL/MSL target 实际构建成功。依赖定义已调整为 `MITHRIL_BUILD_LEGACY OR MITHRIL_ENABLE_SHADER_TOOLCHAIN` 条件，确认不需要打开旧 Vulkan target。

## 3. 发现的缺陷

### BUG-001：shader toolchain 在 legacy OFF 下不可配置

| 属性 | 值 |
|------|-----|
| 严重程度 | 🔴 P0 |
| 状态 | 已修复，复测通过 |
| 复现 | `cmake -DMITHRIL_BUILD_LEGACY=OFF -DMITHRIL_ENABLE_SHADER_TOOLCHAIN=ON` |
| 实际结果（修复前） | 找不到 `glslang::glslang`，无法生成新 MSL target |
| 影响 | Direct Metal 无法完成 GLSL→SPIR-V→MSL 构建，M1 Apple 路径阻塞 |
| 修复验证 | legacy OFF + shader ON configure/build 成功；新 target 可见 glslang 与 SPIRV-Cross GLSL/MSL |

### BUG-002：`depthWrite=false` 强制改变 depth compare

| 属性 | 值 |
|------|-----|
| 严重程度 | 🟠 P1 |
| 状态 | 已修复，复测通过 |
| 位置 | `Mithril-Wrapper-cpp/src/ir/PipelineKey.cpp` |
| 实际行为（修复前） | 关闭深度写入时无条件把 `depthCompare` 改为 `always` |
| 预期行为 | 深度写入与深度比较是独立 GL 状态；应保留原 compare，或由明确的 depth-test enable 状态决定 |
| 修复验证 | `normalized()` 保留 compare 状态；hash 仍基于归一化 key，Windows unit/CTest 通过 |

### BUG-003：Context 切换存在跨对象未加锁写入

| 属性 | 值 |
|------|-----|
| 严重程度 | 🟠 P1 |
| 状态 | 已修复，静态复核通过；TSAN/真机并发仍未执行 |
| 位置 | `Mithril-Wrapper-cpp/src/gl/Context.cpp` |
| 实际行为（修复前） | `makeCurrent()` 可能在未锁定对方 Context 时写 `current_->owner_` |
| 修复验证 | `owner_` 与 thread-local current 的读写统一由静态 `ownershipMutex_` 保护；仍需后续 TSAN/设备回归 |

### BUG-004：Vulkan 边界扫描不能证明最终 runtime 零依赖

| 属性 | 值 |
|------|-----|
| 严重程度 | 🟠 P1 |
| 状态 | 已修复，boundary artifact 复测通过 |
| 位置 | `tools/quality/scan_for_vulkan.ps1` |
| 实际行为（修复前） | 只扫描整个 `src`，无法区分中性核心与未来 Metal 目录，也未检查 target artifact |
| 影响 | 可能漏掉错误加入 target 的旧 Vulkan 文件，也可能误杀未来 Metal 代码；零依赖结论不可靠 |
| 修复验证 | 仅扫描中性目录，支持 `-RequireTargetArtifact` 和 boundary JSON；34 文件扫描及 artifact 检查通过。最终 Apple 产物依赖仍待 Apple runner 验证 |

## 4. ABI parser/manifest 审查

- manifest 重新生成语义完全一致，381 条目均可被 `AbiManifest::parse` 解析。
- parser 使用正则解析 JSON（`AbiManifest.cpp:24-68`），对转义引号、嵌套对象和字段顺序变化不健壮；当前 manifest 未触发该问题，列为 P1 测试债务，不宣称完整 JSON 合规。
- status 规则正确区分 `implemented/provisional/unsupported`；由于宿主 commit/trace 未锁定，当前大量条目保持 provisional，不能通过 release ABI 检查。

## 5. 未测试项

| 项目 | 状态 | 原因 |
|------|------|------|
| macOS Metal 编译/离屏 golden | ⚪ 未测试 | Windows 无 Apple SDK |
| iOS arm64 / iPhone X A11 iOS 16.7.15 | ⚪ 未测试 | 无设备接入 |
| `MTLSharedEvent`/counter/binary archive/argument buffer 动态探测 | ⚪ 未测试 | 无 Metal runtime |
| Amethyst/LWJGL `dlsym` 轨迹 | ⚪ 未测试 | 准确仓库/commit 未提供 |
| Minecraft 1.21.1 主菜单/进入世界/长稳 | ⚪ 未测试 | 无合法资产、启动器和真机 |
| 性能、内存、前后台、resize | ⚪ 未测试 | 同上 |

## 6. 修复后复测

| 复测项 | 结果 | 证据 |
|--------|------|------|
| MSVC legacy OFF/shader OFF configure | 🟢 | Visual Studio 17 2022 x64 配置成功 |
| MSVC Debug/Release build | 🟢 | 两配置构建成功 |
| CTest Debug/Release | 🟢 | 各 2/2 通过，0 失败，0 跳过 |
| Clang 21 `-Werror -pthread` core tests | 🟢 | 编译成功，运行退出码 0 |
| boundary artifact + 34 文件扫描 | 🟢 | 扫描通过，artifact 未含禁止依赖 |
| ABI manifest | 🟢 | 381 条目，semantic diff=0 |
| shader toolchain | 🟢 | legacy OFF + shader ON configure/build，glslang/SPIRV-Cross GLSL/MSL 实际构建 |
| BUG-001/002/003/004 | 🟢 | 修复代码静态复核与上述回归通过 |

## 7. 质量门禁结论

| 门禁 | 结果 | 说明 |
|------|------|------|
| Windows 纯 C++ 编译 | 🟢 | clang++ 21.1.4，warnings-as-errors，退出码 0 |
| CMake legacy OFF/shader OFF | 🟢 | 配置及 Debug/Release 构建成功 |
| CTest | 🟢 | Debug/Release 串行均 2/2 通过 |
| ABI 语义稳定性 | 🟢 | 381→381，semantic diff=0 |
| Shader MSL target | 🟢 | shader ON 配置及 target 构建通过 |
| Vulkan/MoltenVK 核心边界证明 | 🟢 | 中性核心 34 文件与 boundary artifact 通过；Apple 最终产物仍待验证 |
| Apple/真机/Minecraft E2E | ⚪ | 未测试，不能放行 |

**B1-B4 Gate：✅ 通过。** **整体发布 Gate：❌ 不通过。** BUG-001 至 BUG-004 已修复并通过当前 Windows 回归；Apple Metal、iPhone X、Minecraft E2E 仍未测试，不能进入发布或宣称“100%成功进入游戏”。

## 7. 建议

1. 在 macOS/iOS runner 复现 shader target，并对最终 dylib 执行 `otool -L`/`nm`/link map 零依赖门禁。
2. 为 Context ownership 增加 TSAN/并发回归；当前修复只完成锁协议静态验证。
3. 将 ABI parser 替换为结构化 JSON 解析器，或把当前 manifest 格式限制写入 schema 测试。
4. 取得 Amethyst/LWJGL commit、iPhone X 和合法 Minecraft 1.21.1 资产后执行真实 E2E。

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|----------|
| 1.0 | 2026-08-12 | QA Agent | B1-B4 独立审计与初次 Gate 判定 |
| 1.1 | 2026-08-12 | QA Agent | 修复后 Windows/CMake/shader/ABI 复测；B1-B4 通过，发布仍阻塞 |
