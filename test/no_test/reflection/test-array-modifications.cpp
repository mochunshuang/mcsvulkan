#include <meta>

namespace meta = std::meta;

// 类型别名以避免 ^^ 语法歧义
using Int = int;
using Array5 = int[5];
using ArrayUnknown = int[];
using Array2D = int[5][10];
using Array3D = int[5][10][15];
using RefArray = int (&)[5];
using RefArray2D = int (&)[5][10];

// ── remove_extent ──
// 非数组类型：返回自身
static_assert(meta::is_same_type(meta::remove_extent(^^Int), ^^Int));
// 一维有界数组 -> 元素类型
static_assert(meta::is_same_type(meta::remove_extent(^^Array5), ^^Int));
// 一维无界数组 -> 元素类型
static_assert(meta::is_same_type(meta::remove_extent(^^ArrayUnknown), ^^Int));
// 多维数组 -> 去掉最外层维度，得到次层数组
static_assert(meta::is_same_type(meta::remove_extent(^^Array2D), ^^int[10]));
static_assert(meta::is_same_type(meta::remove_extent(^^Array3D), ^^int[10][15]));
// 数组引用不属于数组类型，remove_extent 应返回原类型
static_assert(meta::is_same_type(meta::remove_extent(^^RefArray), ^^RefArray));
static_assert(meta::is_same_type(meta::remove_extent(^^RefArray2D), ^^RefArray2D));

// ── remove_all_extents ──
// 非数组 -> 自身
static_assert(meta::is_same_type(meta::remove_all_extents(^^Int), ^^Int));
// 一维数组 -> 元素
static_assert(meta::is_same_type(meta::remove_all_extents(^^Array5), ^^Int));
static_assert(meta::is_same_type(meta::remove_all_extents(^^ArrayUnknown), ^^Int));
// 多维数组 -> 最内层元素
static_assert(meta::is_same_type(meta::remove_all_extents(^^Array2D), ^^Int));
static_assert(meta::is_same_type(meta::remove_all_extents(^^Array3D), ^^Int));
// 数组引用保持原类型
static_assert(meta::is_same_type(meta::remove_all_extents(^^RefArray), ^^RefArray));

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