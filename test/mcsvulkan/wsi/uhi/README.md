# uhi —— 语义事件库

> **一串原始输入进去，每个通道每次交互至多一个语义事件出来，且恰好播报一次。
> “谁赢”由你写的结构在编译期算出来，不由人在运行期写 if。**

---

## 舞台：接进 `arena_event` + `make_aggregate`

`ui` 是 arena 的**字段**，所以 `bind(self)` 时 `self` 就是 arena 本身——
生成的每个 UI 事件都落到 arena 自己的方法上，不需要外部变量。

```cpp
// ① arena：ui 是字段，handler 也在同一个 self 上
auto arena = make_aggregate<"arena">(
    field<"ctx">(arena_event::event_context{}),
    field<"ui">(uhi::ui<tree>{}),
    field<"clicks">(int{}), field<"click_x">(double{}),
    method<"on_click">([](auto &self, const click_event &e) noexcept {
        ++self.clicks; self.click_x = e.x; }),                 // 带载荷 → 收参数
    method<"on_dbl">  ([](auto &self) noexcept { ++self.dbls; }), // 无载荷 → 只收 self
    method<"bind">    ([](auto &self) noexcept { self.ui.bind(self); }),
    method<"input_event">([](auto &self, auto e, time_point t) noexcept {
        auto ch = arena_event::build_change(self.ctx, e);
        if (ch.change == arena_event::ChangeType::None) return;
        ch.time_point = t;
        self.ui.feed(arena_event::input_frame{self.ctx, ch});   // ← 唯一的输入动词
        arena_event::apply_change(self.ctx, ch);
    }));

// ② 结构
using tree = uhi::all_of<uhi::one_of<uhi::refines<click, dbl>, drag>, key_A>;

// ③ 绑定（延迟）+ 喂真事件
arena.invoke<"bind">();
send(arena, position2d_event{42.0, 0.0}, 0);
send(arena, mousebutton_event{L, PRESS},   10);
send(arena, mousebutton_event{L, RELEASE}, 60);

// ④ 两边都能验
assert(uhi::to_string(arena.ui.events()) == "click:acc@60");
assert(arena.clicks == 1 && arena.click_x == 42.0);
```

`frame_traits` 只写一次，把 `input_frame` 接进来：

```cpp
template <> struct uhi::frame_traits<arena_event::input_frame> {
    static uhi::time_point when(const arena_event::input_frame &f) noexcept
    { return f.change.time_point; }
};
```

**帧类型不认识就进不来**：`frame_like` 约束 + 空壳主模板，让上层那种
"按类型裁剪的派发"（`requires` / `can_noexcept_invoke`）自动把 ui 不认识的东西丢掉，
而不是报错。库本身仍然不认识 `arena_event`。

## 库里只有两个输入动词

```cpp
ui.feed(frame);    // 有帧：事件来了（时间从帧里取）
ui.tick(now);      // 只有时间：让识别器看一眼时钟（超时、兜底）
```

`feed` 收的帧必须带时间，用一行特化告诉库怎么读——

```cpp
template <> struct uhi::frame_traits<my_frame> {
    static uhi::time_point when(const my_frame &f) noexcept;   // 对 arena_event 就是 f.change.time_point
    static std::size_t     who(const my_frame &f) noexcept;    // 可选：多指时这一帧属于哪根手指
};
```

这样 `ui.feed(frame)` 的形状和 `arena.publish(frame)` 完全对齐，接进你的管线不需要适配层。
批量脚本（录制回放/测试）应该在库外——我的测试里就是 20 行 `run`。

绑定是**延迟**的：`ui` 可以先造、先挪、先不绑；`bind` 之后才开始回调。
函数指针没有捕获，所以它只是 `{void* self, void(*)(void*, const event&)}` 两个机器字。

## 用户会碰到的名字

```
描述结构   kind_t<"click"> / kind_t<"drag", drag_event>   leaf<K,S,Fsm>
           source_t<"mouse.left">   all_of  one_of  refines  times
装配       uhi::ui<Tree>
绑定       ui.bind(arena)                   // → arena.invoke<"on_<kind>">(载荷)
输入       ui.feed(frame)                   // 唯一动词：推进一帧
读时间     uhi::frame_traits<Frame>::when   // 你告诉库一次
观测       ui.events() / uhi::to_string(…)  // 走过的事件
看结构     uhi::describe<Tree>()            // 编译期算好的一段字
```

## 绑定那一处，具体长什么样

