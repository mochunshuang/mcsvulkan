#pragma once
#include "./event_type.hpp"
#include "./event_dispatcher.hpp"

namespace mcs::vulkan::event
{
    struct drop_event_dispatcher : event_dispatcher<drop_event>
    {
    };
}; // namespace mcs::vulkan::event