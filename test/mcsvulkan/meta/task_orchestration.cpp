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

template <const auto &object, static_string prefix, size_t min, size_t max>
static constexpr void invoke_aggregate_ranges2(auto &&...args)
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
    using VariantType = std::variant<Task...>; // 不加 monostate

    auto candidatesTasks = std::vector<VariantType>{
        VariantType{std::move(task)}... // 包展开在对象构造外面
    };

    while (!candidatesTasks.empty()) // 改为非空才循环
    {
        bool any_progress = false;
        tasks_type scheduled =
            init_scheduled |
            std::views::transform([](task_spec item) { return item.name; }) |
            std::ranges::to<std::vector>();

        // NOTE: 在循环内修改容器，迭代器会失效
        for (size_t i = 0; i < candidatesTasks.size();) // 注意 i 不自动递增
        {
            auto item = std::move(candidatesTasks[i]);
            candidatesTasks.erase(candidatesTasks.begin() + i); // 删除后 i 指向下一个元素

            tasks_type candidates =
                candidatesTasks | std::views::transform([](VariantType &item) {
                    return std::visit([](auto &&t) noexcept { return t.spec().name; },
                                      item);
                }) |
                std::ranges::to<std::vector>();

            auto insert_index = std::visit(
                [&](auto &&t) noexcept {
                    return t.dispatch(std::span{scheduled}, std::span{candidates});
                },
                item);

            if (insert_index != -1)
            {
                task_spec item_spec =
                    std::visit([](auto &&t) noexcept { return t.spec(); }, item);
                size_t insert_pos = static_cast<size_t>(insert_index);
                // 如果该位置已有元素，向后移动直到找到空位
                while (insert_pos < init_scheduled.size())
                    ++insert_pos;
                init_scheduled.insert(init_scheduled.begin() + insert_pos, item_spec);
                any_progress = true;
                break;
            }
            else
            {
                candidatesTasks.insert(candidatesTasks.begin() + i, std::move(item));
                ++i; // 没调度成功，移到下一个
            }
        }

        if (!any_progress)
            throw std::meta::exception{"Can not buildSchedule.",
                                       std::meta::current_function()};
    }
    return init_scheduled;
}
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

template <task_spec...>
struct task_list;

template <static_string prefix, typename init_spec, schedulable auto... task>
struct make_task_graph;

namespace detail
{
    template <static_string prefix, auto init_spec, auto... task>
    struct gen_task_graph
    {
        static constexpr auto task_sequence = std::define_static_array(
            buildSchedule(std::ranges::to<std::vector>(init_spec), task...));

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
        struct type;
        consteval
        {
            std::vector<std::meta::info> nsdms;
            int i = 0;
            for (const task_spec &spec : task_sequence)
            {
                nsdms.push_back(std::meta::data_member_spec(
                    spec.spec, {.name = make_name(prefix.view(), i++)}));
            }
            std::meta::define_aggregate(^^type, nsdms);
        }
    };
}; // namespace detail
template <static_string prefix, task_spec... init_spec, schedulable auto... task>
struct make_task_graph<prefix, task_list<init_spec...>, task...>
    : detail::gen_task_graph<prefix,
                             std::array<task_spec, sizeof...(init_spec)>{init_spec...},
                             task...>::type
{
    using gen_type = detail::gen_task_graph<
        prefix, std::array<task_spec, sizeof...(init_spec)>{init_spec...}, task...>;
    using base_type = gen_type::type;
    static constexpr auto members =
        std::define_static_array(std::meta::nonstatic_data_members_of(
            ^^base_type, std::meta::access_context::current()));
    static constexpr void print_task_sequence()
    {
        std::cout << gen_type::task_sequence_string() << "\n";
    }
};

template <typename T>
struct static_span
{
    const T *data_;
    std::size_t size_;

