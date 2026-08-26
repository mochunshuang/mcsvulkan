#pragma once

#include <cassert>
#include <optional>
#include <utility>
#include "nsdms_of.hpp"

namespace mcs::vulkan::ecs
{
    template <typename Store>
    struct proxy_value
    {
        using value_type = Store::value_type;
        using id_type = Store::id_type;

        constexpr proxy_value(Store &store, id_type id) noexcept : store_{&store}, id_{id}
        {
        }
        proxy_value(const proxy_value &) = delete;
        proxy_value &operator=(const proxy_value &) = delete;
        constexpr proxy_value(proxy_value &&o) noexcept
            : store_{std::exchange(o.store_, {})}, id_{std::exchange(o.id_, {})}
        {
        }
        constexpr proxy_value &operator=(proxy_value &&o) noexcept
        {
            if (&o != this)
            {
                release();
                store_ = std::exchange(o.store_, {});
                id_ = std::exchange(o.id_, {});
            }
            return *this;
        }
        constexpr void release() noexcept
        {
            if (store_ != nullptr)
            {
                store_->destroy_at(id_);
                store_->release(id_);
                store_ = {};
                id_ = {};
            }
        }
        constexpr ~proxy_value() noexcept
        {
            release();
        }
        constexpr decltype(auto) operator*() noexcept
        {
            assert(store_ != nullptr);
            return (*store_)[id_]; // 直接物理访问
        }
        constexpr decltype(auto) operator*() const noexcept
        {
            assert(store_ != nullptr);
            return static_cast<const Store &>(*store_)[id_];
        }
        constexpr operator bool() const noexcept // NOLINT
        {
            return store_ != nullptr;
        }
        [[nodiscard]] constexpr bool has_value() const noexcept // NOLINT
        {
            return store_ != nullptr;
        }

        template <typename T, typename BindResult>
        static constexpr T to_value_type(const BindResult &b) // NOLINT
        {
            T copy{};
            static constexpr auto members = nsdms_of(^^T);           // NOLINT
            static constexpr auto members2 = nsdms_of(^^BindResult); // NOLINT
            static_assert(members.size() == members2.size());
            template for (constexpr auto I : std::views::indices(members.size()))
            {
                copy.[:members[I]:] = b.[:members2[I]:];
            }
            return copy;
        }

        constexpr value_type value() const
        {
            assert(store_ != nullptr);
            return to_value_type<value_type>(static_cast<const Store &>(*store_)[id_]);
        }
        constexpr std::optional<value_type> try_value() const // NOLINT
        {
            if (store_ == nullptr)
                return std::nullopt;
            return value();
        }

      private:
        Store *store_;
        id_type id_;
    };
}; // namespace mcs::vulkan::ecs
