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

namespace arena_event
{
    // ═══════════════════════════════════════════════════════════════
    // 输入事件的语义模型
    // ═══════════════════════════════════════════════════════════════
    //
    // 输入事件的三阶：
    //   [一] 物理阶    平台原始信号
    //   [二] 规范阶    平台无关的输入事件
    //   [三] 语义阶    从规范阶序列解读出的用户意图
    //
    // 跨平台能力的落点：阶 [二] 是稳定边界。
    // 应用只对接 [二] 和 [三]，永不对接 [一]。

    // 指针设备类型。不是"鼠标/触摸屏"的分类，而是"手感"的分类——
    // slop 阈值、精度、是否支持压力都由此决定。
    enum class PointerDevice : std::uint8_t
    {
        Unknown,
        Touch,
        Mouse,
        Stylus,
        InvertedStylus,
        Trackpad,
    };
    struct PointerId
    {
        constexpr static std::uint32_t Invalid = static_cast<std::uint32_t>(~0);
        std::uint32_t value;
    };
    enum class PointerPhase : std::uint8_t
    {
        None,
        Hover,
        Down,
        Up,
        DownMove,
    };
    enum class KeyStatus : std::uint8_t
    {
        eRELEASE, // 释放
        ePRESS,   // 按下
    };

    enum class MouseButton : std::uint8_t
    {
        eUNDEFINED = 0xFF, //NOTE: 特别映射
        eMOUSE_BUTTON_1 = 0,
        eMOUSE_BUTTON_2 = 1,
        eMOUSE_BUTTON_3 = 2,
        eMOUSE_BUTTON_4 = 3,
        eMOUSE_BUTTON_5 = 4,
        eMOUSE_BUTTON_6 = 5,
        eMOUSE_BUTTON_7 = 6,
        eMOUSE_BUTTON_8 = 7,

        // NOTE: 别名
        eMOUSE_BUTTON_LAST = eMOUSE_BUTTON_8,   // = 7
        eMOUSE_BUTTON_LEFT = eMOUSE_BUTTON_1,   // = 0
        eMOUSE_BUTTON_RIGHT = eMOUSE_BUTTON_2,  // = 1
        eMOUSE_BUTTON_MIDDLE = eMOUSE_BUTTON_3, // = 2

        eSIZE = static_cast<std::uint8_t>(eMOUSE_BUTTON_LAST) + 1
    };
    static_assert(std::to_underlying(MouseButton::eSIZE) == 8);
    static_assert(std::to_underlying(MouseButton{}) == 0);

    template <class T>
    [[nodiscard]] static constexpr auto gen_mouse_button(
        typename T::mousebutton_event e) noexcept
    {
        using form = decltype(e.button);
        using to = MouseButton;

        switch (e.button)
        {
        case form::eMOUSE_BUTTON_LEFT:
            return to::eMOUSE_BUTTON_LEFT;
        case form::eMOUSE_BUTTON_RIGHT:
            return to::eMOUSE_BUTTON_RIGHT;
        case form::eMOUSE_BUTTON_MIDDLE:
            return to::eMOUSE_BUTTON_MIDDLE;
        case form::eMOUSE_BUTTON_4:
            return to::eMOUSE_BUTTON_4;
        case form::eMOUSE_BUTTON_5:
            return to::eMOUSE_BUTTON_5;
        case form::eMOUSE_BUTTON_6:
            return to::eMOUSE_BUTTON_6;
        case form::eMOUSE_BUTTON_7:
            return to::eMOUSE_BUTTON_7;
        case form::eMOUSE_BUTTON_LAST:
            return to::eMOUSE_BUTTON_LAST;
        default:
            return to::eUNDEFINED;
        }
    }
    using mouse_button_value_type = std::underlying_type_t<MouseButtons>;

    struct mouse_button_event
    {
        [[nodiscard]] constexpr bool is_left() const noexcept
        {
            return value_ == std::to_underlying(MouseButtons::eMOUSE_BUTTON_LEFT);
        }
        [[nodiscard]] constexpr bool is_right() const noexcept
        {
            return value_ == std::to_underlying(MouseButtons::eMOUSE_BUTTON_RIGHT);
        }
        [[nodiscard]] constexpr bool is_middle() const noexcept
        {
            return value_ == std::to_underlying(MouseButtons::eMOUSE_BUTTON_MIDDLE);
        }
        [[nodiscard]] constexpr auto value() const noexcept
        {
            return value_;
        }