    consteval static_span(const std::span<const T> &s) noexcept
        : data_(s.data()), size_(s.size())
    {
    }
    consteval static_span(std::vector<T> vec) noexcept
        : static_span(std::define_static_array(vec))
    {
    }
    // ---------- 迭代器 ----------
    // 注意：begin/end/data 必须返回指针，且 const 对象需要返回 const T*
    // forward_like 无法把 T* 变成 const T*（它只能转发指针本身的 const），所以这里保留 if constexpr
    constexpr auto begin(this auto &&self) noexcept
    {
        using U = std::remove_reference_t<decltype(self)>;
        if constexpr (std::is_const_v<U>)
            return static_cast<const T *>(self.data_);
        else
            return self.data_;
    }

    constexpr auto end(this auto &&self) noexcept
    {
        using U = std::remove_reference_t<decltype(self)>;
        if constexpr (std::is_const_v<U>)
            return static_cast<const T *>(self.data_ + self.size_);
        else
            return self.data_ + self.size_;
    }

    constexpr auto data(this auto &&self) noexcept
    {
        using U = std::remove_reference_t<decltype(self)>;
        if constexpr (std::is_const_v<U>)
            return static_cast<const T *>(self.data_);
        else
            return self.data_;
    }

    // ---------- 只读接口（不依赖 const） ----------
    constexpr std::size_t size(this const auto &self) noexcept
    {
        return self.size_;
    }
    constexpr bool empty(this const auto &self) noexcept
    {
        return self.size_ == 0;
    }

    // ---------- 使用 std::forward_like 完美转发元素引用 ----------
    // 自动处理：左值对象 -> T&, const 左值 -> const T&, 右值 -> T&&
    constexpr decltype(auto) operator[](this auto &&self, std::size_t i)
    {
        return std::forward_like<decltype(self)>(self.data_[i]);
    }

    constexpr decltype(auto) front(this auto &&self)
    {
        return std::forward_like<decltype(self)>(self.data_[0]);
    }

    constexpr decltype(auto) back(this auto &&self)
    {
        return std::forward_like<decltype(self)>(self.data_[self.size_ - 1]);
    }
};

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

