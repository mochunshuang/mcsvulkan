#include <meta>
#include <iostream>
#include <string_view>
using namespace std::meta;

// 测试结构体
struct TestFunctions
{
    void f() {} // 普通成员函数
    operator int() const
    {
        return 0;
    } // 转换到 int
    explicit operator bool() const
    {
        return true;
    } // 转换到 bool
    TestFunctions &operator=(const TestFunctions &)
    {
        return *this;
    } // 拷贝赋值
    TestFunctions &operator=(TestFunctions &&)
    {
        return *this;
    } // 移动赋值
    TestFunctions() = default;                      // 默认构造
    TestFunctions(const TestFunctions &) = default; // 拷贝构造
    TestFunctions(TestFunctions &&) = default;      // 移动构造
    TestFunctions(int) {}                           // 带参数构造
    ~TestFunctions() = default;                     // 析构

    // ===== 公共辅助函数：返回各成员函数的 info =====
    static consteval info get_f()
    {
        for (auto mem : members_of(^^TestFunctions, access_context::current()))
            if (has_identifier(mem) && identifier_of(mem) == "f")
                return mem;
        return {};
    }
    static consteval info get_conv_int()
    {
        for (auto mem : members_of(^^TestFunctions, access_context::current()))
            if (is_conversion_function(mem) && return_type_of(mem) == ^^int)
                return mem;
        return {};
    }
    static consteval info get_conv_bool()
    {
        for (auto mem : members_of(^^TestFunctions, access_context::current()))
            if (is_conversion_function(mem) && return_type_of(mem) == ^^bool)
                return mem;
        return {};
    }
    static consteval info get_copy_assign()
    {
        for (auto mem : members_of(^^TestFunctions, access_context::current()))
            if (is_copy_assignment(mem))
                return mem;
        return {};
    }
    static consteval info get_move_assign()
    {
        for (auto mem : members_of(^^TestFunctions, access_context::current()))
            if (is_move_assignment(mem))
                return mem;
        return {};
    }
    static consteval info get_default_ctor()
    {
        for (auto mem : members_of(^^TestFunctions, access_context::current()))
            if (is_default_constructor(mem))
                return mem;
        return {};
    }
    static consteval info get_copy_ctor()
    {
        for (auto mem : members_of(^^TestFunctions, access_context::current()))
            if (is_copy_constructor(mem))
                return mem;
        return {};
    }
    static consteval info get_move_ctor()
    {
        for (auto mem : members_of(^^TestFunctions, access_context::current()))
            if (is_move_constructor(mem))
                return mem;
        return {};
    }
    static consteval info get_int_ctor()
    {
        for (auto mem : members_of(^^TestFunctions, access_context::current()))
            if (is_constructor(mem) && !is_default_constructor(mem) &&
                !is_copy_constructor(mem) && !is_move_constructor(mem))
                return mem;
        return {};
    }
    static consteval info get_dtor()
    {
        for (auto mem : members_of(^^TestFunctions, access_context::current()))
            if (is_destructor(mem))
                return mem;
        return {};
    }
};

// 全局函数
void global_func() {}
void operator+(int, const TestFunctions &) {} // 重载的 +
void operator""_test(unsigned long long) {}   // 字面量运算符

// 提取成员 info
constexpr info f_info = TestFunctions::get_f();
constexpr info conv_int_info = TestFunctions::get_conv_int();
constexpr info conv_bool_info = TestFunctions::get_conv_bool();
constexpr info copy_assign_info = TestFunctions::get_copy_assign();
constexpr info move_assign_info = TestFunctions::get_move_assign();
constexpr info default_ctor_info = TestFunctions::get_default_ctor();
constexpr info copy_ctor_info = TestFunctions::get_copy_ctor();
constexpr info move_ctor_info = TestFunctions::get_move_ctor();
constexpr info int_ctor_info = TestFunctions::get_int_ctor();
constexpr info dtor_info = TestFunctions::get_dtor();

// 全局函数的反射
constexpr info g_func_info = ^^global_func;
constexpr info g_op_plus_info = ^^operator+; // 重载的是全局 operator+
constexpr info g_lit_op_info = ^^operator""_test;

// ==================== 静态断言 ====================
// is_function
static_assert(is_function(f_info));
static_assert(is_function(conv_int_info));
static_assert(is_function(copy_assign_info));
static_assert(is_function(default_ctor_info));
static_assert(is_function(dtor_info));
static_assert(is_function(g_func_info));
static_assert(is_function(g_op_plus_info));
static_assert(!is_function(^^int));
static_assert(!is_function(^^TestFunctions));

