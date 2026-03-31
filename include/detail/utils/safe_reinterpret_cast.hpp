#pragma once

namespace mcs::vulkan
{
    // 指针版本（主要使用）
    template <typename To, typename From>
    constexpr static To safe_reinterpret_cast(From *ptr) noexcept
    {
        static_assert(sizeof(To) == sizeof(From *), "Size mismatch");
        static_assert(alignof(To) == alignof(From *), "Alignment mismatch");
        return reinterpret_cast<To>(ptr);
    }

    // 引用版本
    template <typename To, typename From>
    constexpr static To safe_reinterpret_cast(From &ref) noexcept
    {
        static_assert(sizeof(To) == sizeof(From), "Size mismatch");
        static_assert(alignof(To) == alignof(From), "Alignment mismatch");
        return reinterpret_cast<To>(ref);
    }

}; // namespace mcs::vulkan