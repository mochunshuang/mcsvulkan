#include <meta>
#include <cstddef>

namespace meta = std::meta;

// 辅助类型
using IntRef = int &;

// ────── rank ──────
// 非数组类型 rank == 0
static_assert(meta::rank(^^int) == 0);
static_assert(meta::rank(^^IntRef) == 0);
static_assert(meta::rank(^^void) == 0);

// 一维数组
static_assert(meta::rank(^^int[5]) == 1);
static_assert(meta::rank(^^int[]) == 1); // 未知边界数组

// 多维数组
static_assert(meta::rank(^^int[5][10]) == 2);
static_assert(meta::rank(^^int[5][10][15]) == 3);

// ────── extent ──────
// 非数组类型 extent(I) == 0（包括 I=0）
static_assert(meta::extent(^^int) == 0);
static_assert(meta::extent(^^int, 1) == 0);
static_assert(meta::extent(^^IntRef) == 0);

// 一维数组
static_assert(meta::extent(^^int[5]) == 5);
static_assert(meta::extent(^^int[5], 0) == 5);
static_assert(meta::extent(^^int[5], 1) == 0); // 越界

// 未知边界数组：extent(0) == 0
static_assert(meta::extent(^^int[]) == 0);
static_assert(meta::extent(^^int[], 0) == 0);
static_assert(meta::extent(^^int[], 1) == 0);

// 多维数组
static_assert(meta::extent(^^int[5][10]) == 5);
static_assert(meta::extent(^^int[5][10], 0) == 5);
static_assert(meta::extent(^^int[5][10], 1) == 10);
static_assert(meta::extent(^^int[5][10], 2) == 0);

// 三维数组
static_assert(meta::extent(^^int[5][10][15], 0) == 5);
static_assert(meta::extent(^^int[5][10][15], 1) == 10);
static_assert(meta::extent(^^int[5][10][15], 2) == 15);
static_assert(meta::extent(^^int[5][10][15], 3) == 0);

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