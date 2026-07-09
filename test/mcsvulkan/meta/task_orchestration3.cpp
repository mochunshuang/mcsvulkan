#include "head.hpp"

#include <algorithm>
#include <array>
#include <bits/utility.h>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <iostream>
#include <exception>
#include <optional>
#include <print>
#include <set>
#include <span>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using mcs::vulkan::meta::static_string;

// NOLINTBEGIN
consteval int parse_int(std::string_view sv)
{
    int result = 0;
    for (char c : sv)
    {
        if (c < '0' || c > '9')
            throw std::meta::exception{"sv is not number", std::meta::current_function()};
        result = result * 10 + (c - '0');
    }
    return result;
}
template <static_string beign, static_string end>
static constexpr void invoke_aggregate_ranges(auto &object, auto &&...args)
{
    using T = std::remove_cvref_t<decltype(object)>;
    constexpr auto members = [] {
        if constexpr (requires() { T::members; })
            return T::members;
        else
            return std::define_static_array(std::meta::nonstatic_data_members_of(
                ^^T, std::meta::access_context::current()));
    }();
    constexpr auto find_index = [](static_string name, const auto &members) consteval {
        for (auto I : std::views::indices(members.size()))
            if (std::meta::identifier_of(members[I]) == name)
                return I;
        auto msg = " field [" + std::string(name.view()) + "] is not find in members";
        throw std::meta::exception{msg, std::meta::current_function()};
    };

    constexpr auto beign_index = find_index(beign, members);
    constexpr auto end_index = find_index(end, members);
    static_assert(beign_index <= end_index, "requires beign_index <= end_index");
    template for (constexpr auto I : std::ranges::views::iota(beign_index, end_index + 1))
    {
        object.[:members[I]:](std::forward<decltype(args)>(args)...);
    }
};

struct system_fixed_functions
{
    constexpr static void fixed_0(auto &...)
    {
        // std::println("system_fixed_functions::fixed_0 call...");
        std::cout << "system_fixed_functions::fixed_0 call...\n";
    }
    constexpr static void fixed_1(auto &...)
    {
        std::println("system_fixed_functions::fixed_1 call...");
    }
    constexpr static void fixed_2(auto &...)
    {
        std::println("system_fixed_functions::fixed_2 call...");
    }
    constexpr static void fixed_3(auto &...)
    {
        std::println("system_fixed_functions::fixed_3 call...");
    }
};

struct data_change_function
{
    constexpr static auto fun_0(auto &...)
    {
        //NOTE: 汇编从 5828 到 150 行。 print太照顾周全了: Unicode 表 + 格式化解析器 + 错误字符串 + 状态机
        // std::println("data_change_function::fixed_0 call...");
        std::cout << "data_change_function::fun_0 call...🐕\n";
    }
};

struct test_systems
{
    static void alpha(auto &...)
    {
        std::println("alpha");
    }
    static void beta(auto &...)
    {
        std::println("beta");
    }
    static void gamma(auto &...)
    {
        std::println("gamma");
    }
    static void delta(auto &...)
    {
        std::println("delta");
    }
    static void epsilon(auto &...)
    {
        std::println("epsilon");
    }
};
consteval std::string make_name(std::string_view prefix, int index)
{
    // 1. 计算数字位数（编译期）
    constexpr auto digits = [](int n) -> int {
        int d = 1;
        while (n >= 10)
        {
            n /= 10;
            ++d;
        }
        return d;
    };

    const int num_digits = (index == 0) ? 1 : digits(index);

    // 2. 创建结果字符串，预留空间（前缀 + 数字）
    std::string result;
    result.resize(prefix.size() + num_digits);

    // 3. 复制前缀
    for (size_t i = 0; i < prefix.size(); ++i)
        result[i] = prefix[i];

    // 4. 将数字写入尾部
    // 使用 std::to_chars（C++23 constexpr）或手动转换
    char buf[20];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), index);
    // 复制数字部分
    size_t pos = prefix.size();
    for (char *p = buf; p != ptr; ++p)
        result[pos++] = *p;

    return result;
}

