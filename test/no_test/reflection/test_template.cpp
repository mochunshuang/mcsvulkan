#include <meta>
#include <iostream>
using namespace std::meta;

// ===== 全局模板 =====
template <typename T>
struct MyClass
{
}; // 类模板
template <typename T>
void myFunc()
{
} // 函数模板
template <typename T>
T myVar = T{}; // 变量模板
template <typename T>
using MyAlias = T; // 别名模板
template <typename T>
concept MyConcept = true; // 概念

// 全局字面量运算符模板（正确的形式）
template <char... Chars>
constexpr auto operator""_lit()
{
    return sizeof...(Chars);
}

// ===== 类，包含成员模板 =====
struct S
{
    template <typename T>
    void f()
    {
    } // 函数模板
    template <typename T>
    operator T()
    {
        return T{};
    } // 转换函数模板
    template <typename T>
    void operator+(T)
    {
    } // 运算符函数模板
    template <typename T>
    S(T)
    {
    } // 构造函数模板
};

// 辅助函数：从 S 的成员中提取指定模板反射
consteval info get_member_template(info class_info, bool (*pred)(info))
{
    for (info mem : members_of(class_info, access_context::current()))
        if (pred(mem))
            return mem;
    return {};
}

consteval info get_S_func_template()
{
    return get_member_template(^^S, [](info m) {
      return has_identifier(m) && identifier_of(m) == "f" && is_function_template(m);
    });
}
consteval info get_S_conv_template()
{
    return get_member_template(^^S, [](info m) {
      return is_conversion_function_template(m);
    });
}
consteval info get_S_op_template()
{
    return get_member_template(^^S, [](info m) {
      return is_operator_function_template(m);
    });
}
consteval info get_S_ctor_template()
{
    return get_member_template(^^S, [](info m) {
      return is_constructor_template(m);
    });
}

// ===== 静态断言 =====

// 全局模板的 is_template
static_assert(is_template(^^MyClass));
static_assert(is_template(^^myFunc));
static_assert(is_template(^^myVar));
static_assert(is_template(^^MyAlias));
static_assert(is_template(^^MyConcept));
static_assert(is_template(^^operator""_lit));
static_assert(!is_template(^^MyClass<int>));

// 细分类型
static_assert(is_class_template(^^MyClass));
static_assert(is_function_template(^^myFunc));
static_assert(is_variable_template(^^myVar));
static_assert(is_alias_template(^^MyAlias));
static_assert(is_concept(^^MyConcept));
static_assert(is_literal_operator_template(^^operator""_lit));

// 成员模板
constexpr info S_func_tmpl = get_S_func_template();
constexpr info S_conv_tmpl = get_S_conv_template();
constexpr info S_op_tmpl = get_S_op_template();
constexpr info S_ctor_tmpl = get_S_ctor_template();

// 这些 info 有效（函数模板有标识符）
static_assert(has_identifier(S_func_tmpl));
static_assert(!has_identifier(S_op_tmpl));
// 转换函数模板可能无标识符，不强制 has_identifier

static_assert(is_template(S_func_tmpl));
static_assert(is_template(S_conv_tmpl));
static_assert(is_template(S_op_tmpl));
static_assert(is_template(S_ctor_tmpl));

static_assert(is_function_template(S_func_tmpl));
static_assert(is_function_template(S_ctor_tmpl)); // 构造函数模板也是函数模板
static_assert(is_constructor_template(S_ctor_tmpl));
static_assert(is_conversion_function_template(S_conv_tmpl));
static_assert(is_operator_function_template(S_op_tmpl));

int main()
{
    std::cout << "main done\n";
    return 0;
}