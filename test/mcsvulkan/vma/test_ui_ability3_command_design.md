# test_ui_ability3 · `namespace command` 设计

## 把「一次 / 多次」变成边界

> 目标不是再包一层 `vkCmd*`，而是把 **scope / state / work / order / data** 五件事分开，
> 落到四条边界上：哪些只能一次、哪些必须多次，在类型上可见，在断言上可查，在合批时可计算。
>
> 对应代码：`test_ui_ability3.cpp` 的 `namespace command`（235–402 行）。
> 参考：`test_intent8_draw_ops.hpp`（op stream）、`test_intent8_draw_recorder.hpp`（逐 draw 展开）、
> `test_ui_ability2.cpp`（Recorder → compile → FramePlan → Backend）、
> `vk_command_buffer_data_algo.md`（“状态是肥的，指令是瘦的”）。

---

## 0. 先给结论

### 0.1 一句话规律

```text
括号一次（scope）
状态按变化（sticky）
工作按效果（effect）
同步按依赖（dependency）
```

翻成设计语言：

> **一个 `vkCmd*` 应该出现几次，不取决于它操作了几个对象，而取决于它的“效果”变了几次。**

- `vkCmdBeginRendering` 的效果是“开一个 scope”，所以一个 scope 一次。
- `vkCmdBindPipeline` 的效果是“让后续 draw 用这条管线”，所以一次 pipeline run 一次。
- `vkCmdDraw` 的效果是“画这一组图元”，所以一组图元一次，多少组都行。
- `vkCmdPipelineBarrier2` 的效果是“建立一条依赖”，所以一条依赖一次，多少条都行。

直接落到操作上：

| 操作 | 一次的单位 | 什么时候必须再发 |
|---|---|---|
| `vkBeginCommandBuffer` / `vkEndCommandBuffer` | 每次录制 | 新录制 |
| `vkCmdBeginRendering` / `vkCmdEndRendering` | 每个 render scope | 换 target / 换 load-store |
| `vkCmdBindPipeline` | 每个 pipeline run | pipeline 变 |
| `vkCmdBindDescriptorSets` | 每个 (layout, set) 变化 | set 或 layout 变 |
| `vkCmdSetViewport` | 每个 target | target 尺寸/区域变 |
| `vkCmdSetScissor` / `vkCmdSetStencilReference` | 每个值变化 | 值变 |
| `vkCmdPushConstants` | **每条 draw** | 每条 draw（除非搬进 buffer） |
| `vkCmdDraw*` / `vkCmdDispatch*` / `vkCmdCopy*` | 每组图元 / 每个 dispatch / 每个 copy region | 每个效果 |
| `vkCmdPipelineBarrier2` | 每条依赖 | 依赖变 |

> 注意最后两行的区别：draw/copy 是“工作”，每条都必须发；push constants 是“per-draw 数据”，
> 在 Vulkan 里虽然是 state 写入，但设计上应该按 data 处理（见边界 D）。

### 0.2 五种 scope

“一次”永远是“每个什么一次”。主绘制路径上最常用的“每个什么”，就是这五层：

| scope | 开始 | 结束 | 里面能变什么 | 里面不能变什么 |
|---|---|---|---|---|
| command buffer | `vkBeginCommandBuffer` | `vkEndCommandBuffer` | 一切状态 | 不能提交、不能跨 command buffer |
| render scope | `vkCmdBeginRendering` / `vkCmdBeginRenderPass2` | `vkCmdEndRendering` / `vkCmdEndRenderPass2` | pipeline、descriptor、dynamic state、draw | 不能换 target、不能 copy/blit、不能做 layout transition |
| pipeline run | `vkCmdBindPipeline` | 下一次 bind 或 scope 结束 | descriptor、dynamic state、draw | 换 layout 不兼容的 pipeline 时要重绑 set |
| draw | `vkCmdDraw*` | 下一条命令 | 无（draw 不改状态） | — |
| dependency | `vkCmdPipelineBarrier2` | 下一条依赖 | 无 | — |

这张表覆盖了主绘制路径上的“一次/多次”；query、debug label、subpass 也是同样的括号思想，只是不在 ability3 的路径上。

### 0.3 先纠正三个常见误解

1. **`vkCmdBeginRendering` 不是状态重置。**
   `vkBeginCommandBuffer` 会把所有状态置为 undefined；`vkCmdBeginRendering` 不会。
   pipeline、descriptor set、viewport、scissor、push constants 都还在。
   规范原文：dynamic state “persists for the lifetime of the command buffer, or until modified … or made invalid by binding a pipeline in which that state is statically specified”
   （`vkspec.html#dynamic-state-lifetime`）。

2. **draw 不改状态，bind 不产生像素。**
   draw 是“消费当前寄存器”；bind/set/push 是“写寄存器”。
   所以“每个 draw 前都 bind 一次”不是语义要求，只是录制器的偷懒。

3. **descriptor set 和 push constants 的寿命不是“到下一次 bind 为止”。**
   descriptor set 在 layout 兼容时可以跨 pipeline 存活；push constants 的值也不会因为 bind 一个不兼容的 pipeline 而被清掉
   （`vkspec.html#vkCmdPushConstants`）。
   真正让它们“不能用”的是 **layout 不兼容**：descriptor set 可能被 disturb，push constants 的值还在但 draw 会非法。
   不是 pipeline 句柄本身。

---

## 1. 四条边界

### 边界 A：scope boundary —— 括号在哪里闭合

```text
command buffer
└── render scope (target)
    └── pipeline run
        └── draw
```

- `BeginRendering` 必须在一个 render pass instance 之外（`VUID-vkCmdBeginRendering-renderpass`）。
- `EndRendering` 必须在同一个 command buffer、同一个 scope 内（`VUID-vkCmdEndRendering-commandBuffer-06162`）。
- 不能嵌套；两个 scope 只能顺序出现。
- 没有 target 时，render scope 这一层消失，command buffer 就是唯一的 scope。

**设计含义：** `pass` 必须拥有 `begin/end`，而不是让调用方手动配对。
`command_pass` 现在的写法（`transition` + `rendering_scope_build`）方向是对的，但 `rendering_scope_build::apply`
里 begin 完立刻 end，中间没有 body——它现在只是一个“空 pass”。把 body 加进去，它才是 pass。

### 边界 B：state boundary —— 寄存器什么时候失效

| 状态 | 写入命令 | 寿命 | 失效于 |
|---|---|---|---|
| pipeline | `vkCmdBindPipeline` | 到下一条 bind | 换 bind point / 换 pipeline |
| descriptor sets | `vkCmdBindDescriptorSets` | 到被 disturb | 用不兼容 layout 绑 set N 会 disturb 其它 set（规则见 17.2.1） |
| viewport / scissor | `vkCmdSetViewport/Scissor` | 到被改 / 被 static pipeline 置为 invalid | 绑了把该状态声明为 static 的 pipeline |
| stencil reference | `vkCmdSetStencilReference` | 同上 | 同上 |
| vertex/index buffer | `vkCmdBindVertexBuffers/IndexBuffer` | 到下一条 bind | 换 binding |
| push constants | `vkCmdPushConstants` | 到 command buffer 结束（值不清） | 不因 bind 失效；但 draw 时要求当前 pipeline layout 与写入时的 layout 兼容 |

两条最容易写错的规则：

