#pragma once

#include <meta>
#include <optional>
#include <concepts>

namespace mcs::vulkan::meta
{
    struct class_utils
    {
        static consteval std::optional<std::meta::info> find_member(
            std::meta::info class_ref, static_string name, auto pred,
            std::meta::access_context ctx = std::meta::access_context::current())
            requires(requires() {
                { pred(std::meta::info{}) } -> std::same_as<bool>;
            })
        {
            for (auto m : std::meta::members_of(class_ref, ctx))
                if (std::meta::has_identifier(m) && std::meta::identifier_of(m) == name &&
                    pred(m))
                    return std::optional<std::meta::info>{m};
            return std::optional<std::meta::info>{std::nullopt};
        }
        static consteval auto find_member(
            std::meta::info class_ref, static_string name,
            std::meta::access_context ctx = std::meta::access_context::current())
        {
            return find_member(class_ref, name, [](auto) { return true; }, ctx);
        }
    };
}; // namespace mcs::vulkan::meta