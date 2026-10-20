// ═══════════════════════════════════════════════════════════════════════
//  test_uhi.cpp —— 四个场景。每个场景：
//
//    auto arena = make_aggregate<"arena">(… method<"on_click">(…));
//    uhi::ui<tree> ui{};
//    ui.bind(arena);                  // 延迟绑定：两字函数指针接到 arena 的 self
//    run(ui, {{0,press},{50,release}});   // ← 测试自己的脚本器，不是库的一部分
//    assert(arena.clicks == 1);           // handler 改的是 arena 自己的字段
//
//  编译：
//    g++ -std=c++26 -freflection -O0 -g uhi\v1\src\test_uhi.cpp -o output/test_uhi.exe -lstdc++exp
// ═══════════════════════════════════════════════════════════════════════
#include <cassert>
#include <chrono>
#include <cstdint>
#include <print>
#include <string>
#include <string_view>

#include "uhi.hpp"

#include "../../../../../../include/detail/meta/make_aggregate.hpp"

using namespace std::chrono_literals;
using mcs::vulkan::meta::field;
using mcs::vulkan::meta::make_aggregate;
using mcs::vulkan::meta::method;
using uhi::kind_t;
using uhi::leaf;
using uhi::refines;
using uhi::source_t;
using uhi::step_t;
using uhi::time_point;

// ═══════════════════════════════════════════════════════════════════════
// ① 输入帧：一个变化 + 它的时间（与 arena_event::input_frame 同形）
// ═══════════════════════════════════════════════════════════════════════
struct frame
{
    enum class ev : std::uint8_t { none, press, release, move, key } e = ev::none;
    double                                                          v = 0.0; // 这一帧的标量（位置/位移）
    time_point                                                      t{};
};
constexpr frame press{frame::ev::press};
constexpr frame release{frame::ev::release};
constexpr frame key{frame::ev::key};
constexpr frame none{frame::ev::none};
inline frame    move(double v) { return {frame::ev::move, v, {}}; }
inline frame    press_at(double v) { return {frame::ev::press, v, {}}; }

// 载荷：click 带坐标
struct click_event
{
    double x = 0.0;
};

// 库唯一需要知道的事：这一帧发生在什么时候
template <>
struct uhi::frame_traits<frame>
{
    static uhi::time_point when(const frame &f) noexcept { return f.t; }
};

inline time_point at(int ms) { return time_point{} + std::chrono::milliseconds{ms}; }

