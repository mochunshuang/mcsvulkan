#pragma once

#include "../PhysicalDevice.hpp"
#include <algorithm>
#include <functional>
#include <ranges>
#include <span>
#include <vector>

namespace mcs::vulkan::tool
{
    struct physical_device_selector
    {
        using check_VkPhysicalDeviceProperties =
            std::function<bool(const VkPhysicalDeviceProperties &)>;
        using check_VkQueueFamilyProperties =
            std::function<bool(const VkQueueFamilyProperties &qfp)>;
        using check_features = std::function<bool(const PhysicalDevice &device)>;

        constexpr explicit physical_device_selector(const Instance &instance)
            : devices_{instance.enumeratePhysicalDevices() |
                       std::views::transform(
                           [&](VkPhysicalDevice physicalDevice) constexpr noexcept {
                               return PhysicalDevice{instance, physicalDevice};
                           }) |
                       std::ranges::to<std::vector<PhysicalDevice>>()}
        {
            MCS_ASSERT(not devices_.empty());
        }

        constexpr auto &requiredDeviceExtension(
            std::vector<const char *> &requiredDeviceExtension)
        {
            requiredDeviceExtension_ = requiredDeviceExtension;
            return *this;
        }
        constexpr auto &requiredProperties(
            std::optional<check_VkPhysicalDeviceProperties> requiredProperties)
        {
            requiredProperties_ = std::move(requiredProperties);
            return *this;
        }
        constexpr auto &requiredQueueFamily(
            std::optional<check_VkQueueFamilyProperties> requiredQueueFamily)
        {
            requiredQueueFamily_ = std::move(requiredQueueFamily);
            return *this;
        }
        constexpr auto &requiredFeatures(std::optional<check_features> requiredFeatures)
        {
            requiredFeatures_ = std::move(requiredFeatures);
            return *this;
        }
        struct result
        {
            size_t id{};
            PhysicalDevice physical_device;
        };
        [[nodiscard]] constexpr auto select()
        {
            std::vector<result> ret;

            for (size_t i = 0, size = devices_.size(); i < size; ++i)
            {
                const PhysicalDevice &physicalDevice = devices_[i];

                // Check device properties
                if (requiredProperties_.has_value() &&
                    not(*requiredProperties_)(physicalDevice.getProperties()))
                    continue;

                // Check if any of the queue families support
                if (requiredQueueFamily_.has_value() &&
                    not std::ranges::any_of(physicalDevice.getQueueFamilyProperties(),
                                            *requiredQueueFamily_))
                    continue;

                // Check if all required device extensions are available
                if (not requiredDeviceExtension_.empty() &&
                    not checkDeviceExtensions(physicalDevice))
                    continue;

                // Query for Vulkan 1.3 features
                if (requiredFeatures_.has_value() &&
                    not(*requiredFeatures_)(physicalDevice))
                    continue;

                ret.emplace_back(i, physicalDevice);
            }
            if (ret.empty())
                throw make_vk_exception("failed to find a suitable GPU!.");
            return ret;
        }

      private:
        std::vector<PhysicalDevice> devices_;

        std::span<const char *> requiredDeviceExtension_;
        std::optional<check_VkPhysicalDeviceProperties> requiredProperties_;
        std::optional<check_VkQueueFamilyProperties> requiredQueueFamily_;
        std::optional<check_features> requiredFeatures_;

        [[nodiscard]] constexpr bool checkDeviceExtensions(
            const PhysicalDevice &device) const
        {
            const auto AVAILABLE_DEVICE_EXTENSIONS =
                device.enumerateDeviceExtensionProperties();

            return std::ranges::all_of(
                requiredDeviceExtension_, [&](const char *requiredExtension) noexcept {
                    return std::ranges::any_of(
                        AVAILABLE_DEVICE_EXTENSIONS,
                        [&](const VkExtensionProperties &availableExtension) noexcept {
                            return std::strcmp(availableExtension.extensionName,
                                               requiredExtension) == 0;
                        });
                });
        }
    };
}; // namespace mcs::vulkan::tool