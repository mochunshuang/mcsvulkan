#pragma once

#include "static_string.hpp"

namespace mcs::vulkan::task
{
    template <static_string beign, static_string end>
    static constexpr void invoke_aggregate_ranges(auto &object, auto &&...args)
    {
        using T = std::remove_cvref_t<decltype(object)>;
        constexpr auto members = [] { // NOLINT
            if constexpr (requires() { T::members; })
                return T::members;
            else
                return std::define_static_array(std::meta::nonstatic_data_members_of(
                    ^^T, std::meta::access_context::current()));
        }();
        constexpr auto find_index = [](static_string name, // NOLINT
                                       const auto &members) consteval {
            for (auto I : std::views::indices(members.size()))
                if (std::meta::identifier_of(members[I]) == name)
                    return I;
            auto msg = " field [" + std::string(name.view()) + "] is not find in members";
            throw std::meta::exception{msg, std::meta::current_function()};
        };

        constexpr auto beign_index = find_index(beign, members); // NOLINT
        constexpr auto end_index = find_index(end, members);     // NOLINT
        static_assert(beign_index <= end_index, "requires beign_index <= end_index");
        template for (constexpr auto I :
                      std::ranges::views::iota(beign_index, end_index + 1))
        {
            object.[:members[I]:](std::forward<decltype(args)>(args)...);
        }
    };
}; // namespace mcs::vulkan::task