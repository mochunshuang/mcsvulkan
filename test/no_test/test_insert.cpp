#include <functional>
#include <iostream>
#include <exception>
#include <set>
#include <string>
#include <meta>
#include <unordered_map>
#include <variant>
#include <vector>

// NOLINTBEGIN
struct task_info
{
    std::string name;
    // std::function<void()> func; //NOTE: 会导致不能编译期确定
};
struct queue_config
{
    // 固定锚点前后.
    struct fixed_position
    {
        std::string anchor;
        int position;
    };
    struct after
    {
        std::string anchor;
    };
    struct before
    {
        std::string anchor;
    };
    struct none
    {
    };
};
struct schedulable_task
{
    using config_type = std::variant<queue_config::none, queue_config::fixed_position,
                                     queue_config::after, queue_config::before>;
    task_info task;
    std::vector<config_type> configs;
};

consteval auto test_extract_schedulable_task(std::meta::info info)
{
    // error: uncaught exception of type 'std::meta::exception'; 'what()': 'value cannot be extracted'
    //  error: 'schedulable_task' must be a cv-unqualified structural type that is not a reference type
    auto value = std::meta::extract<schedulable_task>(info);
    return true;
}
constexpr auto test_value = schedulable_task{};

//  error: non-constant condition for static assertion
// static_assert(test_extract_schedulable_task(^^test_value));

consteval auto execute(std::meta::info v)
{
    //  要求最终的 fixed_position
    //  error: call to non-'constexpr' function 'std::set<_Key, _Compare, _Alloc>::set()
    // std::set<int> set;

    // error: call to non-'constexpr' function 'std::unordered_map<_Key, _Tp, _Hash, _Pred, _Alloc>::unordered_map()
    // std::unordered_map<int, int> map;

    return true;
}
static_assert(execute(^^int));

int main()
try
{
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
} // NOLINTEND