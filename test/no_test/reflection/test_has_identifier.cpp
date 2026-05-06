#include <iostream>
#include <exception>

#include <meta>
#include <utility>

using namespace std;
using namespace std::meta;

// ==================== 基础辅助结构与声明 ====================
//当且仅当反射的实体是一个由声明引入的具名实体时返回 true —— 即该实体在源码中有自己的“名字”，包括变量、函数、类、枚举、枚举常量、命名空间、别名等；
// 而匿名类型（lambda 闭包、匿名结构体/联合体）、未命名函数参数、值反射、模板实例化产生的隐式特化类型、以及 int* 这类组合类型都应当返回 false。

// ---------- 基础具名实体 没有名字----------
static_assert(not has_identifier(^^int)); // 基本类型是具名的
static_assert(not has_identifier(^^double));
struct S
{
};
static_assert(has_identifier(^^S));
enum E
{
    e0,
    e1
};
static_assert(has_identifier(^^E));
static_assert(has_identifier(^^e0)); // 枚举常量有名字
enum class EC
{
    x
};
static_assert(has_identifier(^^EC));
static_assert(has_identifier(^^EC::x));

namespace ns
{
}
static_assert(has_identifier(^^ns));  // 具名命名空间
static_assert(!has_identifier(^^::)); // 全局命名空间无名

int globalVar;
static_assert(has_identifier(^^globalVar));

void func();
static_assert(has_identifier(^^func));

template <typename T>
struct Temp
{
};
static_assert(has_identifier(^^Temp)); // 类模板本身有名字

template <typename T>
void tfunc()
{
}
static_assert(has_identifier(^^tfunc)); // 函数模板有名字

// ---------- 别名 ----------
using IntAlias = int;
static_assert(has_identifier(^^IntAlias)); // 别名有名字
typedef double DoubleAlias;
static_assert(has_identifier(^^DoubleAlias));

// ---------- 成员 ----------
struct M
{
    int x;
    static int y;
    void f();
    int operator+(int);
};
static_assert(has_identifier(^^M::x)); // 非静态数据成员
static_assert(has_identifier(^^M::y)); // 静态数据成员
static_assert(has_identifier(^^M::f)); // 成员函数

//NOTE: 特殊函数好像是不能又名字的？
static_assert(not has_identifier(^^M::operator+)); // 运算符函数

// ---------- 匿名类型 ----------
// 匿名结构体
struct
{
    int a;
} anonStruct;
static_assert(!has_identifier(^^decltype(anonStruct)));

// 匿名联合体
union {
    int x;
    double y;
} anonUnion;
static_assert(!has_identifier(^^decltype(anonUnion)));

// 匿名枚举（变量声明直接给出匿名枚举类型）
enum
{
    ae = 0
} anonEnumVar;
static_assert(!has_identifier(^^decltype(anonEnumVar)));

// Lambda 闭包类型
static_assert(!has_identifier(^^decltype([] {})));
static_assert(!has_identifier(^^decltype([](int) -> int { return 0; })));
using TL = decltype([] {}); //NOTE: 可以存放尼玛类型
static_assert(has_identifier(^^TL));
constexpr auto lambda = []() {
};
static_assert(has_identifier(^^lambda)); //lambda 有类型，但是一般都是新类型

// 组合类型（指针、引用、数组、函数）
int arr[3];
int *ptr = arr;
int &ref = arr[0];
void f();
static_assert(has_identifier(^^f));
static_assert(!has_identifier(^^decltype(arr)));
static_assert(!has_identifier(^^decltype(ptr)));
static_assert(!has_identifier(^^decltype(ref)));
static_assert(!has_identifier(^^decltype(f))); // 函数类型

static_assert(has_identifier(^^Temp));
// 模板实例化（隐式特化应从无名）
static_assert(!has_identifier(^^Temp<int>));

// // ---------- 值反射 ----------
// static_assert(!has_identifier(^^42)); //NOTE: 语法错误
// static_assert(!has_identifier(^^true));
static_assert(!has_identifier(reflect_constant(42)));
static_assert(!has_identifier(reflect_constant(true)));

// 具名变量的对象反射（同样无名）：
int someVar = 0;
static_assert(!has_identifier(reflect_object(someVar)));
static_assert(has_identifier(^^someVar));

// ---------- 函数参数 ----------
constexpr bool testParam()
{
    auto lam = [](int namedParam) constexpr {
        return has_identifier(^^namedParam); // true
    };
    return lam(0);
}
static_assert(testParam()); // 有名参数为 true

int main()
try
{
    //NOTE: 一句话应该是 定义且有名的。才有？目前测试是这样，不知道后面如何修正
    // NOTE:has_identifier(info) 返回 true 当且仅当该反射直接对应一个由源码中的声明引入的、有名字的实体（即一个“具名实体”）。
    // 否则（无名字的实体、组合类型、匿名类型、值反射、对象反射等）返回 false
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