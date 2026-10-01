#pragma once

#include "../event/keyboard_event_dispatcher.hpp"
#include "../event/mousebutton_event_dispatcher.hpp"
#include "../event/scroll_event_dispatcher.hpp"
#include "../event/cursor_pos_event_dispatcher.hpp"
#include "../event/cursor_enter_event_dispatcher.hpp"

#include "../event/char_event_dispatcher.hpp"
#include "../event/drop_event_dispatcher.hpp"

#include "../event/window_focus_event_dispatcher.hpp"
#include "../event/window_pos_event_dispatcher.hpp"
#include "../event/window_size_event_dispatcher.hpp"
#include "../event/window_iconify_event_dispatcher.hpp"
#include "../event/window_maximize_event_dispatcher.hpp"
#include "../event/framebuffer_size_event_dispatcher.hpp"

#include "../event/gen_change_event_fn.hpp"

#include "event_notifier.hpp"

#include <chrono>
#include <type_traits>
#include <utility>

namespace mcs::vulkan::input
{
    struct glfw_forward
    {
        // ────────────────────────────────────────────────────────
        // 回调类型别名：一处定义，多处复用
        // ────────────────────────────────────────────────────────
        using keyboard_change_fn = event::gen_change_event_fn<event::keyboard_event>;
        using mousebutton_change_fn =
            event::gen_change_event_fn<event::mousebutton_event>;
        using scroll_event_change_fn = event::gen_change_event_fn<event::scroll_event>;
        using cursor_enter_change_fn =
            event::gen_change_event_fn<event::cursor_enter_event>;
        using char_event_change_fn = event::gen_change_event_fn<event::char_event>;
        using drop_event_change_fn = event::gen_change_event_fn<event::drop_event>;
        using focus_event_change_fn =
            event::gen_change_event_fn<event::window_focus_event>;
        using pos_event_change_fn = event::gen_change_event_fn<event::window_pos_event>;
        using size_event_change_fn = event::gen_change_event_fn<event::window_size_event>;
        using iconify_event_change_fn =
            event::gen_change_event_fn<event::window_iconify_event>;
        using maximize_event_change_fn =
            event::gen_change_event_fn<event::window_maximize_event>;
        using fb_size_event_change_fn =
            event::gen_change_event_fn<event::framebuffer_size_event>;

        // ────────────────────────────────────────────────────────
        // 构造 / 析构：注册转发中心为各 dispatcher 的订阅者
        // ────────────────────────────────────────────────────────
        constexpr glfw_forward() noexcept
        {
            using self = glfw_forward;

            event::keyboard_event_dispatcher::instance().subscribe(
                this, &self::onKeyboardEvent);
            event::mousebutton_event_dispatcher::instance().subscribe(
                this, &self::onMouseButtonEvent);
            event::scroll_event_dispatcher::instance().subscribe(this,
                                                                 &self::onScrollEvent);
            event::cursor_pos_event_dispatcher::instance().subscribe(
                this, &self::onCursorPosEvent);
            event::cursor_enter_event_dispatcher::instance().subscribe(
                this, &self::onCursorEnterEvent);

            event::char_event_dispatcher::instance().subscribe(this, &self::onCharEvent);
            event::drop_event_dispatcher::instance().subscribe(this, &self::onDropEvent);

            event::window_focus_event_dispatcher::instance().subscribe(
                this, &self::onFocusEvent);
            event::window_pos_event_dispatcher::instance().subscribe(this,
                                                                     &self::onPosEvent);
            event::window_size_event_dispatcher::instance().subscribe(this,
                                                                      &self::onSizeEvent);
            event::window_iconify_event_dispatcher::instance().subscribe(
                this, &self::onIconifyEvent);
            event::window_maximize_event_dispatcher::instance().subscribe(
                this, &self::onMaximizeEvent);
            event::framebuffer_size_event_dispatcher::instance().subscribe(
                this, &self::onFramebufferSizeEvent);
        }