// NOTE: 别想了，只能当模板使用
template <static_string prefix>
consteval auto build_task(std::vector<task_spec> scheduled)
{
    static constexpr auto specs = std::define_static_array(scheduled);
    struct task_graph;
    consteval
    {
        std::vector<std::meta::info> nsdms;
        int i = 0;
        for (const task_spec &spec : specs)
        {
            nsdms.push_back(std::meta::data_member_spec(
                spec.spec, {.name = make_name(prefix.view(), i++)}));
        }
        std::meta::define_aggregate(^^task_graph, nsdms);
    }
    // constexpr auto task = task_graph{};
    // return std::define_static_object(task);
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

// 复杂系统函数
// 复杂管线函数（共10个）
struct pipeline_funcs
{
    static void init(auto &...)
    {
        std::println("init");
    }
    static void load(auto &...)
    {
        std::println("load");
    }
    static void parse(auto &...)
    {
        std::println("parse");
    }
    static void validate(auto &...)
    {
        std::println("validate");
    }
    static void transform(auto &...)
    {
        std::println("transform");
    }
    static void analyze(auto &...)
    {
        std::println("analyze");
    }
    static void optimize(auto &...)
    {
        std::println("optimize");
    }
    static void generate(auto &...)
    {
        std::println("generate");
    }
    static void finalize(auto &...)
    {
        std::println("finalize");
    }
    static void cleanup(auto &...)
    {
        std::println("cleanup");
    }
};

constexpr bool complex_graph_test()
{
    // 构建调度结果，使用 static_span 包装以便编译期索引
    constexpr auto span = static_span{buildSchedule(
        {}, // 初始已调度列表为空

        // 1. init（无依赖）
        scheduling_task{
            task_spec{^^decltype([](auto &w, auto &i) { pipeline_funcs::init(w, i); }),
                      "init"},
            [](tasks_view_type, tasks_view_type) constexpr -> dispatch_location {
                return 0;
            }},

        // 2. load（依赖 init）
        scheduling_task{
            task_spec{^^decltype([](auto &w, auto &i) { pipeline_funcs::load(w, i); }),
                      "load"},
            [](tasks_view_type sched, tasks_view_type) constexpr -> dispatch_location {
                auto it = std::ranges::find(sched, static_string{"init"});
                return it != sched.end() ? static_cast<int>(it - sched.begin()) + 1 : -1;
            }},

        // 3. parse（依赖 load）
        scheduling_task{
            task_spec{^^decltype([](auto &w, auto &i) { pipeline_funcs::parse(w, i); }),
                      "parse"},
            [](tasks_view_type sched, tasks_view_type) constexpr -> dispatch_location {
                auto it = std::ranges::find(sched, static_string{"load"});
                return it != sched.end() ? static_cast<int>(it - sched.begin()) + 1 : -1;
            }},

        // 4. validate（依赖 parse）
        scheduling_task{
            task_spec{
                ^^decltype([](auto &w, auto &i) { pipeline_funcs::validate(w, i); }),
                "validate"},
            [](tasks_view_type sched, tasks_view_type) constexpr -> dispatch_location {
                auto it = std::ranges::find(sched, static_string{"parse"});
                return it != sched.end() ? static_cast<int>(it - sched.begin()) + 1 : -1;
            }},

        // 5. transform（依赖 parse，与 validate 并行）
        scheduling_task{
            task_spec{
                ^^decltype([](auto &w, auto &i) { pipeline_funcs::transform(w, i); }),
                "transform"},
            [](tasks_view_type sched, tasks_view_type) constexpr -> dispatch_location {
                auto it = std::ranges::find(sched, static_string{"parse"});
                return it != sched.end() ? static_cast<int>(it - sched.begin()) + 1 : -1;
            }},

        // 6. analyze（依赖 validate 和 transform）
        scheduling_task{
            task_spec{^^decltype([](auto &w, auto &i) { pipeline_funcs::analyze(w, i); }),
                      "analyze"},
            [](tasks_view_type sched, tasks_view_type) constexpr -> dispatch_location {
                auto it1 = std::ranges::find(sched, static_string{"validate"});
                auto it2 = std::ranges::find(sched, static_string{"transform"});
                if (it1 == sched.end() || it2 == sched.end())
                    return -1;
                auto last = std::max(it1, it2);
                return static_cast<int>(last - sched.begin()) + 1;
            }},

        // 7. optimize（依赖 analyze）
        scheduling_task{
            task_spec{
                ^^decltype([](auto &w, auto &i) { pipeline_funcs::optimize(w, i); }),
                "optimize"},
            [](tasks_view_type sched, tasks_view_type) constexpr -> dispatch_location {
                auto it = std::ranges::find(sched, static_string{"analyze"});
                return it != sched.end() ? static_cast<int>(it - sched.begin()) + 1 : -1;
            }},

        // 8. generate（依赖 optimize）
        scheduling_task{
            task_spec{
                ^^decltype([](auto &w, auto &i) { pipeline_funcs::generate(w, i); }),
                "generate"},
            [](tasks_view_type sched, tasks_view_type) constexpr -> dispatch_location {
                auto it = std::ranges::find(sched, static_string{"optimize"});
                return it != sched.end() ? static_cast<int>(it - sched.begin()) + 1 : -1;
            }},

        // 9. finalize（依赖 generate）
        scheduling_task{
            task_spec{
                ^^decltype([](auto &w, auto &i) { pipeline_funcs::finalize(w, i); }),
                "finalize"},
            [](tasks_view_type sched, tasks_view_type) constexpr -> dispatch_location {
                auto it = std::ranges::find(sched, static_string{"generate"});
                return it != sched.end() ? static_cast<int>(it - sched.begin()) + 1 : -1;
            }},

        // 10. cleanup（依赖 init 和 finalize）
        scheduling_task{
            task_spec{^^decltype([](auto &w, auto &i) { pipeline_funcs::cleanup(w, i); }),
                      "cleanup"},
            [](tasks_view_type sched, tasks_view_type) constexpr -> dispatch_location {
                auto itInit = std::ranges::find(sched, static_string{"init"});
                auto itFin = std::ranges::find(sched, static_string{"finalize"});
                if (itInit == sched.end() || itFin == sched.end())
                    return -1;
                auto last = std::max(itInit, itFin);
                return static_cast<int>(last - sched.begin()) + 1;
            }})};

    // ========== 编译期稳定性验证 ==========
    static_assert(span.size() == 10);

    // 预期顺序：按输入顺序解析（validate 先于 transform，因此 validate 在前）
    static_assert(span[0].name.view() == "init");
    static_assert(span[1].name.view() == "load");
    static_assert(span[2].name.view() == "parse");
    static_assert(span[3].name.view() == "validate");
    static_assert(span[4].name.view() == "transform");
    static_assert(span[5].name.view() == "analyze");
    static_assert(span[6].name.view() == "optimize");
    static_assert(span[7].name.view() == "generate");
    static_assert(span[8].name.view() == "finalize");
    static_assert(span[9].name.view() == "cleanup");

    return true;
}
// 1. 线性长链（10个任务，顺序依赖）
constexpr bool linear_chain_test()
{
    constexpr auto span = static_span{buildSchedule(
        {},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "T1"},
                        [](auto, auto) constexpr { return 0; }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "T2"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"T1"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "T3"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"T2"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "T4"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"T3"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "T5"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"T4"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "T6"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"T5"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "T7"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"T6"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "T8"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"T7"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "T9"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"T8"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "T10"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"T9"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }})};
    static_assert(span.size() == 10);
    static_assert(span[0].name == "T1");
    static_assert(span[1].name == "T2");
    static_assert(span[2].name == "T3");
    static_assert(span[3].name == "T4");
    static_assert(span[4].name == "T5");
    static_assert(span[5].name == "T6");
    static_assert(span[6].name == "T7");
    static_assert(span[7].name == "T8");
    static_assert(span[8].name == "T9");
    static_assert(span[9].name == "T10");
    return true;
}

