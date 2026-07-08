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
template <static_string prefix, size_t min, size_t max>
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
    template for (constexpr auto I : std::views::indices(members.size()))
    {

        constexpr auto member_name = [&] {
            if constexpr (requires() { T::template get_member_name<I>().view(); })
                return T::template get_member_name<I>().view();
            else
                return std::meta::identifier_of(members[I]);
        }();
        constexpr auto prefix_name = prefix.view();
        if constexpr (member_name.starts_with(prefix_name))
        {
            constexpr auto num_str = member_name.substr(prefix_name.size());
            constexpr int num = parse_int(num_str);
            if constexpr (num >= min && num <= max)
                object.[:members[I]:](std::forward<decltype(args)>(args)...);
        }
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
    std::meta::info function;
    static_string name;
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
template <static_string prefix, typename init_spec, gen_schedulable_task auto... genTask>
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

        // ------------------ 5. 按计算出的位置稳定排序 ------------------
        auto order = std::views::indices(N) | std::ranges::to<std::vector>();
        // 当 dist 相同时，先出现在模板参数中的候选任务会排在后出现的候选任务之前。
        std::ranges::stable_sort(order, {}, [&](size_t i) { return dist[i]; });

        std::vector<task_info> result;
        for (size_t idx : order)
            result.push_back(all_tasks[idx]);
        return result;
    }

    template <static_string prefix, auto init_task, gen_schedulable_task auto... genTask>
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

            // 辅助：安全整型 → string_view（零硬编码）
            auto int_to_sv = [](int value) constexpr {
                std::array<char, 12> buf{}; // int 最多 “-2147483648” 11 字符
                auto [ptr, ec] =
                    std::to_chars(buf.data(), buf.data() + buf.size(), value);
                return std::string_view(buf.data(), ptr - buf.data());
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
                                    result += "{after: [";
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
                                    result += "{before: [";
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
            return static_string{std::define_static_string(result)};
        }
        struct type;
        consteval
        {
            std::vector<std::meta::info> nsdms;
            int i = 0;
            for (const task_info &spec : task_sequence)
            {
                nsdms.push_back(std::meta::data_member_spec(
                    spec.function, {.name = make_name(prefix.view(), i++)}));
            }
            std::meta::define_aggregate(^^type, nsdms);
        }
    };
}; // namespace detail
template <static_string prefix, task_info... task, gen_schedulable_task auto... genTask>
struct make_task<prefix, init_task<task...>, genTask...>
    : detail::gen_task<prefix, std::array<task_info, sizeof...(task)>{task...},
                       genTask...>::type
{
    using gen_type =
        detail::gen_task<prefix, std::array<task_info, sizeof...(task)>{task...},
                         genTask...>;
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
};

constexpr auto test_make_task(bool print = false)
{

    constexpr auto task =
        make_task<"m_",
                  init_task<task_info{
                      .function = ^^decltype([](auto &world, auto &input) constexpr {
                          return system_fixed_functions::fixed_0(world, input);
                      }),
                      .name = "system_fixed_functions::fixed_0"}>,
                  [] constexpr {
                      return schedulable_task{
                          .task =
                              {
                                  .function = ^^decltype([](auto &world,
                                                            auto &input) constexpr {
                                      return data_change_function::fun_0(world, input);
                                  }),
                                  .name = "data_change_function::fun_0",
                              },
                          .befores = {"system_fixed_functions::fixed_0"}};
                  }>{};
    if (print)
    {
        task.print_task_sequence();
        task.print_task_sequence_detail();
    }

    return true;
}
static_assert(test_make_task());

//------------------------------------------------------------------

// 测试最简单的 before 关系
consteval bool test_simple_before()
{
    constexpr auto task = make_task<
        "t_",
        init_task<task_info{.function = ^^decltype([](auto &, auto &) {}), .name = "A"}>,
        [] constexpr {
            return schedulable_task{
                .task = {.function = ^^decltype([](auto &, auto &) {}), .name = "B"},
                .befores = {"A"}};
        }>{};
    // 期望顺序：A -> B
    return decltype(task)::gen_type::task_sequence_string().view() == "A -> B";
}

consteval bool test_simple_after()
{
    constexpr auto task = make_task<
        "t_",
        init_task<task_info{.function = ^^decltype([](auto &, auto &) {}), .name = "X"}>,
        [] constexpr {
            return schedulable_task{
                .task = {.function = ^^decltype([](auto &, auto &) {}), .name = "Y"},
                .afters = {"X"}}; // Y → X，即 Y 在 X 之前
        }>{};
    return decltype(task)::gen_type::task_sequence_string().view() == "Y -> X";
}
static_assert(test_simple_after());

