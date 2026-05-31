#pragma once

#include <meta>

namespace mcs::vulkan::meta
{
    static consteval auto static_parameters_of(std::meta::info info)
    {
        return std::define_static_array(std::meta::parameters_of(info));
    }
}; // namespace mcs::vulkan::meta
