#pragma once

#include "size_type.hpp"
#include <utility>

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

    // forward_like 适用场景：转发"对象引用"
    // forward_like 不适用场景：转发"裸指针"（需要穿透 const 到指向类型时）
    // 如果 T* 需要变成 const T*，请手动处理，不要用 forward_like。

    //NOTE: forward_like 无法穿透指针的间接层。 因此用错了，就是错误的来源
    // data() - 转发指针 ❌
    // return std::forward_like<decltype(self)>(self.data_);
    //                                        ^^^^^^^^^^
    //                                        int* — 指针类型
    // NOTE: 下面是错误的的实践
    // auto data(this auto &&self) noexcept
    // {
    //     return std::forward_like<decltype(self)>(self.data_);
    // }

    const T *data() const noexcept
    {
        return data_;
    }
    T *data() noexcept
    {
        return data_;
    }

  private:
    T *data_;
};