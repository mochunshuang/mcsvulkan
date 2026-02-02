#include <cassert>
#include <exception>
#include <iostream>

#include <print>

#include "../head.hpp"

using Instance = mcs::vulkan::Instance;
using create_instance = mcs::vulkan::tool::create_instance;
using create_debuger = mcs::vulkan::tool::create_debuger;
using physical_device_selector = mcs::vulkan::tool::physical_device_selector;

using mcs::vulkan::raii_vulkan;

using surface = mcs::vulkan::wsi::glfw::Window;

int main()
try
{
    using mcs::vulkan::check_vkresult;
    raii_vulkan ctx{};

    // 确定不受到这个影响

#ifdef VK_USE_PLATFORM_WIN32_KHR
    std::cout << "define : VK_USE_PLATFORM_WIN32_KHR\n";
    static_assert(false, "should not match.");
#endif // VK_USE_PLATFORM_WIN32_KHR

#ifdef MY_VOLK_DEFINITION
    std::cout << "define : MY_VOLK_DEFINITION\n";
#endif // MY_VOLK_DEFINITION

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}