#pragma once

#include <meta>

namespace mcs::vulkan::meta
{
    static consteval auto static_nsdms_of(std::meta::info info)
    {
        return std::define_static_array(std::meta::nonstatic_data_members_of(
            info, std::meta::access_context::current()));
    }
}; // namespace mcs::vulkan::meta
