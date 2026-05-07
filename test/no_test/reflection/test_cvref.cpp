#include <iostream>
#include <meta>
using namespace std::meta;

// 测试类型限定
static_assert(is_const(^^const int));
static_assert(is_const(^^const volatile int));
static_assert(!is_const(^^int));
static_assert(!is_const(^^volatile int));

static_assert(is_volatile(^^volatile int));
static_assert(is_volatile(^^const volatile int));
static_assert(!is_volatile(^^int));
static_assert(!is_volatile(^^const int));

// 测试变量和成员
const int g_const = 1;
volatile int g_volatile;
const volatile int g_cv{};

struct Qualifiers
{
    const int ci;
    volatile int vi;
    const volatile int cvi;
    mutable int mi;
    int plain;

    void func_lref() & {}
    void func_rref() && {}
    void func_both() const & {} // const & 是左值引用限定
    void func_none() {}
};

static_assert(is_const(^^g_const));
static_assert(is_const(^^Qualifiers::ci));
static_assert(is_const(^^Qualifiers::cvi));
static_assert(!is_const(^^g_volatile));
static_assert(!is_const(^^Qualifiers::vi));
static_assert(!is_const(^^Qualifiers::plain));

static_assert(is_volatile(^^g_volatile));
static_assert(is_volatile(^^Qualifiers::vi));
static_assert(is_volatile(^^Qualifiers::cvi));
static_assert(!is_volatile(^^g_const));
static_assert(!is_volatile(^^Qualifiers::ci));
static_assert(!is_volatile(^^Qualifiers::plain));

static_assert(is_mutable_member(^^Qualifiers::mi));
static_assert(!is_mutable_member(^^Qualifiers::ci));
static_assert(!is_mutable_member(^^Qualifiers::vi));
static_assert(!is_mutable_member(^^Qualifiers::plain));
static_assert(!is_mutable_member(^^g_const)); // 非成员

static_assert(is_lvalue_reference_qualified(^^Qualifiers::func_lref));
static_assert(is_lvalue_reference_qualified(^^Qualifiers::func_both)); // const &
static_assert(!is_lvalue_reference_qualified(^^Qualifiers::func_rref));
static_assert(!is_lvalue_reference_qualified(^^Qualifiers::func_none));

static_assert(is_rvalue_reference_qualified(^^Qualifiers::func_rref));
static_assert(!is_rvalue_reference_qualified(^^Qualifiers::func_lref));
static_assert(!is_rvalue_reference_qualified(^^Qualifiers::func_both));
static_assert(!is_rvalue_reference_qualified(^^Qualifiers::func_none));

int main()
{
    std::cout << "main done\n";
    return 0;
}