      private:
        mouse_button_value_type value_;
    };

    struct arena_pointer
    {
        PointerDevice kind = PointerDevice::Unknown;
        PointerPhase phase = PointerPhase::None;

        // clang-format off
        // ---- phase 谓词 ----
        [[nodiscard]] constexpr bool isNone() const noexcept
        { return phase == PointerPhase::None; }

        [[nodiscard]] constexpr bool isHover() const noexcept
        { return phase == PointerPhase::Hover; }

        [[nodiscard]] constexpr bool isDown() const noexcept
        { return phase == PointerPhase::Down; }

        [[nodiscard]] constexpr bool isUp() const noexcept
        { return phase == PointerPhase::Up; }

        [[nodiscard]] constexpr bool isDownMove() const noexcept
        { return phase == PointerPhase::DownMove; }


        // ---- device 谓词 ----
        [[nodiscard]] constexpr bool isMouse() const noexcept
        { return kind == PointerDevice::Mouse; }

        [[nodiscard]] constexpr bool isTouch() const noexcept
        { return kind == PointerDevice::Touch; }

        [[nodiscard]] constexpr bool isStylus() const noexcept
        { return kind == PointerDevice::Stylus ||
                 kind == PointerDevice::InvertedStylus; }
        // clang-format on
    };

}; // namespace arena_event

using join_arena_interface = void (*)(void *obj, submit_flag flag, void *ctx,
                                      static_string declare_fn) noexcept;

struct member_join_key
{
    void *member;
};
template <typename... Ts>
struct publish_types
{
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

struct member_registry
{
    using publish_fn = void (*)(void *member, const void *type, void *args) noexcept;

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

    constexpr void publish_all(const void *type, void *data) noexcept
    {
        const bool outer = !distributing_;
        if (outer)
            distributing_ = true;

        for (const auto &item : callbacks_)
            if (item.valid)
                item.publish(item.member, type, data);

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
            &member, publish_fn{[](void *m, const void *type, void *p) noexcept {
                [&]<class... Ts>(publish_types<Ts...> *) noexcept {
                    constexpr auto name = static_string{"publish"};
                    auto *obj = static_cast<Member *>(m);
                    auto invoke = [&](auto &d) noexcept {
                        obj->template invoke<name>(std::move(d));
                    };
                    using mcs::vulkan::runtime_type_v;
                    ((requires() { obj->template invoke<name>(std::declval<Ts>()); } &&
                      type == runtime_type_v<Ts> &&
                      (invoke(*static_cast<Ts *>(p)), true)) ||
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
// main
// ═══════════════════════════════════════════════════════════════════════
int main()
try
{
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

    using arena_data_tuple = publish_types<arena_event::arena_pointer>;
    auto arena = make_aggregate<"arena">(
        field<"mermbers">(member_registry{}), //
        method<"publish_types">(
            []() noexcept { return static_cast<arena_data_tuple *>(nullptr); }),
        method<"join">([]<class Self, class Member>(Self &self, Member &m) noexcept {
            self.mermbers.template add<Self>(m);
        }),
        method<"detach">(
            [](auto &self, void *member) noexcept { self.mermbers.detach(member); }),
        method<"input_event">([]<typename event_type>(auto &self, event_type e) noexcept {
            // do something
            std::println("input_event: {}", e);
            auto doPublish = [&](auto data) noexcept {
                using mcs::vulkan::runtime_type_v;
                self.mermbers.publish_all(runtime_type_v<decltype(data)>, &data);
            };
            if constexpr (std::same_as<event_type,
                                       glfw_input_translation::mousebutton_event>)
            {
                if (glfw_input_translation::mousebutton_event{e}.press())
                {
                    arena_event::arena_pointer data{arena_event::PointerDevice::Mouse,
                                                    arena_event::PointerPhase::Down};
                    doPublish(data);
                }
            }
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
        method<"publish">(
            []<typename Self, typename Data>(Self &self, Data data) noexcept {
                //
                std::println("recognizer publish...");
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
            self.submit_click(self.arena_ptr, submit_flag::win, &self, "click");
        }),
        method<"double_click">([](auto &self, auto e) noexcept {
            self.submit_double_click(self.arena_ptr, submit_flag::win, &self,
                                     "double_click");
        }),
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