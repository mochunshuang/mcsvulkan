#include <span>
#include <string_view>
#include <array>
#include <meta>
#include <iostream>
#include <exception>

// 注意：define_static_string 在 std 命名空间，不在 std::meta
using std::define_static_string;

// 测试 string_view
constexpr std::string_view g_sv = "hello";
static_assert([]() consteval {
    constexpr const char *p = define_static_string(g_sv);
    return p[0] == 'h' && p[4] == 'o' && p[5] == '\0';
}());

// 空字符串
constexpr std::string_view g_empty = "";
static_assert([]() consteval {
    constexpr const char *p = define_static_string(g_empty);
    return p[0] == '\0';
}());

// 测试 char8_t (C++20)
constexpr std::u8string_view g_u8 = u8"data";
static_assert([]() consteval {
    constexpr const char8_t *p = define_static_string(g_u8);
    return p[0] == u8'd' && p[3] == u8'a';
}());

// 测试 array<char, N>
constexpr std::array<char, 6> g_arr = {'a', 'b', 'c', 'd', 'e', '\0'};
static_assert([]() consteval {
    constexpr const char *p = define_static_string(g_arr);
    return p[0] == 'a' && p[4] == 'e' && p[5] == '\0';
}());

// 测试 span<const char>
// 注意：span 的构造需要静态地址，因此将底层数组声明为 static constexpr
constexpr const char span_data[] = "world";
constexpr std::span<const char> g_span{span_data};
static_assert([]() consteval {
    constexpr const char *p = define_static_string(g_span);
    return p[0] == 'w' && p[4] == 'd';
}());

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