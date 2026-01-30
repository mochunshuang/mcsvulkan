#define VOLK_IMPLEMENTATION
#include <volk.h>
#include <cstdio>

// NOLINTBEGIN
int main()
{
    printf("1. 正在初始化Volk并加载 vulkan-1.dll...\n");

    // Volk核心：动态加载vulkan-1.dll并获取函数指针
    VkResult result = volkInitialize();
    if (result != VK_SUCCESS)
    {
        printf("   失败！Vulkan不可用或加载出错。错误码: %d\n", result);
        return 1;
    }
    printf("   ✓ 成功！系统vulkan-1.dll已加载。\n");

    // 尝试获取Vulkan实例版本（一个简单的API调用）
    printf("\n2. 尝试获取Vulkan实例版本...\n");
    uint32_t apiVersion = 0;
    // vkEnumerateInstanceVersion 是全局函数，volkInitialize后即可调用
    result = vkEnumerateInstanceVersion(&apiVersion);
    if (result == VK_SUCCESS)
    {
        // 解析版本号
        uint32_t major = VK_VERSION_MAJOR(apiVersion);
        uint32_t minor = VK_VERSION_MINOR(apiVersion);
        uint32_t patch = VK_VERSION_PATCH(apiVersion);
        printf("   ✓ 成功！Vulkan版本: %d.%d.%d (API版本号: %#x)\n", major, minor, patch,
               apiVersion);
    }
    else
    {
        printf("   ✗ 获取版本失败。错误码: %d\n", result);
    }

    printf("\n3. 测试完成。\n");
    printf("   这表明：\n");
    printf("   a) vulkan-1.dll 文件有效。\n");
    printf("   b) Volk可以在不链接 vulkan-1.lib 的情况下工作。\n");
    printf("   c) 系统Vulkan运行时基本正常。\n");
    return 0;
}
// NOLINTEND