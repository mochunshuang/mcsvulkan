#include <iostream>
#include <meta>

namespace meta = std::meta;

// 辅助类型
struct TestClass
{
    int mem;
    void func(int) {}
};

enum TestEnum
{
    A,
    B
};
enum class TestEnumClass : unsigned char
{
    X,
    Y
};
union TestUnion {
    int a;
    double b;
};

// ─────────────────────────────────────────────────────────
// 1. void
static_assert(meta::is_void_type(^^void));
static_assert(!meta::is_void_type(^^int));
static_assert(!meta::is_void_type(^^void *)); // 指针不是 void

// 2. std::nullptr_t
static_assert(meta::is_null_pointer_type(^^std::nullptr_t));
static_assert(!meta::is_null_pointer_type(^^int *));
static_assert(!meta::is_null_pointer_type(^^int));

// 3. 整型 (integral types)
static_assert(meta::is_integral_type(^^int));
static_assert(meta::is_integral_type(^^unsigned long long));
static_assert(meta::is_integral_type(^^bool));
static_assert(meta::is_integral_type(^^char));
static_assert(meta::is_integral_type(^^wchar_t));
static_assert(!meta::is_integral_type(^^float));
static_assert(!meta::is_integral_type(^^double *));

// 4. 浮点类型
static_assert(meta::is_floating_point_type(^^float));
static_assert(meta::is_floating_point_type(^^double));
static_assert(meta::is_floating_point_type(^^long double));
static_assert(!meta::is_floating_point_type(^^int));
static_assert(!meta::is_floating_point_type(^^float *));

// 5. 数组类型
static_assert(meta::is_array_type(^^int[5]));     // 有界数组
static_assert(meta::is_array_type(^^int[]));      // 未知边界数组
static_assert(meta::is_array_type(^^int[5][10])); // 多维数组
static_assert(!meta::is_array_type(^^int *));
static_assert(!meta::is_array_type(^^int (&)[5])); // 数组引用不是数组

// 6. 指针类型
static_assert(meta::is_pointer_type(^^int *));
static_assert(meta::is_pointer_type(^^void *));
static_assert(meta::is_pointer_type(^^const volatile int **));
static_assert(!meta::is_pointer_type(^^int &));
static_assert(!meta::is_pointer_type(^^int[1]));

// 7. 左值引用类型
static_assert(meta::is_lvalue_reference_type(^^int &));
static_assert(meta::is_lvalue_reference_type(^^const double &));
static_assert(!meta::is_lvalue_reference_type(^^int &&));
static_assert(!meta::is_lvalue_reference_type(^^int *));

// 8. 右值引用类型
static_assert(meta::is_rvalue_reference_type(^^int &&));
static_assert(!meta::is_rvalue_reference_type(^^int &));
static_assert(!meta::is_rvalue_reference_type(^^int *));

// 9. 成员对象指针类型
static_assert(meta::is_member_object_pointer_type(^^int TestClass::*)); // 直接反射类型
static_assert(meta::is_member_object_pointer_type(^^const double TestClass::*));
static_assert(!meta::is_member_object_pointer_type(^^int *));
static_assert(
    !meta::is_member_object_pointer_type(^^void (TestClass::*)(int))); // 成员函数指针

// 10. 成员函数指针类型
static_assert(meta::is_member_function_pointer_type(^^void (TestClass::*)(int)));
static_assert(meta::is_member_function_pointer_type(^^void (TestClass::*)() const));
static_assert(!meta::is_member_function_pointer_type(^^int TestClass::*));
static_assert(!meta::is_member_function_pointer_type(^^void (*)(int))); // 普通函数指针

// 11. 枚举类型
static_assert(meta::is_enum_type(^^TestEnum));
static_assert(meta::is_enum_type(^^TestEnumClass));
static_assert(!meta::is_enum_type(^^int));
static_assert(!meta::is_enum_type(^^TestClass));

// 12. 联合类型
static_assert(meta::is_union_type(^^TestUnion));
static_assert(!meta::is_union_type(^^TestClass));
static_assert(!meta::is_union_type(^^int));

// 13. 类类型 (class/struct)
static_assert(meta::is_class_type(^^TestClass));
static_assert(!meta::is_class_type(^^TestUnion));
static_assert(!meta::is_class_type(^^TestEnum));
static_assert(!meta::is_class_type(^^int));

// 14. 函数类型
static_assert(meta::is_function_type(^^int(float, double)));
static_assert(meta::is_function_type(^^void()));
static_assert(meta::is_function_type(^^void(int, ...)));
static_assert(!meta::is_function_type(^^int (*)(float))); // 函数指针不是函数类型

// 15. 反射类型 (std::meta::info)
static_assert(meta::is_reflection_type(^^std::meta::info)); // meta::info 自身是反射类型
static_assert(!meta::is_reflection_type(^^int));
static_assert(!meta::is_reflection_type(^^TestClass));

// ─────────────────────────────────────────────────────────
int main()
{
    std::cout << "Testing primary type categories via reflection (^^)\n";

    return 0;
}