// is_conversion_function
static_assert(is_conversion_function(conv_int_info));
static_assert(is_conversion_function(conv_bool_info));
static_assert(!is_conversion_function(f_info));
static_assert(!is_conversion_function(copy_assign_info));

// is_operator_function
static_assert(is_operator_function(copy_assign_info));
static_assert(is_operator_function(move_assign_info));
static_assert(is_operator_function(g_op_plus_info)); // 全局运算符
static_assert(!is_operator_function(conv_int_info)); // 转换函数不是运算符
static_assert(!is_operator_function(f_info));
static_assert(!is_operator_function(default_ctor_info));

// is_literal_operator
static_assert(is_literal_operator(g_lit_op_info));
static_assert(!is_literal_operator(g_func_info));
static_assert(!is_literal_operator(conv_int_info));

// is_special_member_function
static_assert(is_special_member_function(default_ctor_info));
static_assert(is_special_member_function(copy_ctor_info));
static_assert(is_special_member_function(move_ctor_info));
static_assert(is_special_member_function(copy_assign_info));
static_assert(is_special_member_function(move_assign_info));
static_assert(is_special_member_function(dtor_info));
static_assert(!is_special_member_function(int_ctor_info)); // 非默认/拷贝/移动构造
static_assert(!is_special_member_function(f_info));

// is_constructor
static_assert(is_constructor(default_ctor_info));
static_assert(is_constructor(copy_ctor_info));
static_assert(is_constructor(move_ctor_info));
static_assert(is_constructor(int_ctor_info));
static_assert(!is_constructor(dtor_info));

// is_default_constructor
static_assert(is_default_constructor(default_ctor_info));
static_assert(!is_default_constructor(copy_ctor_info));
static_assert(!is_default_constructor(int_ctor_info));

// is_copy_constructor
static_assert(is_copy_constructor(copy_ctor_info));
static_assert(!is_copy_constructor(default_ctor_info));
static_assert(!is_copy_constructor(move_ctor_info));

// is_move_constructor
static_assert(is_move_constructor(move_ctor_info));
static_assert(!is_move_constructor(copy_ctor_info));
static_assert(!is_move_constructor(default_ctor_info));

// is_assignment
static_assert(is_assignment(copy_assign_info));
static_assert(is_assignment(move_assign_info));
static_assert(!is_assignment(f_info));

// is_copy_assignment
static_assert(is_copy_assignment(copy_assign_info));
static_assert(!is_copy_assignment(move_assign_info));

// is_move_assignment
static_assert(is_move_assignment(move_assign_info));
static_assert(!is_move_assignment(copy_assign_info));

// is_destructor
static_assert(is_destructor(dtor_info));
static_assert(!is_destructor(f_info));

// ============ Lambda 反射测试 ============
consteval bool test_lambda_reflection()
{
    // 1. 简单无捕获 lambda
    {
        auto simple = [](int x) {
            return x;
        };
        // 获取 operator() 的反射
        constexpr info op = ^^decltype(simple)::operator();

        // 是函数
        if (!is_function(op))
            return false;

        // 是运算符函数
        if (!is_operator_function(op))
            return false;
        // 不是转换函数
        if (is_conversion_function(op))
            return false;
        // 不是字面量运算符
        if (is_literal_operator(op))
            return false;
        // 参数列表
        constexpr auto params = std::define_static_array(parameters_of(op));
        if (params.size() != 1)
            return false;
        if (!is_function_parameter(params[0]))
            return false;
        static_assert(std::string_view{"x"} == std::string_view{"x"});
        static_assert(std::meta::identifier_of(params[0]) == std::string_view{"x"});
    }

    return true;
}

static_assert(test_lambda_reflection());

// ================= 模板夹具 =================
struct TemplateFixture
{
    template <class T>
    void tmpl_func(T)
    {
    } // 模板成员函数

    template <class T>
    T tmpl_op_plus(T)
    {
        return {};
    } // 注意：名字带 op，但它是普通函数模板，不是 operator

    template <class T>
    operator T() const
    {
        return {};
    } // 模板转换函数（T）

    template <class T>
    explicit operator T *() const
    {
        return nullptr;
    } // 模板转换函数（T*）

    template <class T>
    TemplateFixture(T)
    {
    } // 模板构造函数

    template <class T>
    static T s_tmpl_var; // 模板静态数据成员

    template <class T>
    bool operator<(const T &) const
    {
        return false;
    } // 真正的运算符函数模板（成员）
};

