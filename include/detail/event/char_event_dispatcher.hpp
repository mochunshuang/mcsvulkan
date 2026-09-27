#pragma once
#include "./event_type.hpp"
#include "./event_dispatcher.hpp"

namespace mcs::vulkan::event
{
    struct char_event_dispatcher : event_dispatcher<char_event>
    {
    };
}; // namespace mcs::vulkan::event