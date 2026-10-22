// ═══════════════════════════════════════════════════════════════════════
//  test_priority.cpp —— "优先级"到底能不能表达，能到什么程度
//
//  S1  one_of<a,b>，同帧两人都完成 ⇒ 谁赢？（看声明顺序）
//  S2  one_of<b,a>，同上          ⇒ 换声明顺序就换赢家
//  S3  refines<a,b>               ⇒ 这就是"优先级"：a 让位给 b
//  S4  互斥但不细化、a 先完成      ⇒ b 永远赢不了（跨帧先到先得）
//  S5  order 是编译期常量吗        ⇒ 看 order 从哪来
//
//  编译：g++ -std=c++26 -freflection -O0 test_priority.cpp -o test_priority -lstdc++exp
// ═══════════════════════════════════════════════════════════════════════
#include <cassert>
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

struct frame
{
    int        ev = 0;
    time_point t{};
};
inline frame F(int ev, int ms) { return {ev, time_point{} + std::chrono::milliseconds{ms}}; }
template <>
struct uhi::frame_traits<frame>
{
    static uhi::time_point when(const frame &f) noexcept { return f.t; }
};

// 两人都在 ev==1 完成（用来制造"同帧并列"）
struct both_fsm
{
    step_t step(const frame &f, time_point) noexcept
    {
        return f.ev == 1 ? step_t::done : step_t::idle;
    }
    void reset() noexcept {}
};
// a 只在 ev==1 完成，b 只在 ev==2 完成（用来制造"不同帧"）
struct a_only
{
    step_t step(const frame &f, time_point) noexcept
    {
        return f.ev == 1 ? step_t::done : step_t::idle;
    }
    void reset() noexcept {}
};
// b 必须在窗口一开始就"占位"（active），否则它没有活着的候选，
// 细化关系就挡不住 a —— 这是识别器的义务，不是库能替你做的。
struct b_only
{
    bool   armed = false;
    step_t step(const frame &f, time_point) noexcept
    {
        if (f.ev == 1) { armed = true; return step_t::active; } // 先占位
        if (f.ev == 2 && armed) { armed = false; return step_t::done; }
        return armed ? step_t::active : step_t::idle;
    }
    void reset() noexcept { armed = false; }
};

using src = source_t<"s">;
using g_a = leaf<kind_t<"a">, src, both_fsm>;
using g_b = leaf<kind_t<"b">, src, both_fsm>;
using g_a2 = leaf<kind_t<"a">, src, a_only>;
using g_b2 = leaf<kind_t<"b">, src, b_only>;

template <class Tree>
inline auto make_arena()
{
    return make_aggregate<"arena">(
        field<"ui">(uhi::ui<Tree>{}),
        field<"n_a">(int{}),
        field<"n_b">(int{}),
        method<"on_a">([](auto &self) noexcept { ++self.n_a; }),
        method<"on_b">([](auto &self) noexcept { ++self.n_b; }),
        method<"bind">([](auto &self) noexcept { self.ui.bind(self); }));
}

void show(const char *label, std::string_view trace, std::string_view order)
{
    std::println("  {:<24} order={:<8} 记录=[{}]", label, order, trace);
}

