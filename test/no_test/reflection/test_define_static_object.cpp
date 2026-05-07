#include <assert.h>
#include <meta>

using std::define_static_object;

// 标量
constexpr int g_int = 42;
static_assert([]() consteval {
    const int *p = define_static_object(g_int);
    return p != nullptr && *p == 42;
}());

constexpr double g_dbl = 3.14;
static_assert([]() consteval {
    const double *p = define_static_object(g_dbl);
    return p != nullptr && *p == 3.14;
}());

// 类对象
struct Point
{
    int x, y;
    constexpr Point(int x, int y) : x{x}, y{y} {}
    constexpr bool operator==(const Point &) const = default;
};
constexpr Point g_pt{1, 2};
static_assert([]() consteval {
    const Point *p = define_static_object(g_pt);
    return p != nullptr && *p == Point{1, 2};
}());

// 联合
union Value {
    int i;
    double d;
    constexpr Value(int i) : i{i} {}
    constexpr bool operator==(const Value &other) const
    {
        return i == other.i;
    }
};
constexpr Value g_val{7};
static_assert([]() consteval {
    const Value *p = define_static_object(g_val);
    return p != nullptr && *p == Value{7};
}());

// 数组：返回指向数组的指针，需要解引用再访问元素
constexpr int g_arr[] = {10, 20, 30};
static_assert([]() consteval {
    const auto p = define_static_object(g_arr); // 类型为 const int(*)[3]
    return p != nullptr && (*p)[0] == 10 && (*p)[1] == 20 && (*p)[2] == 30;
}());

// 空类
struct Empty
{
};
constexpr Empty g_empty{};
static_assert([]() consteval {
    const Empty *p = define_static_object(g_empty);
    return p != nullptr;
}());

// 临时对象（右值）
static_assert([]() consteval {
    const int *p = define_static_object(100);
    return p != nullptr && *p == 100;
}());

#include <iostream>
#include <exception>

int main()
try
{
    const int *p = nullptr;
    {
        p = define_static_object(100);
    }
    assert(p != nullptr && *p == 100);
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