// ═══════════════════════════════════════════════════════════════════════
//  test_tour.cpp —— 综合测试：把库的全部能力放在一个程序里走一遍
//
//  这不是"又一个单元测试"，是**摸底**：一次性验证
//
//    ① 结构推导        编译期算蓝图、cell、slots，并能打印
//    ② 直呼            没有纠纷的 cell 立刻发（键盘）
//    ③ 细化            更具体的没说话之前不发（click 等 dbl）
//    ④ 互斥            先到先得，输的让位（drag vs click）
//    ⑤ 双重让位        一个赢家可以同时压掉几个（drag 压 click+dbl）
//    ⑥ 载荷            FSM 的 payload 穿过两字函数指针进 arena 的字段
//    ⑦ 无载荷 handler  签名由 kind 决定：on_dbl(self) 没有参数
//    ⑧ 绑定            ui / pool 都是 arena 的字段，回调改的是 arena 自己的 self
//    ⑨ 投递链          GLFW→dispatcher→proxy_class_invoke→arena→ui，逐跳可数
//    ⑩ 类型裁剪        不认识的事件在 build_change 处变成 None
//    ⑪ tick            只有时间也能推进（窗口过期不靠 none 帧）
//    ⑫ 兜底 hold       卡死的识别器不会永久占槽
//    ⑬ 多指 pool       每指一台机器，互不牵连
//    ⑭ 不变量审计       同 cell 的 accepted 跨度不重叠、回调恰好一次、投递不丢
//
//  每一步之后都 drain 一次记录并审计；最后打印汇总。
//
//  编译：
//    g++ -std=c++26 -freflection -O2 uhi\v3\src\test_tour.cpp -o output/test_tour.exe -lstdc++exp
// ═══════════════════════════════════════════════════════════════════════
#include <cassert>
#include <cstdint>
#include <print>
#include <string>
#include <string_view>
#include <vector>

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
using uhi::step_t;
using uhi::time_point;

using gesture::click_event;
using gesture::click_fsm;
using gesture::g_click;
using gesture::g_dbl;
using gesture::g_drag;
using gesture::g_key;
using gesture::mouse_src;

// ═══════════════════════════════════════════════════════════════════════
// ① 结构：两个 cell —— 鼠标那一格要裁决，键盘那一格直呼
// ═══════════════════════════════════════════════════════════════════════
using mouse_tree = uhi::all_of<uhi::one_of<refines<g_click, g_dbl>, g_drag>, g_key>;

static_assert(uhi::ui<mouse_tree>::blueprint.view() ==
              "relations: click->dbl click~drag dbl~drag\n"
              "cells: mouse.left{arbitrate:dbl,drag,click} keyboard{direct:key_A}");
static_assert(uhi::cell_count_v<mouse_tree> == 2);
static_assert(uhi::leaf_count_v<mouse_tree> == 4);

// ═══════════════════════════════════════════════════════════════════════
// ② 触摸：另一台机器，带运行时 id（多指）
// ═══════════════════════════════════════════════════════════════════════
struct tap_event
{
    double      x  = 0.0;
    std::size_t id = 0;
};

struct touch_frame
{
    enum class ev : std::uint8_t { none, down, up, move } e = ev::none;
    double      x  = 0.0;
    std::size_t id = 0;
    time_point  t{};
};
inline touch_frame t_down(std::size_t id, double x, int ms) { return {touch_frame::ev::down, x, id, at(ms)}; }
inline touch_frame t_up(std::size_t id, int ms) { return {touch_frame::ev::up, 0.0, id, at(ms)}; }
inline touch_frame t_move(std::size_t id, double x, int ms) { return {touch_frame::ev::move, x, id, at(ms)}; }

template <>
struct uhi::frame_traits<touch_frame>
{
    static uhi::time_point when(const touch_frame &f) noexcept { return f.t; }
    static std::size_t     who(const touch_frame &f) noexcept { return f.id; }
};

struct tap_fsm
{
    bool      s = false;
    tap_event payload{};
    step_t    step(const touch_frame &f, time_point) noexcept
    {
        if (!s)
        {
            if (f.e == touch_frame::ev::down)
            {
                s       = true;
                payload = {f.x, f.id};
                return step_t::active;
            }
            return step_t::idle;
        }
        if (f.e == touch_frame::ev::up) { s = false; return step_t::done; }
        return step_t::active;
    }
    void reset() noexcept { s = false; }
};

