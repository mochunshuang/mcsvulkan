#pragma once

#include "property_align_content.hpp"
#include "property_align-items-self.hpp"
#include "property_aspect_ratio.hpp"
#include "property_display.hpp"
#include "property_flex_direction.hpp"
#include "property_flex_wrap.hpp"
#include "property_flex-basis-grow-shrink.hpp"
#include "property_gap.hpp"
#include "property_justify_content.hpp"
#include "property_layout_direction.hpp"
#include "property_margin-padding-border.hpp"
#include "property_min-max-width-height.hpp"
#include "property_position.hpp"
#include "property_top-right-bottom-left.hpp"
#include "property_width-height.hpp"

namespace mcs::vulkan::yoga
{
    // https://www.yogalayout.dev/docs/styling/
    class Style
    {
      public:
        // NOLINTBEGIN

        // https://www.yogalayout.dev/docs/styling/align-content
        property_align_content align_content{YGAlign::YGAlignFlexStart};

        // https://www.yogalayout.dev/docs/styling/align-items-self
        property_align_items align_items{YGAlign::YGAlignStretch};
        property_align_self align_self{YGAlign::YGAlignAuto};

        // https://www.yogalayout.dev/docs/styling/aspect-ratio
        property_aspect_ratio aspect_ratio{property::zerofloat};

        // https://www.yogalayout.dev/docs/styling/display
        property_display display{YGDisplay::YGDisplayFlex};

        // https://www.yogalayout.dev/docs/styling/flex-basis-grow-shrink
        property_flex_basis flex_basis{property::Auto{}};
        property_flex_grow flex_grow{property::zerofloat};
        property_flex_shrink flex_shrink{1.0F};

        // https://www.yogalayout.dev/docs/styling/flex-wrap
        property_flex_wrap flex_wrap{YGWrap::YGWrapNoWrap};
        // https://www.yogalayout.dev/docs/styling/flex-direction
        property_flex_direction flex_direction{YGFlexDirection::YGFlexDirectionColumn};

        // https://www.yogalayout.dev/docs/styling/gap
        property_gap gap{};

        // https://www.yogalayout.dev/docs/styling/position
        property_position position{YGPositionType::YGPositionTypeRelative};
        // https://www.yogalayout.dev/docs/styling/insets
        property_top top{property::Auto{}};
        property_right right{property::Auto{}};
        property_bottom bottom{property::Auto{}};
        property_left left{property::Auto{}};

        // https://www.yogalayout.dev/docs/styling/justify-content
        property_justify_content justify_content{YGJustify::YGJustifyFlexStart};

        // https://www.yogalayout.dev/docs/styling/layout-direction
        property_layout_direction direction{YGDirection::YGDirectionLTR};

        // https://www.yogalayout.dev/docs/styling/margin-padding-border
        property_margin margin{property::zeropixels};
        property_padding padding{property::zeropixels};
        property_border border{property::zeropixels};

        // https://www.yogalayout.dev/docs/styling/min-max-width-height
        // By default all these constraints are undefined.
        property_min_width min_width{property::undefined};
        property_max_width max_width{property::undefined};
        property_min_height min_height{property::undefined};
        property_max_height max_height{property::undefined};

        // https://www.yogalayout.dev/docs/styling/width-height
        property_width width{property::Auto{}};
        property_height height{property::Auto{}};

        // NOLINTEND

        void apply(YGNodeRef node) const noexcept
        {
            align_content.apply(node);

            align_items.apply(node);
            align_self.apply(node);

            // Accepts any floating point value > 0, the default is undefined.
            if (*aspect_ratio > property::zerofloat)
                aspect_ratio.apply(node);

            display.apply(node);

            flex_basis.apply(node);
            // Flex grow accepts any floating point value >= 0, with 0 being the default
            // value.
            if (*flex_grow >= property::zerofloat)
                flex_grow.apply(node);
            // Flex shrink accepts any floating point value >= 0, with 1 being the default
            // value.
            if (*flex_shrink >= property::zerofloat)
                flex_shrink.apply(node);

            flex_wrap.apply(node);
            flex_direction.apply(node); // NOTE: 依赖wrap值。调整wrap到前面

            gap.apply(node);

            position.apply(node); // NOTE: 耦合性强需要特别注意。事实上就是忽略
            if (*position != YGPositionType::YGPositionTypeStatic)
            {
                // 在线测试：https://www.yogalayout.dev/docs/styling/insets
                // 处理 top/bottom
                if (!std::holds_alternative<property::Auto>(**top))
                {
                    top.apply(node); // top 非 auto，使用 top，忽略 bottom
                }
                else
                {
                    bottom.apply(node); // top 为 auto，则使用 bottom
                }

                // 处理 left/right
                if (!std::holds_alternative<property::Auto>(**left))
                {
                    left.apply(node); // left 非 auto，使用 left，忽略 right
                }
                else
                {
                    right.apply(node); // left 为 auto，则使用 right
                }
            }

            // NOTE: 和其他属性耦合 ，至少依赖 flex direction
            justify_content.apply(node);

            // NOTE: Layout Direction 决定布局的起始边和结束边,有耦合
            direction.apply(node);

            margin.apply(node);
            padding.apply(node);
            border.apply(node);

            if ((**min_width).index() != 0)
                min_width.apply(node);
            if ((**max_width).index() != 0)
                max_width.apply(node);
            if ((**min_height).index() != 0)
                min_height.apply(node);
            if ((**max_height).index() != 0)
                max_height.apply(node);

            width.apply(node);
            height.apply(node);
        }
    };
}; // namespace mcs::vulkan::yoga