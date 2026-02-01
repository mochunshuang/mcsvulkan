#include <cassert>
#include <exception>
#include <iostream>
#include <print>

#include "../head.hpp"

using Instance = mcs::vulkan::Instance;
using create_instance = mcs::vulkan::tool::create_instance;
using create_debuger = mcs::vulkan::tool::create_debuger;
using mcs::vulkan::vkMakeVersion;
using mcs::vulkan::vkApiVersion;

using mcs::vulkan::tool::enable_bulid;

using mcs::vulkan::raii_vulkan;

int main()
try
{
    using mcs::vulkan::check_vkresult;
    raii_vulkan ctx{};

    constexpr auto APIVERSION = vkApiVersion(0, 1, 4, 0);
    auto enables = enable_bulid{}.enableDebugExtension().enableValidationLayer();
    enables.check();
    Instance instance =
        create_instance{}
            .setCreateInfo(
                {.applicationInfo = {.pApplicationName = "Hello Triangle",
                                     .applicationVersion = vkMakeVersion(1, 0, 0),
                                     .pEngineName = "No Engine",
                                     .engineVersion = vkMakeVersion(1, 0, 0),
                                     // apiVersion必须是应用程序设计使用的Vulkan的最高版本
                                     .apiVersion = APIVERSION},
                 .enabledLayers = enables.enabledLayers(),
                 .enabledExtensions = enables.enabledExtensions()})
            .build();
    auto debuger = create_debuger{}
                       .setCreateInfo(create_debuger::defaultCreateInfo())
                       .build(instance);

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}