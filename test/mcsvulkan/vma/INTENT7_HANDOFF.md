# INTENT7_HANDOFF — 数据 + 操作 + 透明 emitter

> 2026-11-08 重构后的交接说明。
> 这份文档描述的是 **三轴模型** 的实现，而不是旧的“PassCompiler 把 Cmd union 翻成 vkCmd*”。

## 1. 目标

`test_intent7` 要验证：

> UIWidget 的意图，经过显式的数据记录和操作展开，最终生成
> **和手写一样显式、可调控的 Vulkan C 命令列表**，并且以
> `[beginRendering ... endRendering]` 为 pass 边界。

链路：

```
UIWidget
  -> CommandBuilder / DrawSegment（UI 意图）
  -> shader_data::emit（意图 -> 操作：bind/set/draw）
  -> FrameBuilder（操作 -> 线性 OpStream）
  -> VkEmitter（Op -> 显式 vkCmd*）
  -> Vulkan C command list
```

## 1.1 根本底层：`[pass ... pass] => [Vulkan C API list]`

抽象层不是终点；每一层最终都必须变成显式、有序的 `vkCmd*` 列表。

- `Opcode -> c_api_name()`：每个 Opcode 必须映射到一个官方 `vkCmd*`，没有 abstract-only op。
- `OpStream -> c_api_list()`：按录制顺序输出 C API 列表。
- `c_api_coverage_ok()`：检查所有 Opcode 都有 C API 名称。
- `VkEmitter` 的 `switch` 有 `default: assert(false)`：未映射的 Opcode 不能在 debug 下静默通过。
- 运行时日志：`[pass-c-api] opcodes=37 calls=54`，随后逐行列出 `#N vkCmd*`。

当前一帧的尾部示例：

```text
#45 vkCmdFillBuffer
#46 vkCmdPipelineBarrier2
#47 vkCmdCopyBuffer
#48 vkCmdFillBuffer
#49 vkCmdCopyImageToBuffer
#50 vkCmdPipelineBarrier2
```

这条列表就是“根本底层”的直接证据：pass 的数据/操作最终落到显式 C API。

### 1.2 系统层器官（本轮补）

| 器官 | 当前实现 | 状态 |
|---|---|---|
| Pass domain | `PassKind { Graphics, Compute, Transfer }`；`beginComputePass/beginTransferPass` | transfer pass + compute pass 都已接入；compute 用 push constants，暂无 compute descriptor set 用例 |
| Queue/submission | `FrameScheduler` + `QueueRole`/`QueueBinding`；acquire/submit/present | 已接入主循环；当前只有 graphics+present 单队列 |
| Resource state | `ResourceStateTracker` + `BarrierPlanner`；显式 `VkImageMemoryBarrier2` | 已接入 pick/present 两个 transition；仍是 per-transition seed，不是全帧 registry |

### 1.3 数据范围（本轮补全）

数据分类保持有限，但每类改成完整 Vulkan 数据容器，新增命令只加 opcode/payload/emitter，不再改 `PassData`：

| 分类 | 完整范围 |
|---|---|
| Descriptor | 多 `VkDescriptorSet`、firstSet、dynamic offsets、descriptor buffer bindings/offsets、resource/sampler heap、push descriptor writes |
| VertexInput | 多 vertex binding、offsets、sizes、strides、firstBinding |
| DynamicState | `StateOwnershipMap` 以真实 `VkDynamicState` 为 key；`DynamicStateValue` 以 bytes 存任意状态值，不再按状态加字段 |
| Sync | memory/buffer/image barrier 数组、dependencyFlags、event、conditional、query/timestamp、timeline semaphore、fence |
| Inheritance | 完整 `VkCommandBufferInheritanceInfo` + rendering/conditional/heap/viewport-scissor + pNext（caller-owned） |
| Transfer | `VkBufferCopy2` / `VkImageCopy2` / `VkBufferImageCopy2` / `VkImageBlit2` / `VkImageResolve2` |
| Feature | `VkPhysicalDeviceFeatures2` + Vulkan11/12/13/14 features + properties/limits + extension names + raw extension feature |
| pNext | `const void* pNext`；caller-owned chain，不产生 per-pass 堆分配 |
| Queue/Submission | queue family properties、`VkSubmitInfo2` 的 semaphore/command buffer submit info、acquire、present、command-buffer inheritance |
| ResourceState | image/buffer state + queue family ownership |
| Classic | `VkRenderPass` / framebuffer / clear values / subpass contents |
| Other | clear attachments、query results、debug label、transform/raw command data |

