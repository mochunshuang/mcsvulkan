#pragma once

#include "sType.hpp"
#include "pNext.hpp"
#include "Flags.hpp"
#include "../LogicalDevice.hpp"

#include <concepts>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace mcs::vulkan::tool
{
    struct create_logical_device
    {
        /*
        typedef struct VkDeviceQueueCreateInfo {
            VkStructureType             sType;
            const void*                 pNext;
            VkDeviceQueueCreateFlags    flags;
            uint32_t                    queueFamilyIndex;
            uint32_t                    queueCount;
            const float*                pQueuePriorities;
        } VkDeviceQueueCreateInfo;
        */
        struct queue_create_info // NOLINTBEGIN
        {
            constexpr VkDeviceQueueCreateInfo operator()() const noexcept
            {
                return {.sType = sType<VkDeviceQueueCreateInfo>(),
                        .pNext = pNext.value(),
                        .flags = flags,
                        .queueFamilyIndex = queueFamilyIndex,
                        .queueCount = queueCount,
                        .pQueuePriorities = &queuePrioritie};
            }
            pNext pNext;
            Flags<VkDeviceQueueCreateFlagBits> flags;
            uint32_t queueFamilyIndex{};
            uint32_t queueCount{};
            float queuePrioritie{};
        }; // NOLINTEND
        /*
        typedef struct VkDeviceCreateInfo {
            VkStructureType                    sType;
            const void*                        pNext;
            VkDeviceCreateFlags                flags;
            uint32_t                           queueCreateInfoCount;
            const VkDeviceQueueCreateInfo*     pQueueCreateInfos;
            // enabledLayerCount is deprecated and should not be used
            uint32_t                           enabledLayerCount;
            // ppEnabledLayerNames is deprecated and should not be used
            const char* const*                 ppEnabledLayerNames;
            uint32_t                           enabledExtensionCount;
            const char* const*                 ppEnabledExtensionNames;
            const VkPhysicalDeviceFeatures*    pEnabledFeatures;
        } VkDeviceCreateInfo;
        */
        struct create_info // NOLINTBEGIN
        {
            struct result_type
            {
                constexpr explicit result_type(const create_info &info) noexcept
                    : queueInfo_{info.queueCreateInfos |
                                 std::views::transform([](const queue_create_info &
                                                              create) constexpr noexcept {
                                     return create();
                                 }) |
                                 std::ranges::to<std::vector<VkDeviceQueueCreateInfo>>()},
                      createInfo_{
                          .sType = sType<VkDeviceCreateInfo>(),
                          .pNext = info.pNext.value(),
                          .flags = info.flags,
                          .queueCreateInfoCount =
                              static_cast<uint32_t>(queueInfo_.size()),
                          .pQueueCreateInfos =
                              queueInfo_.empty() ? nullptr : queueInfo_.data(),
                          .enabledExtensionCount =
                              static_cast<uint32_t>(info.enabledExtensions.size()),
                          .ppEnabledExtensionNames = info.enabledExtensions.empty()
                                                         ? nullptr
                                                         : info.enabledExtensions.data(),
                          .pEnabledFeatures = info.enabledFeatures2
                                                  ? &info.enabledFeatures2->features
                                                  : nullptr}
                {
                }

                [[nodiscard]] constexpr auto &createInfo() const & noexcept
                {
                    return createInfo_;
                }

              private:
                std::vector<VkDeviceQueueCreateInfo> queueInfo_;
                VkDeviceCreateInfo createInfo_;
            };
            constexpr auto operator()() const noexcept
            {
                return result_type{*this};
            }
            pNext pNext;
            VkDeviceCreateFlags flags{};
            std::vector<queue_create_info> queueCreateInfos;
            std::span<const char *const> enabledExtensions;
            // NOTE: 这里不一样。不过不影响
            const VkPhysicalDeviceFeatures2 *enabledFeatures2{};
        }; // NOLINTEND

        [[nodiscard]] constexpr LogicalDevice build(const PhysicalDevice &physicalDevice)
        {
            auto ret = createInfo_();
            return LogicalDevice{physicalDevice,
                                 physicalDevice.createDevice(&ret.createInfo(),
                                                             physicalDevice.allocator())};
        }
        constexpr auto &setCreateInfo(create_info &&createInfo) noexcept
        {
            createInfo_ = std::move(createInfo);
            return *this;
        }

        template <typename... Args>
        static constexpr auto makeQueueCreateInfos(Args &&...args)
        {
            std::vector<queue_create_info> result;
            result.reserve(sizeof...(args));
            (result.emplace_back(std::forward<Args>(args)), ...);
            return result;
        }

      private:
        create_info createInfo_;
    };

}; // namespace mcs::vulkan::tool