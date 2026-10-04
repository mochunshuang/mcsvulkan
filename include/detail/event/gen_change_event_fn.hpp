#pragma once
#include "event_type.hpp"
#include <chrono>

namespace mcs::vulkan::event
{
    template <typename Event>
    using gen_change_event_fn =
        void (*)(void * /*data*/, position2d_event /*pos*/,
                 std::chrono::steady_clock::time_point /*time_point*/, Event) noexcept;

}; // namespace mcs::vulkan::event