`RawCommandData { name, function, arguments }` 是最后的 escape hatch：  
vendor/future 命令可以直接走 raw data，不需要扩 `PassData`；命令映射仍是软的。

### 1.4 热路径分配策略

- `PassData` 不含 `std::vector` / `std::string` / `shared_ptr`；变量数据用 `std::span` 指向调用方存储。
- `PassData` 只保留 core state：Target / Pipeline / Descriptor / PushConstants / VertexInput / DynamicState / Draw / Sync / Inheritance + `FeatureSet*`。Transfer / classic / clear / query / debug / raw / frame data 放在各自 struct 和 OpPayload，不进入 per-pass cache。
- `StateOwnershipMap`：16 项 inline。
- `DynamicState`：16 项 × 128B inline。
- `PushConstants`：256B inline。
- `BarrierPayload`：8 个 memory/buffer/image barrier inline，常见 1 个 barrier 零堆分配。
- `CopyImageToBuffer` / `CopyBuffer` / `ExecuteCommands`：4 项 inline，不再每帧分配 vector。
- `FeatureSet`：`PassData` 只存指针，不再每次 pass 拷贝完整 features/properties。
- `OpStream`：FrameBuilder reserve 64，SecondaryBuilder reserve 16。
- legacy `PassRecorder` / `FrameRecorder` 已删除；`pass_stream.hpp` 只保留 `pass_boundary_barrier()`。

### 1.5 最终 review：test_ui_ability2 迁移

`ui_shader_device` / `ui_backend` 用到的 pass 级能力逐项核对：

| 能力 | 命令 | 状态 |
|---|---|---|
| begin/end rendering | `vkCmdBeginRendering` / `vkCmdEndRendering` | typed |
| pipeline | `vkCmdBindPipeline` | typed |
| descriptor set | `vkCmdBindDescriptorSets` | typed |
| index buffer | `vkCmdBindIndexBuffer` | typed |
| viewport / scissor | `vkCmdSetViewport` / `vkCmdSetScissor` | typed |
| stencil reference | `vkCmdSetStencilReference` | typed（新增） |
| push constants | `vkCmdPushConstants` | typed |
| indirect draw | `vkCmdDrawIndexedIndirect` | typed |
| mip blit | `vkCmdBlitImage` | typed（新增） |
| barrier | `vkCmdPipelineBarrier2` | typed |
| multi-color / depth / resolve | `PassTarget` + `VkRenderingAttachmentInfo` | typed |

vkspec 核对结果：

- `vkCmdSetStencilReference` / `vkCmdSetViewport` / `vkCmdSetScissor` / `vkCmdBindPipeline` / `vkCmdBindDescriptorSets` / `vkCmdBindIndexBuffer` / `vkCmdPushConstants` 的 Render Pass Scope 是 **Both**；validator 已允许 pass 外调用。
- `vkCmdSetStencilReference` 只有在 pipeline 的 `VK_DYNAMIC_STATE_STENCIL_REFERENCE` 是 dynamic 时才有效；validator 用 `StateOwnershipMap` 检查。
- `vkCmdBlitImage` 是 Outside；validator 不在 graphics pass 内允许，logical transfer pass 允许。
- `vkCmdExecuteCommands` 在 dynamic rendering 内要求 `VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT`；validator 已检查。
- `c_api_coverage_ok()` 覆盖全部 Opcode；`VkEmitter` 未映射 opcode 会 assert。

结论：`test_ui_ability2` 的 pass 录制能力可以迁移到本框架；尚未实际改写 `test_ui_ability2.cpp` 源码。

## 2. 三轴模型

- **纵向：数据流**（官方 Pipelines 章的 block diagram）
  `Draw -> IA -> VS -> Tess -> GS -> VertexPost -> Raster -> EarlyFrag -> FS -> LateFrag -> Blend -> Attachments`
  资源：Vertex/Index/Indirect Buffer、Descriptor Sets/Buffers/Heaps、Push Constants、Uniform/Storage Buffer、Sampled/Storage Image。
