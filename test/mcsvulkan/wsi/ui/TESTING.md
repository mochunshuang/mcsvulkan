# UI 层：怎么测、怎么读、怎么改

这份文档回答三件事：**这条管线是怎么跑的**、**怎么验证它是对的**、**出问题时给我什么信息**。

---

## 0. 三十秒版本

```powershell
# ① 自动自测（无窗口、确定性、2 秒）：24 条断言，退出码 0 = 全过
#    a. 用工程目标（需要先构建）
.\output\Debug\bin\mcsvulkan-wsi-test_glfw.exe --selftest
#    b. 或者脱开 Vulkan/GLFW 单独编（不需要任何第三方库）
cd test\mcsvulkan\wsi
g++ -std=c++26 -D__cpp_lib_constexpr_exceptions=0 -DNOMINMAX -DWIN32_LEAN_AND_MEAN `
    -I../../../include -o ui_selftest.exe ui/standalone_test.cpp -lstdc++exp
.\ui_selftest.exe            # --trace 可看到每一步裁决过程

# ② 交互验收（窗口里点点鼠标，看 [arena] / [win] / [state] 三类日志）
.\output\Debug\bin\mcsvulkan-wsi-test_glfw.exe
```

---

## 1. 原理：一条数据流

```
                          ┌─────────────────────────────────────────┐
   硬件（GLFW）           │  ui_input（管线，ui_widget.hpp:184）      │
   ├─ key/mouse/scroll ──►│  ① commit_due(样本时间)  时间先推进        │
   │   （回调通道）        │  ② 命中链上所有人 on_close  收尾上一段手势 │
   └─ 光标位置 ──每帧轮询─►│  ③ 命中链上所有人 on_open   提出新候选     │───┐
                          │  ④ 直通层：原始事件交命中链最内层          │   │
                          │  ⑤ commit_due(样本时间)  本批结算          │   │
                          └─────────────────────────────────────────┘   │
                                                                        ▼
   控件树（widget）── hitTest ──► 命中链[最内层 … 根]              ┌──────────────┐
   · 每个控件绑定若干识别器（add_recognizer<T>）                   │ 裁决层        │
   · 控件可自带裁决层（arena_override）                            │ gesture_arena │
   · 控件重写虚函数接收胜出结构                                    │ offer/retract │
                                                                  │ accept/commit │
   识别器（gesture_recognizer）── offer(candidate) ──────────────►│ 同槽只留一个  │
   · priority 语义特异性（越大越具体）                             └──────┬───────┘
   · hold     还要等多久才无可争议                                        │ 胜出
   · depth    命中链深度                                                  ▼
   · captures_pointer / owner / target                       订阅层（gesture_target 虚函数）
                                                              on_tap / on_drag_* / on_command …
                                                              · 返回 true = 已消费
                                                              · false → parent 继续冒泡
