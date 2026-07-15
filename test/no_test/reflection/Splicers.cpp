#include <assert.h>
#include <cstdint>
#include <iostream>
#include <meta>
#include <cassert>
#include <ranges>

namespace meta = std::meta;

// ===== §4.2.1 寻址拼接 &[:r:] =====

//NOTE:  meta::info 可以存放 任意值
// 1. 静态数据成员 / 变量 → 普通指针
static constexpr int global_val = 10;
constexpr auto ref_vale = ^^global_val;
static_assert(std::is_same_v<std::decay_t<decltype(ref_vale)>, meta::info>);
constexpr auto ptr_global = &[:ref_vale:];
static_assert(*ptr_global == 10);

// 2. 非静态数据成员 → 指针到数据成员
struct Point
{
    int x, y;
};
constexpr auto mem_ptr_x = &[:^^Point::x:];
constexpr auto mem_ptr_y = &[:^^Point::y:];
static_assert(std::is_member_object_pointer_v<decltype(mem_ptr_x)>);

// 3. 自由函数、静态成员函数 → 函数指针
constexpr int square(int n)
{
    return n * n;
}
constexpr auto fn_ptr_square = &[:^^square:];
static_assert((*fn_ptr_square)(3) == 9); // 函数为 constexpr，OK

struct S
{
    static constexpr int triple(int n)
    {
        return n * 3;
    }
    int mult(int n)
    {
        return n * x;
    }
    int x = 5;
};
constexpr auto fn_ptr_triple = &[:^^S::triple:];
static_assert((*fn_ptr_triple)(2) == 6);

// 4. 非静态成员函数 → 成员函数指针
constexpr auto mem_fn_ptr_mult = &[:^^S::mult:];
static_assert(std::is_member_function_pointer_v<decltype(mem_fn_ptr_mult)>);

// 5. 函数模板实例 → 函数指针
template <typename T>
constexpr T id(T v)
{
    return v;
}
constexpr auto id_int = &[:^^id<int>:];
static_assert((*id_int)(42) == 42);

// ===== §4.2 拼接作为表达式 [:r:] =====

// 反射函数的正确调用方式
static_assert([:^^square:](3) == 9);      // 自由函数
static_assert([:^^S::triple:](10) == 30); // 静态成员函数
constexpr auto square_ref = ^^square;
static_assert([:square_ref:](3) == 9); // NOTE: 可以分离

// 拼接待构造对象
constexpr Point p1{1, 2};
static_assert(p1.[:^^Point::x:] == 1); // 成员访问

// ===== §4.2 类型拼接 typename[:r:] =====

// 声明变量
int var1 = 10;
decltype([:^^var1:]) var2 = 20; // 依赖类型推导

// 作为模板参数
std::vector<typename[:^^int:]> vec = {1, 2, 3};

// 作为返回类型
constexpr typename[:^^double:] get_pi()
{
    return 3.14159;
}
static_assert(get_pi() > 3.14);

// ===== §4.2 命名空间拼接 [:r:]:: =====

namespace Outer
{
    static constexpr int answer = 42; // 改为 constexpr
}
constexpr auto ns_outer = ^^Outer;
static_assert([:ns_outer:] ::answer == 42);

constexpr auto ns_global = ^^::;
static_assert([:ns_global:] ::global_val == 10);

// ===== 反射遍历成员 + 表达式拼接 =====

struct Product
{
    int id = 100;
    double price = 19.99;
    std::string name = "Widget";
};

// 获取静态成员信息数组
constexpr auto prod_members = define_static_array(
    meta::nonstatic_data_members_of(^^Product, meta::access_context::unchecked()));

// 编译期访问第 0 个成员值
constexpr Product prod;
static_assert(prod.[:prod_members[0]:] == 100); // id

// 运行时打印
void print_members(const Product &p)
{
    std::cout << "id = " << p.[:prod_members[0]:] << '\n';
    std::cout << "price = " << p.[:prod_members[1]:] << '\n';
    std::cout << "name = " << p.[:prod_members[2]:] << '\n';
}
void print_members2(const Product &p, uint16_t index)
{
    // e:\0_github_project\mcsvulkan\test\no_test\reflection\Splicers.cpp:131:51: error: 'index' is not a constant expression
    // 131 | std::cout << "members = " << p.[:prod_members[index]:] << '\n';
    // std::cout << "members = " << p.[:prod_members[index]:] << '\n';

    constexpr auto mems = prod_members; // 成员反射信息的编译期数组
    template for (constexpr auto e : std::ranges::views::indices(mems.size()))
    {
        if (index == e)
        {
            std::cout << "members[" << e << "] = " << p.[:mems[e]:] << '\n';
            // 无需 break，其他分支的 if 条件都不满足，不会执行
        }
    }
}

// ===== 补充：显式对象参数成员函数 → 函数指针 (§4.2.1) =====
struct ExplicitObj
{
    int value;
    int scale(this const ExplicitObj &self, int n)
    {
        return n * self.value;
    }
};
constexpr auto fn_explicit = &[:^^ExplicitObj::scale:];
static_assert(std::is_function_v<std::remove_pointer_t<decltype(fn_explicit)>>);

// ===== 补充：重载集中选取特定函数/函数模板 (§4.2.1) =====
struct OverloadDemo
{
    template <class T>
    void f(T)
    {
    } // #1
    void f(int) {} // #2
};

