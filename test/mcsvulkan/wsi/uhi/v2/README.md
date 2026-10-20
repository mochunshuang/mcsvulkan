# v2 —— 投递版

## 这一版交付什么

三件事，都不在库的主线上：

1. **集成接口固化成两个名字**（`uhi_integration.hpp`）
2. **手写 arena 的用例**（`test_handwritten.cpp`）——没有工厂函数
3. **投递链的穷举/随机验证**（`test_delivery.cpp`）

库本身只改过一处逻辑（`stage::settle` 的结算顺序，见文末）。

## 集成接口：两个名字

```cpp
uhi::subscribe(arena)          // 外部平台事件 → arena，RAII，析构自动摘掉
method<"input_event">          // arena 自己的输入入口（uhi 只认这个名字）
```

`uhi_integration.hpp` 提供：

| 名字 | 作用 |
|---|---|
| `uhi::platform_events` | 集成层接受的事件集合（5 种） |
| `uhi::accepts_platform_events_v<Arena>` | **契约检查**：arena 的 `input_event` 能否 noexcept 收下每一种 |
| `uhi::subscribe(arena)` | 订阅全部平台分发器，投递到 `arena.invoke<"input_event">` |
| `uhi::proxy_class_invoke` / `invoke_member_help` | 从 test_glfw12 搬来的类型擦除派发（原样） |

于是"投递到不到得了"在**编译期**就答完了：

```cpp
auto arena = make_aggregate<"arena">( ... );
static_assert(uhi::accepts_platform_events_v<decltype(arena)>);
auto conn = uhi::subscribe(arena);     // 编译期过了才谈运行期
```

## 手写 arena（`test_handwritten.cpp`）

**没有任何 `make_arena` 工厂。** 每个用例自己写一整段，像手写 struct：

```cpp
auto arena = make_aggregate<"arena">(
    field<"now">(time_point{}),
    field<"ctx">(arena_event::event_context{}),
    field<"ui">(uhi::ui<tree>{}),
    field<"hits">(int{}),                 // ← 字段名自己定
    field<"spot">(double{}),
    method<"on_click">([](auto &self, const click_event &e) noexcept { ... }),
    method<"bind">([](auto &self) noexcept { self.ui.bind(self); }),
    method<"input_event">([](auto &self, auto e) noexcept { ... }));  // ← 输入策略自己定
```

四个用例，四个不同的 arena（字段不同、handler 不同、连**输入策略**都不同）：

| 用例 | 树 | arena 自己的东西 | 证明了什么 |
|---|---|---|---|
| t1 单手势 | `g_click` | `hits`, `spot` | 一个 kind、一个直呼 cell |
| t2 细化 | `refines<click,dbl>` | `frames`, `singles`, `doubles` | 延迟 + arena 自己记账 |
| t3 互斥+策略 | `one_of<click,drag>` | `ignored`, `taps`, `pulls` | **arena 在 input_event 里忽略右键**（库不参与这种策略） |
| t4 全套 | 混合 | `clicks/dbls/drags/keys/last_x` | 两个 cell 共存，坐标落到字段 |

```
t1 单手势        click:acc@60   → hits=1 spot=42
t2 细化          singles=1 doubles=1 frames=8
t3 互斥+策略     taps=0 pulls=1 ignored=2
t4 全套          clicks=1 keys=1 last_x=15
```

## 翻译矩阵（`test_translation.cpp`）

GLFW **没有事件注入 API**（它是输入读取库，事件只能来自 OS）。但它能送来的值有限，
所以"GLFW 可能产生的一切"可以**不用人手、不用窗口**穷举：

| 矩阵 | 规模 | 结果 |
|---|---|---|
| 键盘 | 122 keys × 4 actions | 通过 |
| 鼠标 | 13 buttons × 4 actions | 通过 |
| 移动 | {有/无历史位置} × {有/无按键} | 通过 |
| 其它类型 | cursor_enter / window_size / char / scroll / drop | 全 ⇒ None |

**2237 项检查，失败 0。** 这一层抓到崩溃级 bug，见下。

## 文件

```
uhi/v2/
  README.md
  src/uhi.hpp            库（= v1/src/uhi.hpp）
  src/uhi_integration.hpp  平台事件集合 + 契约检查 + subscribe（唯一集成层）
  src/arena_event.hpp    从 test_glfw12 搬的输入层（build_change/apply_change）
  src/gestures.hpp       四个手势的 FSM + leaf
  src/test_util.hpp      at / expect / glfw_post / 惯用缩写
  src/test_handwritten.cpp  4 个手写 arena（无工厂）
  src/test_translation.cpp  翻译矩阵：GLFW 全部取值 ⇒ ChangeType
  src/test_delivery.cpp     5 场景 + 语义表 + 穷举 + 随机
```

## 测试规模

| 部分 | 规模 | 结果 |
|---|---|---|
| 手写 arena | 4 个各自独立的 arena | 通过 |
| D1–D5 场景 | 5 条手写序列 | 通过 |
| 语义表 | 3 条 `one_of` 规则 | 通过 |
| 穷举 | {press, release, move, key} 长度 1..6 ⇒ 5460 条序列 / 30948 次投递 / 8938 accepted | 违例 0 |
| 随机 | 100000 次投递（含右键、按键抬起、不认识的事件） | 违例 0，18088 accepted |
| 编译期 | 每个 arena 都过 `accepts_platform_events_v`；kb-only 成员对其余类型为假；`frame_like` 三条 | 成立 |

每次投递后审计：① 同 cell 内 accepted 跨度不重叠；② 回调次数 == accepted 条数；
③ `delivered == posted`。

## 这一轮抓到的两个真 bug

### 1. 未映射的鼠标按钮 = 越界写（崩溃级）

GLFW 会送 4..8 号侧键。`map_button` 对它们返回 `eUNDEFINED = 0xFF`，
`apply_change` 却拿它当下标写 `ctx.buttons[0xFF]` —— 栈越界写。

```
去掉保护：0xC0000409（栈越界）
恢复保护：2237 项检查全过
```

修法：`if (ch.button.button == MouseButton::eUNDEFINED) break;`
（同时把 `eMOUSE_BUTTON_MIDDLE` 补回枚举，和 test_glfw12 一致。）

### 2. 同一帧里"放弃"和"判赢"的顺序

**同一帧里"放弃"和"判赢"的处理顺序。** release 那一帧 drag 已自己放弃（该静默），
但 `settle` 先处理了 dbl 的"想赢"，把还活着的 drag 记成 `superseded` ⇒ 假回滚。

```
修前： drag:sup@160, click:sup@160, dbl:acc@160
修后： click:sup@160, dbl:acc@160
```

修法（`uhi.hpp` `stage::settle`）：① 先处理 `want==2`（放弃，静默）
② 再处理 `want==1`（想赢）③ 最后唤醒 deferred。
连带 v1 的 S3a 也少了一条假事件。

## 跑

```powershell
cd E:\0_github_project\mcsvulkan\test\mcsvulkan\wsi
g++ -std=c++26 -freflection -O2 uhi\v2\src\test_handwritten.cpp -o output/test_handwritten.exe -lstdc++exp
g++ -std=c++26 -freflection -O2 uhi\v2\src\test_delivery.cpp    -o output/test_delivery.exe    -lstdc++exp
.\output\test_handwritten.exe
.\output\test_delivery.exe
```
