#pragma once

#include <vector>

namespace mcs::vulkan::event
{
    template <typename T>
    struct event_dispatcher
    {
        using event_type = T;
        using callback_type = void(void *, event_type event) noexcept;

        struct value_type
        {
            void *ctx;
            callback_type *callback;
            bool valid;
        };

        constexpr void subscribe(void *ctx, callback_type *cb)
        {
            if (distributing_)
            {
                pendingAdd_.push_back({ctx, cb, true});
                return;
            }
            callbacks_.push_back({ctx, cb, true});
        }

        constexpr void unsubscribe(void *ctx, callback_type *cb)
        {
            for (auto &item : callbacks_)
                if (item.ctx == ctx && item.callback == cb)
                    item.valid = false;
            // 分发中：只标记；非分发中：下次 distribute 时会清掉
        }

        constexpr void distribute(event_type event) noexcept
        {
            // 只有最外层才有资格清理和 apply pending，内层只遍历。处理递归
            bool outer = !distributing_;
            if (outer)
                distributing_ = true;

            for (const auto &item : callbacks_)
                if (item.valid)
                    (item.callback)(item.ctx, event);

            if (outer)
            {
                distributing_ = false;

                callbacks_.insert(callbacks_.end(), pendingAdd_.begin(),
                                  pendingAdd_.end());
                pendingAdd_.clear();

                callbacks_.erase(
                    std::remove_if(callbacks_.begin(), callbacks_.end(),
                                   [](const value_type &v) { return !v.valid; }),
                    callbacks_.end());
            }
        }
        constexpr static auto &instance() noexcept
        {
            static event_dispatcher instance;
            return instance;
        }

      private:
        std::vector<value_type> callbacks_;
        std::vector<value_type> pendingAdd_;
        bool distributing_ = false;
        event_dispatcher() = default;
    };

}; // namespace mcs::vulkan::event