```cpp
// uhi.hpp:614
template <class Tree, std::size_t G, class Arena>
void on_event(void *self, const event &e) noexcept
{
    static constexpr auto name = cat("on_", plan_kinds_v<Tree>[G].name);  // "on_click"
    static_cast<Arena *>(self)->template invoke<name>(e);
}

// uhi.hpp:643
template <class Arena>
void bind(Arena &a) noexcept
{
    walk_leaves<0>(tree, [&](auto idx, auto &) {
        constexpr std::size_t g = decltype(idx)::value;
        sinks[g] = sink{&a, &on_event<Tree, g, Arena>};   // 两字，无捕获
    });
}
```

方法名 `on_<kind>` 是**编译期拼出来**的，所以加一个 kind，就多一个
必须存在的 `method<"on_<kind>">`——漏了直接编译失败（`invoke` 找不到成员）。

## 编译期算出来的东西，全都能打印

```
uhi::describe<tree_mixed>()
relations: click->dbl click~drag dbl~drag
cells: mouse.left{arbitrate:dbl,drag,click} keyboard{direct:key_A}
slots:
  cell[0] mouse.left : dbl drag click
  cell[1] keyboard : key_A
stages = 2, sinks = 4, sizeof(ui) = 2792B
```

（`sizeof` 大头是 64 条记录缓冲；生产时可以调小或关掉。）

## 怎么读这本书

| 章 | 回答什么 |
|---|---|
| [00 目标](00-goal.md) | 要解决什么、不解决什么、唯一的核心不变式 |
| [01 概念](01-concepts.md) | 概念的精确定义，以及每个“不是什么” |
| [02 定律](02-laws.md) | 六条必须成立的定律，以及各自在哪验证 |
| [03 角色](03-roles.md) | 工厂图：谁负责什么、谁是谁的上游 |
| [04 设计空间](04-design-space.md) | 哪些分支可能、哪些不可能、哪些已否决（含理由） |
| [05 实现映射](05-implementation.md) | 概念 → 符号 → 代码行 |
| [06 验收](06-acceptance.md) | 四层证明，各自能证明什么、不能证明什么 |

**只读一页的话**：读 [04 设计空间](04-design-space.md)——那里写着为什么现在是这个样子。

## 版本

```
uhi/
  README.md            ← 你在这里
  00-goal.md … 06-acceptance.md
  v1/src/              v1：库 + 玩具帧 + 载荷 + arena 集成
    uhi.hpp
    test_uhi.cpp           同一个库，玩具帧（证明库不认识事件类型）
    test_uhi_arena.cpp     真 arena_event，ui 作为 arena 字段
    test_uhi_reject.cpp    三个必须编译失败的结构
  v2/                  v2：集成契约 + 手写 arena + 投递链验证
    README.md
    src/uhi.hpp            与 v1 逐字节相同
    src/uhi_integration.hpp  两个名字：uhi::subscribe / method<"input_event">
    src/arena_event.hpp      输入层（build_change / apply_change）
    src/gestures.hpp         四个手势 FSM + leaf
    src/test_util.hpp        at / expect / glfw_post
    src/test_handwritten.cpp 4 个手写 arena（没有工厂函数）
    src/test_translation.cpp 翻译矩阵：GLFW 全部取值 ⇒ ChangeType（2237 项）
    src/test_delivery.cpp    5 场景 + 语义表 + 穷举 + 随机
  v3/src/              v3：liveness + 多指 + 综合试跑
    uhi.hpp
    arena_event.hpp / gestures.hpp / uhi_integration.hpp / test_util.hpp
    test_liveness.cpp       tick / hold（兜底只判负，不发明赢家）
    test_multitouch.cpp     pool<Tree,N>：每指一台机器
    test_tour.cpp           ★ 综合：一个程序走完 14 项能力，每步审计
```

| 版本 | 内容 | 状态 |
|---|---|---|
| **v1** | 结构推导 + 帧内裁决 + `ui`/`bind`/`feed`/`events`/`describe` + 载荷 + arena 集成 | ✅ 0 警告 |
| **v2** | 集成契约（`uhi::subscribe` + `method<"input_event">`）+ 手写 arena + 真分发器投递链 + 翻译矩阵 + 穷举/随机 | ✅ 0 警告 |
| **v3** | `ui.tick(now)`（只有时间）+ `leaf<…, HoldMs>` 兜底 + `pool<Tree,N>` 多指 | ✅ 0 警告 |

## 跑一遍

```powershell
cd E:\0_github_project\mcsvulkan\test\mcsvulkan\wsi
g++ -std=c++26 -freflection -O0 -g uhi\v1\src\test_uhi.cpp -o output/test_uhi.exe -lstdc++exp
.\output\test_uhi.exe

# 坏结构：三个都应当编译失败
g++ -std=c++26 -freflection "-DCASE=1" -fsyntax-only uhi\v1\src\test_uhi_reject.cpp
```
