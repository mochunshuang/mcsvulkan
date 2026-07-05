#pragma once

#include "overloads.hpp"
#include <variant>

namespace mcs::vulkan
{
    template <class... Ts>
    constexpr static auto match(const std::variant<Ts...> &varint, auto &&...fn) noexcept(
        noexcept(std::visit(overloads{std::forward<decltype(fn)>(fn)...}, varint)))
        requires(requires() {
            std::visit(overloads{std::forward<decltype(fn)>(fn)...}, varint);
        })
    {
        return std::visit(overloads{std::forward<decltype(fn)>(fn)...}, varint);
    }
}; // namespace mcs::vulkan