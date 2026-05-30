#include "head.hpp"

#include <iostream>
#include <exception>
#include <print>

using mcs::vulkan::meta::identifier_view;

// NOLINTBEGIN
constexpr auto int_info = ^^int;
static_assert(not std::meta::has_identifier(int_info));
// constexpr auto int_view = identifier_view(int_info);

struct my_struct // NOLINT
{
};
constexpr auto my_struct_view = identifier_view(^^my_struct);

constexpr auto lambda_struct_view = [] {
    struct lambda_struct
    {
    };
    return identifier_view(^^lambda_struct);
}();
constexpr auto lambda_struct_view2 = [] {
    struct lambda_struct
    {
    };
    return identifier_view(^^lambda_struct);
}();

namespace identifier
{
    struct my_struct // NOLINT
    {
        int v;
    };
} // namespace identifier

constexpr auto identifier_struct_view = identifier_view(^^identifier::my_struct);

//NOTE: 没什么用. 信息没用
int main()
try
{
    // NOTE: identifier_view 比较干净。定义是什么就是什么，没有唯一性应该
    std::println("identifier_view(^^my_struct): {}", my_struct_view);
    std::println("lambda_struct_view: {}", lambda_struct_view);
    std::println("lambda_struct_view2: {}", lambda_struct_view2);

    std::println("identifier_struct_view: {}", identifier_struct_view);

    std::println("identifier_view(^^identifier::my_struct::v): {}",
                 identifier_view(^^identifier::my_struct::v));
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