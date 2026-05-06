#include <iostream>
#include <exception>
#include <type_traits>

int main()
try
{
    // NOTE: typename + 解析表达 取出类型
    constexpr auto r = ^^int;
    constexpr typename[:r:] x = 42;       // Same as: int x = 42;
    constexpr typename[:^^char:] c = '*'; // Same as: char c = '*';

    //typename前缀可以在与依赖限定名称相同的上下文中省略（即，在标准调用的仅类型上下文中）。例如：
    // Implicit "typename" prefix.
    using MyType = [:sizeof(int) < sizeof(long) ? ^^long : ^^int:];
    static_assert(std::is_same_v<MyType, int>);

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
}