// ═══════════════════════════════════════════════════════════════════════════
//  uhi_all.cpp —— 单文件、自包含
//
//  目标（00-goal）：一串原始输入进去，每个通道每次交互至多一个语义事件出来，
//                   且恰好播报一次。
//  手段（05-implementation）：把"谁该赢"从运行期 if 换成编译期算好的表；
//                  运行期的全部代码是 feed —— 二十行，没有运行期查表、
//                  没有虚函数、没有容器、没有第二份状态。
//
//  编译期一次性：plan() → validate() → cell_data::build() → cell_table（只读表）
//  运行期每帧  ：leaf.step ×N → stage.settle ×cell → 播报 + 复位
//               所有下标都是编译期常量（constexpr），所以没有派发。
//  定律：L1 组合同态 / L2 同通道连通才同格 / L3 一格一次交互至多一个 accepted
//        L4 一帧内 step 顺序不可观测 / L5 单槽格无裁决开销 / L6 一 kind 一生产者
//
//  编译：g++ -std=c++26 -freflection -O2 uhi_all.cpp -o uhi_all -lstdc++exp
// ═══════════════════════════════════════════════════════════════════════════
#include <array>
#include <cassert>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <meta>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "../../../../../../include/detail/meta/static_string.hpp"
#include "../../../../../../include/detail/meta/make_aggregate.hpp"

namespace uhi
{
    using mcs::vulkan::meta::static_string;
    using time_point = std::chrono::milliseconds;

    // ═══════════════════════════════════════════════════════════════════
    // 0. 词汇
    // ═══════════════════════════════════════════════════════════════════

    // 识别器对仲裁层唯一能说的话
    enum class step_t : std::uint8_t
    {
        idle,    // 我没有假设
        active,  // 我有一个进行中的假设
        done,    // 我的假设成立
        aborted, // 我放弃
        hold     // 只有时间过去了，状态不变
    };

    enum class verdict : std::uint8_t
    {
        accepted,  // 赢
        superseded // 让位
    };

    constexpr std::string_view to_string(verdict v) noexcept
    {
        return v == verdict::accepted ? "acc" : "sup";
    }

    // 一条观测记录（定长、可拷贝、不分配）
    struct event
    {
        std::string_view name{};
        verdict v{};
        time_point at{};    // 判决时刻
        time_point begin{}; // 交互开始
        time_point end{};   // 交互结束
    };

    // ── 观测：只写不读，不参与判决。整块可换 ──
    //    trace    ：定长记录，满了计数丢弃（不覆盖、不分配）
    //    no_trace ：生产环境不记，零字段零开销
    struct trace
    {
        static constexpr std::size_t capacity = 64;
        std::array<event, capacity> buf{};
        std::size_t n = 0, dropped_n = 0;
        bool in_feed_;

        constexpr void push(const event &e) noexcept
        {
            if (n < capacity)
                buf[n++] = e;
            else
                ++dropped_n;
        }
        constexpr std::span<const event> events() const noexcept
        {
            return {buf.data(), n};
        }
        constexpr void clear() noexcept
        {
            n = 0;
        }
        constexpr std::size_t dropped() const noexcept
        {
            return dropped_n;
        }
    };

    struct no_trace
    {
    };

    namespace detail
    {
        // 重入闸：只有断言用，feed/tick 各一行
        struct feed_guard
        {
            bool *flag = nullptr;
            explicit feed_guard(bool *f) : flag(f)
            {
                assert(!*flag && "feed/tick 不可重入：handler 里不要再喂");
                *flag = true;
            }
            ~feed_guard()
            {
                *flag = false;
            }
            feed_guard(const feed_guard &) = delete;
            feed_guard &operator=(const feed_guard &) = delete;
        };

        template <class T>
        consteval static_string type_name()
        {
            return static_string{std::define_static_string(
                std::string{std::meta::display_string_of(^^T)})};
        }
    } // namespace detail

    // ═══════════════════════════════════════════════════════════════════
    // 1. plan：编译期的声明表（L1 同态律）
    //      kinds  : 每片叶子的名字 / 通道 / 兜底
    //      awaits : (i,j) —— i 是 j 的抽象版
    //      excl   : (i,j) —— i 与 j 不能共存
    // ═══════════════════════════════════════════════════════════════════
    struct kind_desc
    {
        static_string fn_name{};
        static_string frame{};
        std::chrono::nanoseconds hold{}; // 兜底时限：是一段时间，不是一个数

        friend constexpr bool operator==(const kind_desc &, const kind_desc &) = default;
    };

    struct plan_data
    {
        std::vector<kind_desc> kinds;
        std::vector<std::pair<int, int>> awaits;
        std::vector<std::pair<int, int>> excl;

        // 拼接：b 追加到a, b 的所有编号 + base （换坐标系）
        friend consteval plan_data operator+(plan_data a, const plan_data &b)
        {
            const int base = static_cast<int>(a.kinds.size());
            a.kinds.append_range(b.kinds);
            for (auto [i, j] : b.awaits)
                a.awaits.push_back({i + base, j + base});
            for (auto [i, j] : b.excl)
                a.excl.push_back({i + base, j + base});
            return a;
        }
    };

    // 叶子 = 纯类型：名字 + 状态机类型 + 吃哪种帧 + 兜底。
    //   它不持有状态机：状态机住在 ui 的扁平元组里。
    //   兜底时限用 std::chrono::milliseconds（类型表达"一段时间"），并断言非负。
    template <class Fsm, static_string Name, std::uint64_t HoldMs = 0>
    struct leaf
    {
        using machine = Fsm; // 叶子协议：我的状态机类型（反射靠它认出叶子）
        static constexpr auto name = Name;
        // 兜底时限用无符号毫秒：不可能为负这件事写在类型里
        static constexpr std::chrono::nanoseconds hold =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::milliseconds{static_cast<std::int64_t>(HoldMs)});

