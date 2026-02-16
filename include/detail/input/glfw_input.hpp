#pragma once

#include "input_interface.hpp"
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
        glfw_input(const glfw_input &) = default;
        glfw_input(glfw_input &&) = default;
        glfw_input &operator=(const glfw_input &) = default;
        glfw_input &operator=(glfw_input &&) = default;

        static void onKeyboardEvent(void *self, keyboard_event key) noexcept
        {
            std::println("key: {}", key);
            auto *impl = static_cast<glfw_input *>(self);

            impl->keyboards_[static_cast<uint8_t>(key.key)] = std::move(key); // NOLINT
        }
        static void onMouseButtonEvent(void *self, mousebutton_event mouse) noexcept
        {
            std::println("mouse: {}", mouse);
            auto *impl = static_cast<glfw_input *>(self);
            // NOLINTNEXTLINE
            impl->mousebuttons_[static_cast<uint8_t>(mouse.button)] = std::move(mouse);
        }
        static void onScrollEvent(void *self, scroll_event scroll) noexcept
        {
            std::println("scroll: {}", scroll);
            auto *impl = static_cast<glfw_input *>(self);

            impl->scroll_ = std::move(scroll); // NOLINT
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

        [[nodiscard]] const auto &keyboards() const noexcept
        {
            return keyboards_;
        }

        [[nodiscard]] scroll_event &scroll() noexcept
        {
            return scroll_;
        }

        [[nodiscard]] const position2d_event &cursorPos() const noexcept
        {
            return cursorPos_;
        }
        [[nodiscard]] const cursor_enter_event &cursorEnter() const noexcept
        {
            return cursorEnter_;
        }

        // NOLINTBEGIN

        [[nodiscard]] const auto &get_keyboard_event(const event::Key &key) const noexcept
        {
            return keyboards_[static_cast<keyboard_event::key_store_type>(key)]; // NOLINT
        }
        [[nodiscard]] auto &get_keyboard_event(const event::Key &key) noexcept // NOLINT
        {
            return keyboards_[static_cast<keyboard_event::key_store_type>(key)]; // NOLINT
        }

        [[nodiscard]] const auto &get_mousebutton_event(
            const event::MouseButtons &btn) const noexcept
        {
            return mousebuttons_[static_cast<mousebutton_event::key_store_type>(btn)];
        }
        [[nodiscard]] auto &get_mousebutton_event(const event::MouseButtons &btn) noexcept
        {
            return mousebuttons_[static_cast<mousebutton_event::key_store_type>(btn)];
        }
        // NOLINTEND

        // API::
        auto isKeyPressedOrRepeat(event::Key key) noexcept
        {
            const auto &event = get_keyboard_event(key);
            return event.press() || event.repeat();
        };
        auto isKeyPressed(event::Key key) noexcept
        {
            return get_keyboard_event(key).press();
        };
        auto isKeyRepeat(event::Key key) noexcept
        {
            return get_keyboard_event(key).repeat();
        };

      private:
        std::array<keyboard_event, static_cast<uint8_t>(event::Key::eSIZE)> keyboards_;
        std::array<mousebutton_event, static_cast<uint8_t>(event::MouseButtons::eSIZE)>
            mousebuttons_;
        scroll_event scroll_;
        position2d_event cursorPos_;
        cursor_enter_event cursorEnter_;
    };

}; // namespace mcs::vulkan::input
