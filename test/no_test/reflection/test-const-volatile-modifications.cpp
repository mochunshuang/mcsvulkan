#include <meta>

namespace meta = std::meta;

// 辅助类型别名（避免 ^^ 直接应用于引用/数组等复杂类型时可能产生的语法歧义）
using Int = int;
using ConstInt = const int;
using VolatileInt = volatile int;
using CVInt = const volatile int;

using Ptr = int *;
using ConstPtr = int *const;
using VolatilePtr = int *volatile;
using CVPtr = int *const volatile;
using PtrToConst = const int *;
using PtrToVolatile = volatile int *;

using Array5 = int[5];
using ConstArray5 = const int[5];
using VolArray5 = volatile int[5];
using CVArray5 = const volatile int[5];

using LRef = int &;
using ConstLRef = const int &;
using VolLRef = volatile int &;
using RRef = int &&;
using ConstRRef = const int &&;

// ── remove_const ──
static_assert(meta::is_same_type(meta::remove_const(^^ConstInt), ^^Int));
static_assert(meta::is_same_type(meta::remove_const(^^CVInt), ^^VolatileInt));
static_assert(meta::is_same_type(meta::remove_const(^^ConstPtr),
                                 ^^Ptr)); // int* const -> int*
static_assert(meta::is_same_type(meta::remove_const(^^CVPtr), ^^VolatilePtr));
static_assert(meta::is_same_type(meta::remove_const(^^PtrToConst),
                                 ^^PtrToConst)); // 无顶层 const
static_assert(meta::is_same_type(meta::remove_const(^^ConstArray5),
                                 ^^Array5)); // const int[5] -> int[5]
static_assert(meta::is_same_type(meta::remove_const(^^LRef), ^^LRef)); // 引用无顶层 const

// ── remove_volatile ──
static_assert(meta::is_same_type(meta::remove_volatile(^^VolatileInt), ^^Int));
static_assert(meta::is_same_type(meta::remove_volatile(^^CVInt), ^^ConstInt));
static_assert(meta::is_same_type(meta::remove_volatile(^^VolatilePtr),
                                 ^^Ptr)); // int* volatile -> int*
static_assert(meta::is_same_type(meta::remove_volatile(^^CVPtr), ^^ConstPtr));
static_assert(meta::is_same_type(meta::remove_volatile(^^PtrToVolatile),
                                 ^^PtrToVolatile)); // 无顶层 volatile
static_assert(meta::is_same_type(meta::remove_volatile(^^VolArray5), ^^Array5));
static_assert(meta::is_same_type(meta::remove_volatile(^^VolLRef), ^^VolLRef));

// ── remove_cv ──
static_assert(meta::is_same_type(meta::remove_cv(^^CVInt), ^^Int));
static_assert(meta::is_same_type(meta::remove_cv(^^CVPtr), ^^Ptr));
static_assert(meta::is_same_type(meta::remove_cv(^^CVArray5), ^^Array5));
static_assert(meta::is_same_type(meta::remove_cv(^^ConstInt), ^^Int));
static_assert(meta::is_same_type(meta::remove_cv(^^Int), ^^Int));

// ── add_const (仅对非引用、非函数类型有效，对引用/函数返回自身) ──
static_assert(meta::is_same_type(meta::add_const(^^Int), ^^ConstInt));
static_assert(meta::is_same_type(meta::add_const(^^VolatileInt), ^^CVInt));
static_assert(meta::is_same_type(meta::add_const(^^Ptr),
                                 ^^ConstPtr)); // int* -> int* const
static_assert(meta::is_same_type(meta::add_const(^^Array5), ^^ConstArray5));
static_assert(meta::is_same_type(meta::add_const(^^LRef), ^^LRef)); // int& -> int&
static_assert(meta::is_same_type(meta::add_const(^^RRef), ^^RRef));

// ── add_volatile ──
static_assert(meta::is_same_type(meta::add_volatile(^^Int), ^^VolatileInt));
static_assert(meta::is_same_type(meta::add_volatile(^^ConstInt), ^^CVInt));
static_assert(meta::is_same_type(meta::add_volatile(^^Ptr), ^^VolatilePtr));
static_assert(meta::is_same_type(meta::add_volatile(^^Array5), ^^VolArray5));
static_assert(meta::is_same_type(meta::add_volatile(^^LRef), ^^LRef));

// ── add_cv ──
static_assert(meta::is_same_type(meta::add_cv(^^Int), ^^CVInt));
static_assert(meta::is_same_type(meta::add_cv(^^Ptr), ^^CVPtr));
static_assert(meta::is_same_type(meta::add_cv(^^Array5), ^^CVArray5));
static_assert(meta::is_same_type(meta::add_cv(^^LRef), ^^LRef));

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