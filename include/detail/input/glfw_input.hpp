#pragma once

#include "../event/keyboard_event_dispatcher.hpp"
#include "../event/mousebutton_event_dispatcher.hpp"
#include "../event/scroll_event_dispatcher.hpp"
#include "../event/cursor_pos_event_dispatcher.hpp"
#include "../event/cursor_enter_event_dispatcher.hpp"
#include <array>
#include <cstdint>
#include <print>
#include <utility>

namespace mcs::vulkan::input
{
    struct glfw_input
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

        static void onKeyboardEvent(void *self, event::keyboard_event key) noexcept
        {
            std::println("key: {}", key);
            auto *impl = static_cast<glfw_input *>(self);

            impl->keyboards_[static_cast<uint8_t>(key.key)] = std::move(key); // NOLINT
        }
        static void onMouseButtonEvent(void *self,
                                       event::mousebutton_event mouse) noexcept
        {
            std::println("mouse: {}", mouse);
            auto *impl = static_cast<glfw_input *>(self);
            // NOLINTNEXTLINE
            impl->mousebuttons_[static_cast<uint8_t>(mouse.button)] = mouse;
        }
        static void onScrollEvent(void *self, event::scroll_event scroll) noexcept
        {
            std::println("scroll: {}", scroll);
            auto *impl = static_cast<glfw_input *>(self);

            impl->scroll_ = scroll; // NOLINT
        }
        static void onCursorPosEvent(void *self, event::position2d_event pos) noexcept
        {
            std::println("cursorPos: {}", pos);
            auto *impl = static_cast<glfw_input *>(self);

            impl->cursorPos_ = std::move(pos); // NOLINT
        }
        static void onCursorEnter(void *self, event::cursor_enter_event enter) noexcept
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
                self.keyboards_[static_cast<event::keyboard_event::key_store_type>(key)]);
        }

        [[nodiscard]] decltype(auto) get_mousebutton_event(
            this auto &&self, event::MouseButtons btn) noexcept
        {
            return std::forward_like<decltype(self)>(
                self.mousebuttons_[static_cast<event::mousebutton_event::key_store_type>(
                    btn)]);
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

      private:
        std::array<event::keyboard_event, static_cast<uint8_t>(event::Key::eSIZE)>
            keyboards_;
        std::array<event::mousebutton_event,
                   static_cast<uint8_t>(event::MouseButtons::eSIZE)>
            mousebuttons_;
        event::scroll_event scroll_;
        event::position2d_event cursorPos_;
        event::cursor_enter_event cursorEnter_;
    };

}; // namespace mcs::vulkan::input
