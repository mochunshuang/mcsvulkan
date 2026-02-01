#pragma once
#include "./event_type.hpp"
#include "./event_dispatcher.hpp"

namespace mcs::vulkan::event
{
    struct scroll_event_dispatcher : event_dispatcher<scroll_event>
    {
    };
}; // namespace mcs::vulkan::event