```

**五个固定步骤**（写在 `ui_input::push`，顺序写死不依赖注册顺序）：

| 步骤 | 做什么 | 为什么必须有 |
|---|---|---|
| ① | `commit_due(样本时间)` | 时间必须先推进：上一批窗口到期的候选先胜出，否则"先松手后按下"的因果会被后来的事件改写 |
| ② | 命中链全体 `on_close` | 收尾上一段手势（撤回长按候选、给单击链条升格或定案）。**先收尾再开启**是 Flutter 竞技场 `close & sweep` 的那条规矩，也是修掉"旧候选挡住新候选"的关键 |
| ③ | 命中链全体 `on_open` | 提出本样本的新候选（此刻槽位已清，谁能压过谁只看 priority/depth） |
| ④ | 直通层 | 原始事件（按下/松开/移动/滚轮/按键）不参与竞争，永远按发生顺序送达 |
| ⑤ | `commit_due(样本时间)` | 结算本批：`hold=0` 的候选（快捷键/缩放/拖动/无升格可能的单击）此刻胜出 |

**候选只有四条路能到达订阅层**（`gesture_arena`，ui_arena.hpp:144）：

`offer`（提出，可压掉同槽弱者）｜`retract`（自己撤回）｜`accept`（已无可争议→立即胜出）｜`commit_due`（时间到→胜出）

**为什么单击要等**：松手那一刻 click 已经成立，但 double_click 仍"可能出现"（多击窗口没过）。
所以 click 的 `hold=多击窗口`，必须等到再没有更强候选能出现的时刻才敢胜出；第二击真来了就 `retract` 掉它。
两个策略的差别只有这一处（`upgrade_hold`，ui_arena.hpp）：

```cpp
inline constexpr arbitration kArbitration = arbitration::eARENA;  // 默认：Flutter/Android 语义
// eOPTIMISTIC = Web DOM 语义：单击立刻发，之后真来第二击再补一个 double_click
```

---

## 2. 代码地图：从哪读起

建议阅读顺序（每步只看一个概念）：

| 顺序 | 位置 | 看什么 |
|---|---|---|
| 1 | [test_glfw.cpp:123](test_glfw.cpp#L123) `button_widget` | 控件怎么装配：构造函数里 `add_recognizer<T>()`，重写 `on_tap` 改状态 |
| 2 | [test_glfw.cpp:244](test_glfw.cpp#L244) `editor_widget` | `arena_override()` —— 这个区域自带一套裁决层 |
| 3 | [test_glfw.cpp:397](test_glfw.cpp#L397) `demo_app` | 控件树 + 运行时命令表，`the_app()` 是 main 之外的注入点 |
| 4 | [test_glfw.cpp:483](test_glfw.cpp#L483) `main` | `ui_input pipeline{input, app.root()}`，每帧 `pollEvents + push_cursor + update` |
| 5 | [ui_widget.hpp:440](ui/ui_widget.hpp#L440) `push` | 上面那五个步骤，就是整个算法的骨架 |
| 6 | [ui_widget.hpp:294](ui/ui_widget.hpp#L294) `on_winner` | 胜出者怎么出去：捕获指针 → 交付给 target 的虚函数 → 冒泡 → 设焦点 |
| 7 | [ui_arena.hpp:393](ui/ui_arena.hpp#L393) `tap_recognizer` | 最容易看懂的一个识别器：连击链 + 什么时候 hold |
| 8 | [ui_arena.hpp:560](ui/ui_arena.hpp#L560) `drag_recognizer` | 需要捕获的识别器：begin/update/end 一条流 |
| 9 | [ui_arena.hpp:201](ui/ui_arena.hpp#L201) `priority_arena::outranks` | **裁决的全部规则就这一个函数**：先比特异性，平手比深度 |
| 10 | [ui_arena.hpp:360](ui/ui_arena.hpp#L360) `deepest_first_arena` | 换掉一个虚函数 = 换了一整套裁决规则 |
| 11 | [ui_event.hpp:263](ui/ui_event.hpp#L263) `deliver_to` | `std::visit` → 直接调指定虚函数（编译期分派） |
| 12 | [ui_event.hpp:226](ui/ui_event.hpp#L226) `command_registry` | 运行时逃生口：string 名字 + `std::any` 参数 |

关键类型：`input_sample`(ui_event.hpp:89)、`gesture`(190)、`gesture_target`(196)、`candidate`(ui_arena.hpp:122)、
`gesture_config`(ui_arena.hpp:34)、`widget`(ui_widget.hpp:49)。

---

## 3. 真实日志解读：一次单击的完整裁决

下面是 `ui_selftest.exe --trace` 里"用例①单击"的真实输出（子控件 child 在 (150,150)，父控件 root 也绑了同一套识别器）：

```
[arena:priority_arena] offer   pointer/MOUSE_BUTTON_LEFT prio=15 hold= 500ms depth=1 long_press ... (150,150)
[arena:priority_arena] reject  pointer/MOUSE_BUTTON_LEFT prio=15 hold= 500ms depth=0 long_press ... 被在场候选挡住
[arena:priority_arena] retract pointer/MOUSE_BUTTON_LEFT prio=15 hold= 500ms depth=1 long_press ... 识别器自己撤回
[arena:priority_arena] offer   pointer/MOUSE_BUTTON_LEFT prio=10 hold= 500ms depth=1 click count=1 held=80ms (150,150)
[arena:priority_arena] reject  pointer/MOUSE_BUTTON_LEFT prio=10 hold= 500ms depth=0 click count=1 ... 被在场候选挡住
[PASS] ①单击：松手瞬间只有直通层，click 还没派发（多击窗口未过）
[arena:priority_arena] win     pointer/MOUSE_BUTTON_LEFT prio=10 hold= 500ms depth=1 click count=1 ... 等待窗口到期
[win  ] click MOUSE_BUTTON_LEFT count=1 held=80ms (150,150)   → child
[PASS] ①单击：窗口到期后 click 胜出，交给命中的子控件（不是父控件）
```

逐行读：

1. **按下**：child 与 root 都提出 `long_press`（prio 15，hold 500ms）。child 先提出（命中链深处优先），root 的同级候选被 `reject` —— 靠的正是 `outranks` 的"平手比深度"。
2. **松手**：`retract long_press`（松手就说明长按不成立，识别器自己撤回），随后提出 `click`（count=1，hold=**500ms**）——注意它**没有**在这一刻胜出。
3. **过了 500ms**：`win ... 等待窗口到期`，然后 `[win  ]` 行告诉我们交付给谁（`→ child`）以及最终调用了 child 的 `on_tap`。
4. 所以"单击"在控制台上出现得**比你的手慢半拍**，这不是卡顿，是多击窗口。

`[win  ]` 行末尾的三种形态：`→ 控件名`（有人处理）｜`→ 没人处理`（一路冒泡到根都没接）｜
带 `[route]` 前缀的行表示路由决策（裁决层切换、指针捕获）。

---

## 4. L1 · 自动自测（先做这个）

[ui/self_test.hpp](ui/self_test.hpp) 用注入的样本 + 注入的时间把整条链跑一遍，**不建窗口、不依赖 GLFW 运行时**，
因此每次结果完全一样。入口 [run():515](ui/self_test.hpp#L515)。

跑法一（工程目标，推荐给你自己验证）：

```powershell
cmake --build E:/0_github_project/mcsvulkan/build --target mcsvulkan-wsi-test_glfw
E:/0_github_project/mcsvulkan/output/Debug/bin/mcsvulkan-wsi-test_glfw.exe --selftest   # 退出码 0 = 全过
```

跑法二（脱开 Vulkan/GLFW，2 秒编完，不需要任何第三方库 —— 这是"测这套 API"最轻的方式）：

```powershell
cd E:/0_github_project/mcsvulkan/test/mcsvulkan/wsi
g++ -std=c++26 -D__cpp_lib_constexpr_exceptions=0 -DNOMINMAX -DWIN32_LEAN_AND_MEAN `
    -I../../../include -o ui_selftest.exe ui/standalone_test.cpp -lstdc++exp
.\ui_selftest.exe           # 只看 PASS/FAIL
.\ui_selftest.exe --trace   # 连每一步裁决过程一起看
```

