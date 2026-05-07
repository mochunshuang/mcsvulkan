#include <meta>

namespace meta = std::meta;

// 引用别名
using IntRef = int &;
using ConstIntRef = const int &;
using RvalueRef = int &&;
using ConstRvalueRef = const int &&;

// 可转换为 int 的类型
struct IntConvertible
{
    operator int() const; // 声明即可，traits 无需定义
};

// ────── reference_constructs_from_temporary ──────
// 右值引用绑定同类型纯右值
static_assert(meta::reference_constructs_from_temporary(^^RvalueRef, ^^int));
// const 左值引用绑定纯右值
static_assert(meta::reference_constructs_from_temporary(^^ConstIntRef, ^^int));
// 非常量左值引用禁止绑定临时对象
static_assert(!meta::reference_constructs_from_temporary(^^IntRef, ^^int));
// 绑定需要隐式转换的临时对象（long→int）
static_assert(meta::reference_constructs_from_temporary(^^ConstIntRef, ^^long));
// int&& 不能直接绑定 long 纯右值（类型不同且无引用兼容性）
static_assert(meta::reference_constructs_from_temporary(^^RvalueRef, ^^long));
// 需要用户定义转换产生临时对象，直接构造不支持
static_assert(meta::reference_constructs_from_temporary(^^ConstIntRef, ^^IntConvertible));

// ────── reference_converts_from_temporary ──────
// 同类型直接转换
static_assert(meta::reference_converts_from_temporary(^^RvalueRef, ^^int));
static_assert(meta::reference_converts_from_temporary(^^ConstIntRef, ^^int));
// long → int 发生转换，生成临时 int 绑定
static_assert(meta::reference_converts_from_temporary(^^RvalueRef, ^^long));
static_assert(meta::reference_converts_from_temporary(^^ConstIntRef, ^^long));
// 用户定义转换生成临时 int，即可绑定
static_assert(meta::reference_converts_from_temporary(^^RvalueRef, ^^IntConvertible));
static_assert(meta::reference_converts_from_temporary(^^ConstIntRef, ^^IntConvertible));
// 非常量左值引用依然不能绑定临时
static_assert(!meta::reference_converts_from_temporary(^^IntRef, ^^int));
static_assert(!meta::reference_converts_from_temporary(^^IntRef, ^^long));

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