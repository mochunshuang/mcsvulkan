#pragma once

#include <meta>
#include "info_array.hpp"
#include <span>
#include <vector>

namespace mcs::vulkan::meta
{
    namespace detail
    {
        consteval bool are_all_data_member_specs(std::span<const std::meta::info> specs)
        {
            for (auto s : specs)
                if (!std::meta::is_data_member_spec(s))
                    return false;
            return true;
        }

        consteval bool all_have_identifiers(std::span<const std::meta::info> specs)
        {
            for (auto s : specs)
                if (!std::meta::has_identifier(s))
                    return false;
            return true;
        }

        consteval bool has_unique_member_names(std::span<const std::meta::info> specs)
        {
            std::vector<std::string_view> seen;
            for (auto s : specs)
            {
                std::string_view name = std::meta::identifier_of(s);
                for (auto prev : seen)
                    if (prev == name)
                        return false;
                seen.push_back(name);
            }
            return true;
        }

        consteval bool has_valid_unique_member_specs(
            std::span<const std::meta::info> specs)
        {
            return are_all_data_member_specs(specs) && all_have_identifiers(specs) &&
                   has_unique_member_names(specs);
        }

    }; // namespace detail

    template <info_array... V>
    consteval auto concatenate_member_specs(V... v)
    {
        constexpr std::size_t N = (std::tuple_size_v<V> + ...);
        std::array<std::meta::info, N> data{};
        std::size_t idx = 0;
        auto append = [&](const auto &arr) {
            for (auto e : arr)
                data[idx++] = e;
        };
        (append(v), ...);

        if (!detail::has_valid_unique_member_specs(data))
            throw std::meta::exception(u8"concatenate_member_specs(): duplicate or "
                                       u8"invalid data member names",
                                       ^^concatenate_member_specs);
        return data;
    }

    template <auto member_specs>
    struct aggregate_impl
    {
        struct type;
        consteval
        {
            std::meta::define_aggregate(^^type, member_specs);
        }
    };

    template <auto... Parts>
        requires(info_array<decltype(Parts)> && ...)
    using aggregate_type = aggregate_impl<concatenate_member_specs(Parts...)>::type;

}; // namespace mcs::vulkan::meta