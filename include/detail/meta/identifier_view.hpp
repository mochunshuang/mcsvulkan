#pragma once

#include <meta>
#include <string_view>

namespace mcs::vulkan::meta
{
    consteval std::string_view identifier_view(std::meta::info info)
    {
        if (not std::meta::has_identifier(info))
            throw std::meta::exception(u8"entity has no identifier(unnamed)",
                                       std::meta::current_function());
        return std::meta::identifier_of(info);
    }
}; // namespace mcs::vulkan::meta