        constexpr ~glfw_forward() noexcept
        {
            using self = glfw_forward;

            event::keyboard_event_dispatcher::instance().unsubscribe(
                this, &self::onKeyboardEvent);
            event::mousebutton_event_dispatcher::instance().unsubscribe(
                this, &self::onMouseButtonEvent);
            event::scroll_event_dispatcher::instance().unsubscribe(this,
                                                                   &self::onScrollEvent);
            event::cursor_pos_event_dispatcher::instance().unsubscribe(
                this, &self::onCursorPosEvent);
            event::cursor_enter_event_dispatcher::instance().unsubscribe(
                this, &self::onCursorEnterEvent);

            event::char_event_dispatcher::instance().unsubscribe(this,
                                                                 &self::onCharEvent);
            event::drop_event_dispatcher::instance().unsubscribe(this,
                                                                 &self::onDropEvent);

            event::window_focus_event_dispatcher::instance().unsubscribe(
                this, &self::onFocusEvent);
            event::window_pos_event_dispatcher::instance().unsubscribe(this,
                                                                       &self::onPosEvent);
            event::window_size_event_dispatcher::instance().unsubscribe(
                this, &self::onSizeEvent);
            event::window_iconify_event_dispatcher::instance().unsubscribe(
                this, &self::onIconifyEvent);
            event::window_maximize_event_dispatcher::instance().unsubscribe(
                this, &self::onMaximizeEvent);
            event::framebuffer_size_event_dispatcher::instance().unsubscribe(
                this, &self::onFramebufferSizeEvent);
        }

        glfw_forward(const glfw_forward &) = delete;
        glfw_forward(glfw_forward &&) = delete;
        glfw_forward &operator=(const glfw_forward &) = delete;
        glfw_forward &operator=(glfw_forward &&) = delete;

        // ────────────────────────────────────────────────────────
        // 回调：唯一的成员状态是 cursorPos_，其他一律转发
        // ────────────────────────────────────────────────────────
        static void onKeyboardEvent(void *self, event::keyboard_event e) noexcept
        {
            auto *impl = static_cast<glfw_forward *>(self);
            impl->keyboardNotify_.distribute(impl->cursorPos_,
                                             std::chrono::steady_clock::now(), e);
        }
        static void onMouseButtonEvent(void *self, event::mousebutton_event e) noexcept
        {
            auto *impl = static_cast<glfw_forward *>(self);
            impl->mousebuttonNotify_.distribute(impl->cursorPos_,
                                                std::chrono::steady_clock::now(), e);
        }
        static void onScrollEvent(void *self, event::scroll_event e) noexcept
        {
            auto *impl = static_cast<glfw_forward *>(self);
            impl->scrollNotify_.distribute(impl->cursorPos_,
                                           std::chrono::steady_clock::now(), e);
        }
        static void onCursorEnterEvent(void *self, event::cursor_enter_event e) noexcept
        {
            auto *impl = static_cast<glfw_forward *>(self);
            impl->cursorEnterNotify_.distribute(impl->cursorPos_,
                                                std::chrono::steady_clock::now(), e);
        }
        static void onCharEvent(void *self, event::char_event e) noexcept
        {
            auto *impl = static_cast<glfw_forward *>(self);
            impl->charNotify_.distribute(impl->cursorPos_,
                                         std::chrono::steady_clock::now(), e);
        }
        static void onDropEvent(void *self, event::drop_event e) noexcept
        {
            auto *impl = static_cast<glfw_forward *>(self);
            impl->dropNotify_.distribute(impl->cursorPos_,
                                         std::chrono::steady_clock::now(), e);
        }
        static void onFocusEvent(void *self, event::window_focus_event e) noexcept
        {
            auto *impl = static_cast<glfw_forward *>(self);
            impl->focusNotify_.distribute(impl->cursorPos_,
                                          std::chrono::steady_clock::now(), e);
        }
        static void onPosEvent(void *self, event::window_pos_event e) noexcept
        {
            auto *impl = static_cast<glfw_forward *>(self);
            impl->posNotify_.distribute(impl->cursorPos_,
                                        std::chrono::steady_clock::now(), e);
        }
        static void onSizeEvent(void *self, event::window_size_event e) noexcept
        {
            auto *impl = static_cast<glfw_forward *>(self);
            impl->sizeNotify_.distribute(impl->cursorPos_,
                                         std::chrono::steady_clock::now(), e);
        }
        static void onIconifyEvent(void *self, event::window_iconify_event e) noexcept
        {
            auto *impl = static_cast<glfw_forward *>(self);
            impl->iconifyNotify_.distribute(impl->cursorPos_,
                                            std::chrono::steady_clock::now(), e);
        }
        static void onMaximizeEvent(void *self, event::window_maximize_event e) noexcept
        {
            auto *impl = static_cast<glfw_forward *>(self);
            impl->maximizeNotify_.distribute(impl->cursorPos_,
                                             std::chrono::steady_clock::now(), e);
        }
        static void onFramebufferSizeEvent(void *self,
                                           event::framebuffer_size_event e) noexcept
        {
            auto *impl = static_cast<glfw_forward *>(self);
            impl->fbSizeNotify_.distribute(impl->cursorPos_,
                                           std::chrono::steady_clock::now(), e);
        }

