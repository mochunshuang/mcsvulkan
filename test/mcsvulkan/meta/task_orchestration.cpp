#include "head.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <format>
#include <functional>
#include <iostream>
#include <exception>
#include <optional>
#include <print>
#include <set>
#include <span>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

using mcs::vulkan::meta::make_aggregate;
using bind_member = mcs::vulkan::meta::bind_member;
using mcs::vulkan::meta::static_string;
using mcs::vulkan::meta::static_nsdms_of;
using mcs::vulkan::meta::static_parameters_of;

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
constexpr void invoke_aggregate_ranges(auto &object, auto &&...args)
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

struct task_spec
{
    std::meta::info spec;
    static_string name;
};
using dispatch_location = int;
using tasks_type = std::vector<static_string>;
using tasks_view_type = std::span<const static_string>;

template <typename Scheduling>
    requires(requires(Scheduling scheduling, tasks_view_type scheduled,
                      tasks_view_type candidates) { scheduling(scheduled, candidates); })
struct scheduling_task
{
    consteval scheduling_task(task_spec spec, Scheduling scheduling)
        : spec_{spec}, scheduling_{std::move(scheduling)}
    {
    }
    constexpr task_spec spec() noexcept
    {
        return spec_;
    }
    // dispatch 也使用 tasks_view_type，与内部调用一致
    constexpr dispatch_location dispatch(tasks_view_type scheduled,
                                         tasks_view_type candidates)
    {
        return scheduling_(scheduled, candidates);
    }
    task_spec spec_;
    Scheduling scheduling_;
};
template <typename Task>
concept schedulable =
    requires(Task task, tasks_view_type scheduled, tasks_view_type candidates) {
        { task.spec() } -> std::same_as<task_spec>;
        { task.dispatch(scheduled, candidates) } -> std::same_as<dispatch_location>;
    };
template <schedulable... Task>
consteval auto buildSchedule(std::vector<task_spec> init_scheduled, Task... task)
{
    constexpr auto N = sizeof...(Task);
    auto tasks = std::make_tuple(task...);

    {
        std::vector<static_string> all_name{task.spec().name...};
        for (auto init_spec : init_scheduled)
        {
            all_name.push_back(init_spec.name);
        }
        for (std::size_t i = 0; i < all_name.size(); ++i)
        {
            auto a = all_name[i];
            for (std::size_t j = i + 1; j < all_name.size(); ++j)
            {
                auto b = all_name[j];
                if (a == b)
                    throw std::meta::exception{"buildSchedule requires unique task name",
                                               std::meta::current_function()};
            }
        }
    }

    std::array<bool, sizeof...(Task)> scheduled_flags{};
    auto try_dispatch = [&]<size_t I>() {
        if (scheduled_flags[I])
            return false;

        auto &cur = std::get<I>(tasks);

        tasks_type scheduled =
            init_scheduled |
            std::views::transform([](task_spec item) { return item.name; }) |
            std::ranges::to<std::vector>();
        tasks_type candidates;
        template for (constexpr auto J : std::views::indices(N))
        {
            if (J != I && scheduled_flags[J] == false)
            {
                candidates.push_back(std::get<J>(tasks).spec().name);
            }
        }

        auto insert_index = cur.dispatch(std::span{scheduled}, std::span{candidates});
        if (insert_index != -1)
        {
            init_scheduled.insert(init_scheduled.begin() + insert_index, cur.spec());
            scheduled_flags[I] = true;
            return true;
        }
        return false;
    };

    while (true)
    {
        bool any_progress = false;
        template for (constexpr auto I : std::views::indices(N))
        {
            any_progress |= try_dispatch.template operator()<I>();
        }

        if (!any_progress)
        {
            if (std::all_of(scheduled_flags.begin(), scheduled_flags.end(),
                            [](bool b) { return b; }))
                break;
            throw std::meta::exception{"Can not buildSchedule.",
                                       std::meta::current_function()};
        }
    }
    return init_scheduled;
}

// consteval auto build_task_graph(std::vector<task_spec> task_spec)
// {
//     struct task_graph;
//     consteval
//     {
//         std::vector<std::meta::info> nsdms;
//         template for (const auto &spec : task_spec)
//         {
//             nsdms.push_back(
//                 std::meta::data_member_spec(spec.spec, {.name = spec.name.view()}));
//         }
//     }

//     return task_graph{};
// }

template <typename T>
struct static_span
{
    T *data_;          // 成员变量
    std::size_t size_; // 成员变量

    constexpr static_span(const std::span<T> &s) noexcept
        : data_(s.data()), size_(s.size())
    {
    }

    // 迭代器支持
    constexpr T *begin() const noexcept
    {
        return data_;
    }
    constexpr T *end() const noexcept
    {
        return data_ + size_;
    }

    // 访问器（与成员变量区分）
    constexpr T *data() const noexcept
    {
        return data_;
    }
    constexpr std::size_t size() const noexcept
    {
        return size_;
    }

