#include <cassert>
#include <exception>
#include <iostream>
#include <print>

#include "../head.hpp"

using Instance = mcs::vulkan::Instance;
using create_instance = mcs::vulkan::tool::create_instance;
using create_debuger = mcs::vulkan::tool::create_debuger;
using physical_device_selector = mcs::vulkan::tool::physical_device_selector;
using mcs::vulkan::vkMakeVersion;
using mcs::vulkan::vkApiVersion;

using mcs::vulkan::tool::enable_intance_bulid;
using mcs::vulkan::tool::structure_chain;
using mcs::vulkan::tool::queue_family_index_selector;
using mcs::vulkan::tool::create_logical_device;

using mcs::vulkan::raii_vulkan;
using mcs::vulkan::PhysicalDevice;
using mcs::vulkan::surface_impl;
using mcs::vulkan::surface_interface;
using surface = mcs::vulkan::wsi::glfw::Window;
using mcs::vulkan::Queue;

using raii_vma = mcs::vulkan::raii_vma;

using mcs::vulkan::LogicalDevice;

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr auto TITLE = "test_my_triangle";
using mcs::vulkan::MCS_ASSERT;

static bool isAMDGPU(VkPhysicalDevice physicalDevice)
{
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);

    // AMD 的 vendor ID 是 0x1002
    return properties.vendorID == 0x1002;
}

constexpr auto VMA_USE_BIND_MEMORY2 = true;
constexpr auto VMA_USE_MEMORY_BUDGET = true;
constexpr auto VMA_USE_BUFFER_DEVICE_ADDRESS = true;

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

    auto enables = enable_intance_bulid{}
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
    auto debuger = create_debuger{}
                       .setCreateInfo(create_debuger::defaultCreateInfo())
                       .build(instance);

    std::vector<const char *> requiredDeviceExtension = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME}; // NOLINTEND

    // NOTE: 配置 vma 的 flags
    VmaAllocatorCreateFlags vma_flags = 0;
    // @see
    // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/quick_start.html#:~:text=of%20this%20function.-,Enabling%20extensions,-VMA%20can%20automatically
#if VK_USE_PLATFORM_WIN32_KHR
    std::cout << "WIN32 \n";
    requiredDeviceExtension.emplace_back("VK_KHR_external_memory_win32");
    vma_flags |= VMA_ALLOCATOR_CREATE_KHR_EXTERNAL_MEMORY_WIN32_BIT;
#endif
    if constexpr (VMA_USE_BIND_MEMORY2)
    {
        requiredDeviceExtension.emplace_back("VK_KHR_bind_memory2");
        vma_flags |= VMA_ALLOCATOR_CREATE_KHR_BIND_MEMORY2_BIT;
    }
    if constexpr (VMA_USE_MEMORY_BUDGET)
    {
        requiredDeviceExtension.emplace_back("VK_EXT_memory_budget");
        vma_flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    }
    if constexpr (VMA_USE_BUFFER_DEVICE_ADDRESS)
    {
        requiredDeviceExtension.emplace_back("VK_KHR_buffer_device_address");
        vma_flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    }

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

    if (isAMDGPU(*physical_device))
    {
        std::cout << "amd gpu \n";
        requiredDeviceExtension.emplace_back(
            VK_AMD_DEVICE_COHERENT_MEMORY_EXTENSION_NAME);
        vma_flags |= VMA_ALLOCATOR_CREATE_AMD_DEVICE_COHERENT_MEMORY_BIT;
    }

    // VkSurfaceKHR surface = window.createVkSurfaceKHR(*instance);
    auto surface = surface_impl(instance, window);
    [[maybe_unused]] surface_interface *si = &surface;

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
                .queueCreateInfos = create_logical_device::makeQueueCreateInfos(
                    create_logical_device::queue_create_info{
                        .queueFamilyIndex = GRAPHICS_QUEUE_FAMILY_IDX,
                        .queueCount = 1,
                        .queuePrioritie = 1.0}),
                .enabledExtensions = requiredDeviceExtension,
                .enabledFeatures2 = &enablefeatureChain.head(),
            })
            .build(physical_device);
    requiredDeviceExtension.clear();

    MCS_ASSERT(device);

    const auto GRAPHICS_AND_PRESENT = Queue(
        device, {.queue_family_index = GRAPHICS_QUEUE_FAMILY_IDX, .queue_index = 0});

    raii_vma vma{{.flags = vma_flags,
                  .physicalDevice = *physical_device,
                  .device = *device,
                  .instance = *instance,
                  .vulkanApiVersion = APIVERSION}};

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