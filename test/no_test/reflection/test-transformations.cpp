#include <meta>
#include <initializer_list>
#include <compare>
#include <tuple>
#include <variant>
#include <functional>

namespace meta = std::meta;

// 辅助类型
using Int = int;
using ConstInt = const int;
using VolatileInt = volatile int;
using IntRef = int &;
using IntRRef = int &&;
using ConstIntRef = const int &;
using ConstIntRRef = const int &&;

struct Base
{
};
struct Derived : Base
{
};

enum class Color
{
    red,
    green,
    blue
}; // 默认为 int 底层类型
enum Unscoped
{
    a,
    b
};
enum class Unscoped2
{
    a,
    b
};
using RefWrapInt = std::reference_wrapper<int>;

// 用于 invoke_result
int func(int, double);
using FuncType = int(int, double);
struct Functor
{
    int operator()(int, double);
};
using FunctorType = Functor;

// 用于 common_type / common_reference
struct A
{
};
struct B : A
{
};

using VariantType = std::variant<int, double>;
using Tuple2 = std::tuple<int, double>;
using Pair = std::pair<int, char>;

// ── remove_cvref ──
static_assert(meta::is_same_type(meta::remove_cvref(^^Int), ^^Int));
static_assert(meta::is_same_type(meta::remove_cvref(^^ConstInt), ^^Int));
static_assert(meta::is_same_type(meta::remove_cvref(^^VolatileInt), ^^Int));
static_assert(meta::is_same_type(meta::remove_cvref(^^const volatile int), ^^Int));
static_assert(meta::is_same_type(meta::remove_cvref(^^IntRef), ^^Int));
static_assert(meta::is_same_type(meta::remove_cvref(^^IntRRef), ^^Int));
static_assert(meta::is_same_type(meta::remove_cvref(^^ConstIntRef), ^^Int));
static_assert(meta::is_same_type(meta::remove_cvref(^^ConstIntRRef), ^^Int));

// ── decay ──
static_assert(meta::is_same_type(meta::decay(^^Int), ^^Int));
static_assert(meta::is_same_type(meta::decay(^^ConstInt), ^^Int));
static_assert(meta::is_same_type(meta::decay(^^IntRef), ^^Int));
static_assert(meta::is_same_type(meta::decay(^^IntRRef), ^^Int));
static_assert(meta::is_same_type(meta::decay(^^const int[]), ^^const int *));
static_assert(meta::is_same_type(meta::decay(^^int[5]), ^^int *));
static_assert(meta::is_same_type(meta::decay(^^FuncType), ^^int (*)(int, double)));

// ── common_type ──
static_assert(meta::is_same_type(meta::common_type({^^int, ^^double}), ^^double));
static_assert(meta::is_same_type(meta::common_type({^^int, ^^int}), ^^int));
static_assert(meta::is_same_type(meta::common_type({^^int, ^^double, ^^long}), ^^double));
static_assert(meta::is_same_type(meta::common_type({^^int, ^^const int}), ^^int));

// ── common_reference ──
// 参考 std::common_reference 的行为，可能需要引用类型
static_assert(meta::is_same_type(meta::common_reference({^^int &, ^^const int &}),
                                 ^^const int &));
static_assert(meta::is_same_type(meta::common_reference({^^int &&, ^^int &}),
                                 ^^const int &));
// 对于非引用类型可能产生值类型
static_assert(meta::is_same_type(meta::common_reference({^^int, ^^int &}), ^^int));

// ── underlying_type ──
static_assert(meta::is_same_type(meta::underlying_type(^^Color),
                                 ^^int)); // 枚举底层类型为 int
static_assert(!meta::is_same_type(meta::underlying_type(^^Unscoped), ^^int));
static_assert(meta::is_same_type(meta::underlying_type(^^Unscoped2), ^^int));

// ── invoke_result ──
static_assert(meta::is_same_type(meta::invoke_result(^^FuncType,
                                                     {
                                                         ^^int, ^^double}),
                                 ^^int));
static_assert(meta::is_same_type(meta::invoke_result(^^FunctorType,
                                                     {
                                                         ^^int, ^^double}),
                                 ^^int));

// ── unwrap_reference ──
static_assert(meta::is_same_type(meta::unwrap_reference(^^RefWrapInt),
                                 ^^int &)); // reference_wrapper<int> -> int&
static_assert(meta::is_same_type(meta::unwrap_reference(^^Int), ^^Int));

// ── unwrap_ref_decay ──
static_assert(meta::is_same_type(meta::unwrap_ref_decay(^^RefWrapInt), ^^int &));
static_assert(meta::is_same_type(meta::unwrap_ref_decay(^^Int), ^^int));
static_assert(meta::is_same_type(meta::unwrap_ref_decay(^^const int &), ^^int));

// ── tuple_size / tuple_element ──
static_assert(meta::tuple_size(^^Tuple2) == 2);
static_assert(meta::tuple_size(^^Pair) == 2);
static_assert(meta::is_same_type(meta::tuple_element(0, ^^Tuple2), ^^int));
static_assert(meta::is_same_type(meta::tuple_element(1, ^^Tuple2), ^^double));
static_assert(meta::is_same_type(meta::tuple_element(0, ^^Pair), ^^int));
static_assert(meta::is_same_type(meta::tuple_element(1, ^^Pair), ^^char));

// ── variant_size / variant_alternative ──
static_assert(meta::variant_size(^^VariantType) == 2);
static_assert(meta::is_same_type(meta::variant_alternative(0, ^^VariantType), ^^int));
static_assert(meta::is_same_type(meta::variant_alternative(1, ^^VariantType), ^^double));

// ── type_order ──
constexpr auto order = meta::type_order(^^int, ^^double);
static_assert(order == std::strong_ordering::less ||
              order == std::strong_ordering::greater);
static_assert(meta::type_order(^^int, ^^int) == std::strong_ordering::equal);
static_assert(meta::type_order(^^int, ^^double) != std::strong_ordering::equal);

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