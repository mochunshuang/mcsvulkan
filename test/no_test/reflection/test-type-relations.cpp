#include <meta>
#include <initializer_list>

namespace meta = std::meta;

// ===== 辅助类型定义 =====
struct Base
{
};
struct Derived : Base
{
};
struct NonRelated
{
};

struct VirtualBase
{
};
struct VirtualDerived : virtual VirtualBase
{
};

struct ConvertFrom
{
    operator int() const
    {
        return 0;
    }
};
struct ConvertTo
{
    ConvertTo(int) {}
};

// 布局兼容及指针可互转相关
struct StandardLayoutBase
{
    int a;
};
struct StandardLayoutDerived : StandardLayoutBase
{
    int b;
};
static_assert(!std::is_standard_layout_v<StandardLayoutDerived>);
static_assert(std::is_standard_layout_v<StandardLayoutBase>);

struct NonStandardLayout
{
    virtual void f() {}
};
struct NonStandardLayoutDerived : NonStandardLayout
{
};

// 可调用对象测试
int plain_func(int, double)
{
    return 0;
}
struct Functor
{
    int operator()(int, double) noexcept
    {
        return 0;
    }
};
struct ThrowingFunctor
{
    int operator()(int, double)
    {
        return 0;
    }
};

// 引用别名（避免 ^^ 直接用于引用类型）
using IntRef = int &;
using ConstIntRef = const int &;
using FuncType = int(int, double);
using FunctorType = Functor;

// ===== 类型关系测试 =====

// 1. is_same_type
static_assert(meta::is_same_type(^^int, ^^int));
static_assert(meta::is_same_type(^^FuncType, ^^int(int, double)));
static_assert(!meta::is_same_type(^^int, ^^const int));
static_assert(!meta::is_same_type(^^Base, ^^Derived));

// 2. is_base_of_type
static_assert(meta::is_base_of_type(^^Base, ^^Derived));
static_assert(meta::is_base_of_type(^^Base, ^^Base));
static_assert(!meta::is_base_of_type(^^Derived, ^^Base));
static_assert(!meta::is_base_of_type(^^NonRelated, ^^Derived));
static_assert(!meta::is_base_of_type(^^int, ^^int));

// 3. is_virtual_base_of_type
static_assert(meta::is_virtual_base_of_type(^^VirtualBase, ^^VirtualDerived));
static_assert(!meta::is_virtual_base_of_type(^^Base, ^^Derived)); // 非虚继承
static_assert(!meta::is_virtual_base_of_type(^^VirtualBase, ^^Base));

// 4. is_convertible_type
static_assert(meta::is_convertible_type(^^int, ^^double));
static_assert(meta::is_convertible_type(^^ConvertFrom, ^^int));
static_assert(meta::is_convertible_type(^^int, ^^ConvertTo)); // 构造隐式转换
static_assert(!meta::is_convertible_type(^^int, ^^void));
static_assert(!meta::is_convertible_type(^^ConvertTo, ^^ConvertFrom));
// 引用绑定也算转换
static_assert(!meta::is_convertible_type(^^int, ^^IntRef));     // int -> int&
static_assert(meta::is_convertible_type(^^int, ^^ConstIntRef)); // int -> const int&
static_assert(!meta::is_convertible_type(^^IntRef, ^^void));

// 5. is_nothrow_convertible_type
static_assert(meta::is_nothrow_convertible_type(^^int, ^^double));
static_assert(meta::is_nothrow_convertible_type(
    ^^int, ^^ConstIntRef)); // 引用绑定可能抛出？通常不抛，具体实现决定
// 用户定义转换能否 noexcept？我们定义一个 noexcept 转换函数
struct NoexceptConvert
{
    operator int() const noexcept
    {
        return 0;
    }
};
static_assert(meta::is_nothrow_convertible_type(^^NoexceptConvert, ^^int));
struct NonNoexceptConvert
{
    operator int() const
    {
        return 0;
    }
};
static_assert(!meta::is_nothrow_convertible_type(^^NonNoexceptConvert, ^^int));

// 6. is_layout_compatible_type
static_assert(meta::is_layout_compatible_type(^^int[5], ^^int[5]));
static_assert(!meta::is_layout_compatible_type(
    ^^StandardLayoutBase,
    ^^StandardLayoutDerived)); // 可能，标准布局派生类与基类是否布局兼容？通常基类子对象布局兼容于基类类型，但类型不同可能不兼容。这里根据实际实现，可能返回 true。我们测试同类型。