1. **static pipeline 会“污染”dynamic state 的时序。**
   - 先 `setViewport`，再 bind 一条 static viewport 的 pipeline：合法，但 static 值覆盖。
   - bind static 之后再 `setViewport`，然后 draw：**非法**（`VUID-vkCmdDraw-None-08608`）。
   - bind dynamic pipeline 后没有 `setViewport` 就 draw：**非法**（`VUID-vkCmdDraw-None-07831`）。
   所以状态缓存不能只记“当前值”，还要记“这个值是不是在最后一次 static bind 之后写的”。

2. **descriptor set 的兼容性是“到 N 为止”。**
   layout 对 set 0..N 相同，才叫 compatible for set N。
   在 N 处绑一个不兼容 layout，可能 disturb 低编号的 set；
   如果 N 处原来的 set 是用不兼容 layout 绑的，N 之后的 set 也会被 disturb
   （`vkspec.html#descriptors-compatibility`）。

**设计含义：** `command` 层需要 `pipeline_info`（layout、dynamic state mask、push range、target formats），
否则它只能“转发”，不能“校验”，也不能决定“这个 bind 能不能省”。

### 边界 C：order boundary —— 什么能重排，什么不能

Vulkan 的 primitive order 是（`vkspec.html#drawing-primitive-order`）：

1. submission order（draw 调用顺序）；
2. indirect draw 按 indirect buffer 地址从低到高；
3. 一条 draw 内按 instance 编号从低到高；
4. 一个 instance 内按 vertex/index 顺序。

这条规则直接决定了合批的合法性：

- **相邻 draw 合并成一条 instanced draw**：合法，且保持 painter's order（instance i 就是原来的第 i 个 draw）。
  前提是顶点数/首顶点相同，或者 shader 用 `gl_InstanceIndex` 从 buffer 里取每个实例的数据。
- **相邻 draw 合并成一条 `drawIndexedIndirect`**：合法，indirect command 按 buffer 顺序执行。
- **按 pipeline 全局排序**：只有“顺序无关”的 pass 才能做。
  - 不透明 + depth test/write：通常可以（等深度 tie 仍可能受顺序影响；需要 depth bias 或稳定 tie-break）。
  - 半透明 UI / alpha blending：**不可以**。UI 是 painter's algorithm，重排会改变结果。
  - stencil mask / even-odd：更不可以，模板计数依赖顺序。

**设计含义：** `pass` 需要一个 `ordering` 声明（`painter` / `independent`）。
编译器只能对 `independent` 做排序；`painter` 只能做“相邻合并”。

### 边界 D：data boundary —— 什么该进 state，什么该进 data

这是合批真正的天花板，也是 ability3 现在最值得改的地方。

| 放进 state（每条 draw 前要设） | 放进 data（一条 draw 内可变） |
|---|---|
| pipeline、descriptor set、viewport、scissor、stencil ref | 顶点/实例数据、颜色、矩形、mode、pick id |
| 所有 draw 共享的东西 | 每个图元/实例不同的东西 |

> 注意：`vkCmdPushConstants` 在 Vulkan 里是 state 写入；这里把它归到 data，是因为它承载 per-draw 数据。
> 判断标准不是 API 分类，而是“它是否随 draw 变化”。

- 放进 state 的，每条 draw 都要一次 `vkCmdSet*` / `vkCmdPushConstants`，**打断合批**。
- 放进 data 的，可以一条 draw 画 N 个实例，**不打断合批**。

ability3 现在的 `PushData` 是 128 B，正好是规范保证的 `maxPushConstantsSize` 下限；
`uiRect/uiColor/uiParams/uiAux/uiPick` 全是“每个命令不同”的数据，却都塞在 push constants 里。
所以主 pass 的每条命令都必须 `pushConstants + draw`，**几乎合不了批**
（除非相邻命令的 push/scissor 恰好相同）。这不是 `namespace command` 能解决的，是数据模型问题。

---

## 2. `BeginRendering / EndRendering` 的全部可能性

### 2.1 它到底是什么

一个 render pass instance 由这些输入定义：

```text
VkRenderingInfo
├── flags                 // 内容来源 / suspend / resume / 扩展
├── renderArea            // 这个 scope 影响的区域
├── layerCount / viewMask // 多层 / multiview
├── pColorAttachments[]   // 0..N 个颜色附件（imageView 可为 VK_NULL_HANDLE）
├── pDepthAttachment      // 可空
└── pStencilAttachment    // 可空；常与 depth 共用 imageView
```

每个 attachment 又带：

```text
imageView / imageLayout
resolveMode / resolveImageView / resolveImageLayout
loadOp / storeOp / clearValue
```

`vkCmdBeginRendering` 做的是：**建立这个 scope，执行 load op；附件必须已经处于 `imageLayout` 声明的 layout**。
`vkCmdEndRendering` 做的是：**结束 scope，执行 store op 和 resolve**。
scope 本身不做 layout transition；transition 必须在 begin 之前 / end 之后。

### 2.2 十个轴（设计上要能表达的全部可能性）

| 轴 | 可能取值 | 设计字段 | 关键约束 |
|---|---|---|---|
| scope 种类 | dynamic / classic / none | `target.kind` | dynamic 与 classic 不能混；pipeline 兼容性不同 |
| command buffer 级别 | primary / secondary | `pass.level` | secondary 自己 begin 时不能带 `RENDER_PASS_CONTINUE` |
| 内容来源 | inline / secondary-only / both | `target.flags` | `CONTENTS_SECONDARY` 时 primary 只能 `executeCommands` |
| 生命周期 | 单次 / suspend-resume / continuation(LOAD) | `pass.lifecycle` | suspend 之间不能有 action/sync；resume 的 `VkRenderingInfo` 必须一致 |
| 颜色附件 | 0..N，含 null view | `target.colors` | 格式/采样数必须与 pipeline 的 `VkPipelineRenderingCreateInfo` 匹配 |
| 深度/模板 | 无 / depth / depth+stencil / 分开 | `target.depth/stencil` | 同一 imageView 时 resolve 规则受 `independentResolve` 限制 |
| 区域 | renderArea | `target.area` | 必须在附件尺寸内；不是 scissor |
| 多层/视图 | layerCount / viewMask | `target.layer_count/view_mask` | 与 pipeline 的 viewMask 一致 |
| 载入/存储 | CLEAR/LOAD/DONTCARE + STORE/NONE | `attachment.load/store` | 按内容决定：首个 scope 通常 CLEAR，继续写同一 target 用 LOAD；没有隐式顺序 |
| 结束方式 | end / end2 / suspend | `pass.end_mode` | suspend 不 store/resolve |

### 2.3 六种拓扑（这是“怎么操作这些可能性”的答案）

#### 拓扑 1：单 scope（CLEAR → draws → STORE）

```text
transition(before) → beginRendering(CLEAR) → bind/draw... → endRendering → transition(after)
```

ability3 的 layer pass、main pass 都是这种。

#### 拓扑 2：continuation（LOAD → draws → STORE）

```text
... 前一个 scope 结束 → barrier → beginRendering(LOAD) → draws → endRendering
```

ability3 的“毛玻璃第二段 pass”（13211–13260）就是这种：`loadOp = LOAD`，保留主通道已有画面。
**注意：两个 scope 之间没有隐式顺序**，规范明确说 “There is no implicit ordering between separate render passes”
（`vkspec.html#vkCmdEndRendering` 的 note）。所以第二个 scope 之前必须有一条 barrier，
即使附件相同、layout 相同。

#### 拓扑 3：离屏（CLEAR → STORE → transition to sampled）

```text
transition(layer → COLOR_ATTACHMENT) → beginRendering(CLEAR) → draws → endRendering
→ transition(layer → SHADER_READ_ONLY) → 后续 pass 采样
```

