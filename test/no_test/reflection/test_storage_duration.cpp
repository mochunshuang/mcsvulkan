#include <iostream>
#include <meta>
using namespace std::meta;

// ======== 静态存储期 ========
int globalVar;               // 静态存储期
static int staticVar;        // 静态存储期
constexpr int constVar = 10; // 也属于静态存储期

struct S
{
    static int staticMem; // 静态成员
};
int S::staticMem = 0;

static_assert(has_static_storage_duration(^^globalVar));
static_assert(has_static_storage_duration(^^staticVar));
static_assert(has_static_storage_duration(^^constVar));
static_assert(has_static_storage_duration(^^S::staticMem));
static_assert(!has_thread_storage_duration(^^globalVar));
static_assert(!has_thread_storage_duration(^^S::staticMem));

// ======== 线程存储期 ========
thread_local int tlsVar;
static_assert(has_thread_storage_duration(^^tlsVar));
static_assert(!has_static_storage_duration(^^tlsVar)); // 线程局部的不是静态

// ======== 自动存储期 (只能在函数内反射) ========
consteval bool test_automatic()
{
    int autoVar = 0;
    // 反射局部变量
    constexpr info auto_info = ^^autoVar;

    // 对于无法反射的自动实体，本测试只能验证静态实体不是自动。
    if (has_automatic_storage_duration(^^globalVar))
        return false;
    if (has_automatic_storage_duration(^^tlsVar))
        return false;

    return has_automatic_storage_duration(auto_info);
}
static_assert(test_automatic());

// ======== 无效实体：调用会导致编译错误（注释说明） ========
// has_static_storage_duration(^^int); // 错误：int 不是对象
// has_thread_storage_duration(^^S);        // 错误

int main()
{
    std::cout << "main done\n";
    return 0;
}