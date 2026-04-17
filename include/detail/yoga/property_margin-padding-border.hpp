#pragma once

#include "vaulue_type.hpp"

namespace mcs::vulkan::yoga
{
    // https://www.yogalayout.dev/docs/styling/margin-padding-border
    struct property_margin
        : property::property_box<
              property::value_property,
              [](YGNodeRef node, YGEdge edge, property::value_property value) noexcept {
                  std::visit(
                      [=](auto &&arg) {
                          using T = std::decay_t<decltype(arg)>;
                          if constexpr (std::is_same_v<T, property::Auto>)
                              YGNodeStyleSetMarginAuto(node, edge);
                          else if constexpr (std::is_same_v<T, property::Pixels>)
                              YGNodeStyleSetMargin(node, edge, arg.points);
                          else if constexpr (std::is_same_v<T, property::Percentage>)
                              YGNodeStyleSetMarginPercent(node, edge, arg.percent);
                          else
                              static_assert(false, "non-exhaustive visitor!");
                      },
                      *value);
              }>
    {
    };
    struct property_padding
        : property::property_box<
              property::value_property_noauto,
              [](YGNodeRef node, YGEdge edge,
                 property::value_property_noauto value) noexcept {
                  std::visit(
                      [=](auto &&arg) {
                          using T = std::decay_t<decltype(arg)>;
                          if constexpr (std::is_same_v<T, std::monostate>)
                              ;
                          else if constexpr (std::is_same_v<T, property::Pixels>)
                              YGNodeStyleSetPadding(node, edge, arg.points);
                          else if constexpr (std::is_same_v<T, property::Percentage>)
                              YGNodeStyleSetPaddingPercent(node, edge, arg.percent);
                          else
                              static_assert(false, "non-exhaustive visitor!");
                      },
                      *value);
              }>
    {
    };
    struct property_border
        : property::property_box<property::Pixels, [](YGNodeRef node, YGEdge edge,
                                                      property::Pixels value) noexcept {
            YGNodeStyleSetBorder(node, edge, value.points);
        }>
    {
    };
}; // namespace mcs::vulkan::yoga