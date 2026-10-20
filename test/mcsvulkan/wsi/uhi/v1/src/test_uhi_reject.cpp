// ═══════════════════════════════════════════════════════════════════════
//  test_uhi_reject.cpp —— 坏结构必须编译不过（每个 CASE 单独编译）
//
//    CASE 1  跨通道细化     refines<mouse 的 a, keyboard 的 k>
//    CASE 2  跨通道互斥     one_of<mouse 的 a, keyboard 的 k>
//    CASE 3  重复生产者     all_of<a, a>
//
//  「细化成环」无法构造：每个 leaf 在树里只有一个父亲，而边只从父亲指向孩子，
//   所以 after 图天然无环。validate 里那条环检测是防御性死代码。
//
//  验证方式：
//    g++ -std=c++26 -freflection -I. "-DCASE=1" -fsyntax-only test_uhi_reject.cpp
//    → error: uncaught exception of type 'std::meta::exception' ... 不同通道上
// ═══════════════════════════════════════════════════════════════════════
#include "uhi.hpp"

struct never
{
    uhi::step_t step(int, uhi::time_point) noexcept { return uhi::step_t::idle; }
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
#endif

static_assert(uhi::valid_v<bad>, "坏结构必须在这里失败");
int main() {}
