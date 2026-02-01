#include <cassert>
#include <exception>
#include <iostream>
#include <print>
#include <span>
#include <utility>
#include <vector>

#include "../head.hpp"

using Instance = mcs::vulkan::Instance;
using create_instance = mcs::vulkan::tool::create_instance;
using mcs::vulkan::vkMakeVersion;
using mcs::vulkan::vkApiVersion;
using mcs::vulkan::tool::sType;

using mcs::vulkan::MCS_ASSERT;

using mcs::vulkan::tool::enable_bulid;

using mcs::vulkan::raii_vulkan;

int main()
try
{
    using mcs::vulkan::check_vkresult;
    raii_vulkan ctx{};
    {
        Instance instace{};
        assert(not instace);
    }

    constexpr auto APIVERSION = vkApiVersion(0, 1, 4, 0);

    using app_info = create_instance::app_info;

    {
        app_info info{.pApplicationName = "Hello Triangle",
                      .applicationVersion = vkMakeVersion(1, 0, 0),
                      .pEngineName = "No Engine",
                      .engineVersion = vkMakeVersion(1, 0, 0),
                      // apiVersion必须是应用程序设计使用的Vulkan的最高版本
                      .apiVersion = APIVERSION};
        VkApplicationInfo c{.sType = sType<VkApplicationInfo>(),
                            .pApplicationName = "Hello Triangle",
                            .applicationVersion = vkMakeVersion(1, 0, 0),
                            .pEngineName = "No Engine",
                            .engineVersion = vkMakeVersion(1, 0, 0),
                            // apiVersion必须是应用程序设计使用的Vulkan的最高版本
                            .apiVersion = APIVERSION};

        MCS_ASSERT(info().sType == c.sType);
        MCS_ASSERT(info.pApplicationName == c.pApplicationName);
        MCS_ASSERT(info.applicationVersion == c.applicationVersion);
        MCS_ASSERT(info.pEngineName == c.pEngineName);
        MCS_ASSERT(info.engineVersion == c.engineVersion);
        MCS_ASSERT(info.apiVersion == c.apiVersion);
    }
    {
        // 方法1.1：显式指定大小的静态数组
        const char *layers[] = {"abc", "ace"}; // NOLINT
        std::span<const char *const> a{layers};
        MCS_ASSERT(a.size() == 2);

        std::vector<const char *> layers2 = {"abc", "ace"};
        std::span<const char *const> a2{layers2};
        MCS_ASSERT(a2.size() == 2);

        std::span<const char *const> a3{};
        MCS_ASSERT(a3.size() == 0);
        MCS_ASSERT(a3.data() == nullptr);

        layers2 = {};
        MCS_ASSERT(layers2.size() == 0);
        MCS_ASSERT(layers2.data() != nullptr); // NOTE: 这里需要注意
    }

    auto enables = enable_bulid{}.enableDebugExtension().enableValidationLayer();
    enables.check();

    {
        VkApplicationInfo app{.sType = sType<VkApplicationInfo>(),
                              .pApplicationName = "Hello Triangle",
                              .applicationVersion = vkMakeVersion(1, 0, 0),
                              .pEngineName = "No Engine",
                              .engineVersion = vkMakeVersion(1, 0, 0),
                              // apiVersion必须是应用程序设计使用的Vulkan的最高版本
                              .apiVersion = APIVERSION};

        std::vector<const char *> enabledLayers{"VK_LAYER_KHRONOS_validation"};
        std::vector<const char *> enabledExtensions{VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
        VkInstanceCreateInfo CI = {.sType = sType<VkInstanceCreateInfo>(),
                                   .pApplicationInfo = &app,
                                   .enabledLayerCount = 1,
                                   .ppEnabledLayerNames = enabledLayers.data(),
                                   .enabledExtensionCount = 1,
                                   .ppEnabledExtensionNames = enabledExtensions.data()};
        VkInstance instance; // NOLINT
        VkResult ret = vkCreateInstance(&CI, nullptr, &instance);
        MCS_ASSERT(ret == VK_SUCCESS);
        MCS_ASSERT(instance != nullptr);

        // 创建实例后，需要加载实例函数指针 //NOTE: 没有这个将崩溃
        ::volkLoadInstanceOnly(instance); // 加载实例级别函数指针

        MCS_ASSERT(vkDestroyInstance != nullptr);
        vkDestroyInstance(instance, nullptr);

        auto create = std::move(create_instance{}.setCreateInfo(
            {.applicationInfo = {.pApplicationName = "Hello Triangle",
                                 .applicationVersion = vkMakeVersion(1, 0, 0),
                                 .pEngineName = "No Engine",
                                 .engineVersion = vkMakeVersion(1, 0, 0),
                                 // apiVersion必须是应用程序设计使用的Vulkan的最高版本
                                 .apiVersion = APIVERSION},
             .enabledLayers = enables.enabledLayers(),
             .enabledExtensions = enables.enabledExtensions()}));

        const auto &createInfo = create.createInfo();
        MCS_ASSERT(createInfo.applicationInfo.apiVersion == app.apiVersion);

        auto CI2 = create.createInfo()();
        MCS_ASSERT(CI2.app.apiVersion == app.apiVersion);
    }

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

    MCS_ASSERT(*instance != nullptr);
    MCS_ASSERT(instance);

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}