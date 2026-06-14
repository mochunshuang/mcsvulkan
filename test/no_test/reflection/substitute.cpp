#include <meta>
#include <iostream>
#include <type_traits>

namespace meta = std::meta;

// ======================== 受测模板 ========================
// 变量模板
template <typename T>
constexpr bool is_void_v = false;
template <>
constexpr bool is_void_v<void> = true;

// 别名模板
template <typename T>
using ptr = T *;

// 类模板
template <typename T1, typename T2>
struct pair
{
};

// 非类型参数的类模板（如 std::integral_constant，直接使用标准库）
#include <type_traits> // 为了 std::integral_constant

// 函数模板
template <typename T>
constexpr T identity(T x)
{
    return x;
}

// 含 auto 返回类型的函数模板（can_substitute 应失败）
template <typename T>
auto auto_fn()
{
    return T{};
}

// ======================== 1. can_substitute 全面测试 ========================
// 变量模板
static_assert(meta::can_substitute(^^is_void_v, {
                                                    ^^int}));
static_assert(meta::can_substitute(^^is_void_v, {
                                                    ^^void}));
static_assert(!meta::can_substitute(^^is_void_v, {
                                                     ^^int, ^^double})); // 参数过多

// 别名模板
static_assert(meta::can_substitute(^^ptr, {
                                              ^^int}));
static_assert(!meta::can_substitute(^^ptr, {
                                               ^^int, ^^double}));

// 类模板
static_assert(meta::can_substitute(^^pair, {
                                               ^^int, ^^double}));
static_assert(!meta::can_substitute(^^pair, {
                                                ^^int})); // 参数个数不对

// 函数模板
static_assert(meta::can_substitute(^^identity, {
                                                   ^^int}));
static_assert(!meta::can_substitute(^^identity, {
                                                    ^^int, ^^double}));

// 非类型模板参数（integral_constant）
static_assert(meta::can_substitute(^^std::integral_constant,
                                   {
                                       ^^unsigned, meta::reflect_constant(5u)}));

// auto 返回类型 – 不可替换（标准示例）
static_assert(!meta::can_substitute(^^auto_fn, {
                                                   ^^int}));

// ======================== 2. substitute + 验证替换结果 ========================

// 2.1 变量模板：is_void_v<int> → false
constexpr meta::info r_void_int = meta::substitute(^^is_void_v, {
                                                                    ^^int});
static_assert(not meta::is_type(r_void_int));
static_assert([:r_void_int:] == false);

// 2.2 变量模板：is_void_v<void> → true
constexpr meta::info r_void_void = meta::substitute(^^is_void_v, {
                                                                     ^^void});
static_assert(not meta::is_type(r_void_int));
static_assert(meta::is_type(meta::type_of(r_void_int)));
static_assert([:r_void_void:] == true);

// 2.3 别名模板：ptr<int> → int*
constexpr meta::info r_ptr = meta::substitute(^^ptr, {
                                                         ^^int});
using PtrType = typename[:r_ptr:];
static_assert(std::is_same_v<PtrType, int *>);

// 2.4 类模板：pair<int, double>
constexpr meta::info r_pair = meta::substitute(^^pair, {
                                                           ^^int, ^^double});
using PairType = typename[:r_pair:];
static_assert(std::is_same_v<PairType, pair<int, double>>);

// 2.5 函数模板：identity<int> → 函数反射，再通过 extract 获取函数指针并调用
constexpr meta::info r_identity = meta::substitute(^^identity, {
                                                                   ^^int});
static_assert(meta::is_function(r_identity));
constexpr auto identity_ptr = meta::extract<int (*)(int)>(r_identity);
static_assert(identity_ptr(5) == 5); // 编译期调用验证

// 2.6 非类型模板参数：integral_constant<unsigned, 5>
constexpr meta::info r_ic = meta::substitute(^^std::integral_constant,
                                             {
                                                 ^^unsigned, meta::reflect_constant(5u)});
using ICType = typename[:r_ic:];
static_assert(ICType::value == 5u); // 验证值

// ======================== 3. 入口 ========================
int main()
{
    std::cout << "All substitute / can_substitute tests passed.\n";
    return 0;
}