ability3 的 A/B/C/D 层（11687–11747）就是这种。
**离屏 pass 的数量由“target 变化点”决定，不由 draw 数量决定。**

#### 拓扑 4：suspend / resume（跨 command buffer 拆一个 scope）

```text
CB1: beginRendering(SUSPENDING) → draws → endRendering   // 不 store
CB2: beginRendering(RESUMING)  → draws → endRendering   // store/resolve
```

约束（`vkspec.html#renderpass-suspension`）：

- 两个 `VkRenderingInfo` 的内容必须一致，除了 `SUSPENDING/RESUMING/CONTENTS_SECONDARY` 三个 flag；
- suspend 和 resume 之间不允许任何 action / synchronization 命令，也不允许别的 render pass instance；
- `vkCmdBeginRendering` 不能出现在“suspended 之间”（`VUID-vkCmdBeginRendering-suspended`）。

ability3 不需要它；但设计上不要把 `pass` 写死成“必须一次 begin/end”，否则以后做并行录制要重写。

#### 拓扑 5：secondary 的两种用法

```text
A. self-contained secondary：
   secondary: beginRendering → draws → endRendering
   primary:   executeCommands(secondary)        // primary 不能在 scope 内

B. body-only secondary：
   primary:   beginRendering → executeCommands(secondary) → endRendering
   secondary: 只录 bind/draw，不 begin/end      // 带 RENDER_PASS_CONTINUE
```

- A：secondary 自己 begin 时**不能**带 `VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT`
  （`VUID-vkCmdBeginRendering-commandBuffer-10914`）。
- B：secondary 带 `RENDER_PASS_CONTINUE`，`VkCommandBufferInheritanceRenderingInfo` 必须与 primary 的 target 匹配。
- `CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT` 的语义是“这个 scope 的内容全在 secondary 里，primary 里只有 execute”；
  要 inline + secondary 混用，需要 `nestedCommandBuffer` 特性 / `CONTENTS_INLINE_BIT`。

ability3 现在全是 primary inline；这两种先不实现，但 `pass` 的字段要留得下。

#### 拓扑 6：无 target（compute / transfer）

```text
command buffer begin
  bind compute pipeline / descriptor / push
  dispatch ...
  pipelineBarrier2
  copyBuffer / copyImageToBuffer ...
command buffer end
```

没有 `BeginRendering/EndRendering`，command buffer 本身就是 scope。
**“一次/多次”的规则完全不变**：pipeline 一次一条 run，descriptor 一次一个 layout，push 一次一个 dispatch，
dispatch/copy 多次，barrier 一次一条依赖。
所以 `pass.target` 应该是 `std::optional<target>`，而不是“必须有 target”。

**但要说清楚本设计的边界：** 3.2 的 `pass.body` 只放 `draw_item`，因为 ability3 是纯图形。
无 target 的 pass 现在只能承载 `draw`（合法但通常没意义）；要把 compute/transfer 也放进同一个 `pass`，
把 `body` 改成 `std::vector<op>`，`op = std::variant<draw_item, dispatch_item, copy_item>`，`plan_pass` 按 variant 分发。
边界不变：**无 target 就没有 begin/end，只有 body**；copy 仍然不能进有 target 的 pass。

### 2.4 scope 内部允许什么

| 命令 | 在 render scope 内 | 说明 |
|---|---|---|
| `vkCmdBindPipeline` | 可以 | command table: render pass scope = Both |
| `vkCmdBindDescriptorSets` | 可以 | 同上 |
| `vkCmdPushConstants` | 可以 | 同上 |
| `vkCmdSetViewport/Scissor/StencilReference` | 可以 | 同上 |
| `vkCmdDraw*` | 可以 | 这是 scope 的主要目的 |
| `vkCmdExecuteCommands` | 可以 | 受 secondary 兼容性约束 |
| `vkCmdBeginQuery/EndQuery` | 可以 | 必须配对 |
| `vkCmdCopyBuffer/BlitImage/ClearColorImage` | **不可以** | command table: render pass scope = Outside |
| `vkCmdPipelineBarrier2` | 受限 | 默认不能有 buffer/image memory barrier；不能做附件 layout transition（除非 `dynamicRenderingLocalRead` 等） |
| `vkCmdBeginRendering` | **不可以** | 不能嵌套（`VUID-vkCmdBeginRendering-renderpass`） |
| `vkCmdBeginRenderPass2` | **不可以** | classic 与 dynamic 不能在同一 scope 混用 |

barrier 的精确限制（`vkspec.html#vkCmdPipelineBarrier2`）：

- 未启用 `dynamicRenderingLocalRead` 时，dynamic rendering scope 内**不能有 buffer/image memory barrier**
  （`VUID-vkCmdPipelineBarrier2-None-09554`）；
- 只能有 memory barrier，且 access mask 只能包含 color/depth attachment read/write
  （`VUID-vkCmdPipelineBarrier2-None-09586`）；
- 也就是说：**layout transition 必须放在 scope 外**。

这正是 `transition_image` 现在被放在 `rendering_scope_build` 外面的原因；这个边界要保留，不能为了“少写几个 pass”把 transition 塞进 scope。

### 2.5 结束的语义

| 结束方式 | 发生什么 | 什么时候用 |
|---|---|---|
| `vkCmdEndRendering` | store + resolve（按 attachment 的 storeOp/resolveMode） | 绝大多数情况 |
| `vkCmdEndRendering` + `SUSPENDING` | 不 store；scope 挂起 | 跨 command buffer 拆 scope |
| `vkCmdEndRendering2KHR` | 可带 `VkRenderingEndInfoKHR` | 需要 end 扩展信息时 |
| `storeOp = NONE` / `DONTCARE` | 不保留附件内容 | 中间层、不需要结果的 pass |

**不要依赖“两个 pass 之间自动保序”**：规范明确说没有隐式顺序，需要 barrier。
ability3 的 glass pass 之前如果只靠“同一个 target”就认为画完了，是错的；必须显式 barrier。

---

## 3. `namespace command` 的推荐结构

### 3.1 四层，不要混

```text
UI 命令表 (ui_cmd)
      │  build
      ▼
command::recorder      // 意图：pass + draw_item，不做 Vk* 调用，不做优化
      │  compile
      ▼
command::frame         // 实际：有序 stage，已去重 / 已合批，值语义
      │  plan
      ▼
command::plan          // 实际调用序列：bind/set/push/draw/begin/end/barrier/copy
      │  encode
      ▼
VkCommandBuffer
```

四层的职责：

| 层 | 输入 | 输出 | 允许做什么 | 禁止做什么 |
|---|---|---|---|---|
| recorder | UI/场景 | pass + draw_item | 记录意图、断言边界 | 调 `vkCmd*`、做优化 |
| compiler | recorder 的 pass | 合批后的 pass | 相邻合并、排序（仅 independent） | 改语义、跨 target 合并 |
| planner | pass + pipeline_info | `std::vector<call>` | 去重、校验、决定“一次/多次” | 调 `vkCmd*` |
| encoder | call | `vkCmd*` | 1:1 发射 | 做策略、做优化 |

**为什么要把 planner 单独拿出来？**
因为“实际发了几条命令”必须和 `c_api_list` 完全一致。
planner 是唯一的“基数决定者”：它决定 bind 几次、draw 几次；
encoder 只是把 call 翻译成 `vkCmd*`；`c_api_list` 只是把 call 翻译成字符串。
这样 `c_api_list` 不可能和实际发射不一致。

### 3.2 数据类型（建议的最小集合）

