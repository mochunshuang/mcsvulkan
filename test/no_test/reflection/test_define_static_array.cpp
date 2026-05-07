#include <meta>
#include <array>
#include <span>
#include <initializer_list>

using std::define_static_array;

// 辅助：编译期验证 span 内容和大小
consteval bool check_int_span(std::span<const int> sp,
                              std::initializer_list<int> expected)
{
    if (sp.size() != expected.size())
        return false;
    const auto *it = expected.begin();
    for (int i : sp)
    {
        if (i != *it++)
            return false;
    }
    return true;
}

// 测试1：静态大小的 array -> 静态 span
constexpr std::array<int, 3> g_static_arr = {1, 2, 3};
static_assert([]() consteval {
    auto sp = define_static_array(g_static_arr);
    // 返回的是 span<const int, 3>
    static_assert(sp.size() == 3);
    return check_int_span(sp, {1, 2, 3});
}());

// 测试2：动态大小的 span (const int*) -> 动态 span
constexpr const int dyn_data[] = {10, 20, 30};
constexpr std::span<const int> g_dyn_span(dyn_data);
static_assert([]() consteval {
    auto sp = define_static_array(g_dyn_span);
    return sp.size() == 3 && check_int_span(sp, {10, 20, 30});
}());

// 测试3：空 array -> 静态 span<const int, 0>
constexpr std::array<int, 0> g_empty_arr = {};
static_assert([]() consteval {
    auto sp = define_static_array(g_empty_arr);
    static_assert(std::is_same_v<decltype(sp), std::span<const int, 0>>);
    return sp.size() == 0;
}());

// 测试4：空 dynamic span -> span<const int>（大小为0）
constexpr std::span<const int> g_empty_dyn;
static_assert([]() consteval {
    auto sp = define_static_array(g_empty_dyn);
    static_assert(std::is_same_v<decltype(sp), std::span<const int>>);
    return sp.size() == 0;
}());

// 测试5：initializer_list 直接传入 (作为右值范围)
static_assert([]() consteval {
    constexpr auto sp = define_static_array(std::initializer_list{4, 5, 6});
    static_assert(sp.size() == 3);
    return check_int_span(sp, {4, 5, 6});
}());

// 测试6：不同类型，如 double
constexpr std::array<double, 2> g_dbl_arr = {1.5, 2.5};
static_assert([]() consteval {
    auto sp = define_static_array(g_dbl_arr);
    return sp.size() == 2 && sp[0] == 1.5 && sp[1] == 2.5;
}());

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