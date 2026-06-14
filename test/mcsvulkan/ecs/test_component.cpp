#include <iostream>
#include <exception>

#include "head.hpp"

using mcs::vulkan::ecs::static_string;
using mcs::vulkan::ecs::soa_class;
using mcs::vulkan::ecs::world;
using mcs::vulkan::ecs::proxy_value;
using mcs::vulkan::ecs::name_spec;

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