```cpp
namespace command
{
    // ── 0. 整个 frame 不变的上下文 ─────────────────────────────
    struct pipeline_info
    {
        VkPipeline handle{};
        VkPipelineLayout layout{};
        VkShaderStageFlags push_stages{};
        std::uint32_t push_size{};
        // 这条 pipeline 把哪些状态声明为 dynamic
        std::span<const VkDynamicState> dynamic_states{};
        // 这条 pipeline 期望的 target
        std::span<const VkFormat> color_formats{};
        VkFormat depth_format{VK_FORMAT_UNDEFINED};
        VkFormat stencil_format{VK_FORMAT_UNDEFINED};
        VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
        std::uint32_t view_mask{0};
    };

    struct context
    {
        VkExtent2D extent{};                   // 主目标尺寸
        std::span<const pipeline_info> pipelines{};
    };

    // ── 1. target：一个 render scope 的全部输入（值语义）──────
    struct attachment
    {
        const void *p_next{};             // 扩展（attachment-level）
        VkImageView view{};
        VkImageLayout layout{VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkResolveModeFlagBits resolve_mode{VK_RESOLVE_MODE_NONE};
        VkImageView resolve_view{};
        VkImageLayout resolve_layout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkAttachmentLoadOp load{VK_ATTACHMENT_LOAD_OP_LOAD};
        VkAttachmentStoreOp store{VK_ATTACHMENT_STORE_OP_STORE};
        VkClearValue clear{};
    };

    struct target
    {
        const void *p_next{};             // 扩展（scope-level：FSR/FDM/device group/...）
        VkRect2D area{};
        std::uint32_t layer_count{1};
        std::uint32_t view_mask{0};
        VkRenderingFlags flags{};
        std::vector<attachment> colors;
        std::optional<attachment> depth;
        std::optional<attachment> stencil;
    };

    // ── 2. 状态键：只放“一条 draw 内不能变”的东西 ─────────────
    struct draw_state
    {
        VkPipeline pipeline{};
        VkDescriptorSet set{};
        VkDescriptorSetLayout set_layout{};      // 用于 layout 兼容性校验
        VkRect2D scissor{};
        std::optional<std::uint32_t> stencil_ref; // 没值 = 不改/用 pipeline 静态值
        // viewport 不放这里：它是 target 的属性，一个 pass 一次
        bool operator==(const draw_state &) const noexcept = default;
    };

    // ── 3. draw_item：一条“意图”draw ───────────────────────────
    struct draw_item
    {
        draw_state state;
        PushData push{};                  // 现在：128 B 全量
        std::uint32_t vertex_count{3};
        std::uint32_t first_vertex{0};
    };

    // ── 4. pass：一个 scope + body ─────────────────────────────
    struct pass
    {
        std::optional<target> target;     // 有值：begin/end；空：compute/transfer，无括号
        VkViewport viewport{};            // 有 target 时有效；一个 pass 一次
        std::vector<VkImageMemoryBarrier2> before;  // begin 之前
        std::vector<draw_item> body;      // 只放 draw；path 宏用 group
        std::vector<VkImageMemoryBarrier2> after;   // end 之后
    };

    // ── 5. frame：有序 stage ───────────────────────────────────
    struct copy_image_to_buffer
    {
        VkImage image{};
        VkImageLayout layout{};
        VkBuffer buffer{};
        std::vector<VkBufferImageCopy> regions;
    };

    struct barrier_batch
    {
        std::vector<VkImageMemoryBarrier2> barriers;
    };

    using stage = std::variant<pass, barrier_batch, copy_image_to_buffer>;
    struct frame
    {
        std::vector<stage> stages;
    };
} // namespace command
```

几个刻意的选择：

- `target` 拥有 `std::vector<attachment>`，**不存 `VkRenderingInfo`**。
  `VkRenderingInfo` 里有 `pColorAttachments` 指针；一旦 vector 重新分配，指针就悬空。
  正确做法：`target` 存值，`encode` 时在栈上生成 `VkRenderingInfo` + `VkRenderingAttachmentInfo` 数组。
  你现在的 `rendering_info` / `rendering_info_submit` 已经是这个模式，保留它。

- `draw_state` 是值，不是指针。`draw_item` 可以随意复制、排序、合批。

- `pass` 有 `before/after` 两组 barrier，而不是一个 `transition_image` 字段。
  因为一个 pass 前后可能有多条依赖，而且 before/after 的位置不同：
  `before` 在 begin 之前（可以做 layout transition），`after` 在 end 之后。

- `frame` 用 `std::variant` 保持“pass / barrier / copy”的顺序。
  不要分成 `passes` + `copies` 两个 vector，否则顺序丢失，sync 就错了。

- `pipeline_info` 的生命周期必须覆盖整个 frame 的录制；它通常放在 device 级 registry 里，
  不能指向 `init()` 里的临时数组。`target` / `attachment` 也应预留 `pNext` 扩展位
  （fragment shading rate、fragment density map、device group、multiview per-view area 等），
  扩展结构由 frame 拥有，不能指向栈上的临时变量。

### 3.3 recorder：只记意图，不做 Vk*

```cpp
class recorder
{
  public:
    explicit recorder(context ctx) : ctx_(ctx) {}

    void begin_pass(target t, VkViewport vp,
                    std::span<const VkImageMemoryBarrier2> before = {})
    {
        assert(!in_pass_);
        in_pass_ = true;
        pass_ = pass{};
        pass_.target = std::move(t);
        pass_.viewport = vp;
        pass_.before.assign(before.begin(), before.end());
    }

    void begin_pass_no_target()
    {
        assert(!in_pass_);
        in_pass_ = true;
        pass_ = pass{};
        pass_.target.reset();
    }

    void end_pass(std::span<const VkImageMemoryBarrier2> after = {})
    {
        assert(in_pass_);
        pass_.after.assign(after.begin(), after.end());
        in_pass_ = false;
        frame_.stages.emplace_back(std::move(pass_));
    }

    void transition(std::span<const VkImageMemoryBarrier2> barriers)
    {
        assert(!in_pass_ && "transition 必须在 pass 外");
        if (!barriers.empty())
            frame_.stages.emplace_back(barrier_batch{std::vector<VkImageMemoryBarrier2>(
                barriers.begin(), barriers.end())});
    }

    void copy_image_to_buffer(copy_image_to_buffer c)
    {
        assert(!in_pass_ && "copy 必须在 pass 外");
        frame_.stages.emplace_back(std::move(c));
    }

    // draw 时把“当前状态 + 这条 draw 的数据”固化成 draw_item
    void draw(const draw_state &state, const PushData &push,
              std::uint32_t vertex_count = 3, std::uint32_t first_vertex = 0)
    {
        assert(in_pass_ && "draw 必须在 pass 内");
        pass_.body.push_back(draw_item{state, push, vertex_count, first_vertex});
    }

    frame finish() &&
    {
        assert(!in_pass_);
        return std::move(frame_);
    }

  private:
    context ctx_;
    frame frame_{};
    pass pass_{};
    bool in_pass_{false};
};
```

`recorder` 的边界断言就是“非法状态不可表示”：

- `begin_pass` 时不能已在 pass 内；
- `end_pass` 时必须在 pass 内；
- `transition` / `copy` 时不能在 pass 内；
- `draw` 时必须在 pass 内。

这些断言比注释有用；它们就是 `command` 层的 validation layer。

`group` 是可选扩展：内部再开一个 `recorder` 记录子序列（bind/set/push/draw），最后整体拼进 `body`。
它的存在是为了 path 宏这种“一次命令 = 多次 draw + 内部状态切换”的东西；
**group 是合批边界**，planner 不会跨 group 合并。`recorder` 的 `group` 实现略，语义先定死即可。

