
#define VOLK_IMPLEMENTATION
#include <volk.h>

#include <vector>
#include <cstring>
#include <cstdio>

// 全局回调计数器
static int g_callbackCallCount = 0;

// 验证层回调
static VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              VkDebugUtilsMessageTypeFlagsEXT messageType,
              const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData)
{

    g_callbackCallCount++;

    // 立即无缓冲输出
    printf("[CALLBACK#%d] ", g_callbackCallCount);

    switch (messageSeverity)
    {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        printf("详细");
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        printf("信息");
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        printf("警告");
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        printf("错误");
        break;
    default:
        printf("未知");
    }

    printf(" | %s\n", pCallbackData->pMessage);
    fflush(stdout);

    return VK_FALSE;
}

int main()
{
    printf("=== Volk验证层测试（修正版）===\n");

    // 1. 初始化Volk
    if (volkInitialize() != VK_SUCCESS)
    {
        printf("!!! Volk初始化失败\n");
        return -1;
    }
    printf("✓ Volk初始化成功\n");

    // 2. 创建Vulkan实例（启用验证层 和 调试扩展）
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "VolkValidationTest";
    appInfo.apiVersion = VK_API_VERSION_1_0;

    // 要启用的验证层
    const std::vector<const char *> validationLayers = {"VK_LAYER_KHRONOS_validation"};

    // 关键：要启用的实例扩展（必须包含调试扩展）
    const std::vector<const char *> instanceExtensions = {
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME // 启用调试工具扩展
    };

    VkInstanceCreateInfo instInfo = {};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;
    instInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    instInfo.ppEnabledLayerNames = validationLayers.data();
    // 关键：设置启用扩展
    instInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
    instInfo.ppEnabledExtensionNames = instanceExtensions.data();

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instInfo, nullptr, &instance) != VK_SUCCESS)
    {
        printf("!!! 创建Vulkan实例失败\n");
        return -1;
    }
    printf("✓ Vulkan实例创建成功 (已启用调试扩展)\n");

    // 3. 关键：加载Volk实例函数
    volkLoadInstance(instance);
    printf("✓ Volk实例函数加载完成\n");

    // 4. 创建调试信使 - 使用Volk的安全函数
    VkDebugUtilsMessengerCreateInfoEXT debugInfo = {};
    debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugInfo.pfnUserCallback = debugCallback;
    debugInfo.pUserData = nullptr;

    // 关键修正：使用volkGetInstanceProcAddr而非vkGetInstanceProcAddr
    auto vkCreateDebugUtilsMessengerEXT =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkCreateDebugUtilsMessengerEXT");

    if (!vkCreateDebugUtilsMessengerEXT)
    {
        printf("!!! 错误：volkGetInstanceProcAddr也未能获取函数指针\n");
        printf("    尝试检查：\n");
        printf("    1. 是否在volkLoadInstance()之后调用？\n");
        printf("    2. Vulkan SDK是否正确安装？\n");
        printf("    3. 尝试使用vkGetInstanceProcAddr作为备选...\n");

        // 备选方案
        vkCreateDebugUtilsMessengerEXT =
            (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                instance, "vkCreateDebugUtilsMessengerEXT");

        if (!vkCreateDebugUtilsMessengerEXT)
        {
            printf("!!! 两个方法都失败，可能VK_EXT_debug_utils扩展不支持\n");
            vkDestroyInstance(instance, nullptr);
            return -1;
        }
    }
    printf("✓ 成功获取vkCreateDebugUtilsMessengerEXT函数指针\n");

    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkResult result =
        vkCreateDebugUtilsMessengerEXT(instance, &debugInfo, nullptr, &debugMessenger);

    if (result != VK_SUCCESS)
    {
        printf("!!! 创建调试信使失败: %d\n", result);
        vkDestroyInstance(instance, nullptr);
        return -1;
    }
    printf("✓ 调试信使创建成功\n");

    // 5. 创建设备（必须启用验证层）
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0)
    {
        printf("!!! 没有可用的物理设备\n");
        vkDestroyInstance(instance, nullptr);
        return -1;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    VkPhysicalDevice physicalDevice = devices[0];

    // 查找图形队列
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                             queueFamilies.data());

    uint32_t graphicsQueueIndex = UINT32_MAX;
    for (uint32_t i = 0; i < queueFamilyCount; i++)
    {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            graphicsQueueIndex = i;
            break;
        }
    }

    if (graphicsQueueIndex == UINT32_MAX)
    {
        printf("!!! 没有找到图形队列\n");
        vkDestroyInstance(instance, nullptr);
        return -1;
    }

    // 创建设备（关键：设备也要启用验证层）
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo = {};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = graphicsQueueIndex;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceInfo = {};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    deviceInfo.ppEnabledLayerNames = validationLayers.data();

    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device) != VK_SUCCESS)
    {
        printf("!!! 创建设备失败\n");
        vkDestroyInstance(instance, nullptr);
        return -1;
    }
    printf("✓ 逻辑设备创建成功\n");

    // 6. 加载设备级函数
    volkLoadDevice(device);

    // 7. 触发验证错误
    printf("\n=== 触发验证错误 ===\n");
    printf("初始回调计数: %d\n", g_callbackCallCount);

    // 触发错误1：创建无效的渲染通道
    VkRenderPassCreateInfo rpInfo = {};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    // 缺少必要的attachmentCount和subpassCount

    VkRenderPass badRenderPass = VK_NULL_HANDLE;
    VkResult rpResult = vkCreateRenderPass(device, &rpInfo, nullptr, &badRenderPass);
    printf("创建无效渲染通道 -> 结果: %d\n", rpResult);

    printf("当前回调计数: %d\n", g_callbackCallCount);

    // 8. 测试结果验证
    printf("\n=== 测试结果 ===\n");
    if (g_callbackCallCount == 0)
    {
        printf("!!! 失败：回调函数从未被调用\n");
        printf("可能原因：\n");
        printf("  1. 验证层未被正确加载\n");
        printf("  2. 回调注册时机问题\n");
        printf("  3. 尝试设置环境变量：\n");
        printf("     set VK_LAYER_PATH=E:/mysoftware/VulkanSDK/1.4.321.1/Bin\n");
        printf("     set VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation\n");
    }
    else
    {
        printf("✓ 成功！回调被调用了 %d 次\n", g_callbackCallCount);
        printf("  验证层已正确集成到Volk\n");
    }

    // 9. 清理
    if (badRenderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device, badRenderPass, nullptr);
    }

    vkDestroyDevice(device, nullptr);

    // 使用相同的方法获取销毁函数
    auto vkDestroyDebugUtilsMessengerEXT =
        (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkDestroyDebugUtilsMessengerEXT");

    if (!vkDestroyDebugUtilsMessengerEXT)
    {
        vkDestroyDebugUtilsMessengerEXT =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                instance, "vkDestroyDebugUtilsMessengerEXT");
    }

    if (vkDestroyDebugUtilsMessengerEXT && debugMessenger != VK_NULL_HANDLE)
    {
        vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        printf("✓ 调试信使已销毁\n");
    }

    vkDestroyInstance(instance, nullptr);
    printf("✓ Vulkan实例已销毁\n");
    printf("\n=== 测试结束 ===\n");

    return g_callbackCallCount > 0 ? 0 : 1;
}