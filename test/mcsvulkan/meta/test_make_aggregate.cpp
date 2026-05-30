#include "head.hpp"

#include <iostream>
#include <exception>

using mcs::vulkan::meta::make_aggregate_t;

int main()
try
{
    using T = make_aggregate_t<{"x", ^^int}, {"y", ^^double}, {"z", ^^float}>;
    T a{.x = 1, .y = 2, .z = 3};

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