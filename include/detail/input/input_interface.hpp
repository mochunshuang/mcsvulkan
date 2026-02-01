#pragma once

#include "../event/event_type.hpp"

namespace mcs::vulkan::input
{
    struct input_interface
    {
        using keyboard_event = event::keyboard_event;
        using scroll_event = event::scroll_event;
        using mousebutton_event = event::mousebutton_event;
        using position2d_event = event::position2d_event;
        using cursor_enter_event = event::cursor_enter_event;
    };
}; // namespace mcs::vulkan::input