### 3.4 planner：决定“一次/多次”

```cpp
enum class call_kind
{
    begin_rendering, end_rendering,
    bind_pipeline, bind_set,
    set_viewport, set_scissor, set_stencil_ref,
    push_constants, draw,
    barrier, copy_image_to_buffer,
};

// 为了看清字段，这里用一个“胖 call”；实际可以用 std::variant 省空间。
struct call
{
    call_kind kind{};
    target target{};                     // begin_rendering
    VkPipeline pipeline{};               // bind_pipeline
    VkDescriptorSet set{};               // bind_set
    VkPipelineLayout layout{};           // bind_set / push_constants
    VkViewport viewport{};               // set_viewport
    VkRect2D scissor{};                  // set_scissor
    std::uint32_t stencil_ref{};         // set_stencil_ref
    PushData push{};                     // push_constants
    std::uint32_t vertex_count{};        // draw
    std::uint32_t first_vertex{};        // draw
    std::vector<VkImageMemoryBarrier2> barriers; // barrier
    copy_image_to_buffer copy{};         // copy_image_to_buffer
};

struct emitted
{
    VkPipeline pipeline{VK_NULL_HANDLE};
    VkDescriptorSet set{VK_NULL_HANDLE};
    VkRect2D scissor{};
    bool has_scissor{false};
    VkViewport viewport{};
    bool has_viewport{false};
    std::optional<std::uint32_t> stencil_ref;
};

const pipeline_info &info_of(const context &ctx, VkPipeline p);
bool has_dynamic(const pipeline_info &pi, VkDynamicState s);
bool pipeline_layout_accepts(const pipeline_info &pi, VkDescriptorSetLayout set_layout);

void plan_pass(const pass &p, const context &ctx, std::vector<call> &out)
{
    if (p.target)
        out.push_back(call{.kind = call_kind::begin_rendering, .target = *p.target});

    // 这里每个 pass 重置缓存：安全、简单、多绑几条。
    // 要跨 pass 复用，把 e 提到 frame 级，并保留下面的 static 失效逻辑。
    emitted e{};

    for (const draw_item &d : p.body)
    {
        const pipeline_info &pi = info_of(ctx, d.state.pipeline);

        // pipeline：变了才绑
        if (d.state.pipeline != e.pipeline)
        {
            out.push_back(call{.kind = call_kind::bind_pipeline,
                               .pipeline = d.state.pipeline});
            e.pipeline = d.state.pipeline;

            // 新 pipeline 的 static 状态会覆盖旧值：
            // 下一次遇到“把该状态声明为 dynamic”的 pipeline 时，必须重新 set。
            if (!has_dynamic(pi, VK_DYNAMIC_STATE_VIEWPORT))
                e.has_viewport = false;
            if (!has_dynamic(pi, VK_DYNAMIC_STATE_SCISSOR))
                e.has_scissor = false;
            if (!has_dynamic(pi, VK_DYNAMIC_STATE_STENCIL_REFERENCE))
                e.stencil_ref.reset();
        }

        // descriptor set：变了才绑。
        // layout 兼容性在这里校验；不兼容时调用方必须自己换一个 set，planner 不替你猜。
        if (d.state.set != e.set)
        {
            out.push_back(call{.kind = call_kind::bind_set,
                               .set = d.state.set,
                               .layout = pi.layout});
            e.set = d.state.set;
        }
        // 每条 draw 都校验：当前 pipeline layout 必须能访问这个 set
        assert(pipeline_layout_accepts(pi, d.state.set_layout));

        // viewport：每个 pass 一次；只对 dynamic viewport 的 pipeline 设
        if (p.target && has_dynamic(pi, VK_DYNAMIC_STATE_VIEWPORT) &&
            (!e.has_viewport || e.viewport != p.viewport))
        {
            out.push_back(call{.kind = call_kind::set_viewport, .viewport = p.viewport});
            e.viewport = p.viewport;
            e.has_viewport = true;
        }

        // scissor：变了才设；只对 dynamic scissor 的 pipeline 设
        if (has_dynamic(pi, VK_DYNAMIC_STATE_SCISSOR) &&
            (!e.has_scissor || e.scissor != d.state.scissor))
        {
            out.push_back(call{.kind = call_kind::set_scissor,
                               .scissor = d.state.scissor});
            e.scissor = d.state.scissor;
            e.has_scissor = true;
        }

        // stencil ref：有值、且 pipeline 声明了 dynamic stencil ref，才设
        if (d.state.stencil_ref &&
            has_dynamic(pi, VK_DYNAMIC_STATE_STENCIL_REFERENCE) &&
            d.state.stencil_ref != e.stencil_ref)
        {
            out.push_back(call{.kind = call_kind::set_stencil_ref,
                               .stencil_ref = *d.state.stencil_ref});
            e.stencil_ref = d.state.stencil_ref;
        }
        // 如果 pipeline 没有 dynamic stencil ref，却要求一个非零 ref，这是设计错误
        assert(!d.state.stencil_ref ||
               has_dynamic(pi, VK_DYNAMIC_STATE_STENCIL_REFERENCE));

        // push constants：每条 draw 都要（它是 data，不是 state）
        out.push_back(call{.kind = call_kind::push_constants,
                           .layout = pi.layout,
                           .push = d.push});
        // draw：每条都要
        out.push_back(call{.kind = call_kind::draw,
                           .vertex_count = d.vertex_count,
                           .first_vertex = d.first_vertex});
    }

    if (p.target)
        out.push_back(call{.kind = call_kind::end_rendering});
}
```

这段代码就是“一次/多次”的具体化：

- `begin/end`：一个 pass 一次；
- `bind_pipeline`：一次 pipeline run 一次；
- `bind_set`：一次 set 变化一次；
- `set_viewport`：每个 pass 第一次遇到 dynamic viewport 的 pipeline 时一次；
- `set_scissor`：一次变化一次；
- `set_stencil_ref`：一次变化一次；
- `push_constants`：**每条 draw 一次**（因为它是 data）；
- `draw`：**每条 draw 一次**。

**关于 per-pass reset：** 上面为了简单，每个 pass 把 `emitted e{}` 重置，所以 pipeline/DS 也会在每个 pass 重绑一次。
这是安全但多绑的版本；ability3 只有 ~11 个 pass，多绑 ~44 条命令，相对主 pass 的上千条可以忽略。
如果要跨 pass 复用，把 `e` 提到 `frame` 级：
- pipeline/DS 可以保留（layout 兼容时）；
- viewport/scissor 在 target 改变时必须重设；
- 遇到 static 状态仍要用上面的 `has_viewport/has_scissor/stencil_ref.reset()` 逻辑失效。
不要为了省几条 bind 把 static/dynamic 的时序搞错；先 per-pass reset，profile 之后再优化。

如果你想让 `push_constants` 也变成“一次变化一次”，只有一条路：
把 per-draw 数据搬到 buffer，push 只放 buffer 地址 + 本次 draw 的参数（见第 5 节）。

### 3.5 encoder：只做 1:1 发射

`plan_frame` 负责把 `frame` 展开成 `call` 序列，并且把 `before/after` 放在正确的位置：