struct world_type
{
};
struct input_type
{
};

// ------------------------------------------------------------------
struct task_info
{
    static_string name;
    std::meta::info function;
};

struct schedulable_task
{
    // NOTE: 必须先安排 anchor。才能使用。和anchor的位置是强制绑定的
    struct fixed_position
    {
        static_string anchor;
        int position;
    };

    task_info task;
    std::vector<static_string> befores;
    std::vector<static_string> afters;
    std::optional<fixed_position> fixed;
};
template <typename Gen>
concept gen_schedulable_task = requires(Gen gen) {
    { gen() } -> std::same_as<schedulable_task>;
};

template <task_info...>
struct init_task;
template <typename init_spec, gen_schedulable_task auto... genTask>
struct make_task;

namespace detail
{
    /*
    befores、afters、init 顺序：只需要 一条边（方向为“先→后”，gap = 1），因为它们只要求“至少差 1”，即 pos[后] - pos[先] >= 1
    fixed_position：必须生成 两条边，因为它是等式 pos[task] = pos[anchor] + offset。要强制相等，必须同时提供下限和上限：
        下限：anchor → task，gap = offset（若 offset 为负，则方向需翻转使 gap 为正，但更简单的是直接允许 gap 为负，统一用 pos[to] - pos[from] >= gap 的格式）
        上限：task → anchor，gap = -offset（等价于 pos[anchor] - pos[task] >= -offset）
    */
    struct Edge
    {
        static_string from;
        static_string to;
        int gap;
        constexpr bool operator==(const Edge &) const = default;
    };
    static consteval size_t find_index(const std::vector<static_string> &vec,
                                       static_string name)
    {
        for (size_t i = 0; i < vec.size(); ++i)
            if (vec[i] == name)
                return i;
        throw std::meta::exception{"Name not found", std::meta::current_function()};
    }

    // ===========================================================================
    // 一、什么是“差分约束系统”？
    // ===========================================================================
    // 就是把所有任务之间的顺序要求写成统一的不等式：
    //   pos[后] - pos[前] >= 距离
    // NOTE: 两个 点  + 边权 恰好可以使用有向图论来解决
    //
    // 例如你的任务：
    //   init 顺序： fixed_0 -> fixed_1 -> fixed_2
    //     意思是： pos[fixed_1] - pos[fixed_0] >= 1
    //            pos[fixed_2] - pos[fixed_1] >= 1
    //
    //   data_fun 固定在 fixed_0 之前 1 位：
    //     意思是： pos[data_fun] - pos[fixed_0] = -1
    //     拆成两条不等式：
    //       pos[data_fun] - pos[fixed_0] >= -1    (这条保证不超前太多)
    //       pos[fixed_0] - pos[data_fun] >=  1    (这条保证至少超前1位)
    //
    //   gamma 必须在 fixed_1 之后 (befores)：
    //     意思是： pos[gamma] - pos[fixed_1] >= 1
    //
    // 所有任务的要求最后都变成了一堆“pos[A] - pos[B] >= 某个数字”。
    // 这就是差分约束系统。
    //
    // 我们要求解出一组 pos[任务] 的值，使得所有不等式同时成立。

