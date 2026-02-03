#pragma once

#include "Instance.hpp"

namespace mcs::vulkan
{
    class PhysicalDevice
    {
        using value_type = VkPhysicalDevice;
        const Instance *instance_{};
        value_type value_{};

      public:
        constexpr operator bool() const noexcept // NOLINT
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
        PhysicalDevice() = default;
        constexpr PhysicalDevice(const Instance &instance,
                                 VkPhysicalDevice value) noexcept
            : instance_{&instance}, value_{value}
        {
        }
        [[nodiscard]] auto enumeratePhysicalDevices() const
        {
            return instance_->enumeratePhysicalDevices();
        }
        [[nodiscard]] VkAllocationCallbacks *allocator() const noexcept
        {
            return instance_->allocator();
        }
        [[nodiscard]] auto *instance() const noexcept
        {
            return instance_;
        }
        void destroySurfaceKHR(VkSurfaceKHR surface,
                               const VkAllocationCallbacks *pAllocator) const noexcept
        {
            instance_->destroySurfaceKHR(surface, pAllocator);
        }
        [[nodiscard]] auto getProperties() const noexcept
        {
            return Instance::getPhysicalDeviceProperties(value_);
        }
        [[nodiscard]] auto getQueueFamilyProperties() const
        {
            return Instance::getPhysicalDeviceQueueFamilyProperties(value_);
        }
        [[nodiscard]] auto enumerateDeviceExtensionProperties() const
        {
            return Instance::enumerateDeviceExtensionProperties(value_);
        }
        constexpr auto getFeatures2(VkPhysicalDeviceFeatures2 *pFeatures) const noexcept
        {
            Instance::getPhysicalDeviceFeatures2(value_, pFeatures);
        }

        [[nodiscard]] auto getMemoryProperties() const noexcept
        {
            return Instance::getPhysicalDeviceMemoryProperties(value_);
        }

        constexpr bool getSurfaceSupportKHR(uint32_t queueFamilyIndex,
                                            VkSurfaceKHR surface) const
        {
            return Instance::getPhysicalDeviceSurfaceSupportKHR(value_, queueFamilyIndex,
                                                                surface);
        }
        constexpr VkDevice createDevice(const VkDeviceCreateInfo *pCreateInfo,
                                        const VkAllocationCallbacks *pAllocator) const
        {
            return Instance::createDevice(value_, pCreateInfo, pAllocator);
        }
        constexpr auto getSurfaceCapabilitiesKHR(VkSurfaceKHR surface) const
        {
            return Instance::getPhysicalDeviceSurfaceCapabilitiesKHR(value_, surface);
        }
        constexpr auto getSurfaceFormatsKHR(VkSurfaceKHR surface) const
        {
            return Instance::getPhysicalDeviceSurfaceFormatsKHR(value_, surface);
        }
        constexpr auto getSurfacePresentModesKHR(VkSurfaceKHR surface) const
        {
            return Instance::getPhysicalDeviceSurfacePresentModesKHR(value_, surface);
        }
    };
}; // namespace mcs::vulkan
