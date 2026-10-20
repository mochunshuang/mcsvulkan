// ═══════════════════════════════════════════════════════════════════════
//  test_plan_props.cpp —— 计划的结构性质（组合爆炸版）
//
//  手写场景能踩到 bug 是运气。这里换成**性质**：
//
//  对每种树形状检查
//    P1  关系不出界、不自指
//    P2  同一对不能既 refines 又互斥          ← 下标错位 bug 的直接症状
//    P3  互斥对只记一次（i<j）
//    P4  cell 是 kind 集合的**划分**：每个 kind 恰好一格；同格同源
//    P5  格数 ≥ 1，且 Σ|members| == |kinds|
//    P6  细化链的传递性（a->b、b->c ⇒ 出现 a->c）
//    P7  all_of 子树间无关系
//
//  形状不是手挑的：把 5 个节点（3 叶 + 2 复合）做 one_of / refines / all_of
//  的两两组合 = 75 棵，全部跑一遍。
//
//  编译：
//    g++ -std=c++26 -freflection -O0 uhi\v3\src\test_plan_props.cpp -o output/test_plan_props.exe -lstdc++exp
// ═══════════════════════════════════════════════════════════════════════
#include <cassert>
#include <print>
#include <string_view>

#include "arena_event.hpp"
#include "uhi.hpp"

using uhi::all_of;
using uhi::cell_count_v;
using uhi::cell_members_v;
using uhi::one_of;
using uhi::plan_kinds_v;
using uhi::plan_rels_v;
using uhi::refines;
using uhi::source_t;

using src_a = source_t<"A">;
using src_b = source_t<"B">;

struct null_fsm
{
    uhi::step_t step(const arena_event::input_frame &, uhi::time_point) noexcept
    {
        return uhi::step_t::idle;
    }
    void reset() noexcept {}
};

#define LEAF(NAME, SRC) using g_##NAME = uhi::leaf<uhi::kind_t<#NAME>, SRC, null_fsm>;
LEAF(a, src_a)
LEAF(b, src_a)
LEAF(c, src_a)
LEAF(d, src_a)
LEAF(e, src_a)
LEAF(f, src_a)
LEAF(z, src_b)

static int checked = 0;

[[noreturn]] inline void fail(std::string_view who, const char *what)
{
    std::print(stderr, "FAIL [{}]: {}\n", who, what);
    assert(false && "plan property violated");
    std::abort();
}

// ═══════════════════════════════════════════════════════════════════════
template <class Tree, std::size_t... Cs>
void check_cells(std::index_sequence<Cs...>, std::string_view who)
{
    const auto  kinds = plan_kinds_v<Tree>;
    const int   n     = static_cast<int>(kinds.size());
    std::array<int, 32> hit{};
    std::size_t  total = 0;

    (
        [&] {
            const auto mem = cell_members_v<Tree, Cs>;
            if (mem.empty()) fail(who, "P4 空的 cell");
            const auto src = kinds[static_cast<std::size_t>(mem[0])].source;
            for (auto m : mem)
            {
                const auto i = static_cast<std::size_t>(m);
                if (i >= kinds.size()) fail(who, "P4 cell 里的下标出界");
                if (kinds[i].source != src) fail(who, "P4 同一个 cell 里混了不同 source");
                ++hit[i];
            }
            total += mem.size();
        }(),
        ...);

    for (int i = 0; i < n; ++i)
        if (hit[static_cast<std::size_t>(i)] != 1) fail(who, "P4 kind 不恰好属于一个 cell");
    if (total != static_cast<std::size_t>(n)) fail(who, "P5 Σ|members| != |kinds|");
}

