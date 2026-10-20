# 06 · 验收

四层证明。每层都写清楚**能证明什么、不能证明什么**——后者比前者重要。

---

## 第一层：编译期蓝图（结构对不对）

四条 `static_assert` 逐字比对推导出来的蓝图：

```
tree_parallel   relations: (none)
                cells: mouse.left{direct:click} keyboard{direct:key_A}

tree_refine     relations: click->dbl
                cells: mouse.left{arbitrate:dbl,click}

tree_exclusive  relations: click~drag
                cells: mouse.left{arbitrate:click,drag}

tree_mixed      relations: click->dbl click~drag dbl~drag
                cells: mouse.left{arbitrate:dbl,drag,click} keyboard{direct:key_A}
```

另有四条钉住“叶子遍历顺序 == plan 的 kind 顺序”（否则 `on<Global>` 会错位）。

**能证明**：关系由结构生成；cell 划分正确；退化（direct）判定正确；遍历顺序自洽。
**不能证明**：运行期真的按这个蓝图执行（第二层）。

---

## 第二层：运行期四场戏（行为对不对）

每个场景三句话：装配、开演、对比一行字。

| 场 | 脚本（ms, 帧） | 记录 | arena 字段 | 证明了什么 |
|---|---|---|---|---|
| S1 并行 | 0 press(x=7) / 50 release / 100 key | `click:acc@50, key_A:acc@100` | clicks=1 click_x=7 keys=1 | L5：无纠纷 ⇒ 无裁决；**载荷到达 handler** |
| S2 单击 | 0 p / 50 r / 349 · / 350 · | `click:acc@350` | clicks=1 | 细化关系把 click 挡了 300ms |
| S2b 双击 | 500 p / 550 r / 600 p / 650 r | `click:sup@650, dbl:acc@650` | clicks=1 dbls=1 | L3：一次交互一个 accepted；superseded 不回调 |
| S3a 点 | 0 press(x=42) / 50 release | `drag:sup@50, click:acc@50` | clicks=1 click_x=42 | 互斥：先到先得；载荷正确 |
| S3b 拖 | 100 press / 160 move | `click:sup@160, drag:acc@160` | drags=1 | 互斥：反方向也成立 |
| S4 混合 | 0 p / 10 key / 20 r / 319 · / 320 · | `key_A:acc@10, click:acc@320` | clicks=1 keys=1 | L2：两种制度共存 |

关键在最后两列：`arena.clicks` / `arena.click_x` 是 **`on_click` 方法自己**写的，
说明两件事同时成立——
① accepted 真的走到了 `make_aggregate` 的 `self` 上；
② **载荷穿过了两字函数指针**，且 `on_key_A`（无载荷 kind）连参数都没有。

**能证明**：判决时机、判决种类、延迟语义、cell 隔离、绑定正确、载荷类型由 kind 决定。
**不能证明**：多识别器实例、多指、liveness。

---

## 第二层半：集成（真 `arena_event` + `make_aggregate`）

`test_uhi_arena.cpp`：帧是 `arena_event::input_frame`（由 `build_change`/`apply_change` 产出），
`ui` 是 arena 的字段，`arena.bind()` 里 `self.ui.bind(self)`。

| 场 | 依次送入（ms） | 记录 | arena 字段 |
|---|---|---|---|
| S1 | move(42,0)@0 / press@10 / release@60 | `click:acc@60` | clicks=1 **click_x=42** |
| S2 | move@0 / press@10 / release@60 / move@359 / move@360 | `click:acc@360` | clicks=1 |
| S3 | move@0 / press@10 / move(100,0)@70 | `click:sup@70, drag:acc@70` | drags=1 clicks=0 |
| S4 | move@0 / press@10 / keyA@20 / release@60 / move@360 | `key_A:acc@20, click:acc@360` | clicks=1 keys=1 |

**能证明**：真 `event_context`（按下状态、坐标增量）、真 `build_change` 产出的
`PointerButton`/`PointerDownMove`/`Key` 都能驱动手势；坐标从 `context` 一路传到 arena 字段；
时间来自 `change.time_point`，库不需要额外参数。

**不能证明**：多指、liveness、GLFW 线程。

---

## 第二层半·投递（v2）：真分发器 + 你的 `proxy_class_invoke`

`uhi/v2/src/test_delivery.cpp`。库与 v1 **逐字节相同**——这一层能通过，
说明投递链不需要 uhi 配合。

链路五跳，全部是你现成的机制：

```
event::*_dispatcher::instance().distribute(e)
  → dispatcher_bridge::forward<E>                     订阅者
  → proxy_class_invoke<>::invoke(arena, runtime_type_v<E>, &e)
  → arena.invoke<"input_event">(e)
  → build_change → ui.feed → apply_change → on_<kind>(self, payload)
```

