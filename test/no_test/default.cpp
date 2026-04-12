#include <iostream>

struct Pixels
{
    float points; // 没有默认初始化器
    // 编译器隐式生成默认构造函数，等价于 = default

    Pixels() = default;
};
void foo()
{
    volatile int x = 0xDEADBEEF; // 写入栈
}

int main()
{
    foo();    // 先写入一些垃圾值
    Pixels p; // 默认初始化，points 未初始化（垃圾值）

    // 读取未初始化的值，这是未定义行为，但通常会打印一个随机数
    std::cout << "points = " << p.points << std::endl;

    // 为了对比，使用值初始化（零初始化）
    Pixels q{}; // 值初始化，points 被零初始化
    std::cout << "q.points = " << q.points << std::endl;

    return 0;
}