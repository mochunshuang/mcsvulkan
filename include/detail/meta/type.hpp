#pragma once

#include <meta>

namespace mcs::vulkan::meta
{
    consteval auto type(std::meta::info info)
    {
        return std::meta::is_type(info) ? info : std::meta::type_of(info);
    }

}; // namespace mcs::vulkan::meta