#include <cassert>
#include <iostream>
#include <meta>

namespace meta = std::meta;

// 1. 全局命名空间
static_assert(meta::is_namespace(^^::));

// 2. 类型
static_assert(meta::is_type(^^int));
static_assert(meta::is_type(^^double));

// 3. 命名空间
static_assert(meta::is_namespace(^^std));
static_assert(meta::is_namespace(^^std::meta));

// 4. 变量（const 才可用于 constant_of）
static constexpr int global_var = 42; //NOTE: extract 提取反射值？
static_assert(meta::extract<int>(meta::constant_of(^^global_var)) == 42);

// 静态数据成员
struct WithStatic
{
    static const int val = 7;
};
static_assert(meta::extract<int>(meta::constant_of(^^WithStatic::val)) == 7);

// 5. 函数
void ordinary_func() {}
static_assert(meta::is_function(^^ordinary_func));

struct S
{
    void mem_func() {}
};
static_assert(meta::is_function(^^S::mem_func));

// 6. 非静态数据成员
struct Point
{
    int x, y;
};
constexpr auto mem_x = ^^Point::x;
constexpr auto mem_y = ^^Point::y;
static_assert(meta::is_nonstatic_data_member(mem_x));
static_assert(meta::is_nonstatic_data_member(mem_y));

// 7. 枚举器
enum class Color
{
    red,
    green,
    blue
};
static_assert(meta::is_enumerator(^^Color::red));
static_assert(meta::extract<Color>(meta::constant_of(^^Color::red)) == Color::red);

// 8. 函数模板实例
template <int P1, const int &P2>
void fn()
{
}
static constexpr int p_arr[2] = {1, 2};
constexpr auto fn_spec = ^^fn<p_arr[0], p_arr[1]>;

// fn_spec 是一个具体函数，不是模板
static_assert(meta::is_template(^^fn));
static_assert(!meta::is_template(fn_spec));
static_assert(meta::is_function(fn_spec));
static_assert(meta::has_template_arguments(fn_spec));

// 关键修正：使用 define_static_array 将模板参数列表转为静态数组
constexpr auto targs_static = define_static_array(meta::template_arguments_of(fn_spec));

// 验证模板参数的性质
static_assert(meta::is_value(targs_static[0]));
static_assert(meta::is_object(targs_static[1]));
static_assert(!meta::is_variable(targs_static[1]));

// 拼接值或指针
static_assert([:targs_static[0]:] == 1);
static_assert(&[:targs_static[1]:] == &p_arr[1]);

// 9. 类模板实例
template <typename T>
struct Wrapper
{
};
constexpr auto wrap_int = ^^Wrapper<int>;
static_assert(!meta::is_template(wrap_int));
static_assert(meta::is_type(wrap_int));
static_assert(meta::has_template_arguments(wrap_int));

// NOTE: 可以提取模板参数
constexpr auto wrap_targs_static =
    define_static_array(meta::template_arguments_of(wrap_int));
static_assert(wrap_targs_static.size() == 1);

int main()
{
    std::cout << "Testing reflection operator (^^)\n";

    // 通过拼接读取变量值
    std::cout << "global_var: " << [:^^global_var:] << '\n';           // 42
    std::cout << "WithStatic::val: " << [:^^WithStatic::val:] << '\n'; // 7

    //NOTE: [:info:] 可以替代具体成员名
    // 非静态成员访问
    Point p{1, 2};
    assert(p.[:mem_x:] == 1);
    assert(p.[:mem_y:] == 2);
    std::cout << "p.x: " << p.[:mem_x:] << '\n';
    std::cout << "p.y: " << p.[:mem_y:] << '\n';

    // 枚举器值
    std::cout << "Color::red (int): " << static_cast<int>([:^^Color::red:]) << '\n'; // 0

    // 从静态数组中拼接模板参数（值）
    std::cout << "First template argument (value): " << [:targs_static[0]:] << '\n'; // 1

    std::cout << "All tests passed.\n";
    return 0;
}