// 测试自己的脚本器：给一帧打上时间，喂进去。**不属于库**。
template <class UI>
void run(UI &ui, std::initializer_list<std::pair<int, frame>> script)
{
    for (const auto &step : script)
    {
        frame f = step.second;
        f.t     = at(step.first);
        ui.feed(f);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// ② 四个手势：只有状态机，没有裁决、没有关系
// ═══════════════════════════════════════════════════════════════════════
struct click_fsm
{
    bool        s = false;
    click_event payload{};
    step_t      step(const frame &f, time_point) noexcept
    {
        if (!s)
        {
            if (f.e == frame::ev::press)
            {
                s         = true;
                payload.x = f.v; // 记下按下时的位置
                return step_t::active;
            }
            return step_t::idle;
        }
        if (f.e == frame::ev::release) { s = false; return step_t::done; }
        return step_t::active;
    }
    void reset() noexcept { s = false; }
};

struct dbl_fsm
{
    enum class st : std::uint8_t { idle, first_press, wait_second, second_press };
    st         s = st::idle;
    time_point deadline{};
    static constexpr auto window = 300ms;

    step_t step(const frame &f, time_point now) noexcept
    {
        if (s == st::wait_second && now >= deadline) { s = st::idle; return step_t::aborted; }
        switch (s)
        {
        case st::idle:
            if (f.e == frame::ev::press) { s = st::first_press; return step_t::active; }
            return step_t::idle;
        case st::first_press:
            if (f.e == frame::ev::release) { s = st::wait_second; deadline = now + window; }
            return step_t::active;
        case st::wait_second:
            if (f.e == frame::ev::press) s = st::second_press;
            return step_t::active;
        case st::second_press:
            if (f.e == frame::ev::release) { s = st::idle; return step_t::done; }
            return step_t::active;
        }
        return step_t::idle;
    }
    void reset() noexcept { s = st::idle; }
};

struct drag_fsm
{
    bool   armed = false;
    double acc = 0.0;
    step_t step(const frame &f, time_point) noexcept
    {
        if (f.e == frame::ev::press) { armed = true; acc = 0.0; return step_t::active; }
        if (!armed) return step_t::idle;
        if (f.e == frame::ev::move)
        {
            acc += (f.v < 0 ? -f.v : f.v);
            return acc > 12.0 ? step_t::done : step_t::active;
        }
        if (f.e == frame::ev::release) { armed = false; return step_t::aborted; }
        return step_t::active;
    }
    void reset() noexcept { armed = false; acc = 0.0; }
};

struct key_fsm
{
    step_t step(const frame &f, time_point) noexcept
    {
        return f.e == frame::ev::key ? step_t::done : step_t::idle;
    }
    void reset() noexcept {}
};

// ═══════════════════════════════════════════════════════════════════════
// ③ 四棵树：同一批手势，不同的结构
// ═══════════════════════════════════════════════════════════════════════
using mouse_src = source_t<"mouse.left">;
using key_src   = source_t<"keyboard">;

using g_click = leaf<kind_t<"click", click_event>, mouse_src, click_fsm>;
using g_dbl   = leaf<kind_t<"dbl">, mouse_src, dbl_fsm>;
using g_drag  = leaf<kind_t<"drag">, mouse_src, drag_fsm>;
using g_key   = leaf<kind_t<"key_A">, key_src, key_fsm>;

using tree_parallel  = uhi::all_of<g_click, g_key>;
using tree_refine    = refines<g_click, g_dbl>;
using tree_exclusive = uhi::one_of<g_click, g_drag>;
using tree_mixed     = uhi::all_of<uhi::one_of<refines<g_click, g_dbl>, g_drag>, g_key>;

// ═══════════════════════════════════════════════════════════════════════
// ④ 编译期自检：蓝图必须逐字等于预期
// ═══════════════════════════════════════════════════════════════════════
static_assert(uhi::ui<tree_parallel>::blueprint.view() ==
              "relations: (none)\n"
              "cells: mouse.left{direct:click} keyboard{direct:key_A}");

static_assert(uhi::ui<tree_refine>::blueprint.view() ==
              "relations: click->dbl\n"
              "cells: mouse.left{arbitrate:dbl,click}");

static_assert(uhi::ui<tree_exclusive>::blueprint.view() ==
              "relations: click~drag\n"
              "cells: mouse.left{arbitrate:click,drag}");

static_assert(uhi::ui<tree_mixed>::blueprint.view() ==
              "relations: click->dbl click~drag dbl~drag\n"
              "cells: mouse.left{arbitrate:dbl,drag,click} keyboard{direct:key_A}");

static_assert(uhi::leaf_count_v<tree_mixed> == 4);
static_assert(uhi::plan_kinds_v<tree_mixed>[0].name.view() == "click");
static_assert(uhi::plan_kinds_v<tree_mixed>[1].name.view() == "dbl");
static_assert(uhi::plan_kinds_v<tree_mixed>[2].name.view() == "drag");
static_assert(uhi::plan_kinds_v<tree_mixed>[3].name.view() == "key_A");

// ═══════════════════════════════════════════════════════════════════════
// ⑤ arena：普通的 make_aggregate，handler 改自己的字段
// ═══════════════════════════════════════════════════════════════════════
inline auto make_arena()
{
    return make_aggregate<"arena">(
        field<"clicks">(int{}),
        field<"dbls">(int{}),
        field<"drags">(int{}),
        field<"keys">(int{}),
        field<"click_x">(double{}),
        // 带载荷的 kind：handler 收 const click_event&
        method<"on_click">([](auto &self, const click_event &e) noexcept {
            ++self.clicks;
            self.click_x = e.x;
        }),
        // 不带载荷的 kind：handler 只收 self
        method<"on_dbl">([](auto &self) noexcept { ++self.dbls; }),
        method<"on_drag">([](auto &self) noexcept { ++self.drags; }),
        method<"on_key_A">([](auto &self) noexcept { ++self.keys; }));
}

inline void expect(std::string_view got, std::string_view want, const char *where)
{
    if (got == want) return;
    std::print(stderr, "{}:\n  got  [{}]\n  want [{}]\n", where, got, want);
    assert(false && "unexpected trace");
}

// ═══════════════════════════════════════════════════════════════════════
// ⑥ 四场戏
// ═══════════════════════════════════════════════════════════════════════

// 没有纠纷 ⇒ 没有裁决。click 在 release 当帧就发。
void s1_parallel()
{
    auto                   arena = make_arena();
    uhi::ui<tree_parallel> ui{};
    ui.bind(arena);
    run(ui, {{0, press_at(7.0)}, {50, release}, {100, key}});
    auto s = uhi::to_string(ui.events());
    expect(s, "click:acc@50, key_A:acc@100", "S1");
    assert(arena.clicks == 1 && arena.keys == 1 && arena.dbls == 0);
    assert(arena.click_x == 7.0); // ← 载荷从 FSM 穿过函数指针到达 arena
    std::println("S1 并行       {}   → arena: clicks={} click_x={} keys={}", s, arena.clicks,
                 arena.click_x, arena.keys);
}

// 细化：click 让路给 dbl，release 后要等 dbl 的窗口过期。
void s2_refine()
{
    auto                 arena = make_arena();
    uhi::ui<tree_refine> ui{};
    ui.bind(arena);
    run(ui, {{0, press}, {50, release}, {349, none}, {350, none}});
    auto s = uhi::to_string(ui.events());
    expect(s, "click:acc@350", "S2 单击延迟");
    assert(arena.clicks == 1 && arena.dbls == 0);
    std::println("S2 细化(单击) {}   → arena: clicks={}", s, arena.clicks);

    ui.clear_events();
    run(ui, {{500, press}, {550, release}, {600, press}, {650, release}});
    s = uhi::to_string(ui.events());
    expect(s, "click:sup@650, dbl:acc@650", "S2b 双击");
    assert(arena.clicks == 1 && arena.dbls == 1); // 被 superseded 的那次不回调
    std::println("S2 细化(双击) {}   → arena: clicks={} dbls={}", s, arena.clicks, arena.dbls);
}

// 互斥：先到先得，两个方向都验。
void s3_exclusive()
{
    auto                    arena = make_arena();
    uhi::ui<tree_exclusive> ui{};
    ui.bind(arena);
    run(ui, {{0, press_at(42.0)}, {50, release}});
    auto s = uhi::to_string(ui.events());
    // drag 在 release 这一帧自己放弃了（静默），没有被谁压掉 ⇒ 只有 click 一条
    expect(s, "click:acc@50", "S3a 点");
    assert(arena.clicks == 1 && arena.drags == 0);
    assert(arena.click_x == 42.0);
    std::println("S3 互斥(点)   {}   → arena: clicks={} click_x={}", s, arena.clicks,
                 arena.click_x);

    ui.clear_events();
    run(ui, {{100, press}, {160, move(100.0)}});
    s = uhi::to_string(ui.events());
    expect(s, "click:sup@160, drag:acc@160", "S3b 拖");
    assert(arena.clicks == 1 && arena.drags == 1);
    std::println("S3 互斥(拖)   {}   → arena: drags={}", s, arena.drags);
}

// 混合：同一套系统里，一个 cell 在裁决，另一个在直呼。
void s4_mixed()
{
    auto                arena = make_arena();
    uhi::ui<tree_mixed> ui{};
    ui.bind(arena);
    run(ui, {{0, press}, {10, key}, {20, release}, {319, none}, {320, none}});
    auto s = uhi::to_string(ui.events());
    expect(s, "key_A:acc@10, click:acc@320", "S4");
    assert(arena.keys == 1 && arena.clicks == 1);
    std::println("S4 混合       {}   → arena: clicks={} keys={}", s, arena.clicks, arena.keys);
}

// ═══════════════════════════════════════════════════════════════════════
int main()
{
    std::println("=== 编译期算出来的结构 ===");
    std::println("{}\n", uhi::describe<tree_mixed>().view());

    s1_parallel();
    s2_refine();
    s3_exclusive();
    s4_mixed();
    std::println("=== 四场戏全部通过 ===");
    return 0;
}