consteval bool test_multiple_candidates()
{
    constexpr auto task = make_task<
        "t_",
        init_task<task_info{.function = ^^decltype([](auto &, auto &) {}), .name = "A"}>,
        [] constexpr {
            return schedulable_task{
                .task = {.function = ^^decltype([](auto &, auto &) {}), .name = "B"},
                .befores = {"A"}}; // A → B
        },
        [] constexpr {
            return schedulable_task{
                .task = {.function = ^^decltype([](auto &, auto &) {}), .name = "C"},
                .afters = {"A"}}; // C → A
        }>{};
    // 边：C → A, A → B  ⇒  最终顺序 C -> A -> B
    return decltype(task)::gen_type::task_sequence_string().view() == "C -> A -> B";
}
static_assert(test_multiple_candidates());

consteval bool test_fixed_offset_positive()
{
    constexpr auto task = make_task<
        "t_",
        init_task<task_info{.function = ^^decltype([](auto &, auto &) {}), .name = "R"}>,
        [] constexpr {
            return schedulable_task{
                .task = {.function = ^^decltype([](auto &, auto &) {}), .name = "S"},
                .fixed = schedulable_task::fixed_position{"R", 1}};
        }>{};
    return decltype(task)::gen_type::task_sequence_string().view() == "R -> S";
}
static_assert(test_fixed_offset_positive());

consteval bool test_fixed_negative_offset()
{
    constexpr auto task =
        make_task<"t_",
                  init_task<task_info{.function = ^^decltype([](auto &, auto &) {}),
                                      .name = "Anchor"}>,
                  [] constexpr {
                      return schedulable_task{
                          .task = {.function = ^^decltype([](auto &, auto &) {}),
                                   .name = "Before"},
                          .fixed = schedulable_task::fixed_position{"Anchor", -1}};
                  }>{};
    return decltype(task)::gen_type::task_sequence_string().view() == "Before -> Anchor";
}
static_assert(test_fixed_negative_offset());

consteval bool test_complex_mix()
{
    constexpr auto task = make_task<
        "t_",
        init_task<task_info{.function = ^^decltype([](auto &, auto &) {}),
                            .name = "Base"}>,
        // Fixed +1: S 紧随 Base 之后
        [] constexpr {
            return schedulable_task{
                .task = {.function = ^^decltype([](auto &, auto &) {}), .name = "S"},
                .fixed = schedulable_task::fixed_position{"Base", 1}};
        },
        // Fixed -1: T 紧随 Base 之前
        [] constexpr {
            return schedulable_task{
                .task = {.function = ^^decltype([](auto &, auto &) {}), .name = "T"},
                .fixed = schedulable_task::fixed_position{"Base", -1}};
        },
        // U 必须在 S 之后 (S -> U)
        [] constexpr {
            return schedulable_task{
                .task = {.function = ^^decltype([](auto &, auto &) {}), .name = "U"},
                .befores = {"S"}};
        },
        // V 必须在 Base 之前 (V -> Base)
        [] constexpr {
            return schedulable_task{
                .task = {.function = ^^decltype([](auto &, auto &) {}), .name = "V"},
                .afters = {"Base"}};
        },
        // W 必须在 T 之前 (W -> T)
        [] constexpr {
            return schedulable_task{
                .task = {.function = ^^decltype([](auto &, auto &) {}), .name = "W"},
                .afters = {"T"}};
        }>{};
    // 已知固定: T(-1) -> Base(0) -> S(1)
    // U 在 S 后: S -> U
    // V 在 Base 前: V -> Base
    // W 在 T 前: W -> T
    // 整体链: W -> T -> Base -> S -> U，V -> Base，所以 V 可放在 W 前、W 与 T 之间、T 与 Base 之间
    auto sv = decltype(task)::gen_type::task_sequence_string().view();
    // 检查必要顺序
    return sv.find("T") < sv.find("Base") && sv.find("Base") < sv.find("S") &&
           sv.find("S") < sv.find("U") && sv.find("V") < sv.find("Base") &&
           sv.find("W") < sv.find("T");
}
static_assert(test_complex_mix());

consteval bool test_same_dist_order()
{
    // B 和 C 都在 A 之前 (B->A, C->A)，二者 dist 相等，按模板参数顺序 B 应在 C 前
    constexpr auto task = make_task<
        "t_",
        init_task<task_info{.function = ^^decltype([](auto &, auto &) {}), .name = "A"}>,
        [] constexpr {
            return schedulable_task{
                .task = {.function = ^^decltype([](auto &, auto &) {}), .name = "B"},
                .afters = {"A"}}; // B -> A
        },
        [] constexpr {
            return schedulable_task{
                .task = {.function = ^^decltype([](auto &, auto &) {}), .name = "C"},
                .afters = {"A"}}; // C -> A
        }>{};
    return decltype(task)::gen_type::task_sequence_string().view() == "B -> C -> A";
}
static_assert(test_same_dist_order());
consteval bool test_unique_fixed_sequence()
{
    // init: A -> B (gap=1)
    // C fixed {A, 1} => C = A+1
    // D fixed {B, 1} => D = B+1
    // 解：A=0, B=1, C=1, D=2   -> 相同dist时按参数顺序 B(1) 先于 C(1)
    constexpr auto task = make_task<
        "t_",
        init_task<task_info{.function = ^^decltype([](auto &, auto &) {}), .name = "A"},
                  task_info{.function = ^^decltype([](auto &, auto &) {}), .name = "B"}>,
        [] constexpr {
            return schedulable_task{
                .task = {.function = ^^decltype([](auto &, auto &) {}), .name = "C"},
                .fixed = schedulable_task::fixed_position{"A", 1}};
        },
        [] constexpr {
            return schedulable_task{
                .task = {.function = ^^decltype([](auto &, auto &) {}), .name = "D"},
                .fixed = schedulable_task::fixed_position{"B", 1}};
        }>{};
    return decltype(task)::gen_type::task_sequence_string().view() == "A -> B -> C -> D";
}
static_assert(test_unique_fixed_sequence());