        static consteval plan_data plan()
        {
            return plan_data{
                .kinds = {{Name, detail::type_name<typename Fsm::frame_type>(), hold}},
                .awaits = {},
                .excl = {}};
        }
    };

    // 并行：不加任何关系
    template <class... Rs>
    struct all_of
    {
        using children_t = std::tuple<Rs...>; // 组合子协议（仅类型）

        static consteval plan_data plan()
        {
            plan_data p;
            ((p = p + Rs::plan()), ...);
            return p;
        }
    };

    // 互斥：任意两个不同子树之间填满 excl
    template <class... Rs>
    struct one_of
    {
        using children_t = std::tuple<Rs...>;

        static consteval plan_data plan()
        {
            constexpr int K = sizeof...(Rs);
            std::array<plan_data, K> parts{};
            int k = 0;
            ((parts[static_cast<std::size_t>(k++)] = Rs::plan()), ...);

            plan_data p;
            std::array<int, K + 1> off{};
            for (int i = 0; i < K; ++i)
            {
                off[static_cast<std::size_t>(i + 1)] =
                    off[static_cast<std::size_t>(i)] +
                    static_cast<int>(parts[static_cast<std::size_t>(i)].kinds.size());
                p = p + parts[static_cast<std::size_t>(i)];
            }
            for (int a = 0; a < K; ++a)
                for (int b = a + 1; b < K; ++b)
                    for (int x = off[static_cast<std::size_t>(a)];
                         x < off[static_cast<std::size_t>(a + 1)]; ++x)
                        for (int y = off[static_cast<std::size_t>(b)];
                             y < off[static_cast<std::size_t>(b + 1)]; ++y)
                            p.excl.push_back({x, y});
            return p;
        }
    };

    // 细化：A 里每片叶子都等 B 里每片叶子的失败
    template <class A, class B>
    struct refines
    {
        using children_t = std::tuple<A, B>;

        static consteval plan_data plan()
        {
            auto pa = A::plan();
            auto pb = B::plan();
            const int na = static_cast<int>(pa.kinds.size());
            auto p = pa + pb;
            // NOTE: A 的元素  全部 等 B 的元素
            for (int i = 0; i < na; ++i)
                for (int j = na; j < static_cast<int>(p.kinds.size()); ++j)
                    p.awaits.push_back({i, j});
            return p;
        }
    };

    // ═══════════════════════════════════════════════════════════════════
    // 2. validate：坏结构编译不过（00-goal 第 3 条）
    //    ① 名字唯一 ② 无自环 ③ 关系两端同通道 ④ 细化×互斥不矛盾 ⑤ awaits 无环
    // ═══════════════════════════════════════════════════════════════════
    consteval void validate(const plan_data &p)
    {
        const int n = static_cast<int>(p.kinds.size());
        auto kind = [&](int i) -> const kind_desc & {
            return p.kinds[static_cast<std::size_t>(i)];
        };
        auto same_frame = [&](int i, int j) {
            return kind(i).frame.view() == kind(j).frame.view();
        };

        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j)
                if (kind(i).fn_name.view() == kind(j).fn_name.view())
                    throw "dup kind name";

        for (auto [i, j] : p.awaits)
        {
            if (i == j)
                throw "awaits self-loop";
            if (!same_frame(i, j))
                throw "awaits cross-frame";
        }
        for (auto [i, j] : p.excl)
        {
            if (i == j)
                throw "excl self-loop";
            if (!same_frame(i, j))
                throw "excl cross-frame";
        }
        for (auto [i, j] : p.awaits)
            for (auto [x, y] : p.excl)
                if ((i == x && j == y) || (i == y && j == x))
                    throw "refines x excl conflict";

        // awaits 无环：按等待数做 Kahn
        std::vector<int> indeg(static_cast<std::size_t>(n), 0);
        for (auto [i, j] : p.awaits)
            ++indeg[static_cast<std::size_t>(i)];
        for (int left = n; left > 0;)
        {
            bool prog = false;
            for (int i = 0; i < n; ++i)
                if (indeg[static_cast<std::size_t>(i)] == 0)
                {
                    indeg[static_cast<std::size_t>(i)] = -1;
                    --left;
                    prog = true;
                    for (auto [x, y] : p.awaits)
                        if (y == i)
                            --indeg[static_cast<std::size_t>(x)];
                }
            if (!prog)
                throw "refines cycle";
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // 3. cell_data：切格（L2 划分律）
    //    cell = 同通道 × 关系连通；格内顺序 = 反拓扑序 = 最具体先
    // ═══════════════════════════════════════════════════════════════════
    struct cell_data
    {
        std::vector<kind_desc> kinds;   // 表序（每片叶子：名字 / 通道 / 兜底）
        std::vector<int> table_to_decl; // 表序 → 声明序（编译期常量）
        std::vector<std::pair<int, int>> awaits_local; // 闭包后，表序
        std::vector<std::pair<int, int>> excl_local;   // 表序

        static consteval std::vector<cell_data> build(const plan_data &p)
        {
            const int n = static_cast<int>(p.kinds.size());
            auto frame = [&](int i) {
                return p.kinds[static_cast<std::size_t>(i)].frame.view();
            };
            auto linked = [&](int i, int j) {
                for (auto [a, b] : p.awaits)
                    if ((a == i && b == j) || (a == j && b == i))
                        return true;
                for (auto [a, b] : p.excl)
                    if ((a == i && b == j) || (a == j && b == i))
                        return true;
                return false;
            };

            // ① 并查集：同通道 + 有关系 ⇒ 同格
            std::vector<int> uf(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i)
                uf[static_cast<std::size_t>(i)] = i;
            auto find = [&](int x) {
                while (uf[static_cast<std::size_t>(x)] != x)
                    x = uf[static_cast<std::size_t>(x)] =
                        uf[static_cast<std::size_t>(uf[static_cast<std::size_t>(x)])];
                return x;
            };
            for (int i = 0; i < n; ++i)
                for (int j = i + 1; j < n; ++j)
                    if (frame(i) == frame(j) && linked(i, j))
                        uf[static_cast<std::size_t>(find(i))] = find(j);

            // ② 分组
            std::vector<std::vector<int>> groups;
            std::vector<int> gid(static_cast<std::size_t>(n), -1);
            for (int i = 0; i < n; ++i)
            {
                const int r = find(i);
                if (gid[static_cast<std::size_t>(r)] < 0)
                {
                    gid[static_cast<std::size_t>(r)] = static_cast<int>(groups.size());
                    groups.push_back({});
                }
                groups[static_cast<std::size_t>(gid[static_cast<std::size_t>(r)])]
                    .push_back(i);
            }

            // ③ 每格：局部邻接 → 闭包 → 反拓扑序 → 打包
            std::vector<cell_data> out;
            for (const auto &g : groups)
            {
                const int N = static_cast<int>(g.size());
                std::vector<int> loc(static_cast<std::size_t>(n), -1);
                for (int k = 0; k < N; ++k)
                    loc[static_cast<std::size_t>(g[static_cast<std::size_t>(k)])] = k;
                auto at = [&](int i, int j) {
                    return static_cast<std::size_t>(i * N + j);
                };

                std::vector<char> adj(static_cast<std::size_t>(N * N), 0);
                std::vector<char> ex(static_cast<std::size_t>(N * N), 0);
                for (auto [i, j] : p.awaits)
                {
                    const int li = loc[static_cast<std::size_t>(i)],
                              lj = loc[static_cast<std::size_t>(j)];
                    if (li >= 0 && lj >= 0)
                        adj[at(li, lj)] = 1;
                }
                for (auto [i, j] : p.excl)
                {
                    const int li = loc[static_cast<std::size_t>(i)],
                              lj = loc[static_cast<std::size_t>(j)];
                    if (li < 0 || lj < 0)
                        continue;
                    ex[at(li, lj)] = 1;
                    ex[at(lj, li)] = 1;
                }
                // 传递闭包：更具体的没说话之前，更抽象的都不许落地
                for (int k = 0; k < N; ++k)
                    for (int i = 0; i < N; ++i)
                        for (int j = 0; j < N; ++j)
                            if (adj[at(i, k)] && adj[at(k, j)])
                                adj[at(i, j)] = 1;

                // 反拓扑序：等待数 0 的先出
                std::vector<int> deg(static_cast<std::size_t>(N), 0), order;
                for (int i = 0; i < N; ++i)
                    for (int j = 0; j < N; ++j)
                        if (adj[at(i, j)])
                            ++deg[static_cast<std::size_t>(i)];
                while (static_cast<int>(order.size()) < N)
                {
                    bool prog = false;
                    for (int i = 0; i < N; ++i)
                        if (deg[static_cast<std::size_t>(i)] == 0)
                        {
                            deg[static_cast<std::size_t>(i)] = -1;
                            order.push_back(i);
                            prog = true;
                            for (int j = 0; j < N; ++j)
                                if (adj[at(j, i)] && deg[static_cast<std::size_t>(j)] > 0)
                                    --deg[static_cast<std::size_t>(j)];
                        }
                    if (!prog)
                        throw "cell topo failed";
                }

                cell_data c;
                for (int t : order)
                {
                    c.kinds.push_back(p.kinds[static_cast<std::size_t>(
                        g[static_cast<std::size_t>(t)])]);
                    c.table_to_decl.push_back(g[static_cast<std::size_t>(t)]);
                }
                for (int i = 0; i < N; ++i)
                    for (int j = 0; j < N; ++j)
                        if (adj[at(order[static_cast<std::size_t>(i)],
                                   order[static_cast<std::size_t>(j)])])
                            c.awaits_local.push_back({i, j});
                for (int i = 0; i < N; ++i)
                    for (int j = i + 1; j < N; ++j)
                        if (ex[at(order[static_cast<std::size_t>(i)],
                                  order[static_cast<std::size_t>(j)])])
                            c.excl_local.push_back({i, j});
                out.push_back(std::move(c));
            }
            return out;
        }
    };

    template <class Tree>
    inline constexpr std::size_t cell_count_v = []() consteval {
        return cell_data::build(Tree::plan()).size();
    }();

    // ═══════════════════════════════════════════════════════════════════
    // 4. 树的形状（shape）—— 树只是类型，不产生任何对象
    //
    //    一个类型说清四件事，全部是编译期：
    //      n          : 几片叶子
    //      fsms       : 它们的状态机元组（声明序）
    //      accepts<F> : 有没有叶子认识帧 F（喂错帧编译不过）
    //      each_leaf  : 按声明序把每片叶子的**类型**交给调用方（bind 用）
    //
    //    下面是三个反射原语：子节点 = 类型模板参数；叶子 = 声明了 machine 的节点。
    // ═══════════════════════════════════════════════════════════════════
    namespace detail
    {
        // 组合子 = 声明了成员类型别名 children_t 的类。
        // 子节点 = children_t（std::tuple<Rs...>）的模板实参。
        consteval std::vector<std::meta::info> child_types(std::meta::info node)
        {
            if (!std::meta::is_class_type(node))
                return {};

            constexpr auto ctx = std::meta::access_context::current();
            for (auto m : std::meta::members_of(node, ctx))
            {
                if (!std::meta::has_identifier(m) ||
                    std::meta::identifier_of(m) != "children_t")
                    continue;
                if (!std::meta::is_type_alias(m))
                    continue;

                auto tup = std::meta::dealias(m); // std::tuple<Rs...>
                std::vector<std::meta::info> out;
                for (auto a : std::meta::template_arguments_of(tup))
                    if (std::meta::is_type(a))
                        out.push_back(std::meta::remove_cvref(a));
                return out;
            }
            return {};
        }

        // 按声明序收集叶子（叶子 = 声明了 machine 的节点）
        consteval std::vector<std::meta::info> leaf_infos(std::meta::info node)
        {
            constexpr auto ctx = std::meta::access_context::current();
            std::vector<std::meta::info> out;
            for (auto m : std::meta::members_of(node, ctx))
                if (std::meta::has_identifier(m) &&
                    std::meta::identifier_of(m) == "machine")
                {
                    out.push_back(node);
                    return out;
                }

            for (auto c : child_types(node))
                for (auto x : leaf_infos(c))
                    out.push_back(x);
            return out;
        }

        template <class... Ls>
        struct shape
        {
            static constexpr std::size_t n = sizeof...(Ls);
            using fsms = std::tuple<typename Ls::machine...>;

            template <class Frame>
            static constexpr bool accepts =
                (requires(typename Ls::machine &m, const Frame &f, time_point t) {
                    m.step(f, t);
                } || ...);

            template <class F>
            static constexpr void each_leaf(F &&f) noexcept(
                (noexcept(f.template operator()<Ls>()) && ...))
            {
                (f.template operator()<Ls>(), ...);
            }
        };

        template <class Tree>
        using shape_of = typename[:std::meta::substitute(^^shape, leaf_infos(^^Tree)):];
    } // namespace detail

    // 结构的公开自查数字
    template <class Tree>
    inline constexpr std::size_t leaf_count_v = detail::shape_of<Tree>::n;

    // ═══════════════════════════════════════════════════════════════════
    // 5. 一格的法则（只读，编译期算好，所有实例共享一份）
    //    静态成员直接就是表：awaits[i][j] 读起来就是"i 等 j"
    //    时限用 chrono::nanoseconds（类型表达"一段时间"），下标用 size_t（不可能为负）
    // ═══════════════════════════════════════════════════════════════════
    template <class Tree, std::size_t Cell>
    struct cell_table
    {
        static consteval cell_data cell()
        {
            return cell_data::build(Tree::plan())[Cell];
        }

        // 槽位数：编译期有界
        static constexpr std::size_t S = cell().kinds.size();

        // 每槽的名字（表序）——判决播报时用
        static constexpr std::array<std::string_view, S> names = []() consteval {
            std::array<std::string_view, S> r{};
            for (std::size_t i = 0; i < S; ++i)
                r[i] = cell().kinds[i].fn_name.view();
            return r;
        }();

        // 谁等谁：awaits[i][j] 为真 ⇒ j 活着时 i 不能赢
        static constexpr std::array<std::array<bool, S>, S> awaits = []() consteval {
            std::array<std::array<bool, S>, S> r{};
            for (auto [i, j] : cell().awaits_local)
                r[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = true;
            return r;
        }();

        // 谁和谁不能共存（对称）
        static constexpr std::array<std::array<bool, S>, S> excl = []() consteval {
            std::array<std::array<bool, S>, S> r{};
            for (auto [i, j] : cell().excl_local)
            {
                r[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = true;
                r[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)] = true;
            }
            return r;
        }();

        // 兜底时限：活这么久还没结论，就不是它
        static constexpr std::array<std::chrono::nanoseconds, S> hold = []() consteval {
            std::array<std::chrono::nanoseconds, S> r{};
            for (std::size_t i = 0; i < S; ++i)
                r[i] = cell().kinds[i].hold;
            return r;
        }();

        // 槽位 → 扁平状态机元组的下标（编译期常量，运行时无查表）
        static constexpr std::array<std::size_t, S> table_to_decl = []() consteval {
            std::array<std::size_t, S> r{};
            for (std::size_t i = 0; i < S; ++i)
                r[i] = static_cast<std::size_t>(cell().table_to_decl[i]);
            return r;
        }();
    };

    // ═══════════════════════════════════════════════════════════════════
    // 6. stage：一格的现场（唯一的可变状态）
    //
    //    四步就是全部：收票 → 兜底 → 退出 → 争胜。
    //    槽位数 S 是编译期常量，所以每个槽的判断都是直线代码；
    //    唯一的 pack 展开在 win 里，因为播报需要编译期下标。
    // ═══════════════════════════════════════════════════════════════════
    template <class Tree, std::size_t Cell>
    struct stage
    {
        using law = cell_table<Tree, Cell>;
        static constexpr std::size_t S = law::S;

        struct slot
        {
            bool alive = false;
            bool pending = false; // 已完成、但在等更具体的
            time_point begin{};
            time_point end{};
        };

        // 本帧每槽的票
        enum class vote : std::uint8_t
        {
            none,
            done,
            quit
        };

        std::array<slot, S> s{};
        std::array<vote, S> v{};

      private:
        // 我等的那些人，还有活着的吗
        template <std::size_t I>
        constexpr bool blocked() const noexcept
        {
            for (std::size_t j = 0; j < S; ++j)
                if (j != I && s[j].alive && law::awaits[I][j])
                    return true;
            return false;
        }

        // 收票：识别器说的话 → 现场
        template <std::size_t I>
        constexpr void collect(step_t r, time_point now) noexcept
        {
            switch (r)
            {
            case step_t::active:
                if (!s[I].alive)
                    s[I].alive = true, s[I].begin = now;
                break;
            case step_t::done:
                if (!s[I].alive)
                    s[I].alive = true, s[I].begin = now;
                if (!s[I].pending)
                    s[I].end = now; // 一次交互只记第一次完成
                v[I] = vote::done;
                break;
            case step_t::aborted:
                if (s[I].alive)
                    v[I] = vote::quit;
                break;
            case step_t::idle:
                if (s[I].alive && !s[I].pending)
                    v[I] = vote::quit;
                break;
            case step_t::hold:
                break;
            }
        }

        // ⓪ 兜底：活得太久**还没结论** ⇒ 静默判负
        template <std::size_t I, class IO>
        constexpr void expire(time_point now, IO &io) noexcept
        {
            if (!s[I].alive || v[I] == vote::done)
                return; // 本帧有结论的不算"没结论"
            if (law::hold[I] == std::chrono::nanoseconds::zero())
                return;
            if (now - s[I].begin <= law::hold[I])
                return;
            io.template reset<law::table_to_decl[I]>();
            s[I].alive = false, s[I].pending = false, v[I] = vote::none;
        }

        // ① 退出：从定义域里删掉，不是判决 ⇒ 静默
        template <std::size_t I, class IO>
        constexpr void drop(IO &io) noexcept
        {
            if (v[I] != vote::quit)
                return;
            io.template reset<law::table_to_decl[I]>();
            s[I].alive = false, s[I].pending = false, v[I] = vote::none;
        }

        // 让位：J 若是我 I 的对手，就退场并播报
        template <std::size_t J, std::size_t I, class IO>
        constexpr void yield_to(time_point now, IO &io) noexcept
        {
            if (J == I || !s[J].alive)
                return;
            if (!law::excl[I][J] && !law::awaits[J][I])
                return; // 互斥的，或比我抽象的

            const time_point b = s[J].begin, e = s[J].end;
            s[J].alive = false, s[J].pending = false;
            io.template emit<law::table_to_decl[J]>(false, law::names[J], b, e, now);
            io.template reset<law::table_to_decl[J]>();
        }

        // 我赢了：先让位，最后发赢家（全库唯一播报 accepted 的地方）
        template <std::size_t I, std::size_t... Js, class IO>
        constexpr void win(std::index_sequence<Js...>, time_point now, IO &io) noexcept
        {
            const time_point b = s[I].begin, e = s[I].end;
            (yield_to<Js, I>(now, io), ...);
            s[I].alive = false, s[I].pending = false;
            io.template emit<law::table_to_decl[I]>(true, law::names[I], b, e, now);
            io.template reset<law::table_to_decl[I]>();
        }

        // 争胜：这一格要不要现在赢
        template <std::size_t I, class IO>
        constexpr bool try_win(time_point now, IO &io) noexcept
        {
            if (!s[I].alive)
                return false;
            if (v[I] != vote::done && !s[I].pending)
                return false;
            if (blocked<I>())
            {
                s[I].pending = true;
                return false;
            }
            win<I>(std::make_index_sequence<S>{}, now, io);
            return true;
        }

      public:
        // 一帧的判决：四步，顺序就是语义
        template <class IO>
        constexpr void settle(std::array<step_t, S> sigs, time_point now, IO &io) noexcept
        {
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                (collect<Is>(sigs[Is], now), ...); // 收票
                (expire<Is>(now, io), ...);        // ⓪ 兜底
                (drop<Is>(io), ...);               // ① 退出
                for (std::size_t round = 0; round < S; ++round)
                {
                    bool moved = false;
                    // ② 争胜：本帧完成的 ∪ 上一帧挂起的，按槽位序（最具体先）
                    ((moved = try_win<Is>(now, io) || moved), ...);
                    v.fill(vote::none);
                    if (!moved)
                        break;
                }
            }(std::make_index_sequence<S>{});
            v.fill(vote::none);
        }
    };
    // ═══════════════════════════════════════════════════════════════════
    // 7. sink：{arena, thunk} 两字。构造、载荷探测、thunk 都在它自己身上
    // ═══════════════════════════════════════════════════════════════════
    struct sink
    {
        void *self = nullptr;
        void (*fn)(void *, const void *) noexcept = nullptr;

        constexpr void operator()(const void *p) const noexcept
        {
            if (fn)
                fn(self, p);
        }

        template <class Leaf>
        static constexpr bool has_payload =
            requires(const typename Leaf::machine &m) { m.payload; };

        template <class Leaf>
        using payload_t = std::remove_cvref_t<
            decltype(std::declval<const typename Leaf::machine &>().payload)>;

        template <class Arena, class Leaf>
        static void call(void *self, const void *p) noexcept
        {
            Arena &a = *static_cast<Arena *>(self);
            if constexpr (has_payload<Leaf>)
            {
                static_assert(noexcept(a.template invoke<Leaf::name>(
                                  std::declval<const payload_t<Leaf> &>())),
                              "on_<kind> 必须是 noexcept：胜利回调是无异常的函数指针");
                a.template invoke<Leaf::name>(*static_cast<const payload_t<Leaf> *>(p));
            }
            else
            {
                static_assert(noexcept(a.template invoke<Leaf::name>()),
                              "on_<kind> 必须是 noexcept：胜利回调是无异常的函数指针");
                a.template invoke<Leaf::name>();
            }
        }

        template <class Arena, class Leaf>
        static sink build(Arena &a) noexcept
        {
            return sink{&a, &call<Arena, Leaf>};
        }
    };

    // 调试用：把声明读成机器码的样子（consteval，不参与运行期）
    template <class Tree>
    consteval static_string debug_blueprint()
    {
        const auto cells = cell_data::build(Tree::plan());
        const auto p = Tree::plan();
        std::string out;
        out += "relations:";
        for (auto [i, j] : p.awaits)
        {
            out += ' ';
            out += p.kinds[static_cast<std::size_t>(i)].fn_name.view();
            out += "->";
            out += p.kinds[static_cast<std::size_t>(j)].fn_name.view();
        }
        for (auto [i, j] : p.excl)
        {
            out += ' ';
            out += p.kinds[static_cast<std::size_t>(i)].fn_name.view();
            out += "~";
            out += p.kinds[static_cast<std::size_t>(j)].fn_name.view();
        }
        out += "\ncells:";
        for (const auto &c : cells)
        {
            out += ' ';
            out += c.kinds[0].frame.view();
            out += '{';
            for (std::size_t i = 0; i < c.kinds.size(); ++i)
            {
                if (i)
                    out += ',';
                out += c.kinds[i].fn_name.view();
            }
            out += '}';
        }
        return static_string{std::define_static_string(out)};
    }

    // ═══════════════════════════════════════════════════════════════════
    // 8. ui：运行期只有三样 —— 状态机元组（声明序）+ 每格现场 + 观测
    //    下标全部是编译期常量，所以没有查表。
    // ═══════════════════════════════════════════════════════════════════
    template <class Tree, class Obs,
              class Seq = std::make_index_sequence<cell_count_v<Tree>>>
    struct ui_impl;

    template <class Tree, class Obs, std::size_t... Cs>
    struct ui_impl<Tree, Obs, std::index_sequence<Cs...>>
    {
        using shape = detail::shape_of<Tree>;
        using fsms_t = typename shape::fsms;

        static_assert(std::default_initializable<fsms_t>,
                      "FSM 必须可默认构造：它按值住在 ui 里，而 ui 是 arena 的字段");

        // ── 判决需要的三样 ──
        fsms_t fsms{};                                // 每片叶子的状态机（声明序）
        std::tuple<stage<Tree, Cs>...> stages{};      // 每格的现场
        std::array<sink, leaf_count_v<Tree>> sinks{}; // 绑定期定值

        // ── 不属于判决的两样 ──
        Obs obs{}; // 观测（可换成 no_trace，零字段）

        // 判决落地（唯一副作用点）。Flat 是编译期常量。
        template <std::size_t Flat>
        constexpr void on_verdict(bool accepted, std::string_view name, time_point begin,
                                  time_point end, time_point at) noexcept
        {
            const void *p = nullptr;
            if constexpr (requires { std::get<Flat>(fsms).payload; })
                p = static_cast<const void *>(&std::get<Flat>(fsms).payload);
            if (accepted)
                sinks[Flat](p); // 载荷在 reset 之前取
            obs.push(event{name, accepted ? verdict::accepted : verdict::superseded, at,
                           begin, end});
        }

        template <std::size_t Flat>
        constexpr void reset_kind() noexcept
        {
            if constexpr (requires { std::get<Flat>(fsms).reset(); })
                std::get<Flat>(fsms).reset();
        }

        // stage 的出口：只回答"往哪里写"，不持有时间
        struct io_t
        {
            ui_impl *u = nullptr;
            template <std::size_t Flat>
            constexpr void emit(bool accepted, std::string_view name, time_point b,
                                time_point e, time_point at) noexcept
            {
                u->template on_verdict<Flat>(accepted, name, b, e, at);
            }
            template <std::size_t Flat>
            constexpr void reset() noexcept
            {
                u->template reset_kind<Flat>();
            }
        };

        // 延迟绑定：只用叶子的类型（名字 + 载荷类型），不用对象
        template <class Arena>
        void bind(Arena &a) noexcept
        {
            std::size_t i = 0;
            shape::each_leaf([&]<class Leaf>() noexcept {
                sinks[i++] = sink::template build<Arena, Leaf>(a);
            });
        }

        // ── 输入：推进一帧 ──
        template <class Frame>
        void feed(const Frame &f, time_point now)
        {
            static_assert(
                shape::template accepts<Frame>,
                "这棵树里没有任何识别器认识这个帧类型（检查 machine::frame_type）");
            if constexpr (requires() { detail::feed_guard{&obs.in_feed_}; })
                detail::feed_guard guard{&obs.in_feed_};
            (feed_cell<Frame, Cs>(f, now), ...); // 每格都推进，谁不认识就保持 hold
        }

        // 只过了时间：所有格都推进（兜底、识别器自己的窗口）
        void tick(time_point now)
        {
            if constexpr (requires() { detail::feed_guard{&obs.in_feed_}; })
                detail::feed_guard guard{&obs.in_feed_};
            (tick_cell<Cs>(now), ...);
        }

        std::span<const event> events() const noexcept
        {
            if constexpr (requires() { obs.events(); })
                return obs.events();
            else
                return {};
        }
        void clear_events() noexcept
        {
            if constexpr (requires() { obs.clear(); })
                obs.clear();
        }
        std::size_t dropped() const noexcept
        {
            if constexpr (requires() { obs.dropped(); })
                return obs.dropped();
            else
                return {};
        }

      private:
        // 一格：只 step 认识这个帧的叶子，然后结算
        template <class Frame, std::size_t Cell>
        void feed_cell(const Frame &f, time_point now)
        {
            using law = cell_table<Tree, Cell>;
            std::array<step_t, law::S> sigs{};
            sigs.fill(step_t::hold);
            bool touched = false;
            template for (constexpr auto I : std::ranges::views::indices(law::S))
            {
                auto &m = std::get<law::table_to_decl[I]>(fsms);
                if constexpr (requires { m.step(f, now); })
                {
                    sigs[I] = m.step(f, now);
                    touched = true;
                }
            }
            if (!touched)
                return;

            io_t io{this};
            std::get<Cell>(stages).settle(sigs, now, io);
        }

        template <std::size_t Cell>
        void tick_cell(time_point now)
        {
            using law = cell_table<Tree, Cell>;
            std::array<step_t, law::S> sigs{};
            sigs.fill(step_t::hold);
            template for (constexpr auto I : std::ranges::views::indices(law::S))
            {
                auto &m = std::get<law::table_to_decl[I]>(fsms);
                if constexpr (requires { m.tick(now); })
                    sigs[I] = m.tick(now);
            }
            io_t io{this};
            std::get<Cell>(stages).settle(sigs, now, io);
        }
    };

    template <class Tree, class Obs = trace>
    struct ui : ui_impl<Tree, Obs>
    {
    };

} // namespace uhi

