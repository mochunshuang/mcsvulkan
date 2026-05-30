#include "head.hpp"

#include <cstddef>
#include <iostream>
#include <exception>
#include <print>

using mcs::vulkan::meta::type;

using mcs::vulkan::meta::identifier_view;

// NOTE: 用处不大。不需要
// using mcs::vulkan::meta::value;

template <typename T>
consteval auto value(std::meta::info info)
{
    if (not std::meta::is_value(info))
        throw std::meta::exception(u8"info is not a value",
                                   std::meta::current_function());
    return std::meta::extract<T>(info);
}
template <std::meta::info info>
using type_t = typename[:type(info):];

// NOLINTBEGIN
//========================================== test value ===================================
constexpr auto int_info = ^^int;
static_assert(std::meta::is_type(int_info));
static_assert(not std::meta::is_variable(int_info));

// 'what()': 'info is not a value'
// constexpr auto get_value = value<int>(int_info);
// constexpr auto get_value = value<type_t<int_info>>(int_info);

int v = 0;
constexpr auto value_info = ^^v;
static_assert(not std::meta::is_type(value_info));
static_assert(std::meta::is_variable(value_info));

// NOTE: 值语言 对象语言??? 反正 int 不是对象
static_assert(not std::meta::is_object(value_info));

struct my_struct
{
};
my_struct ms;
static_assert(not std::meta::is_object(^^ms));

// NOTE: 变成对象还要 反射. 这个object 看起来是要附件指定的
static_assert(std::meta::is_object(std::meta::reflect_object(v)));
static_assert(std::meta::is_object(std::meta::reflect_object(ms)));

// error: the value of 'v' is not usable in a constant expression
// constexpr auto get_value = value<type_t<int_info>>(value_info);

// the value of 'v' is not usable in a constant expression
// auto get_value = value<type_t<int_info>>(value_info);

//error: cannot convert 'int' to 'std::meta::info'
// auto get_value = value<type_t<int_info>>(0);

//NOTE: 反射的 参数是 info
auto get_value = value<type_t<int_info>>(std::meta::reflect_constant(0));
// auto get_value = std::meta::extract<int>(std::meta::reflect_constant(0));

//NOTE: 无法类型转化. 类型要求非常严格
// what()': 'value cannot be extracted'
// auto get_value = value<type_t<int_info>>(std::meta::reflect_constant(0.0F));
// auto get_value = std::meta::extract<int>(std::meta::reflect_constant(0.0F));

consteval auto get_result()
{
    int i = 3;
    auto n = value<type_t<^^i>>(std::meta::reflect_constant(i));
    int result = 0;
    for (int j = 0; j < n; ++j)
    {
        result += j;
    }
    return result;
}
static_assert(get_result() == 0 + 1 + 2);

consteval auto get_result2(std::meta::info input)
{
    int i = 3;
    auto n = i + value<type_t<^^i>>(input);
    int result = 0;
    for (int j = 0; j < n; ++j)
        result += j;
    return result;
}

constexpr auto cunt = 2;
static_assert(get_result2(std::meta::reflect_constant(cunt)) == 0 + 1 + 2 + 3 + 4);

// NOTE: 输入和输出 都可以是 info. 这还点用
consteval auto get_result3(std::meta::info input)
{
    int i = 3;
    auto n = i + value<type_t<^^i>>(input);
    size_t result = 0;
    for (int j = 0; j < n; ++j)
        result += j;
    return std::meta::reflect_constant(result);
}
constexpr auto ret = get_result3(std::meta::reflect_constant(cunt));
static_assert(value<type_t<ret>>(ret) == 0 + 1 + 2 + 3 + 4);

template <typename T>
consteval bool is_value_as(std::meta::info R)
{
    if (!std::meta::is_value(R))
        return false;
    return std::meta::is_same_type(std::meta::type_of(R), ^^T);
}
static_assert(is_value_as<size_t>(ret));
static_assert(not is_value_as<int>(ret));
static_assert(not is_value_as<std::string>(ret));

// NOTE: 引用?
// 'value cannot be extracted'
// auto get_ref_value = std::meta::extract<int &>(std::meta::reflect_constant(0.0F));

constexpr int cref_value = 1;
int ref_value = 1;
// 'value cannot be extracted'
// auto get_ref_value = std::meta::extract<int &>(std::meta::reflect_constant(cref_value));
// auto get_ref_value =
//     std::meta::extract<const int &>(std::meta::reflect_constant(cref_value));

//NOTE: 只有对象才有引用? 应该是了
auto get_ref_value =
    std::meta::extract<const int &>(std::meta::reflect_object(cref_value));

// error: the value of 'ref_value' is not usable in a constant expression
// auto get_ref_value2 = std::meta::extract<int &>(std::meta::reflect_constant(ref_value));

// NOTE: 使用 反射API 输入大概率 只能是 常量数据. 运行时数据和静态反射不搭

auto runtime_value = value<type_t<ret>>(ret); //NOTE: 静态反射的输出 可以放到运行时

int main()
try
{
    std::println("runtime_value: {}", runtime_value);
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