- **横向：命令角色**（官方 Fundamentals 章的 Command Type）
  `Control / State / Action / Synchronization / Indirection`。
- **深度：生命周期 + Scope**
  Device / Frame / Pass / CommandBuffer / Draw / Sync / Inheritance / Feature；
  Inside / Outside / Both；Primary / Secondary；queue；video。

## 3. 文件职责

| 文件 | 职责 |
|---|---|
| `test_intent7_pass_data.hpp` | **数据 schema**：`PassTarget`、`PipelineBinding`、`DescriptorBinding`、`PushConstants`、`VertexInputBinding`、`DynamicState`、`DrawArgs`、`SyncState`、`Inheritance`、`FeatureSet`、`PassData`；以及 `PassDesc = PassTarget` 兼容别名、`query_features()`。 |
| `test_intent7_pass_opcode.hpp` | **操作层**：`Opcode`、`OpKind`、`Scope`、`Lifecycle`、`Op`、`OpStream`、`dump()`；`c_api_name()` / `c_api_list()` / `c_api_coverage_ok()` 把 `[pass...pass]` 直接映射到 `[vkCmd*]` 列表。 |
| `test_intent7_pass_emitter.hpp` | **透明 lowering**：`VkEmitter::emit(Op)`，每个 opcode 1:1 映射到显式 `vkCmd*`。 |
| `test_intent7_pass_validator.hpp` | **增量校验**：scope / lifecycle / basic compatibility；不做全状态树。 |
| `test_intent7_pass_builder.hpp` | **FrameBuilder**：`beginPass/state/action/sync/endPass` -> 更新 `PassData` -> 追加 `OpStream` -> 校验 -> emit；可选用 `deferEmit` + `emitAll()` 做纯 build-then-emit。**StreamRecorder**：无校验的 secondary 状态/动作录制器；**SecondaryBuilder**：共享 `PassData`/`PassValidator` 的 secondary 录制器。 |
| `test_intent7_pass_compiler.hpp` | `recordPasses()`：按 `PassSource[]` 顺序 `beginPass -> emit -> endPass`，并在 pass 之间插 `pass_boundary_barrier()`。 |
| `test_intent7_frame_scheduler.hpp` | **系统层**：`QueueRole` / `QueueBinding` / `FrameScheduler`：acquire (`vkAcquireNextImageKHR`)、submit (`vkQueueSubmit`)、present (`vkQueuePresentKHR`)；queue role 建模。 |
| `test_intent7_resource_state.hpp` | **系统层**：`ResourceStateTracker` (layout/access/stage) + `BarrierPlanner` (显式 `VkImageMemoryBarrier2` -> `FrameBuilder::barrier`)；不隐藏同步。 |
| `test_intent7_pass_abi.hpp` | `PipelineState` / `Pass` / `PassManager` / `PassSource`：每帧 extra pass 的数据与 GPU buffer。 |
| `test_intent7_frame_context.hpp` | `ShaderBuffers` + `CommandBuilder` + `shader_data::emit()`：UI 意图 -> 操作。 |
| `test_intent7_pass_tests.hpp` | 可视化用例：main 背景 + 3 个 card pass + 1 个 overlay pass。 |
| `shaders/test_intent7.comp` | 最小 compute shader：push constants + 空 body，用于 `beginComputePass + BindPipeline(COMPUTE) + Dispatch`。 |
| `test_intent7.cpp` | 主 pass target、FrameBuilder、pass sources、dump、submit。 |

## 4. 数据 schema

`PassData` 是命令缓冲区状态的记录：

```cpp
PassData
├─ target          PassTarget（VkRenderingInfo + color[N]/depth/stencil/resolve/load-store/clear）
├─ pipeline        PipelineBinding（VkPipeline + VkPipelineLayout + rendering formats + state ownership）
├─ descriptors     DescriptorBinding（layout + set）
├─ pushConstants   PushConstants（layout/stages/offset/size/data）
├─ vertexInput     VertexInputBinding（index buffer/offset/type）
├─ dynamicState    DynamicState（viewport/scissor/... + valid[]）
├─ draw            DrawArgs（kind + direct/indirect operands）
├─ sync            SyncState（boundary barrier）
├─ inheritance     Inheritance（secondary rendering info）
├─ features        FeatureSet（feature gates）
└─ passActive      bool
```

生命周期：