```cpp
std::vector<call> plan_frame(const frame &f, const context &ctx)
{
    std::vector<call> out;
    for (const stage &s : f.stages)
    {
        if (const auto *p = std::get_if<pass>(&s))
        {
            for (const auto &b : p->before)          // begin 之前
                out.push_back(call{.kind = call_kind::barrier,
                                   .barriers = {b}});
            plan_pass(*p, ctx, out);                 // begin → body → end
            for (const auto &b : p->after)           // end 之后
                out.push_back(call{.kind = call_kind::barrier,
                                   .barriers = {b}});
        }
        else if (const auto *b = std::get_if<barrier_batch>(&s))
            out.push_back(call{.kind = call_kind::barrier,
                               .barriers = b->barriers});
        else if (const auto *c = std::get_if<copy_image_to_buffer>(&s))
            out.push_back(call{.kind = call_kind::copy_image_to_buffer,
                               .copy = *c});
    }
    return out;
}

class encoder
{
  public:
    explicit encoder(CommandBufferView cmd) : cmd_(cmd) {}

    void encode(const std::vector<call> &plan)
    {
        for (const call &c : plan)
        {
            switch (c.kind)
            {
            case call_kind::begin_rendering: {
                // target 的值 → 栈上的 VkRenderingInfo + 附件数组；
                // 指针只在 beginRendering 调用期间有效。
                auto attachments = make_attachment_array(c.target);
                VkRenderingInfo info = make_rendering_info(c.target, attachments);
                cmd_.beginRendering(info);
                break;
            }
            case call_kind::bind_pipeline:
                cmd_.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, c.pipeline);
                break;
            case call_kind::bind_set:
                cmd_.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        c.layout, 0, 1, &c.set, 0, nullptr);
                break;
            case call_kind::set_viewport:
                cmd_.setViewport(0, std::span{&c.viewport, 1});
                break;
            case call_kind::set_scissor:
                cmd_.setScissor(0, std::span{&c.scissor, 1});
                break;
            case call_kind::set_stencil_ref:
                cmd_.setStencilReference(VK_STENCIL_FACE_FRONT_AND_BACK,
                                         c.stencil_ref);
                break;
            case call_kind::push_constants:
                cmd_.pushConstants(c.layout,
                                   VK_SHADER_STAGE_VERTEX_BIT |
                                       VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(PushData), &c.push);
                break;
            case call_kind::draw:
                cmd_.draw(c.vertex_count, 1, c.first_vertex, 0);
                break;
            case call_kind::barrier:
                cmd_.pipelineBarrier2(dependency(c.barriers));
                break;
            case call_kind::copy_image_to_buffer:
                cmd_.copyImageToBuffer(c.copy.image, c.copy.layout, c.copy.buffer,
                                       c.copy.regions);
                break;
            case call_kind::end_rendering:
                cmd_.endRendering();
                break;
            }
        }
    }

  private:
    CommandBufferView cmd_;
};
```

encoder 里**不应该**再出现“这个 bind 能不能省”的判断。
如果 encoder 也开始做策略，`c_api_list` 就会和实际发射分叉。

### 3.6 `c_api_list` 和 validation

`c_api_list` 不要从 `frame` 直接猜；它应该和 encoder 读同一份 `plan`：

```cpp
std::vector<std::string> c_api_list(const std::vector<call> &plan);
```

调试时再加一道保险：

```cpp
#ifdef DEBUG
// encoder 每发一条 vkCmd*，往 actual 列表里 push 名字；
// 最后 assert(actual == c_api_list(plan));
#endif
```

validation 至少要有：

- 每个 `draw_item` 的 `pipeline_info` 与 `target` 的格式/采样/viewMask 匹配；
- 如果 pipeline 把 viewport/scissor 声明为 static，它的静态值必须与 `pass.viewport` / target 匹配；
- `push_size >= sizeof(PushData)`，且 stage flags 覆盖 VS+FS；
- descriptor set 的 layout 与 pipeline layout 兼容；
- 声明了 dynamic viewport/scissor 的 pipeline，draw 前必须有对应的 set；
- `transition` 的 `oldLayout` 与当前跟踪的 layout 一致（需要 image layout 表）；
- 没有 pass 嵌套、没有 draw 在 pass 外、没有 transition/copy 在 pass 内；
- `before/after` 的 barrier 不改变 scope 内附件的 layout。

---

## 4. 映射到 ability3 的现有代码

### 4.1 现有代码 → 新结构

| 现有代码 | 位置 | 新结构 |
|---|---|---|
| `my_render::image_info` / `transition_image_layout` | 170–233 | `barrier_batch` / `pass.before/after` |
| `command::rendering_info` | 267–356 | `command::target` |
| `command::rendering_scope_build` | 358–377 | `command::pass`（加 body） |
| `command::command_pass` | 379–390 | `command::pass`（before/body/after） |
| `command::command_recorder` | 392–400 | `command::recorder` + `planner` |
| `layerBegin` / `layerCmd` | 11640–11683 | `pass{target=layer, body=1..3 draw_item}` |
| 主 pass 的 `beginRendering` | 11779–11800 | `pass{target=main, viewport=W×H}` |
| 逐条录制循环 | 13012–13057 | `recorder.draw(state, push)` |
| `drawPathCommand` | 12224–12327 | `group`（内部有 pipeline/stencil 切换，打断合批） |
| glass 第二段 pass | 13211–13260 | `pass{target=main, load=LOAD}` + `before` barrier |
| picking copy | 13085–13092 | `copy_image_to_buffer` stage |
| present transition | 13313–13330 | `barrier_batch` stage |
| `DrawRecorder`（旧的合批机器） | 1229–1669 | 保留为“数据模型”的参考；`command` 层不替代它 |

### 4.2 layer pass 的写法

```cpp
auto layer_pass = [&](const ui_layer_ctx::layer &layer, auto &&body) {
    command::target t{};
    t.area = {{0, 0}, ui_layer_ctx::kExtent};
    t.layer_count = 1;
    t.colors.push_back(command::attachment{
        .view = layer.view,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .load = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .store = VK_ATTACHMENT_STORE_OP_STORE,
        .clear = {.color = {.float32 = {0, 0, 0, 0}}},
    });

    rec.begin_pass(std::move(t), VkViewport{0, 0, 320, 192, 0, 1});
    body(rec); // 1..3 个 draw
    rec.end_pass();
};
```

调用方只需要给 `target` 和 body；`begin/end` 的配对由 `recorder` 保证。

### 4.3 主 pass 的写法

```cpp
// 把现有的 VkRenderingAttachmentInfo 转成 command::attachment（值语义）
auto to_cmd = [](const VkRenderingAttachmentInfo &a) {
    return command::attachment{
        .view = a.imageView,
        .layout = a.imageLayout,
        .resolve_mode = a.resolveMode,
        .resolve_view = a.resolveImageView,
        .resolve_layout = a.resolveImageLayout,
        .load = a.loadOp,
        .store = a.storeOp,
        .clear = a.clearValue,
    };
};

command::target main_target{};
main_target.area = {{0, 0}, extent};
main_target.colors = {to_cmd(colorAttachment), to_cmd(pickAttachment)};
main_target.depth = to_cmd(depthAttachment);
if (has_stencil)
    main_target.stencil = to_cmd(depthAttachment);

rec.begin_pass(main_target, viewport);   // 按值传入，main_target 之后还要给 glass pass 复用

for (uint32_t i = 0; i < cmdCount; ++i)
{
    const ui_cmd &c = cmds[i];
    if (c.pathKind != 0)
    {
        // group 的 API 略：内部是一串 bind/set/push/draw，不参与合批
        rec.group([&](command::recorder &r) { draw_path(r, c); });
        continue;
    }

    command::draw_state st{};
    st.pipeline = (c.pipe == 0) ? *mainCtx.pipelineTransparentUI
                                : *(*uiPipelines().pipes[c.pipe]);
    st.set = descriptorSet;
    st.set_layout = *descriptorSetLayout;
    st.scissor = c.scissor;
    // pipe 0 的 pipeline 没有 VK_DYNAMIC_STATE_STENCIL_REFERENCE；不能给它 set stencil ref。
    st.stencil_ref = (c.pipe == 0)
                         ? std::optional<std::uint32_t>{}
                         : std::optional<std::uint32_t>{c.ref};

    PushData pc = make_push(c);
    rec.draw(st, pc);
}

rec.end_pass();
```

