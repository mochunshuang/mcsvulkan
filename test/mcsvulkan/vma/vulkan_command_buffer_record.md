# VkCommandBuffer 录制模块 · 一页纸

> 只覆盖 `chapters/cmdbuffers.html` 的范畴：命令池、命令缓冲、录制、生命周期、主/次缓冲。
> 提交（`vkQueueSubmit2`）与呈现只作为边界出现，不展开。
> 配图：`vulkan_command_buffer_map.svg`

---

## 0. 一句话

> **录制 = 在 CPU 上把一串 `vkCmd*` 写进 `VkCommandBuffer` 的内存，并把它的状态从 `Initial` 推到 `Executable`。**
> 此时 GPU 什么都没做；`vkCmdDraw` 不画任何像素。

---

## 1. 五态生命周期

```
              vkBeginCommandBuffer          vkEndCommandBuffer
                      |                            |
                      v                            v
+-----------+   +-----------+   +------------+   +-----------+
|  Initial  |-->| Recording |-->| Executable |-->|  Pending  |
+-----------+   +-----------+   +------------+   +-----------+
      ^               |                |                |
      |               | reset          | reset          | 执行完成
      |               +----------------+                +----------> Executable
      |               |                                 |
      +---------------+---------------------------------+--(ONE_TIME_SUBMIT)--> Invalid
```

| 状态 | 允许做什么 | 禁止什么 |
|---|---|---|
| `Initial` | `vkBeginCommandBuffer`、`vkFreeCommandBuffers` | 录制 |
| `Recording` | 任意 `vkCmd*` | 提交 |
| `Executable` | `vkQueueSubmit2`、reset、被 `vkCmdExecuteCommands` 调用 | 继续录 |
| `Pending` | 只能等 | **任何修改 / reset / free** |
| `Invalid` | reset、free | 提交 |

关键句：**`Pending` 期间禁止对命令缓冲做任何操作**——包括 `vkResetCommandBuffer`。判断依据只能是 fence / semaphore，不能靠“我觉得画完了”。

---

## 2. 标准录制序列

你给的那条链是对的，补上两处更容易踩坑的细节：

```
vkBeginCommandBuffer(VkCommandBufferBeginInfo)
  |
  +-- [可选] vkCmdPipelineBarrier2(VkDependencyInfo)      // UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL
  |
vkCmdBeginRenderPass(VkRenderPassBeginInfo, VkSubpassContents)
  |
  +-- vkCmdBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, VkPipeline)
  |
  +-- [可选] vkCmdBindDescriptorSets / vkCmdPushConstants  // 着色器要用的资源
  +-- [可选] vkCmdBindVertexBuffers / vkCmdBindIndexBuffer
  |
  +-- vkCmdDraw / vkCmdDrawIndexed / vkCmdDrawIndirect
  |
  +-- [可选] vkCmdNextSubpass(...)  然后重复 bind + draw
  |
vkCmdEndRenderPass()
  |
vkEndCommandBuffer()          // -> Executable
```

### 每一步的约束

| 调用 | 关键参数 | 必须成立 |
|---|---|---|
| `vkBeginCommandBuffer` | `VkCommandBufferBeginInfo` | secondary 必须带 `pInheritanceInfo`；`ONE_TIME_SUBMIT` 与 `SIMULTANEOUS_USE` 不能同时置位；开始时全部状态 undefined |
| `vkCmdPipelineBarrier2` | `VkDependencyInfo` | 图像布局必须与后续 usage 匹配；`oldLayout` 必须等于当前布局 |
| `vkCmdBeginRenderPass` | `VkRenderPassBeginInfo` | `framebuffer` 必须与 `renderPass` 兼容；`renderArea` 必须在 framebuffer 内 |
| `vkCmdBindPipeline` | `VkPipeline` | 管线创建时的 `renderPass` / `subpass` 必须与当前 pass 实例匹配（管线用 `renderPass = VK_NULL_HANDLE` 创建时例外，走动态渲染） |
| `vkCmdBindDescriptorSets` | `VkPipelineLayout` | 传入的 layout 必须与管线创建时用的 layout 一致 |
| `vkCmdDraw` | 顶点/实例数 | 当前必须有兼容的 pipeline；顶点/索引缓冲绑定完整 |