        // ── 唯一不转发的事件：更新 cursorPos_（因为它是转发负载的一部分）
        static void onCursorPosEvent(void *self, event::position2d_event e) noexcept
        {
            static_cast<glfw_forward *>(self)->cursorPos_ = e;
        }

        // ────────────────────────────────────────────────────────
        // 对外 API：subscribe / unsubscribe
        // ────────────────────────────────────────────────────────
        template <typename callback_type>
        constexpr void subscribe(void *ctx, callback_type cb)
        {
            using event_type = callback_event_t<callback_type>;
            notifier_of<event_type>(*this).subscribe(ctx, cb);
        }

        template <typename callback_type>
        constexpr void unsubscribe(void *ctx, callback_type cb)
        {
            using event_type = callback_event_t<callback_type>;
            notifier_of<event_type>(*this).unsubscribe(ctx, cb);
        }

        // ────────────────────────────────────────────────────────
        // 查询 API
        // ────────────────────────────────────────────────────────
        [[nodiscard]] decltype(auto) cursorPos(this auto &&self) noexcept
        {
            return std::forward_like<decltype(self)>(self.cursorPos_);
        }

      private:
        // 从 callback 类型反推 event 类型。
        // gen_change_event_fn<Event> = void(*)(void*, position2d_event, time_point, Event)
        template <typename>
        struct callback_event; // 未定义

        template <typename Event>
        struct callback_event<event::gen_change_event_fn<Event>>
        {
            using type = Event;
        };

        template <typename callback_type>
        using callback_event_t =
            typename callback_event<std::decay_t<callback_type>>::type;

        // 类型 → notifier 的唯一映射点：新增事件类型时，只改这里
        template <typename Event>
        static constexpr auto &notifier_of(glfw_forward &self) noexcept // NOLINT
        {
            if constexpr (std::is_same_v<Event, event::keyboard_event>)
                return self.keyboardNotify_;
            else if constexpr (std::is_same_v<Event, event::mousebutton_event>)
                return self.mousebuttonNotify_;
            else if constexpr (std::is_same_v<Event, event::scroll_event>)
                return self.scrollNotify_;
            else if constexpr (std::is_same_v<Event, event::cursor_enter_event>)
                return self.cursorEnterNotify_;
            else if constexpr (std::is_same_v<Event, event::char_event>)
                return self.charNotify_;
            else if constexpr (std::is_same_v<Event, event::drop_event>)
                return self.dropNotify_;
            else if constexpr (std::is_same_v<Event, event::window_focus_event>)
                return self.focusNotify_;
            else if constexpr (std::is_same_v<Event, event::window_pos_event>)
                return self.posNotify_;
            else if constexpr (std::is_same_v<Event, event::window_size_event>)
                return self.sizeNotify_;
            else if constexpr (std::is_same_v<Event, event::window_iconify_event>)
                return self.iconifyNotify_;
            else if constexpr (std::is_same_v<Event, event::window_maximize_event>)
                return self.maximizeNotify_;
            else if constexpr (std::is_same_v<Event, event::framebuffer_size_event>)
                return self.fbSizeNotify_;
            else
                static_assert(false, "glfw_forward::notifier_of: bad event_type");
        }

        // ── 唯一的成员状态：转发负载需要它
        event::position2d_event cursorPos_{};

        // ── 转发中心代理：每个 event_type 一个，不做任何状态存储
        event_notifier<event::keyboard_event> keyboardNotify_;
        event_notifier<event::mousebutton_event> mousebuttonNotify_;
        event_notifier<event::scroll_event> scrollNotify_;
        event_notifier<event::cursor_enter_event> cursorEnterNotify_;
        event_notifier<event::char_event> charNotify_;
        event_notifier<event::drop_event> dropNotify_;
        event_notifier<event::window_focus_event> focusNotify_;
        event_notifier<event::window_pos_event> posNotify_;
        event_notifier<event::window_size_event> sizeNotify_;
        event_notifier<event::window_iconify_event> iconifyNotify_;
        event_notifier<event::window_maximize_event> maximizeNotify_;
        event_notifier<event::framebuffer_size_event> fbSizeNotify_;
    };

} // namespace mcs::vulkan::input