    // ===========================================================================
    // 二、为什么变成“图”和“最长路”？
    // ===========================================================================
    // 把每个任务看成一个点，每条不等式 pos[后] - pos[前] >= gap
    // 想象成一根从“前”指向“后”的箭头，箭头上写着 gap。
    //
    // 现在我们从某个起点出发（加入一个虚拟点 S，连接所有任务，gap=0），
    // 沿着箭头走，每走一步就把箭头上的数字加起来。
    //
    // 如果要求 pos[后] >= pos[前] + gap，那么 “从起点走到这个任务的最长距离”
    // 正好就是它能取到的最小（最早）时间值。 所以我们求最长路。
    // “最长路”不是指任务最终执行顺序上的“晚”，而是指为了满足所有“至少”约束，必须取的下界中的最大值，而这个最大值恰好就是任务的最早合法时间
    // NOTE: 后 可能由多个 前 走来。 pos[X] = max( pos[A]+2 , pos[B]+1 , pos[C]+3 ，....) 走到 X 有 A,B,C,....多条路径，必须都满足
    //
    // 示例： fixed_0->fixed_1 (gap=1)
    // 如果起点到 fixed_0 的距离是 3，那么走到 fixed_1 至少是 3+1=4。
    // 这正是我们的约束：fixed_1 必须在 fixed_0 之后至少 1 位。

    // ===========================================================================
    // 三、Bellman-Ford 算法（求最长路版本）在干什么？
    // ===========================================================================
    // Bellman-Ford 是一个经典的图算法，原来用于求最短路，但它可以求最长路，
    // 并且能处理负的 gap（比如 data_fun 相对 fixed_0 的 -1 就变负权）。
    // NOTE: Bellman-Ford: 反复对每个节点取 max(自身, 所有前驱的 dist + gap).即：当前节点路径的最长路，就满足所有前驱的约束
    //
    // 它不断尝试“走更远”：对于每条箭头 u->v (gap)，
    // 如果 dist[u] + gap > dist[v]，就把 dist[v] 更新为更大的值。
    // 这个过程叫“松弛”。
    // 因为最长简单路径最多经过 N-1 条边，所以重复 N-1 轮一般就够了。
    //
    // 第 N 轮如果还能更新，说明图中存在一个循环，
    // 沿着这个循环走一圈 gap 总和 > 0，也就是“正环”。

    // ===========================================================================
    // 四、“环”是什么？为什么检测到环就报错？
    // ===========================================================================
    // 环就是从某个点出发，沿着箭头走，最后回到自己的路径。
    // 如果环上所有 gap 加起来 > 0，就是正环。
    //
    // 为什么正环意味着无解？
    // 假设 A -> B (gap=1), B -> C (gap=2), C -> A (gap=1)
    // 这意味着：
    //   pos[B] >= pos[A] + 1
    //   pos[C] >= pos[B] + 2
    //   pos[A] >= pos[C] + 1
    // 三式相加得到 pos[A] >= pos[A] + 4，显然不可能。
    //
    // 所以只要出现正环，调度就一定无法满足，代码抛出异常。