// 编译期过滤得到 #1（函数模板）和 #2（普通函数）
consteval meta::info first_func_template()
{
    for (auto mem : meta::members_of(^^OverloadDemo, meta::access_context::unchecked()))
    {
        if (meta::is_function_template(mem))
            return mem;
    }
    return {};
}
consteval meta::info first_plain_func()
{
    for (auto mem : meta::members_of(^^OverloadDemo, meta::access_context::unchecked()))
    {
        if (meta::is_function(mem) && !meta::is_function_template(mem))
            return mem;
    }
    return {};
}
constexpr auto ft_info = first_func_template();
constexpr auto pf_info = first_plain_func();
// 拼接取地址得到对应重载的指针（无需消歧义）
// void (OverloadDemo::*p_ft)(int) = &[:ft_info:]; // 预期：C::f<int> // NOTE: 错误无法隐式推导
void (OverloadDemo::*p_ft)(int) = &[:^^OverloadDemo::f<int>:];
void (OverloadDemo::*p_pf)(int) = &[:pf_info:]; // C::f(int)

// ===== 补充：模拟范围拼接 —— struct → tuple (§4.2.3) =====
template <typename T>
constexpr auto struct_to_tuple(const T &obj)
{
    constexpr auto mems = define_static_array(
        meta::nonstatic_data_members_of(^^T, meta::access_context::unchecked()));
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return std::make_tuple(obj.[:mems[Is]:]...);
    }(std::make_index_sequence<mems.size()>{});
}

// ===== 补充：无 typename 的类型上下文拼接（type-only context） =====
// 在明确需要类型的场合（如模板实参），[:r:] 直接产生类型，无需 typename
// NOTE: 目前做不到
using IntVec = std::vector<typename[:^^int:]>; // 直接作为类型使用.
static_assert(std::is_same_v<IntVec, std::vector<int>>);

// ===== 补充：拼接模板名 template[:r:] =====
// 尝试拼接一个类模板的反射作为模板名
// 注：当前 GCC 16 对 template[:r:] 支持有限，先用 #if 0 包裹，待支持后启用
#if 1
template <typename T>
struct Wrapper
{
    T value;
};
constexpr auto tmpl_info = ^^Wrapper;
constexpr typename[:tmpl_info:]<int> wi{42}; // 预期：等价于 Wrapper<int>
static_assert(wi.value == 42);

// constexpr template [:tmpl_info:]<int> w{42}; //NOTE: 不行

#endif
// 若无 template[:r:] 支持，可用直接反射特化替代

// ===== 补充：拼接命名空间作为命名空间名（命名空间别名） =====
namespace AnotherNS
{
    constexpr int secret = 77;
}
constexpr auto ns_another = ^^AnotherNS;
namespace AliasNS = [:ns_another:]; // 命名空间别名
static_assert(AliasNS::secret == 77);

// ===== 文档 §4.2.2 明确禁止的拼接（均不能编译） =====
// 1. 拼接构造函数/析构函数 (§4.2.2.1)
//    struct X { X(int,int); };
//    constexpr auto rc = ^^X::X;
//    auto x = [:rc:](1,2);                // 禁止
//
// 2. 命名空间定义中拼接命名空间 (§4.2.2.2)
//    namespace B { namespace [:ns_outer:] {} } // 禁止
//
// 3. using 指令/using enum 中拼接命名空间或枚举 (§4.2.2.3)
//    using namespace [:ns_outer:];         // 禁止
//
// 4. 模板参数声明中拼接概念 (§4.2.2.4)
//    template <typename T> concept C = true;
//    template <template [:^^C:] S> struct Inner; // 禁止
//
// 5. 指定初始化器设计器中拼接成员 (§4.2.2.5)
//    constexpr Point s = {.[:^^Point::x:] = 2};  // 禁止

// 测试
struct Demo
{
    int a;
    double b;
};
constexpr Demo d{1, 2.5};
constexpr auto t = struct_to_tuple(d);
static_assert(std::get<0>(t) == 1);
static_assert(std::get<1>(t) == 2.5);

// ===== 主函数：所有运行时断言 =====

int main()
{

    std::cout << "Testing splicers ([:...:])\n";

    // 指针解引用打印
    std::cout << "global_val: " << *ptr_global << '\n';
    std::cout << "square(3): " << (*fn_ptr_square)(3) << '\n';
    std::cout << "triple(2): " << (*fn_ptr_triple)(2) << '\n';
    std::cout << "id<int>(7): " << (*id_int)(7) << '\n';

    // 指针到成员使用
    Point p{10, 20};
    assert(p.*mem_ptr_x == 10);
    assert(p.*mem_ptr_y == 20);

    // 成员函数指针使用
    S s{5};
    assert((s.*mem_fn_ptr_mult)(3) == 15);

    print_members(prod);
    print_members2(prod, 1);

    {
        // 显式对象参数成员函数
        ExplicitObj e{10};
        assert(fn_explicit(e, 3) == 30);
    }
    // NOTE: 静止：初始化器设计器中拼接成员
    {
        // struct S
        // {
        //     int a;
        // };
        // constexpr S s = {.[:^^S::a:] = 2};
    }

    std::cout << "All splicing tests passed.\n";
}