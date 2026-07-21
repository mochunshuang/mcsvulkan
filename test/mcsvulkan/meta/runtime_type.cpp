#include <cstdint>
#include <iostream>
#include <meta>
#include <type_traits>

namespace meta = std::meta;

// 1. 仅声明、永不定义的模板，用于“打孔”表示已分配的票号
template <int N>
struct Helper; //NOTE: “记忆”靠的是编译器对已定义类型的记录

// 2. 票号管理器
struct TU_Ticket
{
    static consteval int latest()
    {
        int k = 0;
        while (meta::is_complete_type(
            meta::substitute(^^Helper, {
                                           std::meta::reflect_constant(k)})))
        {
            ++k;
        }
        return k;
    }
    static consteval void increment()
    {
        meta::define_aggregate(
            meta::substitute(^^Helper,
                             {
                                 std::meta::reflect_constant(latest())}),
            {}); // 空成员列表 -> 空结构体
    }
};

constexpr int x = TU_Ticket::latest();
static_assert(x == 0);
consteval
{
    TU_Ticket::increment();
}
constexpr int y = TU_Ticket::latest();
static_assert(y == 1);
consteval
{
    TU_Ticket::increment();
}
constexpr int z = TU_Ticket::latest();
static_assert(z == 2);

//NOTE: 编译错误: 要求与被定义的模板特化处于同一作用域
// constexpr auto inc = []() consteval {
//     consteval
//     {
//         ::TU_Ticket::increment();
//     }
// };

// clang-format off
struct A {
    int x;
    void show() const { std::cout << x; }
};
struct B {
    int a;
    double b;
    int getA() const { return a; }
    void setA(int v) { a = v; }
};
struct C {
    int id;
    float score;
    bool ok;
    void reset() { id=0; score=0; ok=false; }
    int get() const { return id; }
    void set(int i, float f, bool b) { id=i; score=f; ok=b; }
};
std::vector<A> vecA = {{10}, {20}, {30}};
std::vector<B> vecB = {{1, 1.0}, {2, 2.5}, {3, 3.14}};
std::vector<C> vecC = {{1, 1.0f, true}, {2, 2.5f, false}, {3, 3.14f, true}};

enum class TypeID { A, B, C };
// clang-format on
struct component_key
{
    TypeID type_id;
    uint32_t index;
};
template <typename T>
component_key get_component(auto index)
{
    if constexpr (std::is_same_v<T, A>)
        return {TypeID::A, index};
    else if constexpr (std::is_same_v<T, B>)
        return {TypeID::B, index};
    else
        return {TypeID::C, index};
}

void invoke(const std::string &fun, component_key key)
{
    if (key.type_id == TypeID::A)
    {
        auto &a = vecA.at(key.index);
        if (fun == "show")
        {
            a.show();
            std::cout << '\n';
        }
        else
        {
            std::cerr << "Unknown function \"" << fun << "\" for type A\n";
        }
    }
    else if (key.type_id == TypeID::B)
    {
        auto &b = vecB.at(key.index);
        if (fun == "getA")
        {
            std::cout << b.getA() << '\n';
        }
        else if (fun == "setA")
        {
            // 测试用固定值
            b.setA(100);
            std::cout << "setA(100) called\n";
        }
        else
        {
            std::cerr << "Unknown function \"" << fun << "\" for type B\n";
        }
    }
    else // TypeID::C
    {
        auto &c = vecC.at(key.index);
        if (fun == "reset")
        {
            c.reset();
            std::cout << "reset() called\n";
        }
        else if (fun == "get")
        {
            std::cout << c.get() << '\n';
        }
        else if (fun == "set")
        {
            c.set(7, 3.14f, true);
            std::cout << "set(7, 3.14, true) called\n";
        }
        else
        {
            std::cerr << "Unknown function \"" << fun << "\" for type C\n";
        }
    }
}
void test0()
{
    // 测试 A::show()
    std::cout << "A[0].show() = ";
    invoke("show", {TypeID::A, 0}); // 10
    std::cout << "A[2].show() = ";
    invoke("show", {TypeID::A, 2}); // 30

    // 测试 B::getA() / setA()
    std::cout << "B[1].getA() = ";
    invoke("getA", {TypeID::B, 1}); // 2
    invoke("setA", {TypeID::B, 1});
    std::cout << "B[1].getA() after setA = ";
    invoke("getA", {TypeID::B, 1}); // 100

    // 测试 C::get() / set() / reset()
    std::cout << "C[0].get() = ";
    invoke("get", {TypeID::C, 0}); // 1
    invoke("set", {TypeID::C, 0});
    std::cout << "C[0].get() after set = ";
    invoke("get", {TypeID::C, 0}); // 7
    invoke("reset", {TypeID::C, 0});
    std::cout << "C[0].get() after reset = ";
    invoke("get", {TypeID::C, 0}); // 0
}

struct context
{
};
struct Node
{
};

int main()
{
    test0();
    return 0;
}