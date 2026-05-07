#include <meta>

namespace meta = std::meta;

// 辅助类型
struct Trivial
{
    int x;
};

struct NonTrivialDefault
{
    NonTrivialDefault() {}
};
struct NonTrivialCopy
{
    NonTrivialCopy(const NonTrivialCopy &) {}
};
struct NonTrivialMove
{
    NonTrivialMove(NonTrivialMove &&) {}
};
struct NonTrivialDestructor
{
    ~NonTrivialDestructor() {}
};
struct NonTrivialCopyAssign
{
    NonTrivialCopyAssign &operator=(const NonTrivialCopyAssign &)
    {
        return *this;
    }
};
struct NonTrivialMoveAssign
{
    NonTrivialMoveAssign &operator=(NonTrivialMoveAssign &&)
    {
        return *this;
    }
};

struct FromInt
{
    FromInt(int) {}
};

// 引用别名
using IntRef = int &;
using TrivialRef = Trivial &;

// ──────────────────────────────────────────────
// 1. is_trivially_default_constructible_type
static_assert(meta::is_trivially_default_constructible_type(^^int));
static_assert(meta::is_trivially_default_constructible_type(^^Trivial));
static_assert(!meta::is_trivially_default_constructible_type(^^NonTrivialDefault));
static_assert(!meta::is_trivially_default_constructible_type(^^void));

// 2. is_trivially_copy_constructible_type
static_assert(meta::is_trivially_copy_constructible_type(^^int));
static_assert(meta::is_trivially_copy_constructible_type(^^Trivial));
static_assert(!meta::is_trivially_copy_constructible_type(^^NonTrivialCopy));
static_assert(!meta::is_trivially_copy_constructible_type(^^void));

// 3. is_trivially_move_constructible_type
static_assert(meta::is_trivially_move_constructible_type(^^int));
static_assert(meta::is_trivially_move_constructible_type(^^Trivial));
static_assert(!meta::is_trivially_move_constructible_type(^^NonTrivialMove));
static_assert(!meta::is_trivially_move_constructible_type(^^void));

// 4. is_trivially_constructible_type (带参数列表)
static_assert(meta::is_trivially_constructible_type(^^int, {
                                                               ^^int}));
static_assert(meta::is_trivially_constructible_type(^^int,
                                                    {
                                                        ^^double})); // 标量平凡构造
static_assert(!meta::is_trivially_constructible_type(
    ^^FromInt, {
                   ^^int})); // 用户定义构造函数非平凡
static_assert(!meta::is_trivially_constructible_type(^^Trivial,
                                                     {
                                                         ^^NonTrivialDefault}));

// 5. is_trivially_copy_assignable_type
static_assert(meta::is_trivially_copy_assignable_type(^^int));
static_assert(meta::is_trivially_copy_assignable_type(^^Trivial));
static_assert(!meta::is_trivially_copy_assignable_type(^^NonTrivialCopyAssign));
static_assert(!meta::is_trivially_copy_assignable_type(^^void));

// 6. is_trivially_move_assignable_type
static_assert(meta::is_trivially_move_assignable_type(^^int));
static_assert(meta::is_trivially_move_assignable_type(^^Trivial));
static_assert(!meta::is_trivially_move_assignable_type(^^NonTrivialMoveAssign));
static_assert(!meta::is_trivially_move_assignable_type(^^void));

// 7. is_trivially_assignable_type (第一个 info 需为左值引用类型)
static_assert(meta::is_trivially_assignable_type(^^IntRef, ^^int));    // int& = int
static_assert(meta::is_trivially_assignable_type(^^IntRef, ^^double)); // int& = double
static_assert(meta::is_trivially_assignable_type(^^TrivialRef, ^^Trivial));
static_assert(!meta::is_trivially_assignable_type(^^IntRef, ^^NonTrivialCopyAssign));
static_assert(!meta::is_trivially_assignable_type(^^TrivialRef, ^^NonTrivialCopyAssign));
static_assert(!meta::is_trivially_assignable_type(^^IntRef, ^^void));

// 8. is_trivially_destructible_type
static_assert(meta::is_trivially_destructible_type(^^int));
static_assert(meta::is_trivially_destructible_type(^^Trivial));
static_assert(meta::is_trivially_destructible_type(^^int[5]));
static_assert(!meta::is_trivially_destructible_type(^^NonTrivialDestructor));
static_assert(!meta::is_trivially_destructible_type(^^void));

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