// 2. 完全星形（1个根 + 9个叶子）
constexpr bool star_test()
{
    constexpr auto span = static_span{buildSchedule(
        {},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "Root"},
                        [](auto, auto) constexpr { return 0; }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "L1"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"Root"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "L2"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"Root"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "L3"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"Root"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "L4"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"Root"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "L5"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"Root"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "L6"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"Root"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "L7"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"Root"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "L8"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"Root"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "L9"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"Root"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }})};
    static_assert(span.size() == 10);
    static_assert(span[0].name == "Root");
    static_assert(span[1].name == "L1");
    static_assert(span[2].name == "L2");
    static_assert(span[3].name == "L3");
    static_assert(span[4].name == "L4");
    static_assert(span[5].name == "L5");
    static_assert(span[6].name == "L6");
    static_assert(span[7].name == "L7");
    static_assert(span[8].name == "L8");
    static_assert(span[9].name == "L9");
    return true;
}

// 3. 菱形 DAG（两个根 → 合并 → 两个叶子）
constexpr bool diamond_test()
{
    constexpr auto span = static_span{buildSchedule(
        {},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "A"},
                        [](auto, auto) constexpr { return 0; }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "B"},
                        [](auto, auto) constexpr { return 0; }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "C"},
                        [](auto sched, auto) constexpr {
                            auto itA = std::ranges::find(sched, static_string{"A"});
                            auto itB = std::ranges::find(sched, static_string{"B"});
                            if (itA == sched.end() || itB == sched.end())
                                return -1;
                            auto last = std::max(itA, itB);
                            return static_cast<int>(last - sched.begin()) + 1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "D"},
                        [](auto sched, auto) constexpr {
                            auto itC = std::ranges::find(sched, static_string{"C"});
                            return itC != sched.end()
                                       ? static_cast<int>(itC - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "E"},
                        [](auto sched, auto) constexpr {
                            auto itC = std::ranges::find(sched, static_string{"C"});
                            return itC != sched.end()
                                       ? static_cast<int>(itC - sched.begin()) + 1
                                       : -1;
                        }})};
    static_assert(span.size() == 5);
    static_assert(span[0].name == "A");
    static_assert(span[1].name == "B");
    static_assert(span[2].name == "C");
    static_assert(span[3].name == "D");
    static_assert(span[4].name == "E");
    return true;
}

