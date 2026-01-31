#pragma once

#include "../__vulkan.hpp"

#include "../to_string/VkResult.hpp"
#include "make_vk_exception.hpp"

namespace mcs::vulkan
{
    static constexpr void check_vkresult(
        VkResult code, const std::string &message = "check_vkresult",
        const std::source_location &location = std::source_location::current())
    {
        using to_string::to_string;
        if (code != VK_SUCCESS)
            throw make_vk_exception(
                std::format("[tag: {}][code: {}]", message, to_string<VkResult>(code)),
                location);
    }

}; // namespace mcs::vulkan