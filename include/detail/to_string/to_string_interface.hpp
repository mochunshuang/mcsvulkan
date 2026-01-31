#pragma once

#include <string_view>

#include "../__vulkan.hpp"

#define STR(r) \
    case r:    \
        return #r;
#define ERROR_MAP(r) "error_vk_strmp_" #r;

namespace mcs::vulkan::to_string
{
    template <typename T>
    static constexpr std::string_view to_string(T v) noexcept;

    using VkResult [[maybe_unused]] = ::VkResult;

}; // namespace mcs::vulkan::to_string