// 4. 初始非空列表 + 依赖已存在任务
constexpr bool initial_nonempty_test()
{
    constexpr auto span = static_span{buildSchedule(
        {task_spec{^^decltype([](auto &, auto &) {}), "Pre"}},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "A"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"Pre"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "B"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"A"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }})};
    static_assert(span.size() == 3);
    static_assert(span[0].name == "Pre");
    static_assert(span[1].name == "A");
    static_assert(span[2].name == "B");
    return true;
}

constexpr bool complex_multi_branch_test()
{
    constexpr auto span = static_span{buildSchedule(
        // 初始已调度：Pre
        {task_spec{^^decltype([](auto &, auto &) {}), "Pre"}},

        // A, B 无依赖（根任务）
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "A"},
                        [](auto, auto) constexpr { return 0; }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "B"},
                        [](auto, auto) constexpr { return 0; }},

        // C 依赖 A
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "C"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"A"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        // D 依赖 B
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "D"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"B"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        // E 依赖 A 和 B
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "E"},
                        [](auto sched, auto) constexpr {
                            auto itA = std::ranges::find(sched, static_string{"A"});
                            auto itB = std::ranges::find(sched, static_string{"B"});
                            if (itA == sched.end() || itB == sched.end())
                                return -1;
                            auto last = std::max(itA, itB);
                            return static_cast<int>(last - sched.begin()) + 1;
                        }},
        // F 依赖 C 和 D
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "F"},
                        [](auto sched, auto) constexpr {
                            auto itC = std::ranges::find(sched, static_string{"C"});
                            auto itD = std::ranges::find(sched, static_string{"D"});
                            if (itC == sched.end() || itD == sched.end())
                                return -1;
                            auto last = std::max(itC, itD);
                            return static_cast<int>(last - sched.begin()) + 1;
                        }},
        // G 依赖 E
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "G"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"E"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        // H 依赖 F
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "H"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"F"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        // I 依赖 G 和 H
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "I"},
                        [](auto sched, auto) constexpr {
                            auto itG = std::ranges::find(sched, static_string{"G"});
                            auto itH = std::ranges::find(sched, static_string{"H"});
                            if (itG == sched.end() || itH == sched.end())
                                return -1;
                            auto last = std::max(itG, itH);
                            return static_cast<int>(last - sched.begin()) + 1;
                        }},
        // J 依赖 Pre 和 I
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "J"},
                        [](auto sched, auto) constexpr {
                            auto itPre = std::ranges::find(sched, static_string{"Pre"});
                            auto itI = std::ranges::find(sched, static_string{"I"});
                            if (itPre == sched.end() || itI == sched.end())
                                return -1;
                            auto last = std::max(itPre, itI);
                            return static_cast<int>(last - sched.begin()) + 1;
                        }},
        // K 依赖 J（增加深度，使总任务数达到 12）
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "K"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"J"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }})};

    static_assert(span.size() == 12); // Pre + A..K = 12 个

    // ========== 验证预期顺序 ==========
    // 推导：Pre 先，A 和 B 无依赖插入到 Pre 之后（A 先 B 后），然后 C 依赖 A -> 插入到 A 后，
    // D 依赖 B -> 插入到 B 后，E 依赖 A,B 取 max -> B 后，F 依赖 C,D 取 max -> D 后，
    // G 依赖 E -> E 后，H 依赖 F -> F 后，I 依赖 G,H 取 max -> H 后，J 依赖 Pre 和 I 取 max -> I 后，
    // K 依赖 J -> J 后。
    // 因此完整顺序应为：Pre, A, B, C, D, E, F, G, H, I, J, K
    static_assert(span[0].name == "Pre");
    static_assert(span[1].name == "A");
    static_assert(span[2].name == "B");
    static_assert(span[3].name == "C");
    static_assert(span[4].name == "D");
    static_assert(span[5].name == "E");
    static_assert(span[6].name == "F");
    static_assert(span[7].name == "G");
    static_assert(span[8].name == "H");
    static_assert(span[9].name == "I");
    static_assert(span[10].name == "J");
    static_assert(span[11].name == "K");

    return true;
}

