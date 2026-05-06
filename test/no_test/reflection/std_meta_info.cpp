#include <iostream>
#include <exception>
#include <meta>

using namespace std;
using namespace std::meta;

// ====================== 4.3.1 Comparing reflections =========================
// 类型std::meta::info是等式和不等式有意义的标量类型，但没有定义排序关系。
static_assert(^^int == ^^int);
static_assert(^^int != ^^const int);
static_assert(^^int != ^^int &);

// NOTE: dealias 才比较底层
using Alias = int;
static_assert(^^int != ^^Alias);
static_assert(^^int == dealias(^^Alias));

namespace AliasNS = ::std;
static_assert(^^::std != ^^AliasNS);
static_assert(^^:: == parent_of(^^::std));

// 1.当^^运算符后面跟着一个id表达式时，生成的std::meta::info表示由表达式命名的实体。只有当它们反映相同的实体时，这种反射才是等价的。
int x;
struct S
{
    static int y;
};
static_assert(^^x == ^^x);
static_assert(^^x != ^^S::y);
static_assert(^^S::y == static_data_members_of(^^S, access_context::current())[0]);

// 2.当比较某些类型的反射时，应用特殊规则。当且仅当一个别名的反射都是别名、别名类型相同并且共享相同的名称和范围时，它们才等于另一个反射。
// 特别是，这些规则允许例如，fn<^^std::string>跨翻译单元引用相同的实例化。
using Alias1 = int;
using Alias2 = int;
consteval std::meta::info fn()
{
    using Alias1 = int;
    return ^^Alias;
}
static_assert(^^Alias1 == ^^Alias1);
static_assert(^^Alias1 != ^^int);

//NOTE: 当前是相等的。 目前别名是相等的。 和文档相反，可能是未发布
// static_assert(^^Alias1 != ^^Alias2);
// static_assert(^^Alias1 != fn());

static_assert(dealias(^^Alias2) == dealias(^^Alias1));
static_assert(dealias(^^Alias1) == dealias(fn()));

// 3.对象（包括变量）的反射与其值的反射不相等。不同类型的两个值永远不会相等地比较。
constexpr int i = 42, j = 42;

constexpr std::meta::info r = ^^i, s = ^^i;
static_assert(r == s);
static_assert(std::meta::info{^^i} == std::meta::info{^^i});

// NOTE: 编译期常量的类型，看来是有唯一性的。 类型变量用不相同
static_assert(std::meta::info{^^i} != std::meta::info{^^j});
static_assert(^^i != ^^j); // 'i' and 'j' are different entities.
static_assert(constant_of(^^i) == constant_of(^^j)); // Two equivalent values.

static_assert(^^i != std::meta::reflect_object(i));    // A variable is distinct from the
                                                       // object it designates.
static_assert(^^i != std::meta::reflect_constant(42)); // A reflection of an object
// is not the same as its value.

// NOTE: is_consteval_only_type 目前未实现. 分支分发很重要
// 重要的是std::meta::info不被允许传播到运行时。这没有任何意义，所以简单地阻止类型以任何方式用于运行时是理想的

// 4.3.4 Default arguments

int main()
try
{
    // ============================4.3.2 The associated std::meta namespace===========================================
    // 命名空间std::meta是std::meta::info的关联命名空间，它允许在没有显式限定的情况下调用标准库元函数。例如：
    {
        struct S
        {
        };
        auto name2 = std::meta::identifier_of(^^S); // Okay.
        auto name1 = identifier_of(^^S);            // Also okay.
    }
    // 默认构造或值初始化std::meta::info类型的对象给它一个空反射值。空反射值等于任何其他空反射值，并且不同于引用上述实体之一的任何其他反射。例如：
    {
        struct S
        {
        };
        static_assert(std::meta::info() == std::meta::info());
        static_assert(std::meta::info() != ^^S);
    }
    // std::meta::info 及其复合类型为 consteval-only 类型，必须通过 is_consteval_only_type 特征进行检测，并利用 if consteval 与 if constexpr 的组合来避免运行时分支的实例化导致错误

    {
        //NOTE: 拿到 hello_world 作为字符串
        int hello_world = 42;
        std::cout << identifier_of(^^hello_world)
                  << "\n"; // Doesn't work if identifier_of produces a std::string.
    }
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