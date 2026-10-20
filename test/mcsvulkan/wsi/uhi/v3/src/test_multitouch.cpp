// ═══════════════════════════════════════════════════════════════════════
//  test_multitouch.cpp —— v3 之二：多指
//
//  三根手指 = 三台独立的机器。它们**从不互相裁决**：
//    · 各自有识别器状态（一根手指按住不影响另一根）
//    · 各自有槽位表（互斥只发生在同一根手指内部）
//    · 各自有记录
//  帧只需要报出自己属于哪一指：frame_traits<F>::who(f)。
//
//  编译：
//    g++ -std=c++26 -freflection -O0 -g uhi\v3\src\test_multitouch.cpp -o output/test_multitouch.exe -lstdc++exp
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

// ── 触摸帧：带着手指 id ──────────────────────────────────────────────
struct touch_frame
{
    enum class ev : std::uint8_t { none, down, up, move } e = ev::none;
    double      x = 0.0;
    std::size_t id = 0; // 哪根手指
    time_point  t{};
};
inline time_point at(int ms) { return time_point{} + std::chrono::milliseconds{ms}; }
inline touch_frame down(std::size_t id, double x, int ms) { return {touch_frame::ev::down, x, id, at(ms)}; }
inline touch_frame up(std::size_t id, int ms) { return {touch_frame::ev::up, 0.0, id, at(ms)}; }
inline touch_frame move(std::size_t id, double x, int ms) { return {touch_frame::ev::move, x, id, at(ms)}; }

template <>
struct uhi::frame_traits<touch_frame>
{
    static uhi::time_point when(const touch_frame &f) noexcept { return f.t; }
    static std::size_t     who(const touch_frame &f) noexcept { return f.id; }
};

// ── 识别器 ─────────────────────────────────────────────────────────
struct tap_event
{
    double      x  = 0.0;
    std::size_t id = 0;
};

struct tap_fsm // 点一下
{
    bool      s = false;
    tap_event payload{};
    step_t    step(const touch_frame &f, time_point) noexcept
    {
        if (!s)
        {
            if (f.e == touch_frame::ev::down)
            {
                s         = true;
                payload.x = f.x;
                payload.id = f.id;
                return step_t::active;
            }
            return step_t::idle;
        }
        if (f.e == touch_frame::ev::up) { s = false; return step_t::done; }
        return step_t::active;
    }
    void reset() noexcept { s = false; }
};

struct swipe_fsm // 划过一小段
{
    bool      armed = false;
    double    from  = 0.0;
    tap_event payload{};
    step_t    step(const touch_frame &f, time_point) noexcept
    {
        if (f.e == touch_frame::ev::down)
        {
            armed     = true;
            from      = f.x;
            payload.x = f.x;
            payload.id = f.id;
            return step_t::active;
        }
        if (!armed) return step_t::idle;
        if (f.e == touch_frame::ev::move)
            return (f.x - from) > 40.0 ? step_t::done : step_t::active;
        if (f.e == touch_frame::ev::up) { armed = false; return step_t::aborted; }
        return step_t::active;
    }
    void reset() noexcept { armed = false; from = 0.0; }
};

struct stuck_fsm // 按下后永远等（用来验证"一根手指卡住不拖累别人"）
{
    bool      s = false;
    tap_event payload{};
    step_t step(const touch_frame &f, time_point) noexcept
    {
        if (!s && f.e == touch_frame::ev::down) { s = true; return step_t::active; }
        return s ? step_t::active : step_t::idle;
    }
    void reset() noexcept { s = false; }
};

using touch_src = source_t<"touch">;

// 每根手指：tap 与 swipe 互斥
using finger_tree = uhi::one_of<leaf<kind_t<"tap", tap_event>, touch_src, tap_fsm>,
                                leaf<kind_t<"swipe", tap_event>, touch_src, swipe_fsm>>;

// 手写的 arena：三个数组字段，按手指 id 记
inline auto make_arena()
{
    return make_aggregate<"arena">(
        field<"taps">(std::array<int, 3>{}),
        field<"swipes">(std::array<int, 3>{}),
        field<"last_x">(std::array<double, 3>{}),
        method<"on_tap">([](auto &self, const tap_event &e) noexcept {
            ++self.taps[e.id];
            self.last_x[e.id] = e.x;
        }),
        method<"on_swipe">([](auto &self, const tap_event &e) noexcept { ++self.swipes[e.id]; }));
}

