#pragma once

#include "task_info.hpp"
#include <vector>
#include <optional>

namespace mcs::vulkan::task
{
    struct schedulable_task
    {
        struct fixed_position
        {
            static_string anchor;
            int position;
        };

        task_info task;
        std::vector<static_string> befores;
        std::vector<static_string> afters;
        std::optional<fixed_position> fixed;
    };

}; // namespace mcs::vulkan::task