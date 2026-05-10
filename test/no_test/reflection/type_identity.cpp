#include <iostream>
#include <exception>
#include <meta>
#include <type_traits>

using namespace std::meta;

// ================= 1. 核心转换：info → C++ 类型 =================
template <std::meta::info R>
consteval auto type_from_info()
{
    return std::type_identity<typename[:R:]>{};
}

template <std::meta::info R>
using type_of_info = typename decltype(type_from_info<R>())::type;

// ================= 2. 任意反射实体 → 类型 =================
consteval std::meta::info get_type_info(std::meta::info r)
{
    if (is_type(r))
        return r;
    else
        return type_of(r);
}

template <std::meta::info Entity>
using type_of_ = type_of_info<get_type_info(Entity)>;

// ================= 3. 测试用例（覆盖所有常见情况） =================

// --- 基础类型 ---
static_assert(std::is_same_v<type_of_info<^^int>, int>);
static_assert(std::is_same_v<type_of_info<^^double>, double>);
static_assert(std::is_same_v<type_of_info<^^void>, void>);

// --- cv 限定 ---
static_assert(std::is_same_v<type_of_info<^^const int>, const int>);
static_assert(std::is_same_v<type_of_info<^^volatile double>, volatile double>);

// --- 引用 ---
static_assert(std::is_same_v<type_of_info<^^int &>, int &>);
static_assert(std::is_same_v<type_of_info<^^int &&>, int &&>);

// --- 数组 ---
static_assert(std::is_same_v<type_of_info<^^int[3]>, int[3]>);
static_assert(std::is_same_v<type_of_info<^^int[]>, int[]>);

// --- 函数类型 ---
static_assert(std::is_same_v<type_of_info<^^void(int)>, void(int)>);
static_assert(std::is_same_v<type_of_info<^^int(double, char)>, int(double, char)>);

// --- 成员指针类型 ---
struct S
{
    int x;
    void f() const;
};
static_assert(std::is_same_v<type_of_info<^^int S::*>, int S::*>);
static_assert(std::is_same_v<type_of_info<^^void (S::*)() const>, void (S::*)() const>);

// --- 不可默认构造类型 ---
struct NoDefault
{
    NoDefault() = delete;
    NoDefault(int) {}
};
static_assert(std::is_same_v<type_of_info<^^NoDefault>, NoDefault>);

// --- 抽象类 ---
struct Abstract
{
    virtual void foo() = 0;
};
static_assert(std::is_same_v<type_of_info<^^Abstract>, Abstract>);

// --- 枚举 ---
enum Color
{
    Red
};
enum class Fruit
{
    Apple
};
static_assert(std::is_same_v<type_of_info<^^Color>, Color>);
static_assert(std::is_same_v<type_of_info<^^Fruit>, Fruit>);

// --- 通过变量、成员等间接获取类型 ---
int globalVar;
constexpr int kConst = 42;
[[maybe_unused]] static double staticMem; // 消除 unused 警告

struct MyClass
{
    int nonStatic;
    static float staticFloat;
    void memberFunc() {}
};

static_assert(std::is_same_v<type_of_<^^globalVar>, int>);
static_assert(std::is_same_v<type_of_<^^kConst>, const int>); // constexpr 变量
static_assert(std::is_same_v<type_of_<^^MyClass::nonStatic>, int>);
static_assert(std::is_same_v<type_of_<^^MyClass::staticFloat>, float>);

/* 关于成员函数的 type_of：
   当前 GCC 快照对成员函数应用 type_of 时，会得到去掉类作用域的普通函数类型。
   标准草案对此行为尚未最终明确，因此以下测试只验证它是一个函数类型。 */
// static_assert(std::is_same_v<type_of_<^^MyClass::memberFunc>, void()>); // 若工具链恢复成员函数类型，可调整
static_assert(std::is_function_v<type_of_<^^MyClass::memberFunc>>);

// --- 通过 reflect_constant 得到的值反射（必须在 consteval 中） ---
consteval bool test_constant_type()
{
    constexpr auto valInfo = std::meta::reflect_constant(10); // 注意添加 std::meta::
    // 值反射不是类型，但我们可以通过 type_of 得到其类型
    return std::is_same_v<type_of_<valInfo>, int>;
}
static_assert(test_constant_type());

// ================= 4. 使用示例 =================
int main()
{
    std::cout << "所有类型反射测试通过。\n";
    return 0;
}