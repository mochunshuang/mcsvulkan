// ═══════════════════════════════════════════════════════════════════════
//  table_ops.cpp —— 把模板里"表操作"逐步打印出来：每一步的中间值长什么样
//
//  三个问题：
//    ① 组合子拼表，拼的是什么？（把每一步的 plan 打印出来）
//    ② 为什么"拼接 + 平移"就够了？（换一个反例看漏平移会怎样）
//    ③ 拼好的扁平表怎么变成裁决用的两张矩阵？（拓扑序怎么来的）
//
//  编译：g++ -std=c++26 -O0 table_ops.cpp -o table_ops -lstdc++exp
// ═══════════════════════════════════════════════════════════════════════
#include <cstdio>
#include <print>
#include <string>

constexpr int MAXK = 8;
constexpr int MAXR = 64;

// ═══════════════════════════════════════════════════════════════════════
// ① 表的形态：**扁平的，不是矩阵**
//
//   拼装的时候用扁平表：kinds 是一行名字，rels 是一行三元组（from, to, 对称?）。
//   下标就是"这个 kind 在 kinds 里的位置"——**位置即身份**。
// ═══════════════════════════════════════════════════════════════════════
struct KindInfo
{
    const char *name = "";
};
struct Rel
{
    int from = 0, to = 0;
    bool sym = false; // false = from 等 to（细化）；true = 互斥
};
struct Plan
{
    int n = 0;
    KindInfo k[MAXK]{};
    int nr = 0;
    Rel r[MAXR]{};
};

// 拼接：把 b 的 kind 和 rel 接在 a 后面。**不动任何下标。**
constexpr Plan operator+(Plan a, const Plan &b)
{
    for (int i = 0; i < b.n; ++i)
        a.k[a.n + i] = b.k[i];
    a.n += b.n;
    for (int e = 0; e < b.nr; ++e)
        a.r[a.nr + e] = b.r[e];
    a.nr += b.nr;
    return a;
}

// 平移：把表里所有关系下标加同一个 base。
constexpr Plan shifted(Plan p, int base)
{
    for (int e = 0; e < p.nr; ++e)
    {
        p.r[e].from += base;
        p.r[e].to += base;
    }
    return p;
}

// ── 打印一张扁平表 ─────────────────────────────────────────────────
void dump(std::string_view label, const Plan &p)
{
    std::print("  {:<26} n={} [", label, p.n);
    for (int i = 0; i < p.n; ++i)
        std::print("{}{}:{}", i ? " " : "", i, p.k[i].name);
    std::print("]  rels=[");
    for (int e = 0; e < p.nr; ++e)
        std::print("{}{}{}{}", e ? " " : "", p.k[p.r[e].from].name,
                   p.r[e].sym ? "~" : "->", p.k[p.r[e].to].name);
    std::println("]");
}

// ═══════════════════════════════════════════════════════════════════════
// ② 三个组合子：每个只做"拼接 + 平移"，外加自己那类关系
// ═══════════════════════════════════════════════════════════════════════
struct K_click
{
    static constexpr const char *name = "click";
};
struct K_dbl
{
    static constexpr const char *name = "dbl";
};
struct K_drag
{
    static constexpr const char *name = "drag";
};
struct K_a
{
    static constexpr const char *name = "a";
};
struct K_b
{
    static constexpr const char *name = "b";
};
struct K_d
{
    static constexpr const char *name = "d";
};

// 叶子：贡献 1 个 kind、0 条关系、自己在局部下标 0
template <class K>
struct leaf
{
    static consteval Plan plan()
    {
        Plan p{};
        p.n = 1;
        p.k[0] = {K::name};
        return p;
    }
};

// 细化：A 贴在自己的位置，B 贴在 A 后面；然后 A 里每个 × B 里每个 加一条 A→B
template <class A, class B>
struct refines
{
    static consteval Plan plan()
    {
        const Plan pa = A::plan();                // A 在局部 0
        const Plan pb = shifted(B::plan(), pa.n); // B 在局部 pa.n
        Plan p = pa + pb;
        for (int i = 0; i < pa.n; ++i)
            for (int j = pa.n; j < p.n; ++j)
                p.r[p.nr++] = {i, j, false};
        return p;
    }
};

// 并行：什么都不加，只是拼起来
template <class... Ts>
struct all_of
{
    static consteval Plan plan()
    {
        Plan p{};
        ((p = p + shifted(Ts::plan(), p.n)), ...);
        return p;
    }
};

