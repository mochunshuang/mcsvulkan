// ═══════════════════════════════════════════════════════════════════
//  uhi.cpp —— 最终版：ui<Tree> 多 stage，按帧类型分发
//  编译：g++ -std=c++26 -freflection -O2 uhi.cpp
// ═══════════════════════════════════════════════════════════════════
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <meta>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "../../../include/detail/__meta.hpp"

// NOLINTBEGIN
namespace uhi
{

    using mcs::vulkan::meta::static_string;
    using time_point = std::int64_t;

    // ═══════════════════════════════════════════════════════════════════
    // 基础
    // ═══════════════════════════════════════════════════════════════════

    enum class step_t : std::uint8_t
    {
        idle,
        active,
        done,
        aborted,
        hold
    };

    struct verdict
    {
        std::string_view name;
        bool accepted;
        time_point at;
        const void *payload;
    };

    template <class T>
    consteval static_string type_name()
    {
        return static_string{
            std::define_static_string(std::string{std::meta::display_string_of(^^T)})};
    }

    // ═══════════════════════════════════════════════════════════════════
    // plan 数据
    // ═══════════════════════════════════════════════════════════════════

    struct kind_desc
    {
        static_string fn_name;
        static_string frame;
        std::int64_t hold_ns = 0;
    };

    struct plan_data
    {
        std::vector<kind_desc> kinds;
        std::vector<std::pair<int, int>> awaits;
        std::vector<std::pair<int, int>> excl;
    };

    // ═══════════════════════════════════════════════════════════════════
    // 组合子
    // ═══════════════════════════════════════════════════════════════════

    template <class Fsm, static_string Name>
    struct with
    {
        using fsm_type = Fsm;
        static constexpr auto name = Name;
        static consteval plan_data plan()
        {
            plan_data p;
            p.kinds.push_back({Name, type_name<typename Fsm::frame_type>(), 0});
            return p;
        }
    };

    // concat_shift 就是"把 B 拼到 A 后面，B 的所有编号往后挪"
    consteval plan_data concat_shift(plan_data a, const plan_data &b, int base)
    {
        // ① 把 b 的 kind 名字接在 a 后面
        //    拼接前：a.kinds = [a0, a1]
        //    拼接后：a.kinds = [a0, a1, b0, b1]  （假设 b 有 2 个）
        a.kinds.append_range(b.kinds);

        // ② 把 b 的 awaits 边接进来，但编号要 +base
        //    因为 b 的 plan() 独立生成时，它的编号是 0,1,2...
        //    拼到 a 后面后，b 的第 0 个 kind 现在排在整个 p 的第 base 位
        //    所以 (i, j) 要变成 (i+base, j+base)
        for (auto [i, j] : b.awaits)
            a.awaits.push_back({i + base, j + base});

        // ③ excl 同理
        for (auto [i, j] : b.excl)
            a.excl.push_back({i + base, j + base});

        return a;
    }

    template <class... Rs>
    struct all_of
    {
        static consteval plan_data plan()
        {
            // ① 收集所有子树的独立 plan（每个子树的编号都从 0 开始）
            std::vector<plan_data> parts;
            (parts.push_back(Rs::plan()), ...);

            // ② 按声明顺序首尾相接，每次拼一个新的子树就整体偏移
            plan_data p;
            int base = 0; // 当前子树拼进 p 后的"起始编号"
            for (auto &pt : parts)
            {
                p = concat_shift(p, pt, base);             // pt 的所有编号 += base
                base += static_cast<int>(pt.kinds.size()); // 下一个子树的起始位置
            }
            return p;
            // all_of 的语义：不加任何边 → 交叉块全空
        }
    };

    // 拼接 + 交叉块全填 excl
    template <class... Rs>
    struct one_of
    {
        static consteval plan_data plan()
        {
            constexpr int K = sizeof...(Rs); // 子树数量

            // ① 收集所有子树的独立 plan
            std::vector<plan_data> parts;
            (parts.push_back(Rs::plan()), ...);

            // ② 算每个子树在"全局坐标系"里的范围 [off[i], off[i+1])
            //    off[0]   = 0
            //    off[1]   = 子树 0 的 kind 数
            //    off[2]   = 子树 0 + 子树 1 的 kind 数
            //    ...
            //    off[K]   = 全部 kind 总数
            std::array<int, K + 1> off{};
            for (int i = 0; i < K; ++i)
                off[i + 1] = off[i] + static_cast<int>(parts[i].kinds.size());

            // ③ 拼接：和 all_of 完全一样
            plan_data p;
            int base = 0;
            for (auto &pt : parts)
            {
                p = concat_shift(p, pt, base);
                base += static_cast<int>(pt.kinds.size());
            }

            // ④ 交叉块填 excl：任意两个不同子树的所有 kind 两两互斥
            //    a, b 遍历"子树对"（a < b，避免重复）
            //    x 遍历子树 a 的 kind 编号
            //    y 遍历子树 b 的 kind 编号
            //    加边 (x, y)：x 和 y 互斥
            for (int a = 0; a < K; ++a)
                for (int b = a + 1; b < K; ++b)
                    for (int x = off[a]; x < off[a + 1]; ++x)
                        for (int y = off[b]; y < off[b + 1]; ++y)
                            p.excl.push_back({x, y});
            return p;
        }
    };

