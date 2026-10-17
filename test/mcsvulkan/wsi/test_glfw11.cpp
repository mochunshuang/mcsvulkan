#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <optional>
#include <print>
#include <chrono>
#include <span>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "head.hpp"

// NOLINTBEGIN
using surface = mcs::vulkan::wsi::glfw::Window;

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr auto TITLE = "test_my_triangle";

using mcs::vulkan::input::glfw_input;
using mcs::vulkan::input::glfw_window_state;
using mcs::vulkan::input::glfw_forward;

using mcs::vulkan::event::keyboard_event;
using mcs::vulkan::event::scroll_event;
using mcs::vulkan::event::mousebutton_event;
using mcs::vulkan::event::position2d_event;
using mcs::vulkan::event::cursor_enter_event;

using mcs::vulkan::event::Key;
using mcs::vulkan::event::MouseButtons;
using mcs::vulkan::event::ModifierKey;
using mcs::vulkan::event::Action;

using mcs::vulkan::meta::make_aggregate;
using mcs::vulkan::meta::field;
using mcs::vulkan::meta::method;
using mcs::vulkan::meta::static_string;
using mcs::vulkan::meta::name_spec;
using mcs::vulkan::meta::class_utils;

using mcs::vulkan::runtime_type_v;
using mcs::vulkan::ring_buffer;

// ═══════════════════════════════════════════════════════════════════════
// 【原样保留】事件系统
// ═══════════════════════════════════════════════════════════════════════
struct invoke_member_help
{
    static consteval std::optional<std::meta::info> find_function(std::meta::info cls,
                                                                  static_string name)
    {
        return class_utils::find_member(
            cls, name,
            [](std::meta::info m) -> bool {
                return std::meta::is_function(m) || std::meta::is_function_template(m);
            },
            std::meta::access_context::current());
    }
    static consteval std::optional<std::meta::info> find_nonstatic_data_member(
        std::meta::info cls, static_string name)
    {
        return class_utils::find_member(
            cls, name,
            [](std::meta::info m) -> bool {
                return std::meta::is_nonstatic_data_member(m);
            },
            std::meta::access_context::current());
    }

    template <static_string fn_name, class T>
    static consteval std::meta::info resolve_callable()
    {
        if (auto f = find_function(^^T, fn_name); f.has_value())
            return *f;
        if (auto d = find_nonstatic_data_member(^^T, fn_name); d.has_value())
            return *d;
        return std::meta::info{};
    }

    template <static_string fn_name, class Obj, class... Args>
    static constexpr bool can_noexcept_invoke() noexcept
    {
        using T = std::remove_cvref_t<Obj>;

        if constexpr (requires(T &o, Args &&...as) {
                          {
                              o.template invoke<fn_name>(std::forward<Args>(as)...)
                          } noexcept;
                      })
            return true;

        constexpr auto callable = resolve_callable<fn_name, T>();
        if constexpr (callable == std::meta::info{})
            return false;
        else
            return requires(T &o, Args &&...as) {
                { o.[:callable:](std::forward<Args>(as)...) } noexcept;
            } || requires(T &o, Args &&...as) {
                { o.template[:callable:](std::forward<Args>(as)...) } noexcept;
            };
    }

    template <static_string fn_name, class Obj, class... Args>
        requires(can_noexcept_invoke<fn_name, std::remove_cvref_t<Obj>, Args...>())
    static constexpr decltype(auto) invoke_member(Obj &obj, Args &&...args) noexcept
    {
        using T = std::remove_cvref_t<Obj>;
        constexpr auto m = resolve_callable<fn_name, T>();

        if constexpr (requires { obj.[:m:](std::forward<Args>(args)...); })
            return obj.[:m:](std::forward<Args>(args)...);
        else if constexpr (requires { obj.template[:m:](std::forward<Args>(args)...); })
            return obj.template[:m:](std::forward<Args>(args)...);
        else
            static_assert(sizeof(Obj) == 0,
                          "invoke_member: member exists but not callable");
    }
};
template <typename... Extra>
struct proxy_class_invoke : invoke_member_help
{

    using invoke_type = void (*)(void *ptr, const void *type, void *data,
                                 Extra...) noexcept;
    constexpr void invoke(void *ptr, const void *type, void *data, Extra... e) noexcept
    {
        assert(invoke_ != nullptr);
        invoke_(ptr, type, data, e...);
    }

    proxy_class_invoke() = default;
    constexpr explicit proxy_class_invoke(invoke_type invoke) noexcept : invoke_{invoke}
    {
    }

    template <static_string name, typename Obj, typename... Ts>
    static constexpr void dispach_invoke(Obj *ptr, std::tuple<Ts...> *, const void *type,
                                         void *p, Extra... e) noexcept
    {
        using mcs::vulkan::runtime_type_v;
        auto invoke = [&](auto &d) noexcept {
            if constexpr (requires() { ptr->template invoke<name>(d, e...); })
                ptr->template invoke<name>(d, e...);
            else if constexpr (requires() { invoke_member<name>(*ptr, d, e...); })
                invoke_member<name>(*ptr, d, e...);
        };
        ((can_noexcept_invoke<name, Obj, Ts, Extra...> && type == runtime_type_v<Ts> &&
          (invoke(*static_cast<Ts *>(p)), true)) ||
         ...);
    }

    template <static_string callable_name, typename data_tuple, class T>
    static constexpr auto build() noexcept
    {
        using object_type = std::remove_cvref_t<T>;
        return proxy_class_invoke{invoke_type{
            [](void *ptr, const void *type, void *data, Extra... args) noexcept {
                auto *obj = static_cast<object_type *>(ptr);
                dispach_invoke<callable_name, object_type>(
                    obj, static_cast<data_tuple *>(nullptr), type, data, args...);
            }}};
    }

  private:
    invoke_type invoke_{};
};

struct glfw_input_translation
{
    using time_point_type = std::chrono::steady_clock::time_point;

    using keyboard_event = mcs::vulkan::event::keyboard_event;
    using mousebutton_event = mcs::vulkan::event::mousebutton_event;
    using scroll_event = mcs::vulkan::event::scroll_event;
    using position2d_event = mcs::vulkan::event::position2d_event;
    using cursor_enter_event = mcs::vulkan::event::cursor_enter_event;

    using char_event = mcs::vulkan::event::char_event;
    using drop_event = mcs::vulkan::event::drop_event;

    using window_focus_event = mcs::vulkan::event::window_focus_event;
    using window_pos_event = mcs::vulkan::event::window_pos_event;
    using window_size_event = mcs::vulkan::event::window_size_event;
    using window_iconify_event = mcs::vulkan::event::window_iconify_event;
    using window_maximize_event = mcs::vulkan::event::window_maximize_event;
    using framebuffer_size_event = mcs::vulkan::event::framebuffer_size_event;

