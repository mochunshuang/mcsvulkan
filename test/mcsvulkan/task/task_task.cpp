#include "head.hpp"

#include <iostream>
#include <print>

using mcs::vulkan::meta::static_string;
using mcs::vulkan::task::invoke_aggregate_ranges;
using mcs::vulkan::task::make_task;
using mcs::vulkan::task::init_task;
using mcs::vulkan::task::schedulable_task;
using mcs::vulkan::task::task_info;

// NOLINTBEGIN

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

struct world_type
{
};
struct input_type
{
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
        [] {
            return schedulable_task{
                .task = {.name = "data_fun",
                         .function = ^^decltype([](auto &world, auto &input) {
                             data_change_function::fun_0(world, input);
                         })},
                .fixed = schedulable_task::fixed_position{"fixed_0", -1}};
        },
        // 候选 1
        [] {
            return schedulable_task{
                .task = {.name = "alpha",
                         .function = ^^decltype([](auto &world, auto &input) {
                             test_systems::alpha(world, input);
                         })},
                .befores = {"fixed_1"}};
        },
        // 候选 2
        [] {
            return schedulable_task{
                .task = {.name = "beta",
                         .function = ^^decltype([](auto &world, auto &input) {
                             test_systems::beta(world, input);
                         })},
                .afters = {"fixed_2"},
            };
        },
        // 候选 3
        [] {
            return schedulable_task{
                .task = {.name = "gamma",
                         .function = ^^decltype([](auto &world, auto &input) {
                             test_systems::gamma(world, input);
                         })},
                .fixed = schedulable_task::fixed_position{"fixed_1", 1}};
        },
        // 候选 3.5
        [] {
            return schedulable_task{
                .task = {.name = "middle_task",
                         .function = ^^decltype([](auto &world, auto &input) {
                             test_systems::delta(world, input);
                         })},
                .befores = {"fixed_1"},
                .afters = {"fixed_2"},
                .fixed = {}};
        },
        // 候选 4
        [] {
            return schedulable_task{
                .task = {.name = "delta",
                         .function = ^^decltype([](auto &world, auto &input) {
                             test_systems::delta(world, input);
                         })},
                .afters = {"fixed_0"}};
        },
        // 候选 5
        [] {
            return schedulable_task{
                .task = {.name = "epsilon",
                         .function = ^^decltype([](auto &world, auto &input) {
                             test_systems::epsilon(world, input);
                         })},
                .befores = {"gamma"}};
        }>{};

    if (print)
    {
        std::cout << task.task_sequence_string() << '\n';
        std::cout << task.task_sequence_string_detail() << '\n';

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