    // 拼接 + A×B 填 awaits
    template <class A, class B>
    struct refines
    {
        static consteval plan_data plan()
        {
            // ① 分别算 A 和 B 的独立 plan
            auto pa = A::plan();
            auto pb = B::plan();

            // ② A 的 kind 数 = B 在全局坐标系里的起始编号
            //    拼接后：A 占 [0, na)，B 占 [na, na+nb)
            int na = static_cast<int>(pa.kinds.size());

            // ③ 拼接：B 的所有编号 += na
            auto p = concat_shift(pa, pb, na);

            // ④ 交叉块填 awaits：A 的每个 kind 等 B 的每个 kind 失败
            //    i 遍历 A 的 kind（0 到 na-1）
            //    j 遍历 B 的 kind（na 到总长度-1）
            //    加边 (i, j)：i 等 j 失败
            for (int i = 0; i < na; ++i)
                for (int j = na; j < static_cast<int>(p.kinds.size()); ++j)
                    p.awaits.push_back({i, j});
            return p;
        }
    };

    // ═══════════════════════════════════════════════════════════════════
    // validate —— 表生成前的五项硬检查
    //
    // 全部编译期执行。任何一项失败 → throw → consteval 求值失败 → 编译失败。
    // 用户拿不到一张有问题的表。
    //
    // 五项检查的语义：
    //   ① 名唯一       ：同一个语义事件只能有一个生产者（on_xxx 名字不重复）
    //   ② 无自环       ：awaits/excl 都不能指向自己（i == j）
    //   ③ 同帧流       ：关系两端必须属于同一帧流（不同帧流不会同帧竞争）
    //   ④ 关系不重叠   ：同一对 (i,j) 不能既 awaits 又 excl（语义矛盾）
    //   ⑤ 无环         ：awaits 必须是 DAG（环则永远等不到答案）
    // ═══════════════════════════════════════════════════════════════════
    consteval void validate(const plan_data &p)
    {
        int n = static_cast<int>(p.kinds.size());

        // ─────────────────────────────────────────────────────────────
        // ① 名唯一
        //    两层 for 只扫 i<j，避免重复比较 (i,j) 和 (j,i)。
        //    name 是 static_string，用 view() 拿 string_view 比较。
        // ─────────────────────────────────────────────────────────────
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j)
                if (p.kinds[i].fn_name.view() == p.kinds[j].fn_name.view())
                    throw "dup name";

        // ─────────────────────────────────────────────────────────────
        // ② awaitss 的自环 + 跨帧流
        //    - 自环：i 等 i 失败 = 永远等不到自己失败 = 死锁
        //    - 跨帧流：i 和 j 在不同 feed 里被推进，永远不在同帧竞争，
        //             这条 awaits 边永远不触发，是死边（作者写错了）
        // ─────────────────────────────────────────────────────────────
        for (auto [i, j] : p.awaits)
        {
            if (i == j)
                throw "awaits self-loop";
            if (p.kinds[i].frame.view() != p.kinds[j].frame.view())
                throw "awaits cross-frame";
        }

        // ─────────────────────────────────────────────────────────────
        // ③ excl 的自环 + 跨帧流
        //    同理。excl 的自环 = i 和 i 互斥 = i 永远不能赢。
        // ─────────────────────────────────────────────────────────────
        for (auto [i, j] : p.excl)
        {
            if (i == j)
                throw "excl self-loop";
            if (p.kinds[i].frame.view() != p.kinds[j].frame.view())
                throw "excl cross-frame";
        }

        // ─────────────────────────────────────────────────────────────
        // ④ awaits × excl 不重叠
        //    同一对 (i,j) 不能既是"i 等 j 失败"又是"i 和 j 互斥"。
        //    语义上矛盾：
        //      - awaits(i,j)：j 活着时 i 不能动（i 必须等 j 失败）
        //      - excl(i,j)  ：i 和 j 谁先完成谁赢，直接杀对方
        //    两条规则给出相反结论，裁决器无法执行。
        //
        //    注意检查了对称对 (i,j) 和 (j,i)：
        //      - excl 是对称的，excl 清单只存 (i,j) 一份，但语义上 (j,i) 也成立
        //      - 所以 (i,j) ∈ awaits 且 (j,i) ∈ excl 也是冲突
        // ─────────────────────────────────────────────────────────────
        for (auto [i, j] : p.awaits)
            for (auto [x, y] : p.excl)
                if ((i == x && j == y) || (i == y && j == x))
                    throw "refines x excl conflict";

        // ─────────────────────────────────────────────────────────────
        // ⑤ awaits 无环（Kahn 算法变体）
        //
        //    indeg[i] = i 等待多少个 kind 失败
        //             = awaits 清单里"起点是 i"的边数
        //
        //    ⚠ 这是"反入度"：indeg=0 表示"不等任何"= 最具体的 kind。
        //
        //    算法：
        //      每轮扫一遍，找到 indeg==0 且未处理的 i：
        //        1. 标记 done
        //        2. 把它从所有"等 i 的 j"的 indeg 里减掉
        //           （因为 i 已经被处理了，那些等 i 的 j 少等一个）
        //      一轮没有任何进展 → 说明有环 → throw
        //
        //    为什么这是"反拓扑序"？
        //      正常的 Kahn：入度为 0 的先出 → 拓扑序
        //      这里 indeg 是"等待数"（出度的反义）：等待数 0 的先出 → 反拓扑序
        //      等待数 0 = 等得最少 = 最具体 = 应该最先赢
        //      ────────────────────────────────────────────────────
        //      click(等 2 个 dbl,triple) → indeg=2
        //      dbl(等 1 个 triple)       → indeg=1
        //      triple(不等任何)          → indeg=0  ← 第一个被"处理"
        //      ────────────────────────────────────────────────────
        // ─────────────────────────────────────────────────────────────
        std::vector<int> indeg(n, 0);
        std::vector<bool> done(n, false);