// ═══════════════════════════════════════════════════════════════════════════
//  用户代码：帧、识别器、树、竞技场、场景
// ═══════════════════════════════════════════════════════════════════════════
using uhi::step_t;
using uhi::time_point;
using uhi::leaf;

inline time_point at(int ms)
{
    return time_point{ms};
}

// 帧不带时间：时间是 feed(f, now) 的第二个参数
struct mouse_frame
{
    enum what_t
    {
        press,
        release,
        move,
        wheel
    } what = move;
    double x = 0, y = 0;
    int delta = 0;
};
struct kb_frame
{
    int key = 0;
    bool press = false;
};

inline mouse_frame M(mouse_frame::what_t w, int delta = 0)
{
    return mouse_frame{w, 0, 0, delta};
}
inline kb_frame K(int key, bool press)
{
    return kb_frame{key, press};
}

// N 连击
template <int N>
struct multi_click
{
    using frame_type = mouse_frame;

    struct payload_t
    {
        double x = 0, y = 0;
        int count = 0;
    } payload{};

    int count = 0;
    bool armed = false;
    time_point deadline{};

    step_t step(const mouse_frame &f, time_point now) noexcept
    {
        if (armed && now >= deadline)
        {
            count = 0, armed = false;
            return step_t::aborted;
        }
        if (f.what == mouse_frame::press)
        {
            ++count;
            payload.x = f.x;
            payload.y = f.y;
            payload.count = count;
            return step_t::active;
        }
        if (f.what == mouse_frame::release && count > 0)
        {
            if (count >= N)
            {
                count = 0, armed = false;
                return step_t::done;
            }
            armed = true;
            deadline = now + time_point{300};
            return step_t::active;
        }
        return count > 0 ? step_t::active : step_t::idle;
    }
    step_t tick(time_point now) noexcept
    {
        if (armed && now >= deadline)
        {
            count = 0, armed = false;
            return step_t::aborted;
        }
        return step_t::hold;
    }
    void reset() noexcept
    {
        count = 0, armed = false;
    }
};

