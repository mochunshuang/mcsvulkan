#pragma once
#include "./event_type.hpp"
#include "./event_dispatcher.hpp"

namespace mcs::vulkan::event
{
    struct framebuffer_size_event_dispatcher : event_dispatcher<framebuffer_size_event>
    {
    };
} // namespace mcs::vulkan::event