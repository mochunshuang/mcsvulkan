#pragma once

#include "static_string.hpp"
#include "size_type.hpp"

template <static_string... tag>
struct runtime_info;

template <typename runtime_info, typename... Ts>
struct runtime_type;

template <static_string... Name, typename... Ts>
    requires(sizeof...(Name) - sizeof...(Ts) == 1)
struct runtime_type<runtime_info<Name...>, Ts...>
{
    static constexpr auto class_name = Name...[0]; // NOLINT
    struct type;
    consteval
    {
        constexpr auto N = sizeof...(Ts);
        std::vector<std::meta::info> desc_{};
        std::array<std::meta::info, N> types_{^^Ts...};
        template for (constexpr auto I : std::ranges::views::indices(N))
        {
            auto name = Name...[I + 1];
            desc_.push_back(
                std::meta::data_member_spec(types_[I], {.name = name.view()}));
        }
        std::meta::define_aggregate(^^type, desc_);
    }
    type data_;
    static constexpr auto members = nsdms_of(^^type);

    template <typename... T>
    runtime_type(T &&...m) : data_{std::forward<T>(m)...} // NOLINT
    {
    }
    decltype(auto) operator*(this auto &&self) noexcept
    {
        auto &&[... proxies] = self.data_;
        struct entity_view;
        consteval
        {
            constexpr auto N = sizeof...(proxies);
            std::vector<std::meta::info> specs;
            template for (constexpr auto I : std::ranges::views::indices(N))
            {
                using MemberType =
                    decltype(*std::forward_like<decltype(self)>(proxies...[I]));
                specs.push_back(std::meta::data_member_spec( // clang-format off
                    ^^MemberType, {.name = Name...[I + 1].view()})); // clang-format on
            }
            std::meta::define_aggregate(^^entity_view, specs);
        }
        assert(proxies && ...);
        return entity_view{*std::forward_like<decltype(self)>(proxies)...};
    }
    constexpr auto operator->() noexcept
    {
        return &data_;
    }
    constexpr auto operator->() const noexcept
    {
        return &data_;
    }
    static consteval size_type find_name(static_string name)
    {
        template for (constexpr auto I : std::ranges::views::indices(sizeof...(Ts)))
        {
            if (Name...[I + 1] == name)
                return I;
        }
        return ~0;
    }
    template <static_string name>
        requires(find_name(name) != ~0)
    decltype(auto) get(this auto &&self) noexcept
    {
        constexpr auto I = find_name(name);
        return std::forward_like<decltype(self)>(self.data_.[:members[I]:]);
    }
    template <size_type I>
        requires(I < sizeof...(Ts))
    decltype(auto) get(this auto &&self) noexcept
    {
        return std::forward_like<decltype(self)>(self.data_.[:members[I]:]);
    }
};
