#include <iostream>
#include <exception>

#include "head.hpp"
#include <vector>
using mcs::vulkan::meta::static_string;

template <typename R, typename... Args>
auto make_function()
{

    return [](Args &&...args) -> R {
        // 代码字符串？
        return {};
    };
}

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
}