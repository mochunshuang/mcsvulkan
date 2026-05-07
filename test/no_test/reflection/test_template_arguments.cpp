#include <meta>
#include <iostream>
#include <type_traits>
using namespace std::meta;

// ===== 全局模板实体 =====
template <typename T>
struct ClassTemplate
{
};
template <typename T>
void func_template(T)
{
}
template <typename T>
T var_template = T{};
template <typename T>
using AliasTemplate = T;

// 普通函数
int global_func(double d)
{
    return static_cast<int>(d);
}

// ----- 测试 has_template_arguments / template_of / template_arguments_of -----
consteval bool test_template_args()
{
    // 类模板特化
    if (!has_template_arguments(^^ClassTemplate<int>))
        return false;
    if (template_of(^^ClassTemplate<int>) != ^^ClassTemplate)
        return false;
    int idx = 0;
    for (info arg : template_arguments_of(^^ClassTemplate<int>))
    {
        if (idx == 0 && arg != ^^int)
            return false;
        ++idx;
    }
    if (idx != 1)
        return false;

    // 函数模板特化
    if (!has_template_arguments(^^func_template<double>))
        return false;
    if (template_of(^^func_template<double>) != ^^func_template)
        return false;
    idx = 0;
    for (info arg : template_arguments_of(^^func_template<double>))
    {
        if (idx == 0 && arg != ^^double)
            return false;
        ++idx;
    }
    if (idx != 1)
        return false;

    // 变量模板特化
    if (!has_template_arguments(^^var_template<int>))
        return false;
    if (template_of(^^var_template<int>) != ^^var_template)
        return false;
    idx = 0;
    for (info arg : template_arguments_of(^^var_template<int>))
    {
        if (idx == 0 && arg != ^^int)
            return false;
        ++idx;
    }
    if (idx != 1)
        return false;

    // 别名模板特化
    if (!has_template_arguments(^^AliasTemplate<float>))
        return false;
    if (template_of(^^AliasTemplate<float>) != ^^AliasTemplate)
        return false;
    idx = 0;
    for (info arg : template_arguments_of(^^AliasTemplate<float>))
    {
        if (idx == 0 && arg != ^^float)
            return false;
        ++idx;
    }
    if (idx != 1)
        return false;

    // 非特化实体
    if (has_template_arguments(^^ClassTemplate))
        return false;
    if (has_template_arguments(^^int))
        return false;
    return true;
}
static_assert(test_template_args());

// ----- 测试 parameters_of / return_type_of / variable_of -----
// 前置声明与定义，完全复制你成功的模式
int fun(int a, int b);

int fun(int const c, int b)
{
    // 测试 parameters_of 和 type_of
    static_assert(!is_const(type_of(parameters_of(^^fun)[0])));
    // 测试 variable_of 必须等于定义中的参数反射
    static_assert(variable_of(parameters_of(^^fun)[0]) == ^^c);

    static_assert(variable_of(parameters_of(^^fun)[0]) == ^^c);
    static_assert(is_const(variable_of(parameters_of(^^fun)[0])));

    return c + b;
}

int gun()
{
    return 0;
}

// 再单独测试 return_type_of（可在函数外进行）
consteval bool test_return_type()
{
    if (return_type_of(^^gun) != ^^int)
        return false;
    if (return_type_of(^^global_func) != ^^int)
        return false;
    if (return_type_of(^^func_template<double>) != ^^void)
        return false;
    return true;
}
static_assert(test_return_type());
int main()
{
    std::cout << "main done\n";
    return 0;
}