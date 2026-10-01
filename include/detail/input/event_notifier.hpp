#pragma once

#include "../event/gen_change_event_fn.hpp"

#include <chrono>
#include <iterator>
#include <vector>

namespace mcs::vulkan::input
{
    template <typename event_type>
    struct event_notifier
    {
        using callback_type = event::gen_change_event_fn<event_type>;

        struct value_type
        {
            void *ctx;
            callback_type callback;
            bool valid;
        };

        event_notifier() = default;
        event_notifier(const event_notifier &) = delete;
        event_notifier(event_notifier &&) = delete;
        event_notifier &operator=(const event_notifier &) = delete;
        event_notifier &operator=(event_notifier &&) = delete;
        ~event_notifier() = default;

        constexpr void subscribe(void *ctx, callback_type cb)
        {
            if (distributing_)
            {
                pendingAdd_.push_back({ctx, cb, true});
                return;
            }
            callbacks_.push_back({ctx, cb, true});
        }

        constexpr void unsubscribe(void *ctx, callback_type cb)
        {
            // 正式列表：标记失效 + 打脏标记
            for (auto &item : callbacks_)
                if (item.ctx == ctx && item.callback == cb)
                {
                    item.valid = false;
                    hasInvalid_ = true;
                }

            // 待加入列表：分发中 subscribe 又立刻 unsubscribe
            for (auto &item : pendingAdd_)
                if (item.ctx == ctx && item.callback == cb)
                {
                    item.valid = false;
                    hasInvalid_ = true;
                }
        }

        constexpr void distribute(event::position2d_event cursorPos,
                                  std::chrono::steady_clock::time_point time_point,
                                  event_type event)
        {
            // 只有最外层才有资格 apply pending / 清理，内层只遍历。处理递归。
            const bool outer = !distributing_; // NOLINT
            if (outer)
                distributing_ = true;

            for (const auto &item : callbacks_)
                if (item.valid)
                    (item.callback)(item.ctx, cursorPos, time_point, event);

            if (outer)
            {
                distributing_ = false;

                // 1) 提交本层分发期间新增的订阅（通常为空）
                if (!pendingAdd_.empty())
                {
                    callbacks_.insert(callbacks_.end(),
                                      std::make_move_iterator(pendingAdd_.begin()),
                                      std::make_move_iterator(pendingAdd_.end()));
                    pendingAdd_.clear();
                }

                // 2) 只有真的出现过 unsubscribe 才清理（常态下完全跳过 O(n)）
                if (hasInvalid_)
                {
                    std::erase_if(callbacks_,
                                  [](const value_type &v) noexcept { return !v.valid; });
                    hasInvalid_ = false;
                }
            }
        }

      private:
        std::vector<value_type> callbacks_;
        std::vector<value_type> pendingAdd_;
        bool distributing_ = false;
        bool hasInvalid_ = false;
    };
} // namespace mcs::vulkan::input