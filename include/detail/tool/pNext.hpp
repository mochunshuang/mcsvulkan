#pragma once

#include "structure_chain.hpp"
#include <memory>
#include <utility>

namespace mcs::vulkan::tool
{
    class pNext // NOLINT
    {
        using value_type = void *;

        std::unique_ptr<uint8_t[]> storage_; // NOLINT

      public:
        pNext() = default;
        pNext(const pNext &) = delete;
        constexpr pNext(pNext &&o) noexcept : storage_{std::exchange(o.storage_, {})} {}
        pNext &operator=(const pNext &) = delete;
        pNext &operator=(pNext &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                storage_ = std::exchange(o.storage_, {});
            }
            return *this;
        }
        // NOLINTNEXTLINE
        constexpr operator bool() const noexcept
        {
            return storage_ != nullptr;
        }
        constexpr void destroy() noexcept
        {
            storage_.reset();
        }
        constexpr ~pNext() noexcept
        {
            destroy();
        }
        constexpr void *operator*() noexcept
        {
            return storage_.get();
        }
        constexpr const void *operator*() const noexcept
        {
            return storage_.get();
        }
        [[nodiscard]] constexpr void *value() const noexcept
        {
            return storage_.get();
        }

        constexpr pNext(std::unique_ptr<uint8_t[]> storage) noexcept // NOLINT
            : storage_{std::move(storage)}
        {
        }
    };

    template <typename... N>
    constexpr auto make_pNext(const structure_chain<N...> &value)
    {
        using T = structure_chain<N...>;

        static_assert(sizeof(T) == (sizeof(N) + ...));

        auto storage = std::make_unique_for_overwrite<uint8_t[]>(sizeof(T)); // NOLINT

        T *ptr = reinterpret_cast<T *>(storage.get()); // NOLINT

        // 构造对象
        new (ptr) T(value);

        return pNext(std::move(storage));
    }

}; // namespace mcs::vulkan::tool