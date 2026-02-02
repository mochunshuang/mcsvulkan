#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include "../PhysicalDevice.hpp"
#include "../utils/make_vk_exception.hpp"

namespace mcs::vulkan::tool
{
    struct queue_family_index_selector
    {
        using check_VkQueueFamilyProperties = std::function<bool(
            const VkQueueFamilyProperties &qfp, uint32_t queueFamilyIndex)>;
        using select_callback =
            std::function<uint32_t(const std::vector<uint32_t> &candidate)>;

        constexpr explicit queue_family_index_selector(PhysicalDevice &physicalDevice)
            : queueFamilies_{physicalDevice.getQueueFamilyProperties()}
        {
        }

        constexpr auto &requiredQueueFamily(
            check_VkQueueFamilyProperties checkQueueFamilyFun) noexcept
        {
            checkQueueFamily_ = std::move(checkQueueFamilyFun);
            return *this;
        }

        [[nodiscard]] constexpr auto select()
        {
            std::vector<uint32_t> candidate_;
            if (checkQueueFamily_.has_value())
            {
                for (uint32_t i = 0, queueFamilyCount = queueFamilies_.size();
                     i < queueFamilyCount; i++)
                {
                    if ((*checkQueueFamily_)(queueFamilies_[i], i))
                    {
                        candidate_.emplace_back(i);
                    }
                }
            }
            if (candidate_.empty())
                throw make_vk_exception("failed to find a suitable family index.");
            return candidate_;
        }

      private:
        std::optional<check_VkQueueFamilyProperties> checkQueueFamily_;
        std::vector<VkQueueFamilyProperties> queueFamilies_;
    };

}; // namespace mcs::vulkan::tool