`-lstdc++exp` 是必须的（`std::println` 的 Windows 终端支持在这个库里，工程链接行里也是它排第一）。

**13 个用例（24 条断言）验证的语义**：

| # | 用例 | 期望 / 验证点 |
|---|---|---|
| ① | 单击延迟 | 松手瞬间只有 `raw:down/up`；500ms 后才 `tap:click:1`；父控件不被误触发 |
| ② | 双击胜出 | 第一击 `retract` 且**不补发**；只有 `tap:double_click:2` |
| ③ | 三击 | `count` 到上限 → `hold=0` → 最后一次松手**当场**胜出（不必再等窗口） |
| ④ | 链断（位置漂移 30px） | 上一击**立即定案**，且派发在"第二次按下"**之前**（因果顺序） |
| ⑤ | 长按 | 按住到阈值就胜出，不必松手；之后松手**不会**再冒出 click |
| ⑥ | 拖动流 | `drag_begin → drag_update → (raw:up) → drag_end`，全程没有 click |
| ⑦ | 指针捕获 | 指针划出控件后拖动流**仍归它**，父控件全程没被卷进来 |
| ⑧ | 快捷键与直通层 | `raw:key` 在前、`shortcut` 在后，同帧内顺序固定 |
| ⑨ | 滚轮 | 原始 `wheel` 永远在；`Ctrl+滚轮` 额外产生语义 `zoom` |
| ⑩ | 运行时换裁决层 | 同一动作：自带层（深度优先）→ 子控件赢；强制默认层（特异性优先）→ 父控件赢；放开开关 → 又回子控件 |
| ⑪ | 单成员竞技场 | `max_click_count=1` → 松手**立即**胜出（没有更强的候选就不必等） |
| ⑫ | 已知代价 | 被升格链条取消的单击不补发；第二击改判成长按 |
| ⑬ | 冒泡 | 子控件收到并声明不处理 → 父控件收到同一条并消费 |

