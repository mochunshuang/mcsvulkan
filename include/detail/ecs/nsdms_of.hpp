#pragma once

#include "../meta/nsdms_of.hpp"

namespace mcs::vulkan::ecs
{
    static consteval auto nsdms_of(std::meta::info info)
    {
        return std::define_static_array(meta::nsdms_of(info));
    }
} // namespace mcs::vulkan::ecs
