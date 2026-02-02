#pragma once

#include "utils/check_vkresult.hpp"
#include "utils/make_vk_exception.hpp"
#include "utils/mcs_assert.hpp"

#include <utility>
#include <vector>

namespace mcs::vulkan
{
    // 即使实现支持该扩展，除非在 VkInstance 或 VkDevice
    // 创建时启用该扩展，否则使用该扩展的功能是未定义的行为。
    class Instance
    {
        using value_type = VkInstance;
        value_type value_{};
        VkAllocationCallbacks *allocator_{};

      public:
        Instance() = default;
        constexpr explicit Instance(value_type value,
                                    VkAllocationCallbacks *pAllocator) noexcept
            : value_{value}, allocator_{pAllocator}
        {
            MCS_ASSERT(value_ != nullptr);
            // NOTE: load fun_ptr
            ::volkLoadInstanceOnly(value_);
        }
        Instance(const Instance &) = default;
        constexpr Instance(Instance &&o) noexcept
            : value_(std::exchange(o.value_, {})),
              allocator_{std::exchange(o.allocator_, {})} {};
        Instance &operator=(const Instance &) = default;
        constexpr Instance &operator=(Instance &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                value_ = std::exchange(o.value_, {});
                allocator_ = std::exchange(o.allocator_, {});
            }
            return *this;
        };
        constexpr ~Instance() noexcept
        {
            destroy();
        }
        // NOLINTNEXTLINE
        constexpr operator bool() const noexcept
        {
            return value_ != nullptr;
        }
        constexpr value_type &operator*() noexcept
        {
            return value_;
        }
        constexpr const value_type &operator*() const noexcept
        {
            return value_;
        }
        constexpr void destroy() noexcept
        {
            if (value_ != nullptr)
            {
                vkDestroyInstance(value_, allocator_);
                value_ = {};
                allocator_ = {};
            }
        }
        [[nodiscard]] VkAllocationCallbacks *allocator() const noexcept
        {
            return allocator_;
        }

        constexpr auto createDebugUtilsMessengerEXT(
            const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
            const VkAllocationCallbacks *pAllocator) const
        {
            MCS_ASSERT(vkCreateDebugUtilsMessengerEXT != nullptr);
            VkDebugUtilsMessengerEXT messenger; // NOLINT
            check_vkresult(vkCreateDebugUtilsMessengerEXT(value_, pCreateInfo, pAllocator,
                                                          &messenger),
                           "createDebugUtilsMessengerEXT error");
            return messenger;
        }
        constexpr void destroyDebugUtilsMessengerEXT(
            VkDebugUtilsMessengerEXT debug_,
            const VkAllocationCallbacks *pAllocator) const noexcept
        {
            MCS_ASSERT(vkDestroyDebugUtilsMessengerEXT != nullptr);
            vkDestroyDebugUtilsMessengerEXT(value_, debug_, pAllocator);
        }
        constexpr void destroySurfaceKHR(
            VkSurfaceKHR surface, const VkAllocationCallbacks *pAllocator) const noexcept
        {
            MCS_ASSERT(vkDestroySurfaceKHR != nullptr);
            vkDestroySurfaceKHR(value_, surface, pAllocator);
        }

        [[nodiscard]] constexpr auto enumeratePhysicalDevices() const
        {
            MCS_ASSERT(vkEnumeratePhysicalDevices != nullptr);
            uint32_t gpuCount; // NOLINT
            vkEnumeratePhysicalDevices(value_, &gpuCount, nullptr);
            if (gpuCount == 0)
                throw make_vk_exception("No device with Vulkan support found");

            std::vector<VkPhysicalDevice> physicalDevices{gpuCount};
            check_vkresult(
                vkEnumeratePhysicalDevices(value_, &gpuCount, physicalDevices.data()),
                "enumeratePhysicalDevices error");
            return physicalDevices;
        }

