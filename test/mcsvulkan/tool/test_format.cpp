#include <cassert>
#include <exception>
#include <iostream>
#include <print>

#include "../head.hpp"

using Instance = mcs::vulkan::Instance;
using create_instance = mcs::vulkan::tool::create_instance;
using create_debugger = mcs::vulkan::tool::create_debugger;
using physical_device_selector = mcs::vulkan::tool::physical_device_selector;
using mcs::vulkan::vkMakeVersion;
using mcs::vulkan::vkApiVersion;

using mcs::vulkan::tool::enable_intance_build;
using mcs::vulkan::tool::structure_chain;
using mcs::vulkan::tool::queue_family_index_selector;
using mcs::vulkan::tool::create_logical_device;

using mcs::vulkan::raii_vulkan;
using mcs::vulkan::PhysicalDevice;
using mcs::vulkan::surface_impl;
using mcs::vulkan::surface_base;
using surface = mcs::vulkan::wsi::glfw::Window;
using mcs::vulkan::Queue;

using mcs::vulkan::LogicalDevice;

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr auto TITLE = "test_my_triangle";
using mcs::vulkan::MCS_ASSERT;

int main()
try
{
    using mcs::vulkan::check_vkresult;
    raii_vulkan ctx{};

    surface window{};
    window.setup({.width = WIDTH, .height = HEIGHT}, TITLE); // NOLINT

    constexpr auto APIVERSION = VK_API_VERSION_1_4;
    static_assert(vkApiVersion(1, 4, 0) == VK_API_VERSION_1_4);
    static_assert(vkApiVersion(0, 1, 4, 0) == VK_API_VERSION_1_4);

    auto enables = enable_intance_build{}
                       .enableDebugExtension()
                       .enableValidationLayer()
                       .enableSurfaceExtension<surface>();
    enables.check();
    enables.print();

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
    auto debuger = create_debugger{}
                       .setCreateInfo(create_debugger::defaultCreateInfo())
                       .build(instance);

    std::vector<const char *> requiredDeviceExtension = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME}; // NOLINTEND
    structure_chain<VkPhysicalDeviceFeatures2, VkPhysicalDeviceVulkan13Features,
                    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>
        enablefeatureChain = {{},
                              {.synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE},
                              {.extendedDynamicState = VK_TRUE}};

    auto [id [[maybe_unused]], physical_device [[maybe_unused]]] =
        physical_device_selector{instance}
            .requiredDeviceExtension(requiredDeviceExtension)
            .requiredProperties([](const VkPhysicalDeviceProperties
                                       &device_properties) constexpr noexcept {
                return device_properties.apiVersion >= VK_API_VERSION_1_3;
            })
            .requiredQueueFamily(
                [](const VkQueueFamilyProperties &qfp) constexpr noexcept {
                    return !!(qfp.queueFlags & VK_QUEUE_GRAPHICS_BIT);
                })
            .requiredFeatures([](const PhysicalDevice &physicalDevice) constexpr noexcept
                                  -> bool {
                auto query =
                    structure_chain<VkPhysicalDeviceFeatures2,
                                    VkPhysicalDeviceVulkan13Features,
                                    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>{
                        {}, {}, {}};
                physicalDevice.getFeatures2(&query.head());
                auto &query_vulkan13_features =
                    query.template get<VkPhysicalDeviceVulkan13Features>();
                auto &query_extended_dynamic_state_features =
                    query.template get<VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>();
                return query_vulkan13_features.dynamicRendering &&
                       query_vulkan13_features.synchronization2 &&
                       query_extended_dynamic_state_features.extendedDynamicState;
            })
            .select()[0];

    // VkSurfaceKHR surface = window.createVkSurfaceKHR(*instance);
    mcs::vulkan::surface auto surface = surface_impl(physical_device, window);
    [[maybe_unused]] surface_base *si = &surface;

    const uint32_t GRAPHICS_QUEUE_FAMILY_IDX =
        queue_family_index_selector{physical_device}
            .requiredQueueFamily([&](const VkQueueFamilyProperties &qfp,
                                     uint32_t queueFamilyIndex) -> bool {
                return (qfp.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                       physical_device.getSurfaceSupportKHR(queueFamilyIndex, *surface);
            })
            .select()[0];

    LogicalDevice device =
        create_logical_device{}
            .setCreateInfo({
                .pNext = make_pNext(enablefeatureChain), // NOTE: 这个替代
                .queueCreateInfos = create_logical_device::makeQueueCreateInfos(
                    create_logical_device::queue_create_info{
                        .queueFamilyIndex = GRAPHICS_QUEUE_FAMILY_IDX,
                        .queueCount = 1,
                        .queuePrioritie = 1.0}),
                .enabledExtensions = requiredDeviceExtension,
                // NOTE: 下面的方式是错误的。因为 features.next=nullptr
                //  .pEnabledFeatures = &enablefeatureChain.head().features,
            })
            .build(physical_device);
    requiredDeviceExtension.clear();

    MCS_ASSERT(device);

    const auto GRAPHICS_AND_PRESENT = Queue(
        device, {.queue_family_index = GRAPHICS_QUEUE_FAMILY_IDX, .queue_index = 0});

    VkFormatProperties props = physical_device.getFormatProperties(VK_FORMAT_R32G32_UINT);
    // 1. 检查格式是否支持图像创建（只要任一特性字段非0，就说明可创建图像）
    if (props.linearTilingFeatures == 0 && props.optimalTilingFeatures == 0 &&
        props.bufferFeatures == 0)
    {
        std::cout << " 格式完全不支持\n";
    }

    // 2. 检查是否能作为渲染目标的颜色附件（使用optimal tiling）
    if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
    {
        std::cout << " 格式支持\n";
    }

    while (window.shouldClose() == 0)
    {
        surface::pollEvents();
        break;
    }

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}