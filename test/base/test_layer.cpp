#include <assert.h>
#include <vector>
#define VOLK_IMPLEMENTATION
#include <volk.h>

#include <print>

/*
typedef struct VkLayerProperties {
    char        layerName[VK_MAX_EXTENSION_NAME_SIZE];
    uint32_t    specVersion;
    uint32_t    implementationVersion;
    char        description[VK_MAX_DESCRIPTION_SIZE];
} VkLayerProperties;
*/
// 实现 std::print 适配
template <>
struct std::formatter<VkLayerProperties> : std::formatter<std::string_view>
{
    // 优化后的格式化函数，专为Vulkan调试设计
    auto format(const VkLayerProperties &layer, std::format_context &ctx) const
    {
        // 将Vulkan版本号转换为可读格式
        auto versionToString = [](uint32_t version) -> std::string {
            return std::format("{}.{}.{}", VK_VERSION_MAJOR(version),
                               VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));
        };

        // 获取层名称并去除可能的尾部空格
        std::string layerNameStr(layer.layerName);
        layerNameStr.erase(layerNameStr.find_last_not_of(' ') + 1);

        // 格式化输出，针对Vulkan开发者优化可读性
        std::string info = std::format("Vulkan Layer: {}\n"
                                       "├─ API Version: {}\n"
                                       "├─ Impl Version: {}\n"
                                       "└─ Description: {}\n",
                                       layerNameStr, versionToString(layer.specVersion),
                                       layer.implementationVersion, layer.description);

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
    assert(vkEnumerateInstanceLayerProperties != nullptr);
    // Provided by VK_VERSION_1_0
    uint32_t count; // NOLINT
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> properties(count);
    vkEnumerateInstanceLayerProperties(&count, properties.data());

    for (auto &one : properties)
    {
        std::println("{}", one);
    }
    std::println("properties size: {}", properties.size());

    std::println("main done\n");
    return 0;
}