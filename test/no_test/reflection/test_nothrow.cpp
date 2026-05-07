#include <meta>
#include <initializer_list>

namespace meta = std::meta;

// ---- 辅助类型 (nothrow 系列) ----
struct NothrowDef
{
    NothrowDef() noexcept {}
};
struct MayThrowDef
{
    MayThrowDef() {}
};

struct NothrowCopy
{
    NothrowCopy(const NothrowCopy &) noexcept {}
};
struct MayThrowCopy
{
    MayThrowCopy(const MayThrowCopy &) {}
};

struct NothrowMove
{
    NothrowMove(NothrowMove &&) noexcept {}
};
struct MayThrowMove
{
    MayThrowMove(MayThrowMove &&) {}
};

struct NothrowDest
{
    ~NothrowDest() noexcept {}
};
struct MayThrowDest
{
    ~MayThrowDest() noexcept(false) {}
};

struct NothrowCopyAssign
{
    NothrowCopyAssign &operator=(const NothrowCopyAssign &) noexcept
    {
        return *this;
    }
};
struct MayThrowCopyAssign
{
    MayThrowCopyAssign &operator=(const MayThrowCopyAssign &)
    {
        return *this;
    }
};

struct NothrowMoveAssign
{
    NothrowMoveAssign &operator=(NothrowMoveAssign &&) noexcept
    {
        return *this;
    }
};
struct MayThrowMoveAssign
{
    MayThrowMoveAssign &operator=(MayThrowMoveAssign &&)
    {
        return *this;
    }
};

struct Swappable
{
};
struct NothrowSwappable
{
    friend void swap(NothrowSwappable &, NothrowSwappable &) noexcept {}
};

struct FromInt
{
    FromInt(int) {}
};
struct FromIntNoexcept
{
    FromIntNoexcept(int) noexcept {}
};

// 引用别名
using IntRef = int &;
using NothrowCopyAssignRef = NothrowCopyAssign &;
using NothrowSwappableRef = NothrowSwappable &;

// ---- 测试 is_nothrow_default_constructible_type ----
static_assert(meta::is_nothrow_default_constructible_type(^^int));
static_assert(meta::is_nothrow_default_constructible_type(^^NothrowDef));
static_assert(!meta::is_nothrow_default_constructible_type(^^MayThrowDef));
static_assert(!meta::is_nothrow_default_constructible_type(^^void));

// ---- 测试 is_nothrow_copy_constructible_type ----
static_assert(meta::is_nothrow_copy_constructible_type(^^int));
static_assert(meta::is_nothrow_copy_constructible_type(^^NothrowCopy));
static_assert(!meta::is_nothrow_copy_constructible_type(^^MayThrowCopy));
static_assert(!meta::is_nothrow_copy_constructible_type(^^void));

// ---- 测试 is_nothrow_move_constructible_type ----
static_assert(meta::is_nothrow_move_constructible_type(^^int));
static_assert(meta::is_nothrow_move_constructible_type(^^NothrowMove));
static_assert(!meta::is_nothrow_move_constructible_type(^^MayThrowMove));
static_assert(!meta::is_nothrow_move_constructible_type(^^void));

// ---- 测试 is_nothrow_constructible_type (带参数列表) ----
static_assert(meta::is_nothrow_constructible_type(^^int, {
                                                             ^^int}));
static_assert(meta::is_nothrow_constructible_type(^^int, {
                                                             ^^double}));
static_assert(meta::is_nothrow_constructible_type(^^FromIntNoexcept, {
                                                                         ^^int}));
static_assert(!meta::is_nothrow_constructible_type(^^FromInt,
                                                   {
                                                       ^^int})); // 非 noexcept 构造
static_assert(!meta::is_nothrow_constructible_type(
    ^^FromInt, {
                   ^^double})); // 隐式转换需要非 noexcept

// ---- 测试 is_nothrow_copy_assignable_type ----
static_assert(meta::is_nothrow_copy_assignable_type(^^int));
static_assert(meta::is_nothrow_copy_assignable_type(^^NothrowCopyAssign));
static_assert(!meta::is_nothrow_copy_assignable_type(^^MayThrowCopyAssign));
static_assert(!meta::is_nothrow_copy_assignable_type(^^void));

// ---- 测试 is_nothrow_move_assignable_type ----
static_assert(meta::is_nothrow_move_assignable_type(^^int));
static_assert(meta::is_nothrow_move_assignable_type(^^NothrowMoveAssign));
static_assert(!meta::is_nothrow_move_assignable_type(^^MayThrowMoveAssign));
static_assert(!meta::is_nothrow_move_assignable_type(^^void));

// ---- 测试 is_nothrow_assignable_type (第一个参数需左值引用) ----
static_assert(meta::is_nothrow_assignable_type(^^IntRef, ^^int));
static_assert(meta::is_nothrow_assignable_type(^^IntRef, ^^double));
static_assert(meta::is_nothrow_assignable_type(^^NothrowCopyAssignRef,
                                               ^^NothrowCopyAssign));
static_assert(!meta::is_nothrow_assignable_type(^^IntRef, ^^MayThrowCopyAssign));
static_assert(!meta::is_nothrow_assignable_type(^^IntRef, ^^void));

// ---- 测试 is_nothrow_swappable_type ----
static_assert(meta::is_nothrow_swappable_type(^^int));
static_assert(meta::is_nothrow_swappable_type(^^NothrowSwappable));
static_assert(meta::is_nothrow_swappable_type(^^Swappable)); // 未标记 noexcept
static_assert(!meta::is_nothrow_swappable_type(^^void));

// ---- 测试 is_nothrow_swappable_with_type (需左值引用类型) ----
static_assert(meta::is_nothrow_swappable_with_type(^^IntRef, ^^IntRef));
static_assert(!meta::is_nothrow_swappable_with_type(^^IntRef, ^^double));
static_assert(meta::is_nothrow_swappable_with_type(^^NothrowSwappableRef,
                                                   ^^NothrowSwappableRef));
static_assert(!meta::is_nothrow_swappable_with_type(^^IntRef, ^^void));
static_assert(!meta::is_nothrow_swappable_with_type(^^Swappable, ^^Swappable));

// ---- 测试 is_nothrow_destructible_type ----
static_assert(meta::is_nothrow_destructible_type(^^int));
static_assert(meta::is_nothrow_destructible_type(^^NothrowDest));
static_assert(meta::is_nothrow_destructible_type(^^int[5]));
static_assert(!meta::is_nothrow_destructible_type(^^MayThrowDest));
static_assert(!meta::is_nothrow_destructible_type(^^void));

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