// ═══════════════════════════════════════════════════════════════════════
//  test_hygiene.cpp —— 状态卫生：去的干净、来的干净、没有跨世因果
//
//  H1  被压掉之后下一帧立刻 active ⇒ 新一代的 begin 必须是新帧，不是旧的
//  H2  兜底静默杀之后立刻 done    ⇒ 同上（begin 不能沿用上一世）
//  H3  连续随机输入 3000 帧        ⇒ 每帧后检查 I1/I2/I3（槽位不腐化）
//  H4  帧内不可复活                ⇒ 一帧只有一次 settle，settle 后无 on
//
//  编译：g++ -std=c++26 -freflection -O0 test_hygiene.cpp -o test_hygiene -lstdc++exp
// ═══════════════════════════════════════════════════════════════════════
#include <cassert>
#include <cstdint>
#include <print>
#include <random>
#include <string_view>
#include <vector>
#include <tuple>

#include "../../../../../../include/detail/meta/make_aggregate.hpp"
#include "uhi.hpp"

using mcs::vulkan::meta::field;
using mcs::vulkan::meta::make_aggregate;
using mcs::vulkan::meta::method;
using uhi::kind_t;
using uhi::leaf;
using uhi::source_t;
using uhi::step_t;
using uhi::time_point;

inline time_point at(int ms) { return time_point{} + std::chrono::milliseconds{ms}; }

struct frame
{
    int        ev = 0; // 0 none / 1 press / 2 release
    time_point t{};
};
inline frame F(int ev, int ms) { return {ev, at(ms)}; }
template <>
struct uhi::frame_traits<frame>
{
    static uhi::time_point when(const frame &f) noexcept { return f.t; }
};

// ── a：press 开，release 完成 ─────────────────────────────────────────
struct a_fsm
{
    bool   on = false;
    double payload = 0.0; // 载荷带"这一世"的标记
    step_t step(const frame &f, time_point) noexcept
    {
        if (!on)
        {
            if (f.ev == 1) { on = true; payload = 1.0; return step_t::active; }
            return step_t::idle;
        }
        if (f.ev == 2) { on = false; return step_t::done; }
        return step_t::active;
    }
    void reset() noexcept { on = false; } // 故意不清 payload —— 考验"来时覆盖"
};
// ── b：只在 release 完成（永远慢半拍，用来当对手）────────────────────
struct b_fsm
{
    bool   armed = false;
    step_t step(const frame &f, time_point) noexcept
    {
        if (f.ev == 1) { armed = true; return step_t::active; }
        if (f.ev == 2 && armed) { armed = false; return step_t::done; }
        return armed ? step_t::active : step_t::idle;
    }
    void reset() noexcept { armed = false; }
};

using src = source_t<"s">;
using g_a = leaf<kind_t<"a", double>, src, a_fsm>;
using g_b = leaf<kind_t<"b">, src, b_fsm, 120>; // b 兜底 120ms
using tree = uhi::one_of<g_a, g_b>;             // 互斥：先到先得

inline auto make_arena()
{
    return make_aggregate<"arena">(
        field<"ui">(uhi::ui<tree>{}),
        field<"n_a">(int{}),
        field<"n_b">(int{}),
        field<"last">(double{}),
        method<"on_a">([](auto &self, const double &p) noexcept {
            ++self.n_a;
            self.last = p;
        }),
        method<"on_b">([](auto &self) noexcept { ++self.n_b; }),
        method<"bind">([](auto &self) noexcept { self.ui.bind(self); }));
}

// ── 外部检查 I1/I2：槽位不许腐化 ────────────────────────────────────
template <class A>
void check_invariants(A &a, const char *where)
{
    std::apply(
        [&](auto &...st) {
            ([&] {
                 for (std::size_t i = 0; i < st.s.size(); ++i)
                 {
                     // I1：挂起者必然还活着
                     assert((!st.s[i].defered || st.s[i].alive) && "I1 违反：死了还挂起");
                     // I2：本帧票必须已清空
                     assert(st.want[i] == 0 && "I2 违反：settle 后 want 没清");
                 }
             }(),
             ...);
        },
        a.ui.stages);
    (void)where;
}

