#pragma once

#include <concepts>

namespace mcs::vulkan::event
{
    template <typename T>
    concept distributable = requires(T &dispatcher, typename T::event_type event) {
        { dispatcher.distribute(event) } noexcept -> std::same_as<void>;
    };
}; // namespace mcs::vulkan::event