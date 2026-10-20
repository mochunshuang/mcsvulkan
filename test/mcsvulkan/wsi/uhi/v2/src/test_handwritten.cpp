// ═══════════════════════════════════════════════════════════════════════
//  test_handwritten.cpp —— **不使用任何 arena 工厂**
//
//  每个用例都自己写一整段 make_aggregate<"arena">(...)，像手写 struct 那样：
//  字段叫什么、有几个 handler、input_event 里做什么策略，都由用例自己定。
//  make_aggregate 只是"定义一个类型"，所以它和手写 struct 是等价的——
//  这个文件要证明的就是这一点。
//
//  唯一固定的东西只有两个名字（集成契约）：
//      method<"input_event">        arena 的输入入口（uhi::subscribe 调它）
//      ui.bind(self)               ui 把收件人接到 arena 自己身上
//  每个用例都用 static_assert(uhi::accepts_platform_events_v<...>) 检查契约。
//
//  编译：
//    g++ -std=c++26 -freflection -O0 -g uhi\v2\src\test_handwritten.cpp -o output/test_handwritten.exe -lstdc++exp
// ═══════════════════════════════════════════════════════════════════════
#include <cassert>
#include <print>

#include "../../../../../../include/detail/meta/make_aggregate.hpp"
#include "arena_event.hpp"
#include "gestures.hpp"
#include "test_util.hpp"
#include "uhi.hpp"
#include "uhi_integration.hpp"

using mcs::vulkan::meta::field;
using mcs::vulkan::meta::make_aggregate;
using mcs::vulkan::meta::method;
using uhi::refines;
using uhi::time_point;

using gesture::click_event;
using gesture::g_click;
using gesture::g_dbl;
using gesture::g_drag;
using gesture::g_key;

// ═══════════════════════════════════════════════════════════════════════
// ① 最小：只有一个 click。等价于手写
//      struct arena { time_point now; event_context ctx; ui<g_click> ui;
//                     int hits; double spot; void on_click(...); ... };
// ═══════════════════════════════════════════════════════════════════════
void t1_single_click()
{
    using tree = g_click;

    auto arena = make_aggregate<"arena">(
        field<"now">(time_point{}),
        field<"ctx">(arena_event::event_context{}),
        field<"ui">(uhi::ui<tree>{}),
        field<"hits">(int{}),
        field<"spot">(double{}),

        method<"on_click">([](auto &self, const click_event &e) noexcept {
            ++self.hits;
            self.spot = e.x;
        }),
        method<"bind">([](auto &self) noexcept { self.ui.bind(self); }),
        method<"input_event">([](auto &self, auto e) noexcept {
            auto ch = arena_event::build_change(self.ctx, e);
            if (ch.change == arena_event::ChangeType::None) return;
            ch.time_point = self.now;
            self.ui.feed(arena_event::input_frame{self.ctx, ch});
            arena_event::apply_change(self.ctx, ch);
        }));

    static_assert(uhi::accepts_platform_events_v<decltype(arena)>);

    arena.invoke<"bind">();
    auto conn = uhi::subscribe(arena);

    glfw_post(arena, 0, p2{42.0, 0.0});
    glfw_post(arena, 10, mb{L, PRESS});
    glfw_post(arena, 60, mb{L, RELEASE});

    expect(uhi::to_string(arena.ui.events()), "click:acc@60", "t1");
    assert(arena.hits == 1 && arena.spot == 42.0);
    std::println("t1 单手势        {}   → hits={} spot={}", uhi::to_string(arena.ui.events()),
                 arena.hits, arena.spot);
}

// ═══════════════════════════════════════════════════════════════════════
// ② click + dbl（细化）：arena 自己多记一个 frames 计数器
// ═══════════════════════════════════════════════════════════════════════
void t2_refine_two_gestures()
{
    using tree = refines<g_click, g_dbl>;

    auto arena = make_aggregate<"arena">(
        field<"now">(time_point{}),
        field<"ctx">(arena_event::event_context{}),
        field<"ui">(uhi::ui<tree>{}),
        field<"frames">(int{}), // ← arena 自己的记账
        field<"singles">(int{}),
        field<"doubles">(int{}),

        method<"on_click">([](auto &self, const click_event &) noexcept { ++self.singles; }),
        method<"on_dbl">([](auto &self) noexcept { ++self.doubles; }),
        method<"bind">([](auto &self) noexcept { self.ui.bind(self); }),
        method<"input_event">([](auto &self, auto e) noexcept {
            auto ch = arena_event::build_change(self.ctx, e);
            if (ch.change == arena_event::ChangeType::None) return;
            ch.time_point = self.now;
            ++self.frames; // 每喂一帧记一次
            self.ui.feed(arena_event::input_frame{self.ctx, ch});
            arena_event::apply_change(self.ctx, ch);
        }));

    static_assert(uhi::accepts_platform_events_v<decltype(arena)>);

    arena.invoke<"bind">();
    auto conn = uhi::subscribe(arena);

    // 第一次：单击，等 dbl 窗口过期
    glfw_post(arena, 0, p2{0.0, 0.0});
    glfw_post(arena, 10, mb{L, PRESS});
    glfw_post(arena, 60, mb{L, RELEASE});
    assert(arena.ui.events().empty());
    glfw_post(arena, 360, p2{0.0, 0.0});
    expect(uhi::to_string(arena.ui.events()), "click:acc@360", "t2a");

    // 第二次：双击
    arena.ui.clear_events();
    glfw_post(arena, 400, mb{L, PRESS});
    glfw_post(arena, 450, mb{L, RELEASE});
    glfw_post(arena, 500, mb{L, PRESS});
    glfw_post(arena, 550, mb{L, RELEASE});
    expect(uhi::to_string(arena.ui.events()), "click:sup@550, dbl:acc@550", "t2b");

    assert(arena.singles == 1 && arena.doubles == 1);
    std::println("t2 细化          singles={} doubles={} frames={}", arena.singles,
                 arena.doubles, arena.frames);
}

