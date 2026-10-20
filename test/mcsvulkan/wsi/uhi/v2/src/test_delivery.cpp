// ═══════════════════════════════════════════════════════════════════════
//  test_delivery.cpp —— v2 的验收：arena_event 的**投递链**
//
//  链路上每一跳都是你现成的机制，没有为测试改造过：
//
//    GLFW 回调（这里手工模拟）
//      → mcs::vulkan::event::*_dispatcher::instance().distribute(e)   真分发器
//      → dispatcher_bridge::forward<E>                               订阅者（函数指针）
//      → proxy_class_invoke<>::invoke(arena, runtime_type_v<E>, &e)  你的类型擦除派发
//      → arena.invoke<"input_event">(e)                              反射按名调用
//      → arena_event::build_change → ui.feed(input_frame) → apply_change
//      → cell 结算 → on_<kind>(self, payload) → arena 的字段
//
//  编译期保证（static_assert）：data_tuple 里每个事件类型都能落到
//  arena 的 input_event 上，且是 noexcept —— 这正是 dispach_invoke 的
//  can_noexcept_invoke 判据，能过就说明这条投递是编译期确定的。
//
//  编译：
//    g++ -std=c++26 -freflection -O0 -g uhi\v2\src\test_delivery.cpp -o output/test_delivery.exe -lstdc++exp
// ═══════════════════════════════════════════════════════════════════════
#include <cassert>
#include <chrono>
#include <cstdint>
#include <print>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "uhi.hpp"

#include "../../../../../../include/detail/event/keyboard_event_dispatcher.hpp"
#include "../../../../../../include/detail/event/mousebutton_event_dispatcher.hpp"
#include "../../../../../../include/detail/event/cursor_pos_event_dispatcher.hpp"
#include "../../../../../../include/detail/event/cursor_enter_event_dispatcher.hpp"
#include "../../../../../../include/detail/event/window_size_event_dispatcher.hpp"
#include "../../../../../../include/detail/meta/class_utils.hpp"
#include "../../../../../../include/detail/meta/make_aggregate.hpp"
#include "../../../../../../include/detail/utils/runtime_type.hpp"

using namespace std::chrono_literals;
using mcs::vulkan::meta::class_utils;
using mcs::vulkan::meta::field;
using mcs::vulkan::meta::make_aggregate;
using mcs::vulkan::meta::method;
using mcs::vulkan::meta::static_string;
using mcs::vulkan::runtime_type_v;
using uhi::kind_t;
using uhi::leaf;
using uhi::refines;
using uhi::source_t;
using uhi::step_t;
using uhi::time_point;
namespace event = mcs::vulkan::event;

// ═══════════════════════════════════════════════════════════════════════
#include "uhi_integration.hpp"
#include "arena_event.hpp"

// ═══════════════════════════════════════════════════════════════════════
// ③ 手势
// ═══════════════════════════════════════════════════════════════════════
struct click_event
{
    double x = 0.0;
};

struct click_fsm
{
    bool        s = false;
    click_event payload{};
    step_t      step(const ae::input_frame &f, time_point) noexcept
    {
        const auto &c = f.change;
        if (c.change != ae::ChangeType::PointerButton || !c.button.is_left())
            return s ? step_t::active : step_t::idle;
        if (!s && c.button.is_press())
        {
            s = true; payload.x = f.context.pointer_x;
            return step_t::active;
        }
        if (s && c.button.is_pelease()) { s = false; return step_t::done; }
        return s ? step_t::active : step_t::idle;
    }
    void reset() noexcept { s = false; }
};