        // 计算每个 kind 等待的数量
        for (auto [i, j] : p.awaits)
            ++indeg[i]; // i 在等 j → i 的等待数 +1

        int left = n; // 还有多少个 kind 未处理

        while (left > 0)
        {
            bool prog = false; // 本轮是否处理了至少一个

            for (int i = 0; i < n; ++i)
                if (!done[i] && indeg[i] == 0)
                {
                    // i 不等任何活着的东西（或已经全死），可以处理
                    done[i] = true;
                    --left;
                    prog = true;

                    // i 被处理了，所以"等 i 的 j"少等一个
                    for (auto [x, y] : p.awaits)
                        if (y == i) // x 等 i
                            --indeg[x];
                }

            if (!prog)
                // 一轮下来没有任何 indeg==0 的节点 → 剩下的节点互相等待 → 有环
                throw "refines cycle";
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // cell_data —— 一个 cell 的完整数据
    //
    // cell = 同帧流内由 awaits/excl 连通的 kind 集合，
    //        是运行期裁决的最小单位（一个 cell 一个 stage）。
    //
    // 字段分三组：
    //   ① 标识 + 内容   ：frame、kinds
    //   ② 索引映射       ：table_to_decl、decl_to_table
    //   ③ 关系边（局部） ：awaits_local、excl_local
    //
    // 编号系统（重要，容易混）：
    //   - "声明序 / global"：组合树遍历顺序，0..N_global-1，跨所有 cell
    //   - "表序 / local"   ：本 cell 内反拓扑序，0..N_cell-1，仅本 cell 内有效
    //
    //   本结构的 kind 数组、关系边，全部用**表序（local）**索引。
    //   table_to_decl / decl_to_table 负责两套编号之间的转换。
    // ═══════════════════════════════════════════════════════════════════
    struct cell_data
    {
        // ─────────────────────────────────────────────────────────────
        // ① 标识 + 内容
        // ─────────────────────────────────────────────────────────────

        // 本 cell 的帧流类型名（如 "mouse_frame" / "kb_frame"）。
        // 用途：
        //   · 运行期 ui::feed 按"帧类型"找对应 cell（type_name<Frame>() == frame）
        //   · validate 检查关系两端是否同帧流
        // 同 cell 内所有 kind 的 frame 必然相同（build_cells 合并条件①保证）。
        static_string frame;

        // 本 cell 的所有 kind，**按表序（local）排列**，即反拓扑序：
        //   kinds[0] 等得最少（最具体，最先结算）
        //   kinds[N-1] 等得最多（最抽象，最后结算）
        // 这是 stage::settle 的扫描顺序——越具体越先尝试赢。
        std::vector<kind_desc> kinds;

        // ─────────────────────────────────────────────────────────────
        // ② 索引映射（两套编号之间的翻译）
        // ─────────────────────────────────────────────────────────────

        // 表序 → 声明序。
        //   table_to_decl[t] = "表序第 t 位" 的 kind 在全局声明里的编号。
        // 长度 = N_cell。
        // 用途：
        //   · stage::payload_of：拿到 local 索引后，反查 FSMs 元组里的 decl 位置
        //   · stage::win / reset_at：杀掉 local j 时，需要 reset 对应的 decl FSM
        std::vector<int> table_to_decl;

        // 声明序 → 表序。
        //   decl_to_table[g] = "声明序第 g 位" 的 kind 在本 cell 表序里的位置，
        //                      若 g 不属于本 cell 则为 -1。
        // 长度 = N_global（整个 plan 的 kind 总数）。
        // 用途：
        //   · build_cells 内：把全局边 (gi, gj) 转成局部边 (li, lj)
        //   · validate 之后：检查某条边是否跨 cell
        //
        // 注意长度是 N_global 而不是 N_cell——因为要支持"任意全局编号 → 本 cell"
        // 的反查。-1 表示"不在本 cell"，是合法的哨兵值。
        std::vector<int> decl_to_table;

        // ─────────────────────────────────────────────────────────────
        // ③ 关系边（局部 / 表序编号）
        // ─────────────────────────────────────────────────────────────

        // awaits 边，**已做传递闭包**，用表序编号。
        //   (i, j) ∈ awaits_local ⇒ "表序第 i 位等第 j 位失败"
        //   ⇒ j 活着时 i 不能赢（blocked）
        //   ⇒ i 赢了则 j 必须 superseded（win 的第二条杀逻辑）
        //
        // 为什么闭包：
        //   若只有直接边 (click→dbl), (dbl→triple)，
        //   缺 (click→triple) 时，triple 活着时 click 不会等它，
        //   triple 完成时 click 可能已经先落地了。
        //   闭包补出 click→triple，保证"最具体的没说话之前谁都不许落地"。
        //
        // 索引方向：行 = 谁在等（i），列 = 等谁（j）。
        // 反拓扑序保证：所有 i > j（1 都在对角线以上），
        // 因为"等得多的排在后面"，等得多的必然行号更大。
        std::vector<std::pair<int, int>> awaits_local;

        // excl 边，用表序编号，**每条只存一份**（li < lj）。
        //   (i, j) ∈ excl_local ⇒ "表序第 i 位和第 j 位互斥"
        //   ⇒ 谁先赢谁杀对方（win 的第一条杀逻辑）
        //
        // 为什么只存一份：
        //   语义上互斥是对称的，(i,j) 和 (j,i) 等价。
        //   只存 li < lj 那一份，生成矩阵时双向填充：
        //     excl_matrix[i] |= 1 << j;
        //     excl_matrix[j] |= 1 << i;
        //   这样清单不冗余，矩阵仍对称。
        //
        // 为什么不存 (li > lj) 那一份：
        //   存两份则语义重复，validate 时不好查重；
        //   固定方向（小在前）让清单天然规范。
        std::vector<std::pair<int, int>> excl_local;
    };

    // ═══════════════════════════════════════════════════════════════════
    // build_cells —— 把 plan_data 切成一组 cell_data
    //
    // 输入：plan_data（全局声明序的 kinds + awaits/excl 边列表）
    // 输出：vector<cell_data>（每个 cell 是一组"同帧流 + 约束连通"的 kind）
    //
    // 核心思想：
    //   不同帧流的 kind 在不同 feed 调用里被推进，物理上不可能同帧竞争；
    //   同帧流但无约束关系的 kind 也不需要放在一起（互不干扰）。
    //   所以 cell = 同帧流内由 awaits/excl 连通的 kind 集合。
    //
    // 每个 cell 内部要做四件事：
    //   ① 把全局边投影成局部边（编号从全局转成 cell 内局部）
    //   ② 对 awaits 做传递闭包（补出 a→b→c 推出的 a→c）
    //   ③ 按反拓扑序（等得多的先）重排 kind
    //   ④ 生成 awaits_local / excl_local 两份"按新序号"的边清单
    //
    // 每个 cell 的输出：
    //   - kinds           : 本 cell 的 kind，按反拓扑序排
    //   - table_to_decl   : 本 cell 的 local 索引 → 全局声明索引
    //   - decl_to_table   : 全局声明索引 → 本 cell 的 local 索引（-1 表示不在本 cell）
    //   - awaits_local    : 闭包后的 awaits，用 local 索引
    //   - excl_local      : excl，用 local 索引，只存一份（li < lj）
    // ═══════════════════════════════════════════════════════════════════
    consteval std::vector<cell_data> build_cells(const plan_data &p)
    {
        int n = static_cast<int>(p.kinds.size());

        // ─────────────────────────────────────────────────────────────
        // 阶段 1：并查集分组
        //
        // 合并条件（必须同时满足）：
        //   ① 同帧流       （不同帧流的 kind 不同帧被推进，不应同 cell）
        //   ② 有约束关系   （awaits 或 excl 任一条边存在）
        //
        // 注意：仅"同帧流"不够。两个同帧流但完全独立的 kind（比如 hover
        // 和 wheel）不应该放在同一个 cell 里，因为它们从不竞争。
        //
        // 遍历 i < j 避免重复检查同一对两次；awaits/excl 的边可能以 (i,j)
        // 或 (j,i) 形式存在，两个方向都要查。
        // ─────────────────────────────────────────────────────────────
        std::vector<int> uf(n);
        for (int i = 0; i < n; ++i)
            uf[i] = i;
        auto find = [&](int x) {
            while (uf[x] != x)
                x = uf[x] = uf[uf[x]]; // 路径压缩
            return x;
        };
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j)
            {
                // ① 帧流不同 → 不可能同帧竞争，跳过
                if (p.kinds[i].frame.view() != p.kinds[j].frame.view())
                    continue;

                // ② 检查是否有任何约束关系把 i 和 j 连起来
                bool conn = false;
                for (auto [a, b] : p.awaits)
                    if ((a == i && b == j) || (a == j && b == i))
                    {
                        conn = true;
                        break;
                    }
                if (!conn)
                    for (auto [a, b] : p.excl)
                        if ((a == i && b == j) || (a == j && b == i))
                        {
                            conn = true;
                            break;
                        }

                // ③ 两个条件都满足 → 合并
                if (conn)
                    uf[find(i)] = find(j);
            }

        // ─────────────────────────────────────────────────────────────
        // 阶段 2：按根节点收集所有组
        //
        // group_of_root[r] = "根 r 对应第几个 group"（-1 表示还没有）
        // groups[k]       = 第 k 个 group 的所有全局 kind 编号
        //
        // 不用 unordered_map：kind 数很少（≤15），线性映射更快且编译期安全
        // ─────────────────────────────────────────────────────────────
        std::vector<std::vector<int>> groups;
        std::vector<int> group_of_root(n, -1);
        for (int i = 0; i < n; ++i)
        {
            int r = find(i);
            if (group_of_root[r] < 0)
            {
                // 该根第一次出现 → 新建一个 group
                group_of_root[r] = static_cast<int>(groups.size());
                groups.push_back({});
            }
            groups[group_of_root[r]].push_back(i);
        }

        // ─────────────────────────────────────────────────────────────
        // 阶段 3：逐 cell 处理
        // ─────────────────────────────────────────────────────────────
        std::vector<cell_data> result;
        for (auto &g : groups)
        {
            int N = static_cast<int>(g.size());
            cell_data c;

            // 同 cell 内所有 kind 必然同帧流（阶段 1 的合并条件①保证），
            // 随便取一个作代表即可
            c.frame = p.kinds[g[0]].frame;

            // ───────────────────────────────────────────────────────────
            // 3.1 建局部 awaits 邻接矩阵
            //
            // 输入：全局的 p.awaits（编号是全局声明序）
            // 输出：adj[N][N]，编号是本 cell 内的局部序 g[0..N-1]
            //
            // g[k] 是"本 cell 第 k 个 kind 的全局编号"。
            // 对每条全局边 (i, j)，找到它在 g 里的位置 li, lj。
            //
            // 只需线性扫描 N 次，因为 N ≤ 15，整个矩阵 O(N³) 都无所谓。
            // ───────────────────────────────────────────────────────────
            std::vector<std::vector<bool>> adj(N, std::vector<bool>(N, false));
            for (auto [i, j] : p.awaits)
            {
                int li = -1, lj = -1;
                for (int k = 0; k < N; ++k)
                {
                    if (g[k] == i)
                        li = k;
                    if (g[k] == j)
                        lj = k;
                }
                // 两端都在本 cell 里才记录（validate 已保证不会有跨 cell 的边，
                // 但防御性写在这里，万一有也不会崩）
                if (li >= 0 && lj >= 0)
                    adj[li][lj] = true;
            }

            // ───────────────────────────────────────────────────────────
            // 3.2 传递闭包（Floyd-Warshall）
            //
            // 输入是"直接边"，闭包后是"可达边"。
            // 例：awaits = [(click,dbl), (dbl,triple)]
            //    闭包后 = [(click,dbl), (dbl,triple), (click,triple)]
            //
            // 为什么必须闭包：
            //   click 等 dbl 失败，dbl 等 triple 失败，
            //   所以 triple 还活着时 click 也应该等——这就是"click 等 triple"。
            //   如果不补这条边，triple 活着时 click 不会被告知要等，
            //   可能提前 accepted，然后 triple 完成时已经晚了。
            // ───────────────────────────────────────────────────────────
            for (int k = 0; k < N; ++k)
                for (int i = 0; i < N; ++i)
                    for (int j = 0; j < N; ++j)
                        if (adj[i][k] && adj[k][j])
                            adj[i][j] = true;

            // ───────────────────────────────────────────────────────────
            // 3.3 反拓扑序（Kahn 算法变体）
            //
            // ⚠ indeg 的语义是"反入度"：
            //    indeg[i] = adj[i] 中有多少个 1 = "i 等多少个 kind 失败"
            //    indeg=0 → 不等任何 → 最具体 → 应该最先出
            //
            // 与正常拓扑序对比：
            //    正常 Kahn：indeg = 入度（有多少指向我），0 先出
            //    这里    ：indeg = 出度（我指向多少），0 先出
            //    数学上等价于"对反图做拓扑排序"
            //
            // 输出：order[t] = "拓扑序第 t 位的 kind 在 g 里的原始位置"
            //       也就是说 order 是 g 的一个置换，重排后的顺序就是反拓扑序
            //
            // 例（click→dbl→triple）：
            //   indeg[click]=2, indeg[dbl]=1, indeg[triple]=0
            //   第一个出 triple，然后 dbl，然后 click
            //   order = [triple 的 local 位置, dbl 的 local 位置, click 的 local 位置]
            //
            // 注意：validate 已保证无环，所以 while 一定能终止。
            // 万一有环（不该发生），这里会死循环，编译期报"step limit"。
            // ───────────────────────────────────────────────────────────
            std::vector<int> indeg(N, 0);
            for (int i = 0; i < N; ++i)
                for (int j = 0; j < N; ++j)
                    if (adj[i][j])
                        ++indeg[i];

            std::vector<int> order;
            while (static_cast<int>(order.size()) < N)
                for (int i = 0; i < N; ++i)
                    if (indeg[i] == 0)
                    {
                        indeg[i] = -1; // 标记"已处理"（用 -1 而不是删除）
                        order.push_back(i);
                        // i 处理了，所有"等 i 的"少等一个
                        for (int j = 0; j < N; ++j)
                            if (indeg[j] > 0 && adj[j][i])
                                --indeg[j];
                    }

            // ───────────────────────────────────────────────────────────
            // 3.4 按反拓扑序填充 cell_data 的元数据
            //
            // table_to_decl[t] = 拓扑序第 t 位的 kind 的全局声明编号
            // decl_to_table[global] = 全局声明编号 → 本 cell 的 local 编号
            //                         （-1 表示不在本 cell）
            // ───────────────────────────────────────────────────────────
            c.kinds.resize(N);
            c.table_to_decl.resize(N);
            c.decl_to_table.assign(n, -1);
            for (int t = 0; t < N; ++t)
            {
                int local = order[t];  // 在 g 里的位置
                int global = g[local]; // 全局声明编号
                c.kinds[t] = p.kinds[global];
                c.table_to_decl[t] = global;
                c.decl_to_table[global] = t;
            }

            // ───────────────────────────────────────────────────────────
            // 3.5 生成 awaits_local：按新编号重写闭包后的边
            //
            // adj[order[i]][order[j]] = true 表示
            //   "拓扑序第 i 位等拓扑序第 j 位"
            //   这就是新编号下的边 (i, j)
            //
            // 为什么用 order 重新索引：
            //   adj 的索引是 g 里的局部位置（原始顺序）
            //   而 c.kinds 已经按 order 重排
            //   所以要用 adj[order[i]][order[j]] 才对得上新顺序
            // ───────────────────────────────────────────────────────────
            for (int i = 0; i < N; ++i)
                for (int j = 0; j < N; ++j)
                    if (adj[order[i]][order[j]])
                        c.awaits_local.push_back({i, j});

            // ───────────────────────────────────────────────────────────
            // 3.6 生成 excl_local：只保留本 cell 内的边，且 li < lj 去重
            //
            // p.excl 是全局边，每条只存一份（比如 (0, 1)，不存 (1, 0)）
            // 但转换到 local 编号后，(0, 1) 可能变成 (li=1, lj=0)（如果重排换了位置）
            // 用 li < lj 标准化，避免存两次（虽然 p.excl 本身没有两份，
            // 但重排后按 local 编号看，不强制 li < lj 会看起来方向混乱）
            //
            // 两端都在本 cell 里才记录（li >= 0 && lj >= 0）
            // ───────────────────────────────────────────────────────────
            for (auto [gi, gj] : p.excl)
            {
                int li = c.decl_to_table[gi], lj = c.decl_to_table[gj];
                if (li >= 0 && lj >= 0 && li < lj)
                    c.excl_local.push_back({li, lj});
            }

            result.push_back(std::move(c));
        }
        return result;
    }

