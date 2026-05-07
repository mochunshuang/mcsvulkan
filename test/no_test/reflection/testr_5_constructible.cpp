#include <meta>

namespace meta = std::meta;

struct NoCopyAssign
{
    NoCopyAssign &operator=(const NoCopyAssign &) = delete;
};

struct NoMoveOrCopyAssign
{
    NoMoveOrCopyAssign &operator=(NoMoveOrCopyAssign &&) = delete;
    NoMoveOrCopyAssign &operator=(const NoMoveOrCopyAssign &) = delete;
};

struct Swappable
{
};
struct NonSwappable
{
    NonSwappable() = default;
    NonSwappable(const NonSwappable &) = delete;
    NonSwappable &operator=(const NonSwappable &) = delete;
};

struct HasDestructor
{
    ~HasDestructor() {}
};
struct NoDestructor
{
    ~NoDestructor() = delete;
};

using IntRef = int &;
using ConstIntRef = const int &;

// 1. is_copy_assignable_type
static_assert(meta::is_copy_assignable_type(^^int));
static_assert(meta::is_copy_assignable_type(^^Swappable));
static_assert(!meta::is_copy_assignable_type(^^NoCopyAssign));
static_assert(!meta::is_copy_assignable_type(^^void));

// 2. is_move_assignable_type
static_assert(meta::is_move_assignable_type(^^int));
static_assert(meta::is_move_assignable_type(^^Swappable));
static_assert(
    !meta::is_move_assignable_type(^^NoMoveOrCopyAssign)); // 彻底删除移动和拷贝赋值
static_assert(!meta::is_move_assignable_type(^^void));

// 3. is_assignable_type (第一个 info 必须为左值引用类型)
static_assert(meta::is_assignable_type(^^IntRef, ^^int));       // int& = int
static_assert(meta::is_assignable_type(^^IntRef, ^^double));    // int& = double
static_assert(!meta::is_assignable_type(^^ConstIntRef, ^^int)); // const int& 不能被赋值
static_assert(!meta::is_assignable_type(^^IntRef, ^^void));
static_assert(!meta::is_assignable_type(^^NoCopyAssign, ^^NoCopyAssign));

// 4. is_swappable_type
static_assert(meta::is_swappable_type(^^int));
static_assert(meta::is_swappable_type(^^Swappable));
static_assert(!meta::is_swappable_type(^^NonSwappable));
static_assert(!meta::is_swappable_type(^^void));

// 5. is_swappable_with_type (同样，可能需要左值引用类型)
static_assert(meta::is_swappable_with_type(^^IntRef, ^^IntRef));
static_assert(!meta::is_swappable_with_type(^^IntRef, ^^double));
static_assert(!meta::is_swappable_with_type(^^IntRef, ^^void));
static_assert(!meta::is_swappable_with_type(^^NonSwappable, ^^NonSwappable));

// 6. is_destructible_type (数组类型正确返回 true)
static_assert(meta::is_destructible_type(^^int));
static_assert(meta::is_destructible_type(^^int[5]));
static_assert(meta::is_destructible_type(^^HasDestructor));
static_assert(!meta::is_destructible_type(^^NoDestructor));
static_assert(!meta::is_destructible_type(^^void));

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