| 数据 | 生命周期 | 说明 |
|---|---|---|
| `PassTarget` | pass | 只在 `beginRendering/endRendering` 之间有效 |
| `PipelineBinding` | device / bind | pipeline 创建一次，绑定持续到改变 |
| `DescriptorBinding` | frame / bind | set 可以是 frame 生命周期，绑定持续到改变 |
| `PushConstants` | command buffer / draw | 设置后持续到改变 |
| `VertexInputBinding` | device / bind | buffer 是 device/frame，binding 是 command buffer 状态 |
| `DynamicState` | command buffer | begin 时 undefined；pipeline static 状态会覆盖；`commandBufferInheritance` 可继承 |
| `DrawArgs` | draw | 一次 draw 命令 |
| `SyncState` | sync op | barrier/query/event；本 demo 只用 pass boundary memory barrier |
| `Inheritance` | secondary | secondary 录制/执行期间 |

## 5. Opcode table

| Opcode | Role | Payload | 备注 |
|---|---|---|---|
| `BeginRendering` | Control | `PassTarget` | 在 pass 外调用 |
| `EndRendering` | Control | - | 在 pass 内调用 |
| `BindPipeline` | State | `VkPipeline` + layout | graphics bind point |
| `BindDescriptorSet` | State | layout + set | firstSet=0 |
| `BindDescriptorBuffer` | State | layout + `VkDescriptorBufferBindingInfoEXT` + offset | descriptor buffer 路径 |
| `BindResourceHeap` / `BindSamplerHeap` | State | `VkBindHeapInfoEXT` | descriptor heap；通过 `vkGetDeviceProcAddr` 取函数 |
| `PushConstants` | State | layout/stages/offset/size/data | 最大 128 bytes（demo） |
| `SetViewport` | State | `VkViewport` | first=0, count=1 |
| `SetScissor` | State | `VkRect2D` | first=0, count=1 |
| `SetDepthTestEnable` / `SetDepthWriteEnable` | State | `VkBool32` | 动态深度状态 |
| `SetCullMode` / `SetFrontFace` / `SetPrimitiveTopology` / `SetStencilReference` | State | 对应枚举 | 动态固定功能状态 |
| `BindIndexBuffer` | State | buffer/offset/type | |
| `Draw` / `DrawIndexed` / `DrawIndirect` | Action | draw operands | 直接/索引/间接 |
| `DrawIndexedIndirect` | Action | buffer/offset/drawCount/stride | demo 主路径 |
| `CopyImageToBuffer` | Action | image/layout/buffer/regions | pick 回读 |
| `CopyBuffer` / `CopyBufferToImage` / `CopyImage` / `BlitImage` | Action | src/dst/regions/filter | transfer；`CopyBuffer` 在 per-frame scratch 上实际使用 |
| `FillBuffer` / `UpdateBuffer` | Action | buffer/offset/data | transfer；`FillBuffer` 在 pick 回读前实际使用 |
| `ClearColorImage` / `ClearDepthStencilImage` | Action | image/layout/clear/ranges | outside pass |
| `Dispatch` / `DispatchIndirect` | Action | group counts / indirect buffer | compute, outside pass |
| `ClearAttachments` | Action | attachments + rects | pass 内 clear |
| `BeginQuery` / `EndQuery` | Sync | query pool/query/flags | occlusion/timestamp 等 |
| `Barrier` | Sync | memory/buffer/image barrier arrays | pass 边界 + frame sync |
| `ExecuteCommands` | Indirection | secondary command buffers | CASE 2 的 pass body 走 secondary |

新增一个命令变体时：

1. 在 `Opcode`/`OpKind` 加枚举；
2. 在 `test_intent7_pass_opcode.hpp` 加 payload 结构；
3. 在 `dump()` 加 C 伪代码；
4. 在 `VkEmitter::emit()` 加显式 `vkCmd*`；
5. 在 `FrameBuilder` 加语义方法；
6. 在 `PassValidator` 加 scope/lifecycle 检查。

**不要**为每个命令加一个类；opcode + payload + emitter case 就够了。

## 6. 透明 emitter 与手写质量

`VkEmitter::emit(Op)` 是一个直接的 switch：

```cpp
case Opcode::BindPipeline:
    vk.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipeline);
    break;
case Opcode::DrawIndexedIndirect:
    vk.vkCmdDrawIndexedIndirect(cmd, p.buffer, p.offset, p.drawCount, p.stride);
    break;
```