constexpr bool reverse_chain_test()
{
    // 输入顺序刻意颠倒：E, D, C, B, A
    // 依赖：A 无依赖，B 依赖 A，C 依赖 B，D 依赖 C，E 依赖 D
    constexpr auto span = static_span{buildSchedule(
        {},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "E"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"D"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "D"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"C"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "C"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"B"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "B"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"A"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "A"},
                        [](auto, auto) constexpr { return 0; }})};

    static_assert(span.size() == 5);
    // 最终顺序必须是 A, B, C, D, E（依赖链顺序）
    static_assert(span[0].name == "A");
    static_assert(span[1].name == "B");
    static_assert(span[2].name == "C");
    static_assert(span[3].name == "D");
    static_assert(span[4].name == "E");
    return true;
}
// 测试输入逆序但包含并行根，只验证依赖约束（偏序），不强制根顺序
constexpr bool reverse_with_parallel_test()
{
    constexpr auto span = static_span{buildSchedule(
        {},
        // 输入顺序故意逆序：G, F, E, D, C, B, A
        // 依赖关系：A,B 无依赖；C 依赖 A；D 依赖 B；E 依赖 C 和 D；F 依赖 E；G 依赖 F
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "G"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"F"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "F"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"E"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "E"},
                        [](auto sched, auto) constexpr {
                            auto itC = std::ranges::find(sched, static_string{"C"});
                            auto itD = std::ranges::find(sched, static_string{"D"});
                            if (itC == sched.end() || itD == sched.end())
                                return -1;
                            auto last = std::max(itC, itD);
                            return static_cast<int>(last - sched.begin()) + 1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "D"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"B"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "C"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"A"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "B"},
                        [](auto, auto) constexpr { return 0; }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "A"},
                        [](auto, auto) constexpr { return 0; }})};

    static_assert(span.size() == 7);

    // 断言算法实际产生的顺序（稳定且满足所有依赖）
    static_assert(span[0].name == "B");
    static_assert(span[1].name == "D");
    static_assert(span[2].name == "A");
    static_assert(span[3].name == "C");
    static_assert(span[4].name == "E");
    static_assert(span[5].name == "F");
    static_assert(span[6].name == "G");

    return true;
}

