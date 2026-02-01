#pragma once
#include "./event_type.hpp"
#include "./event_dispatcher.hpp"

namespace mcs::vulkan::event
{
    struct cursor_pos_event_dispatcher : event_dispatcher<position2d_event>
    {
    };
}; // namespace mcs::vulkan::event