没有隐式重排、没有隐藏 barrier、没有隐藏 pass。  
`FrameBuilder::dump()` 输出与手写一致的 C 伪代码，例如：

```text
#0 // [Outside / Sync] vkCmdPipelineBarrier2
vkCmdPipelineBarrier2(cmd, &dependency{memory=0, buffer=0, image=5}); // layout transition
#1 // [Outside / Pass] vkCmdBeginRendering
vkCmdBeginRendering(cmd, &rendering_info{area=1280x900, colorCount=2, hasDepth=true});
#2 // [Inside / CommandBuffer] vkCmdBindIndexBuffer
vkCmdBindIndexBuffer(cmd, buffer=..., offset=0, type=1);
#3 // [Inside / CommandBuffer] vkCmdBindPipeline
vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline=...);
#4 // [Inside / CommandBuffer] vkCmdBindDescriptorSets
vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout=..., 0, 1, &set=..., 0, nullptr);
#5 vkCmdSetViewport(...)
#6 vkCmdSetScissor(...)
#7 vkCmdPushConstants(...)
#8 vkCmdDrawIndexedIndirect(...)
#9 vkCmdEndRendering(cmd);
#10 // [Outside / Sync] vkCmdPipelineBarrier2
vkCmdPipelineBarrier2(cmd, &dependency{memory=1, buffer=0, image=0}); // pass boundary
```

`dump()` 只在第一帧打印；如需每次都打印，可把 `static bool dumped` 改成环境变量控制。

## 7. 同步

- 显式同步是 Op：`Barrier`，payload 拥有 `memoryBarriers/bufferBarriers/imageBarriers` 三个 vector；emitter 在调用时构造临时的 `VkDependencyInfo`，不留下悬空指针。
- frame sync 已经进入 OpStream：
  - `#0`：5 张 image 的 `UNDEFINED -> ATTACHMENT_OPTIMAL` layout transition（color/MSAA/depth/pick/resolve）；
  - 每个 pass 之间：`pass_boundary_barrier()` 的 memory barrier（store -> load）；
  - pass 之后：pick resolve `COLOR_ATTACHMENT -> TRANSFER_SRC`；
  - 最后：swapchain `COLOR_ATTACHMENT -> PRESENT_SRC`。
- `pass_boundary_barrier()` 的 src/dst stage 是 color/depth framebuffer stages，access 是 attachment read/write。
- pass 内 barrier 仍通过 `FrameBuilder::barrier()` 可用，但要注意：
  - `vkCmdPipelineBarrier2` 在 `beginRendering` 内需要 `dynamicRenderingLocalRead` 等条件；
  - 本 demo 的 barrier 都在 pass 之间（Outside），所以没有这个限制。

## 8. 每帧 PassManager

`passManagers[MAX_FRAMES_IN_FLIGHT]` 与 `drawRecorder` 帧槽位对齐，避免 CPU 写 pass buffer 时 GPU 还在读上一帧。  
`FrameBuilder` 每帧新建，`PassData`/`OpStream`/`Validator` 都是帧内临时对象。

## 9. 已知缺口