// ⚠ 简化示例：hover 定义为"每次 move 成立一次"；真实 hover 要进入/离开状态
struct hover_fsm
{
    using frame_type = mouse_frame;
    step_t step(const mouse_frame &f, time_point) noexcept
    {
        return f.what == mouse_frame::move ? step_t::done : step_t::idle;
    }
    void reset() noexcept {}
};

struct wheel_fsm
{
    using frame_type = mouse_frame;
    struct payload_t
    {
        int delta = 0;
    } payload{};
    step_t step(const mouse_frame &f, time_point) noexcept
    {
        if (f.what != mouse_frame::wheel)
            return step_t::idle;
        payload.delta = f.delta;
        return step_t::done;
    }
    void reset() noexcept {}
};

struct key_A_fsm
{
    using frame_type = kb_frame;
    step_t step(const kb_frame &f, time_point) noexcept
    {
        return (f.press && f.key == 'A') ? step_t::done : step_t::idle;
    }
    void reset() noexcept {}
};

// 卡死：永不结束（靠 hold 兜底清掉）
struct stuck_fsm
{
    using frame_type = mouse_frame;
    bool on = false;
    step_t step(const mouse_frame &f, time_point) noexcept
    {
        if (f.what == mouse_frame::press)
        {
            on = true;
            return step_t::active;
        }
        return on ? step_t::active : step_t::idle;
    }
    void reset() noexcept
    {
        on = false;
    }
};

