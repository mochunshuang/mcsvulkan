#include <iostream>
#include <vector>
#include <vulkan/vulkan.hpp>

static inline void chk(vk::Result result)
{
    if (result != vk::Result::eSuccess)
    {
        std::cerr << "Vulkan call returned an error\n";
        std::abort();
    }
}

int main()
{
    // 1. 应用信息
    vk::ApplicationInfo appInfo{};
    appInfo.pApplicationName = "MinimalTest";
    appInfo.apiVersion = VK_API_VERSION_1_4;

    // 2. 实例创建信息
    vk::InstanceCreateInfo createInfo{};
    createInfo.pApplicationInfo = &appInfo;

    // 3. 创建Vulkan实例
    vk::Instance instance{};
    chk(vk::createInstance(&createInfo, nullptr, &instance));

    // 4. 枚举物理设备
    uint32_t deviceCount = 0;
    chk(instance.enumeratePhysicalDevices(&deviceCount, nullptr));
    std::vector<vk::PhysicalDevice> devices(deviceCount);
    chk(instance.enumeratePhysicalDevices(&deviceCount, devices.data()));

    // 5. 选择第一个物理设备
    vk::PhysicalDevice physicalDevice = devices[0];

    // 6. 枚举设备扩展
    uint32_t extensionCount = 0;
    chk(physicalDevice.enumerateDeviceExtensionProperties(nullptr, &extensionCount,
                                                          nullptr));
    std::vector<vk::ExtensionProperties> availableDeviceExtensions(extensionCount);
    chk(physicalDevice.enumerateDeviceExtensionProperties(
        nullptr, &extensionCount, availableDeviceExtensions.data()));

    // 7. 输出结果
    std::cout << "count: " << availableDeviceExtensions.size() << '\n';
    std::cout << "main done\n";

    // 8. 清理
    instance.destroy();
    return 0;
}