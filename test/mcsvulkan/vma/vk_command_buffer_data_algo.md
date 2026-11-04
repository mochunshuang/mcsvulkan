# 从 API 反推：命令缓冲的「数据 + 算法」

> 你给的那条链是 **API 视图**。把它拆成 **数据** 与 **算法**，需要先问一个问题：
> 这些调用里，哪些是在改“内存里的东西”，哪些是在“做判断并写指令”？
> 本文只做这一件事：把 `VkCommandBuffer` 还原成一个可实现的 `数据结构 + 状态机 + 编码器`。

---

## 0. 结论先行

```text
                    ┌──────────────── 数据 ────────────────┐
                    │ ① 句柄表     资源引用（只读）          │
                    │ ② 对象头     生命周期 / 级别 / 池      │
                    │ ③ 状态寄存器 当前绑定与动态状态        │
                    │ ④ 指令流     opcode + 操作数（只追加） │
                    │ ⑤ 元数据     flags / inheritance / 兼容性│
                    └──────────────────┬───────────────────┘
                                       │  被读 / 被写
                    ┌──────────────────┴───────────────────┐
                    │ ① 生命周期转移  Initial→…→Invalid      │
                    │ ② 状态写入      bind / set             │
                    │ ③ 指令发射      emit(opcode, payload)  │
                    │ ④ 前置校验      assert(不变量)         │
                    │ ⑤ 次级合并      execute / splice       │
                    │ ⑥ 重置回收      reset / free           │
                    └──────────────────────────────────────┘
```

一句话的核心：

> **Vulkan 的绘制指令是“瘦”的，状态是“肥”的。**
> `vkCmdDraw(vertexCount, instanceCount, firstVertex, firstInstance)` 只带 4 个整数；
> 管线、描述符、顶点缓冲全在“状态寄存器”里，指令执行时按寄存器解释。
> 所以：**数据 = 寄存器 + 指令流；算法 = 状态机 + 校验 + 发射。**

---

## 1. 数据：五个平面

### 1.1 句柄表（外部资源，只读引用）

命令缓冲**不拥有**这些对象，只存句柄/索引；它们的生命周期由 device 管理。

| 类别 | 句柄 | 被谁用到 |
|---|---|---|
| 管线 | `VkPipeline`、`VkPipelineLayout` | bind / draw / dispatch |
| 描述符 | `VkDescriptorSet` | bind |
| 几何 | `VkBuffer`（VB/IB/indirect） | bind / draw |
| 渲染目标 | `VkRenderPass`、`VkFramebuffer`、`VkImageView` | begin/end render pass |
| 查询 | `VkQueryPool` | begin/end query、timestamp |
| 同步 | `VkEvent` | set/wait event |
| 次级 | `VkCommandBuffer`（SECONDARY） | execute commands |

数据建模上，它们就是 `uint64_t` 句柄表 + 一张 `handle → 元数据` 的旁表（bind point、layout、兼容性、格式），**录制期只读**。

> 两种句柄：`VK_DEFINE_HANDLE`（dispatchable：`VkDevice`、`VkQueue`、`VkCommandBuffer`…）和
> `VK_DEFINE_NON_DISPATCHABLE_HANDLE`（`VkPipeline`、`VkBuffer`、`VkDescriptorSet`…）。
> 两者都是引用，对象归 device 所有；区别只在要不要第一个参数传 dispatchable。
>
> 注意**句柄表 ≠ 状态寄存器**：句柄表是“类型清单”（它是什么、谁拥有），
> 状态寄存器里的 `st.ds.sets[]` 是“绑定槽”（**现在绑的是哪个句柄**）。
> 同一个 `VkDescriptorSet` 同时出现在两处，问的问题不同：一个问“是什么”，一个问“现在绑的谁”。

### 1.2 对象头（命令缓冲自身）

| 字段 | 类型 | 作用 |
|---|---|---|
| `state` | 枚举 5 态 | 生命周期算法唯一依据 |
| `level` | PRIMARY / SECONDARY | 决定能否被提交 / 能否 execute |
| `pool` | `VkCommandPool` | 内存来源 + queue family 绑定 |
| `queue_family` | uint32 | 提交合法性校验 |
| `flags` | ONE_TIME_SUBMIT / SIMULTANEOUS_USE / RENDER_PASS_CONTINUE | 影响完成态与复用 |
| `inheritance` | `VkCommandBufferInheritanceInfo` | secondary 的兼容性声明 |
| `stream` | 可增长数组 | 指令流 |
| `st` | 状态寄存器 | 见 1.3 |

### 1.3 状态寄存器（录制期可变，begin 时清空）

这就是“肥”的部分。它同时服务两个目的：**录制期校验** 与 **执行期解释**。