struct swipe_fsm
{
    bool      armed = false;
    double    from  = 0.0;
    tap_event payload{};
    step_t    step(const touch_frame &f, time_point) noexcept
    {
        if (f.e == touch_frame::ev::down)
        {
            armed   = true;
            from    = f.x;
            payload = {f.x, f.id};
            return step_t::active;
        }
        if (!armed) return step_t::idle;
        if (f.e == touch_frame::ev::move) return (f.x - from) > 40.0 ? step_t::done : step_t::active;
        if (f.e == touch_frame::ev::up) { armed = false; return step_t::aborted; }
        return step_t::active;
    }
    void reset() noexcept { armed = false; from = 0.0; }
};

using touch_src  = uhi::source_t<"touch">;
using touch_tree = uhi::one_of<uhi::leaf<uhi::kind_t<"tap", tap_event>, touch_src, tap_fsm>,
                               uhi::leaf<uhi::kind_t<"swipe", tap_event>, touch_src, swipe_fsm>>;

static constexpr std::size_t lanes = 2;

// ═══════════════════════════════════════════════════════════════════════
// ③ arena：手写。ui + pool 都是它的字段，入口名字固定
// ═══════════════════════════════════════════════════════════════════════
inline auto make_arena()
{
    return make_aggregate<"arena">(
        field<"now">(time_point{}),
        field<"ctx">(arena_event::event_context{}),
        field<"ui">(uhi::ui<mouse_tree>{}),
        field<"touch">(uhi::pool<touch_tree, lanes>{}),
        field<"clicks">(int{}),
        field<"dbls">(int{}),
        field<"drags">(int{}),
        field<"keys">(int{}),
        field<"taps">(std::array<int, lanes>{}),
        field<"swipes">(std::array<int, lanes>{}),
        field<"last_x">(std::array<double, lanes>{}),
        field<"delivered">(int{}), // 到达 input_event 的次数
        field<"dropped">(int{}),   // 类型不认识被丢掉的次数

        // 有载荷：收 const click_event&
        method<"on_click">([](auto &self, const click_event &e) noexcept {
            ++self.clicks;
            self.last_x[0] = e.x;
        }),
        // 无载荷：只收 self
        method<"on_dbl">([](auto &self) noexcept { ++self.dbls; }),
        method<"on_drag">([](auto &self) noexcept { ++self.drags; }),
        method<"on_key_A">([](auto &self) noexcept { ++self.keys; }),
        method<"on_tap">([](auto &self, const tap_event &e) noexcept {
            ++self.taps[e.id];
            self.last_x[e.id] = e.x;
        }),
        method<"on_swipe">([](auto &self, const tap_event &e) noexcept { ++self.swipes[e.id]; }),

        // 绑定：两台机器都接到 arena 自己身上
        method<"bind">([](auto &self) noexcept {
            self.ui.bind(self);
            self.touch.bind(self);
        }),

        // 平台事件入口（uhi::subscribe 认这个名字）
        method<"input_event">([](auto &self, auto e) noexcept {
            ++self.delivered;
            auto ch = arena_event::build_change(self.ctx, e);
            if (ch.change == arena_event::ChangeType::None)
            {
                ++self.dropped;
                return;
            }
            ch.time_point = self.now;
            self.ui.feed(arena_event::input_frame{self.ctx, ch});
            arena_event::apply_change(self.ctx, ch);
        }),

        // 触摸入口（这一层 arena_event 还没有 touch 事件，由测试直接送）
        method<"touch_event">([](auto &self, const touch_frame &f) noexcept { self.touch.feed(f); }),

        // 时间入口：两台机器一起走
        method<"tick">([](auto &self, time_point t) noexcept {
            self.now = t;
            self.ui.tick(t);
            self.touch.tick(t);
        }));
}

using arena_t = decltype(make_arena());
static_assert(uhi::accepts_platform_events_v<arena_t>, "集成契约");

// ═══════════════════════════════════════════════════════════════════════
// ④ 审计：每一步之后 drain 三处记录，检查不变量
// ═══════════════════════════════════════════════════════════════════════
struct audit
{
    struct entry
    {
        std::string_view name;
        time_point       begin{}, end{};
        int              cell = 0;
    };
    std::vector<entry> accepted;
    int                overlaps = 0;
    int                events   = 0;
    std::string        last; // 本步的记录（给 expect 用）

    static int cell_of(std::string_view n) noexcept
    {
        if (n == "key_A") return 1;
        if (n == "tap" || n == "swipe") return 2;
        return 0; // click / dbl / drag
    }

    std::vector<std::string> parts;

