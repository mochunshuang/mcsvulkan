#include <meta>

namespace meta = std::meta;

// 辅助类型
struct HasDefault
{
};
struct NoDefault
{
    NoDefault() = delete;
};

struct Copyable
{
};
struct NoCopy
{
    NoCopy(const NoCopy &) = delete;
};

struct Movable
{
};
struct NoMove
{
    NoMove(NoMove &&) = delete;
};

struct FromInt
{
    FromInt(int) {}
};
struct FromIntDouble
{
    FromIntDouble(int, double) {}
};

// 引用类型别名（避免 ^^ 直接应用于引用产生解析问题）
using IntRef = int &;

// ──────────────────────────────────────────────
// 1. is_default_constructible_type
static_assert(meta::is_default_constructible_type(^^int));
static_assert(meta::is_default_constructible_type(^^HasDefault));
static_assert(meta::is_default_constructible_type(^^int[5])); // int a[5]{}; 合法
static_assert(!meta::is_default_constructible_type(^^NoDefault));
static_assert(!meta::is_default_constructible_type(^^void));

// 2. is_copy_constructible_type
static_assert(meta::is_copy_constructible_type(^^int));
static_assert(meta::is_copy_constructible_type(^^Copyable));
static_assert(!meta::is_copy_constructible_type(^^NoCopy));
static_assert(!meta::is_copy_constructible_type(^^void));

// 3. is_move_constructible_type
static_assert(meta::is_move_constructible_type(^^int));
static_assert(meta::is_move_constructible_type(^^Movable));
static_assert(!meta::is_move_constructible_type(^^NoMove));
static_assert(!meta::is_move_constructible_type(^^void));

// 4. is_constructible_type 带参数列表
// 空参数（值初始化）
static_assert(meta::is_constructible_type(^^int, {
                                                 }));
static_assert(meta::is_constructible_type(^^HasDefault, {
                                                        }));
static_assert(meta::is_constructible_type(^^int[5], {
                                                    })); // 正确：int a[5]{}; 是值初始化
static_assert(!meta::is_constructible_type(^^NoDefault, {
                                                        }));
static_assert(!meta::is_constructible_type(^^void, {
                                                   }));

// 单一参数
static_assert(meta::is_constructible_type(^^int, {
                                                     ^^int}));
static_assert(meta::is_constructible_type(
    ^^int, {
               ^^double})); // int 可从 double 构造（窄化隐式转换）
static_assert(!meta::is_constructible_type(^^int, {
                                                      ^^void}));
static_assert(meta::is_constructible_type(^^FromInt, {
                                                         ^^int}));
static_assert(meta::is_constructible_type(
    ^^FromInt, {
                   ^^double})); // double -> int -> FromInt，合法

// 多参数
static_assert(meta::is_constructible_type(^^FromIntDouble, {
                                                               ^^int, ^^double}));
static_assert(meta::is_constructible_type(^^FromIntDouble,
                                          {
                                              ^^int, ^^int})); // int -> double 隐式转换
static_assert(!meta::is_constructible_type(^^FromIntDouble,
                                           {
                                               ^^int})); // 参数数量不匹配

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