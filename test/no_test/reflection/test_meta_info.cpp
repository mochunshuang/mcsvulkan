#include <iostream>
#include <exception>

#include <meta>

namespace meta = std::meta;

int main()
try
{
    // std::vector<std::meta::info> infos;   //NOTE: 不许存在在运行时
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