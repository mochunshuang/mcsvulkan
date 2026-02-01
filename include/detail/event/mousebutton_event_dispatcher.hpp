#pragma once
#include "./event_type.hpp"
#include "./event_dispatcher.hpp"

namespace mcs::vulkan::event
{
    struct mousebutton_event_dispatcher : event_dispatcher<mousebutton_event>
    {
    };
}; // namespace mcs::vulkan::event