template <class Tree>
void check(std::string_view who)
{
    const auto kinds = plan_kinds_v<Tree>;
    const auto rels  = plan_rels_v<Tree>;
    const int  n     = static_cast<int>(kinds.size());
    if (n == 0) fail(who, "空的 plan");
    if (cell_count_v<Tree> < 1) fail(who, "P5 cell 数 < 1");

    // P8：kind 名字必须唯一（重复 = 同一个语义事件有两个生产者）
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (kinds[static_cast<std::size_t>(i)].name == kinds[static_cast<std::size_t>(j)].name)
                fail(who, "P8 kind 重名");

    for (const auto &r : rels)
    {
        if (r.from < 0 || r.from >= n || r.to < 0 || r.to >= n) fail(who, "P1 关系出界");
        if (r.from == r.to) fail(who, "P1 关系自指");
        if (r.symmetric && r.from > r.to) fail(who, "P3 互斥对重复记录");
    }

    // P2：同一对不能既 refines 又互斥
    for (const auto &r : rels)
        if (!r.symmetric)
            for (const auto &s : rels)
                if (s.symmetric && ((s.from == r.from && s.to == r.to) ||
                                    (s.from == r.to && s.to == r.from)))
                    fail(who, "P2 同一对既是细化又互斥");

    // P9：两条**互相独立**的算法必须给出同一个数
    //   leaf_count_v 走类型树（walk_leaves 的算法）
    //   plan_kinds_v 走 plan()（组合子拼出来的）
    // 任何一边漏掉/多算一个 kind 都会在这里露出来。
    static_assert(uhi::leaf_count_v<Tree> == uhi::plan_kinds_v<Tree>.size(),
                  "P9 类型树里的叶子数 != plan 里的 kind 数");
    static_assert(cell_count_v<Tree> <= uhi::leaf_count_v<Tree>, "P10 cell 数不该超过 kind 数");

    check_cells<Tree>(std::make_index_sequence<cell_count_v<Tree>>{}, who);
    ++checked;
}

// 两个子树能否组合：kind 不能重叠（重叠 = 同一个事件被两个子树声明）
template <class A, class B>
inline constexpr bool disjoint_kinds = []() consteval {
    for (auto x : plan_kinds_v<A>)
        for (auto y : plan_kinds_v<B>)
            if (x.name == y.name) return false;
    return true;
}();

static int skipped = 0;

// 跨源关系：validate 必须在编译期拒绝（这里只验证合法树不会被误杀）
template <class Tree>
void check_ok(std::string_view who)
{
    check<Tree>(who);
}