- typed opcode 只覆盖 test_intent7 用到的命令；其余 `vkCmd*` 仍可通过 `PassRecorder::vk()` / `FrameRecorder::vk()` 或后续新增 opcode 接入。
- `DescriptorBinding` 已有 set / descriptor buffer / resource heap / sampler heap 变体；heap 的 emitter 通过 `vkGetDeviceProcAddr` 取函数（VolkDeviceTable 版本未包含）。
- `Inheritance` / `commandBufferInheritance` 有真实用例：CASE 2 的 pass body 录进 secondary command buffer，primary pass 用 `vkCmdExecuteCommands` 执行；`ExecuteCommandsPayload` 携带 `InheritanceRenderingInfo`，`PassValidator` 独立校验 flags/viewMask/color formats/depth/stencil/samples 与 pass target 匹配。secondary 本身现在用 `SecondaryBuilder` 录制，走同一套 `PassData`/`PassValidator`/`VkEmitter`。
- `PipelineBinding.ownership` 现在从 `StateOwnershipMap` 填充；`PassValidator` 会检查 `vkCmdSet*` 对应的状态在 bound pipeline 中是 `Dynamic`/`Inherited`，而不是 `Static`/`Absent`。
- `DynamicState` 已覆盖 viewport/scissor/depth test/depth write/cull/front/topology；其他状态字段可继续加 opcode。
- `PassValidator` 已做 pipeline-target 的 color/depth/stencil format、viewMask、sample count 兼容性，descriptor layout == pipeline layout，dynamic-state ownership，secondary inheritance，以及 descriptorBuffer/descriptorHeap/dynamicRenderingLocalRead feature gate；其他 feature gate 可以继续加。
- `FrameSync` 和 `CopyImageToBuffer` 已进入 OpStream；`vkCmdBeginCommandBuffer`/`vkCmdEndCommandBuffer`、queue submit/present 仍在 FrameBuilder 外（属于 frame/queue 层）。
- `ClearAttachments` / `BeginQuery` / `EndQuery` 已有 typed opcode，但 demo 未实际使用；`ExecuteCommands` 已实际使用。
- `FillBuffer` / `CopyBuffer` 已在 per-frame transfer scratch + pick 回读路径实际使用；`CopyBufferToImage` / `CopyImage` / `UpdateBuffer` / `ClearColorImage` / `ClearDepthStencilImage` / `Dispatch` / `DispatchIndirect` 已有 typed opcode，但 demo 未实际使用；它们统一走 `push_transfer`（Outside）。
- `PassKind` 已有 Graphics/Compute/Transfer；transfer pass 已接入并产生 `[Inside / Sync]` 的 Fill/Copy/Barrier；compute pass 已接入 compute pipeline + push constants + `vkCmdDispatch`，但还没有 compute descriptor set 用例。
- 数据 schema 已扩到多 descriptor set/dynamic offsets、vertex binding 数组、通用 `VkDynamicState`、完整 sync/inheritance/transfer2/features/properties、pNext 指针（caller-owned）、queue/submission/present、classic render pass、raw command；RT/video/acceleration-structure 仍只有 `RawCommandData`，未 typed。
- `FrameScheduler` 已接管 acquire/submit/present，`QueueRole`/`QueueBinding` 已建模；当前只有 graphics+present 单队列，compute/transfer queue role 尚未实际使用。
- `ResourceStateTracker`/`BarrierPlanner` 已接入 pick/present 两个 transition；当前是 per-transition seed，不是全帧 resource registry。
- `test_intent7.cpp` 的注释在编辑过程中被编码转换损坏，已做 ASCII 清理；代码逻辑和构建不受影响。

## 10. 构建 / 运行

```powershell
cd E:\0_github_project\mcsvulkan\build
ninja mcsvulkan-vma-test_intent7
E:\0_github_project\mcsvulkan\output\test_program\mcsvulkan\vma\test_intent7.exe
```

预期日志：

```text
[frame-scheduler] graphics_family=0 present_family=0
[pass-test] passes=4 card_cmds=1 overlay_cmds=1
[pass-dump]
#0 // [Outside / Pass] vkCmdBeginRendering
...
[pass-c-api] opcodes=37 calls=54
  #0   vkCmdPipelineBarrier2
  #1   vkCmdBeginRendering
  ...
  #45  vkCmdFillBuffer
  #46  vkCmdPipelineBarrier2
  #47  vkCmdCopyBuffer
  #48  vkCmdBindPipeline          // COMPUTE
  #49  vkCmdPushConstants
  #50  vkCmdDispatch
  #51  vkCmdFillBuffer
  #52  vkCmdCopyImageToBuffer
  #53  vkCmdPipelineBarrier2
```

验证层无 `VUID` 错误。  
最终画面：`output/intent7_exec.png`（或重新截图）。

## 11. 设计审计（2026-11-08）

**对齐的部分**

- 主流程仍是 `UIWidget -> DrawSegment intent -> FrameBuilder(OpStream) -> VkEmitter -> vkCmd*`。
- 三轴模型仍成立：纵向数据流（Target/Pipeline/Resources/VertexInput/DynamicState）、横向 Opcode（Control/State/Action/Sync/Indirection）、生命周期/Scope（device/frame/pass/command-buffer/draw/sync/inheritance）。
- main/extra pass 都是 `PassSource`，没有“重开 main pass”的特殊路径。
- frame sync、pass boundary、copy、present 都在 OpStream；emitter 仍是透明 1:1。
- 验证层/provider、CASE 2 secondary、inheritance、state ownership、feature gate 都在同一条路径上。

