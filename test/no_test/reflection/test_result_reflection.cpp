#include <meta>
#include <iostream>
#include <type_traits>

namespace meta = std::meta;

// ======================== 辅助声明 ========================
// 一个 structural 类类型
struct StructuralType
{
    int x;
    double y;
    constexpr bool operator==(const StructuralType &) const = default;
};

// 用于验证反射的等待模板
template <auto D>
struct A
{
};

// 一些测试用函数（使用 constexpr 以支持编译期反射）
constexpr int add(int a, int b)
{
    return a + b;
}
constexpr int global_int = 42;
constexpr StructuralType global_struct{1, 2.0};

// ======================== 1. reflect_constant 测试 ========================
// 1.1 整数值反射
constexpr meta::info r_int = meta::reflect_constant(42);
static_assert(meta::is_value(r_int));
static_assert(r_int == meta::template_arguments_of(^^A<42>)[0]);

// 1.2 浮点值反射
constexpr meta::info r_double = meta::reflect_constant(3.14);
static_assert(meta::is_value(r_double));
static_assert(r_double == meta::template_arguments_of(^^A<3.14>)[0]);

// 1.3 类类型对象反射（值对象）
constexpr meta::info r_struct = meta::reflect_constant(StructuralType{10, 20.0});
static_assert(meta::is_object(r_struct));
static_assert(r_struct == meta::template_arguments_of(^^A<StructuralType{10, 20.0}>)[0]);

// 1.4 指针类型（合法，但不能指向字符串字面量，此处用 nullptr 或全局变量地址）
constexpr int dummy = 0;
constexpr meta::info r_ptr = meta::reflect_constant(&dummy);
static_assert(meta::is_value(r_ptr)); // 指针是值
static_assert(r_ptr == meta::template_arguments_of(^^A<&dummy>)[0]);

// ======================== 2. reflect_object 测试 ========================
// 2.1 全局对象（非引用类型对象）
constexpr meta::info r_obj_int = meta::reflect_object(global_int);
static_assert(meta::is_object(r_obj_int));
static_assert(meta::extract<const int &>(r_obj_int) == 42); // 确认引用内容

// 2.2 类对象
constexpr meta::info r_obj_struct = meta::reflect_object(global_struct);
static_assert(meta::is_object(r_obj_struct));
static_assert(meta::extract<const StructuralType &>(r_obj_struct).x == 1);
static_assert(meta::extract<const StructuralType &>(r_obj_struct).y == 2.0);

// ======================== 3. reflect_function 测试 ========================
// 3.1 自由函数（constexpr）
// ======================== 辅助函数 ========================
// 自由函数（constexpr，以便在编译期验证）

// 另一个自由函数，用于链接属性测试
int plain_func()
{
    return 0;
} // 非 constexpr，但可反射

// 静态成员函数
struct S
{
    static constexpr int triple(int n)
    {
        return n * 3;
    }
    static int plain_static()
    {
        return 0;
    }
};

// 函数指针
constexpr int (*add_ptr)(int, int) = add;
constexpr int (*triple_ptr)(int) = S::triple;

// ======================== 测试 ========================
// 1. 反射自由函数（直接传递函数左值）
constexpr meta::info r_add = meta::reflect_function(add);
static_assert(meta::is_function(r_add));
static_assert(meta::has_identifier(r_add) && meta::identifier_of(r_add) == "add");
// 通过 extract 获取函数指针并调用验证
constexpr auto extracted_add = meta::extract<int (*)(int, int)>(r_add);
static_assert(extracted_add(3, 4) == 7);

// 2. 反射静态成员函数（同样传递左值）
constexpr meta::info r_triple = meta::reflect_function(S::triple);
static_assert(meta::is_function(r_triple));
static_assert(meta::parent_of(r_triple) == ^^S);
constexpr auto extracted_triple = meta::extract<int (*)(int)>(r_triple);
static_assert(extracted_triple(5) == 15);

// 3. 通过函数指针解引用反射（标准推荐用法）
constexpr meta::info r_via_ptr = meta::reflect_function(*add_ptr);
static_assert(meta::is_function(r_via_ptr));
// 应得到与 r_add 相同的函数反射（可选比较）
static_assert(r_via_ptr == r_add);

// 4. 反射非 constexpr 函数（只反射不调用）
constexpr meta::info r_plain = meta::reflect_function(plain_func);
static_assert(meta::is_function(r_plain));
static_assert(meta::has_external_linkage(r_plain)); // 假设有外部链接
static_assert(meta::parent_of(r_plain) == ^^::);    // 在全局命名空间

// 5. 验证函数反射也适用于无捕获 lambda 转换的函数指针
constexpr auto lam = [](int x) {
    return x * x;
};
constexpr int (*lam_ptr)(int) = lam; // 转换为函数指针
constexpr meta::info r_lam_func = meta::reflect_function(*lam_ptr);
static_assert(meta::is_function(r_lam_func));
// 可以提取并调用（因为 lambda 是 constexpr？实际上 lam 是 constexpr 对象，其 operator() 是 constexpr，所以函数指针可用）
constexpr auto extracted_lam = meta::extract<int (*)(int)>(r_lam_func);
static_assert(extracted_lam(6) == 36);

// ======================== 入口 ========================
int main()
{
    std::cout << "All expression result reflection tests passed.\n";
    return 0;
}