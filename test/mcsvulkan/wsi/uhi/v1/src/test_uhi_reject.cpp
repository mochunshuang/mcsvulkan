// ═══════════════════════════════════════════════════════════════════════
//  test_uhi_reject.cpp —— 坏的**结构**与坏的**契约**必须编译不过
//
//    CASE 1  跨通道细化       refines<mouse 的 a, keyboard 的 k>
//    CASE 2  跨通道互斥       one_of<mouse 的 a, keyboard 的 k>
//    CASE 3  重复生产者       all_of<a, a>
//    CASE 4  载荷类型不配对   kind_t<"a", click_event> 配 FSM::payload 是别的类型
//    CASE 5  handler 会抛     on_a 少了 noexcept（胜利回调是无异常 C 函数指针）
//    CASE 6  handler 收错类型 on_a 收 const float&，但载荷是 click_event
//
//  「细化成环」无法构造：每个 leaf 在树里只有一个父亲，边只从父亲指向孩子，
//   所以 awaits 图天然无环。validate 里那条环检测是防御性死代码。
//
//  验证方式：
//    g++ -std=c++26 -freflection "-DCASE=1" -fsyntax-only test_uhi_reject.cpp
//    → error: uncaught exception of type 'std::meta::exception' … 不同通道上
//    CASE 4/5/6 → static_assert 的中文说明
// ═══════════════════════════════════════════════════════════════════════
#include <cstdint>

#include "uhi.hpp"

#include "../../../../../../include/detail/meta/make_aggregate.hpp"

using mcs::vulkan::meta::field;
using mcs::vulkan::meta::make_aggregate;
using mcs::vulkan::meta::method;

struct click_event
{
    double x = 0.0;
};

// ── 帧：只要能被 frame_traits 读时间就行 ────────────────────────────
struct frame
{
    uhi::time_point t{};
};
template <>
struct uhi::frame_traits<frame>
{
    static uhi::time_point when(const frame &f) noexcept { return f.t; }
};

struct never
{
    uhi::step_t step(const frame &, uhi::time_point) noexcept { return uhi::step_t::idle; }
    void        reset() noexcept {}
};

using A = uhi::leaf<uhi::kind_t<"a">, uhi::source_t<"mouse">, never>;
using B = uhi::leaf<uhi::kind_t<"b">, uhi::source_t<"mouse">, never>;
using K = uhi::leaf<uhi::kind_t<"k">, uhi::source_t<"keyboard">, never>;

#if CASE == 1
using bad = uhi::refines<A, K>; // 跨通道的依赖：它们永远不会相遇
#elif CASE == 2
using bad = uhi::one_of<A, K>; // 跨通道的互斥：同上
#elif CASE == 3
using bad = uhi::all_of<A, A>; // 同一个语义事件有两个生产者
#elif CASE == 4
// FSM::payload 是 float，kind 声明的载荷是 click_event
struct wrong_payload_fsm
{
    float       payload{};
    uhi::step_t step(const frame &, uhi::time_point) noexcept { return uhi::step_t::idle; }
    void        reset() noexcept {}
};
using bad = uhi::leaf<uhi::kind_t<"a", click_event>, uhi::source_t<"mouse">, wrong_payload_fsm>;
#elif CASE == 5 || CASE == 6
// payload 配对正确，问题出在 handler 那一侧
struct click_fsm
{
    click_event payload{};
    uhi::step_t step(const frame &, uhi::time_point) noexcept { return uhi::step_t::idle; }
    void        reset() noexcept {}
};
using bad = uhi::leaf<uhi::kind_t<"a", click_event>, uhi::source_t<"mouse">, click_fsm>;
#endif

#if CASE <= 3
static_assert(uhi::valid_v<bad>, "坏结构必须在这里失败");
int main() {}
#else
// CASE 4/5/6 的失败点不在 plan，而在**绑定**：要真的 bind + feed 才会实例化
inline auto make_arena()
{
    return make_aggregate<"arena">(
        field<"ui">(uhi::ui<bad>{}),
        field<"n">(int{}),
#if CASE == 6
        // CASE 6：收 const float&，但载荷是 click_event ⇒ invoke 找不到可调用成员
        method<"on_a">([](auto &self, const float &) noexcept { ++self.n; }),
#elif CASE == 5
        // CASE 5：少了 noexcept ⇒ sink 的 noexcept 函数类型不成立
        method<"on_a">([](auto &self, const click_event &) { ++self.n; }),
#else
        // CASE 4：handler 完全正确，问题只在 FSM::payload 的类型
        method<"on_a">([](auto &self, const click_event &) noexcept { ++self.n; }),
#endif
        method<"bind">([](auto &self) noexcept { self.ui.bind(self); }));
}

int main()
{
    auto arena = make_arena();
    arena.invoke<"bind">();
    arena.ui.feed(frame{});
}
#endif