**主要偏差 / 风险**

1. ~~secondary 的 `StreamRecorder` 绕过了 `PassData`/`PassValidator`~~ **已修**：  
   现在 secondary 由 `SecondaryBuilder` 录制，共享 `PassData`/`PassValidator`，只禁用 `BeginRendering/EndRendering`；pipeline-target、descriptor layout、dynamic ownership 都会在 secondary 内校验。

2. ~~`FeatureSet` 是手工镜像，不是从 `VkPhysicalDeviceFeatures2` 查询得到~~ **部分修**：  
   `pass_data::query_features(physical_device)` 查询本 app 实际启用的 core/Vulkan12/Vulkan13/ExtendedDynamicState feature；descriptorBuffer/descriptorHeap/dynamicRenderingLocalRead 仍显式 false，因为 app 的 device enable chain 没启用它们。查询“支持”不等于“启用”，所以没有把扩展支持直接当成可用。

3. ~~`PassTarget` 的 colorFormats/samples 是 app 提供的元数据，不是从真实 `VkImageView` 推导的~~ **已修**：  
   `initPipeline` 从 `msaaResourcesBuild` / `pickResourcesBuild` / `depthResourcesBuild` 的 create-info 构造 `passTargetTemplate`；`makeMainPassDesc` 从它拷贝 color/depth/samples，不再从 pipeline 常量手写。validator 现在比较 pipeline metadata vs **资源派生的 target metadata**。

4. **legacy `PassRecorder` / `FrameRecorder` 仍在**，但已降级为 raw fallback：  
   `Pass::record` 已删除；`pass_stream.hpp` 顶部明确标注 LEGACY / RAW FALLBACK；主路径只走 `FrameBuilder` / `SecondaryBuilder`。完整删除可后续做。

5. **`FrameBuilder` 默认“边构建边发射”**，但已加 `deferEmit` + `emitAll()`：可以先构建/校验整个 `OpStream`，再单点发射。默认仍是即时模式，不改变 demo 行为。

6. **`screen.makeRenderDirty(currentFrame)` 仍是 workaround**。  
   尝试过只更新 pass target 的 resolve view、不重建 UI；但 main pass 的 UI stream / camera 数据本身也是逐帧重建的，贸然去掉会让 main pass 使用旧 stream。正确拆法需要把“main pass 逐帧录制”和“extra pass target 更新”分开，当前保留强制重建更安全。

7. **`BarrierPayload` 已复制 `dependencyFlags`，但仍不复制 `VkDependencyInfo::pNext`**；`PassTarget::pNext` 也是裸指针。  
   当前 demo 的 pNext 为 null；一般扩展链需要 deep-copy 或明确“只在 immediate emit 时有效”。

8. **compute/transfer opcode**：`FillBuffer` + `CopyBuffer` 已在 per-frame scratch 路径实际使用（Fill -> barrier -> Copy）；`CopyImage`/`UpdateBuffer`/`Dispatch` 等仍 typed 但 demo 未使用，真实 compute pass 用例还没做。

**结论**

核心设计没有偏离：数据/操作/emitter/三轴仍在。偏差集中在“完整性”和“单一事实源”：
feature/format 元数据、legacy 双路径、pNext 生命周期。secondary 校验已由 `SecondaryBuilder` 补上。
这些可以在不改三轴模型的前提下修掉，优先级建议：

1. ~~`SecondaryBuilder`（共享 PassData/Validator，只禁用 Begin/End）~~ 已做；
2. ~~`FeatureSet::from_device`（真实 feature query）~~ 部分做（`query_features` 查 core；扩展 gate 需在启用扩展时同步）；
3. ~~从资源层推导 `PassTarget` metadata~~ 已做（`passTargetTemplate` 来自资源 create-info）；
4. ~~删除/deprecate legacy `PassRecorder`/`FrameRecorder`/`Pass::record`~~ 部分做：`Pass::record` 删除，legacy 标记 raw fallback；完整删除可选；
5. ~~`FrameBuilder` 的 build-only 模式~~ 已加 `deferEmit`/`emitAll`；barrier pNext 支持仍待做。