    // 关键修复：不存 vector 到 constexpr 变量，每次重新算
    template <class Tree>
    consteval std::size_t count_cells()
    {
        return build_cells(Tree::plan()).size();
    }

    template <class Tree>
    inline constexpr std::size_t cell_count = count_cells<Tree>();

    // ═══════════════════════════════════════════════════════════════════
    // cell_table
    // ═══════════════════════════════════════════════════════════════════

    template <class Tree, std::size_t Cell>
    struct cell_table
    {
        static constexpr std::size_t N = []() consteval {
            auto cells = build_cells(Tree::plan());
            return cells[Cell].kinds.size();
        }();

        struct packed_t
        {
            static_string frame{};
            std::array<kind_desc, N> kinds{};
            std::array<std::uint64_t, N> excl{};
            std::array<std::uint64_t, N> awaits_t{};
            std::array<int, N> table_to_decl{};
        };

        static constexpr packed_t data = []() consteval {
            auto cells = build_cells(Tree::plan());
            auto &c = cells[Cell];
            packed_t r{};
            r.frame = c.frame;
            for (std::size_t i = 0; i < N; ++i)
            {
                r.kinds[i] = c.kinds[i];
                r.table_to_decl[i] = c.table_to_decl[i];
            }
            for (auto [i, j] : c.excl_local)
            {
                r.excl[i] |= (1ull << j);
                r.excl[j] |= (1ull << i);
            }
            for (auto [i, j] : c.awaits_local)
                r.awaits_t[i] |= (1ull << j);
            return r;
        }();
    };

