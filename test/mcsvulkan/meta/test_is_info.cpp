#include <iostream>
#include <exception>

#include "head.hpp"

using std::meta::is_class_type;
using mcs::vulkan::meta::is_info;

struct my_struct // NOLINT
{
    static auto static_fun() {}
    auto fun() {}
    int m;
};
bool v;
auto fun() {}

static_assert(static_cast<bool>(is_class_type(^^my_struct)));

//NOTE: is_class_type 内部要求必须是 一个类型.
// static_assert(not static_cast<bool>(is_class_type(^^my_struct::static_fun)));
// static_assert(not static_cast<bool>(is_class_type(^^my_struct::fun)));
// static_assert(not static_cast<bool>(is_class_type(^^v)));

constexpr auto is_class_info = [](std::meta::info info) -> bool {
    try
    {
        return is_class_type(info);
    }
    catch (...)
    {
        return false;
    }
};
static_assert(is_class_info(^^my_struct));

static_assert(not is_class_info(^^my_struct::static_fun));
static_assert(not is_class_info(^^my_struct::fun));
static_assert(not is_class_info(^^my_struct::m));

static_assert(not is_class_info(^^v));
static_assert(not is_class_info(^^::fun));

// NOTE: 简单的封装一下
constexpr auto is_class = is_info<is_class_type>{};
static_assert(not is_class(^^my_struct::static_fun));
static_assert(not is_class(^^my_struct::fun));
static_assert(not is_class(^^my_struct::m));

static_assert(not is_class(^^v));
static_assert(not is_class(^^::fun));

// is_function_type.  //NOTE: 类型 是 类型
constexpr auto is_fun_type = is_info<std::meta::is_function_type>{};
static_assert(not is_fun_type(^^my_struct::static_fun));
static_assert(not is_fun_type(^^my_struct::fun));
static_assert(not is_fun_type(^^my_struct::m));

static_assert(not is_fun_type(^^::fun));

constexpr auto is_fun = is_info<std::meta::is_function>{};
static_assert(is_fun(^^my_struct::static_fun));
static_assert(is_fun(^^my_struct::fun));
static_assert(not is_fun_type(^^my_struct::m));
static_assert(is_fun(^^::fun));

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
}