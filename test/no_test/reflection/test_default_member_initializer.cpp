#include <meta>
#include <iostream>
using namespace std::meta;

struct WithDefault
{
    int a = 42;    // 有默认初始化器
    int b = 7 + 3; // 有默认初始化器（表达式）
    int c;         // 无默认初始化器
    static int d;  // 静态成员（不适用，应返回 false）
    void f() {}    // 函数，不适用
};

int WithDefault::d = 0;

// 基本类型、枚举、其他实体不应有默认成员初始化器
enum E
{
    e0
};
int globalVar = 10;

// 测试
static_assert(has_default_member_initializer(^^WithDefault::a));
static_assert(has_default_member_initializer(^^WithDefault::b));
static_assert(!has_default_member_initializer(^^WithDefault::c));
static_assert(!has_default_member_initializer(^^WithDefault::d)); // 静态成员
static_assert(!has_default_member_initializer(^^WithDefault::f)); // 非数据成员
static_assert(!has_default_member_initializer(^^globalVar));      // 不是非静态数据成员
static_assert(!has_default_member_initializer(^^e0));             // 枚举器
static_assert(!has_default_member_initializer(^^int));            // 类型

int main()
{
    std::cout << "main done\n";
    return 0;
}