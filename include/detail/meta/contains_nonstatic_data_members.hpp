#pragma once

#include "static_string.hpp"
#include "identifier_view.hpp"

namespace mcs::vulkan::meta
{
    consteval bool contains_nonstatic_data_members(
        std::meta::info info, const std::span<const static_string> &members)
    {
        try
        {
            for (std::vector<std::meta::info> mbs = std::meta::nonstatic_data_members_of(
                     info, std::meta::access_context::current());
                 const auto &target : members)
            {
                bool found = false;
                for (auto mb : mbs)
                {
                    if (identifier_view(mb) == target)
                    {
                        found = true;
                        break;
                    }
                }
                if (not found)
                    return false;
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    consteval bool contains_nonstatic_data_members(
        std::meta::info info, std::initializer_list<static_string> members)
    {
        return contains_nonstatic_data_members(info,
                                               std::span{members.begin(), members.end()});
    }

}; // namespace mcs::vulkan::meta