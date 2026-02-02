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

    // 集成volk
    static_assert(VMA_STATIC_VULKAN_FUNCTIONS == 0, "should pass");
    static_assert(VMA_DYNAMIC_VULKAN_FUNCTIONS == 0, "should pass");

    // NOTE: 默认最高版本
    //  VMA_VULKAN_VERSION 1004000
    static_assert(VMA_VULKAN_VERSION == 1004000, "should pass"); // NOLINT

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}