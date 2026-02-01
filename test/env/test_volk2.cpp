#include <iostream>

#define VOLK_IMPLEMENTATION
#include <volk.h>
#include <vector>

static inline void chk(VkResult result)
{
    if (result != VK_SUCCESS)
    {
        std::cerr << "Call returned an error\n";
        std::abort();
    }
}

int main()
{

    chk(volkInitialize());

    // 使用官方定义的结构体
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "MinimalTest";
    appInfo.apiVersion = VK_API_VERSION_1_4;
    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    VkInstance instance = nullptr;
    chk(vkCreateInstance(&createInfo, nullptr, &instance));
    // volkLoadInstance(instance);
    volkLoadInstanceOnly(instance);

    uint32_t deviceCount{0};
    chk(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr));
    std::vector<VkPhysicalDevice> devices(deviceCount);
    chk(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()));

    VkPhysicalDevice physicalDevice = devices[0];
    uint32_t extensionCount; // NOLINT
    chk(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount,
                                             nullptr));
    std::vector<VkExtensionProperties> availableDeviceExtensions{extensionCount};
    chk(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount,
                                             availableDeviceExtensions.data()));

    std::cout << "count: " << availableDeviceExtensions.size() << '\n';
    vkDestroyInstance(instance, nullptr);

    volkFinalize();
    std::cout << "main done\n";
    return 0;
}