// 互斥：先按顺序拼（记住每棵子树的起点和长度），再两两加对称关系
template <class... Ts>
struct one_of
{
    static consteval Plan plan()
    {
        Plan p{};
        int off[MAXK]{}, cnt[MAXK]{}, i = 0;
        ((off[i] = p.n, p = p + shifted(Ts::plan(), p.n), cnt[i] = p.n - off[i], ++i),
         ...);
        for (int a = 0; a < i; ++a)
            for (int b = a + 1; b < i; ++b)
                for (int x = off[a]; x < off[a] + cnt[a]; ++x)
                    for (int y = off[b]; y < off[b] + cnt[b]; ++y)
                        p.r[p.nr++] = {x, y, true};
        return p;
    }
};

// ═══════════════════════════════════════════════════════════════════════
// ③ 反例：漏了平移会怎样
// ═══════════════════════════════════════════════════════════════════════
template <class... Ts>
struct one_of_buggy // 拼接时**不**平移子表
{
    static consteval Plan plan()
    {
        Plan p{};
        int off[MAXK]{}, cnt[MAXK]{}, i = 0;
        ((off[i] = p.n, p = p + Ts::plan(), cnt[i] = p.n - off[i], ++i),
         ...); // ← 少了 shifted
        for (int a = 0; a < i; ++a)
            for (int b = a + 1; b < i; ++b)
                for (int x = off[a]; x < off[a] + cnt[a]; ++x)
                    for (int y = off[b]; y < off[b] + cnt[b]; ++y)
                        p.r[p.nr++] = {x, y, true};
        return p;
    }
};

// ═══════════════════════════════════════════════════════════════════════
// ④ 扁平表 → 裁决用的两张矩阵 + 一个拓扑序
// ═══════════════════════════════════════════════════════════════════════
template <int N>
struct Tables
{
    bool waits[N][N]{};
    bool excl[N][N]{};
    int order[N]{};
};

template <class Tree>
consteval auto build()
{
    constexpr Plan P = Tree::plan();
    constexpr int N = P.n;
    Tables<N> t{};

    // 关系 → 两张矩阵
    for (int e = 0; e < P.nr; ++e)
        if (P.r[e].sym)
            t.excl[P.r[e].from][P.r[e].to] = t.excl[P.r[e].to][P.r[e].from] = true;
        else
            t.waits[P.r[e].from][P.r[e].to] = true;

    // order：waits 图上"什么都不等"的先 ⇒ 最具体先
    int indeg[MAXK]{};
    for (int e = 0; e < P.nr; ++e)
        if (!P.r[e].sym)
            ++indeg[P.r[e].from];
    int cnt = 0;
    while (cnt < N)
    {
        bool moved = false;
        for (int i = 0; i < N; ++i)
            if (indeg[i] == 0)
            {
                indeg[i] = -1;
                t.order[cnt++] = i;
                moved = true;
                for (int e = 0; e < P.nr; ++e)
                    if (!P.r[e].sym && P.r[e].to == i)
                        --indeg[P.r[e].from];
            }
        if (!moved)
            break;
    }
    return t;
}

template <class Tree>
void dump_tables(std::string_view label)
{
    constexpr Plan P = Tree::plan();
    constexpr auto T = build<Tree>();
    constexpr int N = P.n;

    std::println("  {} （{} 个 kind）", label, N);
    std::print("     waits     ");
    for (int j = 0; j < N; ++j)
        std::print("{:>7}", P.k[j].name);
    std::println("");
    for (int i = 0; i < N; ++i)
    {
        std::print("     {:>8}  ", P.k[i].name);
        for (int j = 0; j < N; ++j)
            std::print("{:>7}", T.waits[i][j] ? "yes" : ".");
        std::println("");
    }
    std::print("     excl      ");
    for (int j = 0; j < N; ++j)
        std::print("{:>7}", P.k[j].name);
    std::println("");
    for (int i = 0; i < N; ++i)
    {
        std::print("     {:>8}  ", P.k[i].name);
        for (int j = 0; j < N; ++j)
            std::print("{:>7}", T.excl[i][j] ? "yes" : ".");
        std::println("");
    }
    std::print("     order（最具体先）: ");
    for (int i = 0; i < N; ++i)
        std::print("{} ", P.k[T.order[i]].name);
    std::println("\n");
}