```c
typedef struct {
    VkPipeline      pipeline[2];      // GRAPHICS / COMPUTE 各一个
    struct {                          // 描述符绑定
        uint32_t        first_set;
        uint32_t        count;
        VkDescriptorSet sets[MAX_SETS];
        uint32_t        dyn_count;
        uint32_t        dyn_offsets[MAX_DYN];
    } ds;

    struct { VkBuffer buf; VkDeviceSize off, size, stride; } vb[MAX_VB];
    struct { VkBuffer buf; VkDeviceSize off; VkIndexType type; } ib;

    struct {                          // render pass 实例（作用域）
        bool             active;
        VkRenderPass     pass;
        VkFramebuffer    fb;
        uint32_t         subpass;
        VkSubpassContents contents;
        VkRect2D         area;
    } rp;

    struct {                          // 动态状态
        VkViewport viewport[MAX_VP];
        VkRect2D   scissor[MAX_VP];
        float      line_width, depth_bias[3], blend_const[4];
        uint32_t   stencil_ref, stencil_cmp, stencil_write;
        VkCullModeFlags cull; VkFrontFace front; VkPrimitiveTopology topo;
        // ... 1.3 extended dynamic state 的字段
    } dyn;

    uint64_t defined;                 // 位掩码：哪些状态“已定义”
} RecordState;
```

**`defined` 位掩码是关键**：`vkBeginCommandBuffer` 之后所有状态 undefined，任何 `draw` 前都必须把用到的状态置位。这把“规范里的 undefined 行为”变成了一个可断言的位图。

### 1.4 指令流（append-only）

```c
typedef enum {
    CMD_BEGIN_RP, CMD_END_RP, CMD_NEXT_SUBPASS,
    CMD_BIND_PIPELINE, CMD_BIND_DS, CMD_PUSH_CONST,
    CMD_BIND_VB, CMD_BIND_IB,
    CMD_SET_VIEWPORT, CMD_SET_SCISSOR, CMD_SET_DEPTH_BIAS,
    CMD_DRAW, CMD_DRAW_INDEXED, CMD_DISPATCH,
    CMD_COPY, CMD_BARRIER, CMD_EXECUTE,
} OpCode;

typedef struct {
    uint16_t opcode;
    uint16_t size;                    // 便于跳过 / 序列化
    union {
        struct { VkRenderPass pass; VkFramebuffer fb; VkRect2D area;
                 uint32_t clear_count; } begin_rp;
        struct { VkPipeline p; } bind_pipe;
        struct { uint32_t first, count; uint32_t dyn_count; } bind_ds;
        struct { VkBuffer vb; VkDeviceSize off; } bind_vb;
        struct { uint32_t vcount, icount, first_v, first_i; } draw;
        struct { uint32_t gx, gy, gz; } dispatch;
        struct { VkImage img; VkImageLayout from, to; } barrier;
        struct { uint32_t n; VkCommandBuffer cbs[8]; } execute;
    } u;
} Cmd;
```

注意：**流里没有“当前管线”这种字段**。它有 `CMD_BIND_PIPELINE`，但那条指令的作用是“让 GPU 的寄存器等于这个值”，而不是“让录制器的寄存器等于这个值”——后者由 1.3 单独维护。

### 1.5 元数据（校验用）

- `inheritance`：secondary 声明“我会在哪个 renderPass/subpass 里跑”。
- `compatibility`：pipeline ↔ renderPass、framebuffer ↔ renderPass、descriptorSet ↔ pipelineLayout。
- `queue_family`：pool 的 family，提交时必须匹配。
- `pending` 计数：同一个 command buffer 可能被多次提交（`SIMULTANEOUS_USE`），引用计数决定何时回到 Executable。

---

## 2. 算法：六个函数族

### 2.1 生命周期转移（唯一的“纯算法”）

```c
void begin_cb(CmdBuf *cb, const VkCommandBufferBeginInfo *bi) {
    assert(cb->state == CB_INITIAL ||
           (cb->state == CB_EXECUTABLE && pool_has(cb->pool, RESET_COMMAND_BUFFER)));
    if (cb->level == PRIMARY)
        assert(!(bi->flags & ONE_TIME_SUBMIT && bi->flags & SIMULTANEOUS_USE));
    else
        assert(bi->pInheritanceInfo != NULL);

    cb->flags = bi->flags;
    memset(&cb->st, 0, sizeof cb->st);   // 状态全部 undefined
    cb->st.defined = 0;
    cb->stream.count = 0;                // 丢弃旧指令
    cb->state = CB_RECORDING;
}

void end_cb(CmdBuf *cb) {
    assert(cb->state == CB_RECORDING);
    assert(!cb->st.rp.active);           // 不允许在 pass 内结束
    cb->state = CB_EXECUTABLE;
    seal(cb->stream);                    // 可选：压缩/校验/生成元数据
}

void reset_cb(CmdBuf *cb) {
    assert(cb->state != CB_PENDING);
    cb->state = CB_INITIAL;
    cb->stream.count = 0;
}
```