    template <class UI>
    void drain(const UI &u, int lane)
    {
        auto text = uhi::to_string(u.events());
        if (!text.empty())
        {
            if (lane >= 0) text = "[" + std::to_string(lane) + "] " + text;
            parts.push_back(text);
        }
        for (const auto &e : u.events())
        {
            ++events;
            if (e.v != uhi::verdict::accepted) continue;
            const int cell = (lane < 0 ? 0 : 100 + lane) + cell_of(e.name.view());
            for (const auto &p : accepted)
                if (p.cell == cell && !(p.end <= e.begin || e.end <= p.begin)) ++overlaps;
            accepted.push_back({e.name.view(), e.begin, e.end, cell});
        }
    }

    void drain_all(arena_t &a)
    {
        parts.clear();
        drain(a.ui, -1); // 鼠标 + 键盘那台机器，不加前缀
        for (std::size_t k = 0; k < lanes; ++k) drain(a.touch[k], static_cast<int>(k));
        last.clear();
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            if (i) last += ", ";
            last += parts[i];
        }
        a.ui.clear_events();
        for (auto &l : a.touch.lanes) l.clear_events();
    }

    [[nodiscard]] int count(std::string_view n) const
    {
        int c = 0;
        for (const auto &e : accepted)
            if (e.name == n) ++c;
        return c;
    }
};

inline audit au;

// ═══════════════════════════════════════════════════════════════════════
// ⑤ 一步 = 送事件 + 审计
// ═══════════════════════════════════════════════════════════════════════
template <class Event>
void post(arena_t &a, int ms, const Event &e)
{
    glfw_post(a, ms, e);
    au.drain_all(a);
}

inline void send_touch(arena_t &a, const touch_frame &f)
{
    a.now = f.t;
    a.invoke<"touch_event">(f);
    au.drain_all(a);
}

inline void tick(arena_t &a, int ms)
{
    a.invoke<"tick">(at(ms));
    au.drain_all(a);
}

inline void want_trace(std::string_view got, std::string_view want, const char *where)
{
    if (got == want) return;
    std::print(stderr, "{}:\n  got  [{}]\n  want [{}]\n", where, got, want);
    assert(false && "unexpected trace");
}

