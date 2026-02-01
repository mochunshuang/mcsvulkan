#pragma once

#include <unordered_set>

namespace mcs::vulkan::event
{
    template <typename T>
    struct event_dispatcher
    {
        using event_type = T;
        using callback_type = void(void *, event_type event) noexcept;

        struct value_type
        {
            void *ctx;               // NOLINT
            callback_type *callback; // NOLINT
            bool operator==(const value_type &other) const
            {
                return ctx == other.ctx && callback == other.callback;
            }
        };
        struct value_type_hash
        {
            std::size_t operator()(const value_type &v) const
            {
                std::size_t h1 = std::hash<void *>{}(v.ctx);
                std::size_t h2 = std::hash<callback_type *>{}(v.callback);

                // 使用简单的组合方式（注意：这种组合方式可能不够理想）
                // return h1 ^ (h2 << 1);
                // 更好的组合方式（使用 boost::hash_combine 风格）
                return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2)); // NOLINT
            }
        };

        constexpr void distribute(event_type event) noexcept
        {
            for (const value_type &item : callbacks_)
                (item.callback)(item.ctx, event);
        }
        constexpr void subscribe(void *ctx, callback_type *callback)
        {
            callbacks_.emplace(value_type{ctx, callback});
        }
        constexpr void unsubscribe(void *ctx, callback_type *callback)
        {
            callbacks_.erase(value_type{ctx, callback});
        }

        constexpr static auto &instance() noexcept
        {
            static event_dispatcher instance;
            return instance;
        }

      private:
        std::unordered_set<value_type, value_type_hash> callbacks_;
        event_dispatcher() = default;
    };

}; // namespace mcs::vulkan::event