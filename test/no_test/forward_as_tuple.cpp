#include <iostream>
#include <tuple>

struct Loud
{
    int value;
    Loud(int v) : value(v)
    {
        std::cout << "Loud(" << v << ") constructed\n";
    }
    ~Loud()
    {
        std::cout << "Loud(" << value << ") destroyed\n";
    }
    Loud(const Loud &) = delete; // 禁止拷贝，强调临时对象身份
    Loud(Loud &&) = delete;
};

int main()
{
    std::cout << "=== Case 1: forward_as_tuple(20) ===\n";
    auto t = std::forward_as_tuple(20); // 20 是 int 纯右值，临时 int 对象
    std::cout << "after forward_as_tuple, before end of line?\n";
    // 实际上临时 int 在上行分号前已销毁，但 int 没有用户定义的析构，看不出
    // 我们改用自定义类型 Loud

    std::cout << "\n=== Case 2: forward_as_tuple with Loud temporary ===\n";
    auto t2 = std::forward_as_tuple(Loud(42));
    std::cout << "after forward_as_tuple line (before end of scope?)\n";
    // 预期：Loud(42) constructed -> 然后立刻 Loud(42) destroyed（在分号前）
    // 之后 t2 持有悬空引用

    std::cout << "\n=== Case 3: Direct reference binding (life extension) ===\n";
    const Loud &ref = Loud(100); // 临时对象生命周期延长到 ref 的生命周期
    std::cout << "after const ref binding\n";
    // 析构将在 main 结束前发生

    std::cout << "\n=== Case 4: forward_as_tuple with an lvalue ===\n";
    Loud lvalue(999);
    auto t3 = std::forward_as_tuple(lvalue); // 安全，存储 Loud&
    std::cout << "after forward_as_tuple with lvalue\n";

    std::cout << "\n=== End of main ===\n";
    // 此处 ref 绑定的临时对象析构
}