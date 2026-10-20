// ═══════════════════════════════════════════════════════════════════════
//  test_liveness.cpp —— v3 之一：liveness（兜底）
//
//  要证明的两件事：
//    ① 一个卡死的假设会**堵死别人**（deferred 的邻居永远等不到）
//    ② 给 leaf 一个兜底时限后，ui.tick(now) 会把它静默判负，
//       邻居于是能落地 —— 而且是在"没有新帧"的情况下靠时间解决
//
//  规则：兜底只判负，不发明赢家。"谁赢"永远是识别器自己说的。
//
//  编译：
//    g++ -std=c++26 -freflection -O0 -g uhi\v3\src\test_liveness.cpp -o output/test_liveness.exe -lstdc++exp
// ═══════════════════════════════════════════════════════════════════════
#include <cassert>
#include <chrono>
#include <cstdint>
#include <print>
#include <string_view>

#include "../../../../../../include/detail/meta/make_aggregate.hpp"
#include "uhi.hpp"

using mcs::vulkan::meta::field;
using mcs::vulkan::meta::make_aggregate;
using mcs::vulkan::meta::method;
using uhi::kind_t;
using uhi::leaf;
using uhi::refines;
using uhi::source_t;
using uhi::step_t;
using uhi::time_point;

// ── 玩具帧：liveness 只关心时间，不关心事件类型 ──────────────────────
struct frame
{
    enum class ev : std::uint8_t { none, press, release } e = ev::none;
    time_point t{};
};
inline frame press(int ms) { return {frame::ev::press, time_point{} + std::chrono::milliseconds{ms}}; }
inline frame release(int ms) { return {frame::ev::release, time_point{} + std::chrono::milliseconds{ms}}; }

template <>
struct uhi::frame_traits<frame>
{
    static uhi::time_point when(const frame &f) noexcept { return f.t; }
};

inline time_point at(int ms) { return time_point{} + std::chrono::milliseconds{ms}; }

// ── 识别器 ─────────────────────────────────────────────────────────
struct click_fsm // 正常：press 开，release 收
{
    bool s = false;
    step_t step(const frame &f, time_point) noexcept
    {
        if (!s)
        {
            if (f.e == frame::ev::press) { s = true; return step_t::active; }
            return step_t::idle;
        }
        if (f.e == frame::ev::release) { s = false; return step_t::done; }
        return step_t::active;
    }
    void reset() noexcept { s = false; }
};

// 卡死：进了 wait_second 就再也不出来（不实现 tick，也不超时）
struct stuck_dbl_fsm
{
    enum class st : std::uint8_t { idle, first, wait_second };
    st     s = st::idle;
    step_t step(const frame &f, time_point) noexcept
    {
        switch (s)
        {
        case st::idle:
            if (f.e == frame::ev::press) { s = st::first; return step_t::active; }
            return step_t::idle;
        case st::first:
            if (f.e == frame::ev::release) s = st::wait_second;
            return step_t::active;
        case st::wait_second:
            return step_t::active; // ← 永远等第二次按下，永不放弃
        }
        return step_t::idle;
    }
    void reset() noexcept { s = st::idle; }
};

// 有时间观念：实现 tick(now)，窗口一过就自己放弃
struct timed_dbl_fsm
{
    enum class st : std::uint8_t { idle, first, wait_second };
    st         s = st::idle;
    time_point deadline{};
    static constexpr auto window = std::chrono::milliseconds{300};

    step_t step(const frame &f, time_point now) noexcept
    {
        switch (s)
        {
        case st::idle:
            if (f.e == frame::ev::press) { s = st::first; return step_t::active; }
            return step_t::idle;
        case st::first:
            if (f.e == frame::ev::release) { s = st::wait_second; deadline = now + window; }
            return step_t::active;
        case st::wait_second:
            return step_t::active;
        }
        return step_t::idle;
    }
    step_t tick(time_point now) noexcept // ← 只有时间前进时被调用
    {
        if (s == st::wait_second && now >= deadline) { s = st::idle; return step_t::aborted; }
        return step_t::hold;
    }
    void reset() noexcept { s = st::idle; }
};

using src = source_t<"mouse.left">;

