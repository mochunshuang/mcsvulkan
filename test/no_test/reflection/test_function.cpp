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

int main()
{
    std::cout << "main done\n";
    return 0;
}