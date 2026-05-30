#pragma once

#include <meta>

namespace mcs::vulkan::meta
{
    template <auto is_this_info>
    struct is_info
    {
        consteval bool operator()(std::meta::info info) const
        {
            try
            {
                return is_this_info(info);
            }
            catch (std::meta::exception)
            {
                return false;
            }
        }
    };

}; // namespace mcs::vulkan::meta