// ═══════════════════════════════════════════════════════════════════════
int main()
{
    auto arena = make_arena();
    arena.invoke<"bind">();
    auto conn = uhi::subscribe(arena);

    std::println("=== 综合试跑 ===\n{}\n", uhi::describe<mouse_tree>().view());

    // ── P1 直呼：键盘那一格没有纠纷，立刻发 ────────────────────────
    post(arena, 100, kb{ae::Key::eA, PRESS});
    want_trace(au.last, "key_A:acc@100", "P1 key");
    assert(arena.keys == 1);
    std::println("P1 直呼          {}   keys={}", au.last, arena.keys);

    // ── P2 细化 + tick + 载荷：click 让路给 dbl，窗口靠时间过期 ────
    post(arena, 0, p2{10.0, 0.0});
    post(arena, 10, mb{L, PRESS});
    post(arena, 60, mb{L, RELEASE});
    assert(au.last.empty()); // click 已 done，但要等 dbl 的 300ms
    assert(arena.clicks == 0);
    tick(arena, 359);
    assert(au.last.empty());
    tick(arena, 360); // 从 release(60) 起 300ms ⇒ 360 过期
    want_trace(au.last, "click:acc@360", "P2");
    assert(arena.clicks == 1 && arena.last_x[0] == 10.0); // 载荷来自 FSM
    std::println("P2 细化+tick     {}   （60ms 之后没有帧；click_x={}）", au.last,
                 arena.last_x[0]);

    // ── P3 双击：click 让位，dbl 落地 ──────────────────────────────
    post(arena, 600, mb{L, PRESS});
    post(arena, 650, mb{L, RELEASE});
    post(arena, 700, mb{L, PRESS});
    post(arena, 750, mb{L, RELEASE});
    want_trace(au.last, "click:sup@750, dbl:acc@750", "P3");
    assert(arena.dbls == 1 && arena.clicks == 1); // 第二次 click 被压掉，不回调
    std::println("P3 双击          {}   dbls={}", au.last, arena.dbls);

    // ── P4 拖拽：一个赢家同时压掉 click 与 dbl ─────────────────────
    post(arena, 800, mb{L, PRESS});
    post(arena, 860, p2{200.0, 0.0});
    want_trace(au.last, "dbl:sup@860, click:sup@860, drag:acc@860", "P4");
    assert(arena.drags == 1 && arena.dbls == 1);
    std::println("P4 拖拽          {}   drags={}", au.last, arena.drags);

    // ── P5 类型裁剪：不认识的事件被安静丢掉 ────────────────────────
    [[maybe_unused]] const int dropped_before = arena.dropped;
    post(arena, 900, ws{800, 600});
    want_trace(au.last, "", "P5 无记录");
    assert(arena.dropped == dropped_before + 1);
    std::println("P5 丢弃          dropped={} delivered={}", arena.dropped, arena.delivered);

    // ── P6 多指：两根手指各算各的 ──────────────────────────────────
    send_touch(arena, t_down(0, 5.0, 1000));
    send_touch(arena, t_down(1, 50.0, 1005));
    send_touch(arena, t_up(0, 1050)); // 0 号点
    want_trace(au.last, "[0] tap:acc@1050", "P6 tap");
    send_touch(arena, t_move(1, 200.0, 1060)); // 1 号划走
    want_trace(au.last, "[1] tap:sup@1060, swipe:acc@1060", "P6 swipe");
    assert(arena.taps[0] == 1 && arena.last_x[0] == 5.0);
    assert(arena.swipes[1] == 1 && arena.taps[1] == 0);
    std::println("P6 多指          taps[0]={} swipes[1]={}", arena.taps[0], arena.swipes[1]);

    // ── P7 兜底：卡死的识别器不会永久占槽（子场景，独立 arena）──────
    {
        struct never_done_fsm // 只认 press，永不结束，也没有 tick
        {
            bool   s = false;
            step_t step(const arena_event::input_frame &f, time_point) noexcept
            {
                const auto &c = f.change;
                if (!s && c.change == arena_event::ChangeType::PointerButton && c.button.is_press())
                {
                    s = true;
                    return step_t::active;
                }
                return s ? step_t::active : step_t::idle;
            }
            void reset() noexcept { s = false; }
        };
        using guarded = refines<uhi::leaf<uhi::kind_t<"click">, mouse_src, click_fsm>,
                                uhi::leaf<uhi::kind_t<"dbl">, mouse_src, never_done_fsm, 400>>;

        auto a2 = make_aggregate<"a2">(
            field<"now">(time_point{}),
            field<"ctx">(arena_event::event_context{}),
            field<"ui">(uhi::ui<guarded>{}),
            field<"clicks">(int{}),
            method<"on_click">([](auto &self) noexcept { ++self.clicks; }),
            method<"on_dbl">([](auto &) noexcept {}),
            method<"tick">([](auto &self, time_point t) noexcept {
                self.now = t;
                self.ui.tick(t);
            }),
            method<"bind">([](auto &self) noexcept { self.ui.bind(self); }),
            method<"input_event">([](auto &self, auto e) noexcept {
                auto ch = arena_event::build_change(self.ctx, e);
                if (ch.change == arena_event::ChangeType::None) return;
                ch.time_point = self.now;
                self.ui.feed(arena_event::input_frame{self.ctx, ch});
                arena_event::apply_change(self.ctx, ch);
            }));
        auto c2 = uhi::subscribe(a2);
        a2.invoke<"bind">();

        glfw_post(a2, 0, p2{0.0, 0.0});
        glfw_post(a2, 10, mb{L, PRESS});
        glfw_post(a2, 60, mb{L, RELEASE}); // click 想落地，但 dbl 卡着
        assert(a2.ui.events().empty() && a2.clicks == 0);

        a2.invoke<"tick">(at(500)); // dbl 活了 490ms > 400 ⇒ 兜底判负 ⇒ click 解锁
        want_trace(uhi::to_string(a2.ui.events()), "click:acc@500", "P7");
        assert(a2.clicks == 1);
        std::println("P7 兜底 hold     click:acc@500   （dbl 卡死 400ms 后被静默判负）");
    }

    // ── P8 审计汇总 ────────────────────────────────────────────────
    const int fired = arena.clicks + arena.dbls + arena.drags + arena.keys + arena.taps[0] +
                      arena.taps[1] + arena.swipes[0] + arena.swipes[1];
    std::println("\n--- 审计 ---");
    std::println("投递 {} 次（{} 次被类型裁剪）", arena.delivered, arena.dropped);
    std::println("记录 {} 条，其中 accepted {} 条", au.events, au.accepted.size());
    std::println("回调触发 {} 次，同 cell 跨度重叠 {}", fired, au.overlaps);

    assert(au.overlaps == 0);
    assert(fired == static_cast<int>(au.accepted.size())); // 回调恰好一次
    assert(au.count("click") == 1); // 只有 P2 那次落地：P3/P4 的 click 都是 superseded
    assert(au.count("dbl") == 1);
    assert(au.count("drag") == 1);
    assert(au.count("key_A") == 1);
    assert(au.count("tap") == 1);
    assert(au.count("swipe") == 1);

    std::println("\n=== 综合试跑通过：14 项能力全部覆盖 ===");
    return 0;
}
