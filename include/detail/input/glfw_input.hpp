#pragma once

#include "input_interface.hpp"
#include "../event/keyboard_event_dispatcher.hpp"
#include "../event/mousebutton_event_dispatcher.hpp"
#include "../event/scroll_event_dispatcher.hpp"
#include "../event/cursor_pos_event_dispatcher.hpp"
#include "../event/cursor_enter_event_dispatcher.hpp"
#include "../event/gen_change_event_fn.hpp"
#include <array>
#include <cstdint>
#include <print>
#include <type_traits>
#include <utility>
#include <vector>

namespace mcs::vulkan::input
{
    struct glfw_input : input_interface
    {
        constexpr glfw_input()
        {
            event::keyboard_event_dispatcher::instance().subscribe(
                this, &glfw_input::onKeyboardEvent);
            event::mousebutton_event_dispatcher::instance().subscribe(
                this, &glfw_input::onMouseButtonEvent);
            event::scroll_event_dispatcher::instance().subscribe(
                this, &glfw_input::onScrollEvent);
            event::cursor_pos_event_dispatcher::instance().subscribe(
                this, &glfw_input::onCursorPosEvent);
            event::cursor_enter_event_dispatcher::instance().subscribe(
                this, &glfw_input::onCursorEnter);
        }
        constexpr ~glfw_input() noexcept
        {
            event::keyboard_event_dispatcher::instance().unsubscribe(
                this, &glfw_input::onKeyboardEvent);
            event::mousebutton_event_dispatcher::instance().unsubscribe(
                this, &glfw_input::onMouseButtonEvent);
            event::scroll_event_dispatcher::instance().unsubscribe(
                this, &glfw_input::onScrollEvent);
            event::cursor_pos_event_dispatcher::instance().unsubscribe(
                this, &glfw_input::onCursorPosEvent);
            event::cursor_enter_event_dispatcher::instance().unsubscribe(
                this, &glfw_input::onCursorEnter);
        }
        // 和 this 有关 最好是全部删除
        glfw_input(const glfw_input &) = delete;
        glfw_input(glfw_input &&) = delete;
        glfw_input &operator=(const glfw_input &) = delete;
        glfw_input &operator=(glfw_input &&) = delete;

        static void onKeyboardEvent(void *self, keyboard_event key) noexcept
        {
            std::println("key: {}", key);
            auto *impl = static_cast<glfw_input *>(self);

            impl->keyboards_[static_cast<uint8_t>(key.key)] = std::move(key); // NOLINT
            impl->keyboardNotify_.distribute(impl->cursorPos_,
                                             std::chrono::steady_clock::now(), key);
        }
        static void onMouseButtonEvent(void *self, mousebutton_event mouse) noexcept
        {
            std::println("mouse: {}", mouse);
            auto *impl = static_cast<glfw_input *>(self);
            // NOLINTNEXTLINE
            impl->mousebuttons_[static_cast<uint8_t>(mouse.button)] = mouse;
            impl->mousebuttonNotify_.distribute(impl->cursorPos_,
                                                std::chrono::steady_clock::now(), mouse);
        }
        static void onScrollEvent(void *self, scroll_event scroll) noexcept
        {
            std::println("scroll: {}", scroll);
            auto *impl = static_cast<glfw_input *>(self);

            impl->scroll_ = scroll; // NOLINT
            impl->scrollNotify_.distribute(impl->cursorPos_,
                                           std::chrono::steady_clock::now(), scroll);
        }
        static void onCursorPosEvent(void *self, position2d_event pos) noexcept
        {
            std::println("cursorPos: {}", pos);
            auto *impl = static_cast<glfw_input *>(self);

            impl->cursorPos_ = std::move(pos); // NOLINT
        }
        static void onCursorEnter(void *self, cursor_enter_event enter) noexcept
        {
            auto *impl = static_cast<glfw_input *>(self);

            std::println("cursorEnter: {}", enter);
            impl->cursorEnter_ = std::move(enter); // NOLINT
        }

        [[nodiscard]] decltype(auto) keyboards(this auto &&self) noexcept
        {
            return std::forward_like<decltype(self)>(self.keyboards_);
        }

        [[nodiscard]] decltype(auto) scroll(this auto &&self) noexcept
        {
            return std::forward_like<decltype(self)>(self.scroll_);
        }

        [[nodiscard]] decltype(auto) cursorPos(this auto &&self) noexcept
        {
            return std::forward_like<decltype(self)>(self.cursorPos_);
        }

        // NOLINTBEGIN
        [[nodiscard]] decltype(auto) get_keyboard_event(this auto &&self,
                                                        event::Key key) noexcept
        {
            return std::forward_like<decltype(self)>(
                self.keyboards_[static_cast<keyboard_event::key_store_type>(key)]);
        }