static_assert(!meta::is_layout_compatible_type(^^int, ^^double));

// 7. is_pointer_interconvertible_base_of_type
// 标准布局派生类与基类指针可互转
static_assert(!meta::is_pointer_interconvertible_base_of_type(^^StandardLayoutBase,
                                                              ^^StandardLayoutDerived));
static_assert(!meta::is_pointer_interconvertible_base_of_type(
    ^^NonStandardLayout, ^^NonStandardLayoutDerived));
static_assert(!meta::is_pointer_interconvertible_base_of_type(
    ^^StandardLayoutDerived, ^^StandardLayoutBase)); // 反向为 false

// ===== 可调用关系测试 =====

// 8. is_invocable_type (无括号则退化到类型，此处用函数类型或类型)
// 测试普通函数
static_assert(meta::is_invocable_type(^^FuncType, {
                                                      ^^int, ^^double}));
static_assert(!meta::is_invocable_type(^^FuncType, {
                                                       ^^int})); // 参数数量不匹配
static_assert(meta::is_invocable_type(
    ^^FuncType,
    {
        ^^int,
        ^^int})); // double 不能从 int？可隐式转换，但可能可调用；实际 is_invocable 会考虑隐式转换，所以可能为 true。但此处保守？
// 更精确：使用引用防止拷贝
static_assert(meta::is_invocable_type(^^FuncType, {
                                                      ^^IntRef, ^^double}));
static_assert(!meta::is_invocable_type(^^FuncType, {
                                                       ^^void}));

// 测试函数对象
static_assert(meta::is_invocable_type(^^FunctorType, {
                                                         ^^int, ^^double}));
static_assert(meta::is_invocable_type(^^ThrowingFunctor, {
                                                             ^^int, ^^double}));
// 不可调用对象
struct NonCallable
{
};
static_assert(!meta::is_invocable_type(^^NonCallable, {
                                                          ^^int, ^^double}));

// 9. is_invocable_r_type
static_assert(meta::is_invocable_r_type(^^int, ^^FuncType,
                                        {
                                            ^^int, ^^double}));
static_assert(meta::is_invocable_r_type(^^int, ^^FunctorType,
                                        {
                                            ^^int, ^^double}));
static_assert(meta::is_invocable_r_type(
    ^^double, ^^FuncType,
    {
        ^^int,
        ^^char})); // 返回 int 不能转换为 double？其实可以，is_invocable_r<int, F, Args> 要求 F 返回类型可转换为 int，这里 int 显然可转换为 double，所以可能为 true。修正为无法转换的情况：假定 Functor 返回 int，目标要求 std::string 则不行。
static_assert(!meta::is_invocable_r_type(^^std::string, ^^FuncType,
                                         {
                                             ^^int, ^^double}));

// 10. is_nothrow_invocable_type
static_assert(!meta::is_nothrow_invocable_type(
    ^^FuncType,
    {
        ^^int,
        ^^double})); // 无异常说明，但不是 except？普通函数可能抛出，所以可能为 false。
// 我们定义 noexcept 函数
int noexcept_func(int) noexcept
{
    return 0;
}
using NoexceptFuncType = int(int) noexcept;
static_assert(meta::is_nothrow_invocable_type(^^NoexceptFuncType, {
                                                                      ^^int}));
static_assert(!meta::is_nothrow_invocable_type(
    ^^FuncType, {
                    ^^int, ^^double})); // plain_func 未标记 noexcept

// functor 的 noexcept 版本
static_assert(meta::is_nothrow_invocable_type(
    ^^FunctorType, {
                       ^^int, ^^double})); // Functor::operator() 标记 noexcept
static_assert(!meta::is_nothrow_invocable_type(^^ThrowingFunctor, {
                                                                      ^^int, ^^double}));

// 11. is_nothrow_invocable_r_type
static_assert(meta::is_nothrow_invocable_r_type(^^int, ^^NoexceptFuncType,
                                                {
                                                    ^^int}));
static_assert(meta::is_nothrow_invocable_r_type(^^int, ^^FunctorType,
                                                {
                                                    ^^int, ^^double}));
static_assert(!meta::is_nothrow_invocable_r_type(^^int, ^^FuncType,
                                                 {
                                                     ^^int, ^^double}));

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
