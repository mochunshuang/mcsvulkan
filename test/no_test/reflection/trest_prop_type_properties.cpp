#include <iostream>
#include <meta>
#include <type_traits> // for std::is_* verification references

namespace meta = std::meta;

// ===== 辅助类型定义 =====
struct Trivial
{
    int x;
};
struct NonTrivial
{
    ~NonTrivial() {}
};

struct StdLayout
{
    int a;
    double b;
};
struct NotStd
{
    virtual void f();
};

struct Empty
{
};

struct Poly
{
    virtual void f() {}
};

struct Abstract
{
    virtual void f() = 0;
};

struct Final final
{
};

struct Aggr
{
    int x;
};
struct NonAggr
{
    NonAggr(int) {}
};

enum class Scoped
{
    x
};
enum Unscoped
{
    y
};

// 常量/易变引用别名用于类型测试
using ConstInt = const int;
using VolatileInt = volatile int;
using ConstVolatileInt = const volatile int;
using ConstRef = const int &;
using ConstPtr = int *const;
using PtrToConst = const int *;
using VolatilePtr = int *volatile;

// ===== [meta.unary.prop] 类型属性测试 =====

// 1. is_const_type
static_assert(meta::is_const_type(^^ConstInt));
static_assert(meta::is_const_type(^^ConstVolatileInt));
static_assert(meta::is_const_type(^^ConstPtr)); // int * const —— 常量指针
static_assert(!meta::is_const_type(^^int));
static_assert(!meta::is_const_type(^^ConstRef));   // 引用没有顶层const
static_assert(!meta::is_const_type(^^PtrToConst)); // 指向常量的指针不是常量类型

// 2. is_volatile_type
static_assert(meta::is_volatile_type(^^VolatileInt));
static_assert(meta::is_volatile_type(^^ConstVolatileInt));
static_assert(meta::is_volatile_type(^^VolatilePtr)); // int * volatile
static_assert(!meta::is_volatile_type(^^int));
static_assert(!meta::is_volatile_type(^^ConstInt));
static_assert(!meta::is_volatile_type(^^const int *));

// 3. is_trivially_copyable_type
static_assert(meta::is_trivially_copyable_type(^^int));
static_assert(meta::is_trivially_copyable_type(^^Trivial));
static_assert(meta::is_trivially_copyable_type(^^int[5]));
static_assert(!meta::is_trivially_copyable_type(^^NonTrivial));

// 4. is_standard_layout_type
static_assert(meta::is_standard_layout_type(^^int));
static_assert(meta::is_standard_layout_type(^^StdLayout));
static_assert(!meta::is_standard_layout_type(^^NotStd));

// 5. is_empty_type
static_assert(meta::is_empty_type(^^Empty));
static_assert(!meta::is_empty_type(^^Trivial)); // 有成员
static_assert(!meta::is_empty_type(^^int));

// 6. is_polymorphic_type
static_assert(meta::is_polymorphic_type(^^Poly));
static_assert(!meta::is_polymorphic_type(^^Trivial));
static_assert(!meta::is_polymorphic_type(^^int));

// 7. is_abstract_type
static_assert(meta::is_abstract_type(^^Abstract));
static_assert(!meta::is_abstract_type(^^Poly)); // 虚函数但非纯虚
static_assert(!meta::is_abstract_type(^^int));

// 8. is_final_type
static_assert(meta::is_final_type(^^Final));
static_assert(!meta::is_final_type(^^Trivial));
static_assert(!meta::is_final_type(^^int));

// 9. is_aggregate_type
static_assert(meta::is_aggregate_type(^^Aggr));
static_assert(meta::is_aggregate_type(^^int[5]));
static_assert(!meta::is_aggregate_type(^^NonAggr));
static_assert(!meta::is_aggregate_type(^^int)); // 标量不是聚合

// 10. is_structural_type (如果编译器支持)
#if __glibcxx_is_structural >= 202603L
static_assert(meta::is_structural_type(^^int));
static_assert(!meta::is_structural_type(^^int[5]));
static_assert(meta::is_structural_type(
    ^^Empty)); // 空类可能是 structural（若所有成员public且无虚函数等）
static_assert(!meta::is_structural_type(^^std::string)); // string 不满足
#endif

// 11. is_signed_type
static_assert(meta::is_signed_type(^^int));
static_assert(meta::is_signed_type(^^signed char));
static_assert(meta::is_signed_type(^^long));
static_assert(!meta::is_signed_type(^^unsigned int));
static_assert(!meta::is_signed_type(^^unsigned char));
static_assert(!meta::is_signed_type(^^bool)); // bool 无符号

// 12. is_unsigned_type
static_assert(meta::is_unsigned_type(^^unsigned int));
static_assert(meta::is_unsigned_type(^^unsigned char));
static_assert(meta::is_unsigned_type(^^bool));
static_assert(!meta::is_unsigned_type(^^int));
static_assert(!meta::is_unsigned_type(^^signed char));

// 13. is_bounded_array_type
static_assert(meta::is_bounded_array_type(^^int[5]));
static_assert(meta::is_bounded_array_type(^^int[5][10]));
static_assert(!meta::is_bounded_array_type(^^int[])); // 无界数组
static_assert(!meta::is_bounded_array_type(^^int));

// 14. is_unbounded_array_type
static_assert(meta::is_unbounded_array_type(^^int[]));
static_assert(!meta::is_unbounded_array_type(^^int[5]));
static_assert(!meta::is_unbounded_array_type(^^int));

// 15. is_scoped_enum_type
static_assert(meta::is_scoped_enum_type(^^Scoped));
static_assert(!meta::is_scoped_enum_type(^^Unscoped));
static_assert(!meta::is_scoped_enum_type(^^int));

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