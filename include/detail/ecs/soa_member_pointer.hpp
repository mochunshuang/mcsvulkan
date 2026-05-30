#pragma once

#include "size_type.hpp"
#include <utility>

namespace mcs::vulkan::ecs
{
    template <typename T>
    struct soa_member_pointer
    {
      public:
        using value_type = T;

        constexpr soa_member_pointer() noexcept : data_{nullptr} {}
        constexpr explicit soa_member_pointer(T *data) noexcept : data_{data} {}

        constexpr decltype(auto) operator[](this auto &&self, size_type idx) noexcept
        {
            return std::forward_like<decltype(self)>(self.data_[idx]);
        }

        constexpr const T *data() const noexcept
        {
            return data_;
        }
        constexpr T *data() noexcept
        {
            return data_;
        }

      private:
        T *data_;
    };

}; // namespace mcs::vulkan::ecs