// ═══════════════════════════════════════════════════════════════════════
// ① 卡死会堵死邻居：click 永远 deferred
// ═══════════════════════════════════════════════════════════════════════
void t1_deadlock_without_hold()
{
    using tree = refines<leaf<kind_t<"click">, src, click_fsm>,
                         leaf<kind_t<"dbl">, src, stuck_dbl_fsm>>; // hold = 0

    auto arena = make_aggregate<"arena">(
        field<"ui">(uhi::ui<tree>{}),
        field<"clicks">(int{}),
        field<"dbls">(int{}),
        method<"on_click">([](auto &self) noexcept { ++self.clicks; }),
        method<"on_dbl">([](auto &self) noexcept { ++self.dbls; }),
        method<"bind">([](auto &self) noexcept { self.ui.bind(self); }));

    arena.invoke<"bind">();
    arena.ui.feed(press(10));
    arena.ui.feed(release(60));      // click done，但要等 dbl
    assert(arena.ui.events().empty()); // ← 被卡住了

    arena.ui.tick(at(2000)); // 时间过去很久，也没用：dbl 没兜底
    arena.ui.tick(at(9000));
    assert(arena.ui.events().empty()); // 还是卡着
    assert(arena.clicks == 0);
    std::println("t1 无兜底        永远 deferred（clicks=0, events=0）");
}

// ═══════════════════════════════════════════════════════════════════════
// ② 给了兜底：tick 把卡死的 dbl 静默判负，click 立刻落地
// ═══════════════════════════════════════════════════════════════════════
void t2_hold_unblocks()
{
    using tree = refines<leaf<kind_t<"click">, src, click_fsm>,
                         leaf<kind_t<"dbl">, src, stuck_dbl_fsm, 600>>; // ← 600ms 兜底

    auto arena = make_aggregate<"arena">(
        field<"ui">(uhi::ui<tree>{}),
        field<"clicks">(int{}),
        field<"dbls">(int{}),
        method<"on_click">([](auto &self) noexcept { ++self.clicks; }),
        method<"on_dbl">([](auto &self) noexcept { ++self.dbls; }),
        method<"bind">([](auto &self) noexcept { self.ui.bind(self); }));

    arena.invoke<"bind">();
    arena.ui.feed(press(10));
    arena.ui.feed(release(60));
    assert(arena.ui.events().empty());

    arena.ui.tick(at(700)); // dbl 活了 690ms > 600 ⇒ 判负 ⇒ click 解锁
    auto s = uhi::to_string(arena.ui.events());
    assert(s == "click:acc@700");
    assert(arena.clicks == 1 && arena.dbls == 0);
    // 兜底不发明赢家：click 是自己早就 done 了，只是被挡住
    [[maybe_unused]] const auto &e = arena.ui.events()[0];
    assert(e.begin == at(10) && e.end == at(60)); // 跨度仍是它自己的
    std::println("t2 600ms 兜底    {}   → 跨度仍是 [{},{}]", s, 10, 60);
}

// ═══════════════════════════════════════════════════════════════════════
// ③ 时间观念：FSM 自己实现 tick，于是"没有新帧"也能推进
//     —— 不再需要喂 none 帧来让窗口过期
// ═══════════════════════════════════════════════════════════════════════
void t3_tick_advances_timeouts()
{
    using tree = refines<leaf<kind_t<"click">, src, click_fsm>,
                         leaf<kind_t<"dbl">, src, timed_dbl_fsm>>;

    auto arena = make_aggregate<"arena">(
        field<"ui">(uhi::ui<tree>{}),
        field<"clicks">(int{}),
        field<"dbls">(int{}),
        method<"on_click">([](auto &self) noexcept { ++self.clicks; }),
        method<"on_dbl">([](auto &self) noexcept { ++self.dbls; }),
        method<"bind">([](auto &self) noexcept { self.ui.bind(self); }));

    arena.invoke<"bind">();
    arena.ui.feed(press(10));
    arena.ui.feed(release(60));
    assert(arena.ui.events().empty()); // click 在等 dbl 的 300ms 窗口

    arena.ui.tick(at(359)); // 还没到
    assert(arena.ui.events().empty());
    arena.ui.tick(at(360)); // 窗口过期 ⇒ dbl 自己 aborted ⇒ click 落地
    auto s = uhi::to_string(arena.ui.events());
    assert(s == "click:acc@360");
    assert(arena.clicks == 1 && arena.dbls == 0);
    std::println("t3 纯 tick 推进  {}   （60ms 之后没有任何帧）", s);
}

// ═══════════════════════════════════════════════════════════════════════
int main()
{
    std::println("=== v3 liveness：兜底 + 时间推进 ===");
    t1_deadlock_without_hold();
    t2_hold_unblocks();
    t3_tick_advances_timeouts();
    std::println("=== 三个 liveness 用例全部通过 ===");
    return 0;
}
