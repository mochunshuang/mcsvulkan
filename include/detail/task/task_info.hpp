#pragma once

#include "static_string.hpp"

namespace mcs::vulkan::task
{
    struct task_info
    {
        static_string name;
        std::meta::info function{};
    };
}; // namespace mcs::vulkan::task