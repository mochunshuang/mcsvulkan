// vulkan_minimal.cpp - 真正的单文件Vulkan加载器

// 关键：包含官方的Vulkan头文件
// 这避免了所有重复定义问题
#ifdef __cplusplus
extern "C"
{
#endif

// 只包含最少的Vulkan头文件
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>

#ifdef __cplusplus
}
#endif

#include <windows.h>
#include <stdio.h>

// 我们的函数指针表
static PFN_vkGetInstanceProcAddr g_vkGetInstanceProcAddr = nullptr;
static PFN_vkEnumerateInstanceVersion g_vkEnumerateInstanceVersion = nullptr;
static PFN_vkCreateInstance g_vkCreateInstance = nullptr;
static HMODULE g_vulkanLib = nullptr;

// 初始化函数
VkResult VulkanMinimal_Init()
{
    g_vulkanLib = LoadLibraryA("vulkan-1.dll");
    if (!g_vulkanLib)
    {
        printf("Error: Failed to load vulkan-1.dll\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    g_vkGetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr)GetProcAddress(g_vulkanLib, "vkGetInstanceProcAddr");

    if (!g_vkGetInstanceProcAddr)
    {
        printf("Error: vkGetInstanceProcAddr not found\n");
        FreeLibrary(g_vulkanLib);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // 获取基础函数
    g_vkEnumerateInstanceVersion =
        (PFN_vkEnumerateInstanceVersion)g_vkGetInstanceProcAddr(
            nullptr, "vkEnumerateInstanceVersion");

    g_vkCreateInstance =
        (PFN_vkCreateInstance)g_vkGetInstanceProcAddr(nullptr, "vkCreateInstance");

    printf("Vulkan minimal loader initialized\n");
    return VK_SUCCESS;
}

// 加载实例函数
void VulkanMinimal_LoadInstance(VkInstance instance)
{
    // 这里可以动态加载其他函数
    // 但类型都来自官方头文件
}

// 清理
void VulkanMinimal_Destroy()
{
    if (g_vulkanLib)
    {
        FreeLibrary(g_vulkanLib);
        g_vulkanLib = nullptr;
    }
    g_vkGetInstanceProcAddr = nullptr;
    g_vkEnumerateInstanceVersion = nullptr;
    g_vkCreateInstance = nullptr;
}

// 测试函数
void TestVulkan()
{
    if (VulkanMinimal_Init() != VK_SUCCESS)
    {
        return;
    }

    // 使用来自vulkan_core.h的类型
    uint32_t version = 0;
    if (g_vkEnumerateInstanceVersion)
    {
        g_vkEnumerateInstanceVersion(&version);
        printf("Vulkan version: %d.%d.%d\n", VK_VERSION_MAJOR(version),
               VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));
    }

    // 创建实例
    if (g_vkCreateInstance)
    {
        VkInstance instance = nullptr;

        // 使用官方定义的结构体
        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "MinimalTest";
        appInfo.apiVersion = version;

        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        VkResult result = g_vkCreateInstance(&createInfo, nullptr, &instance);
        if (result == VK_SUCCESS)
        {
            printf("Instance created successfully!\n");

            // 加载更多函数
            PFN_vkDestroyInstance vkDestroyInstance =
                (PFN_vkDestroyInstance)g_vkGetInstanceProcAddr(instance,
                                                               "vkDestroyInstance");

            if (vkDestroyInstance)
            {
                vkDestroyInstance(instance, nullptr);
                printf("Instance destroyed\n");
            }
        }
    }

    VulkanMinimal_Destroy();
}

int main()
{
    printf("Testing minimal Vulkan loader...\n");
    TestVulkan();
    return 0;
}