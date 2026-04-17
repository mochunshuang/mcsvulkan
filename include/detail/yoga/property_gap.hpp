#pragma once

#include "vaulue_type.hpp"

namespace mcs::vulkan::yoga
{

    // https://www.yogalayout.dev/docs/styling/gap
    struct property_gap
    {
        using value_type = property::value_property_noauto;
        value_type row{};    // NOLINT
        value_type column{}; // NOLINT

        property_gap() = default;
        // 单个值简化
        template <typename T>
            requires(requires(T v) { value_type{v}; })
        property_gap(T value) noexcept // NOLINT
            : row{value}, column{value}
        {
        }
        // 明确两个值
        template <typename T>
            requires(requires(T v) { value_type{v}; })
        property_gap(T row, T column) noexcept // NOLINT
            : row{row}, column{column}
        {
        }
        void apply(YGNodeRef node) const noexcept
        {
            // NOLINTNEXTLINE
            constexpr auto vister = [](YGNodeRef node, YGGutter edge,
                                       value_type value) noexcept {
                std::visit(
                    [=](auto &&arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::monostate>)
                            ;
                        else if constexpr (std::is_same_v<T, property::Pixels>)
                            YGNodeStyleSetGap(node, edge, arg.points);
                        else if constexpr (std::is_same_v<T, property::Percentage>)
                            YGNodeStyleSetGapPercent(node, edge, arg.percent);
                        else
                            static_assert(false, "non-exhaustive visitor!");
                    },
                    *value);
            };
            vister(node, YGGutterRow, row);
            vister(node, YGGutterColumn, column);
        }
    };
}; // namespace mcs::vulkan::yoga