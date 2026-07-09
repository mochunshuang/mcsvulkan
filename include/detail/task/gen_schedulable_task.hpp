#pragma once

#include "schedulable_task.hpp"

namespace mcs::vulkan::task
{
    template <typename Gen>
    concept gen_schedulable_task = requires(Gen gen) {
        { gen() } -> std::same_as<schedulable_task>;
    };

}; // namespace mcs::vulkan::task