constexpr bool parallel_begin_end_test()
{
    constexpr auto span = static_span{buildSchedule(
        {},
        // 输入顺序故意逆序：End, D, C, B, A, Begin
        // 依赖：Begin 无依赖；A,B,C,D 依赖 Begin；End 依赖 A,B,C,D
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "End"},
                        [](auto sched, auto) constexpr {
                            auto itA = std::ranges::find(sched, static_string{"A"});
                            auto itB = std::ranges::find(sched, static_string{"B"});
                            auto itC = std::ranges::find(sched, static_string{"C"});
                            auto itD = std::ranges::find(sched, static_string{"D"});
                            if (itA == sched.end() || itB == sched.end() ||
                                itC == sched.end() || itD == sched.end())
                                return -1;
                            auto last = std::max({itA, itB, itC, itD});
                            return static_cast<int>(last - sched.begin()) + 1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "D"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"Begin"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "C"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"Begin"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "B"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"Begin"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "A"},
                        [](auto sched, auto) constexpr {
                            auto it = std::ranges::find(sched, static_string{"Begin"});
                            return it != sched.end()
                                       ? static_cast<int>(it - sched.begin()) + 1
                                       : -1;
                        }},
        scheduling_task{task_spec{^^decltype([](auto &, auto &) {}), "Begin"},
                        [](auto, auto) constexpr { return 0; }})};

    static_assert(span.size() == 6);
    // 调度顺序由算法确定性计算：
    // Begin 最后输入，但无依赖，先尝试前面的都失败，最后 Begin 成功插入索引0
    // 然后依次尝试 D,C,B,A（按输入逆序），它们都依赖 Begin，插入到 Begin 后
    // 由于插入冲突时后插入的会向后移动，最终顺序为：Begin, D, C, B, A, End
    static_assert(span[0].name == "Begin");
    static_assert(span[1].name == "D");
    static_assert(span[2].name == "C");
    static_assert(span[3].name == "B");
    static_assert(span[4].name == "A");
    static_assert(span[5].name == "End");

    return true;
}
#define TEST 0
constexpr auto just_one_times()
{
    constexpr world_type world;
    constexpr input_type input;

    constexpr auto graph2 = build_task_graph<
        "m_", static_span{buildSchedule(
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
                          -> dispatch_location { return 0; }})}>();
    if constexpr (TEST == 0)
    {
        invoke_aggregate_ranges<"m_", 0, 100>(graph2, world, input); //NOTE: 编译错误
        invoke_aggregate_ranges<"m_", 0, 100>(graph2, world, input);
    }

    return true;
}

