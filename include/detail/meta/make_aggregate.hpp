#pragma once

#include "static_string.hpp"
#include "static_nsdms_of.hpp"
#include "static_parameters_of.hpp"
#include <ranges>

namespace mcs::vulkan::meta
{
    template <static_string... tag>
    struct aggregate_info;

    struct bind_member : static_string
    {
    };

    namespace detail
    {
        template <typename runtime_info, typename... Ts>
        struct gen_aggregate;

        template <static_string... Name, typename... Ts>
            requires(sizeof...(Name) - sizeof...(Ts) == 1)
        struct gen_aggregate<aggregate_info<Name...>, Ts...>
        {
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

            static consteval int find_name(auto name) // NOLINT
            {
                template for (constexpr auto I :
                              std::ranges::views::indices(sizeof...(Ts)))
                {
                    if (Name...[I + 1] == name)
                        return I;
                }
                return -1;
            }

            static consteval auto get_bind_index(const auto &fn)
            {
                using Fn = std::remove_cvref_t<decltype(fn)>; // NOLINTNEXTLINE
                static constexpr auto fn_args = static_parameters_of(^^Fn::operator());
                constexpr auto binds = [] { // NOLINT
                    std::vector<int> binds;
                    template for (constexpr auto arg : fn_args)
                    {
                        if constexpr (std::meta::annotations_of_with_type(arg,
                                                                          ^^bind_member)
                                          .size() > 0)
                        {
                            constexpr auto bind_name = std::meta::extract<bind_member>(
                                std::meta::annotations_of_with_type(arg,
                                                                    ^^bind_member)[0]);
                            constexpr auto I = find_name(bind_name);
                            if (I == -1)
                                return std::define_static_array(binds);
                            binds.push_back(I);
                        }
                    }
                    return std::define_static_array(binds);
                }();
                return binds;
            }
        };
    }; // namespace detail

    template <typename runtime_info, typename... Ts>
    struct aggregate;

    template <static_string... Name, typename... Ts>
        requires(sizeof...(Name) - sizeof...(Ts) == 1)
    struct aggregate<aggregate_info<Name...>, Ts...>
        : detail::gen_aggregate<aggregate_info<Name...>, Ts...>::type
    {
        using gen_type = detail::gen_aggregate<aggregate_info<Name...>, Ts...>;
        using base_type = gen_type::type;
        static constexpr auto class_name = Name...[0];                // NOLINT
        static constexpr auto members = static_nsdms_of(^^base_type); // NOLINT

        template <static_string m_fn>
            requires(gen_type::find_name(m_fn) != -1)
        decltype(auto) invoke(this auto &&self, auto... args)
        {
            constexpr auto I = gen_type::find_name(m_fn);
            return self.[:members[I]:](std::forward<decltype(self)>(self),
                                       std::forward<decltype(args)>(args)...);
        }
        template <static_string m_fn>
            requires(gen_type::find_name(m_fn) != -1)
        decltype(auto) invoke_static(this auto &&self, auto... args) // NOLINT
        {
            constexpr auto I = gen_type::find_name(m_fn);
            return self.[:members[I]:](std::forward<decltype(args)>(args)...);
        }

        decltype(auto) constexpr with(this auto &&self, auto &&fn, auto &&...args)
            requires(not std::is_pointer_v<std::decay_t<decltype(fn)>>)
        {
            constexpr auto binds = gen_type::get_bind_index(fn); // NOLINT
            return [&]<std::size_t... Is>(
                       std::index_sequence<Is...>) constexpr -> decltype(auto) {
                return std::forward<decltype(fn)>(fn)(
                    std::forward_like<decltype(self)>(self.[:members[binds[Is]]:])...,
                    std::forward<decltype(args)>(args)...);
            }(std::make_index_sequence<binds.size()>());
        }
    };

    template <static_string... name, typename... T>
    static constexpr auto make_aggregate(T &&...member)
        -> aggregate<aggregate_info<name...>, std::decay_t<T>...>
    {
        return {std::forward<T>(member)...};
    }

    template <static_string... name, typename... T>
    static constexpr auto make_aggregate_ref(T &...member)
        -> aggregate<aggregate_info<name...>, T &...>
    {
        return {std::forward<T &>(member)...};
    }

}; // namespace mcs::vulkan::meta