// ═══════════════════════════════════════════════════════════════════════
// ① 三根手指各自点一下：互不影响，payload 带着自己的 id
// ═══════════════════════════════════════════════════════════════════════
void t1_independent_taps()
{
    uhi::pool<finger_tree, 3> pool{};
    auto arena = make_arena();
    // on_tap 收 tap_event；swipe 无载荷 ⇒ 用不带参数的 handler
    pool.bind(arena);

    // 交错：三根手指同时按住，先后抬起
    pool.feed(down(0, 10.0, 0));
    pool.feed(down(1, 20.0, 5));
    pool.feed(down(2, 30.0, 10));
    pool.feed(up(1, 60));
    pool.feed(up(2, 70));
    pool.feed(up(0, 80));

    assert(arena.taps[0] == 1 && arena.taps[1] == 1 && arena.taps[2] == 1);
    assert(arena.last_x[0] == 10.0 && arena.last_x[1] == 20.0 && arena.last_x[2] == 30.0);

    std::print("t1 三指独立      ");
    for (int k = 0; k < 3; ++k)
        std::print("[{}] {}  ", k, uhi::to_string(pool[k].events()));
    std::println("");
}

// ═══════════════════════════════════════════════════════════════════════
// ② 互斥只在同一根手指内部：0 号划走，1 号照样点
// ═══════════════════════════════════════════════════════════════════════
void t2_exclusion_is_per_finger()
{
    uhi::pool<finger_tree, 2> pool{};
    auto arena = make_arena();
    pool.bind(arena);

    pool.feed(down(0, 0.0, 0));
    pool.feed(down(1, 100.0, 0));
    pool.feed(move(0, 200.0, 50)); // 0 号划走 ⇒ 0 号里的 tap 让位
    pool.feed(up(1, 60));          // 1 号照常点

    assert(arena.taps[1] == 1);
    assert(arena.last_x[1] == 100.0);
    assert(uhi::to_string(pool[0].events()) == "tap:sup@50, swipe:acc@50");
    assert(uhi::to_string(pool[1].events()) == "tap:acc@60"); // swipe 在 up 那一帧自己放弃了，静默
    std::println("t2 互斥按指划分  [0] {}  [1] {}", uhi::to_string(pool[0].events()),
                 uhi::to_string(pool[1].events()));
}

// ═══════════════════════════════════════════════════════════════════════
// ③ 一根手指卡住，不拖累别人；tick 单独收拾它
// ═══════════════════════════════════════════════════════════════════════
void t3_stuck_finger_isolated()
{
    // 两个 kind 都要有兜底：一根手指按住不放，两个假设都卡住了
    using stuck_tree = uhi::one_of<leaf<kind_t<"tap", tap_event>, touch_src, tap_fsm, 500>,
                                   leaf<kind_t<"swipe", tap_event>, touch_src, stuck_fsm, 500>>;

    static_assert(uhi::plan_kinds_v<stuck_tree>[0].hold_ns == 500'000'000, "hold 没进 plan");
    static_assert(uhi::plan_kinds_v<stuck_tree>[1].hold_ns == 500'000'000, "hold 没进 plan(2)");
    uhi::pool<stuck_tree, 2> pool{};
    auto arena = make_arena();
    pool.bind(arena);

    pool.feed(down(0, 5.0, 0)); // 0 号按下后永远不抬（卡死在 swipe 上）
    pool.feed(down(1, 60.0, 10));
    pool.feed(up(1, 40));
    assert(arena.taps[1] == 1); // 1 号完全不受影响
    assert(arena.taps[0] == 0);

    pool.tick(at(600)); // 两指都卡住了 600ms > 500 ⇒ 兜底判负
    assert(arena.taps[0] == 0); // 兜底不发明赢家

    pool.feed(down(0, 70.0, 700)); // 0 号重新开始，正常工作
    pool.feed(up(0, 750));
    assert(arena.taps[0] == 1);
    assert(arena.last_x[0] == 70.0);
    std::println("t3 卡住不牵连    [0] {}  [1] {}", uhi::to_string(pool[0].events()),
                 uhi::to_string(pool[1].events()));
}

// ═══════════════════════════════════════════════════════════════════════
int main()
{
    std::println("=== v3 多指：每指一台机器 ===");
    t1_independent_taps();
    t2_exclusion_is_per_finger();
    t3_stuck_finger_isolated();
    std::println("=== 三个多指用例全部通过 ===");
    return 0;
}
