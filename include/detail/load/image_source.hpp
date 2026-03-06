#pragma once

#include <concepts>

namespace mcs::vulkan::load
{
    template <typename T>
    concept image_source = requires(const T &t) {
        { t.width() } noexcept -> std::convertible_to<int>;
        { t.height() } noexcept -> std::convertible_to<int>;
        { t.channels() } noexcept -> std::convertible_to<int>;
        { t.data() } noexcept -> std::convertible_to<const void *>;
        { t.size() } noexcept -> std::convertible_to<std::size_t>;
    };

}; // namespace mcs::vulkan::load