// ═══════════════════════════════════════════════════════════════════════
// ③ click + drag（互斥）：**arena 自己定策略**——输入里忽略右键
// ═══════════════════════════════════════════════════════════════════════
void t3_rival_with_policy()
{
    using tree = uhi::one_of<g_click, g_drag>;

    auto arena = make_aggregate<"arena">(
        field<"now">(time_point{}),
        field<"ctx">(arena_event::event_context{}),
        field<"ui">(uhi::ui<tree>{}),
        field<"ignored">(int{}), // ← 被策略挡掉的事件
        field<"taps">(int{}),
        field<"pulls">(int{}),

        method<"on_click">([](auto &self, const click_event &) noexcept { ++self.taps; }),
        method<"on_drag">([](auto &self) noexcept { ++self.pulls; }),
        method<"bind">([](auto &self) noexcept { self.ui.bind(self); }),
        method<"input_event">([](auto &self, auto e) noexcept {
            auto ch = arena_event::build_change(self.ctx, e);
            if (ch.change == arena_event::ChangeType::None) return;
            // 策略：右键一律不进手势
            if (ch.change == arena_event::ChangeType::PointerButton && !ch.button.is_left())
            {
                ++self.ignored;
                return;
            }
            ch.time_point = self.now;
            self.ui.feed(arena_event::input_frame{self.ctx, ch});
            arena_event::apply_change(self.ctx, ch);
        }));

    static_assert(uhi::accepts_platform_events_v<decltype(arena)>);

    arena.invoke<"bind">();
    auto conn = uhi::subscribe(arena);

    glfw_post(arena, 0, p2{0.0, 0.0});
    glfw_post(arena, 10, mb{R, PRESS});   // 被策略挡掉
    glfw_post(arena, 20, mb{R, RELEASE}); // 被策略挡掉
    assert(arena.ignored == 2 && arena.ui.events().empty());

    glfw_post(arena, 100, mb{L, PRESS});
    glfw_post(arena, 160, p2{100.0, 0.0}); // 拖动 ⇒ drag 赢
    expect(uhi::to_string(arena.ui.events()), "click:sup@160, drag:acc@160", "t3");

    assert(arena.taps == 0 && arena.pulls == 1 && arena.ignored == 2);
    std::println("t3 互斥+策略     taps={} pulls={} ignored={}", arena.taps, arena.pulls,
                 arena.ignored);
}

// ═══════════════════════════════════════════════════════════════════════
// ④ 全套：两个 cell（鼠标裁决 / 键盘直呼），每个 kind 一个字段
// ═══════════════════════════════════════════════════════════════════════
void t4_full()
{
    using tree = uhi::all_of<uhi::one_of<refines<g_click, g_dbl>, g_drag>, g_key>;

    auto arena = make_aggregate<"arena">(
        field<"now">(time_point{}),
        field<"ctx">(arena_event::event_context{}),
        field<"ui">(uhi::ui<tree>{}),
        field<"clicks">(int{}),
        field<"dbls">(int{}),
        field<"drags">(int{}),
        field<"keys">(int{}),
        field<"last_x">(double{}),

        method<"on_click">([](auto &self, const click_event &e) noexcept {
            ++self.clicks;
            self.last_x = e.x;
        }),
        method<"on_dbl">([](auto &self) noexcept { ++self.dbls; }),
        method<"on_drag">([](auto &self) noexcept { ++self.drags; }),
        method<"on_key_A">([](auto &self) noexcept { ++self.keys; }),
        method<"bind">([](auto &self) noexcept { self.ui.bind(self); }),
        method<"input_event">([](auto &self, auto e) noexcept {
            auto ch = arena_event::build_change(self.ctx, e);
            if (ch.change == arena_event::ChangeType::None) return;
            ch.time_point = self.now;
            self.ui.feed(arena_event::input_frame{self.ctx, ch});
            arena_event::apply_change(self.ctx, ch);
        }));

    static_assert(uhi::accepts_platform_events_v<decltype(arena)>);

    arena.invoke<"bind">();
    auto conn = uhi::subscribe(arena);

    glfw_post(arena, 0, p2{15.0, 0.0});
    glfw_post(arena, 10, mb{L, PRESS});
    glfw_post(arena, 20, kb{ae::Key::eA, PRESS}); // 另一个 cell ⇒ 立刻
    expect(uhi::to_string(arena.ui.events()), "key_A:acc@20", "t4a");

    glfw_post(arena, 60, mb{L, RELEASE});
    glfw_post(arena, 360, p2{15.0, 0.0}); // dbl 过期 ⇒ click
    expect(uhi::to_string(arena.ui.events()), "key_A:acc@20, click:acc@360", "t4b");

    assert(arena.clicks == 1 && arena.keys == 1 && arena.last_x == 15.0);
    std::println("t4 全套          clicks={} keys={} last_x={}", arena.clicks, arena.keys,
                 arena.last_x);
}

// ═══════════════════════════════════════════════════════════════════════
int main()
{
    std::println("=== 手写 arena（没有任何工厂函数）===");
    t1_single_click();
    t2_refine_two_gestures();
    t3_rival_with_policy();
    t4_full();
    std::println("=== 四个手写用例全部通过 ===");
    return 0;
}