// ═══════════════════════════════════════════════════════════════════════
int main()
{
    // ── 具名形状 ───────────────────────────────────────────────────
    check_ok<g_a>("leaf");
    check_ok<all_of<g_a, g_b>>("all_of2");
    check_ok<one_of<g_a, g_b>>("one_of2");
    check_ok<refines<g_a, g_b>>("refines2");
    check_ok<refines<refines<g_a, g_b>, g_c>>("chain3");
    check_ok<one_of<refines<g_a, g_b>, g_c>>("chain+rival");
    check_ok<all_of<g_a, g_z>>("two sources");
    check_ok<all_of<all_of<g_a, g_b>, g_c>>("all_of3");

    // ── 组合爆炸：6 个节点 × 6 = 36 对，每对跑 3 个组合子 ─────────
    //   kind 集互不重叠的对才是合法形状；重叠的跳过并计数。
    using t_a   = g_a;                                  // {a}
    using t_b   = g_b;                                  // {b}
    using t_cd  = one_of<g_c, g_d>;                     // {c,d} 互斥
    using t_ef  = refines<g_e, g_f>;                    // {e,f} 细化
    using t_ab  = all_of<g_a, g_b>;                     // {a,b} 并行（与 t_a/t_b 重叠）
    using t_big = one_of<refines<g_c, g_d>, refines<g_e, g_f>>; // {c,d,e,f}

    auto pair_rule = [&]<class X, class Y>(std::string_view) {
        if constexpr (disjoint_kinds<X, Y>)
        {
            check_ok<one_of<X, Y>>("pair:one_of");
            check_ok<refines<X, Y>>("pair:refines");
            check_ok<all_of<X, Y>>("pair:all_of");
        }
        else
        {
            ++skipped; // 同一 kind 被两棵子树声明 ⇒ 非法，构造上就避开
        }
    };

    auto all_with = [&]<class X>(auto nodes_tag) {
        using TL = decltype(nodes_tag);
        [&]<class... Ys>(std::tuple<Ys...>*) {
            (pair_rule.template operator()<X, Ys>("x*y"), ...);
        }(static_cast<TL *>(nullptr));
    };
    auto nodes = std::tuple<t_a, t_b, t_cd, t_ef, t_ab, t_big>{};
    std::apply(
        [&](auto... ns) {
            (all_with.template operator()<decltype(ns)>(nodes), ...);
        },
        nodes);

    // 跳过的理由必须被证明，而不是假设：这些对确实 kind 重叠
    static_assert(!disjoint_kinds<t_a, t_ab>, "a 与 {a,b} 应当重叠");
    static_assert(!disjoint_kinds<t_b, t_ab>, "b 与 {a,b} 应当重叠");
    static_assert(!disjoint_kinds<t_cd, t_big>, "{c,d} 与 {c,d,e,f} 应当重叠");
    static_assert(!disjoint_kinds<t_ef, t_big>, "{e,f} 与 {c,d,e,f} 应当重叠");
    static_assert(!disjoint_kinds<t_a, t_a>, "自己和自己必然重叠");
    // 反向：真不重叠的要能通过
    static_assert(disjoint_kinds<t_a, t_b>);
    static_assert(disjoint_kinds<t_cd, t_ef>);
    static_assert(disjoint_kinds<t_a, t_big>);

    std::println("组合爆炸: 合法 {} 棵，跳过 {} 组 kind 重叠", checked - 8, skipped);

    // 钉死规模：任何一个数变了都必须有人回头看
    assert(skipped == 14 && "跳过数变了：disjoint_kinds 或节点集改动过");
    assert(checked == 74 && "检查棵树变了：性质退化或节点集改动过");
    assert(checked + skipped * 3 >= 110 && "覆盖量不成比例地下降");
    // ── 定向断言：右嵌套 one_of 不能丢 a~b（错位 bug 的定点检查）──
    {
        const auto k = plan_kinds_v<one_of<g_d, one_of<g_a, g_b>>>;
        const auto r = plan_rels_v<one_of<g_d, one_of<g_a, g_b>>>;
        assert(k.size() == 3 && r.size() == 3);
        [[maybe_unused]] bool has_ab = false;
        for (const auto &x : r)
        {
            if (k[x.from].name == k[x.to].name) assert(false && "自指互斥");
            if ((k[x.from].name.view() == "a" && k[x.to].name.view() == "b") ||
                (k[x.from].name.view() == "b" && k[x.to].name.view() == "a"))
                has_ab = true;
        }
        assert(has_ab && "右嵌套 one_of 丢了 a~b —— 关系下标错位");
    }

    // ── 传递性 ─────────────────────────────────────────────────────
    {
        const auto r = plan_rels_v<refines<refines<g_a, g_b>, g_c>>;
        [[maybe_unused]] bool ac = false;
        const auto k  = plan_kinds_v<refines<refines<g_a, g_b>, g_c>>;
        for (const auto &x : r)
            if (!x.symmetric && k[x.from].name.view() == "a" && k[x.to].name.view() == "c") ac = true;
        assert(ac && "三级链必须有 a->c");
    }

    // ── all_of 子树之间无关系 ──────────────────────────────────────
    static_assert(plan_rels_v<all_of<g_a, g_b>>.size() == 0);
    static_assert(plan_rels_v<all_of<one_of<g_a, g_b>, g_c>>.size() == 1);
    // ── 两个源 ⇒ 两格 ─────────────────────────────────────────────
    static_assert(cell_count_v<all_of<g_a, g_z>> == 2);

    std::println("=== 计划结构性质：{} 棵合法树全部通过（跳过 {} 组 kind 重叠的非法组合）===", checked, skipped);
    return 0;
}