//NOTE: 不怕重复调用，因为lambda 天然就不同。不会被合并
constexpr auto test_make_task_graph(bool print = false)
{
    constexpr world_type world;
    constexpr input_type input;
    constexpr auto task = make_task_graph<
        "m_",
        task_list<task_spec{.spec = ^^decltype([](auto &world, auto &input) {
                                return system_fixed_functions::fixed_0(world, input);
                            }),
                            .name = "system_fixed_functions::fixed_0"}>,
        scheduling_task{
            task_spec{.spec = ^^decltype([](auto &world, auto &input) constexpr {
                          return data_change_function::fun_0(world, input);
                      }),
                      .name = "data_change_function::fun_0"},
            [](tasks_view_type scheduled, tasks_view_type candidates) { return 0; }}>{};
    if (print)
        invoke_aggregate_ranges<"m_", 0, 100>(task, world, input);
    {
        constexpr auto task = make_task_graph<
            "m_",
            task_list<task_spec{
                .spec = ^^decltype([](auto &world, auto &input) constexpr {
                    return system_fixed_functions::fixed_0(world, input);
                }),
                .name = "system_fixed_functions::fixed_0"}>,
            scheduling_task{
                task_spec{.spec = ^^decltype([](auto &world, auto &input) constexpr {
                              return data_change_function::fun_0(world, input);
                          }),
                          .name = "data_change_function::fun_0"},
                [](tasks_view_type scheduled, tasks_view_type candidates) {
                    return 0;
                }}>{};
        if (print)
        {
            invoke_aggregate_ranges<"m_", 0, 100>(task, world, input);

            task.print_task_sequence();
        }
    }
    return true;
}
static_assert(test_make_task_graph()); //NOTE: print不允许
/*
e:\0_github_project\mcsvulkan\test\mcsvulkan\meta\task_orchestration.cpp:421:19: error: call to non-'constexpr' function 'std::basic_ostream<char, _Traits>& std::operator<<(basic_ostream<char, _Traits>&, const char*) [with _Traits = char_traits<char>]'
  421 |         std::cout << "system_fixed_functions::fixed_0 call...\n";
      |         ~~~~~~~~~~^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
*/
// static_assert(test_make_task_graph(true));
// NOLINTEND
int main()
try
{
    just_one_times();
    test_make_task_graph(true);

    static_assert(complex_graph_test(), "complex_graph_test failed");
    static_assert(linear_chain_test(), "linear_chain_test failed");
    static_assert(star_test(), "star_test failed");
    static_assert(diamond_test(), "diamond_test failed");
    static_assert(initial_nonempty_test(), "initial_nonempty_test failed");
    static_assert(complex_multi_branch_test(), "complex_multi_branch_test failed");
    static_assert(reverse_chain_test(), "reverse_chain_test failed");
    static_assert(reverse_with_parallel_test(), "reverse_with_parallel_test failed");
    static_assert(parallel_begin_end_test());

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

        if constexpr (TEST == 1) //NOTE: 有问题
        {
            invoke_aggregate_ranges<"m_", 0, 100>(graph, world, input); //NOTE: 编译错误
        }

        //NOTE: 两次就编译期错误的了。可怕得很
        {
            // constexpr auto result = std::define_static_array(buildSchedule(
            //     {task_spec{.spec = ^^decltype([](auto &world, auto &input) {
            //                    return system_fixed_functions::fixed_0(world, input);
            //                }),
            //                .name = "system_fixed_functions::fixed_0"}},
            //     scheduling_task{
            //         task_spec{.spec = ^^decltype([](auto &world, auto &input) {
            //                       return data_change_function::fun_0(world, input);
            //                   }),
            //                   .name = "data_change_function::fun_0"},
            //         [](tasks_view_type scheduled, tasks_view_type candidates) constexpr
            //             -> dispatch_location { return 0; }}));
            // constexpr auto span = static_span{result};
            // constexpr auto graph = build_task_graph<"m_", span>();

            // invoke_aggregate_ranges<"m_", 0, 100>(graph, world, input);
        }
        {
            constexpr auto span2 = static_span{buildSchedule(
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
                        -> dispatch_location { return 0; }})};
            constexpr auto graph2 = build_task_graph<"m_", span2>();
            // invoke_aggregate_ranges<"m_", 0, 100>(graph2, world, input); //NOTE: 编译错误
        }
        {
            //NOTE: 结论 consteval 之后的，必须 define_static 才是对的
            constexpr auto t0 = std::constant_wrapper<[](auto &world, auto &input) {
                return system_fixed_functions::fixed_0(world, input);
            }>{};
            constexpr auto t1 = std::constant_wrapper<[](auto &world, auto &input) {
                return data_change_function::fun_0(world, input);
            }>{};
            constexpr auto t3 = std::constant_wrapper<
                [](tasks_view_type scheduled,
                   tasks_view_type candidates) constexpr -> dispatch_location {
                    return 0;
                }>{};
            constexpr auto span2 = static_span{buildSchedule(
                {task_spec{.spec = ^^decltype(t0),
                           .name = "system_fixed_functions::fixed_0"}},
                scheduling_task{task_spec{.spec = ^^decltype(t1),
                                          .name = "data_change_function::fun_0"},
                                t3})};
            constexpr auto graph2 = build_task_graph<"m_", span2>();
            // invoke_aggregate_ranges<"m_", 0, 100>(graph2, world, input); //NOTE: 编译错误
        }
        {
            constexpr auto span2 = static_span{std::define_static_array(buildSchedule(
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
                        -> dispatch_location { return 0; }}))};
            constexpr auto graph2 = build_task_graph<"m_", span2>();
            // invoke_aggregate_ranges<"m_", 0, 100>(graph2, world, input); //NOTE: 编译错误
        }
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