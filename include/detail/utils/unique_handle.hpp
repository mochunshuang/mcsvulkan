#pragma once

#include <type_traits>
#include <utility>

namespace mcs::vulkan
{
    // NOTE: auto 值语意 可以将lambda 隐式为指针，变得更简洁
    template <typename Ptr_Type, auto Deleter>
        requires(std::is_pointer_v<Ptr_Type> &&
                 requires(Ptr_Type value) {
                     { Deleter(value) } noexcept -> std::same_as<void>;
                 })
    struct unique_handle
    {
      private:
        Ptr_Type value_{};

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
                Deleter(value_);
        }
        unique_handle() = default;
        constexpr explicit unique_handle(Ptr_Type value) noexcept : value_{value} {}
        constexpr ~unique_handle() noexcept
        {
            release();
        }
        unique_handle(const unique_handle &) = delete;
        constexpr unique_handle(unique_handle &&o) noexcept
            : value_{std::exchange(o.value_, {})}
        {
        }
        unique_handle &operator=(const unique_handle &) = delete;
        constexpr unique_handle &operator=(unique_handle &&o) noexcept
        {
            if (&o != this)
                value_ = std::exchange(o.value_, {});
            return *this;
        }
    };
}; // namespace mcs::vulkan