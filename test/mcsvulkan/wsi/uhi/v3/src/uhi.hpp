#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  uhi.hpp —— 语义事件库 · 第一幕：角色与剧本
//
//  目标：你写结构（谁并行、谁细化谁、谁和谁互斥），
//        库在编译期算出"哪些东西需要裁决"，运行期只执行算出来的那张表。
//
//  ┌─ 角色 ─────────────────────────────────────────────────────────────┐
//  │ kind<"click">        语义事件的身份                                │
//  │ source<"mouse.left"> 物理通道的身份。key / mouse 天然不同通道       │
//  │ leaf<K,S,Fsm>        唯一实现 step 的角色（= Qt 的 event() override）│
//  │ all_of<Rs...>        并行：不加任何关系（无因果 ⇒ 无纠纷 ⇒ 无裁决）  │
//  │ one_of<Rs...>        互斥：加对称边（同源、先到先得）               │
//  │ refines<A,B>         B 细化 A：加有向边 A→B（A 必须等 B 失败）      │
//  │ times<D,R>           时窗：只改时序，不改关系                       │
//  ├─ 推导物 ──────────────────────────────────────────────────────────┤
//  │ plan   编译期：kinds + relations（只有 consteval 里存在）           │
//  │ cell   plan 推出：同源 × 连通分量。|kinds|>1 ⇒ 裁决，否则退化       │
//  │ stage  一个 cell 的运行期存活表（按 kind 稠密数组，无 handle）      │
//  └───────────────────────────────────────────────────────────────────┘
//
//  剧本：
//    编译期：组合树 → plan → 检查 → cell 划分 → 每 cell 的"特异性序"
//    运行期：一帧 = ① 所有叶子各走一步（此刻没有任何判决可见）
//                   ② 各 cell 按"最具体者先"结算
//                   ③ 判决回调（caller 决定 reset / 派发）
//
//  这个文件不认识 event_context，不认识 arena，也不认识任何具体手势。
//
//  ═══ 怎么读这个文件（别从第一行读到最后一行）══════════════════════════
//   · 想知道"用户写什么"        → README 的舞台那一节，四行
//   · 想知道"一帧怎么跑的"      → 直接跳到 arena_impl::feed（约 595 行），20 行
//   · 想知道"结构怎么变成 cell" → cells_of（约 318 行）+ validate（约 263 行）
//   · 剩下 200 行 consteval 推导是给编译器读的，人只需知道它们做什么
// ═══════════════════════════════════════════════════════════════════════
#include <array>
#include <cassert>
#include <charconv>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <meta>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "../../../../../../include/detail/meta/static_string.hpp"

namespace uhi
{
    using mcs::vulkan::meta::static_string;
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;

    // ═══════════════════════════════════════════════════════════════════
    // 角色 1/2：两个身份
    //   kind 可以带载荷：kind_t<"click">（无载荷） / kind_t<"drag", drag_event>
    // ═══════════════════════════════════════════════════════════════════
    struct no_payload
    {
    };

    template <static_string Name, class Payload = no_payload>
    struct kind_t
    {
        static constexpr static_string name = Name;
        using payload_type = Payload;
    };
    template <static_string Name>
    struct source_t
    {
        static constexpr static_string name = Name;
    };

    // ═══════════════════════════════════════════════════════════════════
    // plan 的数据形态（只在 consteval 里存在）
    // ═══════════════════════════════════════════════════════════════════
    struct kind_desc
    {
        static_string fn_name;
        static_string source;
        // 兜底时限：一个假设活了这么久还没结论，就静默判负（0 = 不设兜底）
        std::int64_t hold_ns = 0;

        friend constexpr bool operator==(const kind_desc &, const kind_desc &) = default;
    };
    struct relation
    {
        int from = 0; // from 必须等 to 失败（refines 生成）
        int to = 0;
        bool symmetric = false; // true: 互斥（one_of 生成）
        friend constexpr bool operator==(const relation &, const relation &) = default;
    };
    struct plan_data
    {
        std::vector<kind_desc> kinds;
        std::vector<relation> rels;
    };
    consteval plan_data operator+(plan_data a, const plan_data &b)
    {
        a.kinds.insert(a.kinds.end(), b.kinds.begin(), b.kinds.end());
        a.rels.insert(a.rels.end(), b.rels.begin(), b.rels.end());
        return a;
    }
    // 把子计划的关系下标整体平移。
    // 拼到父计划后面时必须做：子计划里的关系是 0 基的，直接并进来会指到别的 kind。
    consteval plan_data offset_rels(plan_data p, int base)
    {
        for (auto &r : p.rels)
        {
            r.from += base;
            r.to += base;
        }
        return p;
    }

    // ═══════════════════════════════════════════════════════════════════
    // 角色 3：leaf —— 唯一有 step 的角色
    //   Fsm 提供：step(frame, now) -> step_t，reset()
    // ═══════════════════════════════════════════════════════════════════
    enum class step_t : std::uint8_t
    {
        idle,    // 没有假设
        active,  // 假设进行中
        done,    // 识别成功（请求裁决）
        aborted, // 放弃（悄悄消失，不产生判决）
        hold     // 只过了时间，状态不变（tick 用）
    };

