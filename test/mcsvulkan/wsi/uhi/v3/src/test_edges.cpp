// ═══════════════════════════════════════════════════════════════════════
//  test_edges.cpp —— 三个边角问题
//
//  E1  组合生成出来的对象，能不能**继续当组合子的操作数**？
//  E2  一帧里没人说话 / 不认识这一帧的人说话，会怎样？
//  E3  离散、不连续、时间倒退、根本不调 tick —— 算法还成立吗？
//
//  编译：g++ -std=c++26 -freflection -O0 test_edges.cpp -o test_edges -lstdc++exp
// ═══════════════════════════════════════════════════════════════════════
#include <cassert>
#include <cstdint>
#include <print>
#include <string_view>

#include "uhi.hpp"
#include "../../../../../../include/detail/meta/make_aggregate.hpp"

using uhi::kind_t;
using uhi::leaf;
using uhi::refines;
using uhi::source_t;
using uhi::step_t;
using uhi::time_point;

// ── 帧：只带一个事件号 + 时刻 ────────────────────────────────────────
struct frame
{
    int        ev = 0; // 0 none / 1 press / 2 release / 3 key
    time_point t{};
};
inline frame F(int ev, int ms) { return {ev, time_point{} + std::chrono::milliseconds{ms}}; }

template <>
struct uhi::frame_traits<frame>
{
    static uhi::time_point when(const frame &f) noexcept { return f.t; }
};

// ═══════════════════════════════════════════════════════════════════════
// E1：组合对象再组合 —— 4 层嵌套
//   inner = refines<a,b>
//   mid   = all_of<inner, c>           ← 拿上一层的组合结果当操作数
//   side  = one_of<d,e>
//   outer = one_of<mid, side>          ← 再套一层
// ═══════════════════════════════════════════════════════════════════════
namespace e1
{
    struct never
    {
        step_t step(const frame &, time_point) noexcept { return step_t::idle; }
        void   reset() noexcept {}
    };
    using src = source_t<"one">;
    using g_a = leaf<kind_t<"a">, src, never>;
    using g_b = leaf<kind_t<"b">, src, never>;
    using g_c = leaf<kind_t<"c">, src, never>;
    using g_d = leaf<kind_t<"d">, src, never>;
    using g_e = leaf<kind_t<"e">, src, never>;

    using inner = refines<g_a, g_b>;
    using mid   = uhi::all_of<inner, g_c>;                 // ← 组合结果当操作数
    using side  = uhi::one_of<g_d, g_e>;
    using outer = uhi::one_of<mid, side>;                  // ← 再套一层

    static_assert(uhi::valid_v<outer>, "4 层嵌套必须通过 validate");
    static_assert(uhi::leaf_count_v<outer> == 5);
    static_assert(uhi::plan_rels_v<outer>.size() == 8, "1 条细化 + 1 条互斥 + 6 条跨组");
    static_assert(uhi::cell_count_v<outer> == 1, "全同源且连通 ⇒ 一格");

    void run()
    {
        std::println("── E1  组合结果再组合（4 层）──");
        std::println("  inner = refines<a,b>");
        std::println("  mid   = all_of<inner, c>      ← 组合结果当操作数");
        std::println("  side  = one_of<d,e>");
        std::println("  outer = one_of<mid, side>     ← 再套一层");
        std::println("  蓝图：{}", uhi::ui<outer>::blueprint.view());
        std::println("  ⇒ 4 层嵌套，cell 数=1，关系 8 条，全部编译期成立。\n");
    }
} // namespace e1

// ═══════════════════════════════════════════════════════════════════════
// E2 / E3 共用一个 arena：mouse 格（click←dbl）+ keyboard 格（key）
// ═══════════════════════════════════════════════════════════════════════
namespace common
{
    struct click_fsm
    {
        bool   s = false;
        // 契约：成员名必须叫 payload，类型必须等于 kind 声明的载荷类型
        double payload = 0;
        step_t step(const frame &f, time_point) noexcept
        {
            if (!s)
            {
                if (f.ev == 1) { s = true; payload = 1.0; return step_t::active; }
                return step_t::idle;
            }
            if (f.ev == 2) { s = false; return step_t::done; }
            return step_t::active; // ← 契约：有候选就别返回 idle
        }
        void reset() noexcept { s = false; }
    };