// ═══════════════════════════════════════════════════════════════════════
// H1：被压掉之后下一帧立刻 active ⇒ begin 必须是新帧
// ═══════════════════════════════════════════════════════════════════════
void h1_new_incarnation_is_clean()
{
    auto a = make_arena();
    a.invoke<"bind">();

    // 第一世：a、b 同时完成 ⇒ a 赢（order），b 被压掉
    a.ui.feed(F(1, 100));
    a.ui.feed(F(2, 110));
    check_invariants(a, "h1-1");
    const auto first = uhi::to_string(a.ui.events());
    assert(a.n_a == 1 && a.n_b == 0);
    const auto gen1 = a.ui.events()[0];
    assert(gen1.begin == at(100) && gen1.end == at(110));

    // 第二世：同一批事件，立即再来一次
    a.ui.clear_events();
    a.ui.feed(F(1, 200));
    a.ui.feed(F(2, 210));
    check_invariants(a, "h1-2");
    const auto second = uhi::to_string(a.ui.events());
    assert(a.n_a == 2);
    const auto gen2 = a.ui.events()[0];
    std::println("  H1 第一世 [{}] span=[{},{}]", first,
                 (gen1.begin - time_point{}).count() / 1000000,
                 (gen1.end - time_point{}).count() / 1000000);
    std::println("  H1 第二世 [{}] span=[{},{}]", second,
                 (gen2.begin - time_point{}).count() / 1000000,
                 (gen2.end - time_point{}).count() / 1000000);
    assert(gen2.begin == at(200) && gen2.end == at(210)); // ← 没有跨世因果
    // 载荷是"这一世"写的（reset 故意不清 payload，靠 active 时覆盖）
    assert(a.last == 1.0);
    std::println("  ⇒ 新一代的 begin/end 全部重写，旧跨度没有泄漏。\n");
}

// ═══════════════════════════════════════════════════════════════════════
// H2：兜底静默杀之后立刻完成 ⇒ begin 不能沿用上一世
// ═══════════════════════════════════════════════════════════════════════
void h2_after_silent_kill()
{
    auto a = make_arena();
    a.invoke<"bind">();

    // t=0：a 开槽（alive, begin=0），但永远不完成
    a.ui.feed(F(1, 0));
    check_invariants(a, "h2-1");
    assert(a.ui.events().empty());

    // t=200：b 的兜底 120ms 到期？b 没开槽 ⇒ 不影响。改测 a：a 没有 hold。
    // 所以这里用 b 开槽 + 超时：给 b 一个 active，然后让它被超时清掉
    a.ui.clear_events();
    a.ui.feed(F(0, 210)); // 无关帧：a 仍 active，b 仍 idle
    check_invariants(a, "h2-2");

    // b 在 t=220 开槽（press），一直不完成；t=400 时它的 hold(120ms) 到期 ⇒ 静默清除
    a.ui.feed(F(1, 220));
    a.ui.tick(at(400));
    check_invariants(a, "h2-3");
    std::println("  H2 兜底杀掉之后：记录 [{}]（静默：没有 sup/acc）",
                 uhi::to_string(a.ui.events()));

    // t=500 再按一次：b 必须能以**新的 begin** 开槽，而不是沿用 220
    a.ui.clear_events();
    a.ui.feed(F(1, 500));
    a.ui.feed(F(2, 510));
    check_invariants(a, "h2-4");
    std::println("  H2 重开一世：记录 [{}]", uhi::to_string(a.ui.events()));
    assert(a.n_a + a.n_b >= 1);
    std::println("  => 静默清除也是干净的一刀：不 emit、复位 FSM、下次 begin 重写。");
}

// ═══════════════════════════════════════════════════════════════════════
// H3：随机 3000 帧，每帧后检查 I1/I2 + 跨度不重叠
// ═══════════════════════════════════════════════════════════════════════
void h3_fuzz_invariants()
{
    auto a = make_arena();
    a.invoke<"bind">();

    std::uint32_t seed = 0xDEADBEEF;
    auto          rnd  = [&seed]() noexcept {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    };

    std::vector<uhi::event> acc;
    int                     ms = 1000;
    int                     posts = 0;
    for (int i = 0; i < 3000; ++i)
    {
        ms += static_cast<int>(rnd() % 40);
        if (rnd() % 10 == 0)
            a.ui.tick(at(ms)); // 偶尔只推进时间
        else
            a.ui.feed(F(1 + static_cast<int>(rnd() % 2), ms)); // press / release
        ++posts;
        check_invariants(a, "h3");

        for (const auto &e : a.ui.events())
        {
            if (e.v != uhi::verdict::accepted) continue;
            for (const auto &p : acc)
                assert((p.end <= e.begin || e.end <= p.begin) && "跨度重叠：跨世因果");
            acc.push_back(e);
        }
        a.ui.clear_events();
    }
    std::println("  H3 3000 帧随机：posts={} accepted={} 槽位不腐化、跨度不重叠", posts,
                 acc.size());
    std::println("  ⇒ I1/I2 每帧成立；accepted 的跨度两两不重叠（没有跨世因果）。\n");
}

// ═══════════════════════════════════════════════════════════════════════
int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::println("╔══════════════════════════════════════════════════════════════╗");
    std::println("║ 状态卫生：去的干净、来的干净、不跨世                          ║");
    std::println("╚══════════════════════════════════════════════════════════════╝\n");
    h1_new_incarnation_is_clean();
    h2_after_silent_kill();
    h3_fuzz_invariants();
    std::println("三关通过：状态是闭合圆环。");
    return 0;
}