### 2.2 状态写入（bind / set）

所有 bind 都是同一个模式：**校验 → 写寄存器 → 发射指令**。

```c
void cmd_bind_pipeline(CmdBuf *cb, VkPipelineBindPoint bp, VkPipeline p) {
    assert(cb->state == CB_RECORDING);
    assert(meta(p).bind_point == bp);
    if (cb->st.rp.active)
        assert(compatible(meta(p).render_pass, cb->st.rp.pass));

    cb->st.pipeline[bp] = p;             // ① 数据
    emit(cb, CMD_BIND_PIPELINE, p);      // ② 指令
}
```

### 2.3 指令发射（emit）

```c
void emit(CmdBuf *cb, OpCode op, Payload u) {
    if (cb->stream.count == cb->stream.cap) grow(&cb->stream);
    cb->stream.items[cb->stream.count++] = (Cmd){ .opcode = op, .u = u };
}
```

复杂度 O(1) 摊还；内存线性于指令数。

### 2.4 前置校验（draw 是重灾区）

`vkCmdDraw` 本身不改任何状态，只做一次“全量前置条件检查”然后发 4 个整数：

```c
void cmd_draw(CmdBuf *cb, uint32_t vc, uint32_t ic, uint32_t fv, uint32_t fi) {
    assert(cb->state == CB_RECORDING);
    assert(cb->st.rp.active);                       // 必须处于渲染作用域
    assert(cb->st.pipeline[GRAPHICS] != NULL);
    assert(has_all(cb->st.defined, NEED_DRAW));     // 动态状态齐全
    assert(ds_bound_matches_layout(cb));            // 描述符与 layout 一致
    assert(vb_bound_for_pipeline(cb));              // 每个 binding 都有 VB
    assert(compatible(meta(p).render_pass, cb->st.rp.pass));

    emit(cb, CMD_DRAW, (Payload){ .vcount = vc, .icount = ic,
                                  .first_v = fv, .first_i = fi });
}
```

这就是“瘦指令”的来源：**校验发生在 CPU，指令里只留变化量。**

### 2.5 次级合并（execute）

```c
void cmd_execute(CmdBuf *cb, uint32_t n, VkCommandBuffer *cbs) {
    assert(cb->level == PRIMARY);
    for (i in cbs) {
        assert(cbs[i]->state == CB_EXECUTABLE);
        assert(cbs[i]->level == SECONDARY);
        if (cb->st.rp.active)
            assert(cbs[i]->flags & RENDER_PASS_CONTINUE &&
                   compatible(cbs[i]->inheritance, cb->st.rp));
    }
    emit(cb, CMD_EXECUTE, n, cbs);
    invalidate_state(&cb->st);   // 除 render pass 状态外，全部 undefined
}
```

合并有两种实现：**内联 splice**（把次级指令拷进主缓冲，省一次跳转）或 **保留调用**（提交时展开）。前者快、后者省内存。

### 2.6 重置 / 回收

```c
void free_cb(CmdBuf *cb) {
    assert(cb->state != CB_PENDING);
    pool_release(cb->pool, cb);
}
```

---

## 3. 必须成立的不变量

| # | 不变量 | 违反后果 |
|---|---|---|
| I1 | `state == RECORDING` 才允许 `vkCmd*` | 未定义行为 / 验证层报错 |
| I2 | `draw` 必须在 render pass 或 dynamic rendering 作用域内 | 同上 |
| I3 | `draw` 前 pipeline / descriptor / VB / IB / dynamic state 必须齐全 | 同上 |
| I4 | pipeline 的 `renderPass/subpass` 与当前 pass 兼容 | 同上 |
| I5 | `execute` 的 secondary 必须 executable 且兼容 | 同上 |
| I6 | `execute` 之后 primary 状态 undefined（pass 状态除外） | 状态错乱 |
| I7 | `Pending` 期间禁止一切修改 | 数据竞争 / 设备丢失 |
| I8 | `end` 时不能仍在 render pass 内 | 未定义行为 |
| I9 | command buffer 只能提交到 pool 的 queue family | 验证层报错 |
| I10 | `ONE_TIME_SUBMIT` 完成后进入 Invalid | 复用即错 |

这 10 条就是“算法”的规格；实现里它们应该以 `assert` / `VUID` 的形式存在，而不是注释。