    // ===========================================================================
    // 五、它们之间的联系：一个完整的流水线
    // ===========================================================================
    // 1. 你把任务依赖关系（init, befores, afters, fixed）声明出来。
    // 2. 程序把这些声明自动翻译成统一的差分约束不等式。
    // 3. 用 Bellman-Ford 最长路算法算出每个任务的最早合法位置（dist[]）。
    //    如果发现有正环，直接报编译期错误。
    // 4. 把任务按照 dist 从小到大排序（同 dist 的任务可以并发）。
    // 5. 在同 dist 的任务里，根据 fixed 的正负做二次排序，
    //    让固定任务“贴紧”它的锚点。
    // 6. 最终生成一个确定的任务序列。
    //
    // 整个过程在编译期完成，零运行时开销。
    static consteval auto get_task_sequence(std::vector<task_info> init_sequence,
                                            gen_schedulable_task auto... genTask)
    {
        auto candidates = std::vector<schedulable_task>{schedulable_task{genTask()}...};

        // 1. unique name check
        std::vector<static_string> all_name;
        for (task_info task : init_sequence)
            all_name.push_back(task.name);
        for (schedulable_task task : candidates)
            all_name.push_back(task.task.name);
        for (std::size_t i = 0; i < all_name.size(); ++i)
            for (std::size_t j = i + 1; j < all_name.size(); ++j)
                if (all_name[i] == all_name[j])
                    throw std::meta::exception{"buildSchedule requires unique task name",
                                               std::meta::current_function()};

        auto find_all_dependents = [](const schedulable_task &candidate, auto ranges) {
            std::vector<static_string> dependents;
            if (candidate.fixed.has_value())
                dependents.push_back(candidate.fixed.value().anchor);
            else
            {
                dependents.append_range(candidate.befores);
                dependents.append_range(candidate.afters);
            }
            for (auto dep : dependents)
            {
                bool find = {};
                for (auto name : ranges)
                {
                    if (name == dep)
                    {
                        find = true;
                        break;
                    }
                }
                if (not find)
                    return false; //not find one
            }
            return true;
        };
        // 2. check candidate value
        for (auto candidate : candidates)
        {
            // conflict check
            if (candidate.fixed.has_value() &&
                (not candidate.befores.empty() || not candidate.afters.empty()))
                throw std::meta::exception{
                    "schedulable_task is error: fixed conflict with befores,afters",
                    std::meta::current_function()};

            // do not dependent itself
            auto task_name = candidate.task.name;
            auto is_self = [&](static_string t) {
                return t == task_name;
            };
            if (candidate.fixed.has_value() &&
                candidate.fixed.value().anchor == task_name)
                throw std::meta::exception{"error by anchor itself",
                                           std::meta::current_function()};
            else
            {
                if (auto result = std::ranges::find_if(candidate.befores, is_self);
                    result != candidate.befores.end())
                    throw std::meta::exception{"error by before itself",
                                               std::meta::current_function()};
                if (auto result = std::ranges::find_if(candidate.afters, is_self);
                    result != candidate.afters.end())
                    throw std::meta::exception{"error by after itself",
                                               std::meta::current_function()};
            }
            // all dependent names exist
            if (not find_all_dependents(
                    candidate, all_name | std::views::filter([&is_self](static_string t) {
                                   return not is_self(t);
                               })))
                throw std::meta::exception{"error by dependent task does not exist",
                                           std::meta::current_function()};
        }

        // 3. generate id
        std::vector<Edge> all_edge = [&]() {
            std::vector<Edge> edge;
            auto push_edge_if_not_find = [&](Edge e) {
                if (std::ranges::find(edge, e) == edge.end())
                    edge.push_back(e);
            };
            // init 顺序 T_i 在 T_{i+1} 之前	T_i → T_{i+1}，gap = 1
            for (size_t i = 1; i < init_sequence.size(); ++i)
                push_edge_if_not_find({.from = init_sequence[i - 1].name,
                                       .to = init_sequence[i].name,
                                       .gap = 1});
            // candidates
            for (auto &candidate : candidates)
            {
                auto cur_task = candidate.task.name;
                if (candidate.fixed.has_value())
                {
                    // cur_task 是 B，anchor 是锚点 A
                    auto [anchor, gap] = *(candidate.fixed);
                    push_edge_if_not_find({.from = anchor, .to = cur_task, .gap = gap});
                    push_edge_if_not_find({.from = cur_task, .to = anchor, .gap = -gap});
                }
                else
                {
                    for (auto pre : candidate.befores)
                        push_edge_if_not_find({.from = pre, .to = cur_task, .gap = 1});
                    for (auto post : candidate.afters)
                        push_edge_if_not_find({.from = cur_task, .to = post, .gap = 1});
                }
            }
            return edge;
        }();

        // ------------------ 4. 差分约束求解（最长路） ------------------
        // 4.1 收集所有任务（保持与 all_name 相同顺序）
        std::vector<task_info> all_tasks;
        all_tasks.reserve(init_sequence.size() + candidates.size());
        for (const auto &t : init_sequence)
            all_tasks.push_back(t);
        for (const auto &t : candidates)
            all_tasks.push_back(t.task);

        const size_t N = all_name.size();
        const size_t SRC = N;                                        // 虚拟源点索引
        constexpr int NEG_INF = std::numeric_limits<int>::min() / 2; // 安全负无穷
        std::vector<int> dist(N + 1, NEG_INF);
        dist[SRC] = 0;

        // 4.2 边表 (from, to, weight)
        std::vector<std::tuple<size_t, size_t, int>> edges;
        for (const auto &e : all_edge)
        {
            size_t u = find_index(all_name, e.from);
            size_t v = find_index(all_name, e.to);
            edges.emplace_back(u, v, e.gap);
        }
        // 虚拟源点保证所有节点位置 >= 0
        for (size_t i = 0; i < N; ++i)
            edges.emplace_back(SRC, i, 0);

        // 4.3 Bellman‑Ford 求最长路
        for (size_t step = 0; step <= N; ++step)
        { // 最多松弛 N 轮
            bool updated = false;
            for (const auto &[u, v, w] : edges)
            {
                if (dist[u] != NEG_INF && dist[v] < dist[u] + w)
                {
                    if (step == N)
                    { // 第 N+1 轮仍能松弛 → 存在正环
                        throw std::meta::exception{
                            "Circular dependency or impossible fixed offset",
                            std::meta::current_function()};
                    }
                    dist[v] = dist[u] + w;
                    updated = true;
                }
            }
            if (!updated)
                break;
        }

        // 最终正环确认（理论上 step==N 时已抛出异常，这里是双保险）
        for (const auto &[u, v, w] : edges)
        {
            if (dist[u] != NEG_INF && dist[v] < dist[u] + w)
            {
                throw std::meta::exception{
                    "Circular dependency or impossible fixed offset",
                    std::meta::current_function()};
            }
        }

        // ------------------ 5. 按计算出的位置稳定排序（利用 fixed 偏移进行槽内二次排序）------------------

        // 收集候选任务中 fixed 的信息（init 任务不会有 fixed）
        std::vector<const schedulable_task::fixed_position *> fixed_info(N, nullptr);
        for (const auto &candidate : candidates)
        {
            if (candidate.fixed.has_value())
            {
                size_t idx = find_index(all_name, candidate.task.name);
                fixed_info[idx] = &candidate.fixed.value();
            }
        }

        // 槽内排序键：(类别, 偏移量)
        // 类别 0：正偏移或零偏移（最前）
        // 类别 1：非固定（中间）
        // 类别 2：负偏移（最后）
        // 同一类别内按 offset 升序，保证贴锚点的任务在最边缘
        auto get_slot_key = [&](size_t idx) -> std::pair<int, int> {
            auto *fp = fixed_info[idx];
            if (!fp)
                return {1, 0}; // 非固定任务
            int offset = fp->position;
            if (offset >= 0)
                return {0, offset}; // 正/零偏移 → 最前，offset 小更靠前
            else
                return {2, offset}; // 负偏移 → 最后，offset 小（绝对值大）更靠前
        };

        auto order = std::views::indices(N) | std::ranges::to<std::vector>();
        std::ranges::stable_sort(order, std::less<>{}, [&](size_t i) {
            return std::pair{dist[i], get_slot_key(i)};
        });

        // 固定位置冲突检测（可选，更友好的报错）
        for (size_t k = 1; k < order.size(); ++k)
        {
            size_t prev = order[k - 1], curr = order[k];
            if (fixed_info[prev] && fixed_info[curr] && dist[prev] == dist[curr])
            {
                // 两个固定任务在同槽，且分类相同，进一步检查是否真的是冲突
                // 但差分约束已保证合法性，这里只是双保险
                throw std::meta::exception{
                    std::format("Fixed tasks '{}' and '{}' conflict at same position {}",
                                all_name[prev].view(), all_name[curr].view(), dist[prev]),
                    std::meta::current_function()};
            }
        }

        std::vector<task_info> result;
        for (size_t idx : order)
            result.push_back(all_tasks[idx]);
        return result;
    }

