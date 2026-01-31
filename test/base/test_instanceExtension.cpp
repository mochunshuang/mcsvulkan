#include <assert.h>
#include <cstdint>
#include <vector>
#define VOLK_IMPLEMENTATION
#include <volk.h>

#include <print>

/*
typedef struct VkExtensionProperties {
    char        extensionName[VK_MAX_EXTENSION_NAME_SIZE];
    uint32_t    specVersion;
} VkExtensionProperties;
*/
// 实现 std::print 适配
template <>
struct std::formatter<VkExtensionProperties> : std::formatter<std::string_view>
{
    auto format(const VkExtensionProperties &ext, std::format_context &ctx) const
    {
        std::string extNameStr(ext.extensionName);
        extNameStr.erase(extNameStr.find_last_not_of(' ') + 1);

        std::string info = std::format("Extension: {} (Spec: {}.{}.{})", extNameStr,
                                       VK_VERSION_MAJOR(ext.specVersion),
                                       VK_VERSION_MINOR(ext.specVersion),
                                       VK_VERSION_PATCH(ext.specVersion));

        return formatter<std::string_view>::format(info, ctx);
    }
};

int main()
{
    if (auto result = volkInitialize(); result != VK_SUCCESS)
    {
        std::print("   失败！Vulkan不可用或加载出错。错误码: %d\n",
                   static_cast<int>(result));
        return 1;
    }
    assert(vkEnumerateInstanceExtensionProperties != nullptr);
    uint32_t count; // NOLINT
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extension{count};
    vkEnumerateInstanceExtensionProperties(nullptr, &count, extension.data());
    for (auto &one : extension)
    {
        std::println("{}", one);
    }
    std::println("extension size: {}", extension.size());

    std::println("main done\n");
    return 0;
}