// 复杂调度综合测试，使用全部预定义系统函数
constexpr bool test_complex_orchestration(bool print = false)
{
    constexpr auto task = make_task<
        "sys_",
        init_task<task_info{.function = ^^decltype([](auto &world, auto &input) {
                                system_fixed_functions::fixed_0(world, input);
                            }),
                            .name = "fixed_0"},
                  task_info{.function = ^^decltype([](auto &world, auto &input) {
                                system_fixed_functions::fixed_1(world, input);
                            }),
                            .name = "fixed_1"},
                  task_info{.function = ^^decltype([](auto &world, auto &input) {
                                system_fixed_functions::fixed_2(world, input);
                            }),
                            .name = "fixed_2"}>,
        // 候选 0：数据变化函数，固定在 fixed_0 之前一位
        [] constexpr {
            return schedulable_task{
                .task = {.function = ^^decltype([](auto &world, auto &input) {
                             data_change_function::fun_0(world, input);
                         }),
                         .name = "data_fun"},
                .fixed = schedulable_task::fixed_position{"fixed_0", -1}};
        },
        // 候选 1：alpha 必须在 fixed_1 之后
        [] constexpr {
            return schedulable_task{
                .task = {.function = ^^decltype([](auto &world, auto &input) {
                             test_systems::alpha(world, input);
                         }),
                         .name = "alpha"},
                .befores = {"fixed_1"}}; // fixed_1 -> alpha
        },
        // 候选 2：beta 必须在 fixed_2 之前
        [] constexpr {
            return schedulable_task{
                .task = {.function = ^^decltype([](auto &world, auto &input) {
                             test_systems::beta(world, input);
                         }),
                         .name = "beta"},
                .afters = {"fixed_2"}}; // beta -> fixed_2
        },
        // 候选 3：gamma 固定在 fixed_1 之后一位
        [] constexpr {
            return schedulable_task{
                .task = {.function = ^^decltype([](auto &world, auto &input) {
                             test_systems::gamma(world, input);
                         }),
                         .name = "gamma"},
                .fixed = schedulable_task::fixed_position{"fixed_1", 1}};
        },
        // 候选 4：delta 必须在 fixed_0 之前
        [] constexpr {
            return schedulable_task{
                .task = {.function = ^^decltype([](auto &world, auto &input) {
                             test_systems::delta(world, input);
                         }),
                         .name = "delta"},
                .afters = {"fixed_0"}}; // delta -> fixed_0
        },
        // 候选 5：epsilon 必须在 gamma 之后
        [] constexpr {
            return schedulable_task{
                .task = {.function = ^^decltype([](auto &world, auto &input) {
                             test_systems::epsilon(world, input);
                         }),
                         .name = "epsilon"},
                .befores = {"gamma"}}; // gamma -> epsilon
        }>{};

    if (print)
    {
        task.print_task_sequence();
        task.print_task_sequence_detail();
    }

    /*
当前 all_name 的构建顺序是：

先放入 init 序列中的任务：fixed_0 → fixed_1 → fixed_2

再按模板参数书写顺序放入候选任务：data_fun → alpha → beta → gamma → delta → epsilon

因此 fixed_2 在 all_name 中的索引为 2，gamma 的索引为 6。当 dist 相同时（均为 3），stable_sort 会保持这个原始顺序，
所以 fixed_2 排在 gamma 之前，从而出现了 fixed_2 -> alpha -> gamma 这一段，gamma 没有直接跟在 fixed_1 后面。
*/
    // 验证唯一序列：
    //beta 和 delta 在同为 dist=0 的情况下，书写顺序上 beta 先于 delta
    return decltype(task)::gen_type::task_sequence_string().view() ==
           "data_fun -> beta -> delta -> fixed_0 -> fixed_1 -> fixed_2 -> alpha "
           "-> gamma -> epsilon";
}
static_assert(test_complex_orchestration()); // 编译期自检

// NOLINTEND
int main()
try
{
    test_make_task(true);
    test_complex_orchestration(true);
    world_type world;
    input_type input;

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