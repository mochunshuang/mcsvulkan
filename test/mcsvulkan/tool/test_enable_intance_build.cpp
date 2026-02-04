#include <cassert>
#include <exception>
#include <iostream>
#include <print>

#include "../head.hpp"

using Instance = mcs::vulkan::Instance;
using create_instance = mcs::vulkan::tool::create_instance;
using mcs::vulkan::tool::enable_intance_build;

using mcs::vulkan::MCS_ASSERT;

int main()
try
{
    using mcs::vulkan::check_vkresult;
    check_vkresult(::volkInitialize());

    auto enables = enable_intance_build{}.enableDebugExtension().enableValidationLayer();
    MCS_ASSERT(enables.extensionContains(VK_EXT_DEBUG_UTILS_EXTENSION_NAME));
    MCS_ASSERT(enables.layerContains("VK_LAYER_KHRONOS_validation"));
    enables.check();

    MCS_ASSERT(
        std::ranges::find(enables.enabledLayers(), "VK_LAYER_KHRONOS_validation") !=
        enables.enabledLayers().end());
    MCS_ASSERT(std::ranges::find(enables.enabledExtensions(),
                                 VK_EXT_DEBUG_UTILS_EXTENSION_NAME) !=
               enables.enabledExtensions().end());

    ::volkFinalize();
    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}