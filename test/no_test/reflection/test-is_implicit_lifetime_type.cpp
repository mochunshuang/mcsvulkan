#include <meta>

namespace meta = std::meta;

// 辅助类型
struct ImplicitLifetime
{
    int x;
}; // 平凡
struct NonLifetime
{
    NonLifetime() {}
}; // 用户定义的构造函数

struct NonTrivialDestructor
{
    ~NonTrivialDestructor() {}
};
struct Aggregate
{
    int a;
    double b;
};

// ─── is_implicit_lifetime_type ───
static_assert(meta::is_implicit_lifetime_type(^^int));
static_assert(meta::is_implicit_lifetime_type(^^double));
static_assert(meta::is_implicit_lifetime_type(^^ImplicitLifetime));
static_assert(meta::is_implicit_lifetime_type(^^Aggregate));
static_assert(meta::is_implicit_lifetime_type(^^int[5]));
static_assert(meta::is_implicit_lifetime_type(^^int[]));

static_assert(meta::is_implicit_lifetime_type(^^NonLifetime));
static_assert(!meta::is_implicit_lifetime_type(^^NonTrivialDestructor));
static_assert(!meta::is_implicit_lifetime_type(^^void));
static_assert(!meta::is_implicit_lifetime_type(^^int &));

#include <iostream>
#include <exception>

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
}