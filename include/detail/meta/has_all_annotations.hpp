#pragma once

#include "is_info.hpp"
#include_next <ranges>

namespace mcs::vulkan::meta
{

    template <typename... A>
    consteval bool has_all_annotations(const std::vector<std::meta::info> &infos,
                                       A... annotations)
    {
        constexpr auto N = sizeof...(A);
        template for (constexpr auto I :
                      std::define_static_array(std::ranges::views::iota(size_t{0}, N)))
        {
            bool found = false;
            for (auto j : infos)
            {
                try
                {
                    if (std::meta::extract<A...[I]>(j) == annotations...[I])
                    {
                        found = true;
                        break;
                    }
                }
                catch (...)
                {
                }
            }
            if (not found)
                return false;
        }

        return true;
    }

}; // namespace mcs::vulkan::meta