#pragma once
#include "./event_type.hpp"
#include "./event_dispatcher.hpp"

namespace mcs::vulkan::event
{
    struct window_focus_event_dispatcher : event_dispatcher<window_focus_event>
    {
    };
} // namespace mcs::vulkan::event