    // ═══════════════════════════════════════════════════════════════════
    // fsms_of
    // ═══════════════════════════════════════════════════════════════════

    template <class T>
    struct fsms_of;
    template <class Fsm, static_string Name>
    struct fsms_of<with<Fsm, Name>>
    {
        using type = std::tuple<Fsm>;
    };
    template <class... Rs>
    struct fsms_of<all_of<Rs...>>
    {
        using type =
            decltype(std::tuple_cat(std::declval<typename fsms_of<Rs>::type>()...));
    };
    template <class... Rs>
    struct fsms_of<one_of<Rs...>>
    {
        using type =
            decltype(std::tuple_cat(std::declval<typename fsms_of<Rs>::type>()...));
    };
    template <class A, class B>
    struct fsms_of<refines<A, B>>
    {
        using type = decltype(std::tuple_cat(std::declval<typename fsms_of<A>::type>(),
                                             std::declval<typename fsms_of<B>::type>()));
    };

    // ═══════════════════════════════════════════════════════════════════
    // stage
    // ═══════════════════════════════════════════════════════════════════

    template <class Tree, std::size_t Cell>
    struct stage
    {
        static constexpr auto &T = cell_table<Tree, Cell>::data;
        static constexpr std::size_t N = T.kinds.size();

        std::array<bool, N> alive{};
        std::array<bool, N> deferred{};
        std::array<std::uint8_t, N> want{};