    struct dbl_fsm
    {
        bool       down = false, waiting = false;
        time_point deadline{};
        step_t     step(const frame &f, time_point now) noexcept
        {
            if (f.ev == 1) { down = true; return step_t::active; }
            if (f.ev == 2 && down)
            {
                down = false;
                if (waiting) { waiting = false; return step_t::done; }
                waiting  = true;
                deadline = now + std::chrono::milliseconds{300};
                return step_t::active;
            }
            return (down || waiting) ? step_t::active : step_t::idle;
        }
        step_t tick(time_point now) noexcept
        {
            if (waiting && now >= deadline) { waiting = false; return step_t::aborted; }
            return step_t::hold;
        }
        void reset() noexcept { down = false; waiting = false; }
    };

    struct key_fsm
    {
        step_t step(const frame &f, time_point) noexcept
        {
            return f.ev == 3 ? step_t::done : step_t::idle;
        }
        void reset() noexcept {}
    };

    using msrc = source_t<"mouse">;
    using ksrc = source_t<"keyboard">;
    using g_click = leaf<kind_t<"click", double>, msrc, click_fsm>;
    using g_dbl   = leaf<kind_t<"dbl">, msrc, dbl_fsm>;
    using g_key   = leaf<kind_t<"key_A">, ksrc, key_fsm>;
    using tree    = uhi::all_of<refines<g_click, g_dbl>, g_key>;

    inline auto make_arena()
    {
        return mcs::vulkan::meta::make_aggregate<"arena">(
            mcs::vulkan::meta::field<"ui">(uhi::ui<tree>{}),
            mcs::vulkan::meta::field<"clicks">(int{}),
            mcs::vulkan::meta::field<"dbls">(int{}),
            mcs::vulkan::meta::field<"keys">(int{}),
            mcs::vulkan::meta::field<"last_begin">(int64_t{}),
            mcs::vulkan::meta::method<"on_click">([](auto &self, const double &) noexcept {
                ++self.clicks;
                self.last_begin = 0;
            }),
            mcs::vulkan::meta::method<"on_dbl">([](auto &self) noexcept { ++self.dbls; }),
            mcs::vulkan::meta::method<"on_key_A">([](auto &self) noexcept { ++self.keys; }),
            mcs::vulkan::meta::method<"bind">([](auto &self) noexcept { self.ui.bind(self); }));
    }
} // namespace common

// ═══════════════════════════════════════════════════════════════════════
// E2：没人说话的帧 / 只有别人说话的帧
// ═══════════════════════════════════════════════════════════════════════
void e2()
{
    using namespace common;
    std::println("── E2  一帧里没人说话 / 只有别人说话 ──");
    auto a = make_arena();
    a.invoke<"bind">();
    const auto ev = [&] { return uhi::to_string(a.ui.events()); };

    a.ui.feed(F(1, 0));  // press
    a.ui.feed(F(2, 10)); // release ⇒ click 完成但被 dbl 挡住
    std::println("  press+release 后：记录 [{}]（应为空：click 在等 dbl）",
                 ev().empty() ? "空" : ev());

    // ① 完全没人说话的帧（ev=0）：两个 FSM 都 idle 或 active，没人说 done
    a.ui.feed(F(0, 20));
    std::println("  ev=0（谁也不表态）：记录 [{}]（应仍为空）", ev().empty() ? "空" : ev());

    // ② 只有 keyboard 那格说话的帧：key 立刻发，mouse 格不受影响
    a.ui.feed(F(3, 30));
    std::println("  key 按下：记录 [{}]（key_A 该发，click 仍挂起）", ev());
    assert(a.keys == 1);
    assert(a.clicks == 0);

    // ③ tick 让 dbl 过期 ⇒ 上一帧挂起的 click 才落地
    a.ui.tick(time_point{} + std::chrono::milliseconds{310});
    std::println("  tick@310：记录 [{}]（click 才落地）", ev());
    assert(a.clicks == 1);

    std::println("  ⇒ 没人说话的帧是常态：want 全 0，② 什么也不做；");
    std::println("     但 ⓪ 和 ③ 仍要跑（兜底、唤醒），所以 tick 不能省。");
    std::println("  ⇒ 只有别人说话的帧也不影响：每格独立结算。\n");
}

