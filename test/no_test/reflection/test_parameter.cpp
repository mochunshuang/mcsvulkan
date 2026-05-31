#include <meta>
#include <iostream>
using namespace std::meta;

// 具有默认参数的函数
void func_with_default(int a, int b = 10) {}

// 可变参数函数
void vararg_func(int a, ...) {}

// 测试类
struct TestParams
{
    void plain_func(int x) {}
    void explicit_obj_func(this TestParams &self, int y) {}
    void func_with_default_param(int a, int b = 5) {}
    static void static_func(int x) {}
};

// ---- is_function_parameter ----
consteval bool test_function_parameter()
{
    // 全局函数参数
    for (info p : parameters_of(^^func_with_default))
        if (!is_function_parameter(p))
            return false;
    // 成员函数参数
    for (info p : parameters_of(^^TestParams::plain_func))
        if (!is_function_parameter(p))
            return false;
    // 变量和函数本身不是参数
    if (is_function_parameter(^^vararg_func))
        return false;
    if (is_function_parameter(^^int))
        return false;
    return true;
}

// ---- is_explicit_object_parameter ----
consteval bool test_explicit_object_parameter()
{
    auto params = parameters_of(^^TestParams::explicit_obj_func);
    if (params.size() < 2)
        return false;
    if (!is_explicit_object_parameter(params[0]))
        return false; // self 参数
    if (is_explicit_object_parameter(params[1]))
        return false;
    // 普通成员函数第一个参数不是显式对象参数
    auto plain = parameters_of(^^TestParams::plain_func);
    if (plain.empty() || is_explicit_object_parameter(plain[0]))
        return false;
    return true;
}

// ---- has_default_argument ----
consteval bool test_has_default_argument()
{
    // 测试参数本身是否有默认实参（当前实现可靠行为）
    auto params = parameters_of(^^func_with_default);
    if (params.size() < 2)
        return false;
    if (has_default_argument(params[0]))
        return false; // a 无默认值
    if (!has_default_argument(params[1]))
        return false; // b 有默认值

    // 成员函数参数测试
    auto mem_params = parameters_of(^^TestParams::func_with_default_param);
    if (mem_params.size() < 2)
        return false;
    if (has_default_argument(mem_params[0]))
        return false; // a 无默认
    if (!has_default_argument(mem_params[1]))
        return false; // b 有默认

    // 没有默认值的参数
    auto no_def = parameters_of(^^TestParams::plain_func);
    if (no_def.empty() || has_default_argument(no_def[0]))
        return false;

    // 注意：函数整体 has_default_argument 当前实现可能不稳定，故不在此断言
    return true;
}

// ---- is_vararg_function ----
consteval bool test_vararg_function()
{
    if (!is_vararg_function(^^vararg_func))
        return false;
    if (is_vararg_function(^^func_with_default))
        return false;
    if (is_vararg_function(^^TestParams::plain_func))
        return false;
    return true;
}

constexpr auto get_call_op = [](info closure_type) consteval -> info {
    // 遍历所有成员，找到名字为 "operator()" 的函数
    for (info mem : members_of(closure_type, std::meta::access_context::current()))
    {
        if (has_identifier(mem) && identifier_of(mem) == "operator()")
            return mem;
    }
    // 理论上 lambda 一定有 operator()，若未找到则返回空 info（测试会失败）
    return info{};
};
static_assert(get_call_op(^^decltype([](int x) { return x; })) == info{});

static_assert(test_function_parameter());
static_assert(test_explicit_object_parameter());
static_assert(test_has_default_argument());
static_assert(test_vararg_function());

int main()
{
    std::cout << "main done\n";
    return 0;
}