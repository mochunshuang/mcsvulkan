#pragma once

#include "vaulue_type.hpp"

namespace mcs::vulkan::yoga
{
    // https://www.yogalayout.dev/docs/styling/insets
    template <YGEdge edge>
    struct insets
    {
        using value_type = property::value_property;
        value_type value{}; // NOLINT

        constexpr auto &operator*() noexcept
        {
            return value;
        }
        constexpr const auto &operator*() const noexcept
        {
            return value;
        }

        insets() = default;
        template <typename T>
            requires(requires(T v) { value_type{v}; })
        insets(T value) noexcept // NOLINT
            : value{value}
        {
        }
        template <typename T>
            requires(requires(T v) { value_type{v}; })
        constexpr auto &operator=(T value) noexcept
        {
            this->value = value;
            return *this;
        }

        void apply(YGNodeRef node) const noexcept
        {
            std::visit(
                [=](auto &&arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, property::Auto>)
                        YGNodeStyleSetPositionAuto(node, edge);
                    else if constexpr (std::is_same_v<T, property::Pixels>)
                        YGNodeStyleSetPosition(node, edge, arg.points);
                    else if constexpr (std::is_same_v<T, property::Percentage>)
                        YGNodeStyleSetPositionPercent(node, edge, arg.percent);
                    else
                        static_assert(false, "non-exhaustive visitor!");
                },
                *value);
        }
    };
    struct property_top : insets<YGEdge::YGEdgeTop>
    {
        using insets::operator=;
    };
    struct property_right : insets<YGEdge::YGEdgeRight>
    {
        using insets::operator=;
    };
    struct property_bottom : insets<YGEdge::YGEdgeBottom>
    {
        using insets::operator=;
    };
    struct property_left : insets<YGEdge::YGEdgeLeft>
    {
        using insets::operator=;
    };
}; // namespace mcs::vulkan::yoga