// ═══════════════════════════════════════════════════════════════════════
int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::println("╔══════════════════════════════════════════════════════════════╗");
    std::println("║ 表操作：每一步的中间值                                        ║");
    std::println("╚══════════════════════════════════════════════════════════════╝\n");

    // ── 第一幕：单叶子 ────────────────────────────────────────────
    std::println("── 第一幕：叶子 ──");
    std::println("  leaf<click>::plan() 的返回值：");
    dump("leaf<click>", leaf<K_click>::plan());
    dump("leaf<dbl>", leaf<K_dbl>::plan());
    dump("leaf<drag>", leaf<K_drag>::plan());
    std::println("  每片叶子：n=1，自己在局部下标 0，没有任何关系。\n");

    // ── 第二幕：refines 拼两张 ────────────────────────────────────
    std::println("── 第二幕：refines<click, dbl> ──");
    std::println("  步骤：");
    std::println("    A = leaf<click>.plan()            → n=1 [0:click]");
    std::println("    B = shifted(leaf<dbl>.plan(), 1) → n=1 [0:dbl] 但内部下标 +1");
    std::println("    p = A + B                        → n=2 [0:click 1:dbl]");
    std::println("    再给 A 的每个 × B 的每个 加一条 → 0->1");
    dump("refines<click,dbl>", refines<leaf<K_click>, leaf<K_dbl>>::plan());
    std::println(
        "  关系写成 0->1，不是 \"click->dbl\"：表里只有位置，名字只是给人看的。\n");

    // ── 第三幕：把上一幕当成黑盒，再和 drag 组合 ───────────────────
    std::println("── 第三幕：one_of< refines<click,dbl>, drag > ──");
    std::println("  步骤：");
    std::println(
        "    A = refines<click,dbl>.plan()      → n=2 [0:click 1:dbl]  rels 0->1");
    std::println("    贴到 base=0                         → 下标不变");
    std::println("    B = drag.plan()                    → n=1 [0:drag]");
    std::println("    贴到 base=2                         → 全部下标 +2");
    std::println("    两两加互斥（A 的 2 个 × B 的 1 个）  → 0~2, 1~2");
    dump("one_of<refines, drag>",
         one_of<refines<leaf<K_click>, leaf<K_dbl>>, leaf<K_drag>>::plan());
    std::println(
        "  注意 0->1 这条**没有被碰过**：它只在 A 的内部，A 贴到 base=0 就不需要改。");
    std::println("  而 0~2 / 1~2 是这一层新加的，直接用的绝对下标。\n");

    // ── 第四幕：为什么必须平移（反例）────────────────────────────
    std::println("── 第四幕：漏掉平移会怎样 ──");
    std::println("  目标：one_of< d, one_of<a,b> >   期望得到 d~a 和 d~b，且 a~b 保留");
    dump("正确（有 shifted）", one_of<leaf<K_d>, one_of<leaf<K_a>, leaf<K_b>>>::plan());
    dump("错误（无 shifted）",
         one_of_buggy<leaf<K_d>, one_of<leaf<K_a>, leaf<K_b>>>::plan());
    std::println("  错误版：内层 one_of<a,b> 给出的 a~b 是**局部下标 0~1**；");
    std::println(
        "  它被贴到 base=1 之后，0~1 就被读成 d~a —— a~b 消失了，还凭空多出一条 d~a。");
    std::println("  这就是我真实踩过的 bug 的形状。\n");

    // ── 第五幕：扁平表 → 矩阵 ─────────────────────────────────────
    std::println("── 第五幕：扁平表变成裁决用的矩阵 ──");
    dump_tables<one_of<refines<leaf<K_click>, leaf<K_dbl>>, leaf<K_drag>>>(
        "one_of<refines<click,dbl>, drag>");
    std::println("  order 是怎么来的：indeg[i] = \"i 等了几个\"。");
    std::println("    click 等 dbl ⇒ indeg[click]=1；dbl、drag 都不等 ⇒ 0");
    std::println("    第一轮：dbl、drag 是 0 ⇒ 取出；取出 dbl 后 click 的 indeg 减到 0");
    std::println("    第二轮：取出 click  ⇒ order = [dbl, drag, click] = 最具体先");
    std::println("  两种表示各服务一个阶段：");
    std::println("    · 扁平表（kinds + rels）—— 拼装期用，好拼、好平移");
    std::println("    · 两张矩阵 —— 裁决期用，blocked()/win() 按行查，O(1)");
    return 0;
}