        bool blocked(std::size_t i) const
        {
            for (std::size_t j = 0; j < N; ++j)
                if (j != i && alive[j] && ((T.awaits_t[i] >> j) & 1))
                    return true;
            return false;
        }

        template <class Fsms>
        const void *payload_at(std::size_t t_idx, Fsms &fsms)
        {
            int decl = T.table_to_decl[t_idx];
            const void *r = nullptr;
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                auto try_one = [&]<std::size_t I>() {
                    if (static_cast<int>(I) == decl)
                    {
                        if constexpr (requires { std::get<I>(fsms).payload; })
                            r = static_cast<const void *>(&std::get<I>(fsms).payload);
                    }
                };
                (try_one.template operator()<Is>(), ...);
            }(std::make_index_sequence<std::tuple_size_v<Fsms>>{});
            return r;
        }

        template <std::size_t DeclIdx, class Fsms>
        static void reset_one(Fsms &fsms)
        {
            if constexpr (requires { std::get<DeclIdx>(fsms).reset(); })
                std::get<DeclIdx>(fsms).reset();
        }

        template <class Fsms>
        void reset_at(Fsms &fsms, int decl)
        {
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                ((static_cast<int>(Is) == decl ? reset_one<Is>(fsms) : void()), ...);
            }(std::make_index_sequence<std::tuple_size_v<Fsms>>{});
        }

        template <class Fsms>
        void win(std::size_t i, Fsms &fsms, std::vector<verdict> &out, time_point now)
        {
            for (std::size_t j = 0; j < N; ++j)
                if (j != i && alive[j] &&
                    (((T.excl[i] >> j) & 1) || ((T.awaits_t[j] >> i) & 1)))
                {
                    alive[j] = false;
                    deferred[j] = false;
                    out.push_back(
                        {T.kinds[j].name.view(), false, now, payload_at(j, fsms)});
                    reset_at(fsms, T.table_to_decl[j]);
                }
            alive[i] = false;
            deferred[i] = false;
            out.push_back({T.kinds[i].name.view(), true, now, payload_at(i, fsms)});
            reset_at(fsms, T.table_to_decl[i]);
        }

        template <class Fsms>
        std::vector<verdict> settle(std::array<step_t, N> sigs, Fsms &fsms,
                                    time_point now)
        {
            std::vector<verdict> out;
            for (std::size_t i = 0; i < N; ++i)
            {
                switch (sigs[i])
                {
                case step_t::active:
                    if (!alive[i])
                        alive[i] = true;
                    break;
                case step_t::done:
                    alive[i] = true;
                    want[i] = 1;
                    break;
                case step_t::aborted:
                    if (alive[i])
                        want[i] = 2;
                    break;
                case step_t::hold:
                    break;
                case step_t::idle:
                    if (alive[i] && !deferred[i])
                        want[i] = 2;
                    break;
                }
            }
            for (std::size_t i = 0; i < N; ++i)
                if (want[i] == 2)
                {
                    alive[i] = false;
                    deferred[i] = false;
                    want[i] = 0;
                }

            for (std::size_t round = 0; round < N; ++round)
            {
                bool moved = false;
                for (std::size_t i = 0; i < N; ++i)
                {
                    if (!alive[i])
                        continue;
                    if (want[i] != 1 && !deferred[i])
                        continue;
                    if (blocked(i))
                    {
                        deferred[i] = true;
                        continue;
                    }
                    win(i, fsms, out, now);
                    moved = true;
                }
                for (auto &w : want)
                    w = 0;
                if (!moved)
                    break;
            }
            for (auto &w : want)
                w = 0;
            return out;
        }
    };

    // ═══════════════════════════════════════════════════════════════════
    // ui：多 stage，按帧类型分发
    // ═══════════════════════════════════════════════════════════════════

    template <class Tree, class Seq = std::make_index_sequence<cell_count<Tree>>>
    struct ui_impl;

    template <class Tree, std::size_t... Cs>
    struct ui_impl<Tree, std::index_sequence<Cs...>>
    {
        using Fsms = typename fsms_of<Tree>::type;
        Fsms fsms;
        std::tuple<stage<Tree, Cs>...> stages;

        template <class Frame>
        std::vector<verdict> feed(const Frame &f, time_point now)
        {
            std::vector<verdict> result;
            do_feed<Frame, Cs...>(f, now, result);
            return result;
        }

        std::vector<verdict> tick(time_point now)
        {
            std::vector<verdict> result;
            do_tick<Cs...>(now, result);
            return result;
        }

      private:
        template <class Frame, std::size_t First, std::size_t... Rest>
        void do_feed(const Frame &f, time_point now, std::vector<verdict> &result)
        {
            constexpr auto &T = cell_table<Tree, First>::data;
            if constexpr (type_name<Frame>().view() == T.frame.view())
            {
                constexpr std::size_t N = T.kinds.size();
                std::array<step_t, N> sigs{};
                [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                    ((sigs[Is] =
                          std::get<static_cast<std::size_t>(T.table_to_decl[Is])>(fsms)
                              .step(f, now)),
                     ...);
                }(std::make_index_sequence<N>{});
                result = std::get<First>(stages).settle(sigs, fsms, now);
            }
            else if constexpr (sizeof...(Rest) > 0)
            {
                do_feed<Frame, Rest...>(f, now, result);
            }
        }

        template <std::size_t First, std::size_t... Rest>
        void do_tick(time_point now, std::vector<verdict> &result)
        {
            auto &st = std::get<First>(stages);
            constexpr auto &T = cell_table<Tree, First>::data;
            constexpr std::size_t N = T.kinds.size();
            std::array<step_t, N> sigs{};
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                auto do_one = [&]<std::size_t I>() {
                    auto &fsm =
                        std::get<static_cast<std::size_t>(T.table_to_decl[I])>(fsms);
                    if constexpr (requires { fsm.tick(now); })
                        sigs[I] = fsm.tick(now);
                    else
                        sigs[I] = step_t::hold;
                };
                (do_one.template operator()<Is>(), ...);
            }(std::make_index_sequence<N>{});
            auto vs = st.settle(sigs, fsms, now);
            for (auto &v : vs)
                result.push_back(v);
            if constexpr (sizeof...(Rest) > 0)
                do_tick<Rest...>(now, result);
        }
    };

    template <class Tree>
    struct ui : ui_impl<Tree>
    {
    };

} // namespace uhi