        static constexpr auto getPhysicalDeviceProperties(
            VkPhysicalDevice physicalDevice) noexcept
        {
            MCS_ASSERT(vkGetPhysicalDeviceProperties != nullptr);
            VkPhysicalDeviceProperties deviceProperties;
            vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
            return deviceProperties;
        }
        static constexpr auto getPhysicalDeviceQueueFamilyProperties(
            VkPhysicalDevice physicalDevice)
        {
            MCS_ASSERT(vkGetPhysicalDeviceQueueFamilyProperties != nullptr);
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                                     nullptr);
            std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                                     queueFamilies.data());
            return queueFamilies;
        }

        [[nodiscard]] static auto enumerateDeviceExtensionProperties(
            VkPhysicalDevice physicalDevice)
        {
            MCS_ASSERT(vkEnumerateDeviceExtensionProperties != nullptr);
            uint32_t extensionCount; // NOLINT
            check_vkresult(vkEnumerateDeviceExtensionProperties(
                physicalDevice, nullptr, &extensionCount, nullptr));
            std::vector<VkExtensionProperties> availableDeviceExtensions{extensionCount};
            check_vkresult(vkEnumerateDeviceExtensionProperties(
                physicalDevice, nullptr, &extensionCount,
                availableDeviceExtensions.data()));
            return availableDeviceExtensions;
        }
        [[nodiscard]] static auto getPhysicalDeviceFeatures2(
            VkPhysicalDevice physicalDevice,
            VkPhysicalDeviceFeatures2 *pFeatures) noexcept
        {
            MCS_ASSERT(vkGetPhysicalDeviceFeatures2 != nullptr);
            vkGetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
        }

        [[nodiscard]] static auto getPhysicalDeviceMemoryProperties(
            VkPhysicalDevice physicalDevice) noexcept
        {
            MCS_ASSERT(vkGetPhysicalDeviceMemoryProperties != nullptr);
            VkPhysicalDeviceMemoryProperties prop;
            vkGetPhysicalDeviceMemoryProperties(physicalDevice, &prop);
            return prop;
        }
        [[nodiscard]] static auto getPhysicalDeviceSurfaceSupportKHR(
            VkPhysicalDevice physicalDevice, size_t queueFamilyIndex,
            VkSurfaceKHR surface)
        {
            MCS_ASSERT(vkGetPhysicalDeviceSurfaceSupportKHR != nullptr);
            VkBool32 support; // NOLINT
            check_vkresult(vkGetPhysicalDeviceSurfaceSupportKHR(
                physicalDevice, queueFamilyIndex, surface, &support));
            return support == VK_TRUE;
        }
        static constexpr auto createDevice(VkPhysicalDevice physicalDevice,
                                           const VkDeviceCreateInfo *pCreateInfo,
                                           const VkAllocationCallbacks *pAllocator)
        {
            MCS_ASSERT(vkCreateDevice != nullptr);
            VkDevice device; // NOLINT
            check_vkresult(
                vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, &device));
            return device;
        }

        static constexpr auto getPhysicalDeviceSurfaceCapabilitiesKHR(
            VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
        {
            MCS_ASSERT(vkGetPhysicalDeviceSurfaceCapabilitiesKHR != nullptr);
            VkSurfaceCapabilitiesKHR surfaceCapabilities;
            check_vkresult(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                physicalDevice, surface, &surfaceCapabilities));
            return surfaceCapabilities;
        }
        static constexpr auto getPhysicalDeviceSurfaceFormatsKHR(
            VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
        {
            MCS_ASSERT(vkGetPhysicalDeviceSurfaceFormatsKHR != nullptr);
            uint32_t formatCount; // NOLINT
            check_vkresult(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface,
                                                                &formatCount, nullptr));
            std::vector<VkSurfaceFormatKHR> formats(formatCount);
            check_vkresult(vkGetPhysicalDeviceSurfaceFormatsKHR(
                physicalDevice, surface, &formatCount, formats.data()));
            return formats;
        }

        static constexpr auto getPhysicalDeviceSurfacePresentModesKHR(
            VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
        {
            MCS_ASSERT(vkGetPhysicalDeviceSurfacePresentModesKHR != nullptr);
            uint32_t presentModeCount; // NOLINT
            check_vkresult(vkGetPhysicalDeviceSurfacePresentModesKHR(
                physicalDevice, surface, &presentModeCount, nullptr));
            std::vector<VkPresentModeKHR> presentModes(presentModeCount);
            check_vkresult(vkGetPhysicalDeviceSurfacePresentModesKHR(
                physicalDevice, surface, &presentModeCount, presentModes.data()));
            return presentModes;
        }
    };
}; // namespace mcs::vulkan