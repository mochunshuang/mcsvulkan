#include <meta>

namespace meta = std::meta;

// 类型别名
using Int = int;
using PtrInt = int *;
using PtrConstInt = const int *;
using ConstPtrInt = int *const;
using PtrPtrInt = int **;
using PtrFunc = void (*)(int);
using FuncType = void(int);
using PtrArray = int (*)[5];
using ArrayType = int[5];
using RefInt = int &;
using RRefInt = int &&;

// ── remove_pointer ──
// 非指针类型返回自身
static_assert(meta::is_same_type(meta::remove_pointer(^^Int), ^^Int));
static_assert(meta::is_same_type(meta::remove_pointer(^^RefInt), ^^RefInt));
static_assert(meta::is_same_type(meta::remove_pointer(^^RRefInt), ^^RRefInt));

// 指针类型移除指针
static_assert(meta::is_same_type(meta::remove_pointer(^^PtrInt), ^^Int));
static_assert(meta::is_same_type(meta::remove_pointer(^^PtrConstInt), ^^const int));
static_assert(meta::is_same_type(
    meta::remove_pointer(^^ConstPtrInt),
    ^^Int)); // 顶层 const 保留于指针移除？std::remove_pointer<int* const>::type 是 int
static_assert(meta::is_same_type(meta::remove_pointer(^^PtrPtrInt), ^^PtrInt));

// 函数指针移除得到函数类型
static_assert(meta::is_same_type(meta::remove_pointer(^^PtrFunc), ^^FuncType));

// 数组指针移除得到数组类型
static_assert(meta::is_same_type(meta::remove_pointer(^^PtrArray), ^^ArrayType));

// ── add_pointer ──
// 非指针/非引用/非函数类型
static_assert(meta::is_same_type(meta::add_pointer(^^Int), ^^PtrInt));

// 引用类型：add_pointer 生成指向被引用类型的指针
static_assert(meta::is_same_type(meta::add_pointer(^^RefInt), ^^PtrInt));
static_assert(meta::is_same_type(meta::add_pointer(^^RRefInt), ^^PtrInt));

// 函数类型生成函数指针
static_assert(meta::is_same_type(meta::add_pointer(^^FuncType), ^^PtrFunc));

// 数组类型生成数组指针
static_assert(meta::is_same_type(meta::add_pointer(^^ArrayType), ^^PtrArray));

// 已经是指针类型则再加一层
static_assert(meta::is_same_type(meta::add_pointer(^^PtrInt), ^^PtrPtrInt));

// const/volatile 修饰
using ConstInt = const int;
using PtrConstInt2 = const int *;
static_assert(meta::is_same_type(meta::add_pointer(^^ConstInt), ^^PtrConstInt));

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