---

## 4. API → 数据 / 算法 映射

| API | 改哪些数据 | 走哪段算法 |
|---|---|---|
| `vkAllocateCommandBuffers` | 对象头：state=Initial、level、pool | 分配 |
| `vkBeginCommandBuffer` | state→Recording；清空 ③；清空 ④ | 生命周期 |
| `vkCmdBeginRenderPass` | `st.rp = {pass, fb, subpass=0, contents}` | 校验 + 状态 + 发射 |
| `vkCmdBindPipeline` | `st.pipeline[bp]` | 校验 + 状态 + 发射 |
| `vkCmdBindDescriptorSets` | `st.ds` | 校验 + 状态 + 发射 |
| `vkCmdBindVertexBuffers` | `st.vb[]` | 校验 + 状态 + 发射 |
| `vkCmdBindIndexBuffer` | `st.ib` | 校验 + 状态 + 发射 |
| `vkCmdSetViewport/Scissor/...` | `st.dyn.*` | 状态 + 发射 |
| `vkCmdDraw*` | **不变** | 全量校验 + 发射（瘦指令） |
| `vkCmdDispatch*` | **不变** | 校验 + 发射 |
| `vkCmdNextSubpass` | `st.rp.subpass++` | 校验 + 状态 + 发射 |
| `vkCmdEndRenderPass` | `st.rp.active = false` | 校验 + 状态 + 发射 |
| `vkCmdExecuteCommands` | 除 rp 外全部失效 | 校验 + 合并 + 发射 |
| `vkEndCommandBuffer` | state→Executable | 生命周期 |
| `vkResetCommandBuffer` | state→Initial；清空 ③④ | 生命周期 |
| `vkQueueSubmit2`（边界） | 不改命令缓冲内部数据，只改 state→Pending | 另一模块 |

**只有生命周期那几个 API 是“纯数据操作”，其余 `vkCmd*` 都是“校验 + 状态 + 发射”三件套。**

---

## 5. 校验分层：别把所有断言都放在录制期

| 层 | 谁做 | 例子 |
|---|---|---|
| 录制期（CPU，便宜） | 你的录制器 / 验证层 | state==Recording、pipeline 兼容、VB 齐全 |
| 提交期（GPU 前） | 驱动 | descriptor 越界、query 未结束、fence 复用 |
| 执行期（GPU） | 硬件 | 越界访问（robustness）、shader 分支 |

录制期能查的尽量查（失败早、定位准）；查不了的（比如依赖 GPU 数据的间接绘制）留给后面。这也是为什么 `vkCmdDrawIndirect` 的校验比 `vkCmdDraw` 弱——参数在 GPU 内存里。

---

## 6. 内存与复杂度

| 项 | 量级 |
|---|---|
| 单个 `Cmd` | 16–48 B（取决于 union 最大成员） |
| 一次 draw | 1 条指令 + 0 状态变更 |
| 一次 bind | 1 条指令 + 1 次状态写入 |
| 录制 | O(1) / 指令，摊还 |
| 重置 | O(1)（计数归零），除非 `RELEASE_RESOURCES` |
| 合并 secondary | O(n) splice 或 O(1) 调用 |
| 状态校验 | O(1) 位运算（`defined` 掩码） + O(#VB) 检查 |

真正的成本在**校验**，不在编码。这也是为什么成熟的渲染后端会把“状态设置”批量化（render graph 的 pass 合并）。

---

## 7. 这套模型能干什么

1. **自研后端**：把这套 `CmdBuf` 映射到 D3D12/Metal 的 command list，只换 `emit` 的实现。
2. **录制器 / 序列化**：`stream` 可以直接落盘，做离线回放或跨进程传输。
3. **校验层**：第 3 节的 10 条不变量做成断言，就是一个小型 validation layer。
4. **Render Graph**：把“状态 + 指令流”当成 IR，做 pass 重排、屏障插入、资源别名。
5. **教学**：API 名字会忘，但这 5 类数据 + 6 类算法不会。

---

## 8. 一页纸

```text
数据                          算法
① 句柄表（只读）      ──▶     ④ 校验（读状态 + 读元数据）
② 对象头（生命周期）  ──▶     ① 状态转移
③ 状态寄存器（肥）    ──▶     ② 状态写入
④ 指令流（瘦）        ◀──     ③ 发射
⑤ 元数据（兼容性）    ──▶     ⑤ 合并 / ⑥ 重置

draw():  校验(②③⑤) ──▶ emit(4 个整数)        // 不改状态
bind():  校验(②③⑤) ──▶ 写③ ──▶ emit(句柄)    // 改状态
begin(): 改② ──▶ 清③④                        // 不改指令
```
