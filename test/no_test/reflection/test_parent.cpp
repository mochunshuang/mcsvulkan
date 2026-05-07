#include <meta>
#include <iostream>
#include <span>

using namespace std::meta;

// 测试实体声明
struct Base
{
};
struct Derived : Base
{
};

struct Outer
{
    struct Inner
    {
    };
    union {
        int u;
        double v;
    };
    enum NestedEnum
    {
        A
    };
    int member;
    void func();
};
void Outer::func() {}

int global_var;
void global_func() {}
extern "C" void c_func() {} // C 语言链接（无 parent）

namespace ns
{
    int ns_var;
}

// 别名
using IntAlias = int;
using IntAlias2 = IntAlias;

// 辅助：获取派生类的第一个直接基类的反射
consteval info get_base(info derived)
{
    auto bases = bases_of(derived, access_context::current());
    std::span<const info> sp(bases);
    return sp.empty() ? info{} : sp[0];
}

// ============ has_parent ============
static_assert(!has_parent(^^::));     // 全局命名空间
static_assert(!has_parent(^^c_func)); // C 链接函数
static_assert(!has_parent(^^int));    // 非类非枚举类型
static_assert(!has_parent(^^int *));  // 指针类型

static_assert(has_parent(^^global_var));        // 全局变量
static_assert(has_parent(^^global_func));       // 全局函数
static_assert(has_parent(^^Outer));             // 类
static_assert(has_parent(^^Outer::Inner));      // 嵌套类
static_assert(has_parent(^^Outer::member));     // 数据成员
static_assert(has_parent(^^Outer::u));          // 匿名联合体成员
static_assert(has_parent(^^Outer::func));       // 成员函数
static_assert(has_parent(^^Outer::NestedEnum)); // 嵌套枚举
static_assert(has_parent(^^Outer::A));          // 枚举器
static_assert(has_parent(get_base(^^Derived))); // 直接基类关系

// ============ parent_of ============
static_assert(parent_of(^^global_var) == ^^::);              // 全局变量 → 全局命名空间
static_assert(parent_of(^^global_func) == ^^::);             // 全局函数 → 全局命名空间
static_assert(parent_of(^^Outer) == ^^::);                   // 全局类 → 全局命名空间
static_assert(parent_of(^^Outer::Inner) == ^^Outer);         // 嵌套类 → 外层类
static_assert(parent_of(^^Outer::member) == ^^Outer);        // 数据成员 → 所属类
static_assert(parent_of(^^Outer::func) == ^^Outer);          // 成员函数 → 所属类
static_assert(parent_of(^^Outer::NestedEnum) == ^^Outer);    // 嵌套枚举 → 外层类
static_assert(parent_of(^^Outer::A) == ^^Outer::NestedEnum); // 枚举器 → 枚举类型
static_assert(parent_of(get_base(^^Derived)) == ^^Derived);  // 基类关系 → 派生类

// 匿名联合体成员的特殊处理
constexpr info u_parent = parent_of(^^Outer::u);
static_assert(is_union_type(u_parent));        // 最内层匿名联合体类型
static_assert(parent_of(u_parent) == ^^Outer); // 该联合体的 parent 是 Outer

// ============ dealias ============
static_assert(dealias(^^IntAlias) == ^^int);  // 单层别名
static_assert(dealias(^^IntAlias2) == ^^int); // 多层别名
static_assert(dealias(^^int) == ^^int);       // 非别名保持不变
static_assert(dealias(^^Outer) == ^^Outer);   // 类名不变

int main()
{
    std::cout << "main done\n";
    return 0;
}