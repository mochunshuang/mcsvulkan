#pragma once

#include "../__vulkan.hpp"
#include <vector>

namespace mcs::vulkan::tool
{
    class Initialization
    {
      public:
        static constexpr std::vector<VkLayerProperties> instanceLayerProperties(
            VkLayerProperties *pProperties = nullptr)
        {
            uint32_t layerCount; // NOLINT
            vkEnumerateInstanceLayerProperties(&layerCount, pProperties);
            std::vector<VkLayerProperties> availableLayers{layerCount};
            vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
            return availableLayers;
        }

        static constexpr std::vector<VkExtensionProperties> instanceExtensionPropertie(
            const char *pLayerName = nullptr)
        {
            // 枚举全局扩展（不属于任何层）
            uint32_t extensionCount = 0;
            vkEnumerateInstanceExtensionProperties(pLayerName, &extensionCount, nullptr);

            std::vector<VkExtensionProperties> availableinstanceExtensions{
                extensionCount};
            vkEnumerateInstanceExtensionProperties(pLayerName, &extensionCount,
                                                   availableinstanceExtensions.data());
            return availableinstanceExtensions;
        }
    };

}; // namespace mcs::vulkan::tool