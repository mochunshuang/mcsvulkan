#include <meta>
#include <span>
#include <string_view>
using namespace std::meta;
using namespace std::string_view_literals;

// ========== 测试类 ==========
struct S
{
    void f() {}
    void f_noexcept() noexcept {}

    S() = default;
    S(const S &) = delete;
    S(S &&) = default;
    S &operator=(const S &)
    {
        return *this;
    }
    S &operator=(S &&) = delete;
    ~S() = default;

    explicit S(int) {}
    explicit operator bool() const
    {
        return true;
    }
    void plain() {}

    // ----- 返回特殊成员 info 的公开辅助函数 -----
    static consteval info get_default_ctor()
    {
        for (auto mem : members_of(^^S, access_context::current()))
            if (is_default_constructor(mem))
                return mem;
        return {};
    }
    static consteval info get_copy_ctor()
    {
        for (auto mem : members_of(^^S, access_context::current()))
            if (is_copy_constructor(mem))
                return mem;
        return {};
    }
    static consteval info get_move_ctor()
    {
        for (auto mem : members_of(^^S, access_context::current()))
            if (is_move_constructor(mem))
                return mem;
        return {};
    }
    static consteval info get_dtor()
    {
        for (auto mem : members_of(^^S, access_context::current()))
            if (is_destructor(mem))
                return mem;
        return {};
    }
    static consteval info get_copy_assign()
    {
        for (auto mem : members_of(^^S, access_context::current()))
            if (is_copy_assignment(mem))
                return mem;
        return {};
    }
    static consteval info get_move_assign()
    {
        for (auto mem : members_of(^^S, access_context::current()))
            if (is_move_assignment(mem))
                return mem;
        return {};
    }
    // 显式转换构造函数（非默认/拷贝/移动）
    static consteval info get_explicit_ctor()
    {
        for (auto mem : members_of(^^S, access_context::current()))
            if (is_constructor(mem) && !is_default_constructor(mem) &&
                !is_copy_constructor(mem) && !is_move_constructor(mem))
                return mem;
        return {};
    }
    // 转换函数 operator bool
    static consteval info get_conv_op()
    {
        for (auto mem : members_of(^^S, access_context::current()))
            if (is_conversion_function(mem))
                return mem;
        return {};
    }
};

// 全局函数
void globalFunc() {}
void globalFuncNoexcept() noexcept {}
void globalFuncDeleted() = delete;

// ========== 提取特殊成员 info ==========
constexpr info ctor_def = S::get_default_ctor();
constexpr info ctor_copy = S::get_copy_ctor();
constexpr info ctor_move = S::get_move_ctor();
constexpr info dtor = S::get_dtor();
constexpr info op_copy = S::get_copy_assign();
constexpr info op_move = S::get_move_assign();
constexpr info ctor_exp = S::get_explicit_ctor();
constexpr info conv_op = S::get_conv_op();

// 普通成员可直接用 ^^ 反射
constexpr info normal = ^^S::f;
constexpr info normal_noex = ^^S::f_noexcept;
constexpr info plain = ^^S::plain;

// 全局函数
constexpr info g_normal = ^^globalFunc;
constexpr info g_noexcept = ^^globalFuncNoexcept;
constexpr info g_deleted = ^^globalFuncDeleted;

// ========== 静态断言测试 ==========

// ---- is_deleted ----
static_assert(is_deleted(ctor_copy));
static_assert(is_deleted(op_move));
static_assert(!is_deleted(ctor_def));
static_assert(!is_deleted(dtor));
static_assert(!is_deleted(normal));
static_assert(is_deleted(g_deleted));
static_assert(!is_deleted(g_normal));

// ---- is_defaulted ----
static_assert(is_defaulted(ctor_def));
static_assert(is_defaulted(ctor_move));
static_assert(is_defaulted(dtor));
static_assert(!is_defaulted(ctor_copy));
static_assert(!is_defaulted(op_copy));

// ---- is_user_provided ----
static_assert(is_user_provided(normal));
static_assert(is_user_provided(normal_noex));
static_assert(!is_user_provided(ctor_def));
static_assert(!is_user_provided(ctor_copy));
static_assert(!is_user_provided(ctor_move));
static_assert(!is_user_provided(dtor));
static_assert(is_user_provided(op_copy));
static_assert(is_user_provided(g_normal));
static_assert(!is_user_provided(g_deleted));

// ---- is_user_declared ----
static_assert(is_user_declared(normal));
static_assert(is_user_declared(ctor_def));
static_assert(is_user_declared(ctor_copy));
static_assert(is_user_declared(ctor_move));
static_assert(is_user_declared(dtor));
static_assert(is_user_declared(op_copy));
static_assert(is_user_declared(g_deleted));

// ---- is_explicit ----
static_assert(is_explicit(ctor_exp));
static_assert(is_explicit(conv_op));
static_assert(!is_explicit(plain));

// ---- is_noexcept ----
static_assert(is_noexcept(normal_noex));
static_assert(!is_noexcept(normal));
static_assert(is_noexcept(ctor_def)); // 未指定 noexcept. 但是默认就是无异常的
static_assert(is_noexcept(g_noexcept));
static_assert(!is_noexcept(g_normal));

int main()
{
    return 0;
}