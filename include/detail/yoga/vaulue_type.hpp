#pragma once

#include "../__yoga_import.hpp"
#include <variant>

namespace mcs::vulkan::yoga
{
    namespace property
    {
        struct Auto // NOLINT
        {
        };
        struct Pixels // NOLINT
        {
            float points{};
        };
        struct Percentage // NOLINT
        {
            float percent{};
        };

        constexpr auto zerofloat = 0.0F;               // NOLINT
        constexpr auto zeropixels = Pixels{zerofloat}; // NOLINT
        constexpr auto undefined = std::monostate{};   // NOLINT

        struct value_property
        {
            using value_type = std::variant<Auto, Pixels, Percentage>;
            value_type value_{}; // NOLINT

            // NOLINTBEGIN // 隐式转换构造函数（不加 explicit）
            value_property() = default;
            constexpr value_property(Auto v) noexcept : value_(v) {}
            constexpr value_property(Pixels v) noexcept : value_(v) {}
            constexpr value_property(Percentage v) noexcept : value_(v) {}
            constexpr value_property(value_type v) noexcept : value_(v) {}
            // NOLINTEND

            constexpr auto &operator*() noexcept
            {
                return value_;
            }
            constexpr const auto &operator*() const noexcept
            {
                return value_;
            }
            constexpr auto &operator=(Auto value) noexcept
            {
                value_ = value;
                return *this;
            }
            constexpr auto &operator=(Pixels value) noexcept
            {
                value_ = value;
                return *this;
            }
            constexpr auto &operator=(Percentage value) noexcept
            {
                value_ = value;
                return *this;
            }
        };

        struct value_property_noauto
        {
            using value_type = std::variant<std::monostate, Pixels, Percentage>;
            value_type value_{}; // NOLINT

            // NOLINTBEGIN // 隐式转换构造函数（不加 explicit）
            value_property_noauto() = default;
            constexpr value_property_noauto(Pixels v) noexcept : value_(v) {}
            constexpr value_property_noauto(Percentage v) noexcept : value_(v) {}
            constexpr value_property_noauto(value_type v) noexcept : value_(v) {}
            // NOLINTEND

            constexpr auto &operator*() noexcept
            {
                return value_;
            }
            constexpr const auto &operator*() const noexcept
            {
                return value_;
            }
            constexpr auto &operator=(std::monostate value) noexcept
            {
                value_ = value;
                return *this;
            }
            constexpr auto &operator=(Pixels value) noexcept
            {
                value_ = value;
                return *this;
            }
            constexpr auto &operator=(Percentage value) noexcept
            {
                value_ = value;
                return *this;
            }
        };

        template <typename value_type, auto applyImpl>
        struct property_box
        {
            value_type top{};    // NOLINT
            value_type right{};  // NOLINT
            value_type bottom{}; // NOLINT
            value_type left{};   // NOLINT

            // 1 个值：四边相同
            template <typename T>
                requires(requires(T v) { value_type{v}; })
            property_box(T value) noexcept // NOLINT
                : top{value}, right{value}, bottom{value}, left{value}
            {
            }
            // 2 个值：上下 | 左右
            template <typename T>
                requires(requires(T v) { value_type{v}; })
            property_box(T vertical, T horizontal) noexcept // NOLINT
                : top{vertical}, right{horizontal}, bottom{vertical}, left{horizontal}
            {
            }
            // 3 个值：上 | 左右 | 下
            template <typename T>
                requires(requires(T v) { value_type{v}; })
            property_box(T top, T horizontal, T bottom) noexcept // NOLINT
                : top{top}, right{horizontal}, bottom{bottom}, left{horizontal}
            {
            }
            // 4 个值：上 | 右 | 下 | 左
            template <typename T>
                requires(requires(T v) { value_type{v}; })
            property_box(T top, T right, T bottom, T left) noexcept // NOLINT
                : top{top}, right{right}, bottom{bottom}, left{left}
            {
            }
            void apply(YGNodeRef node) const noexcept
            {
                applyImpl(node, YGEdge::YGEdgeTop, top);
                applyImpl(node, YGEdge::YGEdgeRight, right);
                applyImpl(node, YGEdge::YGEdgeBottom, bottom);
                applyImpl(node, YGEdge::YGEdgeLeft, left);
            }
        };

        template <typename value_type, auto applyImpl>
        struct single_value
        {
            value_type value_{}; // NOLINT

            constexpr auto &operator*() noexcept
            {
                return value_;
            }
            constexpr const auto &operator*() const noexcept
            {
                return value_;
            }

            single_value() = default;
            template <typename T>
                requires(requires(T v) { value_type{v}; })
            single_value(T value) noexcept // NOLINT
                : value_{value}
            {
            }
            template <typename T>
                requires(requires(T v) { value_type{v}; })
            constexpr auto &operator=(T value) noexcept
            {
                this->value_ = value;
                return *this;
            }

            void apply(YGNodeRef node) const noexcept
            {
                applyImpl(node, value_);
            }
        };

        template <auto AutoFn, auto PixelsFn, auto PercentFn>
        struct common_property
        {
            using value_type = value_property;
            value_type value_{}; // NOLINT

            constexpr auto &operator*() noexcept
            {
                return value_;
            }
            constexpr const auto &operator*() const noexcept
            {
                return value_;
            }

            common_property() = default;
            template <typename T>
                requires(requires(T v) { value_type{v}; })
            common_property(T value) noexcept // NOLINT
                : value_{value}
            {
            }
            template <typename T>
                requires(requires(T v) { value_type{v}; })
            constexpr auto &operator=(T value) noexcept
            {
                this->value_ = value;
                return *this;
            }

            void apply(YGNodeRef node) const noexcept
            {
                std::visit(
                    [=](auto &&arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, property::Auto>)
                            AutoFn(node);
                        else if constexpr (std::is_same_v<T, property::Pixels>)
                            PixelsFn(node, arg.points);
                        else if constexpr (std::is_same_v<T, property::Percentage>)
                            PercentFn(node, arg.percent);
                        else
                            static_assert(false, "non-exhaustive visitor!");
                    },
                    *value_);
            }
        };

        template <auto PixelsFn, auto PercentFn>
        struct noauto_property
        {
            using value_type = value_property_noauto;
            value_type value_{}; // NOLINT

            constexpr auto &operator*() noexcept
            {
                return value_;
            }
            constexpr const auto &operator*() const noexcept
            {
                return value_;
            }

            noauto_property() = default;
            template <typename T>
                requires(requires(T v) { value_type{v}; })
            noauto_property(T value) noexcept // NOLINT
                : value_{value}
            {
            }
            template <typename T>
                requires(requires(T v) { value_type{v}; })
            constexpr auto &operator=(T value) noexcept
            {
                this->value_ = value;
                return *this;
            }

            void apply(YGNodeRef node) const noexcept
            {
                std::visit(
                    [=](auto &&arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::monostate>)
                            ;
                        else if constexpr (std::is_same_v<T, property::Pixels>)
                            PixelsFn(node, arg.points);
                        else if constexpr (std::is_same_v<T, property::Percentage>)
                            PercentFn(node, arg.percent);
                        else
                            static_assert(false, "non-exhaustive visitor!");
                    },
                    *value_);
            }
        };
    } // namespace property

    using property::Auto;
    using property::Pixels;
    using property::Percentage;

}; // namespace mcs::vulkan::yoga