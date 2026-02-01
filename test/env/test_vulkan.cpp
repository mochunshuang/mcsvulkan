#include <iostream>

#include <vulkan/vulkan_core.h>
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

    std::cout << "main done\n";
    return 0;
}
void chk(bool result);