// 自由函数模板
template <class T>
void free_tmpl_func(T)
{
}
template <class T>
T free_tmpl_op(T, T)
{
    return {};
} // 名字带 op，其实不是 operator
template <class T>
T free_tmpl_var = {};

// 真正的自由运算符函数模板
namespace op_ns
{
    template <class T>
    T operator+(T, T)
    {
        return {};
    }
} // namespace op_ns

// 类模板 / 别名模板 / 变量模板 / concept
template <class T>
struct ClassTmpl
{
};
template <class T>
using AliasTmpl = T *;
template <class T>
T VarTmpl = {};
template <class T>
concept ConceptTmpl = std::is_integral_v<T>;

// 字面量运算符模板
template <char... Cs>
auto operator""_tmpl()
{
    return 0;
}

// ================= 辅助：按谓词从 members_of 里过滤 =================
// ^^X::X、^^X::operator T 这类名可能构成重载集，无法直接反射，
// 只能遍历 members_of 再用 is_*_template 谓词过滤。
template <class Pred>
consteval info find_if_member(info cls, Pred pred)
{
    for (auto m : members_of(cls, access_context::current()))
        if (pred(m))
            return m;
    return {};
}

// ================= 模板相关静态断言 =================

// ---- is_function_template ----
static_assert(is_function_template(^^free_tmpl_func));
static_assert(is_function_template(^^TemplateFixture::tmpl_func));
static_assert(!is_function_template(^^global_func));     // 普通函数
static_assert(!is_function_template(^^TemplateFixture)); // 类不是

// ---- is_constructor_template ----
// ^^TemplateFixture::TemplateFixture 是重载集（默认+拷贝+移动+模板），
// 无法直接反射，必须用谓词过滤。
constexpr info ctor_tmpl = find_if_member(^^TemplateFixture, [](info m) {
  return is_constructor_template(m);
});
static_assert(ctor_tmpl != info{});
static_assert(is_constructor_template(ctor_tmpl)); // 二次确认

// ---- is_conversion_function_template ----
// ^^TemplateFixture::operator int 也是重载集（operator T / operator T*），
// 同样只能谓词过滤。
constexpr info conv_tmpl = find_if_member(^^TemplateFixture, [](info m) {
  return is_conversion_function_template(m);
});
static_assert(conv_tmpl != info{});
static_assert(is_conversion_function_template(conv_tmpl));

// ---- is_operator_function_template ----
// free_tmpl_op 是普通函数模板，不是 operator → 为假
static_assert(!is_operator_function_template(^^free_tmpl_op));
static_assert(is_operator_function_template(^^op_ns::operator+)); // 自由 operator+
// 成员 operator< 也只能通过谓词过滤拿到
constexpr info op_mem_tmpl = find_if_member(^^TemplateFixture, [](info m) {
  return is_operator_function_template(m);
});
static_assert(op_mem_tmpl != info{});
static_assert(is_operator_function_template(op_mem_tmpl));

// ---- is_literal_operator_template ----
static_assert(is_literal_operator_template(^^operator""_tmpl));

// ---- is_variable_template ----
static_assert(is_variable_template(^^free_tmpl_var));
static_assert(is_variable_template(^^TemplateFixture::s_tmpl_var));

// ---- is_class_template ----
static_assert(is_class_template(^^ClassTmpl));

// ---- is_alias_template ----
static_assert(is_alias_template(^^AliasTmpl));

// ---- is_concept ----
static_assert(is_concept(^^ConceptTmpl));

// ---- is_template（宽泛版）----
static_assert(is_template(^^free_tmpl_func));
static_assert(is_template(^^ClassTmpl));
static_assert(is_template(^^AliasTmpl));
static_assert(is_template(^^VarTmpl));
static_assert(is_template(^^ConceptTmpl));
static_assert(!is_template(^^global_func));
static_assert(!is_template(^^TestFunctions));

// ---- 区分「模板本身」与「实例化后的函数」----
static_assert(is_function_template(^^free_tmpl_op));          // 模板本身
static_assert(!is_function_template(^^free_tmpl_op<int>));    // 实例化后
static_assert(!is_function_template(^^free_tmpl_op<double>)); // 另一个实例化

// ---- 模板参数分解 ----
constexpr info inst = ^^free_tmpl_op<int>; // 一次实例化后的函数
static_assert(has_template_arguments(inst));
static_assert(template_of(inst) == ^^free_tmpl_op);
static_assert(template_arguments_of(inst).size() == 1);
static_assert(template_arguments_of(inst)[0] == ^^int);

int main()
{
    std::cout << "main done\n";
    return 0;
}