| 场 | 送入 | 记录 | 说明 |
|---|---|---|---|
| D1 | move(42)/press/release | `click:acc@60` | `click_x=42`；`delivered=3` 证明三跳都到 |
| D2 | window_size(800,600) | 空 | 到了 `input_event` 但 `ChangeType::None` ⇒ 不产帧 |
| D3 | press/release/…/360 | `click:acc@360` | 真 input_frame 下的细化延迟 |
| D4 | press/keyA/release/360 | `key_A:acc@20, click:acc@360` | 两个 cell 共存 |
| D5 | press/move(100) | `dbl:sup, click:sup, drag:acc` | 真 input_frame 下的互斥 |

编译期三条断言：
- `all_deliverable<data_tuple>`：5 个事件类型**逐个** `can_noexcept_invoke` 为真；
- `!can_noexcept_invoke<"input_event", kb_only, mousebutton_event>`：只收键盘的成员
  对其它类型为假 ⇒ 被 `dispach_invoke` **丢掉**（"丢弃不能投递的类型"）；
- `frame_like<input_frame>` 为真、`frame_like<int>` 为假。

### 语义表（3 条规则钉死）

`one_of<A,B>` 对 A、B **展开后的所有 kind** 两两互斥，所以：

| # | 序列 | 记录 |
|---|---|---|
| 1 | press / move(100) | `dbl:sup, click:sup, drag:acc` —— 拖拽同时压掉两个 |
| 2 | press / release / timeout / press / move | `click:acc@360, dbl:sup@470, click:sup@470, drag:acc@470` —— 两次交互互不干扰 |
| 3 | press / release / press / release | `click:sup@160, dbl:acc@160` —— drag 在 release 自己放弃，**不被记成 sup** |

第 3 条就是本轮抓到的 bug（见 [04 决策 15](04-design-space.md)）：修法是
"一帧内先处理放弃，再处理想赢"。

### 手写 arena（`test_handwritten.cpp`）

**没有工厂函数。** 四个用例各写一整段 `make_aggregate<"arena">(...)`，
字段名、handler、连 `input_event` 的**输入策略**都各自不同：

| 用例 | 树 | arena 自己的东西 | 证明 |
|---|---|---|---|
| t1 | `g_click` | `hits`, `spot` | 一个 kind 一个直呼 cell |
| t2 | `refines<click,dbl>` | `frames`, `singles`, `doubles` | 延迟 + arena 自己记账 |
| t3 | `one_of<click,drag>` | `ignored`, `taps`, `pulls` | arena 在 `input_event` 里忽略右键（库不参与策略） |
| t4 | 混合 | `clicks/dbls/drags/keys/last_x` | 两 cell 共存、坐标落字段 |

每个都过 `static_assert(uhi::accepts_platform_events_v<decltype(arena)>)` ——
**契约在编译期检查，类型是自由的**。

### 穷举 + 随机（任意合法输入序列）

每一次投递后都审计三条不变量：

1. 同一个 cell 内，任意两条 accepted 的时间跨度**不重叠**（一次交互一个赢家）；
2. 回调次数 == accepted 条数（superseded 不回调、也不重复回调）；
3. `delivered == posted`（每一跳都真的到了 `input_event`）。

| 测试 | 规模 | 结果 |
|---|---|---|
| 穷举 | 字母表 {press, release, move, key}，长度 1..6 ⇒ **5460 条序列 / 30948 次投递 / 8938 accepted** | 违例 0 |
| 随机 | **100000 次投递**（含右键、按键抬起、`window_size` 这种不认识的事件） | 违例 0，18088 accepted |

**能证明**：任意（包括非法、乱序的）事件序列走完整投递链后，
每格每交互至多一个赢家、回调恰好一次、投递无丢失。
**不能证明**：多指、liveness、GLFW 线程模型。

### 翻译矩阵（`test_translation.cpp`，不需要人手也不需要窗口）

GLFW 没有事件注入 API，但它**能送来的值是有限的**，所以这一层可以穷举：

| 矩阵 | 规模 | 检查 |
|---|---|---|
| 键盘 | 122 keys × 4 actions = 488 | change==Key、key 透传、press/repeat 归一、context 按住状态 |
| 鼠标 | 13 buttons × 4 actions = 52 | 左/右/中映射、其余 ⇒ `eUNDEFINED` **必须丢弃**、不踩坏 context |
| 移动 | {有/无历史位置} × {有/无按键} | Down/Up 分类、dx/dy 只在有历史时算、apply 后位置有效 |
| 其它类型 | 5 种（cursor_enter / window_size / char / scroll / drop） | 全部 ⇒ `ChangeType::None` |