    using data_tuple =
        std::tuple<keyboard_event, mousebutton_event, scroll_event, position2d_event,
                   cursor_enter_event, char_event, drop_event, window_focus_event,
                   window_pos_event, window_size_event, window_iconify_event,
                   window_maximize_event, framebuffer_size_event>;
    using proxy_class_type = proxy_class_invoke<>;

    struct receiver
    {
        constexpr receiver() noexcept = default;
        receiver(void *receiver, proxy_class_type fn) noexcept
            : receiver_{receiver}, fn_{fn}
        {
        }
        template <typename T>
        constexpr void invoke(T data) noexcept
        {
            if (receiver_ == nullptr)
                return;
            using mcs::vulkan::runtime_type_v;
            fn_.invoke(receiver_, runtime_type_v<T>, &data);
        }

      private:
        void *receiver_{};
        proxy_class_type fn_;
    };

    template <typename... event_type>
    struct SubscriptionForwarder
    {
        using callback_type = void (*)(void *self, event_type...);

        struct value_type
        {
            void *ctx;
            callback_type callback;
            bool valid;
        };
        SubscriptionForwarder() = default;
        SubscriptionForwarder(const SubscriptionForwarder &) = delete;
        SubscriptionForwarder(SubscriptionForwarder &&) = delete;
        SubscriptionForwarder &operator=(const SubscriptionForwarder &) = delete;
        SubscriptionForwarder &operator=(SubscriptionForwarder &&) = delete;
        ~SubscriptionForwarder() = default;

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
                {
                    item.valid = false;
                    hasInvalid_ = true;
                }
            for (auto &item : pendingAdd_)
                if (item.ctx == ctx && item.callback == cb)
                {
                    item.valid = false;
                    hasInvalid_ = true;
                }
        }