// ═══════════════════════════════════════════════════════════════════
// 用户代码
// ═══════════════════════════════════════════════════════════════════

struct mouse_frame
{
    bool press = false;
    bool release = false;
    double x = 0, y = 0;
};
struct kb_frame
{
    int key = 0;
    bool press = false;
};

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
    uhi::time_point deadline{};

    uhi::step_t step(const mouse_frame &f, uhi::time_point now) noexcept
    {
        if (armed && now >= deadline)
        {
            count = 0;
            armed = false;
            return uhi::step_t::aborted;
        }
        if (f.press)
        {
            ++count;
            payload.x = f.x;
            payload.y = f.y;
            payload.count = count;
            return uhi::step_t::active;
        }
        if (f.release && count > 0)
        {
            if (count >= N)
            {
                count = 0;
                armed = false;
                return uhi::step_t::done;
            }
            deadline = now + 300;
            armed = true;
            return uhi::step_t::active;
        }
        return count > 0 ? uhi::step_t::active : uhi::step_t::idle;
    }
    uhi::step_t tick(uhi::time_point now) noexcept
    {
        if (armed && now >= deadline)
        {
            count = 0;
            armed = false;
            return uhi::step_t::aborted;
        }
        return uhi::step_t::hold;
    }
    void reset() noexcept
    {
        count = 0;
        armed = false;
    }
};

