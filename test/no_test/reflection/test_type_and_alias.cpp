#include <iostream>
#include <meta>
using namespace std::meta;

// 变量
int var;
extern int var_extern;
static int var_static;
constexpr int var_constexpr = 1;
static_assert(is_variable(^^var));
static_assert(is_variable(^^var_extern));
static_assert(is_variable(^^var_static));
static_assert(is_variable(^^var_constexpr));
static_assert(!is_type(^^var));
static_assert(!is_namespace(^^var));

// 类型
struct S
{
};
enum E
{
};
using T = int;
template <typename>
struct Temp
{
};
using TempInt = Temp<int>; // 别名模板特化
static_assert(is_type(^^S));
static_assert(is_type(^^E));
static_assert(is_type(^^int));
static_assert(is_type(^^T));       // 别名
static_assert(is_type(^^TempInt)); // 别名模板特化
static_assert(!is_variable(^^S));
static_assert(!is_namespace(^^S));

// 命名空间
namespace ns
{
}
static_assert(is_namespace(^^ns));
static_assert(!is_type(^^ns));
static_assert(!is_variable(^^ns));

// 类型别名
using MyInt = int;
using MyFloat = float;
template <typename T>
using MyVec = std::vector<T>;
using VecInt = MyVec<int>; // 别名模板特化
static_assert(is_type_alias(^^MyInt));
static_assert(is_type_alias(^^MyFloat));
static_assert(is_type_alias(^^VecInt)); // 特化也是别名
static_assert(!is_type_alias(^^int));   // 不是别名
static_assert(!is_type_alias(^^S));

// 命名空间别名
namespace long_name
{
}
namespace alias = long_name;
static_assert(is_namespace_alias(^^alias));
static_assert(!is_namespace_alias(^^long_name));
static_assert(!is_namespace_alias(^^ns));

int main()
{
    std::cout << "main done\n";
    return 0;
}