    constexpr T &operator[](std::size_t i) const
    {
        return data_[i];
    }

    // 可选：其他接口
    constexpr bool empty() const noexcept
    {
        return size_ == 0;
    }
    constexpr T &front() const
    {
        return data_[0];
    }
    constexpr T &back() const
    {
        return data_[size_ - 1];
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

template <static_string prefix, auto span_spec>
consteval auto build_task_graph()
{
    struct task_graph;
    consteval
    {
        std::vector<std::meta::info> nsdms;
        int i = 0;
        for (const task_spec &spec : span_spec)
        {
            nsdms.push_back(std::meta::data_member_spec(
                spec.spec, {.name = make_name(prefix.view(), i++)}));
        }
        std::meta::define_aggregate(^^task_graph, nsdms);
    }
    return task_graph{};
}

struct world_type
{
};
struct input_type
{
};

struct system_fixed_functions
{
    static void fixed_0(auto &...)
    {
        std::println("system_fixed_functions::fixed_0 call...");
    }
    static void fixed_1(auto &...)
    {
        std::println("system_fixed_functions::fixed_1 call...");
    }
    static void fixed_2(auto &...)
    {
        std::println("system_fixed_functions::fixed_2 call...");
    }
    static void fixed_3(auto &...)
    {
        std::println("system_fixed_functions::fixed_3 call...");
    }
};

struct data_change_function
{
    static auto fun_0(auto &...)
    {
        std::println("data_change_function::fixed_0 call...");
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

void test()
{
    //NOTE: function_ref  无法处于常量表达式
    struct MyClass
    {
        int get()
        {
            return value;
        }
        int value = 42;
    };

    MyClass obj;
    // 绑定 const 成员函数，签名必须带 const
    std::function_ref<int()> ref(std::constant_wrapper<&MyClass::get>{}, obj);
    std::cout << ref() << '\n'; // 42
    {
        // std::function_ref<int()> ref(&MyClass::get, obj); //NOTE: 编译错误
        // std::cout << ref() << '\n'; // 42
    }
}
void test_task()
{
    constexpr bool test_ok = []() consteval -> bool {
        auto complex_schedule = buildSchedule(
            {},

            // A - alpha
            scheduling_task{task_spec{.spec = ^^decltype([](auto &w, auto &i) {
                                          test_systems::alpha(w, i);
                                      }),
                                      .name = "alpha"},
                            [](tasks_view_type, tasks_view_type) constexpr
                                -> dispatch_location { return 0; }},

            // B - beta (依赖 alpha)
            scheduling_task{task_spec{.spec = ^^decltype([](auto &w, auto &i) {
                                          test_systems::beta(w, i);
                                      }),
                                      .name = "beta"},
                            [](tasks_view_type scheduled,
                               tasks_view_type) constexpr -> dispatch_location {
                                auto it =
                                    std::ranges::find(scheduled, static_string{"alpha"});
                                if (it == scheduled.end())
                                    return -1;
                                return static_cast<int>(it - scheduled.begin()) + 1;
                            }},

            // C - gamma (依赖 alpha)
            scheduling_task{task_spec{.spec = ^^decltype([](auto &w, auto &i) {
                                          test_systems::gamma(w, i);
                                      }),
                                      .name = "gamma"},
                            [](tasks_view_type scheduled,
                               tasks_view_type) constexpr -> dispatch_location {
                                auto it =
                                    std::ranges::find(scheduled, static_string{"alpha"});
                                if (it == scheduled.end())
                                    return -1;
                                return static_cast<int>(it - scheduled.begin()) + 1;
                            }},

            // D - delta (依赖 beta 和 gamma)
            scheduling_task{task_spec{.spec = ^^decltype([](auto &w, auto &i) {
                                          test_systems::delta(w, i);
                                      }),
                                      .name = "delta"},
                            [](tasks_view_type scheduled,
                               tasks_view_type) constexpr -> dispatch_location {
                                auto itB =
                                    std::ranges::find(scheduled, static_string{"beta"});
                                auto itC =
                                    std::ranges::find(scheduled, static_string{"gamma"});
                                if (itB == scheduled.end() || itC == scheduled.end())
                                    return -1;
                                auto last = std::max(itB, itC);
                                return static_cast<int>(last - scheduled.begin()) + 1;
                            }},

            // E - epsilon (依赖 alpha 和 delta)
            scheduling_task{task_spec{.spec = ^^decltype([](auto &w, auto &i) {
                                          test_systems::epsilon(w, i);
                                      }),
                                      .name = "epsilon"},
                            [](tasks_view_type scheduled,
                               tasks_view_type) constexpr -> dispatch_location {
                                auto itA =
                                    std::ranges::find(scheduled, static_string{"alpha"});
                                auto itD =
                                    std::ranges::find(scheduled, static_string{"delta"});
                                if (itA == scheduled.end() || itD == scheduled.end())
                                    return -1;
                                auto last = std::max(itA, itD);
                                return static_cast<int>(last - scheduled.begin()) + 1;
                            }});

        if (complex_schedule.size() != 5)
            return false;
        if (complex_schedule[0].name != "alpha")
            return false;

        auto pos_delta =
            std::ranges::find_if(complex_schedule,
                                 [](const task_spec &t) { return t.name == "delta"; }) -
            complex_schedule.begin();
        auto pos_epsilon =
            std::ranges::find_if(complex_schedule,
                                 [](const task_spec &t) { return t.name == "epsilon"; }) -
            complex_schedule.begin();

        if (pos_delta <= 0)
            return false;
        if (pos_epsilon <= pos_delta)
            return false;

        return true;
    }();

    static_assert(test_ok, "Complex DAG schedule test failed");
}
// NOLINTEND
int main()
try
{
    test_task();
    world_type world;
    input_type input;

    // static_assert 调用处
    static_assert(
        buildSchedule({task_spec{.spec = ^^decltype([](auto &world, auto &input) {
                                     return system_fixed_functions::fixed_0(world, input);
                                 }),
                                 .name = "system_fixed_functions::fixed_0"}},
                      scheduling_task{
                          task_spec{.spec = ^^decltype([](auto &world, auto &input) {
                                        return data_change_function::fun_0(world, input);
                                    }),
                                    .name = "data_change_function::fun_0"},
                          [](tasks_view_type scheduled,
                             tasks_view_type candidates) constexpr -> dispatch_location {
                              return 0;
                          }})
            .size() == 2);

    {
        constexpr auto result = std::define_static_array(buildSchedule(
            {task_spec{.spec = ^^decltype([](auto &world, auto &input) {
                           return system_fixed_functions::fixed_0(world, input);
                       }),
                       .name = "system_fixed_functions::fixed_0"}},
            scheduling_task{
                task_spec{.spec = ^^decltype([](auto &world, auto &input) {
                              return data_change_function::fun_0(world, input);
                          }),
                          .name = "data_change_function::fun_0"},
                [](tasks_view_type scheduled, tasks_view_type candidates) constexpr
                    -> dispatch_location { return 0; }}));
        constexpr auto span = static_span{result};
        constexpr auto graph = build_task_graph<"m_", span>();

        invoke_aggregate_ranges<"m_", 0, 100>(graph, world, input);
    }

    // 真正的循环依赖：A 需要 B，B 需要 C，C 需要 A
    constexpr bool cycle_test = []() consteval -> bool {
        try
        {
            auto s = buildSchedule(
                {},
                scheduling_task{
                    task_spec{.spec = ^^decltype([](auto &, auto &) {}), .name = "A"},
                    [](tasks_view_type scheduled,
                       tasks_view_type) constexpr -> dispatch_location {
                        // A 只能在 B 已调度后放置
                        auto it = std::ranges::find(scheduled, static_string{"B"});
                        if (it != scheduled.end())
                            return static_cast<int>(it - scheduled.begin()) + 1;
                        return -1;
                    }},
                scheduling_task{
                    task_spec{.spec = ^^decltype([](auto &, auto &) {}), .name = "B"},
                    [](tasks_view_type scheduled,
                       tasks_view_type) constexpr -> dispatch_location {
                        auto it = std::ranges::find(scheduled, static_string{"C"});
                        if (it != scheduled.end())
                            return static_cast<int>(it - scheduled.begin()) + 1;
                        return -1;
                    }},
                scheduling_task{
                    task_spec{.spec = ^^decltype([](auto &, auto &) {}), .name = "C"},
                    [](tasks_view_type scheduled,
                       tasks_view_type) constexpr -> dispatch_location {
                        auto it = std::ranges::find(scheduled, static_string{"A"});
                        if (it != scheduled.end())
                            return static_cast<int>(it - scheduled.begin()) + 1;
                        return -1;
                    }});
            return false; // 未抛出异常，失败
        }
        catch (const std::meta::exception &)
        {
            return true; // 预期异常，成功
        }
    }();
    static_assert(cycle_test, "buildSchedule should detect cycles");

    // 测试名称重复
    constexpr bool dup_name_test = []() consteval -> bool {
        try
        {
            auto s = buildSchedule(
                {},
                scheduling_task{
                    task_spec{.spec = ^^decltype([](auto &, auto &) {}), .name = "X"},
                    [](tasks_view_type, tasks_view_type) constexpr -> dispatch_location {
                        return 0;
                    }},
                scheduling_task{
                    task_spec{.spec = ^^decltype([](auto &, auto &) {}), .name = "X"},
                    [](tasks_view_type, tasks_view_type) constexpr -> dispatch_location {
                        return 0;
                    }});
            return false;
        }
        catch (const std::meta::exception &)
        {
            return true;
        }
    }();
    static_assert(dup_name_test, "buildSchedule should detect duplicate names");

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