#pragma once

#include "../__yoga_import.hpp"
#include <utility>

namespace mcs::vulkan::yoga
{
    class Config
    {
        using value_type = YGConfigRef;
        value_type value_{};

      public:
        constexpr operator bool() const noexcept // NOLINT
        {
            return value_ != nullptr;
        }
        constexpr auto &operator*() noexcept
        {
            return value_;
        }
        constexpr const auto &operator*() const noexcept
        {
            return value_;
        }
        [[nodiscard]] constexpr auto get() const noexcept
        {
            return value_;
        }
        constexpr void release() noexcept
        {
            if (value_ != nullptr)
                YGConfigFree(std::exchange(value_, {}));
        }
        Config() = default;
        constexpr explicit Config(value_type value) noexcept : value_{value} {}
        explicit Config(YGErrata errata) noexcept : value_{YGConfigNew()}
        {
            YGConfigSetErrata(value_, errata);
        }
        constexpr ~Config() noexcept
        {
            release();
        }
        Config(const Config &) = delete;
        constexpr Config(Config &&o) noexcept : value_{std::exchange(o.value_, {})} {}
        Config &operator=(const Config &) = delete;
        constexpr Config &operator=(Config &&o) noexcept
        {
            if (&o != this)
                value_ = std::exchange(o.value_, {});
            return *this;
        }

        auto &setErrata(YGErrata errata) & noexcept
        {
            YGConfigSetErrata(value_, errata);
            return *this;
        }
        auto &setPointScaleFactor(float pixelsInPoint) & noexcept
        {
            YGConfigSetPointScaleFactor(value_, pixelsInPoint);
            return *this;
        }
        auto &setUseWebDefaults(bool enabled) & noexcept
        {
            YGConfigSetUseWebDefaults(value_, enabled);
            return *this;
        }

        auto &&setErrata(YGErrata errata) && noexcept
        {
            YGConfigSetErrata(value_, errata);
            return std::move(*this);
        }
        auto &&setPointScaleFactor(float pixelsInPoint) && noexcept
        {
            YGConfigSetPointScaleFactor(value_, pixelsInPoint);
            return std::move(*this);
        }
        auto &&setUseWebDefaults(bool enabled) && noexcept
        {
            YGConfigSetUseWebDefaults(value_, enabled);
            return std::move(*this);
        }
    };
}; // namespace mcs::vulkan::yoga