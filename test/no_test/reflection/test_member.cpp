#include <meta>
#include <iostream>
using namespace std::meta;

// 基类
struct Base
{
};
struct Derived : Base
{
};

// 包含多种成员的类
struct MyClass
{
    int non_static_data;       // 非静态数据成员
    static int static_data;    // 静态数据成员
    void func();               // 非静态成员函数
    static void static_func(); // 静态成员函数
    struct Nested
    {
    }; // 嵌套类型（类成员）
};
int MyClass::static_data = 0;
void MyClass::func() {}
void MyClass::static_func() {}

// 命名空间成员
namespace test_ns
{
    int ns_var;
    void ns_func() {}
    struct ns_type
    {
    };
} // namespace test_ns

int global_var;
void global_func() {}

// 辅助：获取基类反射
consteval info get_base(info derived)
{
    auto bases = bases_of(derived, access_context::current());
    if (bases.empty())
        return {};
    return bases[0];
}

// ========== 测试断言 ==========

// is_class_member
static_assert(is_class_member(^^MyClass::non_static_data));
static_assert(is_class_member(^^MyClass::static_data));
static_assert(is_class_member(^^MyClass::func));
static_assert(is_class_member(^^MyClass::static_func));
static_assert(is_class_member(^^MyClass::Nested));
static_assert(!is_class_member(^^global_var));
static_assert(!is_class_member(^^test_ns::ns_var));
static_assert(!is_class_member(^^MyClass)); // 类本身不是成员
static_assert(!is_class_member(^^int));

// is_namespace_member
static_assert(is_namespace_member(^^global_var));
static_assert(is_namespace_member(^^global_func));
static_assert(is_namespace_member(^^test_ns::ns_var));
static_assert(is_namespace_member(^^test_ns::ns_func));
static_assert(is_namespace_member(^^test_ns::ns_type));
static_assert(is_namespace_member(^^MyClass));                   // 全局类是命名空间成员
static_assert(!is_namespace_member(^^MyClass::non_static_data)); // 类成员不是
static_assert(is_namespace_member(^^int)); //NOTE: 类型属于 命名空间的成员
static_assert(!is_namespace_member(^^MyClass::Nested));

// is_nonstatic_data_member
static_assert(is_nonstatic_data_member(^^MyClass::non_static_data));
static_assert(!is_nonstatic_data_member(^^MyClass::static_data));
static_assert(!is_nonstatic_data_member(^^MyClass::func));
static_assert(!is_nonstatic_data_member(^^MyClass::Nested));
static_assert(!is_nonstatic_data_member(^^global_var));

// is_static_member
static_assert(is_static_member(^^MyClass::static_data));
static_assert(is_static_member(^^MyClass::static_func));
static_assert(!is_static_member(^^MyClass::non_static_data));
static_assert(!is_static_member(^^MyClass::func));
// 嵌套类型是否算静态成员？规范未明确，通常静态成员指数据或函数，类型不是
static_assert(!is_static_member(^^MyClass::Nested));
static_assert(!is_static_member(^^global_var));

// is_base
constexpr info base_info = get_base(^^Derived);
static_assert(is_base(base_info));
static_assert(!is_base(^^MyClass::non_static_data));
static_assert(!is_base(^^Derived)); // 类型本身不是基类关系
static_assert(!is_base(^^int));

int main()
{
    std::cout << "main done\n";
    return 0;
}