**2237 项检查，失败 0。**

这一层抓到的是**崩溃级**的问题：GLFW 会送 4..8 号侧键，`map_button` 给
`eUNDEFINED = 0xFF`，`apply_change` 却拿它当下标写 `buttons[0xFF]`。
去掉那句保护后程序立刻 `0xC0000409`（栈越界写）。
证据：临时删掉保护 → 崩溃；恢复 → 2237 项全过。

---

## 第五层（v3）：liveness 与多指

### liveness（`v3/src/test_liveness.cpp`）

| 用例 | 序列 | 结果 |
|---|---|---|
| t1 无兜底 | 双击树，dbl 卡死在 wait_second | click 永远 deferred：**卡死会堵死邻居** |
| t2 600ms 兜底 | 同 t1，`leaf<…,600>` | `click:acc@700`，跨度仍是 `[10,60]`（兜底不发明赢家） |
| t3 纯 tick | `tick(359)` / `tick(360)`，60ms 之后没有任何帧 | `click:acc@360`：窗口靠时间推进过期，不需要 `none` 帧 |

t1/t2 是**同一份输入只差一个参数**，这一点就是验收物。

### 多指（`v3/src/test_multitouch.cpp`）

| 用例 | 序列 | 结果 |
|---|---|---|
| t1 三指独立 | 三指同时按住、交错抬起 | `[0] tap:acc@80 [1] tap:acc@60 [2] tap:acc@70`，payload 各带自己的 id |
| t2 互斥按指划分 | 0 号划走 + 1 号点 | `[0] tap:sup@50, swipe:acc@50` / `[1] tap:acc@60` |
| t3 卡住不牵连 | 0 号按住不放（hold=500）+ 1 号正常 | 1 号照常；`tick(600)` 只清 0 号；0 号随后恢复正常（载荷是新的 70，不是旧的 5） |

**能证明**：时间可以独立推进；卡死的假设不会永久占槽；一根手指的状态/裁决/记录与别人无关。
**仍不能证明**：跨指针的手势（双指缩放需要跨通道状态，那是另一层）。

---

## 第六层（v3）：综合试跑

`v3/src/test_tour.cpp` —— 一个程序走完全部能力，每步 drain + 审计：

| 段 | 能力 | 记录 |
|---|---|---|
| P1 | 直呼 | `key_A:acc@100` |
| P2 | 细化 + tick + 载荷 | `click:acc@360`（60ms 后无帧），`click_x=10` |
| P3 | 双击让位 | `click:sup@750, dbl:acc@750` |
| P4 | 一个赢家压掉两个 | `dbl:sup@860, click:sup@860, drag:acc@860` |
| P5 | 类型裁剪 | `window_size` 无记录，`dropped=1` |
| P6 | 多指 | `[0] tap:acc@1050`、`[1] tap:sup@1060, swipe:acc@1060` |
| P7 | 兜底 | dbl 卡死 400ms → `click:acc@500` |
| P8 | 审计 | 投递 14 / 裁剪 1 / 记录 10 / accepted 6 / 回调 6 / 跨度重叠 0 |

**全套回归**：8 个程序 × {debug, release} 全绿，0 警告；3 个坏结构仍编译失败。

---

## 第三层：坏结构必须编译不过

`test_uhi_reject.cpp`，三个 CASE 分别编译，实际报错：

```
CASE 1  refines<A(mouse), K(keyboard)>
  what(): 'kind [a] 与 [k] 在不同通道上：它们永远不会相遇，关系没有意义'

CASE 2  one_of<A(mouse), K(keyboard)>
  what(): 同一句

CASE 3  all_of<A, A>
  what(): 'duplicate kind [a]：同一个语义事件只能有一个生产者'
```

**能证明**：三类错误在编译期被拦住，错误信息说人话。
**不能证明**：所有错误类别；已知环检测不可达（树只有一个父亲），保留作防御。

---

## 第四层：质量与可观测性

```
-Wall -Wextra -O0        → 0 warnings, 0 errors
-O2 -DNDEBUG             → 通过
uhi::describe<tree>()    → 编译期算出的结构全部可打印（蓝图/slots/尺寸）
```

**能证明**：没有未使用变量、没有隐式截断；结构是**可读**的，不是黑盒。

---

## 待补的验收（v2 / v3）

| 版本 | 验收物 |
|---|---|
| v2 | `static_assert` 证明 `on_click` 的签名由 kind 的载荷类型决定；一场 handler 通过 `self` 写回 arena 数据的戏 |
| v3 | 两路 pointer 并发不串扰；一个卡死的 FSM 不会永久占住 cell |
