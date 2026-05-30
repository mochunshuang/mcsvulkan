#pragma once

#include <meta>

namespace mcs::vulkan::meta
{
    static consteval auto nsdms_of(std::meta::info info)
    {
        return std::meta::nonstatic_data_members_of(info,
                                                    std::meta::access_context::current());
    }
}; // namespace mcs::vulkan::meta