        [[nodiscard]] decltype(auto) get_mousebutton_event(
            this auto &&self, event::MouseButtons btn) noexcept
        {
            return std::forward_like<decltype(self)>(
                self.mousebuttons_[static_cast<mousebutton_event::key_store_type>(btn)]);
        }
        // NOLINTEND

        // API::
        [[nodiscard]] auto isKeyPressedOrRepeat(event::Key key) const noexcept
        {
            const auto &event = get_keyboard_event(key);
            return event.press() || event.repeat();
        };
        [[nodiscard]] auto isKeyPressed(event::Key key) const noexcept
        {
            return get_keyboard_event(key).press();
        };
        [[nodiscard]] auto isKeyRepeat(event::Key key) const noexcept
        {
            return get_keyboard_event(key).repeat();
        };
        [[nodiscard]] auto isMouseButtonPressed(event::MouseButtons key) const noexcept
        {
            return get_mousebutton_event(key).press();
        };
        [[nodiscard]] auto isMouseButtonRelease(event::MouseButtons key) const noexcept
        {
            return get_mousebutton_event(key).release();
        };

        // using keyboard_change_fn = event::keyboard_change_fn;
        // using mousebutton_change_fn = event::mousebutton_event;
        // using scroll_event_change_fn = event::scroll_event;
        using keyboard_change_fn = event::gen_change_event_fn<keyboard_event>;
        using mousebutton_change_fn = event::gen_change_event_fn<mousebutton_event>;
        using scroll_event_change_fn = event::gen_change_event_fn<scroll_event>;

        template <typename callback_type>
        constexpr void subscribe(void *ctx, callback_type cb)
        {
            if constexpr (std::is_same_v<keyboard_change_fn, std::decay_t<callback_type>>)
                keyboardNotify_.subscribe(ctx, cb);
            else if constexpr (std::is_same_v<mousebutton_change_fn,
                                              std::decay_t<callback_type>>)
                mousebuttonNotify_.subscribe(ctx, cb);
            else if constexpr (std::is_same_v<scroll_event_change_fn,
                                              std::decay_t<callback_type>>)
                scrollNotify_.subscribe(ctx, cb);
            else
                static_assert(false, "bad callback_type");
        }
        template <typename callback_type>
        constexpr void unsubscribe(void *ctx, callback_type cb)
        {
            if constexpr (std::is_same_v<keyboard_change_fn, std::decay_t<callback_type>>)
                keyboardNotify_.unsubscribe(ctx, cb);
            else if constexpr (std::is_same_v<mousebutton_change_fn,
                                              std::decay_t<callback_type>>)
                mousebuttonNotify_.unsubscribe(ctx, cb);
            else if constexpr (std::is_same_v<scroll_event_change_fn,
                                              std::decay_t<callback_type>>)
                scrollNotify_.unsubscribe(ctx, cb);
            else
                static_assert(false, "bad callback_type");
        }

        template <typename event_type>
        struct event_change_distributor
        {
            using callback_type = event::gen_change_event_fn<event_type>;

            struct value_type
            {
                void *ctx;
                callback_type callback;
                bool valid;
            };

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
                for (auto &item : callbacks_)
                    if (item.ctx == ctx && item.callback == cb)
                        item.valid = false;
                // 分发中：只标记；非分发中：下次 distribute 时会清掉
            }

            constexpr void distribute(position2d_event cursorPos,
                                      std::chrono::steady_clock::time_point time_point,
                                      event_type event) noexcept
            {
                // 只有最外层才有资格清理和 apply pending，内层只遍历。处理递归
                bool outer = !distributing_;
                if (outer)
                    distributing_ = true;

                for (const auto &item : callbacks_)
                    if (item.valid)
                        (item.callback)(item.ctx, cursorPos, time_point, event);

                if (outer)
                {
                    distributing_ = false;

                    callbacks_.insert(callbacks_.end(), pendingAdd_.begin(),
                                      pendingAdd_.end());
                    pendingAdd_.clear();

                    callbacks_.erase(std::remove_if(callbacks_.begin(), callbacks_.end(),
                                                    [](const value_type &v) noexcept {
                                                        return !v.valid;
                                                    }),
                                     callbacks_.end());
                }
            }

          private:
            std::vector<value_type> callbacks_;
            std::vector<value_type> pendingAdd_;
            bool distributing_ = false;
        };

      private:
        std::array<keyboard_event, static_cast<uint8_t>(event::Key::eSIZE)> keyboards_;
        std::array<mousebutton_event, static_cast<uint8_t>(event::MouseButtons::eSIZE)>
            mousebuttons_;
        scroll_event scroll_;
        position2d_event cursorPos_;
        cursor_enter_event cursorEnter_;

        event_change_distributor<keyboard_event> keyboardNotify_;
        event_change_distributor<mousebutton_event> mousebuttonNotify_;
        event_change_distributor<scroll_event> scrollNotify_;
    };

}; // namespace mcs::vulkan::input