> 这套用例不只是测试，也是**语义规格**：想改行为，先改这里的期望，再改代码。

---

## 5. L2 · 交互式手工验收（窗口里点点）

窗口 800×600，四个控件（[describe_bindings] 启动时会打印在控制台）：

| 控件 | 区域（x,y,w,h） | 绑定的识别器 | 自带裁决层 |
|---|---|---|---|
| `button` | (40,40,160,48) | tap / long_press | — |
| `canvas` | (230,40,530,220) | tap / drag(20) / zoom | — |
| `editor` | (40,300,720,260) | tap / long_press / drag(20) / shortcut | ✅ deepest_first |
| `desktop`（根，铺满） | (0,0,800,600) | tap / drag(**25**) / shortcut | — |

> 窗口没有渲染，可能是纯色/空白，这不影响输入测试。
> 鼠标移动时 `cursorPos:` 行会很吵 —— 那是既有 `glfw_input` 的打印，与本层无关。

按顺序做这 13 步（坐标给的是窗口内容坐标）：

| 步骤 | 操作 | 期望在控制台看到 |
|---|---|---|
| S1 | 移到按钮 (120,64)，再移开 | `[state] button.hover = true` → `= false` |
| S2 | 单击按钮，**等半秒** | `offer ... long_press` → `retract` → `offer click count=1 hold=500ms` → 500ms 后 `win` + `[win  ] click ... → button` + `[state] button.active = true` |
| S3 | 快速双击按钮 | `[arena] retract click count=1`（第一击落败）→ `offer count=2` → `[win ] double_click → button` + `[state] button 双击被消费…` |
| S4 | 长按按钮 0.6 秒再松 | `offer long_press(hold=500ms)` → 到点 `win long_press` + `[state] button 被长按 → 复位`；松手不再有 click |
| S5 | 单击画布 (495,150) | 延迟半秒后 `[win ] click ... → canvas` + `[state] canvas 单击 → 选中物体` |
| S6 | 双击画布 | `[win ] double_click ... → canvas` + `[state] canvas 双击 → 复位` |
| S7 | 从画布 (400,150) 拖到 (520,190) | `[route] 指针捕获 → pointer/MOUSE_BUTTON_LEFT（canvas）` + `drag_begin` + 多行 `drag_update` + `[state] canvas 拖动 → offset=(…)` + 松手 `drag_end`；**全程没有 click** |
| S8 | Ctrl+滚轮在画布上 | `[win ] zoom ... → canvas` + `[state] canvas 缩放 → zoom=1.10`（原始 `wheel` 也在） |
| S9 | 点一下编辑器（给焦点），再按 Ctrl+A | `[state] editor 快捷键 CONTROL+A`（焦点链 = 编辑器 + 根，深度赢） |
| S10 | 右键点编辑器 (400,430) | `[menu ] 编辑器菜单 @(400,430)：重命名 / 重构 / 格式化` |
| S11 | 右键点桌面空白 (400,280) | `[menu ] 桌面菜单 @(400,280)：新建文件夹 / 显示设置` |
| S12 | 右键拖编辑器 | `[state] editor 右键拖动 → 列选择开始` + `列选择 d=(…)` + `列选择结束`（**不是**移动窗口） |
| S13 | 按 **F1**，再右键拖编辑器 | `[state] F1 → 强制默认裁决层 = true` + `[route] 裁决层切换 → priority_arena（命中：editor）`，这次是 `[state] desktop 右键拖动 → 移动窗口开始` |
| S14 | 关窗退出 | `统计：裁决层切换 N 次，未被处理的事件 M 个` |

