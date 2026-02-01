#pragma once
#include "./event_type.hpp"
#include "./event_dispatcher.hpp"

namespace mcs::vulkan::event
{
    struct keyboard_event_dispatcher : event_dispatcher<keyboard_event>
    {
    };
}; // namespace mcs::vulkan::event