#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "../PhysicalDevice.hpp"
#include "../utils/make_vk_exception.hpp"

namespace mcs::vulkan::tool
{
    struct create_queue_family_index_selector
    {
        using check_VkQueueFamilyProperties = std::function<bool(
            const VkQueueFamilyProperties &qfp, uint32_t queueFamilyIndex)>;

        constexpr auto &requiredQueueFamily(
            check_VkQueueFamilyProperties checkQueueFamilyFun) noexcept
        {
            checkQueueFamily_ = std::move(checkQueueFamilyFun);
            return *this;
        }

        template <std::ranges::random_access_range R>
            requires std::same_as<std::ranges::range_value_t<R>, VkQueueFamilyProperties>
        [[nodiscard]] constexpr auto select(const R &queueFamilies)
        {
            std::vector<uint32_t> candidate;
            if (checkQueueFamily_.has_value())
            {
                for (uint32_t i = 0, queueFamilyCount = queueFamilies.size();
                     i < queueFamilyCount; i++)
                {
                    if ((*checkQueueFamily_)(queueFamilies[i], i))
                    {
                        candidate.emplace_back(i);
                    }
                }
            }
            if (candidate.empty())
                throw make_vk_exception("failed to find a suitable family index.");
            return candidate;
        }
        [[nodiscard]] constexpr auto select(const PhysicalDevice &physicalDevice)
        {
            return select(physicalDevice.getQueueFamilyProperties());
        }

      private:
        std::optional<check_VkQueueFamilyProperties> checkQueueFamily_;
    };

}; // namespace mcs::vulkan::tool