struct key_A_fsm
{
    using frame_type = kb_frame;
    struct payload_t
    {
        int key = 0;
    } payload{};
    uhi::step_t step(const kb_frame &f, uhi::time_point) noexcept
    {
        if (f.press && f.key == 'A')
        {
            payload.key = f.key;
            return uhi::step_t::done;
        }
        return uhi::step_t::idle;
    }
    void reset() noexcept {}
};

using click_fsm = multi_click<1>;
using dbl_fsm = multi_click<2>;
using triple_fsm = multi_click<3>;

using mouse_tree = uhi::refines<
    uhi::refines<uhi::with<click_fsm, "on_click">, uhi::with<dbl_fsm, "on_dbl">>,
    uhi::with<triple_fsm, "on_triple">>;

using my_ui = uhi::all_of<mouse_tree, uhi::with<key_A_fsm, "on_key_A">>;

// ═══════════════════════════════════════════════════════════════════
// main
// ═══════════════════════════════════════════════════════════════════

static void dump(const char *label, const std::vector<uhi::verdict> &vs)
{
    std::printf("  %-20s :", label);
    if (vs.empty())
    {
        std::printf(" (none)\n");
        return;
    }
    for (auto &v : vs)
        std::printf(" %.*s:%s@%lld", (int)v.name.size(), v.name.data(),
                    v.accepted ? "acc" : "sup", (long long)v.at);
    std::printf("\n");
}

int main()
{
    static_assert(uhi::cell_count<my_ui> == 2, "mouse + keyboard = 2 cells");

    using CT0 = uhi::cell_table<my_ui, 0>;
    using CT1 = uhi::cell_table<my_ui, 1>;
    std::printf("=== cell 0 (%.*s) N=%zu ===\n", (int)CT0::data.frame.view().size(),
                CT0::data.frame.view().data(), CT0::N);
    for (std::size_t i = 0; i < CT0::N; ++i)
        std::printf("  [%zu] %-10.*s excl=0x%llx awaits_t=0x%llx\n", i,
                    (int)CT0::data.kinds[i].name.view().size(),
                    CT0::data.kinds[i].name.view().data(),
                    (unsigned long long)CT0::data.excl[i],
                    (unsigned long long)CT0::data.awaits_t[i]);
    std::printf("=== cell 1 (%.*s) N=%zu ===\n", (int)CT1::data.frame.view().size(),
                CT1::data.frame.view().data(), CT1::N);
    for (std::size_t i = 0; i < CT1::N; ++i)
        std::printf("  [%zu] %-10.*s excl=0x%llx awaits_t=0x%llx\n", i,
                    (int)CT1::data.kinds[i].name.view().size(),
                    CT1::data.kinds[i].name.view().data(),
                    (unsigned long long)CT1::data.excl[i],
                    (unsigned long long)CT1::data.awaits_t[i]);

    uhi::ui<my_ui> u;

    std::printf("\n=== S1: 单击 (mouse feed + tick) ===\n");
    dump("mouse press@0", u.feed(mouse_frame{.press = true}, 0));
    dump("mouse release@10", u.feed(mouse_frame{.release = true}, 10));
    dump("tick@400", u.tick(400));

    std::printf("\n=== S2: key_A ===\n");
    dump("kb press@500", u.feed(kb_frame{.key = 'A', .press = true}, 500));

    std::printf("\n=== S3: 三击 ===\n");
    dump("mouse press@1000", u.feed(mouse_frame{.press = true}, 1000));
    dump("mouse release@1010", u.feed(mouse_frame{.release = true}, 1010));
    dump("mouse press@1100", u.feed(mouse_frame{.press = true}, 1100));
    dump("mouse release@1110", u.feed(mouse_frame{.release = true}, 1110));
    dump("mouse press@1200", u.feed(mouse_frame{.press = true}, 1200));
    dump("mouse release@1210", u.feed(mouse_frame{.release = true}, 1210));

    return 0;
}
// NOLINTEND