// 松开才给结论；配短 hold 就是"晚结论 vs 兜底"的测试对象
struct slower_fsm
{
    using frame_type = mouse_frame;
    bool down = false;
    step_t step(const mouse_frame &f, time_point) noexcept
    {
        if (f.what == mouse_frame::press)
        {
            down = true;
            return step_t::active;
        }
        if (f.what == mouse_frame::release && down)
        {
            down = false;
            return step_t::done;
        }
        return down ? step_t::active : step_t::idle;
    }
    void reset() noexcept
    {
        down = false;
    }
};

using tree_main = uhi::all_of<uhi::refines<uhi::refines<leaf<multi_click<1>, "on_click">,
                                                        leaf<multi_click<2>, "on_dbl">>,
                                           leaf<multi_click<3>, "on_triple">>,
                              leaf<hover_fsm, "on_hover">, leaf<wheel_fsm, "on_wheel">,
                              leaf<key_A_fsm, "on_key_A">>;

using tree_live =
    uhi::refines<leaf<slower_fsm, "on_slower">, leaf<stuck_fsm, "on_stuck", 100>>;
using tree_late = leaf<slower_fsm, "on_slower", 100>;

static_assert(uhi::leaf_count_v<tree_main> == 6);
static_assert(uhi::cell_count_v<tree_main> == 4, "3 个 mouse 格 + 1 个键盘格");
static_assert(uhi::cell_count_v<tree_live> == 1);