    template <auto init_task, gen_schedulable_task auto... genTask>
    struct gen_task
    {
        static constexpr auto task_sequence = std::define_static_array(
            get_task_sequence(std::ranges::to<std::vector>(init_task), genTask...));
        static consteval auto task_sequence_string()
        {

            std::string result;
            bool first = true;
            for (const auto &spec : task_sequence)
            {
                if (!first)
                    result += " -> ";
                first = false;
                result += spec.name.view();
            }
            return static_string{std::define_static_string(result)};
        }
        static consteval auto task_sequence_detail_string()
        {
            auto cand_tasks = std::array{genTask()...};

            // 辅助：判断一个名字是否属于 init_sequence
            auto is_init = [&](static_string name) constexpr {
                for (const auto &t : init_task)
                    if (t.name == name)
                        return true;
                return false;
            };

            std::string result;
            bool first = true;
            for (const task_info &info : task_sequence)
            {
                if (!first)
                    result += " -> ";
                first = false;

                result += info.name.view();

                if (!is_init(info.name)) // 属于 candidates
                {
                    // 查找对应的 schedulable_task
                    for (const auto &cand : cand_tasks)
                    {
                        if (cand.task.name == info.name)
                        {
                            if (cand.fixed.has_value())
                            {
                                result += "{fixed: ";
                                result += cand.fixed->anchor.view();
                                result += " += ";
                                // 直接转换并附加
                                std::array<char, 12> buf{};
                                auto [ptr, ec] =
                                    std::to_chars(buf.data(), buf.data() + buf.size(),
                                                  cand.fixed->position);
                                result.append(buf.data(), ptr - buf.data());
                                result += "}";
                            }
                            else
                            {
                                if (!cand.befores.empty())
                                {
                                    result += "{befores: [";
                                    for (size_t i = 0; i < cand.befores.size(); ++i)
                                    {
                                        if (i)
                                            result += ", ";
                                        result += cand.befores[i].view();
                                    }
                                    result += "]}";
                                }
                                if (!cand.afters.empty())
                                {
                                    result += "{afters: [";
                                    for (size_t i = 0; i < cand.afters.size(); ++i)
                                    {
                                        if (i)
                                            result += ", ";
                                        result += cand.afters[i].view();
                                    }
                                    result += "]}";
                                }
                            }
                            break;
                        }
                    }
                }
            }
            // ---- 追加原始声明顺序信息 ----
            result += "\n[in sequence: ";

            // 1. init 任务
            bool first_init = true;
            for (const auto &t : init_task)
            {
                if (!first_init)
                    result += " -> ";
                first_init = false;
                result += t.name.view();
            }
            // 2. 候选任务
            for (const auto &cand : cand_tasks)
            {
                if (!first_init)
                    result += " -> ";
                first_init = false;
                result += cand.task.name.view();
            }
            result += "]";

            return static_string{std::define_static_string(result)};
        }
        struct type;
        consteval
        {
            std::vector<std::meta::info> nsdms;
            int i = 0;
            for (const task_info &spec : task_sequence)
            {
                nsdms.push_back(std::meta::data_member_spec(spec.function,
                                                            {.name = spec.name.view()}));
            }
            std::meta::define_aggregate(^^type, nsdms);
        }
    };
}; // namespace detail
template <task_info... task, gen_schedulable_task auto... genTask>
struct make_task<init_task<task...>, genTask...>
    : detail::gen_task<std::array<task_info, sizeof...(task)>{task...}, genTask...>::type
{
    using gen_type =
        detail::gen_task<std::array<task_info, sizeof...(task)>{task...}, genTask...>;
    using base_type = gen_type::type;
    static constexpr auto members =
        std::define_static_array(std::meta::nonstatic_data_members_of(
            ^^base_type, std::meta::access_context::current()));
    static constexpr void print_task_sequence()
    {
        std::cout << gen_type::task_sequence_string() << "\n";
    }
    static constexpr void print_task_sequence_detail()
    {
        std::cout << gen_type::task_sequence_detail_string() << "\n";
    }
    template <size_t I>
    static consteval static_string field_name()
    {
        return gen_type::task_sequence[I].name;
    }
    template <size_t I>
    static consteval auto get_member_name()
    {
        return field_name<I>();
    }
};