注意：`draw_path` 里会改 pipeline/stencil/push，所以它是一个 `group`，**不能**和相邻的普通 draw 合并。
如果以后想让 path 也进合批，需要把 stencil 操作搬到数据里（比如用 stencil value 编码），那是另一个数据模型问题。

### 4.4 glass pass 的写法

```cpp
// 第二段 pass：LOAD，保留已有画面；barrier 作为 pass.before，在 begin 之前发
command::target glass_target = main_target;
glass_target.colors[0].load = VK_ATTACHMENT_LOAD_OP_LOAD;
glass_target.colors[1].load = VK_ATTACHMENT_LOAD_OP_LOAD;
glass_target.depth->load = VK_ATTACHMENT_LOAD_OP_LOAD;

rec.begin_pass(std::move(glass_target), viewport,
               std::span{&image_to_sampled_or_transfer_barrier, 1});
rec.draw(glass_state_1, glass_push_1);
rec.draw(glass_state_2, glass_push_2);
rec.end_pass();
```

**关键：** `glass_target` 的附件和主 pass 相同，但 `loadOp` 不同；它仍然是一个独立的 `pass`。
两个 pass 之间必须有一条 barrier（`pass.before`），不能假设“同一个 target 自动保序”。

### 4.5 copy / present

```cpp
rec.copy_image_to_buffer(command::copy_image_to_buffer{
    .image = resolveImage,
    .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    .buffer = pickingFrames[currentFrame].buffer(),
    .regions = {{...}},
});

rec.transition(std::span{&swapchain_to_present_barrier, 1});
```

它们都在 pass 外，正好符合“copy 只能在 render scope 外”的边界。

---

## 5. 合批：能合什么，不能合什么

### 5.1 primitive order 是合批的法律

规范 27.3 的顺序：

```text
submission order
  > indirect buffer 地址从低到高
    > instance 编号从低到高
      > vertex/index 顺序
```

所以：

- **相邻 draw 合并成一条 instanced draw**：合法，instance i = 原第 i 个 draw，顺序保持。
- **相邻 draw 合并成一条 indirect draw**：合法，indirect command 按 buffer 顺序执行。
- **全局按 pipeline 排序**：只有 `ordering == independent` 的 pass 合法。
  UI 是 painter's algorithm，不能排序；不透明 3D 可以。

### 5.2 合批的 key

```cpp
struct batch_key
{
    VkPipeline pipeline;
    VkDescriptorSet set;
    VkRect2D scissor;
    std::uint32_t stencil_ref;
    std::uint32_t vertex_count;
    std::uint32_t first_vertex;
    auto operator<=>(const batch_key &) const = default;
};
```

只有 `batch_key` 相同的**相邻** `draw_item` 才能合并。
`push` 不在 key 里，因为它不是“状态”，是“每条 draw 的数据”；
要合并，必须把 `push` 变成 buffer 里的 per-instance 数据。

> 如果走 `drawIndexedIndirect`，`vertex_count/first_vertex` 可以移出 key，由 indirect command 提供；
> 这时 key 只剩 `pipeline + set + scissor + stencil_ref`。代价是每条 command 还要占 20 B 的 indirect buffer。
> 对 UI 来说，先用 instanced draw 更简单；draw 参数差异大时再上 indirect。

### 5.3 合批的四个杀手

| 杀手 | 为什么 | 怎么破 |
|---|---|---|
| **push constants** | 每条 draw 不同，必须每条 draw 一次 `vkCmdPushConstants` | 搬到 storage buffer，用 `gl_InstanceIndex` / `gl_DrawID` 取；push 只留 buffer 地址 |
| **scissor** | 每条 draw 不同，必须每条 draw 一次 `vkCmdSetScissor` | 让 scissor 表示“clip scope”，不是“形状 box”；形状裁剪已经在 `uiRect`/SDF 里 |
| **pipeline** | 不同 blend/stencil/color write 是不同 pipeline | 减少 pipeline 排列（dynamic blend state），或只在相邻同 pipeline 内合 |
| **stencil / path 宏** | 内部有 pipeline/stencil/push 切换 | 把它当 batch 边界；不要跨 group 合并 |

### 5.4 ability3 的具体瓶颈

`add()` 里（11835–11838 行）：

```cpp
VkRect2D sc = (scissor.extent.width == 0u || scissor.extent.height == 0u)
                  ? box
                  : scissor;
```

默认 scissor = 形状 box。大多数命令的 box 不同，所以大多数命令的 scissor 不同。
这意味着：

- 按 `pipeline + scissor` 合批：几乎合不了；
- 按 `pipeline` 合批：可以省 pipeline bind，但每条 draw 前的 `setScissor + pushConstants + draw` 还在；
- 主 pass 的 draw 数不会因为命令层优化而减少。

要真正合批，必须做数据模型改动：

1. **scissor 改成 clip scope**（比如面板的裁剪框，或全屏），形状裁剪交给 shader 的 `uiRect`/SDF。
   这样很多命令共享同一个 scissor，`batch_key` 才有机会相等。
2. **push 改成 buffer**：`PushData` 的 `uiRect/uiColor/uiParams/uiAux/uiPick` 搬到 per-instance buffer，
   push 只留 `buffer address + base offset`；shader 用 `gl_InstanceIndex` 取。
   你的 `PushData` 已经有 `commandConstantsAddress` 字段，`DrawRecorder` 已经在用这条路径；
   主 UI 路径只是还没切过去。

这两步做完，`command` 层的合批才有意义；否则它只能省掉重复的 pipeline/descriptor bind，
省不了 draw 数。

### 5.5 ability2 / intent8 已经证明了什么

- **ability2**：`Recorder → compile → FramePlan(passes + batches) → Backend`，
  合批条件是“同管线 + 同 scissor”，注释里写的是 326 draw → 224 命令（`test_ui_ability2.cpp` 12516 行附近）。
  它证明：**合批是 compile 层的事，不是 backend 层的事**。
- **intent8**：`DrawCall → OpStream → VkEmitter`，
  `FrameRecorder::draw` 每条 draw 都展开 bind pipeline + bind DS + viewport + scissor + push + draw
  （`test_intent8_draw_recorder.hpp` 130–248 行）。
  它证明的是“语义展开”的极限：**不丢信息，但没有去重**。
  下一步正好是 `planner`：把重复的 bind 去掉，把相邻同 key 的 draw 合并。

ability3 的 `namespace command` 应该站在两者之间：

- 比 intent8 高一层：有 `pass/target`，知道 scope 边界；
- 比 ability2 低一层：只做 Vulkan 命令，不做 UI 语义（shading/clip/layer 由上层决定）。

---

## 6. 落地顺序（建议）

### 第 1 步：数据 + encoder，先接 layer pass

- 定义 `target` / `pass` / `frame` / `draw_item`；
- `recorder` 只支持 `begin_pass/end_pass/draw/transition`；
- 把 A/B/C/D 层（11687–11747）改成 `pass`；
- 用 PPM dump（13263–13310）对比改造前后像素，必须完全一致。

验收：layer pass 的 `begin/end` 配对、layout transition 位置、viewport/scissor 都正确；无 VUID。

### 第 2 步：接主 pass