    // 对外只播报两种判决：赢，和被赢家取代。
    // "自己放弃"不是判决——没发生的交互不该通知任何人。
    enum class verdict : std::uint8_t
    {
        accepted,  // 你就是这次交互的解释
        superseded // 更具体的（或先到的）赢了，你让位
    };

    //   Hold：兜底时限。假设活过这么久还没结论 ⇒ 静默判负（0ms = 不设兜底）
    template <class K, class S, class Fsm, std::int64_t HoldMs = 0>
    struct leaf
    {
        using kind = K;
        using source = S;
        using payload_type = typename K::payload_type;

        static constexpr std::int64_t hold_ms = HoldMs;

        Fsm fsm{};

        static consteval plan_data plan()
        {
            return plan_data{{kind_desc{K::name, S::name, HoldMs * 1'000'000}}, {}};
        }
        // 只接受"这个识别器认识的帧"。多帧类型共存时，feed 靠这条约束做分发：
        // 不认识的帧不会实例化这里的函数体，槽位也保持原样。
        template <class Frame>
            requires requires(Fsm &m, const Frame &f, time_point t) { m.step(f, t); }
        step_t step(const Frame &f, time_point now) noexcept
        {
            return fsm.step(f, now);
        }
        // 只有时间前进（没有帧）：FSM 想处理就给 tick(now)，不想就 hold（状态不变）
        step_t tick(time_point now) noexcept
        {
            if constexpr (requires { fsm.tick(now); })
                return fsm.tick(now);
            else
                return step_t::hold;
        }
        // 载荷存在 FSM 里（约定：FSM 有 payload 成员），这里只把地址交出去。
        //
        // 契约：kind 的 payload_type 必须**恰好**等于 fsm.payload 的类型。
        // 这条不能靠 void* 擦除蒙混过去——擦完再 static_cast 回去就是 UB，
        // 所以在这里用 static_assert 钉死。
        [[nodiscard]] const void *payload() const noexcept
        {
            if constexpr (std::is_same_v<payload_type, no_payload>)
                return nullptr;
            else
            {
                using fsm_payload = std::remove_cvref_t<decltype(fsm.payload)>;
                static_assert(
                    std::is_same_v<fsm_payload, payload_type>,
                    "FSM 的 payload 类型必须等于 kind_t 的载荷类型：\n"
                    "  kind_t<\"on_x\", P> 的 P\n"
                    "  必须与 FSM::payload 的类型完全一致（否则 on_x 收到的是野指针）");
                return &fsm.payload;
            }
        }
        void reset() noexcept
        {
            fsm.reset();
        }
    };

    template <class T>
    inline constexpr bool is_leaf_v = false;
    template <class K, class S, class F, std::int64_t H>
    inline constexpr bool is_leaf_v<leaf<K, S, F, H>> = true;

    // 树里有多少叶子（遍历顺序 = plan 顺序）
    template <class T>
    inline constexpr std::size_t leaf_count_v = []() consteval {
        if constexpr (is_leaf_v<T>)
            return std::size_t{1};
        else
        {
            std::size_t n = 0;
            T tmp{};
            std::apply(
                [&](auto &...c) {
                    ((n += leaf_count_v<std::remove_cvref_t<decltype(c)>>), ...);
                },
                tmp.children);
            return n;
        }
    }();

    // ═══════════════════════════════════════════════════════════════════
    // 角色 4/5/6/7：四个组合子（每个都是 children + plan()）
    // ═══════════════════════════════════════════════════════════════════
    template <class... Rs>
    struct all_of
    {
        std::tuple<Rs...> children{};
        static consteval plan_data plan()
        {
            plan_data p{};
            ((p = p + offset_rels(Rs::plan(), static_cast<int>(p.kinds.size()))), ...);
            return p;
        }
    };

    template <class... Rs>
    struct one_of
    {
        std::tuple<Rs...> children{};

        static consteval plan_data plan()
        {
            plan_data p{};
            constexpr std::size_t n = sizeof...(Rs);
            std::array<std::size_t, n> off{};
            std::array<std::size_t, n> cnt{};
            std::size_t i = 0;
            ((off[i] = p.kinds.size(),
              p = p + offset_rels(Rs::plan(), static_cast<int>(p.kinds.size())),
              cnt[i] = p.kinds.size() - off[i], ++i),
             ...);
            for (std::size_t a = 0; a < n; ++a)
                for (std::size_t b = a + 1; b < n; ++b)
                    for (std::size_t x = off[a]; x < off[a] + cnt[a]; ++x)
                        for (std::size_t y = off[b]; y < off[b] + cnt[b]; ++y)
                            p.rels.push_back(
                                relation{static_cast<int>(x), static_cast<int>(y), true});
            return p;
        }
    };

    template <class A, class B>
    struct refines
    {
        std::tuple<A, B> children{};

        static consteval plan_data plan()
        {
            const auto na = static_cast<int>(A::plan().kinds.size());
            auto p = A::plan() + offset_rels(B::plan(), na);
            for (int i = 0; i < na; ++i)
                for (std::size_t j = static_cast<std::size_t>(na); j < p.kinds.size();
                     ++j)
                    p.rels.push_back(relation{i, static_cast<int>(j), false});
            return p;
        }
    };

