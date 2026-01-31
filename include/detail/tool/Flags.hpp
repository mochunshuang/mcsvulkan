#pragma once

#include <type_traits>

#include "../__vulkan.hpp"

namespace mcs::vulkan::tool
{
    template <typename FlagBits>
        requires(std::is_enum_v<FlagBits>)
    class Flags
    {
        using underlying_type = std::underlying_type_t<FlagBits>;

        using value_type = ::VkFlags;
        value_type value_;

      public:
        // constexpr value_type &operator*() noexcept
        // {
        //     return value_;
        // }
        // constexpr const value_type &operator*() const noexcept
        // {
        //     return value_;
        // }

        // 转换为底层类型 比上面可读性好 // NOLINTNEXTLINE
        constexpr operator value_type() const noexcept
        {
            return value_;
        }

        // 从枚举值构造 // NOLINTNEXTLINE  允许隐式转化
        constexpr Flags(FlagBits flag) noexcept
            : value_(static_cast<underlying_type>(flag))
        {
        }
        constexpr Flags() noexcept : Flags(FlagBits{}) {}
        // 从底层值构造 // NOLINTNEXTLINE  允许隐式转化.
        constexpr Flags(underlying_type val) noexcept : value_(val) {}

        Flags(Flags &&) = default;
        Flags &operator=(const Flags &) = default;
        Flags &operator=(Flags &&) = default;
        ~Flags() = default;

        // 复制构造
        constexpr Flags(const Flags &) = default;

        // 转换为包装器本身（为了重载解析）
        constexpr Flags operator~() const noexcept
        {
            return Flags(~value_);
        }

        // 位运算
        constexpr Flags operator|(FlagBits flag) const noexcept
        {
            return Flags(value_ | static_cast<underlying_type>(flag));
        }

        constexpr Flags operator|(Flags other) const noexcept
        {
            return Flags(value_ | other.value_);
        }

        constexpr Flags operator&(FlagBits flag) const noexcept
        {
            return Flags(value_ & static_cast<underlying_type>(flag));
        }

        constexpr Flags operator&(Flags other) const noexcept
        {
            return Flags(value_ & other.value_);
        }

        constexpr Flags operator^(FlagBits flag) const noexcept
        {
            return Flags(value_ ^ static_cast<underlying_type>(flag));
        }

        constexpr Flags operator^(Flags other) const noexcept
        {
            return Flags(value_ ^ other.value_);
        }

        // 复合赋值
        constexpr Flags &operator|=(FlagBits flag) noexcept
        {
            value_ |= static_cast<underlying_type>(flag);
            return *this;
        }

        constexpr Flags &operator|=(Flags other) noexcept
        {
            value_ |= other.value_;
            return *this;
        }

        constexpr Flags &operator&=(FlagBits flag) noexcept
        {
            value_ &= static_cast<underlying_type>(flag);
            return *this;
        }

        constexpr Flags &operator&=(Flags other) noexcept
        {
            value_ &= other.value_;
            return *this;
        }

        constexpr Flags &operator^=(FlagBits flag) noexcept
        {
            value_ ^= static_cast<underlying_type>(flag);
            return *this;
        }

        constexpr Flags &operator^=(Flags other) noexcept
        {
            value_ ^= other.value_;
            return *this;
        }

        // 比较运算符
        friend constexpr bool operator==(Flags a, Flags b) noexcept = default;
    };
}; // namespace mcs::vulkan::tool