struct dbl_fsm
{
    enum class st : std::uint8_t { idle, first_press, wait_second, second_press };
    st         s = st::idle;
    time_point deadline{};
    static constexpr auto window = 300ms;
    step_t step(const ae::input_frame &f, time_point now) noexcept
    {
        if (s == st::wait_second && now >= deadline) { s = st::idle; return step_t::aborted; }
        const auto &c = f.change;
        if (c.change != ae::ChangeType::PointerButton || !c.button.is_left())
            return s == st::idle ? step_t::idle : step_t::active;
        switch (s)
        {
        case st::idle:
            if (c.button.is_press()) { s = st::first_press; return step_t::active; }
            return step_t::idle;
        case st::first_press:
            if (c.button.is_pelease()) { s = st::wait_second; deadline = now + window; }
            return step_t::active;
        case st::wait_second:
            if (c.button.is_press()) s = st::second_press;
            return step_t::active;
        case st::second_press:
            if (c.button.is_pelease()) { s = st::idle; return step_t::done; }
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
    step_t step(const ae::input_frame &f, time_point) noexcept
    {
        const auto &c = f.change;
        if (c.change == ae::ChangeType::PointerButton && c.button.is_left())
        {
            if (c.button.is_press()) { armed = true; acc = 0.0; return step_t::active; }
            if (c.button.is_pelease()) { armed = false; return step_t::aborted; }
        }
        if (!armed) return step_t::idle;
        if (c.change == ae::ChangeType::PointerDownMove)
        {
            acc += (c.move.dx < 0 ? -c.move.dx : c.move.dx) +
                   (c.move.dy < 0 ? -c.move.dy : c.move.dy);
            return acc > 12.0 ? step_t::done : step_t::active;
        }
        return step_t::active;
    }
    void reset() noexcept { armed = false; acc = 0.0; }
};

struct key_fsm
{
    step_t step(const ae::input_frame &f, time_point) noexcept
    {
        const auto &c = f.change;
        if (c.change == ae::ChangeType::Key && c.key.key == ae::Key::eA && c.key.is_press())
            return step_t::done;
        return step_t::idle;
    }
    void reset() noexcept {}
};

// ═══════════════════════════════════════════════════════════════════════
// ④ 结构
// ═══════════════════════════════════════════════════════════════════════
using mouse_src = source_t<"mouse.left">;
using key_src   = source_t<"keyboard">;

using g_click = leaf<kind_t<"click", click_event>, mouse_src, click_fsm>;
using g_dbl   = leaf<kind_t<"dbl">, mouse_src, dbl_fsm>;
using g_drag  = leaf<kind_t<"drag">, mouse_src, drag_fsm>;
using g_key   = leaf<kind_t<"key_A">, key_src, key_fsm>;

using tree_click = g_click;
using tree_mixed = uhi::all_of<uhi::one_of<refines<g_click, g_dbl>, g_drag>, g_key>;

// ═══════════════════════════════════════════════════════════════════════
// ⑤ arena：ui 是字段，input_event 只收一个事件（时间来自 arena 的 now）
// ═══════════════════════════════════════════════════════════════════════
template <class Tree>
inline auto make_arena()
{
    return make_aggregate<"arena">(
        field<"now">(time_point{}),            // 时钟是 arena 的字段（测试可注入）
        field<"ctx">(ae::event_context{}),
        field<"ui">(uhi::ui<Tree>{}),
        field<"clicks">(int{}),
        field<"dbls">(int{}),
        field<"drags">(int{}),
        field<"keys">(int{}),
        field<"click_x">(double{}),
        field<"unknown">(int{}),               // 记下"不认识的事件"到达了多少次

        method<"on_click">([](auto &self, const click_event &e) noexcept {
            ++self.clicks;
            self.click_x = e.x;
        }),
        method<"on_dbl">([](auto &self) noexcept { ++self.dbls; }),
        method<"on_drag">([](auto &self) noexcept { ++self.drags; }),
        method<"on_key_A">([](auto &self) noexcept { ++self.keys; }),

        method<"bind">([](auto &self) noexcept { self.ui.bind(self); }),

        // ← 投递的终点：只有一个事件参数，时间读 arena 自己的 now
        method<"input_event">([](auto &self, auto e) noexcept {
            auto ch = ae::build_change(self.ctx, e);
            ++self.unknown;                    // 证明每一跳都到了这里
            if (ch.change == ae::ChangeType::None) return; // 不认识的类型：安静丢掉
            ch.time_point = self.now;
            self.ui.feed(ae::input_frame{self.ctx, ch});
            ae::apply_change(self.ctx, ch);
        }));
}

// ═══════════════════════════════════════════════════════════════════════
// ⑦ 编译期保证
//   (a) 全类型投递：data_tuple 里每个事件都必须能 noexcept 落到 input_event
//   (b) 裁剪：只收 keyboard 的成员，其它类型在编译期就 invokable=false
//       ⇒ dispach_invoke 会把它丢掉（这就是"丢弃不能投递的类型"）
//   (c) ui 侧：feed 只收 frame_like
// ═══════════════════════════════════════════════════════════════════════
using arena_t = decltype(make_arena<tree_mixed>());

// (a) 全类型投递：platform_events 里每一种都必须能落到 input_event
static_assert(uhi::accepts_platform_events_v<arena_t>);


// 一个"只收 keyboard_event"的 arena：用来证明按类型裁剪是真的
inline auto make_keyboard_only_arena()
{
    return make_aggregate<"kb_only">(
        field<"n">(int{}),
        method<"input_event">([](auto &self, const event::keyboard_event &) noexcept {
            ++self.n;
        }));
}
using kb_only_t = decltype(make_keyboard_only_arena());

// (b) 裁剪：同一个桥会投递 5 种类型，但只收 keyboard 的成员只认得其中 1 种，
//     其余 4 种在编译期就不满足 can_noexcept_invoke ⇒ 被 dispach_invoke 丢掉
static_assert(uhi::invoke_member_help::can_noexcept_invoke<"input_event", kb_only_t,
                                                      event::keyboard_event>());
static_assert(!uhi::invoke_member_help::can_noexcept_invoke<"input_event", kb_only_t,
                                                       event::mousebutton_event>());
static_assert(!uhi::invoke_member_help::can_noexcept_invoke<"input_event", kb_only_t,
                                                       event::position2d_event>());
// (c) ui 侧：帧类型不认识就进不来
static_assert(uhi::frame_like<ae::input_frame>);
static_assert(!uhi::frame_like<int>);
static_assert(!uhi::frame_like<event::keyboard_event>);

// 蓝图
static_assert(uhi::ui<tree_mixed>::blueprint.view() ==
              "relations: click->dbl click~drag dbl~drag\n"
              "cells: mouse.left{arbitrate:dbl,drag,click} keyboard{direct:key_A}");

// ═══════════════════════════════════════════════════════════════════════
// ⑧ 场景：手工模拟 GLFW 会送来的事件，走真分发器
// ═══════════════════════════════════════════════════════════════════════
#include "test_util.hpp"

// 场景：手工模拟 GLFW 会送来的事件，走真分发器

// D1：一个 click 走完整条链
void d1_click()
{
    auto arena = make_arena<tree_click>();
    arena.invoke<"bind">();
    auto conn = uhi::subscribe(arena);

    glfw_post(arena, 0, p2{42.0, 0.0});
    glfw_post(arena, 10, mb{L, PRESS});
    glfw_post(arena, 60, mb{L, RELEASE});

    auto s = uhi::to_string(arena.ui.events());
    expect(s, "click:acc@60", "D1");
    assert(arena.clicks == 1 && arena.click_x == 42.0);
    assert(arena.unknown == 3); // 三跳都真的到了 input_event
    std::println("D1 click          {}   → clicks={} click_x={} delivered={}", s, arena.clicks,
                 arena.click_x, arena.unknown);
}

// D2：不认识的类型（window_size）被安静丢掉
void d2_unknown_dropped()
{
    auto arena = make_arena<tree_click>();
    arena.invoke<"bind">();
    auto conn = uhi::subscribe(arena);

    glfw_post(arena, 0, ws{800, 600});
    auto s = uhi::to_string(arena.ui.events());
    expect(s, "", "D2");
    assert(arena.unknown == 1);  // 到了 input_event
    assert(arena.ui.events().empty()); // 但没有产生帧
    std::println("D2 unknown        [{}]  → delivered={} frames=0", s, arena.unknown);
}

// D3：细化 —— click 让路给 dbl
void d3_refine()
{
    auto arena = make_arena<tree_mixed>();
    arena.invoke<"bind">();
    auto conn = uhi::subscribe(arena);

    glfw_post(arena, 0, p2{0.0, 0.0});
    glfw_post(arena, 10, mb{L, PRESS});
    glfw_post(arena, 60, mb{L, RELEASE});
    assert(arena.ui.events().empty()); // deferred

    glfw_post(arena, 359, p2{0.0, 0.0});
    assert(arena.ui.events().empty());
    glfw_post(arena, 360, p2{0.0, 0.0}); // dbl 窗口过期
    auto s = uhi::to_string(arena.ui.events());
    expect(s, "click:acc@360", "D3");
    assert(arena.clicks == 1);
    std::println("D3 refine(click)  {}   → clicks={}", s, arena.clicks);
}

// D4：混合 —— 键盘立刻发，鼠标那格在等 dbl
void d4_mixed()
{
    auto arena = make_arena<tree_mixed>();
    arena.invoke<"bind">();
    auto conn = uhi::subscribe(arena);

    glfw_post(arena, 0, p2{0.0, 0.0});
    glfw_post(arena, 10, mb{L, PRESS});
    glfw_post(arena, 20, kb{ae::Key::eA, PRESS});
    auto s = uhi::to_string(arena.ui.events());
    expect(s, "key_A:acc@20", "D4a");
    assert(arena.keys == 1 && arena.clicks == 0);

    glfw_post(arena, 60, mb{L, RELEASE});
    glfw_post(arena, 360, p2{0.0, 0.0});
    s = uhi::to_string(arena.ui.events());
    expect(s, "key_A:acc@20, click:acc@360", "D4b");
    assert(arena.clicks == 1 && arena.keys == 1);
    std::println("D4 mixed          {}   → clicks={} keys={}", s, arena.clicks, arena.keys);
}

// D5：互斥 —— 拖拽赢，click 让位
void d5_drag()
{
    auto arena = make_arena<tree_mixed>();
    arena.invoke<"bind">();
    auto conn = uhi::subscribe(arena);

    glfw_post(arena, 0, p2{0.0, 0.0});
    glfw_post(arena, 10, mb{L, PRESS});
    glfw_post(arena, 70, p2{100.0, 0.0}); // 按着移动 ⇒ PointerDownMove，dx=100
    auto s = uhi::to_string(arena.ui.events());
    // one_of 让 drag 同时与 click、dbl 互斥 ⇒ 两个都让位
    expect(s, "dbl:sup@70, click:sup@70, drag:acc@70", "D5");
    assert(arena.drags == 1 && arena.clicks == 0 && arena.dbls == 0);
    std::println("D5 drag           {}   → drags={} clicks={}", s, arena.drags, arena.clicks);
}

// ═══════════════════════════════════════════════════════════════════════
// ⑨ 不变量审计
//
//   每投递一次就把 ui.events() 收干，检查：
//     ① 同一个 cell 里，任意两条 accepted 的时间跨度不重叠（"一次交互一个赢家"）
//     ② 回调次数 == accepted 条数（superseded 不回调、也不重复回调）
//     ③ delivered == posted（每一跳都真的到了 input_event）
// ═══════════════════════════════════════════════════════════════════════
struct audit
{
    std::vector<uhi::event> accepted;
    int                     overlaps = 0;

    static int cell_of(std::string_view n) noexcept { return n == "key_A" ? 1 : 0; }

    template <class Arena>
    void drain(Arena &a)
    {
        for (const auto &e : a.ui.events())
        {
            if (e.v != uhi::verdict::accepted) continue;
            for (const auto &p : accepted)
                if (cell_of(p.name.view()) == cell_of(e.name.view()) &&
                    !(p.end <= e.begin || e.end <= p.begin))
                    ++overlaps;
            accepted.push_back(e);
        }
        a.ui.clear_events();
    }
};

template <class Arena>
void check(Arena &a, const audit &au, std::size_t posted, const char *where)
{
    const int fired = a.clicks + a.dbls + a.drags + a.keys;
    if (au.overlaps != 0 || fired != static_cast<int>(au.accepted.size()) ||
        a.unknown != static_cast<int>(posted))
    {
        std::print(stderr,
                   "{}: overlaps={} fired={} accepted={} delivered={} posted={}\n", where,
                   au.overlaps, fired, au.accepted.size(), a.unknown, posted);
        assert(false && "invariant violated");
    }
}

// ═══════════════════════════════════════════════════════════════════════
// ⑩ 穷举：字母表 {press, release, move, key}，长度 1..6，共 5460 条序列
// ═══════════════════════════════════════════════════════════════════════
enum class sym : std::uint8_t { press, release, move, key };

template <class Arena>
void emit(Arena &a, sym s, int ms, double &x)
{
    switch (s)
    {
    case sym::press:   glfw_post(a, ms, mb{L, PRESS}); break;
    case sym::release: glfw_post(a, ms, mb{L, RELEASE}); break;
    case sym::move:    x += 100.0; glfw_post(a, ms, p2{x, 0.0}); break;
    case sym::key:     glfw_post(a, ms, kb{ae::Key::eA, PRESS}); break;
    }
}

void run_exhaustive()
{
    long sequences = 0, posts = 0, accepted = 0;
    for (int len = 1; len <= 6; ++len)
    {
        const int total = 1 << (2 * len); // 4^len
        for (int code = 0; code < total; ++code)
        {
            auto  a = make_arena<tree_mixed>();
            audit au;
            a.invoke<"bind">();
            auto conn = uhi::subscribe(a);

            int    c  = code;
            int    ms = 0;
            double x  = 0.0;
            for (int i = 0; i < len; ++i)
            {
                const auto s = static_cast<sym>(c & 3);
                c >>= 2;
                ms += 10;
                emit(a, s, ms, x);
                au.drain(a);
                ++posts;
            }
            check(a, au, static_cast<std::size_t>(len), "exhaustive");
            accepted += static_cast<long>(au.accepted.size());
            ++sequences;
        }
    }
    std::println("穷举   : {} 条序列, {} 次投递, {} 个 accepted, 违例 0", sequences, posts,
                 accepted);
}

// ═══════════════════════════════════════════════════════════════════════
// ⑪ 随机：10 万次投递，含右键、按键抬起、window_size（不认识的类型）
// ═══════════════════════════════════════════════════════════════════════
void run_fuzz(int n = 100000)
{
    auto  a = make_arena<tree_mixed>();
    audit au;
    a.invoke<"bind">();
    auto conn = uhi::subscribe(a);

    std::uint32_t seed = 0x12345678;
    auto          rnd  = [&seed]() noexcept {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    };

    int         ms     = 0;
    double      x      = 0.0;
    std::size_t posted = 0;
    for (int i = 0; i < n; ++i)
    {
        ms += (rnd() % 40 == 0) ? 400 : static_cast<int>(rnd() % 15); // 偶尔大跳让窗口过期
        switch (rnd() % 8)
        {
        case 0: glfw_post(a, ms, mb{L, PRESS}); break;
        case 1: glfw_post(a, ms, mb{L, RELEASE}); break;
        case 2: glfw_post(a, ms, mb{R, PRESS}); break;
        case 3: glfw_post(a, ms, mb{R, RELEASE}); break;
        case 4:
            x += static_cast<double>(rnd() % 200) - 100.0;
            glfw_post(a, ms, p2{x, 0.0});
            break;
        case 5: glfw_post(a, ms, kb{ae::Key::eA, PRESS}); break;
        case 6: glfw_post(a, ms, kb{ae::Key::eA, RELEASE}); break;
        default: glfw_post(a, ms, ws{800, 600}); break; // 不认识的类型
        }
        ++posted;
        au.drain(a);
    }
    check(a, au, posted, "fuzz");
    std::println("随机   : {} 次投递, {} 个 accepted, 违例 0", posted, au.accepted.size());
}

// ═══════════════════════════════════════════════════════════════════════
// ⑫ one_of 的语义表（D5 里发现的那个疑问，钉成规则）
//
//   规则：one_of<A,B> 对 A、B **展开后的所有 kind** 两两互斥。
//   所以 drag 赢时，click 与 dbl 都会 superseded —— 这是规则，不是 bug。
// ═══════════════════════════════════════════════════════════════════════
void run_semantics_table()
{
    // 1) 拖拽赢：同时压掉 click 和 dbl
    {
        auto  a = make_arena<tree_mixed>();
        audit au;
        a.invoke<"bind">();
        auto conn = uhi::subscribe(a);
        glfw_post(a, 0, p2{0.0, 0.0});
        glfw_post(a, 10, mb{L, PRESS});
        glfw_post(a, 70, p2{100.0, 0.0});
        expect(uhi::to_string(a.ui.events()), "dbl:sup@70, click:sup@70, drag:acc@70",
               "one_of#1 拖拽压掉两个");
        au.drain(a);
        check(a, au, 3, "one_of#1");
    }
    // 2) click 先赢（dbl 过期），之后新的 press+move 才轮到 drag
    {
        auto  a = make_arena<tree_mixed>();
        audit au;
        a.invoke<"bind">();
        auto conn = uhi::subscribe(a);
        glfw_post(a, 0, p2{0.0, 0.0});
        glfw_post(a, 10, mb{L, PRESS});
        glfw_post(a, 60, mb{L, RELEASE});
        glfw_post(a, 360, p2{0.0, 0.0}); // dbl 过期 ⇒ click 落地
        glfw_post(a, 400, mb{L, PRESS});
        glfw_post(a, 470, p2{100.0, 0.0}); // 第二次按下后拖动
        expect(uhi::to_string(a.ui.events()),
               "click:acc@360, dbl:sup@470, click:sup@470, drag:acc@470", "one_of#2 分两次交互");
        au.drain(a);
        check(a, au, 6, "one_of#2");
    }
    // 3) 双击成立：click 让位一次，dbl 落地
    {
        auto  a = make_arena<tree_mixed>();
        audit au;
        a.invoke<"bind">();
        auto conn = uhi::subscribe(a);
        glfw_post(a, 0, p2{0.0, 0.0});
        glfw_post(a, 10, mb{L, PRESS});
        glfw_post(a, 60, mb{L, RELEASE});
        glfw_post(a, 110, mb{L, PRESS});
        glfw_post(a, 160, mb{L, RELEASE});
        expect(uhi::to_string(a.ui.events()), "click:sup@160, dbl:acc@160", "one_of#3 双击");
        au.drain(a);
        check(a, au, 5, "one_of#3");
    }
    std::println("语义表 : 3 条规则钉死（one_of 展开后两两互斥）");
}

// ═══════════════════════════════════════════════════════════════════════
int main()
{
    std::println("=== v2 投递链：GLFW → dispatcher → proxy → arena → ui ===");
    std::println("{}\n", uhi::describe<tree_mixed>().view());

    d1_click();
    d2_unknown_dropped();
    d3_refine();
    d4_mixed();
    d5_drag();
    std::println("--- 五个场景通过，开始语义表 / 穷举 / 随机 ---");
    run_semantics_table();
    run_exhaustive();
    run_fuzz();
    std::println("=== 全部通过 ===");
    return 0;
}