    template <class D, class R>
    struct times
    {
        D duration{};
        std::tuple<R> children{};

        static consteval plan_data plan()
        {
            return R::plan();
        }
    };

    // 遍历：f(index_constant<全局序号>, leaf&)
    template <std::size_t I, class T, class F>
    void walk_leaves(T &t, F &&f)
    {
        if constexpr (is_leaf_v<T>)
            f(std::integral_constant<std::size_t, I>{}, t);
        else
        {
            constexpr std::size_t n = std::tuple_size_v<decltype(t.children)>;
            [&]<std::size_t... Js>(std::index_sequence<Js...>) {
                constexpr auto off = []() consteval {
                    std::array<std::size_t, n> a{};
                    std::size_t o = 0;
                    [&]<std::size_t... K>(std::index_sequence<K...>) {
                        ((a[K] = o,
                          o += leaf_count_v<
                              std::remove_cvref_t<decltype(std::get<K>(t.children))>>),
                         ...);
                    }(std::make_index_sequence<n>{});
                    return a;
                }();
                (walk_leaves<I + off[Js]>(std::get<Js>(t.children), f), ...);
            }(std::make_index_sequence<n>{});
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // 编译期：plan → 检查 → cell
    // ═══════════════════════════════════════════════════════════════════
    namespace detail
    {
        consteval void fail(std::string msg)
        {
            throw std::meta::exception{msg, std::meta::current_function()};
        }

        consteval bool awaits(const plan_data &p, std::size_t i, std::size_t j)
        {
            for (const auto &r : p.rels)
                if (!r.symmetric && r.from == static_cast<int>(i) &&
                    r.to == static_cast<int>(j))
                    return true;
            return false;
        }
        consteval bool excludes(const plan_data &p, std::size_t i, std::size_t j)
        {
            for (const auto &r : p.rels)
                if (r.symmetric &&
                    ((r.from == static_cast<int>(i) && r.to == static_cast<int>(j)) ||
                     (r.from == static_cast<int>(j) && r.to == static_cast<int>(i))))
                    return true;
            return false;
        }

        consteval void validate(const plan_data &p)
        {
            const auto n = p.kinds.size();
            for (std::size_t i = 0; i < n; ++i)
                for (std::size_t j = i + 1; j < n; ++j)
                    if (p.kinds[i].fn_name == p.kinds[j].fn_name)
                        fail("duplicate kind [" + std::string(p.kinds[i].fn_name.view()) +
                             "]：同一个语义事件只能有一个生产者");

            for (const auto &r : p.rels)
            {
                if (r.from < 0 || r.to < 0 || static_cast<std::size_t>(r.from) >= n ||
                    static_cast<std::size_t>(r.to) >= n)
                    fail("relation index out of range");
                if (p.kinds[r.from].source != p.kinds[r.to].source)
                    fail("kind [" + std::string(p.kinds[r.from].fn_name.view()) +
                         "] 与 [" + std::string(p.kinds[r.to].fn_name.view()) +
                         "] 在不同通道上：它们永远不会相遇，关系没有意义");
            }

            for (std::size_t i = 0; i < n; ++i)
                for (std::size_t j = 0; j < n; ++j)
                    if (i != j && (awaits(p, i, j) || awaits(p, j, i)) &&
                        excludes(p, i, j))
                        fail("kind [" + std::string(p.kinds[i].fn_name.view()) +
                             "] 与 [" + std::string(p.kinds[j].fn_name.view()) +
                             "] 既被细化又互斥：声明自相矛盾");

            std::vector<int> indeg(n, 0);
            for (std::size_t i = 0; i < n; ++i)
                for (std::size_t j = 0; j < n; ++j)
                    if (awaits(p, i, j))
                        ++indeg[i];
            std::vector<bool> done(n, false);
            std::size_t left = n;
            while (left > 0)
            {
                bool any = false;
                for (std::size_t i = 0; i < n; ++i)
                    if (!done[i] && indeg[i] == 0)
                    {
                        done[i] = true;
                        --left;
                        any = true;
                        for (std::size_t j = 0; j < n; ++j)
                            if (awaits(p, j, i))
                                --indeg[j];
                    }
                if (!any)
                    fail("细化关系成环：谁等谁永远没有答案");
            }
        }

        struct cell_info
        {
            static_string source;
            std::vector<int> members; // 全局 kind 序号，按"最具体先"
        };

        consteval std::vector<cell_info> cells_of(const plan_data &p)
        {
            const auto n = p.kinds.size();
            std::vector<int> root(n);
            for (std::size_t i = 0; i < n; ++i)
                root[i] = static_cast<int>(i);
            const auto find = [&](int x) {
                while (root[x] != x)
                    x = root[x] = root[root[x]];
                return x;
            };
            for (const auto &r : p.rels)
                root[find(r.from)] = find(r.to);

            std::vector<cell_info> cells;
            std::vector<int> cell_root;
            std::vector<int> which(n, -1);
            for (std::size_t i = 0; i < n; ++i)
            {
                const int r = find(static_cast<int>(i));
                for (std::size_t c = 0; c < cell_root.size(); ++c)
                    if (cell_root[c] == r)
                        which[i] = static_cast<int>(c);
                if (which[i] == -1)
                {
                    which[i] = static_cast<int>(cells.size());
                    cell_root.push_back(r);
                    cells.push_back(cell_info{p.kinds[i].source, {}});
                }
                cells[which[i]].members.push_back(static_cast<int>(i));
            }

            for (auto &c : cells)
            {
                std::vector<int> indeg(c.members.size(), 0);
                for (std::size_t a = 0; a < c.members.size(); ++a)
                    for (std::size_t b = 0; b < c.members.size(); ++b)
                        if (awaits(p, c.members[a], c.members[b]))
                            ++indeg[a];
                std::vector<int> order;
                while (order.size() < c.members.size())
                    for (std::size_t a = 0; a < c.members.size(); ++a)
                        if (indeg[a] == 0)
                        {
                            indeg[a] = -1;
                            order.push_back(c.members[a]);
                            for (std::size_t b = 0; b < c.members.size(); ++b)
                                if (awaits(p, c.members[b], c.members[a]))
                                    --indeg[b];
                        }
                c.members = order;
            }
            return cells;
        }

        consteval std::string blueprint(const plan_data &p)
        {
            const auto cells = cells_of(p);
            std::string s = "relations:";
            if (p.rels.empty())
                s += " (none)";
            for (const auto &r : p.rels)
            {
                s += ' ';
                s += p.kinds[r.from].fn_name.view();
                s += r.symmetric ? "~" : "->";
                s += p.kinds[r.to].fn_name.view();
            }
            s += "\ncells:";
            for (const auto &c : cells)
            {
                s += ' ';
                s += c.source.view();
                s += c.members.size() > 1 ? "{arbitrate:" : "{direct:";
                for (std::size_t i = 0; i < c.members.size(); ++i)
                {
                    if (i)
                        s += ',';
                    s += p.kinds[c.members[i]].fn_name.view();
                }
                s += '}';
            }
            return s;
        }
    } // namespace detail

    // ── 落成普通数组（constexpr 变量里不能有 vector） ─────────────────
    template <class Tree>
    inline constexpr auto plan_kinds_v = std::define_static_array(Tree::plan().kinds);
    template <class Tree>
    inline constexpr auto plan_rels_v = std::define_static_array(Tree::plan().rels);

    template <class Tree>
    inline constexpr bool valid_v = []() consteval {
        detail::validate(Tree::plan());
        return true;
    }();

    template <class Tree>
    inline constexpr std::size_t cell_count_v = detail::cells_of(Tree::plan()).size();

    template <class Tree>
    inline constexpr auto cell_size_v = []() consteval {
        std::array<std::size_t, cell_count_v<Tree>> a{};
        const auto cells = detail::cells_of(Tree::plan());
        for (std::size_t i = 0; i < cells.size(); ++i)
            a[i] = cells[i].members.size();
        return a;
    }();

    template <class Tree, std::size_t Cell, std::size_t N>
    consteval std::array<int, N> cell_members_arr()
    {
        const auto cells = detail::cells_of(Tree::plan());
        std::array<int, N> a{};
        for (std::size_t i = 0; i < N; ++i)
            a[i] = cells[Cell].members[i];
        return a;
    }

    template <class Tree, std::size_t Cell>
    inline constexpr auto cell_members_v =
        cell_members_arr<Tree, Cell, cell_size_v<Tree>[Cell]>();

    template <class Tree>
    consteval int cell_of_kind(std::size_t global_kind)
    {
        const auto cells = detail::cells_of(Tree::plan());
        for (std::size_t c = 0; c < cells.size(); ++c)
            for (int m : cells[c].members)
                if (m == static_cast<int>(global_kind))
                    return static_cast<int>(c);
        return -1;
    }

    // ═══════════════════════════════════════════════════════════════════
    // 角色：stage —— 一个 cell 的运行期存活表
    //   一个 kind 一个槽（I0 天然成立），无 handle、无容量、无堆
    //     awaits(i,j)   ⇒ i 在 j 还活着时不能赢（deferred）
    //     awaits(j,i)   ⇒ i 赢了 j 就得让位（superseded）
    //     excludes(i,j) ⇒ i 赢了 j 就得让位
    //   log(global_kind, 0=accepted / 1=superseded / 2=rejected)
    // ═══════════════════════════════════════════════════════════════════
    template <class Tree, std::size_t Cell>
    struct stage
    {
        static constexpr auto members = cell_members_v<Tree, Cell>;
        static constexpr int N = static_cast<int>(members.size());

        struct slot
        {
            bool alive = false;
            bool defered = false;
            time_point begin{}, end{};
        };

        std::array<slot, N> s{};
        std::array<std::uint8_t, N> want{}; // 0 none / 1 done / 2 abort

        // 每个 kind 的兜底时限（0 = 不设）
        static constexpr std::array<std::int64_t, N> holds_ns = []() consteval {
            std::array<std::int64_t, N> h{};
            for (int i = 0; i < N; ++i)
                h[i] = plan_kinds_v<Tree>[static_cast<std::size_t>(members[i])].hold_ns;
            return h;
        }();

        static constexpr int local_of(std::size_t global) noexcept
        {
            for (int i = 0; i < N; ++i)
                if (members[i] == static_cast<int>(global))
                    return i;
            return -1;
        }

        // 直接边（只用于构造下面两张矩阵；运行期不再扫关系串）
        static constexpr bool awaits_direct(int i, int j) noexcept
        {
            for (const auto &r : plan_rels_v<Tree>)
                if (!r.symmetric && r.from == members[i] && r.to == members[j])
                    return true;
            return false;
        }
        static constexpr bool excludes_direct(int i, int j) noexcept
        {
            for (const auto &r : plan_rels_v<Tree>)
                if (r.symmetric && ((r.from == members[i] && r.to == members[j]) ||
                                    (r.from == members[j] && r.to == members[i])))
                    return true;
            return false;
        }

        // awaits 的**传递闭包**：更具体的没说话之前，更抽象的一律不许发。
        //   click→dbl→triple 这种三级链，click 必须一起等 triple：
        //   否则 dbl 一超时就让 click 落地，紧接着来第三次点击就再也解释不了了。
        // ── 运行期只有这两张表，二者都是"格内、闭包后、只读"的 ──
        //   awaits_t[i][j]：i 等 j（j 活着 i 不能赢）
        //   excl_t[i][j]  ：i 与 j 互斥（对称）
        // 判决只读这两张，不再扫关系串 ⇒ 只有一种表示，没有"两份表靠定理粘着"。
        static constexpr auto awaits_t = []() consteval {
            std::array<std::array<bool, N>, N> a{};
            for (int i = 0; i < N; ++i)
                for (int j = 0; j < N; ++j)
                    a[i][j] = awaits_direct(i, j);
            for (int k = 0; k < N; ++k)
                for (int i = 0; i < N; ++i)
                    for (int j = 0; j < N; ++j)
                        if (a[i][k] && a[k][j])
                            a[i][j] = true;
            return a;
        }();
        static constexpr auto excl_t = []() consteval {
            std::array<std::array<bool, N>, N> a{};
            for (int i = 0; i < N; ++i)
                for (int j = 0; j < N; ++j)
                    a[i][j] = excludes_direct(i, j);
            return a;
        }();

        template <std::size_t Global>
        void on(step_t r, time_point now) noexcept
        {
            constexpr int i = local_of(Global);
            static_assert(i >= 0, "这个 kind 不属于本 cell");
            switch (r)
            {
            case step_t::active:
                if (!s[i].alive)
                {
                    s[i].alive = true;
                    s[i].begin = now;
                }
                break;
            case step_t::done:
                if (!s[i].alive) // 单帧手势：隐式开在当帧
                {
                    s[i].alive = true;
                    s[i].begin = now;
                }
                // 一次交互只记**第一次**完成时刻：之后 FSM 再喊 done 也不改跨度。
                //（挂起期间重复 done 是识别器在重申，不是交互又结束了一次）
                if (!s[i].defered)
                    s[i].end = now;
                want[i] = 1;
                break;
            case step_t::aborted:
                if (s[i].alive)
                    want[i] = 2;
                break;
            case step_t::hold:
                break; // 只过了时间：什么都不变
            case step_t::idle:
                if (s[i].alive && !s[i].defered)
                    want[i] = 2;
                break;
            }
        }

        template <class Log, class Kill>
        void settle(Log &log, Kill &kill, time_point now) noexcept
        {
            // ⓪ 兜底：活得比 hold 还久、又没有结论的假设 ⇒ 静默判负。
            //    只判负，不发明赢家——"谁赢"永远是识别器说的话。
            for (int i = 0; i < N; ++i)
                if (s[i].alive && want[i] != 1 && holds_ns[i] > 0 &&
                    std::chrono::duration_cast<std::chrono::nanoseconds>(now - s[i].begin)
                            .count() > holds_ns[i])
                {
                    s[i].alive = false;
                    s[i].defered = false;
                    want[i] = 0;
                    kill(static_cast<std::size_t>(
                        members[i])); // 识别器也要清：它不知道槽位没了
                }

            // ① 先处理"自己放弃"：它们在这一帧被审判之前就已经离场，
            //    不该再被算成"被赢家压掉"。否则会给上层一段假回滚。
            for (int i = 0; i < N; ++i)
                if (want[i] == 2)
                {
                    s[i].alive = false;
                    s[i].defered = false;
                    want[i] = 0;
                    kill(static_cast<std::size_t>(members[i]));
                }

            // ② 争胜：**本帧完成的** 与 **上一帧挂起的** 合在一起，按槽位序
            //    （最具体先）统一选；反复扫到不动点，因为赢一个可能解锁后面的。
            //    这与"判决 = 最小不动点"的定义一致。
            //    （旧版把两者分成两遍：先本帧完成、再 pending，
            //      等于给"本帧完成"的人加了与 order 无关的优先权。）
            for (int again = 0; again < N; ++again)
            {
                bool moved = false;
                for (int i = 0; i < N; ++i)
                {
                    if (!s[i].alive)
                        continue;
                    if (want[i] != 1 && !s[i].defered)
                        continue;
                    if (blocked(i))
                    {
                        s[i].defered = true;
                        continue;
                    }
                    win(i, log);
                    moved = true;
                }
                want.fill(0); // 第一轮之后只剩 pending
                if (!moved)
                    break;
            }
            want.fill(0);

            // I1：挂起者必然还活着（死后不该留 pending 标志）
            for (int i = 0; i < N; ++i)
                assert(!s[i].defered || s[i].alive);
        }

        bool blocked(int i) const noexcept
        {
            for (int j = 0; j < N; ++j)
                if (j != i && s[j].alive && awaits_t[i][j])
                    return true;
            return false;
        }

        template <class Log>
        void win(int i, Log &log) noexcept
        {
            for (int j = 0; j < N; ++j)
                if (j != i && s[j].alive && (excl_t[i][j] || awaits_t[j][i]))
                {
                    s[j].alive = false;
                    s[j].defered = false; // I1：死后不留挂起标志
                    log(static_cast<std::size_t>(members[j]), verdict::superseded,
                        s[j].begin, s[j].end);
                }
            s[i].alive = false;
            s[i].defered = false;
            log(static_cast<std::size_t>(members[i]), verdict::accepted, s[i].begin,
                s[i].end);
        }
    };

    // ═══════════════════════════════════════════════════════════════════
    // 帧的时间从哪来：库不认识你的帧，你告诉它一次。
    //   template <> struct uhi::frame_traits<my_frame> {
    //       static uhi::time_point when(const my_frame &f) noexcept; };
    // 对 arena_event::input_frame 就是一行：return f.change.time_point;
    // ═══════════════════════════════════════════════════════════════════
    // 主模板必须是"定义好的空壳"：没有 when ⇒ frame_like 为 false。
    // （如果只声明不定义，探测时会变成硬错误而不是"不满足"，上层就无法按类型裁剪。）
    template <class Frame>
    struct frame_traits
    {
    };

    // 只有"能被读时间"的帧才允许进 feed。
    // 于是上层那种"按类型裁剪"的派发（requires/can_noexcept_invoke）会
    // 自动把 ui 不认识的类型丢掉，而不是报错。
    template <class F>
    concept frame_like = requires(const F &f) {
        {
            frame_traits<std::remove_cvref_t<F>>::when(f)
        } noexcept -> std::same_as<time_point>;
    };

    // ═══════════════════════════════════════════════════════════════════
    // 载荷 + 胜利回调（绑定到 make_aggregate 的 self 上）
    //
    //   胜利回调是 2 字函数指针，可以**延迟绑定**：
    //       uhi::ui<tree> ui{};
    //       ui.bind(arena);      // 之后，accepted → arena.invoke<"on_click">(e)
    //
    //   arena 是 make_aggregate 造的普通聚合体，uhi 不认识它的类型，
    //   只在 bind 时把 {&arena, thunk<Arena,G>} 两字存下来。
    // ═══════════════════════════════════════════════════════════════════
    struct event
    {
        static_string name{}; // 哪个语义事件（自带名字，便于打印/日志）
        verdict v{};
        time_point at{};           // 判决发生的时刻（本帧）
        time_point begin{}, end{}; // 这次交互的时间跨度
    };

    struct sink
    {
        void *self = nullptr;
        void (*fn)(void *, const void *) noexcept = nullptr;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return fn != nullptr;
        }
        void operator()(const void *payload) const noexcept
        {
            if (fn)
                fn(self, payload);
        }
    };

    // 编译期字符串拼接："on_" + "click" → "on_click"
    consteval static_string cat(static_string a, static_string b)
    {
        std::string s{a.view()};
        s += b.view();
        return static_string{std::define_static_string(s)};
    }

    // 每个 leaf 一个 thunk：把 {void*, 载荷} 还原成 arena.invoke<"on_<kind>">(载荷)
    //   有载荷的 kind → on_<kind>(const Payload&)
    //   无载荷的 kind → on_<kind>()
    template <class Arena, class Leaf>
    void on_leaf(void *self, const void *payload) noexcept
    {
        using K = typename Leaf::kind;
        using P = typename K::payload_type;
        static constexpr auto name = cat("on_", K::name);

        // 契约：arena 的 on_<kind> 必须收得下载荷，而且必须 noexcept。
        //   ——不接受这个类型 ⇒ invoke 找不到可调用的成员，编译失败（好）
        //   ——不是 noexcept    ⇒ 这里显式拦下，否则会在运行期 terminate
        if constexpr (std::is_same_v<P, no_payload>)
        {
            static_assert(noexcept(std::declval<Arena &>().template invoke<name>()),
                          "on_<kind> 必须是 noexcept：胜利回调是无异常的 C 函数指针，"
                          "handler 里抛异常会直接 terminate");
            static_cast<Arena *>(self)->template invoke<name>();
        }
        else
        {
            static_assert(noexcept(std::declval<Arena &>().template invoke<name>(
                              std::declval<const P &>())),
                          "on_<kind> 必须是 noexcept：胜利回调是无异常的 C 函数指针，"
                          "handler 里抛异常会直接 terminate");
            static_cast<Arena *>(self)->template invoke<name>(
                *static_cast<const P *>(payload));
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // ui —— 舞台。用户只碰这几个名字：
    //     uhi::ui<Tree> ui;             // 装配
    //     ui.bind(arena);               // 延迟绑定到 make_aggregate 的 self
    //     ui.feed(frame)                // 唯一的输入动词（时间从帧里取）
    //     ui.tick(now)                  // 只有时间：让识别器看一眼时钟
    //     ui.events() / uhi::to_string  // 走过的事件（观测）
    //     uhi::describe<Tree>()         // 编译期结构 → 一段字
    // ═══════════════════════════════════════════════════════════════════
    template <class Tree, class Seq>
    struct ui_impl;

    template <class Tree, std::size_t... Cs>
    struct ui_impl<Tree, std::index_sequence<Cs...>>
    {
        Tree tree{};
        std::tuple<stage<Tree, Cs>...> stages{};
        std::array<sink, leaf_count_v<Tree>> sinks{};

        static constexpr std::size_t trace_capacity = 32;
        std::array<event, trace_capacity> trace{};
        std::size_t n_events = 0;
        bool in_feed_ = false;

        // 延迟绑定：把每个 leaf 接到 arena 的 on_<kind> 方法上
        template <class Arena>
        void bind(Arena &a) noexcept
        {
            walk_leaves<0>(tree, [&](auto idx, auto &lf) {
                constexpr std::size_t g = decltype(idx)::value;
                using leaf_t = std::remove_cvref_t<decltype(lf)>;
                sinks[g] = sink{&a, &on_leaf<Arena, leaf_t>};
            });
        }

        // 库里唯一的输入动词：推进一帧（时间从帧里取）
        //   ① 所有叶子走一步，顺手记下载荷地址
        //   ② 各 cell 结算
        //   ③ 回调 + 记录 + 复位
        template <class Frame>
            requires frame_like<Frame>
        void feed(const Frame &f)
        {
            assert(!in_feed_ && "feed 不可重入：handler 里不要再 feed");
            in_feed_ = true;
            const time_point now = frame_traits<std::remove_cvref_t<Frame>>::when(f);

            // 只 step **认识这个帧** 的识别器：一次 feed 进入的是"帧流"，
            // 不是"所有识别器"。不认识的既不 step、也不 on()，它的槽位保持原样。
            std::array<const void *, leaf_count_v<Tree>> payloads{};
            std::array<bool, cell_count_v<Tree>> touched{};
            [[maybe_unused]] bool any_stepped = false;
            walk_leaves<0>(tree, [&](auto idx, auto &lf) {
                constexpr std::size_t g = decltype(idx)::value;
                constexpr int c = cell_of_kind<Tree>(g);
                if constexpr (requires { lf.step(f, now); })
                {
                    std::get<static_cast<std::size_t>(c)>(stages).template on<g>(
                        lf.step(f, now), now);
                    touched[static_cast<std::size_t>(c)] = true;
                    any_stepped = true;
                    payloads[g] = lf.payload();
                }
            });

            auto kill = [&](std::size_t g) {
                reset_kind(tree, g);
            };
            auto record = [&](std::size_t g, verdict v, time_point b, time_point e) {
                const event ev{plan_kinds_v<Tree>[g].name, v, now, b, e};
                if (v == verdict::accepted)
                    sinks[g](payloads[g]); // ← 唯一的胜利回调
                if (n_events < trace.size())
                    trace[n_events++] = ev; // 硬边界，不靠断言
                reset_kind(tree, g);
            };
            // 本帧必须有识别器认领——否则是"喂错帧类型"这类静默失效，直接断言掉
            assert(any_stepped && "没有任何识别器认领这个帧类型：检查 Frame");

            // 只结算"本帧被喂过"的格子；没被喂的格子完全不动（同帧流多格也各自独立）
            (
                [&] {
                    if (touched[Cs])
                        std::get<Cs>(stages).settle(record, kill, now);
                }(),
                ...);
            in_feed_ = false;
        }

        // 只过了时间：让所有识别器看一眼时钟，再结算。
        //   · FSM 想处理超时 ⇒ 实现 tick(now)
        //   · 兜底的 hold 也在 settle 里检查
        //   于是"没有帧就不推进"这件事被解掉了：卡住的假设会被兜底清掉。
        void tick(time_point now)
        {
            assert(!in_feed_ && "tick 不可重入：handler 里不要再 tick/feed");
            in_feed_ = true;

            std::array<const void *, leaf_count_v<Tree>> payloads{};
            walk_leaves<0>(tree, [&](auto idx, auto &lf) {
                constexpr std::size_t g = decltype(idx)::value;
                constexpr int c = cell_of_kind<Tree>(g);
                std::get<static_cast<std::size_t>(c)>(stages).template on<g>(lf.tick(now),
                                                                             now);
                payloads[g] = lf.payload();
            });

            auto kill = [&](std::size_t g) {
                reset_kind(tree, g);
            };
            auto record = [&](std::size_t g, verdict v, time_point b, time_point e) {
                const event ev{plan_kinds_v<Tree>[g].name, v, now, b, e};
                if (v == verdict::accepted)
                    sinks[g](payloads[g]);
                if (n_events < trace.size())
                    trace[n_events++] = ev;
                reset_kind(tree, g);
            };
            (std::get<Cs>(stages).settle(record, kill, now), ...);
            in_feed_ = false;
        }

        // 观测：这一路走过的事件（观察用，不是输入）
        [[nodiscard]] std::span<const event> events() const noexcept
        {
            return {trace.data(), n_events};
        }
        void clear_events() noexcept
        {
            n_events = 0;
        }

        static void reset_kind(Tree &t, std::size_t g) noexcept
        {
            walk_leaves<0>(t, [&](auto idx, auto &lf) {
                if (decltype(idx)::value == g)
                    lf.reset();
            });
        }
    };

    template <class Tree>
    struct ui : ui_impl<Tree, std::make_index_sequence<cell_count_v<Tree>>>
    {
        static constexpr auto blueprint = []() consteval {
            return static_string{
                std::define_static_string(detail::blueprint(Tree::plan()))};
        }();
        static constexpr auto kinds = plan_kinds_v<Tree>;
    };

    inline std::string to_string(std::span<const event> tr)
    {
        std::string s;
        for (const auto &e : tr)
        {
            if (!s.empty())
                s += ", ";
            s += e.name.view();
            s += e.v == verdict::accepted ? ":acc" : ":sup";
            s += "@";
            s += std::to_string(
                std::chrono::duration_cast<std::chrono::milliseconds>(e.at - time_point{})
                    .count());
        }
        return s;
    }

    // 把编译期算出来的结构全部打出来（因为全是编译期确定，所以全可打印）
    namespace detail
    {
        consteval void append_num(std::string &s, std::size_t v)
        {
            char buf[24]{};
            const auto r = std::to_chars(buf, buf + sizeof(buf), v);
            s.append(buf, static_cast<std::size_t>(r.ptr - buf));
        }
    } // namespace detail

    template <class Tree>
    consteval static_string describe()
    {
        std::string s = detail::blueprint(Tree::plan());
        const auto cells = detail::cells_of(Tree::plan());
        s += "\nslots:";
        for (std::size_t c = 0; c < cells.size(); ++c)
        {
            s += "\n  cell[";
            detail::append_num(s, c);
            s += "] ";
            s += cells[c].source.view();
            s += " :";
            for (int m : cells[c].members)
            {
                s += ' ';
                s += plan_kinds_v<Tree>[m].name.view();
            }
        }
        s += "\nstages = ";
        detail::append_num(s, cell_count_v<Tree>);
        s += ", sinks = ";
        detail::append_num(s, leaf_count_v<Tree>);
        s += ", sizeof(ui) = ";
        detail::append_num(s, sizeof(ui<Tree>));
        s += "B";
        return static_string{std::define_static_string(s)};
    }

    // ═══════════════════════════════════════════════════════════════════
    // 多指：N 条独立通道
    //
    //   每一指是**一台独立的机器**：自己的识别器状态、自己的槽位表、自己的记录。
    //   指针之间从不互相裁决 —— 两根手指不是"谁赢"，是"各算各的"。
    //   所以这里不需要把实例 id 塞进 stage/leaf：一台机器就是一份状态。
    //
    //   帧只要告诉 pool 它属于哪一指：
    //     template <> struct uhi::frame_traits<touch_frame> {
    //         static uhi::time_point when(const touch_frame &f) noexcept;
    //         static std::size_t      who(const touch_frame &f) noexcept;  // 可选
    //     };
    //   没有 who 的帧一律进 0 号通道（鼠标就这样用）。
    // ═══════════════════════════════════════════════════════════════════
    template <class F>
    concept frame_with_lane = requires(const F &f) {
        {
            frame_traits<std::remove_cvref_t<F>>::who(f)
        } noexcept -> std::convertible_to<std::size_t>;
    };

    template <class Tree, std::size_t Lanes>
    struct pool
    {
        static constexpr std::size_t lanes_count = Lanes;
        std::array<ui<Tree>, Lanes> lanes{};

        template <class Arena>
        void bind(Arena &a) noexcept
        {
            for (auto &l : lanes)
                l.bind(a);
        }

        template <class Frame>
            requires frame_like<Frame>
        void feed(const Frame &f)
        {
            std::size_t k = 0;
            if constexpr (frame_with_lane<Frame>)
                k = frame_traits<std::remove_cvref_t<Frame>>::who(f);
            assert(k < Lanes && "指针 id 超出 pool 大小");
            lanes[k < Lanes ? k : 0].feed(f);
        }

        // 时间对**所有**通道一起走：一根手指卡住不该拖住别人
        void tick(time_point now) noexcept
        {
            for (auto &l : lanes)
                l.tick(now);
        }

        [[nodiscard]] ui<Tree> &operator[](std::size_t k) noexcept
        {
            return lanes[k];
        }
        [[nodiscard]] const ui<Tree> &operator[](std::size_t k) const noexcept
        {
            return lanes[k];
        }
    };
} // namespace uhi