- 把 13012–13057 的逐条循环改成 `recorder.draw(state, push)`；
- `planner` 做 pipeline/DS/scissor/stencil 的去重；
- `drawPathCommand` 先包成 `group`（不参与合批）；
- 再次 PPM 对比。

验收：pipeline bind 次数从“每条命令”降到“每个 pipeline run 一次”；draw 数不变；像素一致。

### 第 3 步：接 glass pass + copy + present

- glass 第二段 pass 用 `LOAD`；
- 两个 pass 之间加显式 barrier；
- picking copy / frame dump / present transition 放进 `frame.stages`。

验收：glass 之前的 barrier 存在；copy 在 pass 外；present layout 正确；无 VUID。

### 第 4 步：`c_api_list` + validation

- `planner` 输出 `std::vector<call>`；
- encoder 1:1 发射；
- debug 下比较 `actual == c_api_list(plan)`；
- 加 3.6 的断言。

验收：任何“draw 在 pass 外 / transition 在 pass 内 / 少 set viewport”都会在录制期 assert，而不是等 validation layer。

### 第 5 步：合批（先做不改变数据模型的）

- 相邻同 `batch_key` 的 draw 合并成 instanced draw；
- 只对 `ordering == independent` 的 pass 排序；
- 测量：draw 数、bind 数、CPU 录制时间。

预期：bind 数下降；draw 数只在“相邻同 key”时下降。

### 第 6 步：数据模型改造（真正的合批）

- push → per-instance buffer；
- scissor → clip scope；
- 再测一次。

**注意：这一步会改 shader ABI。** ability3 现在复用 ability1 的 `test_ui_ability.vert/frag`
（`vma.cmake` 231 行），`PushData` 的 128 B 布局是两边共用的；
所以要么加新的 shader 变体，要么先把 `PushData` 的空闲字段用起来。不要在第 1 步就动它。

预期：draw 数明显下降；这一步的收益远大于第 5 步。

### 第 7 步：secondary / suspend-resume

只有确实需要并行录制时再做；ability3 现在是单 command buffer，不需要。

---

## 7. 反模式清单

1. **把 `vkCmdBeginRendering` 当状态重置**：以为每个 pass 开始时状态一定是空的，或者以为“必须重绑”。
   正确：state 跨 pass 存活；只有 `vkBeginCommandBuffer` 重置。
   简化做法：pass 开头重绑 viewport/scissor 可以，但要知道这是“多绑”而不是“必须”；
   pipeline/DS 也可以保留，只要 layout 兼容。

2. **draw 里顺手 bind**：`recorder.draw` 内部调 `vkCmdBindPipeline`。
   正确：draw 只记录意图；planner 决定 bind 几次。

3. **transition 塞进 pass**：为了“少写一个 pass”，在 begin 后做 layout transition。
   正确：transition 在 `pass.before/after`；scope 内不做 layout transition。

4. **copy/blit 塞进 pass**：copy 的 render pass scope 是 Outside。
   正确：copy 是独立的 `stage`。

5. **每条 draw 重绑 descriptor set**：set 没变就不要绑。
   正确：planner 按 `(layout, firstSet, set)` 去重。

6. **每条 draw 重绑同一条 pipeline**：intent8 的 `FrameRecorder::draw` 现在就是这样。
   正确：planner 按 pipeline run 去重。

7. **对半透明 UI 按 pipeline 全局排序**：破坏 painter's order。
   正确：只做相邻合并；`ordering == painter` 禁止排序。

8. **每条 draw 用不同的 push，然后指望合批**：push 是 data，不是 state；它天然打断合批。
   正确：push → buffer，用 instance/draw id 取。

9. **scissor = 形状 box**：每条命令一个 scissor，合批无从谈起。
   正确：scissor = clip scope；形状裁剪交给 shader。

10. **存 `VkRenderingInfo` 到 vector 里，再 push_back 附件**：指针悬空。
    正确：`target` 存值，encode 时生成指针视图。

11. **classic render pass 和 dynamic rendering 混用**：pipeline 的 `renderPass` 不兼容。
    正确：一个 target 只用一种；ability3 只 dynamic。

12. **`pNext` 指向临时结构**：`VkRenderingInfo::pNext` 的生命周期必须覆盖 `vkCmdBeginRendering` 调用。
    正确：扩展结构放在 encode 的栈上，或由 target 拥有。

---

## 8. 一页纸

```text
一次：
  command buffer begin/end        每个录制一次
  begin/end rendering             每个 scope 一次
  bind pipeline                   每个 pipeline run 一次
  bind descriptor set             每个 (layout,set) 变化一次
  set viewport                    每个 target 一次
  set scissor/stencil ref         每个值变化一次
  transition/barrier              每条依赖一次

多次：
  draw / dispatch / copy          每个效果一次
  push constants                  每条 draw 一次（除非搬进 buffer）
  begin/end query                 每个 query 一次

不能：
  嵌套 begin/end rendering
  在 scope 内 copy/blit
  在 scope 内做 layout transition
  跨 pass 依赖隐式顺序
  对 painter's order 的 pass 全局排序
  用 push/scissor 装 per-draw 数据还指望合批

合批：
  只合并相邻 + batch_key 相同
  push → buffer
  scissor → clip
  pipeline → 减少排列
  path/stencil 宏 → batch 边界
```

---

## 9. 参考

### 本地规范和指南

- [Vulkan 1.4 spec: Dynamic State](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#pipelines-dynamic-state)
- [Vulkan 1.4 spec: Dynamic State Lifetime](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#dynamic-state-lifetime)
- [Vulkan 1.4 spec: Pipeline Layout Compatibility](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#descriptors-compatibility)
- [Vulkan 1.4 spec: vkCmdPushConstants](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#vkCmdPushConstants)
- [Vulkan 1.4 spec: vkCmdBeginRendering](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#vkCmdBeginRendering)
- [Vulkan 1.4 spec: vkCmdEndRendering](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#vkCmdEndRendering)
- [Vulkan 1.4 spec: Render Pass Suspension](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#renderpass-suspension)
- [Vulkan 1.4 spec: Primitive Order](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#drawing-primitive-order)
- [Vulkan 1.4 spec: Rasterization Order](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#primsrast-order)
- [Vulkan 1.4 spec: vkCmdPipelineBarrier2](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#vkCmdPipelineBarrier2)
- [Vulkan 1.4 spec: vkCmdExecuteCommands](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#vkCmdExecuteCommands)
- [Vulkan 1.4 spec: VkCommandBufferInheritanceRenderingInfo](E:/mysoftware/VulkanSDK/1.4.341.0/vkspec.html#VkCommandBufferInheritanceRenderingInfo)
- [LunarG Guide: Pipeline Dynamic State](https://vulkan.lunarg.com/doc/view/1.4.341.0/windows/antora/guide/latest/dynamic_state.html)
- [LunarG Guide: Push Constants](https://vulkan.lunarg.com/doc/view/1.4.341.0/windows/antora/guide/latest/push_constants.html)

### 工作区里的材料

- `test_ui_ability3.cpp`：`namespace command`（235–402）、主 pass（11514–13335）
- `test_ui_ability2.cpp`：`ui::FramePlan` / `Batch` / `Backend`（2570–2700、12440–12537）
- `test_intent8_draw_ops.hpp`：`OpStream`（189–204）
- `test_intent8_draw_recorder.hpp`：`FrameRecorder::draw` 的逐 draw 展开（130–248）
- `vk_command_buffer_data_algo.md`：“状态是肥的，指令是瘦的”
- `vulkan_command_buffer_record.md`：命令缓冲生命周期与录制序列