        constexpr void distribute(event_type... event)
        {
            const bool outer = !distributing_;
            if (outer)
                distributing_ = true;

            for (const auto &item : callbacks_)
                if (item.valid)
                    (item.callback)(item.ctx, event...);

            if (outer)
            {
                distributing_ = false;
                if (!pendingAdd_.empty())
                {
                    callbacks_.insert(callbacks_.end(),
                                      std::make_move_iterator(pendingAdd_.begin()),
                                      std::make_move_iterator(pendingAdd_.end()));
                    pendingAdd_.clear();
                }
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

    struct forward_object
    {
        SubscriptionForwarder<position2d_event> cursorPosNotify_;
        SubscriptionForwarder<keyboard_event> keyboardNotify_;
        SubscriptionForwarder<mousebutton_event> mousebuttonNotify_;
        SubscriptionForwarder<scroll_event> scrollNotify_;
        SubscriptionForwarder<cursor_enter_event> cursorEnterNotify_;
        SubscriptionForwarder<char_event> charNotify_;
        SubscriptionForwarder<drop_event> dropNotify_;
        SubscriptionForwarder<window_focus_event> focusNotify_;
        SubscriptionForwarder<window_pos_event> posNotify_;
        SubscriptionForwarder<window_size_event> sizeNotify_;
        SubscriptionForwarder<window_iconify_event> iconifyNotify_;
        SubscriptionForwarder<window_maximize_event> maximizeNotify_;
        SubscriptionForwarder<framebuffer_size_event> fbSizeNotify_;

        template <typename event_type>
        constexpr void subscribe(void *ctx,
                                 SubscriptionForwarder<event_type>::callback_type cb)
            requires(requires() { notifier_of<event_type>(*this).subscribe(ctx, cb); })
        {
            notifier_of<event_type>(*this).subscribe(ctx, cb);
        }

        template <typename event_type>
        constexpr void unsubscribe(void *ctx,
                                   SubscriptionForwarder<event_type>::callback_type cb)
            requires(requires() { notifier_of<event_type>(*this).unsubscribe(ctx, cb); })
        {
            notifier_of<event_type>(*this).unsubscribe(ctx, cb);
        }

        template <typename Event>
        static constexpr auto &notifier_of(auto &self) noexcept
        {
            if constexpr (std::is_same_v<Event, position2d_event>)
                return self.cursorPosNotify_;
            else if constexpr (std::is_same_v<Event, keyboard_event>)
                return self.keyboardNotify_;
            else if constexpr (std::is_same_v<Event, mousebutton_event>)
                return self.mousebuttonNotify_;
            else if constexpr (std::is_same_v<Event, scroll_event>)
                return self.scrollNotify_;
            else if constexpr (std::is_same_v<Event, cursor_enter_event>)
                return self.cursorEnterNotify_;
            else if constexpr (std::is_same_v<Event, char_event>)
                return self.charNotify_;
            else if constexpr (std::is_same_v<Event, drop_event>)
                return self.dropNotify_;
            else if constexpr (std::is_same_v<Event, window_focus_event>)
                return self.focusNotify_;
            else if constexpr (std::is_same_v<Event, window_pos_event>)
                return self.posNotify_;
            else if constexpr (std::is_same_v<Event, window_size_event>)
                return self.sizeNotify_;
            else if constexpr (std::is_same_v<Event, window_iconify_event>)
                return self.iconifyNotify_;
            else if constexpr (std::is_same_v<Event, window_maximize_event>)
                return self.maximizeNotify_;
            else if constexpr (std::is_same_v<Event, framebuffer_size_event>)
                return self.fbSizeNotify_;
            else
                static_assert(false, "notifier_of: bad event_type");
        }
    };

    template <typename event_type>
    constexpr static void forwarding(void *self, event_type e) noexcept
    {
        auto *impl = static_cast<glfw_input_translation *>(self);
        impl->receiver_.invoke(e);
    }

    template <typename event_type>
    struct event_dispatcher
    {
        constexpr static auto &instance() noexcept
        {
            namespace event = mcs::vulkan::event;
            if constexpr (std::is_same_v<keyboard_event, event_type>)
                return event::keyboard_event_dispatcher::instance();
            else if constexpr (std::is_same_v<mousebutton_event, event_type>)
                return event::mousebutton_event_dispatcher::instance();
            else if constexpr (std::is_same_v<scroll_event, event_type>)
                return event::scroll_event_dispatcher::instance();
            else if constexpr (std::is_same_v<position2d_event, event_type>)
                return event::position2d_event_dispatcher::instance();
            else if constexpr (std::is_same_v<cursor_enter_event, event_type>)
                return event::cursor_enter_event_dispatcher::instance();
            else if constexpr (std::is_same_v<char_event, event_type>)
                return event::char_event_dispatcher::instance();
            else if constexpr (std::is_same_v<drop_event, event_type>)
                return event::drop_event_dispatcher::instance();
            else if constexpr (std::is_same_v<window_focus_event, event_type>)
                return event::window_focus_event_dispatcher::instance();
            else if constexpr (std::is_same_v<window_pos_event, event_type>)
                return event::window_pos_event_dispatcher::instance();
            else if constexpr (std::is_same_v<window_size_event, event_type>)
                return event::window_size_event_dispatcher::instance();
            else if constexpr (std::is_same_v<window_iconify_event, event_type>)
                return event::window_iconify_event_dispatcher::instance();
            else if constexpr (std::is_same_v<window_maximize_event, event_type>)
                return event::window_maximize_event_dispatcher::instance();
            else if constexpr (std::is_same_v<framebuffer_size_event, event_type>)
                return event::framebuffer_size_event_dispatcher::instance();
            else
                static_assert(false, "not find dispatcher");
        }
    };

    template <typename... Events>
    static constexpr void subscribe_all(void *ptr)
    {
        (event_dispatcher<Events>::instance().subscribe(ptr, &forwarding<Events>), ...);
    }
    template <typename... Events>
    static constexpr void unsubscribe_all(void *ptr) noexcept
    {
        static_assert((noexcept(event_dispatcher<Events>::instance().unsubscribe(
                           ptr, &forwarding<Events>)) &&
                       ...),
                      "~class() need noexcept is true");
        (event_dispatcher<Events>::instance().unsubscribe(ptr, &forwarding<Events>), ...);
    }

    constexpr explicit glfw_input_translation()
    {
        subscribe_all<keyboard_event, mousebutton_event, scroll_event, position2d_event,
                      cursor_enter_event, char_event, drop_event, window_focus_event,
                      window_pos_event, window_size_event, window_iconify_event,
                      window_maximize_event, framebuffer_size_event>(this);
    }
    constexpr ~glfw_input_translation() noexcept
    {
        unsubscribe_all<keyboard_event, mousebutton_event, scroll_event, position2d_event,
                        cursor_enter_event, char_event, drop_event, window_focus_event,
                        window_pos_event, window_size_event, window_iconify_event,
                        window_maximize_event, framebuffer_size_event>(this);
    }
    glfw_input_translation(const glfw_input_translation &) = delete;
    glfw_input_translation(glfw_input_translation &&) = delete;
    glfw_input_translation &operator=(const glfw_input_translation &) = delete;
    glfw_input_translation &operator=(glfw_input_translation &&) = delete;

    template <typename Obj>
        requires(requires() {
            proxy_class_type::build<"input_event", data_tuple, Obj>();
        })
    constexpr void setReceiver(Obj &obj) noexcept
    {
        receiver_ = {&obj, proxy_class_type::build<"input_event", data_tuple, Obj>()};
    }

  private:
    receiver receiver_;
};

// 框架

enum class submit_flag : std::uint8_t
{
    lose,
    win,
};
enum class Disposition : std::uint8_t
{
    accepted, //表示识别器宣布获胜，认为自己已经识别出了对应手势
    rejected, //：表示识别器承认失败，主动退出竞争。
};

using declare_interface = void (*)(void *obj, submit_flag flag, void *ctx,
                                   static_string declare_fn) noexcept;
template <typename Arena, class Obj>
constexpr auto make_declare_adapter() noexcept -> declare_interface
{
    return [](void *p, submit_flag flag, void *ctx, static_string fn) noexcept {
        constexpr auto name = static_string{"declare"};

        auto *arena = static_cast<Arena *>(p);
        auto *obj = static_cast<Obj *>(ctx);
        if constexpr (requires() {
                          arena->template invoke<name>(submit_flag::win, obj, fn);
                      })
            arena->template invoke<name>(submit_flag::win, obj, std::move(fn));
        else
            invoke_member<name>(*arena, submit_flag::win, obj, fn);
    };
}

enum class resolve_result : std::uint8_t
{
    rejection,
    accept
};

struct submit_function
{
    using resolve_interface = void (*)(resolve_result result, void *obj,
                                       static_string fn) noexcept;

    template <class Obj>
    auto build() noexcept -> resolve_interface
    {
        return [](resolve_result result, void *ptr, static_string fn) noexcept {
            constexpr auto name = static_string{"resolve"};
            auto *obj = static_cast<Obj *>(ptr);
            if constexpr (requires() { obj->template invoke<name>(result, fn); })
                obj->template invoke<name>(result, fn);
            else
                invoke_member<name>(*obj, result, fn);
        };
    }
    submit_function() = default;

    void resolve(submit_flag win)
    {
        //
    }

  private:
    void *recognizer{};
    static_string fn{};
};

struct arena_members
{
};

#include <chrono>
using Clock = std::chrono::steady_clock;
using time_point = Clock::time_point;

#include <cmath>
struct Point2D
{
    double x = 0.0;
    double y = 0.0;

    // 到 x 轴的距离
    constexpr double distanceToXAxis() const noexcept
    {
        return std::abs(y);
    }

    // 到 y 轴的距离
    constexpr double distanceToYAxis() const noexcept
    {
        return std::abs(x);
    }

    // 到原点 (0,0) 的距离
    constexpr double distanceToOrigin() const noexcept
    {
        return std::hypot(x, y);
    }

    // 到另一个点的距离
    constexpr double distanceTo(const Point2D &other) const noexcept
    {
        return std::hypot(x - other.x, y - other.y);
    }
};

// clang-format off
namespace arena_event
{
    // ───────── 设备 ─────────
    enum class PointerDevice : std::uint8_t
    { Unknown, Touch, Mouse, Stylus, InvertedStylus, Trackpad };

    struct PointerId
    {
        constexpr static std::uint32_t Invalid = static_cast<std::uint32_t>(~0);
        std::uint32_t value{Invalid};
    };

    // ───────── 标识（原样） ─────────
    using Key = mcs::vulkan::event::Key;

    enum class MouseButton : std::uint8_t
    {
        eUNDEFINED = 0xFF,
        eMOUSE_BUTTON_1 = 0, eMOUSE_BUTTON_2 = 1, eMOUSE_BUTTON_3 = 2,
        eMOUSE_BUTTON_4 = 3, eMOUSE_BUTTON_5 = 4, eMOUSE_BUTTON_6 = 5,
        eMOUSE_BUTTON_7 = 6, eMOUSE_BUTTON_8 = 7,
        eMOUSE_BUTTON_LAST   = eMOUSE_BUTTON_8,
        eMOUSE_BUTTON_LEFT   = eMOUSE_BUTTON_1,
        eMOUSE_BUTTON_RIGHT  = eMOUSE_BUTTON_2,
        eMOUSE_BUTTON_MIDDLE = eMOUSE_BUTTON_3,
        eSIZE = static_cast<std::uint8_t>(eMOUSE_BUTTON_LAST) + 1
    };

    // ───────── 三组严格分离的枚举 ─────────
    enum class HeldState : std::uint8_t { Up, Down };
    enum class Transition : std::uint8_t { Press, Release };
    enum class PointerState : std::uint8_t { Outside, Hover, Down, DownMove };

    // ═══════════ ① event_context：只装"现在仍成立"的东西 ═══════════
    struct event_context
    {
        std::array<HeldState, std::to_underlying(Key::eSIZE)> keys{};
        std::array<HeldState, static_cast<std::size_t>(MouseButton::eSIZE)> buttons{};
        ModifierKey modifier{};

        PointerState  pointer_state  = PointerState::Outside;
        PointerDevice pointer_device = PointerDevice::Unknown;
        double        pointer_x      = 0.0;
        double        pointer_y      = 0.0;
        bool          pointer_position_valid = false;   // ★ 加

        [[nodiscard]] decltype(auto) get_keyboard_event(this auto &&self, Key k) noexcept
        { return std::forward_like<decltype(self)>(self.keys[std::to_underlying(k)]); }

        [[nodiscard]] decltype(auto) get_mousebutton_event(this auto &&self, MouseButton b) noexcept
        { return std::forward_like<decltype(self)>(self.buttons[std::to_underlying(b)]); }

        [[nodiscard]] bool any_button_down() const noexcept
        {
            for (auto s : buttons) if (s == HeldState::Down) return true;
            return false;
        }
    };

    // ═══════════ ② change_event：这一帧变成了什么 ═══════════
    enum class ChangeType : std::uint8_t
    { None, Key, PointerButton, PointerDownMove,PointerUpMove, Scroll, CursorEnter, Char };

    struct key_change
    {
        Key        key{};
        Transition transition{};
    };
    struct button_change
    {
        MouseButton button{};
        Transition  transition{};
    };
    struct move_change
    {
        double x = 0.0, y = 0.0;   // 新位置
        double dx = 0.0, dy = 0.0; // 相对 context.pointer 的位移
    };

    struct change_event
    {
        ChangeType  change     = ChangeType::None;
        time_point  time_point{};
        ModifierKey modifier{};       // 事件发生"之后"的修饰键

        key_change            key{};
        button_change         button{};
        move_change           move{};
        mcs::vulkan::event::scroll_event       scroll{};
        mcs::vulkan::event::cursor_enter_event cursor_enter{};
        unsigned              codepoint = 0;
    };

    // ═══════════ ③ input_frame：识别器唯一入参 ═══════════
    struct input_frame
    {
        event_context context;  // 变化前的快照
        change_event  change;   // 本帧变化
    };

    // ═══════════ 转换 1：平台事件 → change_event（只读 ctx） ═══════════
    template <class PlatformButton>
    constexpr MouseButton map_button(PlatformButton b) noexcept
    {
        using P = PlatformButton;
        switch (b)
        {
        case P::eMOUSE_BUTTON_LEFT:   return MouseButton::eMOUSE_BUTTON_LEFT;
        case P::eMOUSE_BUTTON_RIGHT:  return MouseButton::eMOUSE_BUTTON_RIGHT;
        case P::eMOUSE_BUTTON_MIDDLE: return MouseButton::eMOUSE_BUTTON_MIDDLE;
        case P::eMOUSE_BUTTON_4:      return MouseButton::eMOUSE_BUTTON_4;
        case P::eMOUSE_BUTTON_5:      return MouseButton::eMOUSE_BUTTON_5;
        case P::eMOUSE_BUTTON_6:      return MouseButton::eMOUSE_BUTTON_6;
        case P::eMOUSE_BUTTON_7:      return MouseButton::eMOUSE_BUTTON_7;
        case P::eMOUSE_BUTTON_8:      return MouseButton::eMOUSE_BUTTON_8;    // ← 加
        case P::eMOUSE_BUTTON_LAST:   return MouseButton::eMOUSE_BUTTON_LAST; // 值不同，不冲突
        default:                      return MouseButton::eUNDEFINED;
        }
    }

    template <class Event>
    inline change_event build_change(const event_context &ctx, const Event &e) noexcept
    {
        using T = std::remove_cvref_t<Event>;
        change_event ch{};
        ch.time_point = Clock::now();
        ch.modifier   = ctx.modifier;

        if constexpr (std::is_same_v<T, mcs::vulkan::event::keyboard_event>)
        {
            ch.change          = ChangeType::Key;
            ch.key.key         = e.key;
            ch.key.transition  = (e.press() || e.repeat()) ? Transition::Press : Transition::Release;
            ch.modifier        = e.modifier_key;   // ← 原来是 e.mods
        }
        else if constexpr (std::is_same_v<T, mcs::vulkan::event::mousebutton_event>)
        {
            ch.change             = ChangeType::PointerButton;
            ch.button.button      = map_button(e.button);
            ch.button.transition  = (e.press() || e.repeat()) ? Transition::Press : Transition::Release;
            ch.modifier           = e.modifier_key;
        }
        else if constexpr (std::is_same_v<T, mcs::vulkan::event::position2d_event>)
        {
            ch.change = ctx.any_button_down() ? ChangeType::PointerDownMove
                                            : ChangeType::PointerUpMove;
            ch.move.x = e.xpos;
            ch.move.y = e.ypos;

            if (ctx.pointer_position_valid)            // ★ 只有有效才算增量
            {
                ch.move.dx = e.xpos - ctx.pointer_x;
                ch.move.dy = e.ypos - ctx.pointer_y;
            }
            // 无效时 dx/dy 保持 0，识别器看到 dx=0 就知道"这一帧没有真实增量"
        }
        else if constexpr (std::is_same_v<T, mcs::vulkan::event::scroll_event>)
        {
            ch.change = ChangeType::Scroll;        ch.scroll = e;
        }
        else if constexpr (std::is_same_v<T, mcs::vulkan::event::cursor_enter_event>)
        {
            ch.change = ChangeType::CursorEnter;   ch.cursor_enter = e;
        }
        else if constexpr (std::is_same_v<T, mcs::vulkan::event::char_event>)
        {
            ch.change = ChangeType::Char;          ch.codepoint = e.codepoint;
        }
        return ch;
    }

    // ═══════════ 转换 2：change_event → 写回 ctx（唯一迁移点） ═══════════
    inline void apply_change(event_context &ctx, const change_event &ch) noexcept
    {
        switch (ch.change)
        {
        case ChangeType::Key:
            ctx.keys[std::to_underlying(ch.key.key)] =
                (ch.key.transition == Transition::Press) ? HeldState::Down : HeldState::Up;
            ctx.modifier = ch.modifier;
            break;

        case ChangeType::PointerButton:
            ctx.buttons[std::to_underlying(ch.button.button)] =
                (ch.button.transition == Transition::Press) ? HeldState::Down : HeldState::Up;
            // 多按钮场景下，第二个按钮按下 / 任一按钮抬起时，不能把 DownMove 打回 Down，否则"移动过"的历史丢失
            if (ch.button.transition == Transition::Press)
            {
                ctx.pointer_device = PointerDevice::Mouse;
                if (ctx.pointer_state != PointerState::DownMove)
                    ctx.pointer_state = PointerState::Down;
            }
            else
            {
                if (ctx.any_button_down())
                {
                    if (ctx.pointer_state != PointerState::DownMove)
                        ctx.pointer_state = PointerState::Down;
                }
                else
                {
                    ctx.pointer_state = (ctx.pointer_state == PointerState::Outside)
                                        ? PointerState::Outside
                                        : PointerState::Hover;
                }
            }
            break;

        case ChangeType::PointerDownMove:
        case ChangeType::PointerUpMove:
            ctx.pointer_x = ch.move.x;
            ctx.pointer_y = ch.move.y;
            ctx.pointer_position_valid = true;         // ★
            ctx.pointer_state = ctx.any_button_down()
                ? PointerState::DownMove
                : PointerState::Hover;
            if (ctx.pointer_device == PointerDevice::Unknown)
                ctx.pointer_device = PointerDevice::Mouse;
            break;

        /*
        Outside ──Enter(true)──> Hover ──Press──> Down ──Move──> DownMove ──Release(全部)──> Hover
                                        ▲                                  │
                                        └──Enter(true)──────────────────────┘
                                            （按着从窗外回来 → Down，不是 DownMove）
        */
        case ChangeType::CursorEnter:
            if (ch.cursor_enter.value)
            {
                ctx.pointer_device = PointerDevice::Mouse;
                ctx.pointer_state  = ctx.any_button_down() ? PointerState::Down : PointerState::Hover; // ← DownMove → Down
                // ★ 不置 true：位置仍要靠第一次 move 更新
            }
            else
            {
                ctx.pointer_device         = PointerDevice::Unknown;
                ctx.pointer_state          = PointerState::Outside;
                ctx.pointer_position_valid = false;    // ★ 离开窗口位置失效
            }
            break;

        case ChangeType::Scroll:
            // NOTE: ctx 不存储 Scroll 的值
            break; 
        case ChangeType::Char: // NOTE: ctx 不存储 Char 的值
        case ChangeType::None:
            break;
        }
    }
}
// clang-format on

using join_arena_interface = void (*)(void *obj, submit_flag flag, void *ctx,
                                      static_string declare_fn) noexcept;

struct member_join_key
{
    void *member;
};
template <typename... Ts>
struct publish_types
{
    // 在当前 erased_ref::build(T&&) 的设计下，publish_types 必须是引用类型
    static_assert((std::is_reference_v<Ts> && ...), "publish_types: 每项必须是引用类型");
    template <typename T>
    static constexpr bool has_type()
    {
        return (std::is_same_v<T, Ts> || ...);
    };
};
template <class Self, class Data>
consteval bool requires_type_from_self()
{
    using FT = std::remove_pointer_t<decltype(std::declval<Self>().publish_types())>;
    return FT::template has_type<Data>();
}

// ═══════════════════════════════════════════════════════════════════════
// 擦除句柄：void* + tag
//   data 指向被引用对象本身，tag = type_of<T&&>（折叠后已编码 cv + 值类别）
// ═══════════════════════════════════════════════════════════════════════
/*
层	    载体	    负责什么	    通用性
数据层	void*	    搬地址	        完全通用，任意类型
类型层	tag	编码    cv + 值类别	    只能编码引用类型
路由层	白名单	    和 tag 空间同构	必须列引用
*/
struct erased_ref
{
    void *data = nullptr;
    const void *tag = nullptr;
    explicit operator bool() const noexcept
    {
        return tag != nullptr;
    }
    template <class T>
    [[nodiscard]] T get() const noexcept
    {
        using U = std::remove_reference_t<T>;
        return static_cast<T>(*static_cast<U *>(data));
    }

    template <typename T>
    [[nodiscard]] static constexpr erased_ref build(T &&t) noexcept
    {
        using mcs::vulkan::runtime_type_v;
        return {const_cast<void *>(static_cast<const void *>(std::addressof(t))),
                runtime_type_v<T &&>};
    }
};

struct member_registry
{
    using publish_fn = void (*)(void *member, erased_ref ref) noexcept;

    struct entry
    {
        void *member;
        publish_fn publish;
        bool valid;
    };

    template <class Arena, class Member>
    constexpr void add(Member &member) noexcept
    {
        auto entry = make_entry<Arena>(member);
        if (distributing_)
        {
            for (auto &item : pendingAdd_)
                if (item.member == entry.member)
                {
                    item = entry;
                    return;
                }
            pendingAdd_.push_back(entry);
            return;
        }
        for (auto &item : callbacks_)
            if (item.member == entry.member)
            {
                item = entry;
                return;
            }
        callbacks_.push_back(entry);
    }

    constexpr void detach(void *member) noexcept
    {
        for (auto &item : callbacks_)
            if (item.member == member)
            {
                item.valid = false;
                hasInvalid_ = true;
            }
        for (auto &item : pendingAdd_)
            if (item.member == member)
            {
                item.valid = false;
                hasInvalid_ = true;
            }
    }

    constexpr void publish_all(erased_ref ref) noexcept
    {
        const bool outer = !distributing_;
        if (outer)
            distributing_ = true;

        for (const auto &item : callbacks_)
            if (item.valid)
                item.publish(item.member, ref); // ← ref 原样传递

        if (outer)
        {
            distributing_ = false;
            for (auto &item : pendingAdd_)
                if (item.valid)
                    callbacks_.push_back(std::move(item));
            pendingAdd_.clear();
            if (hasInvalid_)
            {
                std::erase_if(callbacks_,
                              [](const entry &e) noexcept { return !e.valid; });
                hasInvalid_ = false;
            }
        }
    }

  private:
    template <class Arena, class Member>
    constexpr static entry make_entry(Member &member) noexcept
    {
        return entry{
            &member, publish_fn{[](void *m, erased_ref ref) noexcept {
                auto *obj = static_cast<Member *>(m);
                [&]<class... Ts>(publish_types<Ts...> *) noexcept {
                    constexpr auto name = static_string{"publish"};
                    using mcs::vulkan::runtime_type_v;
                    ((requires() { obj->template invoke<name>(std::declval<Ts>()); } &&
                      ref.tag == runtime_type_v<Ts> &&
                      (obj->template invoke<name>(ref.template get<Ts>()), true)) ||
                     ...);
                }(static_cast<decltype(std::declval<Arena>().publish_types())>(nullptr));
            }},
            true};
    }

    std::vector<entry> callbacks_;
    std::vector<entry> pendingAdd_;
    bool distributing_ = false;
    bool hasInvalid_ = false;
};

struct member_detach_key
{
    using detach_fn = void (*)(void *, void *) noexcept;
    template <class Arena>
    static constexpr member_detach_key build() noexcept
    {
        return member_detach_key{detach_fn{[](void *ptr, void *member) noexcept {
            auto *arena = static_cast<Arena *>(ptr);
            arena->template invoke<"detach">(member); // 直接传 void*
        }}};
    }
    constexpr void detach(void *arena, void *member) noexcept
    {
        if (fn_ == nullptr)
            return;
        fn_(arena, member);
    }
    member_detach_key() = default;
    constexpr explicit member_detach_key(detach_fn fn) noexcept : fn_{fn} {}

  private:
    detach_fn fn_{};
};

enum class decision
{
    pending,
    accepted,
    rejected,
    superseded
};

// ═══════════════════════════════════════════════════════════════════════
//  pointer_state 状态机测试（纯逻辑，不启动 GLFW）
// ═══════════════════════════════════════════════════════════════════════
//  记号：E+ = CursorEnter(true)   E- = CursorEnter(false)
//        Lp/Lr = 左键 press/release   Rp/Rr = 右键 press/release
//        M  = position2d_event       B_any = any_button_down()
//
//   ┌──────────┐  E+ / !B_any          ┌─────────┐
//   │ Outside  │ ───────────────────>  │  Hover  │
//   │          │ <───────────────────  │         │
//   └────┬─────┘  E-  (任意状态)        └────┬────┘
//        │                                    │ Lp
//        │ E+ / B_any                         ▼
//        │ （按着从窗外回来）             ┌─────────┐
//        └────────────────────────────> │  Down   │
//                                       └────┬────┘
//                                            │ M
//                                            ▼
//                                        ┌──────────┐
//                                        │ DownMove │ ── M ──> DownMove (stay)
//                                        └────┬─────┘
//                                             │ Lr / !B_any
//                                             ▼
//                                        ┌─────────┐
//                                        │  Hover  │
//                                        └─────────┘
//  保护边（★ 多按钮不回退）：
//    DownMove -- Bp(其他) --> DownMove
//    DownMove -- Br(其他仍按) --> DownMove
//
//  不变量：
//    (1) Outside    ⇒ position_valid == false
//    (2) DownMove   ⇒ position_valid == true
//    (3) Down       ∧ B_any
//    (4) E- 从任意  → Outside ∧ position_valid == false
// ═══════════════════════════════════════════════════════════════════════
void test_pointer_state_machine()
{
    namespace ae = arena_event;
    using mcs::vulkan::event::Action;
    using mcs::vulkan::event::MouseButtons;

    auto inject = [](ae::event_context &ctx, auto e) noexcept {
        auto ch = ae::build_change(ctx, e);
        ae::apply_change(ctx, ch);
    };

    constexpr cursor_enter_event E_on{true};
    constexpr cursor_enter_event E_off{false};
    constexpr mousebutton_event Lp{MouseButtons::eMOUSE_BUTTON_LEFT, Action::ePRESS};
    constexpr mousebutton_event Lr{MouseButtons::eMOUSE_BUTTON_LEFT, Action::eRELEASE};
    constexpr mousebutton_event Rp{MouseButtons::eMOUSE_BUTTON_RIGHT, Action::ePRESS};
    constexpr mousebutton_event Rr{MouseButtons::eMOUSE_BUTTON_RIGHT, Action::eRELEASE};

    // ─── 初始：Outside ─────────────────────────────────────────────
    {
        ae::event_context ctx;
        assert(ctx.pointer_state == ae::PointerState::Outside);
        assert(ctx.pointer_position_valid == false); // 不变量 (1)
        assert(ctx.pointer_device == ae::PointerDevice::Unknown);
    }

    // ─── Outside --E+ / !B_any--> Hover ────────────────────────────
    {
        ae::event_context ctx;
        inject(ctx, E_on);
        assert(ctx.pointer_state == ae::PointerState::Hover);
        assert(ctx.pointer_device == ae::PointerDevice::Mouse);
        assert(ctx.pointer_position_valid == false); // 等首次 move
    }

    // ─── Hover --Lp--> Down ────────────────────────────────────────
    {
        ae::event_context ctx;
        inject(ctx, E_on);
        inject(ctx, Lp);
        assert(ctx.pointer_state == ae::PointerState::Down);
        assert(ctx.buttons[0] == ae::HeldState::Down); // 不变量 (3)
    }

    // ─── Down --M--> DownMove ──────────────────────────────────────
    {
        ae::event_context ctx;
        inject(ctx, E_on);
        inject(ctx, Lp);
        inject(ctx, position2d_event{100.0, 200.0});
        assert(ctx.pointer_state == ae::PointerState::DownMove);
        assert(ctx.pointer_position_valid == true); // 不变量 (2)
        assert(ctx.pointer_x == 100.0 && ctx.pointer_y == 200.0);
    }

    // ─── DownMove --M--> DownMove (stay) ───────────────────────────
    {
        ae::event_context ctx;
        inject(ctx, E_on);
        inject(ctx, Lp);
        inject(ctx, position2d_event{100.0, 200.0});
        inject(ctx, position2d_event{150.0, 260.0});
        assert(ctx.pointer_state == ae::PointerState::DownMove);
        assert(ctx.pointer_x == 150.0 && ctx.pointer_y == 260.0);
    }

    // ─── DownMove --Lr / !B_any--> Hover ───────────────────────────
    {
        ae::event_context ctx;
        inject(ctx, E_on);
        inject(ctx, Lp);
        inject(ctx, position2d_event{100.0, 200.0});
        inject(ctx, Lr);
        assert(ctx.pointer_state == ae::PointerState::Hover);
        assert(ctx.buttons[0] == ae::HeldState::Up);
    }

    // ─── Down --Rp(其他按钮)--> Down (stay) ────────────────────────
    {
        ae::event_context ctx;
        inject(ctx, E_on);
        inject(ctx, Lp);
        inject(ctx, Rp);
        assert(ctx.pointer_state == ae::PointerState::Down);
        assert(ctx.buttons[0] == ae::HeldState::Down);
        assert(ctx.buttons[1] == ae::HeldState::Down);
    }

    // ─── Down --Rr(其他仍按)--> Down (stay) ────────────────────────
    {
        ae::event_context ctx;
        inject(ctx, E_on);
        inject(ctx, Lp);
        inject(ctx, Rp);
        inject(ctx, Rr);
        assert(ctx.pointer_state == ae::PointerState::Down);
        assert(ctx.buttons[0] == ae::HeldState::Down);
    }

    // ─── DownMove --Rp(其他)--> DownMove (★ 保护) ──────────────────
    {
        ae::event_context ctx;
        inject(ctx, E_on);
        inject(ctx, Lp);
        inject(ctx, position2d_event{10.0, 10.0});
        inject(ctx, Rp);
        assert(ctx.pointer_state == ae::PointerState::DownMove);
    }

    // ─── DownMove --Lr(其他仍按)--> DownMove (★ 保护) ──────────────
    {
        ae::event_context ctx;
        inject(ctx, E_on);
        inject(ctx, Lp);
        inject(ctx, Rp);
        inject(ctx, position2d_event{10.0, 10.0});
        inject(ctx, Lr);
        assert(ctx.pointer_state == ae::PointerState::DownMove);
        assert(ctx.buttons[0] == ae::HeldState::Up);
        assert(ctx.buttons[1] == ae::HeldState::Down);
    }

    // ─── DownMove --E- --> Outside (不变量 (4)) ────────────────────
    {
        ae::event_context ctx;
        inject(ctx, E_on);
        inject(ctx, Lp);
        inject(ctx, position2d_event{10.0, 10.0});
        inject(ctx, E_off);
        assert(ctx.pointer_state == ae::PointerState::Outside);
        assert(ctx.pointer_position_valid == false);
        assert(ctx.pointer_device == ae::PointerDevice::Unknown);
        // 平台事实：窗口外松开不通知，buttons 保持
        assert(ctx.buttons[0] == ae::HeldState::Down);
    }

    // ─── Outside --E+ / B_any--> Down (不是 DownMove) ──────────────
    {
        ae::event_context ctx;
        inject(ctx, E_on);
        inject(ctx, Lp);
        inject(ctx, position2d_event{10.0, 10.0});
        inject(ctx, E_off); // 拖出去
        inject(ctx, E_on);  // 按着回来
        assert(ctx.pointer_state == ae::PointerState::Down);
        assert(ctx.pointer_position_valid == false); // 等首次 move
    }

    // ─── Down --E- --> Outside (未移动就离开) ─────────────────────
    {
        ae::event_context ctx;
        inject(ctx, E_on);
        inject(ctx, Lp); // Hover → Down，没 move
        assert(ctx.pointer_state == ae::PointerState::Down);
        inject(ctx, E_off);
        assert(ctx.pointer_state == ae::PointerState::Outside);
        assert(ctx.pointer_position_valid == false);
        assert(ctx.buttons[0] == ae::HeldState::Down); // 平台限制：仍持有
    }
    std::println("test_pointer_state_machine done");
}
// ═══════════════════════════════════════════════════════════════════════
//  position_valid 语义 + 键盘 repeat + None 事件（纯逻辑）
// ═══════════════════════════════════════════════════════════════════════
//  规则：
//    position_valid == false ⇒ build_change 产出 dx = dy = 0（无假位移）
//    position_valid == true  ⇒ dx = e.xpos - ctx.pointer_x（真实帧间增量）
//  路径：
//    首次 move（进入窗口后第一次）  → dx = 0，apply 后 valid = true
//    E-                             → valid = false
//    E+                             → valid 不变（等 move 刷新）
//    键盘 eREPEAT                   → keys[] 保持 Down（不掉键）
//    未映射事件（window_size 等）    → ChangeType::None
// ═══════════════════════════════════════════════════════════════════════
void test_position_valid_and_edge_cases()
{
    namespace ae = arena_event;
    using mcs::vulkan::event::Action;
    using mcs::vulkan::event::Key;

    auto inject = [](ae::event_context &ctx, auto e) noexcept {
        auto ch = ae::build_change(ctx, e);
        ae::apply_change(ctx, ch);
    };

    // ─── 首帧 move 无假位移 ─────────────────────────────────────────
    {
        ae::event_context ctx;
        inject(ctx, cursor_enter_event{true});
        auto ch = ae::build_change(ctx, position2d_event{500.0, 600.0});
        assert(ch.move.dx == 0.0 && ch.move.dy == 0.0);
        assert(ch.change == ae::ChangeType::PointerUpMove);
        ae::apply_change(ctx, ch);
        assert(ctx.pointer_position_valid == true);
    }

    // ─── 第二帧起为真实增量 ─────────────────────────────────────────
    {
        ae::event_context ctx;
        inject(ctx, cursor_enter_event{true});
        inject(ctx, position2d_event{500.0, 600.0});
        auto ch = ae::build_change(ctx, position2d_event{510.0, 620.0});
        assert(ch.move.dx == 10.0 && ch.move.dy == 20.0);
    }

    // ─── 离开窗口后，回来首帧仍无假位移 ─────────────────────────────
    {
        ae::event_context ctx;
        inject(ctx, cursor_enter_event{true});
        inject(ctx, position2d_event{500.0, 600.0});
        inject(ctx, cursor_enter_event{false});
        assert(ctx.pointer_position_valid == false);
        // 旧坐标保留但不可信，识别器要靠 position_valid 判断
        assert(ctx.pointer_x == 500.0 && ctx.pointer_y == 600.0);

        inject(ctx, cursor_enter_event{true});
        assert(ctx.pointer_position_valid == false);
        auto ch = ae::build_change(ctx, position2d_event{800.0, 100.0});
        assert(ch.move.dx == 0.0 && ch.move.dy == 0.0);
    }

    // ─── 键盘 eREPEAT 保持 Down，不掉键 ─────────────────────────────
    {
        ae::event_context ctx;
        inject(ctx, keyboard_event{Key::eA, Action::ePRESS});
        assert(ctx.keys[std::to_underlying(Key::eA)] == ae::HeldState::Down);
        inject(ctx, keyboard_event{Key::eA, Action::eREPEAT});
        assert(ctx.keys[std::to_underlying(Key::eA)] == ae::HeldState::Down);
        inject(ctx, keyboard_event{Key::eA, Action::eRELEASE});
        assert(ctx.keys[std::to_underlying(Key::eA)] == ae::HeldState::Up);
    }

    // ─── 未映射平台事件 → ChangeType::None ─────────────────────────
    {
        ae::event_context ctx;
        auto ch = ae::build_change(ctx, mcs::vulkan::event::window_size_event{100, 100});
        assert(ch.change == ae::ChangeType::None);
    }
    std::println("test_position_valid_and_edge_cases done");
}

// ═══════════════════════════════════════════════════════════════════════
// main
// ═══════════════════════════════════════════════════════════════════════
int main()
try
{
    test_pointer_state_machine();
    test_position_valid_and_edge_cases();

    {
        constexpr Point2D p{3.0, 4.0};
        constexpr Point2D q{1.0, 2.0};
        static_assert(p.distanceToXAxis() == 4);
        static_assert(p.distanceToYAxis() == 3);
        static_assert(p.distanceToOrigin() == 5);
        static_assert(p.distanceTo(q) == std::sqrt(8));
    }

    surface window{};
    window.setup({.width = WIDTH, .height = HEIGHT}, TITLE);

    using arena_data_tuple =
        publish_types<const arena_event::input_frame &, arena_event::input_frame &&>;
    auto arena = make_aggregate<"arena">(
        field<"mermbers">(member_registry{}),                 //
        field<"event_context">(arena_event::event_context{}), //
        method<"publish_types">(
            []() noexcept { return static_cast<arena_data_tuple *>(nullptr); }),
        method<"publish">([](auto &self, auto &&data) noexcept {
            self.mermbers.publish_all(
                erased_ref::build(std::forward<decltype(data)>(data)));
        }),
        method<"join">([]<class Self, class Member>(Self &self, Member &m) noexcept {
            self.mermbers.template add<Self>(m);
        }),
        method<"detach">(
            [](auto &self, void *member) noexcept { self.mermbers.detach(member); }),
        method<"input_event">([]<typename event_type>(auto &self, event_type e) noexcept {
            std::println("input_event: {}", e);
            const auto ch = arena_event::build_change(self.event_context, e);
            if (ch.change == arena_event::ChangeType::None)
                return;
            self.template invoke<"publish">(
                arena_event::input_frame{self.event_context, ch}); // prvalue ✓
            arena_event::apply_change(self.event_context, ch);     // 用局部 ch ✓
        }),
        method<"declare">([]<typename Self>(Self &self, submit_flag result, auto *ctx,
                                            static_string fn) noexcept {
            switch (result)
            {
            case submit_flag::win: /* 赢 */
            {
                if constexpr (requires() { ctx->pending_flag; })
                {
                    constexpr auto name = static_string{"resolve_pending"};
                    if constexpr (requires() {
                                      {
                                          ctx->template invoke<name>(result, fn)
                                      } noexcept -> std::same_as<bool>;
                                  })
                    {
                        bool pending = ctx->template invoke<name>(result, fn);
                        if (not pending)
                        {
                        }
                    }
                    else
                    {
                        bool pending = invoke_member<name>(*ctx, result, fn);
                        if (not pending)
                        {
                        }
                    }
                }
            }
            break;
            case submit_flag::lose: /* 输 */
                break;
            }
        }),
        method<"return_value">([](auto *sndr, auto e) noexcept { return e + 1; }),
        method<"on_tick">([](time_point point) noexcept {
            // do something
        }));

    auto arbitrator = make_aggregate<"arbitrator">(
        method<"ruling">([]<typename event_type>(auto &self, event_type e) noexcept {

        }));

    // NOTE: 结果走 arena 的唯一出口? 没必要. 直接 resume recognizer 关联的函数即可
    constexpr auto null_submit_ptr = static_cast<declare_interface>(nullptr);
    auto recognizer = make_aggregate<"recognizer">(
        field<"arena_ptr">(static_cast<void *>(nullptr)),
        field<"submit_click">(null_submit_ptr),
        field<"submit_double_click">(null_submit_ptr),
        field<"detach_key">(member_detach_key{}),
        method<"publish">([]<typename Self, typename Frame>(Self &self,
                                                            Frame &&frame) noexcept {
            const auto &[ctx, ch] = frame; // ctx 查历史，ch 查变化
            switch (ch.change)
            {
            case arena_event::ChangeType::PointerButton:
                if (ch.button.button == arena_event::MouseButton::eMOUSE_BUTTON_LEFT &&
                    ch.button.transition == arena_event::Transition::Release)
                    self.template invoke<"click">(frame);
                break;
            case arena_event::ChangeType::PointerDownMove:
                // 一定是"按着按钮在移动"→ drag
                self.template invoke<"drag">(frame);
                break;
            case arena_event::ChangeType::PointerUpMove:
                // 一定是"悬停移动"→ hover 高亮 / tooltip / 光标形状
                self.template invoke<"hover">(frame);
                break;
            default:
                break;
            }
        }),
        method<"bind_arena">(
            []<typename Self, typename Arena>(Self &self, Arena &arena) noexcept {
                self.arena_ptr = &arena;
                self.submit_click = make_declare_adapter<Arena, Self>();
                self.submit_double_click = make_declare_adapter<Arena, Self>();
                // NOTE: 如何关联?
                arena.template invoke<"join">(self); // ← 只传 self

                self.detach_key = member_detach_key::build<Arena>();
            }),
        method<"click">([](auto &self, auto e) noexcept {
            // self.submit_click(self.arena_ptr, submit_flag::win, &self, "click");
            std::println("recognizer click...");
        }),
        method<"double_click">([](auto &self, auto e) noexcept {
            // self.submit_double_click(self.arena_ptr, submit_flag::win, &self,
            //                          "double_click");
            std::println("recognizer double_click...");
        }),
        method<"drag">(
            [](auto &self, auto e) noexcept { std::println("recognizer drag..."); }),
        method<"hover">(
            [](auto &self, auto e) noexcept { std::println("recognizer hover..."); }),
        method<"resolve">([](auto &self, auto e) noexcept {

        }),
        method<"resolve_pending">([](auto &self, auto e) noexcept {
            self.submit_double_click(self.arena_ptr, submit_flag::win, &self,
                                     "double_click");
        }),
        method<"detach_arena">(
            [](auto &self) noexcept { self.detach_key.detach(self.arena_ptr, &self); }));

    recognizer.bind_arena(recognizer, arena);

    glfw_input_translation input;
    input.setReceiver(arena);
    while (window.shouldClose() == 0)
    {
        surface::pollEvents();
    }

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}
// NOLINTEND

/*
arbitrator 是候选事件的冲突裁决器。
win/lose 是它的输出，不是识别器的输入。
多识别器可能竞争时，必须统一走仲裁；无竞争时，可以编译期消除。
算法骨架由编译期规则决定，具体胜负由运行时输入序列和状态决定。
仲裁器只给 decision，不负责 emit；真正 emit 用户事件的是识别器。

原始输入事件
   │
   ▼
识别器 recognizer
   │  识别出候选事件：Click / DoubleClick / Drag / LongPress ...
   │  但它不直接 emit
   ▼
候选提交 claim / propose
   │
   ▼
仲裁器 arbitrator
   │  根据编译期规则 + 运行时状态裁决：
   │  谁 accepted，谁 rejected，谁 pending，谁 superseded
   ▼
decision: accepted / rejected / pending / superseded
   │
   ▼
识别器收到 decision
   │  accepted 才真正 emit 用户事件
   │  rejected 则取消/回滚
*/