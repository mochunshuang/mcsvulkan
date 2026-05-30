#pragma once

#include "static_string.hpp"

namespace mcs::vulkan::meta
{
    struct name_spec // NOLINT
    {
        static_string name;
        std::meta::info info;
    };
}; // namespace mcs::vulkan::meta