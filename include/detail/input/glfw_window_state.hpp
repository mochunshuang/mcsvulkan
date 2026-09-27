#pragma once

#include "../event/window_focus_event_dispatcher.hpp"
#include "../event/window_pos_event_dispatcher.hpp"
#include "../event/window_size_event_dispatcher.hpp"
#include "../event/window_iconify_event_dispatcher.hpp"
#include "../event/window_maximize_event_dispatcher.hpp"
#include "../event/framebuffer_size_event_dispatcher.hpp"

#include <print>

namespace mcs::vulkan::input
{
    struct glfw_window_state
    {
        constexpr glfw_window_state() noexcept
        {
            event::window_focus_event_dispatcher::instance().subscribe(
                this, &glfw_window_state::onFocus);
            event::window_pos_event_dispatcher::instance().subscribe(
                this, &glfw_window_state::onPos);
            event::window_size_event_dispatcher::instance().subscribe(
                this, &glfw_window_state::onSize);
            event::window_iconify_event_dispatcher::instance().subscribe(
                this, &glfw_window_state::onIconify);
            event::window_maximize_event_dispatcher::instance().subscribe(
                this, &glfw_window_state::onMaximize);
            event::framebuffer_size_event_dispatcher::instance().subscribe(
                this, &glfw_window_state::onFramebufferSize);
        }

        constexpr ~glfw_window_state() noexcept
        {
            event::window_focus_event_dispatcher::instance().unsubscribe(
                this, &glfw_window_state::onFocus);
            event::window_pos_event_dispatcher::instance().unsubscribe(
                this, &glfw_window_state::onPos);
            event::window_size_event_dispatcher::instance().unsubscribe(
                this, &glfw_window_state::onSize);
            event::window_iconify_event_dispatcher::instance().unsubscribe(
                this, &glfw_window_state::onIconify);
            event::window_maximize_event_dispatcher::instance().unsubscribe(
                this, &glfw_window_state::onMaximize);
            event::framebuffer_size_event_dispatcher::instance().unsubscribe(
                this, &glfw_window_state::onFramebufferSize);
        }

        glfw_window_state(const glfw_window_state &) = delete;
        glfw_window_state(glfw_window_state &&) = delete;
        glfw_window_state &operator=(const glfw_window_state &) = delete;
        glfw_window_state &operator=(glfw_window_state &&) = delete;

        // ═══════════════════════════════════════════════════════════
        // 回调：只做字段更新，不做任何决策
        // ═══════════════════════════════════════════════════════════
        static void onFocus(void *self, event::window_focus_event e) noexcept
        {
            std::println("window_focus_event: {}", e);
            static_cast<glfw_window_state *>(self)->focused_ = e.focused;
        }
        static void onPos(void *self, event::window_pos_event e) noexcept
        {
            std::println("window_pos_event: {}", e);
            auto *impl = static_cast<glfw_window_state *>(self);
            impl->posX_ = e.x;
            impl->posY_ = e.y;
        }
        static void onSize(void *self, event::window_size_event e) noexcept
        {
            std::println("window_size_event: {}", e);
            auto *impl = static_cast<glfw_window_state *>(self);
            impl->width_ = e.width;
            impl->height_ = e.height;
        }
        static void onIconify(void *self, event::window_iconify_event e) noexcept
        {
            std::println("window_iconify_event: {}", e);
            static_cast<glfw_window_state *>(self)->iconified_ = e.iconified;
        }
        static void onMaximize(void *self, event::window_maximize_event e) noexcept
        {
            std::println("window_maximize_event: {}", e);
            static_cast<glfw_window_state *>(self)->maximized_ = e.maximized;
        }
        static void onFramebufferSize(void *self,
                                      event::framebuffer_size_event e) noexcept
        {
            std::println("framebuffer_size_event: {}", e);
            auto *impl = static_cast<glfw_window_state *>(self);
            impl->fbWidth_ = e.width;
            impl->fbHeight_ = e.height;
        }

        // ═══════════════════════════════════════════════════════════
        // 查询 API
        // ═══════════════════════════════════════════════════════════
        [[nodiscard]] bool isFocused() const noexcept
        {
            return focused_;
        }
        [[nodiscard]] bool isIconified() const noexcept
        {
            return iconified_;
        }
        [[nodiscard]] bool isMaximized() const noexcept
        {
            return maximized_;
        }

        [[nodiscard]] int posX() const noexcept
        {
            return posX_;
        }
        [[nodiscard]] int posY() const noexcept
        {
            return posY_;
        }

        [[nodiscard]] int width() const noexcept
        {
            return width_;
        }
        [[nodiscard]] int height() const noexcept
        {
            return height_;
        }

        [[nodiscard]] int framebufferWidth() const noexcept
        {
            return fbWidth_;
        }
        [[nodiscard]] int framebufferHeight() const noexcept
        {
            return fbHeight_;
        }

      private:
        // 默认值：合理假设，等第一个事件到达后覆盖
        bool focused_ = true;
        bool iconified_ = false;
        bool maximized_ = false;
        int posX_ = 0;
        int posY_ = 0;
        int width_ = 0;
        int height_ = 0;
        int fbWidth_ = 0;
        int fbHeight_ = 0;
    };

}; // namespace mcs::vulkan::input