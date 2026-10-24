// ═══════════════════════════════════════════════════════════════════════════
//  test_matrix_series.cpp —— 连续剧：看得见的表
//
//  这不是再讲一遍原理。它把两处数据直接摆出来，每集只回答一个问题：
//    ① 建表   ：plan 里的边怎么一步步变成 beats 矩阵（闭包加了几条边，数出来）
//    ② 建表后 ：每喂一帧，内部表 alive / pending / begin / end 怎么变，
//                播了什么，以及"为什么是它而不是别人"
//
//  没有新增任何观测钩子：stage::alive/pending/begin/end 本来就是 public，
//  Tree::plan() 与 law_of<Tree>::beats 也是 public，events() 一直都在。
//  每集都有断言，失败直接返回非零 —— 它同时是回归测试。
//
//  编译：g++ -std=c++26 -freflection -O2 test_matrix_series.cpp
//        -o output/test_matrix_series -lstdc++exp
// ═══════════════════════════════════════════════════════════════════════════
#define main uhi_matrix_engine_main
#include "uhi_matrix.cpp"
#undef main

#include <algorithm>
#include <cstdlib>
#include <new>

// ── 数堆分配：只在测试里替换全局 new/delete ────────────────────────────
//   目的只有一个：钉住"热路径零分配" —— feed/tick 一帧不碰堆。
//   只计数、照常转发给 malloc，所以其余行为不变（格式化缓冲区等照旧）。
namespace
{
    std::size_t g_heap_allocs = 0;
}
void *operator new(std::size_t n)
{
    ++g_heap_allocs;
    if (void *p = std::malloc(n))
        return p;
    throw std::bad_alloc{};
}
void operator delete(void *p) noexcept
{
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept
{
    std::free(p);
}
void *operator new[](std::size_t n)
{
    return ::operator new(n);
}
void operator delete[](void *p) noexcept
{
    ::operator delete(p);
}
void operator delete[](void *p, std::size_t) noexcept
{
    ::operator delete(p);
}

namespace show
{
    constexpr std::string pad(std::string_view s, std::size_t w)
    {
        std::string r{s};
        while (r.size() < w)
            r += ' ';
        return r;
    }

    constexpr std::string ms(uhi::time_point t)
    {
        return t == uhi::time_point{} ? std::string{"-"} : std::to_string(t.count());
    }

    // 名字有两种：测试里手写的是 string_view，表里的是 static_string（8 B）
    constexpr std::string_view sv_of(std::string_view s)
    {
        return s;
    }
    constexpr std::string_view sv_of(const uhi::static_string &s)
    {
        return s.view();
    }

    template <class Names, class Mask>
    constexpr std::string set(const Names &names, const Mask &m,
                              std::string_view empty = "∅")
    {
        std::string s;
        for (std::size_t i = 0; i < names.size(); ++i)
            if (m.has(i))
                s += (s.empty() ? "" : ","), s += sv_of(names[i]);
        return s.empty() ? std::string{empty} : s;
    }

    template <std::size_t S>
    constexpr std::size_t edges(const std::array<uhi::bset<S>, S> &m)
    {
        std::size_t n = 0;
        for (std::size_t i = 0; i < S; ++i)
            for (std::size_t j = 0; j < S; ++j)
                n += m[i].has(j);
        return n;
    }

    inline int fails = 0;
    inline int checks = 0;
    inline void check(bool ok, const char *what)
    {
        ++checks;
        if (!ok)
        {
            ++fails;
            std::println("  ✗ 断言失败：{}", what);
        }
    }

    inline void head(std::string_view tag, std::string_view title)
    {
        // 用横线分隔，不用方框：标题里有中文，方框右边缘永远对不齐
        std::println("");
        std::println(
            "──────────────────────────────────────────────────────────────────");
        std::println(" {}  {}", tag, title);
        std::println(
            "──────────────────────────────────────────────────────────────────");
    }
} // namespace show

// ═══════════════════════════════════════════════════════════════════════════
//  表：快照 / 打印 / 变化 / 通道 / blocked
// ═══════════════════════════════════════════════════════════════════════════
template <class Tree>
struct table
{
    using law = uhi::law_of<Tree>;
    static constexpr std::size_t S = law::S;

    struct snap
    {
        uhi::bset<S> alive{}, pending{};
        std::array<uhi::time_point, S> begin{}, end{};
    };

    static snap take(const uhi::stage<Tree> &st)
    {
        return snap{st.alive, st.pending, st.begin, st.end};
    }

    static void print(std::string_view label, const snap &s)
    {
        std::println(" {} {}", show::pad(label, 8),
                     "idx kind         alive pending  begin/end");
        for (std::size_t i = 0; i < S; ++i)
            std::println("          {}   {} {}     {}        {}/{}", i,
                         show::pad(show::sv_of(law::names[i]), 12),
                         s.alive.has(i) ? "•" : "·", s.pending.has(i) ? "•" : "·",
                         show::ms(s.begin[i]), show::ms(s.end[i]));
    }

    // 只打变化的格子 —— 这就是"内部表数据的变化"
    static void diff(const snap &a, const snap &b)
    {
        std::println(" 变化");
        for (std::size_t i = 0; i < S; ++i)
        {
            std::string line;
            auto add = [&](std::string_view col, std::string from, std::string to) {
                if (from == to)
                    return;
                if (!line.empty())
                    line += "  ";
                line += std::string{col} + " " + from + "→" + to;
            };
            add("alive", a.alive.has(i) ? "•" : "·", b.alive.has(i) ? "•" : "·");
            add("pending", a.pending.has(i) ? "•" : "·", b.pending.has(i) ? "•" : "·");
            add("begin", show::ms(a.begin[i]), show::ms(b.begin[i]));
            add("end", show::ms(a.end[i]), show::ms(b.end[i]));
            if (!line.empty())
                std::println("        {:<12}{}", law::names[i], line);
        }
    }

    // 这一帧的通道：直接用引擎那一个（不再自己按帧名重算一遍）
    template <class Frame>
    static consteval uhi::bset<S> scope()
    {
        return uhi::scope_of<Tree, Frame>();
    }

    // blocked = 活着的行并起来（公开状态 + 编译期矩阵，两下算出来）
    static uhi::bset<S> blocked_of(const uhi::bset<S> &alive)
    {
        uhi::bset<S> b{};
        for (std::size_t j = 0; j < S; ++j)
            if (alive.has(j))
                b |= law::beats[j];
        return b;
    }
};

// 一集的通用节拍：已经喂完一帧 → 打变化和播报
template <class Tree>
void after_frame(const uhi::debug_ui<Tree> &u, std::string_view label,
                 const typename table<Tree>::snap &before)
{
    using T = table<Tree>;
    std::println(" 输入   {}", label);
    T::diff(before, T::take(u.st));
    std::print(" 播报  ");
    if (u.events().empty())
        std::print("（无）");
    for (const auto &e : u.events())
        std::print(" {}:{}@{}({},{})", e.name, uhi::to_string(e.v), e.at.count(),
                   e.begin.count(), e.end.count());
    std::println("");
}

// 连通块：测试自己算一遍（并查集），和手写的分块对拍。
//   规则只有一句：两人之间只要有一条 beats 边（哪个方向都算），就是同一块。
template <class Tree>
std::vector<std::uint64_t> blocks_of()
{
    using law = uhi::law_of<Tree>;
    constexpr std::size_t S = law::S;
    std::array<std::size_t, S> parent{};
    for (std::size_t i = 0; i < S; ++i)
        parent[i] = i;
    auto root = [&](std::size_t x) {
        while (parent[x] != x)
            x = parent[x] = parent[parent[x]];
        return x;
    };
    for (std::size_t i = 0; i < S; ++i)
        for (std::size_t j = 0; j < S; ++j)
            if (law::beats[i].has(j) && root(i) != root(j))
                parent[root(i)] = root(j);
    std::vector<std::uint64_t> out;
    for (std::size_t i = 0; i < S; ++i)
        if (root(i) == i) // i 是某组的代表 ⇒ 一块
        {
            std::uint64_t m = 0;
            for (std::size_t j = 0; j < S; ++j)
                if (root(j) == i)
                    m |= std::uint64_t{1} << j;
            out.push_back(m);
        }
    std::sort(out.begin(), out.end());
    return out;
}

// 把一块（位掩码）印成 {a,b}
template <class Tree>
std::string block_str(std::uint64_t m)
{
    using law = uhi::law_of<Tree>;
    std::string s;
    for (std::size_t i = 0; i < law::S; ++i)
        if ((m >> i) & 1)
            s += (s.empty() ? "" : ","), s += show::sv_of(law::names[i]);
    return "{" + s + "}";
}

// 手写期望 = 每块的位掩码；算出来的必须一模一样（不只是个数相同）
template <class Tree, std::size_t N>
void expect_blocks(std::array<std::uint64_t, N> expect)
{
    auto got = blocks_of<Tree>();
    std::sort(expect.begin(), expect.end());
    bool ok = got.size() == N;
    for (std::size_t i = 0; ok && i < N; ++i)
        ok = got[i] == expect[i];
    std::print("     期望");
    for (auto m : expect)
        std::print(" {}", block_str<Tree>(m));
    std::print("   实际");
    for (auto m : got)
        std::print(" {}", block_str<Tree>(m));
    std::println("   {}", ok ? "✓" : "✗");
    show::check(ok, "B8 连通块手算对拍");
    show::check(static_cast<std::size_t>(N) == uhi::law_of<Tree>::cell_count,
                "B8 块数 == law::cell_count");
}

// 平局的两种声明顺序（R5 用）
template <class Tr>
auto clash_episode(std::string_view tag)
{
    using L = uhi::law_of<Tr>;
    constexpr std::size_t S = L::S;
    std::println(" {}", tag);
    std::println(
        "      idx kind 矩阵行（x = 这一行打得赢那一列：行 aa、列 bb ⇒ aa 赢 bb）");
    for (std::size_t i = 0; i < S; ++i)
    {
        std::print("      {}   {} ", i, show::pad(show::sv_of(L::names[i]), 4));
        for (std::size_t j = 0; j < S; ++j)
            std::print("{}", L::beats[i].has(j) ? 'x' : '.');
        std::println("   行 = {}", show::set(L::names, L::beats[i]));
    }
    uhi::debug_ui<Tr> u;
    u.feed(M(mouse_frame::press), at(3000));
    std::print("      播报 ");
    for (const auto &e : u.events())
        std::print(" {}:{}@{}({},{})", e.name, uhi::to_string(e.v), e.at.count(),
                   e.begin.count(), e.end.count());
    std::println("");
    // events() 是 span，指向 u 里面的缓冲；u 要销毁了，所以拷出来
    const auto ev = u.events();
    return std::vector<uhi::event>{ev.begin(), ev.end()};
}

// ═══════════════════════════════════════════════════════════════════════════
//  第一部分：建表（B1 … B7）
//
//  这段是"plan → 表"的全过程，全部在编译期。每一集都拿**手写的期望值**去对拍，
//  对不上就 throw —— 也就是编译不过，而不是运行时才发现。
//
//  这一部分故意用 4 片最小叶子（只有 plan 参与，识别器不参与），这样期望值
//  短到能一眼心算：a b c d。
// ═══════════════════════════════════════════════════════════════════════════
struct dmy
{
    using frame_type = mouse_frame;
    uhi::step_t step(const mouse_frame &, uhi::time_point) noexcept
    {
        return uhi::step_t::idle;
    }
    void reset() noexcept {}
};
using La = uhi::leaf<dmy, "a">;
using Lb = uhi::leaf<dmy, "b">;
using Lc = uhi::leaf<dmy, "c">;
using Ld = uhi::leaf<dmy, "d">;

// 把边集打成人能读的样子：用 plan 自己的名字，不依赖 law 的数组长度
template <class Plan, class Range>
consteval std::string fmt_edges(const Range &e, const Plan &p, std::string_view sep)
{
    std::string s;
    for (auto [a, b] : e)
    {
        if (!s.empty())
            s += ' ';
        s += p.kinds[a].name.view();
        s += sep;
        s += p.kinds[b].name.view();
    }
    return s.empty() ? std::string{"（无）"} : s;
}
// 和手写期望逐条对拍（顺序无关）
template <std::size_t N>
consteval bool eq_set(std::vector<std::pair<uhi::index_t, uhi::index_t>> got,
                      std::array<std::pair<uhi::index_t, uhi::index_t>, N> want)
{
    std::sort(got.begin(), got.end());
    std::sort(want.begin(), want.end());
    if (got.size() != N)
        return false;
    for (std::size_t i = 0; i < N; ++i)
        if (got[i] != want[i])
            return false;
    return true;
}

consteval std::string kinds_of(const uhi::plan_data &p)
{
    std::string s;
    for (auto &k : p.kinds)
    {
        if (!s.empty())
            s += ' ';
        s += k.name.view();
    }
    return s;
}

// ── B0：这一帧谁能播？—— 从一句话到三个集合 ─────────────────────────────
//
//  这一集补的是"过程"：目的（谁该落地）怎么落到数据（位图），
//  数据又怎么被两个动作变成答案。分四段：映射 → 收票 → 两个动作 → 结论。
consteval uhi::static_string b0_script()
{
    using law = uhi::law_of<tree_main>;
    static constexpr std::size_t S = law::S; // 6

    // ── 位号 ↔ 名字：三条 static_assert 钉死 ────────────────────
    static_assert(law::names[0] == "on_click");
    static_assert(law::names[1] == "on_dbl");
    static_assert(law::names[2] == "on_triple");

    // ── 两个 helper：mask / blocked_by ──────────────────────────
    constexpr auto mask = [](std::initializer_list<uhi::index_t> idx) {
        uhi::bset<S> m{};
        for (auto i : idx)
            m.set(i);
        return m;
    };
    constexpr auto blocked_by = [](const uhi::bset<S> &who) {
        uhi::bset<S> r{};
        for (uhi::index_t j = 0; j < S; ++j)
            if (who.has(j))
                r |= law::beats[j];
        return r;
    };

    // ── 这一帧的现场：四个 constexpr 常量 ──────────────────────
    constexpr auto alive = mask({0, 1, 2});
    constexpr auto done = mask({0});

    // NOTE: 受害者名单（被 who 中任何一个打赢的人）
    constexpr auto blocked = blocked_by(alive); // 被活着的人挡住的人

    // alive & ~blocked 是「活着的人里没被挡住的」，等于 {on_triple}。
    // 但 on_triple 这一帧没结论（不在 done 里），所以它不是候选，不能播。
    constexpr auto winners = done & ~blocked; // 完成的名单 和 不被阻塞的 交集

    /*
static_assert(alive.w[0]   == 0b000111ull, ...);  // 位 0,1,2 = 1
static_assert(done.w[0]    == 0b000001ull, ...);  // 位 0 = 1
static_assert(blocked.w[0] == 0b000011ull, ...);  // 位 0,1 = 1
static_assert(winners.none(), ...);               // 所有位 = 0
static_assert((alive & ~blocked).w[0] == 0b000100ull, ...); // 位 2 = 1

static_assert(alive.w[0] == ...);  // 位 0..63
static_assert(alive.w[1] == ...);  // 位 64..69
*/
    // w[0] 能代表全部，因为 S=6 时 bset<6> 只有一个字
    // ── 静态断言：中间值与答案全部钉死 ─────────────────────────
    static_assert(alive.w[0] == 0b000111ull, "alive 应为 {on_click,on_dbl,on_triple}");
    static_assert(done.w[0] == 0b000001ull, "done  应为 {on_click}");
    // NOTE: blocked 内 都至少被on_triple打赢一次
    static_assert(blocked.w[0] == 0b000011ull, "blocked 应为 {on_click,on_dbl}");
    static_assert(winners.none(), "winners 应为空");

    /*
alive   = 0b000111 = {on_click, on_dbl, on_triple}
blocked = 0b000011 = {on_click, on_dbl}
~blocked（只在 [0,6) 内）= 0b111100 = {on_triple, on_hover, on_wheel, on_key_A}

alive & ~blocked
= 0b000111 & 0b111100
= 0b000100
= {on_triple}
*/
    static_assert((alive & ~blocked).w[0] == 0b000100ull,
                  "alive∖blocked 应为 {on_triple}（不是候选）");

    // bool operator==(const bset &) const = default;
    static_assert(alive == mask({0, 1, 2}), "alive 应为 {on_click,on_dbl,on_triple}");
    static_assert(done == mask({0}), "done  应为 {on_click}");
    static_assert(blocked == mask({0, 1}), "blocked 应为 {on_click,on_dbl}");
    static_assert(winners == mask({}), "winners 应为空");
    static_assert((alive & ~blocked) == mask({2}),
                  "alive∖blocked 应为 {on_triple}（不是候选）");

    // ── 打印 helper：名字 / 位图 / 十进制，三者是同一份数据 ─────
    auto name_of = [&](const uhi::bset<S> &m) {
        return "{" + show::set(law::names, m, "") + "}"; // 空集 = "{}"
    };
    auto bits = [](const uhi::bset<S> &m) {
        std::string s = "0b";
        for (int k = static_cast<int>(S) - 1; k >= 0; --k)
            s += m.has(k) ? '1' : '0';
        return s;
    };
    auto num = [](const uhi::bset<S> &m) {
        std::string s;
        uhi::debug_ui<tree_main>::put_num(s, m.w[0]); // consteval 里没有 std::to_string
        return s;
    };
    auto cell = [&](const uhi::bset<S> &m) {
        std::string name = name_of(m);
        while (name.size() < 30)
            name += ' ';
        return name + " " + bits(m) + " = " + num(m);
    };
    // 单个 kind 的一行：位号 / 名字 / 位图 / 十进制
    auto kind_row = [&](std::size_t i) {
        std::string s = "      #";
        uhi::debug_ui<tree_main>::put_num(s, i);
        while (s.size() < 11)
            s += ' ';
        s += show::sv_of(law::names[i]);
        while (s.size() < 24)
            s += ' ';
        std::string b = "0b";
        for (int k = static_cast<int>(S) - 1; k >= 0; --k)
            b += (static_cast<std::size_t>(k) == i) ? '1' : '0';
        s += b;
        s += " = ";
        uhi::debug_ui<tree_main>::put_num(s, std::uint64_t{1} << i);
        return s;
    };

    std::string out;
    out += " 场景  用户按下又松开：单击已经成立，双击的窗口还开着。\n";
    out += "       要答的只有一句：这一帧谁有资格落地？\n\n";

    out += " 先认识 bset —— 这段所有代码里唯一的\"集合类型\"。\n\n";

    out += " ① bset<S> = 一排 S 个开关（S ≤ 64 时就是一个 uint64）\n";
    out += "     位 i 是 1 ⇔ \"第 i 个 kind 属于这个集合\"。\n";
    out += "     位 i 是谁由 law::names[i] 决定（函数开头三条 static_assert 钉死）。\n\n";
    out += "     名字 ↔ 位号 ↔ 位图（只看这一位 ON）：\n";
    for (std::size_t i = 0; i < S; ++i)
        out += kind_row(i) + "\n";
    out += "\n";
    out += "     集合 {on_click,on_dbl} = 位0 ON + 位1 ON = 0b000011 = 3\n";
    out += "     （位 0 永远在最右边；位号越大，位图里越靠左。别把低位读成高位。）\n\n";

    out += " ② 只有五个操作，全是对位图的直接操作\n";
    out += "     set(i)    把第 i 个开关拨 ON\n";
    out += "     has(i)    第 i 个开关是不是 ON\n";
    out += "     none()    是不是全部 OFF\n";
    out += "     a | b     并集（按位 OR）\n";
    out += "     a & b     交集（按位 AND）\n";
    out += "     ~a        补集（只在 [0,S) 内，高位抹零）\n";
    out += "     a & ~b    差集：a 里 ON、b 里 OFF 的那些位\n\n";
    out += "     mask({0,1,2}) 是 set 的简写；blocked_by 是\"行并集\"的简写。\n\n";

    out += " ③ 收票：识别器的一句话只动两列\n";
    out += "       alive  ← active / done 置位；aborted / idle 清位\n";
    out += "       done   ← 只有 done 置位（active 不置）\n";
    out += "     这一帧：click 说 done，dbl / triple 说 active。\n";
    out += "       alive = " + cell(alive) + "\n";
    out += "       done  = " + cell(done) + "\n\n";

    out += " ④ 两个动作，一行一个\n";
    out += "     blocked = 行(alive 里的人)并起来\n";
    out += "             = 行(on_click) ∪ 行(on_dbl) ∪ 行(on_triple)\n";
    out += "             = " + cell(law::beats[0]) + "   ∪\n";
    out += "               " + cell(law::beats[1]) + "   ∪\n";
    out += "               " + cell(law::beats[2]) + "\n";
    out += "             = " + cell(blocked) + "\n";
    out += "     winners = done ∖ blocked\n";
    out += "             = " + cell(done) + "  ∖\n";
    out += "               " + cell(blocked) + "\n";
    out += "             = " + cell(winners) + "\n\n";

    out += " ⑤ 结论：一个都不播。\n";
    out += "     click 有结论（在 done 里），但被 blocked 挡着 → 不落地。\n";
    out += "     若拿 alive 去减会剩 on_triple：\n";
    out += "       alive ∖ blocked = " + cell(alive & ~blocked) + "\n";
    out += "     可它本帧没结论、不是候选；alive 与 done 是两列，别混。\n\n";

    out += " 目的 → 数据 → 答案，就这三行：\n";
    out += "   两个位图（alive / done） → 一步并集（blocked） → 一步差集（winners）\n";
    out += "   把集合存成整数，是为了让这两步各是一条指令。\n";
    out += "   R2 会把这一帧真的喂进去，你会看到同一个 blocked = " + name_of(blocked) +
           "。\n";

    return uhi::static_string{std::define_static_string(out)};
}

consteval uhi::static_string b1_script()
{
    std::string out;

    {
        using T = uhi::refines<uhi::all_of<La, Lb>, uhi::all_of<Lc, Ld>>;
        const auto p = T::plan();
        out += "  ① refines<all_of<a,b>, all_of<c,d>>\n";
        out += "     kinds  : " + kinds_of(p) + "\n";
        out += "     awaits : " + fmt_edges(p.awaits, p, "→") + "\n";
        out += "     excl   : " + fmt_edges(p.excl, p, "~") + "\n";
        out += "     心算   : A 侧每一片 × B 侧每一片 = (a,c)(a,d)(b,c)(b,d)，excl 空\n";
        if (!eq_set(p.awaits, std::array<std::pair<uhi::index_t, uhi::index_t>, 4>{
                                  std::pair{0, 2}, std::pair{0, 3}, std::pair{1, 2},
                                  std::pair{1, 3}}))
            throw "B1① awaits 不符";
        if (!p.excl.empty())
            throw "B1① excl 应为空";
    }

    {
        using T = uhi::one_of<uhi::all_of<La, Lb>, uhi::all_of<Lc, Ld>>;
        const auto p = T::plan();
        out += "  ② one_of<all_of<a,b>, all_of<c,d>>\n";
        out += "     kinds  : " + kinds_of(p) + "\n";
        out += "     awaits : " + fmt_edges(p.awaits, p, "→") + "\n";
        out += "     excl   : " + fmt_edges(p.excl, p, "~") + "\n";
        out += "     心算   : 子树之间填满互斥 (a,c)(a,d)(b,c)(b,d)；子树内不填；awaits "
               "空\n";
        if (!p.awaits.empty())
            throw "B1② awaits 应为空";
        if (!eq_set(p.excl, std::array<std::pair<uhi::index_t, uhi::index_t>, 4>{
                                std::pair{0, 2}, std::pair{0, 3}, std::pair{1, 2},
                                std::pair{1, 3}}))
            throw "B1② excl 不符";
    }

    {
        using T = uhi::all_of<uhi::refines<La, Lb>, Lc>;
        const auto p = T::plan();
        out += "  ③ all_of<refines<a,b>, c>\n";
        out += "     kinds  : " + kinds_of(p) + "\n";
        out += "     awaits : " + fmt_edges(p.awaits, p, "→") + "\n";
        out += "     excl   : " + fmt_edges(p.excl, p, "~") + "\n";
        out += "     心算   : all_of 只拼接，不加边；所以只有内层那一条 a→b\n";
        if (!eq_set(p.awaits, std::array<std::pair<uhi::index_t, uhi::index_t>, 1>{
                                  std::pair{0, 1}}))
            throw "B1③ awaits 不符";
        if (!p.excl.empty())
            throw "B1③ excl 应为空";
    }

    out += "  断言：三条全中 ✓（对不上则编译失败）\n";
    return uhi::static_string{std::define_static_string(out)};
}

// ── B2：拼接要加 base（编号换坐标系）────────────────────────────────────
consteval uhi::static_string b2_script()
{
    std::string out;

    {
        using T = uhi::refines<Lc, uhi::refines<La, Lb>>;
        const auto p = T::plan();
        out += "  ① refines<c, refines<a,b>>\n";
        out += "     kinds  : " + kinds_of(p) + "\n";
        out += "     内层 refines<a,b> 的局部意图：a 等 b ⇒ 局部 (0,1)\n";
        out += "     拼到外层时整体 +base(=1) ⇒ (1,2)；外层再补 c×{a,b} ⇒ (0,1)(0,2)\n";
        out += "     awaits : " + fmt_edges(p.awaits, p, "→") + "\n";
        if (!eq_set(p.awaits, std::array<std::pair<uhi::index_t, uhi::index_t>, 3>{
                                  std::pair{0, 1}, std::pair{0, 2}, std::pair{1, 2}}))
            throw "B2① awaits 不符（base 没加上？）";
    }

    {
        using T = uhi::one_of<La, uhi::refines<Lb, Lc>>;
        const auto p = T::plan();
        out += "  ② one_of<a, refines<b,c>>\n";
        out += "     kinds  : " + kinds_of(p) + "\n";
        out += "     内层 refines<b,c> 的 awaits 局部 (0,1)，+base(=1) ⇒ (1,2)\n";
        out += "     外层互斥只连子树：a×{b,c} ⇒ (0,1)(0,2)；b 与 c 之间不互斥\n";
        out += "     awaits : " + fmt_edges(p.awaits, p, "→") + "\n";
        out += "     excl   : " + fmt_edges(p.excl, p, "~") + "\n";
        if (!eq_set(p.awaits, std::array<std::pair<uhi::index_t, uhi::index_t>, 1>{
                                  std::pair{1, 2}}))
            throw "B2② awaits 不符";
        if (!eq_set(p.excl, std::array<std::pair<uhi::index_t, uhi::index_t>, 2>{
                                std::pair{0, 1}, std::pair{0, 2}}))
            throw "B2② excl 不符";
    }

    out += "  断言：两条全中 ✓\n";
    return uhi::static_string{std::define_static_string(out)};
}

// ── B3：翻面：等待关系 → 赢关系，然后定方向 ──────────────────────────────
//
//  "转置"这个词容易把人带偏，说清它做的两件事：
//    ① 翻面：awaits 写的是"a 比 b 抽象 ⇒ b 没死 a 不许落地"。这是**先后**，
//       不含胜负。写成 m[b].set(a)（物理上就是邻接矩阵转置）之后，
//       "b 赢 a"才第一次有数据形态。
//    ② 定向：excl 本来是对称的（谁先说话谁赢）。对称边会让两人永远互挡，
//       矩阵也不再是偏序。取"先声明的赢"，把它削成单向。
//  两步都做完，矩阵才是严格偏序 ⇒ 极大元唯一 ⇒ "赢"才算有定义。
//  所以胜负不是运行期算出来的，是在建表时被这两个方向**定**下来的。
consteval uhi::static_string b3_script()
{
    std::string out;

    // NOTE: 等待关系 变成 赢关系的表达
    {
        using T = uhi::refines<uhi::all_of<La, Lb>, uhi::all_of<Lc, Ld>>;
        using law = uhi::law_of<T>;
        const auto p = T::plan();
        out += "  ① refines<all_of<a,b>, all_of<c,d>>\n";
        out += "     awaits : " + fmt_edges(p.awaits, p, "→") + "\n";
        out += "     翻面：a 等 c ⇒ c 赢 a；逐行读出来\n";
        // 手写的期望：每行应该赢谁（和下面逐行打出来的比对）
        const std::array<std::string_view, 4> expect = {"∅", "∅", "a,b", "a,b"};
        for (std::size_t i = 0; i < law::S; ++i)
        {
            const auto row = show::set(law::names, law::beats[i]);
            out += "       " + show::pad(show::sv_of(law::names[i]), 3) + " 行 = " + row +
                   "\n";
            if (row != std::string{expect[i]})
                throw "B3① 行不符";
        }
    }

    /*
「对称」指的是 excl 本身没有方向
one_of<a,b,c> 生成的三对是 (a,b) (a,c) (b,c) —— 存的是“无向的一对”。
语义上 a~b 和 b~a 是同一句话：“这两个不能同时赢”。它不包含“谁赢”的信息。
把 excl 忠实地写成矩阵，就应该两个格子都是 1 :
      a b c
   a  . 1 1        ← a 压 b、a 压 c
   b  1 . 1        ← b 也压 a
   c  1 1 .
这就是“对称边”：a 和 b 之间两个方向都有。
对称的矩阵根本不是偏序，“极大元”这个概念在上面没有定义。

「单向」就是只留一个方向
for (auto [a, b] : p.excl)
    m[a < b ? a : b].set(a < b ? b : a);   // 只写在"下标更小"那一行
只写小行、不写大行，于是：
      a b c
   a  . 1 1        ← a 压 b、a 压 c
   b  . . 1        ← b 只压 c，不再压 a
   c  . . .

NOTE: 一句话
excl 只说“不能共存”，没说“谁赢”。把方向补进去的那一步，就是 m[min].set(max)，补的规则是“先声明的赢”。
补完矩阵才变成偏序（反对称），blocked 才有可能空，赢家才存在。
所以 B3 那句“对称边被削成单向”不准确的说法应该是：对称的互斥关系缺一个方向，这一行把它补上。
*/
    // NOTE: 互斥关系 变成 赢关系的表达
    {
        using T = uhi::one_of<La, Lb, Lc>;
        using law = uhi::law_of<T>;
        const auto p = T::plan();
        out += "  ② one_of<a,b,c>\n";
        out += "     excl   : " + fmt_edges(p.excl, p, "~") + "\n";
        out += "     定向：先声明的赢 ⇒ a 赢 {b,c}，b 赢 {c}（对称边被削成单向）\n";
        // 手写的期望：a 赢 b,c；b 赢 c；c 谁也不赢
        const std::array<std::string_view, 3> expect = {"b,c", "c", "∅"};
        for (std::size_t i = 0; i < law::S; ++i)
        {
            const auto row = show::set(law::names, law::beats[i]);
            out += "       " + show::pad(show::sv_of(law::names[i]), 3) + " 行 = " + row +
                   "\n";
            if (row != std::string{expect[i]})
                throw "B3② 行不符";
        }
    }

    out += "  断言：7 行全中 ✓\n";
    return uhi::static_string{std::define_static_string(out)};
}

// ── B4：闭包（手搭一条 3 链，看它确实补边）──────────────────────────────
consteval uhi::static_string b4_script()
{
    std::string out;
    constexpr std::array<std::string_view, 3> nm = {"a", "b", "c"};

    std::array<uhi::bset<3>, 3> m{};
    {
        m[0].set(1); // a 赢 b
        m[1].set(2); // b 赢 c
    }
    const auto before = m;
    const auto after = uhi::closure<3>(m);

    auto rows = [&](const std::array<uhi::bset<3>, 3> &mm, std::string_view tag) {
        out += "     ";
        out += tag;
        out += "：";
        for (std::size_t i = 0; i < 3; ++i)
            out += " " + std::string{nm[i]} + "→" +
                   (mm[i].none() ? std::string{"∅"} : "{" + show::set(nm, mm[i]) + "}");
        out += "\n";
    };

    out += "  ① 手搭 3 链：a→b、b→c\n";
    rows(before, "闭包前");
    rows(after, "闭包后");
    out += "     心算：a→b、b→c ⇒ 应补出 a→c（一条），其余不变\n";

    std::size_t added = 0;
    for (std::size_t i = 0; i < 3; ++i)
        for (std::size_t j = 0; j < 3; ++j)
            if (after[i].has(j) && !before[i].has(j))
                ++added;
    if (added != 1 || !after[0].has(2) || !after[0].has(1))
        throw "B4① 闭包没有补出 a→c";
    out += "     断言：新增 1 条 = a→c ✓\n";

    {
        using T = tree_main;
        constexpr std::size_t S = uhi::law_of<T>::S;
        const auto p = T::plan();
        std::array<uhi::bset<S>, S> raw{};
        for (auto [a, b] : p.awaits)
            raw[b].set(a);
        for (auto [a, b] : p.excl)
            raw[a < b ? a : b].set(a < b ? b : a);
        std::size_t e0 = 0;
        for (std::size_t i = 0; i < S; ++i)
            for (std::size_t j = 0; j < S; ++j)
                e0 += raw[i].has(j);
        const auto clo = uhi::closure<S>(raw);
        std::size_t e1 = 0;
        for (std::size_t i = 0; i < S; ++i)
            for (std::size_t j = 0; j < S; ++j)
                e1 += clo[i].has(j);
        out += "  ② 真实树 tree_main：raw 边数 ";
        uhi::debug_ui<tree_main>::put_num(out, e0);
        out += " → 闭包后 ";
        uhi::debug_ui<tree_main>::put_num(out, e1);
        out += "（新增 ";
        uhi::debug_ui<tree_main>::put_num(out, e1 - e0);
        out += "）\n";
        out += "     心算：refines 生成的是全对边，所以闭包在这套组合子下不添边\n";
        if (e1 != e0)
            throw "B4② 真实树的闭包添边了，说明 refines 不是全对边";
    }

    out += "  断言：闭包代码确实会补边；真实树今天不需要它补 ✓\n";
    return uhi::static_string{std::define_static_string(out)};
}

// ── B5：五道门 + 两条矩阵断言（正向全过）────────────────────────────────
consteval uhi::static_string b5_script()
{
    using T = tree_main;
    using law = uhi::law_of<T>;
    constexpr std::size_t S = law::S;
    const auto p = T::plan();
    std::string out;

    out += "  注：查重只查 kind 的 name（身份，要求唯一）；\n";
    out += "      fn_name（回调名）不查重 —— 多个 kind 共用一个回调是合法用法。\n\n";

    for (std::size_t i = 0; i < S; ++i)
        for (std::size_t j = i + 1; j < S; ++j)
            if (p.kinds[i].name.view() == p.kinds[j].name.view())
                throw "B5① kind 重名";
    out += "  ① kind 的 name 唯一            ✓\n";

    for (auto [a, b] : p.awaits)
        if (a == b)
            throw "B5① awaits 自环";
    for (auto [a, b] : p.excl)
        if (a == b)
            throw "B5① excl 自环";
    out += "  ② 无自环                      ✓\n";

    for (auto [a, b] : p.awaits)
        if (p.kinds[a].frame.view() != p.kinds[b].frame.view())
            throw "B5② awaits 跨通道";
    for (auto [a, b] : p.excl)
        if (p.kinds[a].frame.view() != p.kinds[b].frame.view())
            throw "B5② excl 跨通道";
    out += "  ③ 边两端同通道                ✓\n";

    for (auto [i, j] : p.awaits)
        for (auto [x, y] : p.excl)
            if ((i == x && j == y) || (i == y && j == x))
                throw "B5③ refines × excl 直冲";
    out += "  ④ refines × excl 不直冲       ✓";
    out += p.excl.empty() ? "（本树无 excl，空洞真）\n" : "\n";

    // ④ 用真的 validate（它内部跑 Kahn）
    validate(p);
    out += "  ⑤ awaits 无环（Kahn）         ✓\n";

    for (std::size_t i = 0; i < S; ++i)
        if (law::beats[i].has(i))
            throw "B5⑤ 矩阵对角线非空";
    out += "  ⑥ 矩阵对角线全空 = 严格偏序    ✓\n";

    for (std::size_t i = 0; i < S; ++i)
        for (std::size_t j = 0; j < S; ++j)
            if (law::beats[i].has(j) &&
                p.kinds[i].frame.view() != p.kinds[j].frame.view())
                throw "B5⑥ 矩阵跨通道边";
    out += "  ⑦ 矩阵每条边两端同通道         ✓\n";

    out += "  以上七条全在编译期；任一条不成立，这个程序编译不过。\n";
    return uhi::static_string{std::define_static_string(out)};
}
// ── B11 的四个样本：一个"只有必须项"的最小机器 + 三个反例 ─────────────
//   （放命名空间域：局部类的 typedef 若被 concept 短路没用到，会报未使用）
struct b11_minimal
{
    using frame_type = mouse_frame;
    uhi::step_t step(const mouse_frame &, uhi::time_point) noexcept
    {
        return uhi::step_t::idle;
    }
};
struct b11_no_frame
{
    uhi::step_t step(const mouse_frame &, uhi::time_point) noexcept
    {
        return uhi::step_t::idle;
    }
};
struct b11_wrong_ret
{
    using frame_type = mouse_frame;
    int step(const mouse_frame &, uhi::time_point) noexcept
    {
        return 0;
    }
};
struct b11_no_default
{
    using frame_type = mouse_frame;
    explicit b11_no_default(int) {}
    uhi::step_t step(const mouse_frame &, uhi::time_point) noexcept
    {
        return uhi::step_t::idle;
    }
};

struct b11_throwing_step
{
    using frame_type = mouse_frame;
    uhi::step_t step(const mouse_frame &, uhi::time_point) // 漏了 noexcept
    {
        return uhi::step_t::idle;
    }
};
struct b11_throwing_tick
{
    using frame_type = mouse_frame;
    uhi::step_t step(const mouse_frame &, uhi::time_point) noexcept
    {
        return uhi::step_t::idle;
    }
    uhi::step_t tick(uhi::time_point) // 漏了 noexcept
    {
        return uhi::step_t::hold;
    }
};
struct b11_throwing_reset
{
    using frame_type = mouse_frame;
    uhi::step_t step(const mouse_frame &, uhi::time_point) noexcept
    {
        return uhi::step_t::idle;
    }
    void reset() {} // 漏了 noexcept
};

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::println("连续剧：看得见的表（每个格子都是打出来的，不靠心算）");

    // ══════════════════════════════════════════════════════════════════
    //  Part 1  建表（B1 … B7）：plan → 表的全过程，全在编译期
    // ══════════════════════════════════════════════════════════════════
    show::head("B0", "这一帧谁能播？—— 问题本身就是集合运算");
    std::print("{}", b0_script().view());

    show::head("B1", "组合子 → plan：三张边集，和手写期望逐条对拍");
    std::print("{}", b1_script().view());

    show::head("B2", "拼接要加 base：嵌套组合子的编号换算");
    std::print("{}", b2_script().view());

    show::head("B3", "翻面：等待/互斥关系 → 赢关系（物理上是转置，语义上定胜负）");
    std::print("{}", b3_script().view());

    show::head("B4", "闭包：手搭 3 链看它补边；真实树今天不需要它补");
    std::print("{}", b4_script().view());

    show::head("B5", "七道门：validate 五项 + 矩阵两条（全部编译期）");
    std::print("{}", b5_script().view());

    show::head("B6", "静态表读一下：.rodata 里到底是什么");
    {
        using law = uhi::law_of<tree_main>;
        constexpr std::size_t S = law::S;
        std::println(" law::beats    {} 行 × {} 字节 = {}   （每行一个 uint64 位图）", S,
                     sizeof(uhi::bset<S>), sizeof(law::beats));
        for (std::size_t i = 0; i < S; ++i)
        {
            std::print("   beats[{}] = 0x{:016x}   赢 {}", i, law::beats[i].w[0],
                       show::set(law::names, law::beats[i]));
            std::println("");
        }
        std::println(" law::names  {} × {} 字节 = {}   （static_string：只有一个指针）",
                     S, sizeof(uhi::static_string), sizeof(law::names));
        for (std::size_t i = 0; i < S; ++i)
            std::println("   names[{}] = \"{}\"  len={}（view() 时现算 strlen）", i,
                         law::names[i], law::names[i].view().size());
        std::println(" law::hold   {} × {} 字节 = {}", S,
                     sizeof(std::chrono::nanoseconds), sizeof(law::hold));
        std::println(" .rodata 合计 {} 字节（字符串本体另计）",
                     sizeof(law::beats) + sizeof(law::names) + sizeof(law::hold));
        std::println(" 对照运行期对象：stage = {} 字节（2 个位图 {} + 2 列时间 {}）",
                     sizeof(uhi::stage<tree_main>), 2 * sizeof(uhi::bset<S>),
                     2 * S * sizeof(uhi::time_point));

        show::check(sizeof(law::beats) == S * sizeof(uhi::bset<S>), "B6 beats 布局");
        show::check(sizeof(law::names) == S * sizeof(uhi::static_string),
                    "B6 names 布局");
        show::check(sizeof(law::hold) == S * sizeof(std::chrono::nanoseconds),
                    "B6 hold 布局");
    }

    show::head("B7", "坐标对齐：同一个 i 出现在四处");
    {
        using law = uhi::law_of<tree_main>;
        constexpr std::size_t S = law::S;
        std::array<std::string_view, S> leaf_names{};
        uhi::detail::shape_of<tree_main>::each_leaf(
            [&]<std::size_t I, class Leaf>() { leaf_names[I] = Leaf::name.view(); });
        std::println(" i  law::names (=plan)  each_leaf    hold");
        bool aligned = true;
        for (std::size_t i = 0; i < S; ++i)
        {
            std::println(" {}  {:<18} {:<10}  {}", i, law::names[i], leaf_names[i],
                         law::hold[i].count() / 1'000'000);
            aligned = aligned && (law::names[i] == leaf_names[i]);
        }
        std::println(" ⇒ fsms 下标 / 矩阵行列 / sink 下标 / names 下标 是同一个 i");
        show::check(aligned, "B7 四处下标是同一个人");
    }

    show::head("B8", "连通块：只要有一条 beats 边（任一方向）就是同一块");
    {
        std::println(" 规则：两人之间有边 ⇒ 同块。并查集一轮轮把邻居并进来，");
        std::println(
            "       最后\"自己是自己代表\"的个数 = 块数。跟名字是否唯一无关。\n");

        std::println(" ① refines<all_of<a,b>, all_of<c,d>>   a→c a→d b→c b→d");
        expect_blocks<uhi::refines<uhi::all_of<La, Lb>, uhi::all_of<Lc, Ld>>, 1>(
            {0b1111});

        std::println(" ② all_of<refines<a,b>, c>             a→b，c 孤零零");
        expect_blocks<uhi::all_of<uhi::refines<La, Lb>, Lc>, 2>({0b011, 0b100});

        std::println(" ③ one_of<a, refines<b,c>>             a~b a~c，b→c");
        expect_blocks<uhi::one_of<La, uhi::refines<Lb, Lc>>, 1>({0b111});

        std::println(
            " ④ tree_main                          click←dbl←triple 一条链，其余孤立");
        expect_blocks<tree_main, 4>({0b000111, 0b001000, 0b010000, 0b100000});
    }

    show::head("B9", "两个名字：kind 的身份 vs 回调的名字");
    {
        // 两个不同的 kind（不同 payload），共用同一个回调 on_click
        struct pay_a
        {
            int x{7};
        };
        struct pay_b
        {
            double y{2.5};
        };
        struct fsm_a
        {
            using frame_type = mouse_frame;
            pay_a payload;
            uhi::step_t step(const mouse_frame &f, uhi::time_point) noexcept
            {
                return f.what == mouse_frame::press ? uhi::step_t::done
                                                    : uhi::step_t::idle;
            }
            void reset() noexcept {}
        };
        struct fsm_b
        {
            using frame_type = mouse_frame;
            pay_b payload;
            uhi::step_t step(const mouse_frame &f, uhi::time_point) noexcept
            {
                return f.what == mouse_frame::release ? uhi::step_t::done
                                                      : uhi::step_t::idle;
            }
            void reset() noexcept {}
        };

        using TA = uhi::leaf<fsm_a, "click_a", 0, "on_click">; // kind 名 click_a
        using TB =
            uhi::leaf<fsm_b, "click_b", 0, "on_click">; // kind 名 click_b，回调同上
        using TT = uhi::all_of<TA, TB>;
        using LT = uhi::law_of<TT>;

        std::println(" kind 名（进日志/蓝图）= {} / {}", LT::names[0], LT::names[1]);
        std::println(" 回调名（invoke<…>）  = {} / {}", TA::fn_name.view(),
                     TB::fn_name.view());

        // 一个泛型回调，payload 不同就 if constexpr 分流
        auto ar = make_aggregate<"arena">(
            mcs::vulkan::meta::field<"ui">(uhi::debug_ui<TT>{}),
            mcs::vulkan::meta::field<"n">(int{}),
            mcs::vulkan::meta::field<"sum">(double{}),
            mcs::vulkan::meta::method<"on_click">([](auto &self, auto &&p) noexcept {
                ++self.n;
                if constexpr (requires { p.x; })
                    self.sum += p.x;
                else
                    self.sum += p.y;
            }),
            mcs::vulkan::meta::method<"bind">(
                [](auto &self) noexcept { self.ui.bind(self); }));
        ar.invoke<"bind">();

        ar.ui.feed(M(mouse_frame::press), at(100));   // click_a 有结论
        ar.ui.feed(M(mouse_frame::release), at(200)); // click_b 有结论
        std::print(" 播报  ");
        for (const auto &e : ar.ui.events())
            std::print(" {}:{}", e.name, uhi::to_string(e.v));
        std::println("");

        show::check(ar.n == 2, "B9 两个 kind 调到同一个回调（n=2）");
        show::check(ar.sum == 9.5, "B9 两个 payload 各自被读到（7 + 2.5）");
        std::println(" ⇒ 日志按 kind 名区分（click_a / click_b），回调按 fn_name "
                     "合并（on_click）");
    }

    show::head("B10", "裁决代数：三条纯函数，只吃位图（树/时间/回调都不在里头）");
    {
        using DT = tree_main;
        using LT = uhi::law_of<DT>;
        constexpr std::size_t S = LT::S;
        constexpr auto mk = [](std::initializer_list<uhi::index_t> idx) {
            uhi::bset<S> b{};
            for (auto i : idx)
                b.set(i);
            return b;
        };
        constexpr auto mouse =
            uhi::scope_of<DT, mouse_frame>(); // {click,dbl,triple,hover,wheel}
        auto show3 = [](std::string_view label, const uhi::bset<S> &x) {
            std::println("     {:<8}= {}", label,
                         x.none() ? std::string{"∅"}
                                  : "{" + show::set(LT::names, x) + "}");
        };

        std::println(" 场景 A  alive={{click,dbl,triple}}  cand={{click}}  scope=mouse");
        {
            constexpr auto alive = mk({0, 1, 2});
            constexpr auto cand = mk({0});
            const auto blocked = uhi::blocked_by<DT>(alive, mouse);
            const auto winners = uhi::winners_of<DT>(cand, alive, mouse);
            const auto prey = uhi::prey_of<DT>(winners, alive, mouse);
            show3("blocked", blocked);
            show3("winners", winners);
            show::check(blocked == mk({0, 1}), "B10A blocked = {click,dbl}");
            show::check(winners.none(), "B10A winners 空（click 被挡）");
            show::check(prey.none(), "B10A prey 空（没人赢）");
        }

        std::println(" 场景 B  alive={{click,dbl,triple}}  cand={{triple}}");
        {
            constexpr auto alive = mk({0, 1, 2});
            constexpr auto cand = mk({2});
            const auto blocked = uhi::blocked_by<DT>(alive, mouse);
            const auto winners = uhi::winners_of<DT>(cand, alive, mouse);
            const auto prey = uhi::prey_of<DT>(winners, alive, mouse);
            show3("blocked", blocked);
            show3("winners", winners);
            show3("prey", prey);
            show::check(winners == mk({2}), "B10B triple 赢");
            show::check(prey == mk({0, 1}), "B10B prey = {click,dbl}");
        }

        std::println(
            " 场景 C  alive={{click,dbl,triple}}  cand={{dbl}}  scope={{click}}");
        {
            constexpr auto alive = mk({0, 1, 2});
            constexpr auto cand = mk({1});
            constexpr auto narrow = mk({0}); // 这一帧只有 click 那条通道
            const auto blocked = uhi::blocked_by<DT>(alive, narrow);
            const auto winners = uhi::winners_of<DT>(cand, alive, narrow);
            show3("blocked", blocked);
            show3("winners", winners);
            show::check(blocked.none(), "B10C 不在本帧通道里的人不挡你");
        }

        std::println(" ⇒ 这三条不含时间、不含状态、不碰回调：喂位图就出结果，");
        std::println(
            "   stage 只是按帧的节奏把它们串起来（B0 讲的那句话，这里是它的骨架）。");
    }

    show::head("B11", "FSM 的契约：五个问题，两个必须、三个可选");
    {
        // 样本见文件头 b11_minimal / b11_no_frame / b11_wrong_ret / b11_no_default

        std::println(" ① step(f,t) -> step_t     必须 noexcept   ② tick(t) -> step_t   可选，有则 noexcept");
        std::println(" ③ reset()                可选，有则 noexcept   ④ payload          可选");
        std::println(" ⑤ frame_type             必须（只为编译期检查，不进运行期数据）");

        static_assert(uhi::fsm::machine<b11_minimal>, "两个必须就够");
        static_assert(!uhi::fsm::machine<b11_no_frame>, "缺 frame_type 不算");
        static_assert(!uhi::fsm::machine<b11_wrong_ret>, "step 不回 step_t 不算");
        static_assert(!uhi::fsm::machine<b11_no_default>, "不能默认构造不算");
        static_assert(uhi::fsm::machine<multi_click<1>>, "真实机器满足");
        // noexcept 也是契约的一部分：不会在运行期悄悄 terminate
        static_assert(!uhi::fsm::machine<b11_throwing_step>, "step 漏 noexcept 不算");
        static_assert(!uhi::fsm::machine<b11_throwing_tick>, "有 tick 就必须 noexcept");
        static_assert(!uhi::fsm::machine<b11_throwing_reset>, "有 reset 就必须 noexcept");
        static_assert(uhi::fsm::has_tick<b11_throwing_tick>,
                      "它确实有 tick（只是不合格）—— 不会被当成\"没有 tick\"忽略");
        // ④ 选哪条回调分支：编译期就定了（不是运行期 if）
        static_assert(uhi::sink::has_payload<uhi::leaf<multi_click<1>, "c1">>,
                      "multi_click 带载荷 ⇒ 走带参分支");
        static_assert(!uhi::sink::has_payload<uhi::leaf<hover_fsm, "hv">>,
                      "hover_fsm 无载荷 ⇒ 走空参分支");

        // 三个可选：没有就是默认行为，不是错误
        static_assert(!uhi::fsm::has_tick<b11_minimal> &&
                      !uhi::fsm::can_payload<b11_minimal>);
        b11_minimal mm{};
        show::check(uhi::fsm::tick(mm, at(0)) == uhi::step_t::hold,
                    "B11 没有 tick ⇒ 自动 hold");
        uhi::fsm::reset(mm); // 没有 reset ⇒ 空操作，能编过就是过
        show::check(true, "B11 没有 reset ⇒ 空操作");


    // ══════════════════════════════════════════════════════════════════
    //  Part 2  建表后（R1 … R8）：每喂一帧，内部表怎么变
    // ══════════════════════════════════════════════════════════════════
    // ── E2 / E3 / E4：一台机器走三步 ───────────────────────────────────
    using Tree = tree_main;
    using T = table<Tree>;
    using L = uhi::law_of<Tree>;

    show::head("R1", "按下：只改 alive / begin，没人有结论 ⇒ 什么都不播");
    {
        auto a = make_arena<Tree>();
        auto &u = a.ui;
        a.invoke<"bind">(); // 绑了才有回调；不绑也能跑，只是没人被通知
        const auto s0 = T::take(u.st);
        std::println(" 表的初始值：");
        T::print("表(初始)", s0);

        // 这一下带上坐标：后面要验"载荷真的从识别器传到了回调"，而不只是回调被调过
        u.feed(mouse_frame{mouse_frame::press, 42.5, 0.0, 0}, at(1000));
        after_frame<Tree>(u, "press@1000", s0);

        const auto s1 = T::take(u.st);
        show::check(s1.alive.has(0) && s1.alive.has(1) && s1.alive.has(2),
                    "E2 三个鼠标 kind 都活着");
        show::check(s1.begin[0] == at(1000) && s1.end[0] == uhi::time_point{},
                    "E2 begin=1000、end 还没写");
        show::check(u.events().empty(), "E2 这一帧不播");

        show::head("R2", "松开：click 有结论了，但 blocked 里有它");
        const auto s2 = T::take(u.st);
        u.feed(M(mouse_frame::release), at(1010));
        after_frame<Tree>(u, "release@1010", s2);
        const auto s3 = T::take(u.st);

        std::println(" 演算");
        std::println("   blocked = 行(click) ∪ 行(dbl) ∪ 行(triple)");
        std::println("           = {} ∪ {} ∪ {}", show::set(L::names, L::beats[0]),
                     show::set(L::names, L::beats[1]), show::set(L::names, L::beats[2]));
        std::println("           = {}", show::set(L::names, T::blocked_of(s2.alive)));
        // 两条实现互相对一下：测试手写的行并 vs 引擎的 blocked_by（应当一模一样）
        show::check(T::blocked_of(s2.alive) ==
                        uhi::blocked_by<Tree>(s2.alive, T::template scope<mouse_frame>()),
                    "R2 手写行并 == uhi::blocked_by");
        std::println("   有结论的是 click（end=1010 已写），它 ∈ blocked");
        std::println(
            "   ⇒ 赢家 = ∅ ⇒ 挂起：pending=•，alive 保持 •，跨度保留 (1000,1010)");
        show::check(s3.alive.has(0) && s3.pending.has(0), "E3 click 挂起而不是落地");
        show::check(u.events().empty(), "E3 这一帧不播");
        // 跨度两列各钉一条：done 只写 end，不动 begin
        show::check(s3.begin[0] == at(1000), "E3 done 不改 begin（起点还是 1000）");
        show::check(s3.end[0] == at(1010), "E3 end = 第一次有结论（1010）");

        show::head("R3", "时间过了：挡住它的人被清掉，那次交互才落地");
        const auto s4 = T::take(u.st);
        u.tick(at(1400));
        after_frame<Tree>(u, "tick@1400", s4);
        const auto s5 = T::take(u.st);

        std::println(" 演算");
        std::println("   tick 让两支多击超时（aborted）⇒ 静默清掉 dbl / triple（不播）");
        std::println("   活着的只剩 click ⇒ blocked = {} ⇒ 赢家 = {{click}}",
                     show::set(L::names, T::blocked_of(s5.alive)));
        show::check(a.hits[H_CLICK] == 1, "E4 单击回调恰好一次");
        show::check(a.last_x == 42.5, "E4 载荷内容真的传到了回调（x=42.5）");
        show::check(s5.alive.none() && s5.pending.none(), "E4 表清空");
        const auto e4 = u.events();
        show::check(e4.size() == 1 && e4[0].name == "on_click", "E4 只有一条 accepted");
        show::check(e4[0].begin == at(1000) && e4[0].end == at(1010),
                    "E4 跨度还是 (1000,1010)");
        show::check(e4[0].at == at(1400), "E4 播报时刻是 1400");
        std::println(" 注意  跨度（begin/end）与播报时刻（at）是三列数据；延迟只动 at");
    }

    /*
跨度规则（已定）：一次交互 = 第一次活着 → 第一次有结论。
重新武装、被挡、再完成，跨度都不变，变的只有播报时刻 at。
所以 R4 里 click 让位时播的是 (2000,2010)，不是最后一次 (2160,2170) —— 它是同一次交互。
改法（若哪天要“播最近一次”）就是去掉 !pending 那道闸，让每次 done 都覆盖 end。
*/
    // ── E5 同帧定案 ────────────────────────────────────────────────────
    show::head("R4", "三击同帧定案：一个赢家吃掉两个");
    {
        auto a = make_arena<Tree>();
        auto &u = a.ui;
        a.invoke<"bind">();
        u.feed(M(mouse_frame::press), at(2000));
        u.feed(M(mouse_frame::release), at(2010));
        u.feed(M(mouse_frame::press), at(2080));
        u.feed(M(mouse_frame::release), at(2090));
        u.clear_events();

        const auto s = T::take(u.st);
        std::println(" 表(前)：两下之后（click/dbl 都有结论但被挡住）");
        T::print("", s);

        u.feed(M(mouse_frame::press), at(2160));
        u.feed(M(mouse_frame::release), at(2170));
        after_frame<Tree>(u, "release@2170（第三下松开）", s);

        std::println(" 演算");
        std::println("   blocked = {} （三行并起来）",
                     show::set(L::names, T::blocked_of(s.alive)));
        std::println(
            "   有结论的 = {{click, triple}}（这一帧 dbl 只回了 active，没有结论）");
        std::println("   ∖blocked ⇒ 赢家 = {{triple}}");
        std::println("   prey = 行(triple) ∩ alive ∖ 赢家 = {}",
                     show::set(L::names, L::beats[2]));
        std::println("   ⇒ dbl 没有结论也被吃掉了：prey 只要求「活着且在我行里」");

        const auto e5 = u.events();
        show::check(e5.size() == 3, "E5 一帧三条记录");
        show::check(e5[0].name == "on_click" && e5[0].v == uhi::verdict::superseded,
                    "E5 第一条 click 让位");
        show::check(e5[1].name == "on_dbl" && e5[1].v == uhi::verdict::superseded,
                    "E5 第二条 dbl 让位");
        show::check(e5[2].name == "on_triple" && e5[2].v == uhi::verdict::accepted,
                    "E5 第三条 triple 接受");
        show::check(a.hits[H_TRIPLE] == 1 && a.hits[H_CLICK] == 0 && a.hits[H_DBL] == 0,
                    "E5 让位不叫回调（只有 accepted 叫）");
        // dbl 播报的跨度是它上一次结论（2090），不是这一帧（2170）—— 证明它本帧无结论
        show::check(e5[1].end == at(2090) && e5[1].at == at(2170),
                    "E5 dbl 这一帧没有新结论（end 还是 2090）");
        // 跨度规则钉在 click 身上：它 2080/2090 重新武装又完成过，跨度仍是第一次的
        show::check(e5[0].begin == at(2000) && e5[0].end == at(2010) &&
                        e5[0].at == at(2170),
                    "E5 click 让位：跨度 = 第一次交互 (2000,2010)，at = 2170");
        std::println(" 注意  让位顺序 = 声明序（click 在 dbl 前），不是「具体度」");
    }

    // ── E6 平局 ────────────────────────────────────────────────────────
    show::head("R5", "平局：换声明顺序 = 换矩阵里那个 x = 换赢家");
    {
        struct both
        {
            using frame_type = mouse_frame;
            uhi::step_t step(const mouse_frame &f, uhi::time_point) noexcept
            {
                return f.what == mouse_frame::press ? uhi::step_t::done
                                                    : uhi::step_t::idle;
            }
            void reset() noexcept {}
        };
        using leaf_a = uhi::leaf<both, "aa">;
        using leaf_b = uhi::leaf<both, "bb">;

        const auto ab =
            clash_episode<uhi::one_of<leaf_a, leaf_b>>("one_of<aa,bb>：aa 先声明");
        const auto ba =
            clash_episode<uhi::one_of<leaf_b, leaf_a>>("one_of<bb,aa>：bb 先声明");

        show::check(ab.size() == 2 && ab[1].name == "aa" &&
                        ab[1].v == uhi::verdict::accepted,
                    "E6 先声明的赢（aa）");
        show::check(ba.size() == 2 && ba[1].name == "bb" &&
                        ba[1].v == uhi::verdict::accepted,
                    "E6 换序后另一个赢（bb）");
        std::println(" 结论  互斥写成一条有向边 m[先声明].set(后声明)：");
        std::println("       平局规则在矩阵里，不在扫描顺序里。excl 若写成对称，");
        std::println("       两人会永远互相挡住 —— 那才需要靠顺序解开。");
    }

    // ── E7 兜底 ────────────────────────────────────────────────────────
    show::head("R6", "兜底：只判负，不发明赢家");
    {
        using TL = tree_live;
        using TLt = table<TL>;
        using L7 = uhi::law_of<TL>;
        auto a = make_arena<TL>();
        auto &u = a.ui;
        a.invoke<"bind">();

        u.feed(M(mouse_frame::press), at(4000));
        u.feed(M(mouse_frame::release), at(4010));
        const auto s = TLt::take(u.st);
        std::println(" release@4010 的表：slower 有结论，stuck 占着位（hold=100ms）");
        TLt::print("", s);
        std::println(" 演算  blocked = 行(stuck) = {} ⇒ slower 挂起",
                     show::set(L7::names, L7::beats[1]));
        show::check(s.pending.has(0) && s.alive.has(1), "E7 slower 挂起、stuck 仍活着");
        show::check(u.events().empty(), "E7 先不播");

        const auto s2 = TLt::take(u.st);
        u.tick(at(4250));
        std::println(" tick@4250（stuck 的 hold 到点，静默清掉）：");
        TLt::diff(s2, TLt::take(u.st));
        std::print(" 播报  ");
        for (const auto &e : u.events())
            std::print(" {}:{}@{}({},{})", e.name, uhi::to_string(e.v), e.at.count(),
                       e.begin.count(), e.end.count());
        std::println("");
        show::check(a.hits[H_SLOWER] == 1, "E7 stuck 被清掉后 slower 才赢");
        show::check(a.hits[H_STUCK] == 0, "E7 stuck 从未 accepted");

        // ── 子例：兜底从 begin 算，不从 end 算 ────────────────────────
        //   一个"长按"：按住 200ms 才肯 done，但 hold = 100ms。
        //   begin = 6000 ⇒ 6100 到期 ⇒ 6150 那帧就被静默清掉；
        //   6200 它本可 done，但人已经出局 —— 长按不是点击。
        struct slow_press
        {
            using frame_type = mouse_frame;
            time_point t0{};
            bool held = false;
            uhi::step_t step(const frame_type &f, time_point now) noexcept
            {
                if (f.what == mouse_frame::press)
                {
                    held = true;
                    t0 = now;
                    return uhi::step_t::active;
                }
                if (!held)
                    return uhi::step_t::idle;
                return now - t0 >= time_point{200} ? uhi::step_t::done
                                                   : uhi::step_t::active;
            }
            void reset() noexcept
            {
                held = false;
            }
        };
        using TP = uhi::all_of<uhi::leaf<slow_press, "on_slow", 100>>;
        auto ap = make_aggregate<"arena">(
            mcs::vulkan::meta::field<"ui">(uhi::debug_ui<TP>{}),
            mcs::vulkan::meta::field<"n">(int{}),
            mcs::vulkan::meta::method<"on_slow">([](auto &self) noexcept { ++self.n; }),
            mcs::vulkan::meta::method<"bind">(
                [](auto &self) noexcept { self.ui.bind(self); }));
        ap.invoke<"bind">();

        ap.ui.feed(M(mouse_frame::press), at(6000)); // begin = 6000，hold 到 6100
        ap.ui.feed(M(mouse_frame::move), at(6150));  // 150 > 100 ⇒ 静默清掉
        std::println(" hold=100ms、begin=6000 ⇒ 6150 那帧被清；此时它还想 done");
        show::check(ap.ui.st.alive.none(), "B 兜底从 begin 算：6150 已出局");
        ap.ui.feed(M(mouse_frame::move), at(6200)); // 本可 done 的那一帧
        show::check(ap.n == 0 && ap.ui.events().empty(),
                    "B 出局之后那句 done 没人听（长按 != 点击）");
        std::println(" ⇒ 兜底窗口 = [begin, begin+hold]，不是 [end, end+hold]");

        // ── 子例：hold 的另外三种情形 ──────────────────────────────────
        struct never_done
        {
            using frame_type = mouse_frame;
            uhi::step_t step(const frame_type &, time_point) noexcept
            {
                return uhi::step_t::active;
            }
            void reset() noexcept {}
        };
        struct done_now
        {
            using frame_type = mouse_frame;
            uhi::step_t step(const frame_type &f, time_point) noexcept
            {
                return f.what == mouse_frame::press ? uhi::step_t::done
                                                    : uhi::step_t::idle;
            }
            void reset() noexcept {}
        };
        struct always_on
        {
            using frame_type = mouse_frame;
            bool on = false;
            uhi::step_t step(const frame_type &f, time_point) noexcept
            {
                on = on || f.what == mouse_frame::press;
                return on ? uhi::step_t::active : uhi::step_t::idle;
            }
            void reset() noexcept
            {
                on = false;
            }
        };

        // (a) hold = 0 就是"没有兜底"：等多久都不杀
        {
            using TN = uhi::all_of<uhi::leaf<never_done, "on_never">>;
            uhi::debug_ui<TN> un;
            un.feed(M(mouse_frame::press), at(7000));
            un.tick(at(7000 + 1'000'000)); // 一秒钟之后
            show::check(un.st.alive.has(0) && un.events().empty(),
                        "B10a hold=0 ⇒ 永不超时");
            std::println(" (a) hold=0：tick 到 +1000000 也还活着");
        }

        // (b) 边界：恰好 begin+hold 不杀，多 1ms 才杀（判据是严格大于）
        {
            uhi::debug_ui<TP> uq;               // 复用 slow_press（hold=100）
            uq.feed(M(mouse_frame::press), at(8000)); // begin=8000 ⇒ 门槛 8100
            uq.tick(at(8100));
            show::check(uq.st.alive.has(0), "B10b 恰好 100ms：还没杀");
            uq.tick(at(8101));
            show::check(!uq.st.alive.has(0), "B10b 101ms：杀了");
            std::println(" (b) 边界：8100 活着、8101 死 —— 判据是 now-begin > hold");
        }

        // (c) 有结论但一直被挡（pending）也会被 hold 杀 —— 兜底管的是"没落地"，不是"没结论"
        {
            using TQ = uhi::refines<uhi::leaf<done_now, "on_c", 100>,
                                    uhi::leaf<always_on, "on_b">>;
            uhi::debug_ui<TQ> uz;
            uz.feed(M(mouse_frame::press), at(9000)); // on_c 有结论但被 on_b 挡 ⇒ pending
            show::check(uz.st.alive.has(0) && uz.st.pending.has(0), "B10c on_c 挂起");
            uz.tick(at(9200)); // 200 > 100：挂着的也到期
            show::check(!uz.st.alive.has(0) && uz.st.alive.has(1),
                        "B10c 挂起的 on_c 被清，on_b（hold=0）还在");
            show::check(uz.events().empty(), "B10c 静默：判负不发明赢家");
            std::println(
                " (c) pending 也会被 hold 杀：兜底管\"没落地\"，不只是\"没结论\"");
        }
    }

    // ── E8 通道隔离 ────────────────────────────────────────────────────
    show::head("R7", "通道隔离：一个鼠标帧碰不到键盘的挂起候选");
    {
        struct kb_done
        {
            using frame_type = kb_frame;
            uhi::step_t step(const kb_frame &f, uhi::time_point) noexcept
            {
                return (f.press && f.key == 'A') ? uhi::step_t::done : uhi::step_t::idle;
            }
            void reset() noexcept {}
        };
        struct kb_stay
        {
            using frame_type = kb_frame;
            bool on = false;
            uhi::step_t step(const kb_frame &f, uhi::time_point) noexcept
            {
                if (f.press && f.key == 'B')
                {
                    on = true;
                    return uhi::step_t::active;
                }
                return on ? uhi::step_t::active : uhi::step_t::idle;
            }
            void reset() noexcept
            {
                on = false;
            }
        };
        using TC = uhi::all_of<
            uhi::refines<uhi::leaf<kb_done, "kb_a">, uhi::leaf<kb_stay, "kb_b", 100>>,
            uhi::leaf<hover_fsm, "on_hover">>;
        using TCt = table<TC>;
        using L8 = uhi::law_of<TC>;

        std::println(" 通道 = 谁认识这个帧类型（编译期算，运行期不用问）：");
        constexpr auto sc_mouse = TCt::template scope<mouse_frame>();
        constexpr auto sc_kb = TCt::template scope<kb_frame>();
        std::println("       mouse_frame ⇒ {}", show::set(L8::names, sc_mouse));
        std::println("       kb_frame    ⇒ {}", show::set(L8::names, sc_kb));
        show::check(!sc_mouse.has(0) && !sc_mouse.has(1),
                    "E8 鼠标帧的 scope 不含键盘两列");

        uhi::debug_ui<TC> u;
        u.feed(K('B', true), at(5000));
        u.feed(K('A', true), at(5001));
        const auto s = TCt::take(u.st);
        std::println(" 键盘按 B 再按 A 之后：");
        TCt::print("", s);
        show::check(s.pending.has(0), "E8 kb_a 挂起");

        u.clear_events();
        const auto s2 = TCt::take(u.st);
        u.feed(M(mouse_frame::move), at(5150));
        std::println(
            " 喂一个纯鼠标帧（此刻 kb_b 的 hold 其实已经到点，但本帧看不见它）：");
        TCt::diff(s2, TCt::take(u.st));
        std::print(" 播报  ");
        for (const auto &e : u.events())
            std::print(" {}:{}", e.name, uhi::to_string(e.v));
        std::println("");
        show::check(u.events().size() == 1 && u.events()[0].name == "on_hover",
                    "E8 鼠标帧只结算鼠标通道");
        show::check(TCt::take(u.st).pending.has(0), "E8 kb_a 的挂起位没被动过");
        show::check(TCt::take(u.st).alive.has(1), "E8 kb_b 仍活着（见都没见到）");

        u.clear_events();
        const auto s3 = TCt::take(u.st);
        u.tick(at(5160));
        std::println(" 再给一次 tick（时间对所有通道一起走）：");
        TCt::diff(s3, TCt::take(u.st));
        std::print(" 播报  ");
        for (const auto &e : u.events())
            std::print(" {}:{}", e.name, uhi::to_string(e.v));
        std::println("");
        show::check(u.events().size() == 1 && u.events()[0].name == "kb_a",
                    "E8 tick 才让 kb_a 落地");
    }

    // ── E9 同帧两个赢家 ────────────────────────────────────────────────
    show::head("R8", "同帧两个赢家：选择律选的是「不被吃掉的」，不是「只选一个」");
    {
        struct on_press
        {
            using frame_type = mouse_frame;
            uhi::step_t step(const mouse_frame &f, uhi::time_point) noexcept
            {
                return f.what == mouse_frame::press ? uhi::step_t::done
                                                    : uhi::step_t::idle;
            }
            void reset() noexcept {}
        };
        struct on_release
        {
            using frame_type = mouse_frame;
            uhi::step_t step(const mouse_frame &f, uhi::time_point) noexcept
            {
                return f.what == mouse_frame::release ? uhi::step_t::done
                                                      : uhi::step_t::idle;
            }
            void reset() noexcept {}
        };
        using TN = uhi::refines<
            uhi::all_of<uhi::leaf<on_press, "na">, uhi::leaf<on_press, "nb">>,
            uhi::leaf<on_release, "nc">>;
        using TNt = table<TN>;
        using L9 = uhi::law_of<TN>;

        std::println(
            " 结构 refines<all_of<na,nb>, nc>：na、nb 都等 nc，na 与 nb 之间没有边");
        for (std::size_t i = 0; i < L9::S; ++i)
            std::println("       {} {} 赢 {}", i, show::pad(show::sv_of(L9::names[i]), 4),
                         show::set(L9::names, L9::beats[i]));
        show::check(!L9::beats[0].has(1) && !L9::beats[1].has(0),
                    "E9 na 与 nb 互不可比（矩阵两格都是空的）");

        uhi::debug_ui<TN> u;
        const auto s = TNt::take(u.st);
        u.feed(M(mouse_frame::press), at(7000));
        after_frame<TN>(u, "press@7000（na、nb 同时有结论，nc 还没说话）", s);
        std::println(" 演算  blocked = 行(na) ∪ 行(nb) = ∅ ⇒ 两个都是极大元");
        std::println("       所以这一帧有**两个** accepted —— 它们不是同一场竞争");

        const auto e9 = u.events();
        show::check(e9.size() == 2, "E9 一帧两条记录");
        show::check(e9[0].v == uhi::verdict::accepted && e9[0].name == "na",
                    "E9 na 接受");
        show::check(e9[1].v == uhi::verdict::accepted && e9[1].name == "nb",
                    "E9 nb 接受");
    }
        std::println(" ⇒ 三条可选都有默认值，所以\"怎么写\"只剩一个问题：");
        std::println("   「一帧喂给你，你回哪一句 step_t？」答了这句，就能用。");
    }

    show::head("R9", "零分配：喂一帧不碰堆（把全局 new 换掉数次数）");
    {
        uhi::debug_ui<tree_main> u2;
        u2.feed(M(mouse_frame::move), at(20000)); // 预热（惰性初始化、格式化器都在这一下）
        const auto before = g_heap_allocs;
        for (int k = 0; k < 100; ++k)
        {
            u2.feed(M(mouse_frame::press), at(20100 + k * 2));
            u2.feed(M(mouse_frame::release), at(20101 + k * 2));
            u2.tick(at(20102 + k * 2));
        }
        const auto after = g_heap_allocs;
        show::check(after == before, "R9 100 轮 feed/tick 零分配");
        std::println(" 堆分配  {} → {}  （差 {}）", before, after, after - before);
        std::println(" ⇒ stage 的两个位图、两列时间、events 的定长缓冲，全在 ui 对象里；");
        std::println("   feed/tick 只做位运算和写数组，一次 malloc 都不需要。");
    }


    // ── 收尾 ───────────────────────────────────────────────────────────
    std::println("");
    std::println("──────────────────────────────────────────────────────────────────");
    std::println(" 全集回顾");
    std::println("  ── 建表（编译期）");
    std::println("   B0 意图      裁决 = 三个集合的差集；集合存成整数，差集才是一条指令");
    std::println(
        "   B1 组合子    refines/one_of/all_of 各自产出什么边，与手写期望逐条对拍");
    std::println("   B2 偏移      拼接时 +base；嵌套树的全局编号");
    std::println("   B3 翻面+定向 等待关系 → 赢关系；excl 削成单向 ⇒ 谁赢谁");
    std::println("   B4 闭包      手搭 3 链会补边；当前组合子已闭合，不添边");
    std::println("   B5 七道门    validate 五项 + 矩阵两项，全编译期，破一即编译失败");
    std::println("   B6 静态表    beats/names/hold 的字节数与逐行位图");
    std::println("   B7 对齐      四处同一个 i（names / each_leaf / hold）");
    std::println("   B8 连通块    有边就同块；三棵小树 + tree_main 手算对拍");
    std::println("   B9 两个名字  name = kind 身份（唯一）；fn_name = 回调名（可共用）");
    std::println("   B10 代数     blocked_by / winners_of / prey_of：只吃位图的纯函数");
    std::println(
        "   B11 FSM 契约 五个问题：step/frame_type 必须，tick/reset/payload 可选");
    std::println("  ── 建表后（运行期）");
    std::println("   R1 收票     alive/begin 由识别器的一句话决定，与裁决无关");
    std::println("   R2 挡住     blocked = 活着的行并起来；有结论 ≠ 能播");
    std::println("   R3 跨帧     pending 保住跨度，换掉的只是播报时刻 at");
    std::println("   R4 同帧     blocked 的补集 = 极大元；一个赢家吃掉它整行");
    std::println("   R5 平局     excl 定向后，赢家由矩阵里那个 x 决定");
    std::println("   R6 兜底     hold 只杀人，不造赢家");
    std::println("   R7 隔离     scope 让本帧没走的路，一个位都不动");
    std::println("   R8 两个赢家 极大元可以不止一个；赢家集是反链，互不可比");
    std::println("   R9 零分配   100 轮 feed/tick，全局 new 一次没被叫");
    std::println("");
    std::println(" 检查 {} 条，失败 {} 条", show::checks, show::fails);
    return show::fails == 0 ? 0 : 1;
}
