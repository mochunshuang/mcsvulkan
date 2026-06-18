#include <iostream>
#include <exception>

// NOLINTBEGIN
// 聚合类型 是一种可用于聚合初始化（T{...} 或 T = {...}）的数据类型
struct S
{
    int x;
    std::string name; // 非聚合成员，但 S 可以仍是聚合（C++17）
};
static_assert(not std::is_aggregate_v<std::string>);
static_assert(std::is_aggregate_v<S>); // 成立！

static_assert(std::is_default_constructible_v<S>);
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
} // NOLINTEND