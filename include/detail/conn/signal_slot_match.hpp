#pragma once

#include "traits_slot.hpp"
#include "valid_signal.hpp"

namespace mcs::vulkan::conn
{
    template <typename signal_type, typename slot_type>
    concept signal_slot_match =
        std::same_as<typename detail::valid_signal_impl<signal_type>::args_tuple,
                     typename traits_slot<slot_type>::args_tuple>;

}; // namespace mcs::vulkan::conn