> `vkCmdBindPipeline` 本身**不要求**在 render pass 内——只要在 `vkCmdDraw` 前绑好即可。但管线与 pass 的兼容性约束仍然成立。

---

## 3. 主缓冲 / 次缓冲

```
PRIMARY   : 可提交给 VkQueue；可在 pass 内 vkCmdExecuteCommands(SECONDARY)
SECONDARY : 不可直接提交；只能被 PRIMARY 执行
```

- **不继承状态**：secondary 不继承 primary 的任何绑定（除 render pass 实例内的 pass 状态）。
- `VkCommandBufferInheritanceInfo` 只声明“我将要在哪个 renderPass / subpass 里跑”，用于驱动校验与优化，不是状态传递。
- secondary 要在 pass 内执行，必须用 `VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT` 开始录制。
- `vkCmdExecuteCommands` 之后，primary 的状态变为 undefined（render pass 实例内的 pass 状态除外），需要重新 bind。

---

## 4. 并行录制

```
thread 0: VkCommandPool(0) -> VkCommandBuffer(0, SECONDARY) --.
thread 1: VkCommandPool(1) -> VkCommandBuffer(1, SECONDARY) --+--> PRIMARY vkCmdExecuteCommands
thread 2: VkCommandPool(2) -> VkCommandBuffer(2, SECONDARY) --'
```

- 一个 `VkCommandPool` 同一时刻只能被一个线程使用（外部同步），所以**每线程一个池**。
- 池绑定单一 `queueFamilyIndex`；池里的命令缓冲只能提交到该 family 的队列。
- 这是 Vulkan 里最稳的并行录制方式：录制完全在 CPU 侧，互不干扰，最后合并。

---

## 5. 与“提交”的边界

```
录制:  vkBeginCommandBuffer ... vkEndCommandBuffer      -> Executable
提交:  vkQueueSubmit2(VkSubmitInfo2, VkFence)           -> Pending
```

- 录制阶段**不做**任何 GPU 同步；`VkSemaphore` / `VkFence` 只在提交时出现。
- `vkCmdPipelineBarrier2` 是“录制进命令流”的同步指令，它的效果发生在 GPU 执行到那一条时。
- `vkCmdBeginQuery` / `vkCmdWriteTimestamp2` 同理：写进流，执行时才有值。

---

## 6. 检查清单

| # | 检查项 |
|---|---|
| 1 | `Pending` 态是否被 reset / 重录？（只能等 fence） |
| 2 | 命令池的 `queueFamilyIndex` 与提交队列是否一致？ |
| 3 | `vkBeginCommandBuffer` 后是否把所有需要的状态都重新绑定了一遍？ |
| 4 | secondary 是否提供了 `VkCommandBufferInheritanceInfo`？ |
| 5 | 管线创建时的 `renderPass` / `subpass` 是否与录制时一致？ |
| 6 | `VkFramebuffer` 的附件是否与 `VkRenderPass` 兼容？ |
| 7 | `ONE_TIME_SUBMIT` 的缓冲提交后是否当作 `Invalid` 处理？ |
| 8 | `vkCmdExecuteCommands` 之后是否重新 bind 了管线/描述符？ |

---

## 7. 与另外两个模块的接缝

```
                  +-----------------------+
   资源/描述符 --> |  vkCmdBind* 录制      | <-- 管线
                  +-----------+-----------+
                              |
                     VkCommandBuffer
                              |
                  +-----------v-----------+
                  |  vkQueueSubmit2       |  <-- 同步原语在这里出现
                  +-----------------------+
```

录制模块只认三样东西：**命令池（内存来源）**、**管线/描述符/缓冲（被绑定物）**、**render pass 或渲染信息（作用域）**。
除此之外的一切（semaphore、fence、swapchain）都属于提交与呈现模块。
