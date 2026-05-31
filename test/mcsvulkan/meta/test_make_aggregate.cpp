#include "head.hpp"

#include <assert.h>
#include <cassert>
#include <cstddef>
#include <functional>
#include <iostream>
#include <exception>
#include <type_traits>
#include <utility>
#include <vector>

using mcs::vulkan::meta::make_aggregate;
using bind_member = mcs::vulkan::meta::bind_member;
using mcs::vulkan::meta::static_string;
using mcs::vulkan::meta::static_nsdms_of;
using mcs::vulkan::meta::static_parameters_of;

static constexpr auto static_fun(int x[[= bind_member{"x"}]], int y)
{
    return x + y;
}
static_assert(std::meta::is_function(^^static_fun));
static_assert(std::meta::is_function_type(std::meta::type_of(^^static_fun)));
static_assert(std::meta::annotations_of_with_type(^^static_fun, ^^bind_member).size() ==
              0);
//

//NOTE: BUG 也不算 BUG。 函数变成指针，丢失注解信息
constexpr auto parameters = static_parameters_of(^^static_fun);
static_assert(std::meta::annotations_of_with_type(parameters[0], ^^bind_member).size() >
              0);
constexpr auto v = static_fun; //NOTE: 变成指针，丢失注解信息
static_assert(std::is_same_v<std::decay_t<decltype(v)>, int (*)(int, int)>);
constexpr auto parameters2 =
    static_parameters_of(^^std::remove_pointer_t<std::decay_t<decltype(v)>>);
static_assert(std::meta::annotations_of_with_type(parameters2[0], ^^bind_member).size() ==
              0);

// static_assert(std::meta::is_function_type(std::meta::type_of(^^decltype([]() {}))));

int main()
try
{

    auto b = make_aggregate<"class", "x", "fn">(1, [](auto &&self, auto value) noexcept {
        return std::forward_like<decltype(self)>(self.x) + value;
    });
    assert(b.x == 1);
    static_assert(decltype(b)::class_name == "class");

    assert(b.template invoke<"fn">(2) == 3);
    assert(b.template with([]([[= bind_member{"x"}]] int a, int y) { return a + y; },
                           2) == 3);
    // assert(b.template with(static_fun, 2) == 3);

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