//------------------------------------------------------------------

// 复杂调度综合测试，使用全部预定义系统函数
constexpr bool test_complex_orchestration(bool print = false)
{
    constexpr auto task = make_task<
        init_task<
            {.name = "fixed_0", .function = ^^decltype([](auto &world, auto &input) {
                                    system_fixed_functions::fixed_0(world, input);
                                })},
            {.name = "fixed_1", .function = ^^decltype([](auto &world, auto &input) {
                                    system_fixed_functions::fixed_1(world, input);
                                })},
            {.name = "fixed_2", .function = ^^decltype([](auto &world, auto &input) {
                                    system_fixed_functions::fixed_2(world, input);
                                })}>,
        // 候选 0
        [] constexpr {
            return schedulable_task{
                .task = {.name = "data_fun",
                         .function = ^^decltype([](auto &world, auto &input) {
                             data_change_function::fun_0(world, input);
                         })},
                .fixed = schedulable_task::fixed_position{"fixed_0", -1}};
        },
        // 候选 1
        [] constexpr {
            return schedulable_task{
                .task = {.name = "alpha",
                         .function = ^^decltype([](auto &world, auto &input) {
                             test_systems::alpha(world, input);
                         })},
                .befores = {"fixed_1"},
                .afters = {},
                .fixed = std::nullopt};
        },
        // 候选 2
        [] constexpr {
            return schedulable_task{
                .task = {.name = "beta",
                         .function = ^^decltype([](auto &world, auto &input) {
                             test_systems::beta(world, input);
                         })},
                .befores = {},
                .afters = {"fixed_2"},
                .fixed = std::nullopt};
        },
        // 候选 3
        [] constexpr {
            return schedulable_task{
                .task = {.name = "gamma",
                         .function = ^^decltype([](auto &world, auto &input) {
                             test_systems::gamma(world, input);
                         })},
                .fixed = schedulable_task::fixed_position{"fixed_1", 1}};
        },
        // 候选 3.5
        [] constexpr {
            return schedulable_task{
                .task = {.name = "middle_task",
                         .function = ^^decltype([](auto &world, auto &input) {
                             test_systems::delta(world, input);
                         })},
                .befores = {"fixed_1"},
                .afters = {"fixed_2"},
                .fixed = std::nullopt};
        },
        // 候选 4
        [] constexpr {
            return schedulable_task{
                .task = {.name = "delta",
                         .function = ^^decltype([](auto &world, auto &input) {
                             test_systems::delta(world, input);
                         })},
                .befores = {},
                .afters = {"fixed_0"},
                .fixed = std::nullopt};
        },
        // 候选 5
        [] constexpr {
            return schedulable_task{
                .task = {.name = "epsilon",
                         .function = ^^decltype([](auto &world, auto &input) {
                             test_systems::epsilon(world, input);
                         })},
                .befores = {"gamma"},
                .afters = {},
                .fixed = std::nullopt};
        }>{};

    if (print)
    {
        task.print_task_sequence();
        task.print_task_sequence_detail();

        world_type world;
        input_type input;
        invoke_aggregate_ranges<"fixed_0", "epsilon">(task, world, input);
    }

    static_assert(task.get_member_name<0>() == "beta");

    return decltype(task)::gen_type::task_sequence_string().view() ==
           "beta -> delta -> data_fun -> fixed_0 -> fixed_1 -> gamma -> alpha -> "
           "middle_task -> fixed_2 -> epsilon";
}
static_assert(test_complex_orchestration()); // 编译期自检

// NOLINTEND
int main()
try
{
    test_complex_orchestration(true);

    std::cout << "main done\n";
    return 0;
}
catch (const std::exception &e)
{
    std::cerr << "Exception: " << e.what() << '\n';
    return 1;
}
catch (...)
{
    std::cerr << "Unknown exception\n";
    return 1;
}