S12 vs S13 就是**"命中链决定裁决层"**的核心演示：同样的动作、同样的控件树，
只因为裁决规则从"深度优先"换成"特异性优先"，胜出者就从编辑器变成桌面。

**日志 tag 速查**：

| tag | 含义 |
|---|---|
| `[arena:<层名>] offer/reject/preempt/retract/accept/win` | 裁决过程（层名会随区域切换） |
| `[route] 裁决层切换 → …` / `[route] 指针捕获 → …` | 路由决策 |
| `[win  ] <事件> → <控件>` / `→ 没人处理` | 谁胜出、交给了谁、有没有被消费 |
| `[state] …` | 演示控件的状态真的变了（这就是"对象发生改变"） |
| `[menu ] …` | 运行时命令表按 string 名字分发 |
| `key:` / `mouse:` / `scroll:` / `cursorPos:` | 既有 `glfw_input` 的原始打印 |

---

## 6. L3 · 用这套 API 加东西

### 6.1 加一个识别器（例：中键点击 = 关闭标签）

```cpp
class middle_click_recognizer : public ui::gesture_recognizer
{
  public:
    middle_click_recognizer(ui::gesture_target *t, int depth) : gesture_recognizer{t, depth} {}

    void on_close(const ui::input_sample &, ui::gesture_arena &) override {}   // 没有要收尾的

    void on_open(const ui::input_sample &s, ui::gesture_arena &arena) override
    {
        const auto *down = ui::as<ui::raw::pointer_down>(s.payload);
        if (down == nullptr || down->button != MouseButtonMiddle)
            return;
        ui::candidate cand{};
        cand.payload = ui::custom_gesture{ui::ctx_of(s), "close_tab", std::any{}};
        cand.priority = 50;                       // 比单击(10)/拖动(20)都强
        cand.depth = depth();                     // 平手时用
        cand.slot = ui::pointer_slot(down->button);
        cand.target = target();                   // 胜出后交给哪个控件
        arena.offer(cand.slot, s.time, std::move(cand));
    }
};
// 绑定（控件构造里，或从外部）：widget.add_recognizer<middle_click_recognizer>();
```

要点：**识别器不关心别人**，只负责"提出候选 + 说明自己多具体 + 要等多久"；它通过 `arena` 与其它识别器竞争，
通过 `target` 决定胜出后落到谁身上。

### 6.2 加一套裁决层（例：模态层，只让最深的控件赢）

```cpp
class modal_arena : public ui::priority_arena
{
  public:
    using priority_arena::priority_arena;            // 继承构造（含 sink 注入）
    [[nodiscard]] const char *name() const noexcept override { return "modal_arena"; }
  protected:
    [[nodiscard]] bool outranks(const ui::candidate &challenger,
                                const ui::candidate &holder) const noexcept override
    {
        return challenger.depth > holder.depth;      // 只认深度
    }
};
// 用法一：某个控件自带 ——  return &my_arena;   // widget::arena_override()
// 用法二：运行时整体替换 ——  pipeline.request_arena(&my_arena);   // 空闲时生效
```

