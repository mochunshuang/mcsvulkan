#define VOLK_IMPLEMENTATION
#include <volk.h>
#include <vulkan/vulkan_profiles.hpp>
#include <iostream>
#include <vector>

#include <format>

int main()
{
    // 初始化Vulkan
    if (volkInitialize() != VK_SUCCESS)
    {
        std::cerr << "volk初始化失败\n";
        return 1;
    }

    VkInstance instance = VK_NULL_HANDLE;
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;

    if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS)
    {
        std::cerr << "无法创建Vulkan实例\n";
        return 1;
    }

    volkLoadInstance(instance);

    // 获取物理设备
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    if (deviceCount == 0)
    {
        std::cerr << "未找到物理设备\n";
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    VkPhysicalDevice device = devices[0];
    VkPhysicalDeviceProperties devProps;
    vkGetPhysicalDeviceProperties(device, &devProps);

    // ========== 第一步：获取并打印所有Profile名称 ==========
    uint32_t profileCount = 0;
    vpGetProfiles(&profileCount, nullptr);
    std::vector<VpProfileProperties> allProfiles(profileCount);
    vpGetProfiles(&profileCount, allProfiles.data());

    std::cout << std::format("\n=== 第一步：所有可用的Vulkan Profile ({}个) ===\n",
                             profileCount);
    std::cout << std::format("设备: {}\n\n", devProps.deviceName);

    for (uint32_t i = 0; i < profileCount; i++)
    {
        std::cout << std::format("{:3}. {}\n", i + 1, allProfiles[i].profileName);
    }

    // ========== 第二步：检查关键Profile的支持情况 ==========
    std::cout << std::format("\n\n=== 第二步：关键Profile支持检查 ===\n");

    // 定义要详细检查的关键Profile
    const std::vector<const char *> keyProfiles = {
        "VP_KHR_roadmap_2022", "VP_KHR_roadmap_2024", "VP_LUNARG_desktop_baseline_2024",
        "VP_LUNARG_minimum_requirements_1_0"};

    for (const auto &targetName : keyProfiles)
    {
        std::cout << std::format("\n--- 检查: {} ---\n", targetName);
        std::cout << "---------------------------------------------------\n";
        // 在Profile列表中查找
        const VpProfileProperties *targetProfile = nullptr;
        for (const auto &profile : allProfiles)
        {
            if (strcmp(profile.profileName, targetName) == 0)
            {
                targetProfile = &profile;
                break;
            }
        }

        if (!targetProfile)
        {
            std::cout << "❌ 系统中未找到此Profile\n";
            continue;
        }

        // 检查支持情况
        VkBool32 supported = VK_FALSE;
        VkResult result = vpGetPhysicalDeviceProfileSupport(instance, device,
                                                            targetProfile, &supported);

        if (result == VK_SUCCESS)
        {
            if (supported)
            {
                std::cout << std::format("✅ 支持 (规范版本: v{})\n",
                                         targetProfile->specVersion);
            }
            else
            {
                std::cout << std::format("❌ 不支持 (规范版本: v{})\n",
                                         targetProfile->specVersion);
                std::cout << "   原因: 设备不满足此Profile的硬件要求\n";
            }
        }
        else
        {
            std::cout << "⚠️  检查失败 (错误码: " << result << ")\n";
        }
    }

    vkDestroyInstance(instance, nullptr);
    return 0;
}