// ═══════════════════════════════════════════════════════════════════════════
//  竞技场：make_aggregate —— ui 是字段，on_<kind> 是方法
// ═══════════════════════════════════════════════════════════════════════════
enum : int
{
    H_CLICK,
    H_DBL,
    H_TRIPLE,
    H_HOVER,
    H_WHEEL,
    H_KEY,
    H_SLOWER,
    H_STUCK,
    H_N
};

template <class Tree>
auto make_arena()
{
    using mcs::vulkan::meta::field;
    using mcs::vulkan::meta::make_aggregate;
    using mcs::vulkan::meta::method;

    return make_aggregate<"arena">(
        field<"ui">(uhi::ui<Tree>{}), field<"hits">(std::array<int, H_N>{}),
        field<"last_x">(double{}), field<"last_delta">(int{}),
        method<"on_click">([](auto &self, const multi_click<1>::payload_t &p) noexcept {
            ++self.hits[H_CLICK];
            self.last_x = p.x;
        }),
        method<"on_dbl">([](auto &self, const multi_click<2>::payload_t &p) noexcept {
            ++self.hits[H_DBL];
            self.last_x = p.x;
        }),
        method<"on_triple">([](auto &self, const multi_click<3>::payload_t &p) noexcept {
            ++self.hits[H_TRIPLE];
            self.last_x = p.x;
        }),
        method<"on_hover">([](auto &self) noexcept { ++self.hits[H_HOVER]; }),
        method<"on_wheel">([](auto &self, const wheel_fsm::payload_t &p) noexcept {
            ++self.hits[H_WHEEL];
            self.last_delta = p.delta;
        }),
        method<"on_key_A">([](auto &self) noexcept { ++self.hits[H_KEY]; }),
        method<"on_slower">([](auto &self) noexcept { ++self.hits[H_SLOWER]; }),
        method<"on_stuck">([](auto &self) noexcept { ++self.hits[H_STUCK]; }),
        method<"bind">([](auto &self) noexcept { self.ui.bind(self); }));
}

