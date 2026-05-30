#include "head.hpp"

#include <assert.h>
#include <iostream>
#include <exception>
#include <print>
#include <type_traits>

using mcs::vulkan::meta::type;
template <std::meta::info info>
using type_t = typename[:type(info):];
using mcs::vulkan::meta::identifier_view;

// NOLINTBEGIN
//========================================== test type ===================================
constexpr auto int_info = ^^int;
static_assert(not std::meta::has_identifier(int_info));

//NOTE: type 不会添加或生成 identifier
static_assert(not std::meta::has_identifier(type(int_info)));
// constexpr auto int_view = identifier_view(type(int_info));

static_assert(std::meta::is_type(int_info));

//NOTE: 可以反射非 constexpr 变量
int value = 0;
constexpr auto value_info = ^^value;
static_assert(not std::meta::is_type(value_info));

// NOTE: 值 和 类型 都能用 info 储存,但是要手动感知. 应该叫 var_info
static_assert(std::meta::is_variable(value_info));
static_assert(not std::meta::is_variable(int_info));

//========================================== test type_t ===================================
static_assert(std::is_same_v<type_t<value_info>, int>);
static_assert(std::is_same_v<type_t<^^int>, int>);

const int cvalue = 0;
constexpr auto cvalue_info = ^^cvalue;
static_assert(not std::is_same_v<type_t<value_info>, type_t<cvalue_info>>);

//NOTE:  type_t 的必要性
// 'what()': 'reflection does not represent a type'
// constexpr auto test_same = std::meta::is_same_type(value_info, cvalue_info);

// NOTE: 需要你能 上帝视角 指定 info 是值变量反射 还是 类型反射? 做不到的
static_assert(not std::meta::is_same_type(type(value_info), type(cvalue_info)));

//NOTE: 可以方便的将 info 转化为 类型info. 方便得到 类型比较
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