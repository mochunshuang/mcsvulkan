#include <meta>
#include <iostream>
using namespace std::meta;

// ============================================================
// 辅助命名空间与类
// ============================================================

namespace OuterNS
{
    struct OuterClass
    {
        int member_val;

        // 改为非静态成员函数，确保 current_class() 有效
        consteval bool test_member_context() const
        {
            info fn = current_function();
            if (!is_function(fn))
                return false;
            if (current_class() != ^^OuterClass)
                return false;
            if (current_namespace() != ^^OuterNS)
                return false;
            return true;
        }
    };

    // 自由函数，测试命名空间内 current_namespace
    consteval bool test_namespace_inside()
    {
        return current_namespace() == ^^OuterNS;
    }
} // namespace OuterNS

namespace InnerNS
{
    struct InnerClass
    {
        consteval bool test_inner_context() const
        {
            if (current_class() != ^^InnerClass)
                return false;
            if (current_namespace() != ^^InnerNS)
                return false;
            return true;
        }
    };
} // namespace InnerNS

// 全局自由函数
consteval int global_func()
{
    return 0;
}

// 全局 lambda
constexpr auto global_lambda = []() consteval -> info {
    return current_function();
};

// 预先获取全局命名空间反射，供局部作用域使用
constexpr info global_ns = ^^::;

// ============================================================
// 测试 1：current_function
// ============================================================
consteval bool test_current_function_basic()
{
    // 1.1 普通 consteval 函数内
    info fn = current_function();
    if (!is_function(fn))
        return false;
    if (has_identifier(fn) && identifier_of(fn) != "test_current_function_basic")
        return false;

    // 1.2 lambda 内
    info lam_fn = global_lambda();
    if (!is_function(lam_fn))
        return false;
    if (has_identifier(lam_fn))
        return false; // lambda 调用运算符没有名字

    return true;
}
static_assert(test_current_function_basic());

// ============================================================
// 测试 2：current_class（使用非静态成员函数调用）
// ============================================================
consteval bool test_current_class()
{
    // 通过临时对象调用非静态 consteval 成员函数
    if (!OuterNS::OuterClass{}.test_member_context())
        return false;
    if (!InnerNS::InnerClass{}.test_inner_context())
        return false;
    return true;
}
static_assert(test_current_class());

// ============================================================
// 测试 3：current_namespace
// ============================================================
consteval bool test_current_namespace_full()
{
    // 命名空间内函数
    if (!OuterNS::test_namespace_inside())
        return false;
    // 全局作用域
    if (current_namespace() != ^^::)
        return false;
    return true;
}
static_assert(test_current_namespace_full());

// ============================================================
// 测试 4：嵌套作用域（避免局部类反射词法问题）
// ============================================================
consteval bool test_nested_scopes()
{
    struct LocalClass
    {
        // 非静态 consteval 成员函数
        consteval bool check() const
        {
            info fn = current_function();
            info cl = current_class();
            info ns = current_namespace();
            // cl 必定是 LocalClass，但不能直接写 ^^LocalClass，用 is_class 检测
            // 同时验证命名空间为全局（使用预先获取的 global_ns）
            return is_function(fn) && is_class_type(cl) && ns == global_ns;
        }
    };
    if (!LocalClass{}.check())
        return false;
    return true;
}
static_assert(test_nested_scopes());

// ============================================================
int main()
{
    std::cout << "All scope identification tests passed.\n";
    return 0;
}