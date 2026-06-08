#include <exception>
#include <stdexcept>
#include <system_error>
#include <vector>
#include <tuple>
#include <ranges>

#include "unique_spec_name.hpp"
#include "nsdms_of.hpp"
#include "proxy_value.hpp"
#include "size_type.hpp"
#include "name_spec.hpp"

namespace mcs::vulkan::ecs
{
    template <name_spec... registration>
    struct soa_class;

    template <typename>
    struct world;

    template <name_spec... spec>
        requires(unique_spec_name(std::meta::template_arguments_of(^^soa_class<spec...>)))
    struct world<soa_class<spec...>>
    {
        struct soa_member_type;
        consteval
        {
            auto soa_members =
                std::meta::template_arguments_of(^^soa_class<spec...>) |
                std::views::transform([](std::meta::info arg) -> std::meta::info {
                    auto s = std::meta::extract<name_spec>(arg);
                    return std::meta::data_member_spec(s.info, {.name = s.name.view()});
                });
            std::meta::define_aggregate(^^soa_member_type, soa_members);
        }
        soa_member_type soa_sets_{};

        static constexpr auto soa_members = nsdms_of(^^soa_member_type);
        static constexpr auto soa_value_members = [] {
            std::vector<std::meta::info> result;
            template for (constexpr auto m : soa_members)
            {
                using M = [:std::meta::type_of(m):];
                using value_type = M::value_type;
                result.push_back(std::meta::dealias(^^value_type));
            }
            return std::define_static_array(result);
        }();

        struct soa_meta_info
        {
            using registration_type = [:std::meta::substitute(
                                            ^^std::tuple,
                                            soa_members |
                                                std::views::transform(
                                                    [](std::meta::info arg)
                                                        -> std::meta::info {
                                                        return std::meta::type_of(arg);
                                                    })):];
            // 编译期：类型 -> ID
            template <typename T>
            static consteval size_type id_of()
            {
                constexpr auto N = std::tuple_size_v<registration_type>;
                template for (constexpr auto I : std::ranges::views::indices(N))
                {
                    if (std::same_as<T, std::tuple_element_t<I, registration_type>>)
                        return I;
                }
                throw std::meta::exception{"Type not in tuple",
                                           std::meta::current_function()};
            }

            template <static_string name>
            static consteval size_type id_by_name()
            {
                for (size_type i{}; i < soa_members.size(); ++i)
                {
                    if (std::meta::identifier_of(soa_members[i]) == name.view())
                        return i;
                }
                throw std::meta::exception{"Name not in tuple",
                                           std::meta::current_function()};
            }

            // 编译期：ID -> 类型
            template <size_type I>
            using type_at = std::tuple_element_t<I, registration_type>;
        };

        consteval static size_type get_name_idex(std::span<const std::meta::info> input,
                                                 static_string name)
        {
            for (size_type i{}; i < input.size(); ++i)
                if (std::meta::identifier_of(input[i]) == name.view())
                    return i;
            return ~0;
        }

        consteval static size_type find_info_value(std::span<const std::meta::info> input,
                                                   std::meta::info value)
        {
            for (size_type i{}; i < input.size(); ++i)
                if (input[i] == value)
                    return i;
            return ~0;
        }

        template <static_string name>
            requires(get_name_idex(soa_members, name) != ~0)
        constexpr decltype(auto) get_soa(this auto &&self) noexcept
        {
            constexpr auto I = get_name_idex(soa_members, name);
            return std::forward_like<decltype(self)>(self.soa_sets_.[:soa_members[I]:]);
        }
        template <typename T>
            requires(find_info_value(soa_value_members, std::meta::dealias(^^T)) != ~0)
        constexpr decltype(auto) get_soa(this auto &&self) noexcept
        {
            constexpr auto I =
                find_info_value(soa_value_members, std::meta::dealias(^^T));
            return std::forward_like<decltype(self)>(self.soa_sets_.[:soa_members[I]:]);
        }

        template <typename T>
            requires(find_info_value(soa_value_members, std::meta::dealias(^^T)) != ~0)
        constexpr decltype(auto)
        make_soa_value(this auto &&self, auto &&...args) noexcept(
            noexcept(std::forward_like<decltype(self)>(self.template get_soa<T>())
                         .construct_at(0, std::forward<decltype(args)>(args)...)))
        {
            auto &store = std::forward_like<decltype(self)>(self.template get_soa<T>());
            using S = std::remove_reference_t<decltype(store)>;
            using proxy_type = proxy_value<S>;

            auto slot = store.allocate();
            if (slot) [[likely]]
            {
                auto id = *slot;
                store.construct_at(id, std::forward<decltype(args)>(args)...);
                return std::optional<proxy_type>{proxy_type{store, id}};
            }
            else [[unlikely]]
                return std::optional<proxy_type>{std::nullopt};
        }
        template <typename T>
            requires(find_info_value(soa_value_members, std::meta::dealias(^^T)) != ~0)
        constexpr decltype(auto) make_soa_value_throw(this auto &&self, auto &&...args)
        {
            auto &store = std::forward_like<decltype(self)>(self.template get_soa<T>());
            using S = std::remove_reference_t<decltype(store)>;
            using proxy_type = proxy_value<S>;

            auto slot = store.allocate();
            if (slot) [[likely]]
            {
                auto id = *slot;
                store.construct_at(id, std::forward<decltype(args)>(args)...);
                return proxy_type{store, id};
            }
            else [[unlikely]]
                throw std::logic_error{"store.allocate() faild!"};
        }

        static consteval auto unique_soa_component_type()
        {
            std::vector<std::meta::info> result;
            constexpr auto N = soa_members.size();
            template for (constexpr auto I : std::ranges::views::indices(N))
            {
                using T = [:std::meta::type_of(soa_members[I]):];
                using value_type = T::value_type;
                auto components = nsdms_of(^^value_type);
                for (auto c : components)
                {
                    auto type = std::meta::type_of(c);
                    bool find{};
                    for (auto item : result)
                        if (item == type)
                        {
                            find = true;
                            break;
                        }
                    if (not find)
                        result.push_back(type);
                }
            }
            return result;
        }
        struct soa_component_meta_info
        {
            using registration_type = [:std::meta::substitute(
                                            ^^std::tuple, unique_soa_component_type()):];
            // 编译期：类型 -> ID
            template <typename T>
            static consteval size_type id_of()
            {
                constexpr auto N = unique_soa_component_type().size();
                template for (constexpr auto I : std::ranges::views::indices(N))
                {
                    if (std::same_as<T, std::tuple_element_t<I, registration_type>>)
                        return I;
                }
                throw std::meta::exception{"Type not in tuple",
                                           std::meta::current_function()};
            }
            template <static_string name>
            static consteval size_type id_by_name()
            {
                auto components = unique_soa_component_type();
                for (size_type i{}; i < components.size(); ++i)
                {
                    // NOTE: identifier_of 对基本类型失效
                    if (std::meta::display_string_of(components[i]) == name.view())
                        return i;
                }
                throw std::meta::exception{"Name not in tuple",
                                           std::meta::current_function()};
            }

            // 编译期：ID -> 类型
            template <size_type I>
            using type_at = std::tuple_element_t<I, registration_type>;
        };
    };
}; // namespace mcs::vulkan::ecs
