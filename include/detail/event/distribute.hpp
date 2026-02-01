#pragma once

#include "./distributable.hpp"

namespace mcs::vulkan::event
{
    template <distributable Dispatcher>
    static constexpr void distribute(typename Dispatcher::event_type &&event) noexcept
    {
        Dispatcher::instance().distribute(std::move(event));
    }
}; // namespace mcs::vulkan::event