static void dump(const char *label, std::span<const uhi::event> ev)
{
    const auto ms = [](time_point t) {
        return t.count();
    };
    std::print("  {:<24}", label);
    if (ev.empty())
    {
        std::println("(none)");
        return;
    }
    for (const auto &e : ev)
        std::print(" {}:{}@{}({},{})", e.name, uhi::to_string(e.v), ms(e.at), ms(e.begin),
                   ms(e.end));
    std::println("");
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::println("╔══════════════════════════════════════════════════════════════════╗");
    std::println("║ uhi：4 格（3× mouse + 1× keyboard），零分配，判决 = 消除到不动点  ║");
    std::println("╚══════════════════════════════════════════════════════════════════╝");
    std::println("  {}", uhi::debug_blueprint<tree_main>().view());
    std::println("");

    auto a = make_arena<tree_main>();
    auto &u = a.ui;
    const auto ev = [&] {
        return u.events();
    };
    a.invoke<"bind">();

    // ── S1 单击：click 完成但被 dbl/triple 挡住，tick 后才落地 ─────────
    std::println("── S1 单击（延迟落地，跨度不能丢）──");
    u.feed(M(mouse_frame::press), at(0));
    u.feed(M(mouse_frame::release), at(10));
    dump("release@10", ev());
    assert(ev().empty());
    u.tick(at(400));
    dump("tick@400", ev());
    assert(a.hits[H_CLICK] == 1);
    assert(ev().back().begin == at(0) && ev().back().end == at(10) &&
           ev().back().at == at(400));
    u.clear_events();

    // ── S2 双击：要等 triple 的窗口过期才判 ────────────────────────────
    std::println("\n── S2 双击 ──");
    u.feed(M(mouse_frame::press), at(1000));
    u.feed(M(mouse_frame::release), at(1010));
    u.feed(M(mouse_frame::press), at(1080));
    u.feed(M(mouse_frame::release), at(1090));
    dump("release@1090", ev());
    assert(ev().empty());
    u.tick(at(1400));
    const auto e2 = ev();
    dump("tick@1400", e2);
    assert(a.hits[H_DBL] == 1 && a.hits[H_CLICK] == 1);
    assert(e2.size() == 2 && e2[1].name == std::string_view{"on_dbl"});
    assert(e2[1].begin == at(1000) && e2[1].end == at(1090) && e2[1].at == at(1400));
    u.clear_events();

    // ── S3 三击：同一帧定案，两个让位 ──────────────────────────────────
    std::println("\n── S3 三击 ──");
    u.feed(M(mouse_frame::press), at(1200));
    u.feed(M(mouse_frame::release), at(1210));
    u.feed(M(mouse_frame::press), at(1280));
    u.feed(M(mouse_frame::release), at(1290));
    u.feed(M(mouse_frame::press), at(1360));
    u.feed(M(mouse_frame::release), at(1370));
    dump("release@1370", ev());
    assert(a.hits[H_TRIPLE] == 1);
    assert(ev().size() == 3);
    u.clear_events();

    // ── S4 同一通道多格：hover 与 wheel 各自发，互不干扰 ───────────────
    std::println("\n── S4 同一通道多 cell ──");
    u.feed(M(mouse_frame::move), at(2000));
    dump("move@2000", ev());
    assert(a.hits[H_HOVER] == 1);
    u.clear_events();
    u.feed(M(mouse_frame::wheel, +3), at(2100));
    dump("wheel@2100", ev());
    assert(a.hits[H_WHEEL] == 1 && a.last_delta == 3);
    u.clear_events();
    u.feed(M(mouse_frame::move), at(2200));
    u.feed(M(mouse_frame::wheel, -2), at(2200));
    dump("move+wheel@2200", ev());
    assert(a.hits[H_HOVER] == 2 && a.hits[H_WHEEL] == 2 && a.last_delta == -2);
    u.clear_events();

    // ── S5 键盘格：与 mouse 格完全无关 ─────────────────────────────────
    std::println("\n── S5 键盘格 ──");
    u.feed(K('A', true), at(3000));
    dump("kb A@3000", ev());
    assert(a.hits[H_KEY] == 1);
    u.clear_events();

    // ── S6 兜底：卡死的被清掉，挂起的才解锁 ────────────────────────────
    std::println("\n── S6 兜底（stuck 100ms）──");
    auto b = make_arena<tree_live>();
    auto &ub = b.ui;
    b.invoke<"bind">();
    ub.feed(M(mouse_frame::press), at(0));
    ub.feed(M(mouse_frame::release), at(10));
    dump("release@10", ub.events());
    assert(ub.events().empty());
    ub.tick(at(50));
    dump("tick@50", ub.events());
    assert(ub.events().empty());
    ub.tick(at(150));
    const auto e6 = ub.events();
    dump("tick@150", e6);
    assert(b.hits[H_SLOWER] == 1);
    assert(e6.size() == 1 && e6[0].name == std::string_view{"on_slower"});
    assert(e6[0].begin == at(0) && e6[0].end == at(10) && e6[0].at == at(150));

    // ── S7 兜底不许杀"本帧有结论"的假设 ────────────────────────────────
    std::println("\n── S7 晚结论 vs 兜底（hold=100ms，200ms 才 done）──");
    auto d = make_arena<tree_late>();
    auto &ud = d.ui;
    d.invoke<"bind">();
    ud.feed(M(mouse_frame::press), at(0));
    ud.feed(M(mouse_frame::release), at(200));
    const auto e7 = ud.events();
    dump("release@200", e7);
    assert(d.hits[H_SLOWER] == 1);
    assert(e7.size() == 1 && e7[0].begin == at(0) && e7[0].end == at(200));

    // 观测是分离出去的策略：换成 no_trace 就真的少一块
    static_assert(sizeof(uhi::ui<tree_main, uhi::no_trace>) < sizeof(uhi::ui<tree_main>),
                  "no_trace 必须比默认 trace 小：观测不属于判决");

    std::println("\n── 收尾 ──");
    std::println("  sizeof(ui<trace>)    = {} B（含观测缓冲）",
                 sizeof(uhi::ui<tree_main>));
    std::println("  sizeof(ui<no_trace>) = {} B（生产用，无观测字段）",
                 sizeof(uhi::ui<tree_main, uhi::no_trace>));
    std::println("  全部场景通过");
    return 0;
}