`priority_arena` 里真正决定胜负的只有 `outranks` 一个虚函数；换它 = 换规则。
想让某类候选根本进不来，就重写 `offer` 做过滤。

### 6.3 加一个运行时命令（名字在运行时才知道）

```cpp
ui::command_registry::instance().add("close_tab",
    [](ui::gesture_target &t, const std::any &args) -> bool {
        auto *editor = dynamic_cast<editor_widget *>(&t);
        if (editor == nullptr) return false;      // 不是编辑器 → 交回冒泡链
        editor->close_current_tab();
        return true;
    });
// 触发：某个识别器产出 custom_gesture{"close_tab", any}; 或控件直接调用
// ui::command_registry::instance().invoke(*this, "close_tab", std::any{});
```

### 6.4 调参旋钮

| 旋钮 | 位置 | 作用 |
|---|---|---|
| `kGesture.click_max_time / multi_click_interval / long_press_time` | ui_arena.hpp:34 | 手势阈值（每个控件可以带自己的一份，构造识别器时传入） |
| `kGesture.max_click_count = 1` | 同上 | 关掉多击 → 单击松手立即胜出 |
| `kArbitration` | ui_arena.hpp | `eARENA`（默认）/ `eOPTIMISTIC`（Web 语义） |
| `ui::trace_input` | ui_event.hpp:44 | 运行时开关日志：`ui::trace_input = false;` 可静音整层 |
| `pipeline.set_force_default_arena(bool)` | ui_widget.hpp:239 | 忽略控件自带的裁决层（demo 用 F1 切） |

---

## 7. 出问题时给我什么信息

1. 你做了什么（哪一步、坐标、顺序、间隔大概多少 ms）；
2. 从操作**前 2 行到后 5 行**的完整控制台片段（`[arena]`/`[route]`/`[win  ]`/`[state]` 都要，别只贴一行 —— 裁决的因果在前后文里）；
3. `--selftest` 的输出（如果在 `ui/` 里改过东西）；
4. **期望**是什么 vs **实际**是什么。

判断"为什么没触发"的固定套路：在操作那一刻附近找这四类行 ——
`reject`（被在场候选挡住）｜`preempt`（被更强候选取代）｜`retract`（识别器自己撤回）｜
`hold=…ms`（还在等窗口）。这四行基本能解释所有"我以为该触发却什么都没发生"。

---

## 8. 已知限制与设计取舍

- **光标来源是轮询**：每帧 `glfwGetCursorPos` → `push_cursor`（ui_widget.hpp:250）。
  因为往 `include/detail/input/glfw_input.hpp` 加"光标移动变更通道"的那次写入没被允许（提权被拒）；
  轮询是合法的来源，两种来源可以并存（识别层不关心差别）。
- **单击会被延迟一个多击窗口**（竞技场语义）；切 `eOPTIMISTIC` 就是 Web 语义（先发后补）。
- **已知代价**（用例⑫）：第一击被升格链条撤回后不补发；Flutter 同样如此。
- **裁决层切换只在空闲时**生效（旧层不能还有候选在等），所以连点时切换会延后一拍。
- **原始层与语义层可以落在不同控件上**（用例⑩B）：原始事件按命中链走，手势按裁决结果走 —— 与 DOM 的模型一致。
- **长按期间位移不撤销长按**（未实现移动容忍）：有移动事件后，在 `long_press_recognizer::on_close` 里加一个
  `move` 分支按位移 `retract` 即可。
- **只做了语法检查**：`build/` 在我的工作区之外，写 `.obj` 需要你刚才拒绝过的那种提权，所以完整构建由你跑。