// ═══════════════════════════════════════════════════════════════════════
// E3：离散 / 不连续
// ═══════════════════════════════════════════════════════════════════════
void e3()
{
    using namespace common;
    std::println("── E3  离散、不连续、时间倒退 ──");
    const auto ms = [](int t) { return time_point{} + std::chrono::milliseconds{t}; };

    {   // ① 巨大间隔：t=0 按下，t=10 松开，直接 tick 到 10^9 毫秒
        auto a = make_arena();
        a.invoke<"bind">();
        a.ui.feed(F(1, 0));
        a.ui.feed(F(2, 10));
        a.ui.tick(ms(1'000'000'000));
        std::println("  ① 间隔 10^9 ms 后 tick：记录 [{}]", uhi::to_string(a.ui.events()));
        assert(a.clicks == 1); // 只靠绝对时刻比较，间隔多大都成立
    }
    {   // ② 同一时刻两帧（时间没前进）
        auto a = make_arena();
        a.invoke<"bind">();
        a.ui.feed(F(1, 0));
        a.ui.feed(F(2, 0));
        a.ui.feed(F(2, 0)); // 同一毫秒再松一次
        a.ui.tick(ms(0));
        std::println("  ② 同一毫秒两次松开：记录 [{}]（只能有一个 click）",
                     uhi::to_string(a.ui.events()));
        assert(a.clicks == 0); // tick(0) 还没到 300ms 窗口
        a.ui.tick(ms(300));
        assert(a.clicks == 1);
    }
    {   // ③ 时间倒退：tick 到比 begin 更早的时刻
        auto a = make_arena();
        a.invoke<"bind">();
        a.ui.feed(F(1, 100));
        a.ui.feed(F(2, 150));
        a.ui.tick(ms(1)); // 倒退
        std::println("  ③ 时间倒退 tick(1)：记录 [{}]（不崩、不误判）",
                     uhi::to_string(a.ui.events()));
        assert(a.clicks == 0);
        a.ui.tick(ms(450)); // 再正常推进
        assert(a.clicks == 1);
    }
    {   // ④ 根本不调 tick：两次点击会就地凑成双击（不需要等，因为第二下已经在窗口内）
        auto a = make_arena();
        a.invoke<"bind">();
        a.ui.feed(F(1, 0));
        a.ui.feed(F(2, 10)); // click 完成但挂起（等 dbl）
        a.ui.feed(F(1, 20)); // 又按一次
        a.ui.feed(F(2, 30)); // 又松一次 ⇒ dbl 立刻完成，不需要 tick
        std::println("  ④ 不调 tick，两次点击：记录 [{}]", uhi::to_string(a.ui.events()));
        assert(a.clicks == 0 && a.dbls == 1); // 双击，不是"一次点击"
        a.ui.tick(ms(400));
        std::println("     之后再 tick：记录不变 [{}]（已结算）",
                     uhi::to_string(a.ui.events()));
        assert(a.clicks == 0 && a.dbls == 1);
    }
    {   // ⑤ 只有单击、又永远不 tick ⇒ 永远挂起（这就是 tick 存在的理由）
        auto a = make_arena();
        a.invoke<"bind">();
        a.ui.feed(F(1, 0));
        a.ui.feed(F(2, 10));
        a.ui.feed(F(0, 20)); // 再来 1000 帧"没人说话"也没用：没人推进时间
        a.ui.feed(F(0, 1000));
        std::println("  ⑤ 单击 + 不 tick（哪怕后来很多帧）：记录 [{}]（永远挂起）",
                     uhi::to_string(a.ui.events()));
        assert(a.clicks == 0);
        a.ui.tick(ms(310)); // 只有 tick 能推进"时间"这件事
        std::println("     一调 tick：记录 [{}]", uhi::to_string(a.ui.events()));
        assert(a.clicks == 1);
    }
    {   // ⑥ 同一毫秒一串事件（burst）
        auto a = make_arena();
        a.invoke<"bind">();
        a.ui.feed(F(1, 0));
        a.ui.feed(F(2, 0));
        a.ui.feed(F(1, 0));
        a.ui.feed(F(2, 0));
        a.ui.feed(F(1, 0));
        a.ui.feed(F(2, 0));
        a.ui.tick(ms(300));
        std::println("  ⑥ 同一毫秒 3 次点击：记录 [{}]", uhi::to_string(a.ui.events()));
        assert(a.clicks == 1 && a.dbls == 1); // 第 2 次凑成 dbl，第 3 次归并
    }
    std::println("  ⇒ 算法只做\"绝对时刻比较\"和\"集合运算\"，不假设时间均匀、不假设帧间隔。");
    std::println("     它是一个**离散递推**：每帧只依赖上一帧的 alive/pending/begin。");
    std::println("     唯一的隐含前提：时间不倒退（倒退不崩也不误判，但窗口判断会失真）。\n");
}

// ═══════════════════════════════════════════════════════════════════════
int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::println("╔══════════════════════════════════════════════════════════════╗");
    std::println("║ 三个边角问题                                                  ║");
    std::println("╚══════════════════════════════════════════════════════════════╝\n");
    e1::run();
    e2();
    e3();
    std::println("三个边角问题全部通过。");
    return 0;
}