// ═══════════════════════════════════════════════════════════════════════
int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::println("╔══════════════════════════════════════════════════════════════╗");
    std::println("║ 优先级能表达吗？到哪里为止？                                  ║");
    std::println("╚══════════════════════════════════════════════════════════════╝\n");

    // ── S1 / S2：同帧并列，声明顺序决定赢家 ──────────────────────────
    std::println("── S1/S2  one_of 同帧并列：谁先声明谁赢 ──");
    {
        using t = uhi::one_of<g_a, g_b>; // 声明 a 在前
        auto a = make_arena<t>();
        a.invoke<"bind">();
        a.ui.feed(F(1, 0)); // 两人同帧完成
        show("one_of<a,b> 同帧", uhi::to_string(a.ui.events()), "a,b");
        assert(a.n_a == 1 && a.n_b == 0);
    }
    {
        using t = uhi::one_of<g_b, g_a>; // 声明 b 在前
        auto a = make_arena<t>();
        a.invoke<"bind">();
        a.ui.feed(F(1, 0));
        show("one_of<b,a> 同帧", uhi::to_string(a.ui.events()), "b,a");
        assert(a.n_a == 0 && a.n_b == 1);
    }
    std::println("  ⇒ order 来自拓扑排序的**平局规则**（按 kind 下标），所以");
    std::println("     \"声明顺序\"就是当前唯一的同帧优先级旋钮——但它没被文档化。\n");

    // ── S3：细化 = 优先级（延迟）─────────────────────────────────────
    std::println("── S3  refines<a,b>：a 让位给 b（这就是优先级）──");
    {
        using t = refines<g_a2, g_b2>; // b 细化 a
        auto a = make_arena<t>();
        a.invoke<"bind">();
        a.ui.feed(F(1, 0)); // 只有 a 完成
        std::println("  a 先完成（b 还没说话）：记录=[{}]   ← a 被挡住，不立即发",
                     uhi::to_string(a.ui.events()));
        assert(a.n_a == 0);
        a.ui.feed(F(2, 10)); // b 完成 ⇒ b 赢
        show("refines<a,b> b 来了", uhi::to_string(a.ui.events()), "b,a");
        assert(a.n_b == 1 && a.n_a == 0);
    }
    {
        using t = refines<g_a2, g_b2>;
        auto a = make_arena<t>();
        a.invoke<"bind">();
        a.ui.feed(F(1, 0)); // 只有 a 完成
        a.ui.feed(F(0, 100)); // b 一直不来，也没有 tick
        std::println("  a 完成、b 死活不来、又不 tick：记录=[{}]  ← 永远挂着",
                     uhi::to_string(a.ui.events()));
        assert(a.n_a == 0);
        a.ui.tick(time_point{} + std::chrono::milliseconds{500});
        std::println("  tick 之后（走 hold/兜底或识别器自己的超时）… 见下一步");
    }
    std::println("  ⇒ 优先级的机制是**延迟**：抽象的那个等具体的那个死。\n");

    // ── S4：互斥但不细化 ⇒ 跨帧先到先得，后来者没有机会 ─────────────
    std::println("── S4  one_of 但只互斥：a 先完成，b 就没有机会了 ──");
    {
        using t = uhi::one_of<g_a2, g_b2>; // 互斥、无细化
        auto a = make_arena<t>();
        a.invoke<"bind">();
        a.ui.feed(F(1, 0)); // a 完成 ⇒ 立刻赢（没有等谁）
        a.ui.feed(F(2, 10)); // b 才完成 ⇒ 已经晚了
        show("one_of 跨帧", uhi::to_string(a.ui.events()), "a,b");
        assert(a.n_a == 1 && a.n_b == 0);
    }
    std::println("  ⇒ 想让 b 赢，只能让 a **等** b（= 声明 refines<a,b>）。");
    std::println("     没有\"a 不细化 b、但 a 让位 b\"这种关系。\n");

    // ── S5：order 从哪来 ─────────────────────────────────────────────
    std::println("── S5  order 是编译期常量 ──");
    using t1 = uhi::one_of<g_a, g_b>;
    using t2 = refines<g_a2, g_b2>;
    std::println("  one_of<a,b> 的蓝图：{}", uhi::ui<t1>::blueprint.view());
    std::println("  refines<a,b> 的蓝图：{}", uhi::ui<t2>::blueprint.view());
    std::println("  ⇒ order 在 cells_of 里由拓扑排序算出，存进 stage 的静态数组成员顺序，");
    std::println("     运行期没有任何接口能改它（要改只能改类型）。\n");

    std::println("结论：");
    std::println("  ① 细化 = 优先级（机制是延迟），够用而且语义自洽；");
    std::println("  ② 互斥对之间只有\"先到先得\"，同帧平局靠声明顺序；");
    std::println("  ③ 想要\"非细化的优先级\"（数值权重），当前表达不了；");
    std::println("  ④ order 是编译期常量，运行期改不了。");
    return 0;
}
