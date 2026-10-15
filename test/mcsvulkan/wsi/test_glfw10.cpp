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
template <typename Obj, typename P>
concept can_invoke = requires(Obj *ptr, position2d_event pos,
                              std::chrono::steady_clock::time_point time_point, P p) {
    requires requires {
        { ptr->template invoke<"input_event">(pos, time_point, p) } noexcept;
    } || requires {
        { ptr->input_event(pos, time_point, p) } noexcept;
    };
};

namespace dispach
{
    template <class T>
    static void onPosition2d(void *self, position2d_event pos,
                             std::chrono::steady_clock::time_point time_point,
                             position2d_event e) noexcept
    {
        static_cast<T *>(self)->forward(pos, time_point, e);
    }
    template <class T>
    static void onKeyboard(void *self, position2d_event pos,
                           std::chrono::steady_clock::time_point time_point,
                           keyboard_event e) noexcept
    {
        static_cast<T *>(self)->forward(pos, time_point, e);
    }
    template <class T>
    static void onMousebutton(void *self, position2d_event pos,
                              std::chrono::steady_clock::time_point time_point,
                              mousebutton_event e) noexcept
    {
        static_cast<T *>(self)->forward(pos, time_point, e);
    }
} // namespace dispach

struct dynamic_pointer
{
    using Fn = void (*)(void *ptr, position2d_event pos,
                        std::chrono::steady_clock::time_point tp, const void *type,
                        void *p) noexcept;

    template <class Dispach, class Agg>
    static constexpr dynamic_pointer make() noexcept
    {
        return dynamic_pointer{[](void *ptr, position2d_event pos,
                                  std::chrono::steady_clock::time_point tp,
                                  const void *type, void *p) noexcept {
            auto *agg = static_cast<std::decay_t<Agg> *>(ptr);
            Dispach::dispach(agg, pos, tp, type, p);
        }};
    }

    template <typename P>
        requires(!std::is_reference_v<P> && std::is_same_v<P, std::remove_cv_t<P>>)
    constexpr void dispach(position2d_event pos,
                           std::chrono::steady_clock::time_point time_point, void *ptr,
                           P p) noexcept
    {
        void *volatile vp = &p;
        fn(ptr, pos, time_point, runtime_type_v<P>, vp);
    }

    dynamic_pointer() = default;

  private:
    Fn fn{};
    explicit constexpr dynamic_pointer(Fn f) noexcept : fn(f) {}
};

template <typename... Extra>
struct proxy_class_invoke
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

// ═══════════════════════════════════════════════════════════════════════
// 【强类型】PointerId
// ═══════════════════════════════════════════════════════════════════════
struct PointerId
{
    std::uint64_t value = 0;
    constexpr bool operator==(const PointerId &) const noexcept = default;
};

template <>
struct std::hash<PointerId>
{
    std::size_t operator()(const PointerId &p) const noexcept
    {
        return std::hash<std::uint64_t>{}(p.value);
    }
};

// ═══════════════════════════════════════════════════════════════════════
// 手势事件
// ═══════════════════════════════════════════════════════════════════════
using Clock = std::chrono::steady_clock;
using time_point = Clock::time_point;

enum class MouseGesture : std::uint8_t
{
    Click,
    DoubleClick,
    LongPressStart,
    LongPressMoveUpdate,
    LongPressEnd,
    DragStart,
    DragUpdate,
    DragEnd,
    ScrollUp,
    ScrollDown,
    ScrollLeft,
    ScrollRight,
};

struct GestureEvent
{
    MouseGesture kind;
    MouseButtons button = MouseButtons::eUNDEFINED;
    ModifierKey modifier = ModifierKey::None();
    double x{}, y{};
    double x0{}, y0{};
};

namespace std
{
    template <>
    struct formatter<MouseGesture>
    {
        static constexpr auto parse(std::format_parse_context &ctx) noexcept
        {
            return ctx.begin();
        }
        static auto format(MouseGesture g, std::format_context &ctx)
        {
            auto n = [&] {
                switch (g)
                {
                case MouseGesture::Click:
                    return "Click";
                case MouseGesture::DoubleClick:
                    return "DoubleClick";
                case MouseGesture::LongPressStart:
                    return "LongPressStart";
                case MouseGesture::LongPressMoveUpdate:
                    return "LongPressMoveUpdate";
                case MouseGesture::LongPressEnd:
                    return "LongPressEnd";
                case MouseGesture::DragStart:
                    return "DragStart";
                case MouseGesture::DragUpdate:
                    return "DragUpdate";
                case MouseGesture::DragEnd:
                    return "DragEnd";
                case MouseGesture::ScrollUp:
                    return "ScrollUp";
                case MouseGesture::ScrollDown:
                    return "ScrollDown";
                case MouseGesture::ScrollLeft:
                    return "ScrollLeft";
                case MouseGesture::ScrollRight:
                    return "ScrollRight";
                }
                return "?";
            }();
            return std::format_to(ctx.out(), "{}", n);
        }
    };
} // namespace std

inline auto log_origin = Clock::now();
inline void trace(std::string_view tag, std::string_view msg) noexcept
{
    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - log_origin)
            .count();
    std::println(stderr, "T[{:>7}ms][{:>13}] {}", ms, tag, msg);
}

// ═══════════════════════════════════════════════════════════════════════
// 【接口 1】ArenaMember
// ═══════════════════════════════════════════════════════════════════════
struct ArenaMember
{
    virtual void accept_gesture(PointerId) noexcept = 0;
    virtual void reject_gesture(PointerId) noexcept = 0;
    virtual ~ArenaMember() = default;
};

// ═══════════════════════════════════════════════════════════════════════
// 【接口 2】GestureArenaEntry — 识别器与竞技场之间的唯一句柄
// ═══════════════════════════════════════════════════════════════════════
class GestureArena;

class GestureArenaEntry
{
  public:
    GestureArenaEntry() noexcept = default;

    void resolve(bool accepted) noexcept;
    void hold() noexcept;
    [[nodiscard]] bool valid() const noexcept
    {
        return arena_ != nullptr;
    }

  private:
    friend class GestureArena;
    GestureArenaEntry(GestureArena *a, PointerId id, ArenaMember *m) noexcept
        : arena_(a), pointer_(id), member_(m)
    {
    }
    GestureArena *arena_ = nullptr;
    PointerId pointer_{};
    ArenaMember *member_ = nullptr;
};

// ═══════════════════════════════════════════════════════════════════════
// 【实体 3】GestureArena — 单指针竞技场
// ═══════════════════════════════════════════════════════════════════════
class GestureArena
{
  public:
    explicit GestureArena(PointerId id) noexcept : pointer_(id) {}

    GestureArenaEntry add(ArenaMember *m) noexcept
    {
        if (!is_open_)
            return {};
        members_.push_back(m);
        return GestureArenaEntry{this, pointer_, m};
    }

    void close() noexcept
    {
        if (!is_open_)
            return;
        is_open_ = false;
        try_to_resolve();
    }

    void sweep() noexcept
    {
        if (is_held_)
        {
            has_pending_sweep_ = true;
            return;
        }
        do_sweep();
    }

    void hold() noexcept
    {
        is_held_ = true;
    }

    void release() noexcept
    {
        is_held_ = false;
        if (has_pending_sweep_)
        {
            has_pending_sweep_ = false;
            do_sweep();
        }
    }

    [[nodiscard]] bool resolved() const noexcept
    {
        return resolved_;
    }

  private:
    friend class GestureArenaEntry;

    void resolve(ArenaMember *m, bool accepted) noexcept
    {
        if (resolved_)
            return;
        if (accepted)
        {
            if (is_open_)
            {
                if (!eager_winner_)
                    eager_winner_ = m;
            }
            else
            {
                resolve_in_favor_of(m);
            }
        }
        else
        {
            auto it = std::find(members_.begin(), members_.end(), m);
            if (it != members_.end())
            {
                members_.erase(it);
                m->reject_gesture(pointer_);
            }
            if (!is_open_)
                try_to_resolve();
        }
    }

    void try_to_resolve() noexcept
    {
        if (is_held_ || resolved_)
            return;
        if (members_.size() == 1)
        {
            resolve_in_favor_of(members_[0]);
        }
        else if (members_.empty())
        {
            resolved_ = true;
        }
        else if (eager_winner_)
        {
            resolve_in_favor_of(eager_winner_);
        }
    }

    void resolve_in_favor_of(ArenaMember *winner) noexcept
    {
        if (resolved_)
            return;
        resolved_ = true;
        auto snapshot = std::move(members_);
        members_.clear();
        eager_winner_ = nullptr;
        is_held_ = false;
        for (auto *m : snapshot)
            if (m != winner)
                m->reject_gesture(pointer_);
        winner->accept_gesture(pointer_);
    }

    void do_sweep() noexcept
    {
        if (resolved_)
            return;
        if (members_.empty())
        {
            resolved_ = true;
            return;
        }
        auto *first = members_[0];
        resolved_ = true;
        auto snapshot = std::move(members_);
        members_.clear();
        eager_winner_ = nullptr;
        for (std::size_t i = 1; i < snapshot.size(); ++i)
            snapshot[i]->reject_gesture(pointer_);
        first->accept_gesture(pointer_);
    }

    PointerId pointer_;
    std::vector<ArenaMember *> members_;
    bool is_open_ = true;
    bool is_held_ = false;
    bool has_pending_sweep_ = false;
    bool resolved_ = false;
    ArenaMember *eager_winner_ = nullptr;
};

inline void GestureArenaEntry::resolve(bool accepted) noexcept
{
    if (arena_)
        arena_->resolve(member_, accepted);
}

inline void GestureArenaEntry::hold() noexcept
{
    if (arena_)
        arena_->hold();
}

// ═══════════════════════════════════════════════════════════════════════
// 【实体 4】GestureArenaManager — 每个 pointer 一个竞技场
// ═══════════════════════════════════════════════════════════════════════
class GestureArenaManager
{
  public:
    GestureArenaEntry add(PointerId id, ArenaMember *m) noexcept
    {
        auto [it, inserted] = arenas_.try_emplace(id, id);
        return it->second.add(m);
    }

    void close(PointerId id) noexcept
    {
        if (auto it = arenas_.find(id); it != arenas_.end())
            it->second.close();
    }

    void sweep(PointerId id) noexcept
    {
        if (auto it = arenas_.find(id); it != arenas_.end())
            it->second.sweep();
    }

    void hold(PointerId id) noexcept
    {
        if (auto it = arenas_.find(id); it != arenas_.end())
            it->second.hold();
    }

    void release(PointerId id) noexcept
    {
        if (auto it = arenas_.find(id); it != arenas_.end())
            it->second.release();
    }

    void erase(PointerId id) noexcept
    {
        arenas_.erase(id);
    }

  private:
    std::unordered_map<PointerId, GestureArena> arenas_;
};

// ═══════════════════════════════════════════════════════════════════════
// 输入事件（供识别器看）
// ═══════════════════════════════════════════════════════════════════════
struct PointerEvent
{
    PointerId id;
    double x = 0, y = 0;
    MouseButtons button = MouseButtons::eUNDEFINED;
    ModifierKey modifiers = ModifierKey::None();
};

// ═══════════════════════════════════════════════════════════════════════
// 【接口 5】Recognizer
//
// 公共虚接口（被事件源/竞技场调用）：
//   on_down / on_move / on_up / on_tick
//   accept_gesture / reject_gesture（继承自 ArenaMember）
//
// protected 便利方法（识别器主动调用）：
//   join_arena(id) → Entry   加入竞技场
//   emit(event)               产出识别结果
//
// private 依赖（外部看不见）：
//   arenas_ / output_         由 GestureEngine 注入
// ═══════════════════════════════════════════════════════════════════════
struct GestureEngine;

struct Recognizer : ArenaMember
{
    virtual void on_down(const PointerEvent &) noexcept {}
    virtual void on_move(const PointerEvent &) noexcept {}
    virtual void on_up(const PointerEvent &) noexcept {}
    virtual void on_tick(time_point) noexcept {}

    virtual ~Recognizer() = default;

  protected:
    [[nodiscard]] GestureArenaEntry join_arena(PointerId id) noexcept;
    void emit(const GestureEvent &g) noexcept;

  private:
    friend struct GestureEngine;
    GestureArenaManager *arenas_ = nullptr;
    std::vector<GestureEvent> *output_ = nullptr;
};

// ═══════════════════════════════════════════════════════════════════════
// 常量
// ═══════════════════════════════════════════════════════════════════════
inline constexpr auto DBL_WINDOW = std::chrono::milliseconds(150);
inline constexpr auto LONG_PRESS = std::chrono::milliseconds(500);
inline constexpr double DRAG_SLOP_SQ = 16.0;

// ═══════════════════════════════════════════════════════════════════════
// TapRecognizer — 单击 + 双击
// ═══════════════════════════════════════════════════════════════════════
struct TapRecognizer : Recognizer
{
    enum State : std::uint8_t
    {
        Idle,
        Pressed,
        WaitingSecond
    };
    State state = Idle;
    GestureArenaEntry entry;
    PointerId current_id{};
    time_point press_time{};
    time_point deadline{};
    double press_x = 0, press_y = 0;
    GestureEvent pending{};
    bool has_pending = false;

    void on_down(const PointerEvent &e) noexcept override
    {
        // 双击窗口内？
        if (state == WaitingSecond && Clock::now() < deadline)
        {
            trace("tap", "2nd DOWN → DoubleClick");
            pending = GestureEvent{MouseGesture::DoubleClick,
                                   e.button,
                                   e.modifiers,
                                   e.x,
                                   e.y,
                                   press_x,
                                   press_y};
            has_pending = true;
            current_id = e.id; // 先更新，让老 arena 的 accept 被过滤
            entry.resolve(true);
            entry = join_arena(e.id);
            entry.resolve(true);
            state = Idle;
            return;
        }

        trace("tap", "DOWN → Pressed");
        current_id = e.id;
        state = Pressed;
        press_time = Clock::now();
        press_x = e.x;
        press_y = e.y;
        entry = join_arena(e.id);
    }

    void on_up(const PointerEvent &e) noexcept override
    {
        if (state != Pressed)
            return;
        auto dur = Clock::now() - press_time;
        if (dur >= LONG_PRESS)
        {
            trace("tap", "long UP → resolve(false)");
            state = Idle;
            entry.resolve(false);
            return;
        }
        trace("tap", "short UP → WaitingSecond, hold");
        state = WaitingSecond;
        deadline = Clock::now() + DBL_WINDOW;
        pending = GestureEvent{MouseGesture::Click,
                               e.button,
                               e.modifiers,
                               press_x,
                               press_y,
                               press_x,
                               press_y};
        has_pending = true;
        entry.hold();
    }

    void on_tick(time_point now) noexcept override
    {
        if (state != WaitingSecond)
            return;
        if (now < deadline)
            return;
        trace("tap", "window expired → resolve(true) Click");
        state = Idle;
        entry.resolve(true);
    }

    void accept_gesture(PointerId id) noexcept override
    {
        trace("tap", "accept");
        if (id != current_id)
            return;
        if (has_pending)
        {
            emit(pending);
            has_pending = false;
        }
        state = Idle;
    }

    void reject_gesture(PointerId id) noexcept override
    {
        trace("tap", "reject");
        if (id != current_id)
            return;
        has_pending = false;
        state = Idle;
    }
};

// ═══════════════════════════════════════════════════════════════════════
// LongPressRecognizer
// ═══════════════════════════════════════════════════════════════════════
struct LongPressRecognizer : Recognizer
{
    enum State : std::uint8_t
    {
        Idle,
        Possible,
        Accepted
    };
    State state = Idle;
    GestureArenaEntry entry;
    PointerId current_id{};
    time_point press_time{};
    double press_x = 0, press_y = 0;
    MouseButtons saved_button = MouseButtons::eUNDEFINED;
    ModifierKey saved_mod = ModifierKey::None();
    GestureEvent pending{};
    bool has_pending = false;

    void on_down(const PointerEvent &e) noexcept override
    {
        if (state != Idle)
            return;
        trace("lp", "DOWN → Possible");
        current_id = e.id;
        state = Possible;
        press_time = Clock::now();
        press_x = e.x;
        press_y = e.y;
        saved_button = e.button;
        saved_mod = e.modifiers;
        pending = GestureEvent{MouseGesture::LongPressStart,
                               saved_button,
                               saved_mod,
                               press_x,
                               press_y,
                               press_x,
                               press_y};
        has_pending = true;
        entry = join_arena(e.id);
    }

    void on_up(const PointerEvent &e) noexcept override
    {
        if (state == Accepted)
        {
            trace("lp", "Accepted + UP → LongPressEnd");
            emit(GestureEvent{MouseGesture::LongPressEnd, saved_button, saved_mod, e.x,
                              e.y, press_x, press_y});
            state = Idle;
            return;
        }
        if (state == Possible)
        {
            trace("lp", "Possible + early UP → resolve(false)");
            state = Idle;
            has_pending = false;
            entry.resolve(false);
        }
    }

    void on_move(const PointerEvent &e) noexcept override
    {
        if (state == Accepted)
        {
            emit(GestureEvent{MouseGesture::LongPressMoveUpdate, saved_button, saved_mod,
                              e.x, e.y, press_x, press_y});
            return;
        }
        if (state == Possible)
        {
            double dx = e.x - press_x;
            double dy = e.y - press_y;
            if (dx * dx + dy * dy > DRAG_SLOP_SQ)
            {
                trace("lp", "beyond slop → resolve(false)");
                state = Idle;
                has_pending = false;
                entry.resolve(false);
            }
        }
    }

    void on_tick(time_point now) noexcept override
    {
        if (state == Possible && now - press_time >= LONG_PRESS)
        {
            trace("lp", "500ms → resolve(true)");
            entry.resolve(true);
        }
    }

    void accept_gesture(PointerId id) noexcept override
    {
        trace("lp", "accept");
        if (id != current_id)
            return;
        if (has_pending)
        {
            emit(pending);
            has_pending = false;
        }
        state = Accepted;
    }

    void reject_gesture(PointerId id) noexcept override
    {
        trace("lp", "reject");
        if (id != current_id)
            return;
        has_pending = false;
        state = Idle;
    }
};

// ═══════════════════════════════════════════════════════════════════════
// DragRecognizer
// ═══════════════════════════════════════════════════════════════════════
struct DragRecognizer : Recognizer
{
    enum State : std::uint8_t
    {
        Idle,
        Possible,
        Accepted
    };
    State state = Idle;
    GestureArenaEntry entry;
    PointerId current_id{};
    double origin_x = 0, origin_y = 0;
    MouseButtons saved_button = MouseButtons::eUNDEFINED;
    ModifierKey saved_mod = ModifierKey::None();
    GestureEvent pending{};
    bool has_pending = false;

    void on_down(const PointerEvent &e) noexcept override
    {
        if (state != Idle)
            return;
        trace("drag", "DOWN → Possible");
        current_id = e.id;
        state = Possible;
        origin_x = e.x;
        origin_y = e.y;
        saved_button = e.button;
        saved_mod = e.modifiers;
        pending = GestureEvent{
            MouseGesture::DragStart, saved_button, saved_mod, e.x, e.y, e.x, e.y};
        has_pending = true;
        entry = join_arena(e.id);
    }

    void on_up(const PointerEvent &e) noexcept override
    {
        if (state == Accepted)
        {
            trace("drag", "Accepted + UP → DragEnd");
            emit(GestureEvent{MouseGesture::DragEnd, saved_button, saved_mod, e.x, e.y,
                              origin_x, origin_y});
            state = Idle;
            return;
        }
        if (state == Possible)
        {
            trace("drag", "Possible + early UP → resolve(false)");
            state = Idle;
            has_pending = false;
            entry.resolve(false);
        }
    }

    void on_move(const PointerEvent &e) noexcept override
    {
        if (state == Accepted)
        {
            emit(GestureEvent{MouseGesture::DragUpdate, saved_button, saved_mod, e.x, e.y,
                              origin_x, origin_y});
            return;
        }
        if (state == Possible)
        {
            double dx = e.x - origin_x;
            double dy = e.y - origin_y;
            if (dx * dx + dy * dy > DRAG_SLOP_SQ)
            {
                trace("drag", "beyond slop → resolve(true)");
                entry.resolve(true);
            }
        }
    }

    void accept_gesture(PointerId id) noexcept override
    {
        trace("drag", "accept");
        if (id != current_id)
            return;
        if (has_pending)
        {
            emit(pending);
            has_pending = false;
        }
        state = Accepted;
    }

    void reject_gesture(PointerId id) noexcept override
    {
        trace("drag", "reject");
        if (id != current_id)
            return;
        has_pending = false;
        state = Idle;
    }
};

// ═══════════════════════════════════════════════════════════════════════
// GestureEngine — 装配 + 分发 + 时钟
// ═══════════════════════════════════════════════════════════════════════
struct GestureEngine
{
    GestureArenaManager arenas;
    std::vector<GestureEvent> output;
    std::vector<Recognizer *> recognizers;

    PointerEvent current{};
    std::uint64_t next_id = 1;

    void attach(Recognizer *r) noexcept
    {
        r->arenas_ = &arenas;
        r->output_ = &output;
        recognizers.push_back(r);
    }

    void dispatch(auto e) noexcept
    {
        using E = std::decay_t<decltype(e)>;
        if constexpr (std::same_as<E, mousebutton_event>)
            on_button(e);
        else if constexpr (std::same_as<E, position2d_event>)
            on_position(e);
        else if constexpr (std::same_as<E, scroll_event>)
            on_scroll(e);
        // 其他类型静默忽略
    }

    void on_button(const mousebutton_event &e) noexcept
    {
        current.button = e.button;
        current.modifiers = e.modifier_key;
        if (e.press())
        {
            current.id = PointerId{next_id++};
            trace("engine", "DOWN");
            for (auto *r : recognizers)
                r->on_down(current);
            arenas.close(current.id);
        }
        else
        {
            trace("engine", "UP");
            for (auto *r : recognizers)
                r->on_up(current);
            arenas.sweep(current.id);
        }
    }

    void on_position(const position2d_event &e) noexcept
    {
        current.x = e.xpos;
        current.y = e.ypos;
        for (auto *r : recognizers)
            r->on_move(current);
    }

    void on_scroll(const scroll_event &e) noexcept
    {
        GestureEvent g{};
        if (e.yoffset > 0)
            g.kind = MouseGesture::ScrollUp;
        else if (e.yoffset < 0)
            g.kind = MouseGesture::ScrollDown;
        else if (e.xoffset > 0)
            g.kind = MouseGesture::ScrollRight;
        else if (e.xoffset < 0)
            g.kind = MouseGesture::ScrollLeft;
        else
            return;
        g.x = e.xoffset;
        g.y = e.yoffset;
        output.push_back(g);
    }

    void tick() noexcept
    {
        auto now = Clock::now();
        for (auto *r : recognizers)
            r->on_tick(now);
    }

    template <typename Fn>
    void drain(Fn &&fn) noexcept
    {
        for (const auto &g : output)
            fn(g);
        output.clear();
    }
};

// ═══════════════════════════════════════════════════════════════════════
// Recognizer 的 protected 便利方法实现（需要 GestureEngine 已定义）
// ═══════════════════════════════════════════════════════════════════════
inline GestureArenaEntry Recognizer::join_arena(PointerId id) noexcept
{
    return arenas_->add(id, this);
}

inline void Recognizer::emit(const GestureEvent &g) noexcept
{
    output_->push_back(g);
}

// ═══════════════════════════════════════════════════════════════════════
// main
// ═══════════════════════════════════════════════════════════════════════
int main()
try
{
    log_origin = Clock::now();

    surface window{};
    window.setup({.width = WIDTH, .height = HEIGHT}, TITLE);

    glfw_input_translation input;

    GestureEngine engine;
    TapRecognizer tap;
    LongPressRecognizer lp;
    DragRecognizer drag;

    engine.attach(&tap);
    engine.attach(&lp);
    engine.attach(&drag);

    // 用 aggregate 作为 receiver：唯一的方法是 input_event
    auto receiver = make_aggregate<"GlfwReceiver">(method<"input_event">(
        [&engine](auto && /*self*/, auto e) noexcept { engine.dispatch(e); }));

    input.setReceiver(receiver);

    while (window.shouldClose() == 0)
    {
        surface::pollEvents();
        engine.tick();

        engine.drain([](const GestureEvent &g) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() -
                                                                            log_origin)
                          .count();
            switch (g.kind)
            {
            case MouseGesture::Click:
            case MouseGesture::DoubleClick:
            case MouseGesture::LongPressStart:
                std::println("[{:>7}ms] {} button={} at ({:.0f},{:.0f}) mod={}", ms,
                             g.kind, g.button, g.x, g.y, g.modifier);
                break;

            case MouseGesture::LongPressMoveUpdate:
            case MouseGesture::LongPressEnd:
            case MouseGesture::DragStart:
            case MouseGesture::DragUpdate:
            case MouseGesture::DragEnd:
                std::println(
                    "[{:>7}ms] {} button={} from ({:.0f},{:.0f}) to ({:.0f},{:.0f})", ms,
                    g.kind, g.button, g.x0, g.y0, g.x, g.y);
                break;

            case MouseGesture::ScrollUp:
            case MouseGesture::ScrollDown:
            case MouseGesture::ScrollLeft:
            case MouseGesture::ScrollRight:
                std::println("[{:>7}ms] {} delta=({:+.2f},{:+.2f})", ms, g.kind, g.x,
                             g.y);
                break;
            }
        });
    }

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}
// NOLINTEND

// ═══════════════════════════════════════════════════════════════════════════
// Flutter 手势系统 —— 角色定位 / 组织 / 责任 / 边界
//
// 本文是一份对照说明，用于把 Flutter 手势系统移植到 C++。
// 目的：明确每个角色的"是什么"，以及"不是什么"。
//
// 分层全景（自上而下）：
//
//   [0] 引擎层     PointerDataPacket
//   [1] 转换层     converter.dart  → PointerDown/Move/Up/Cancel
//                  resampler.dart  → 可选重采样
//   [2] 绑定层     GestureBinding
//                  ├─ _handlePointerEventImmediately
//                  ├─ 持有 pointerRouter / gestureArena / hitTest
//                  └─ 自己是 hit-test path 的最后一个 entry
//   [3] 命中层     HitTestResult.path  (叶子在前，祖先在后，binding 在末尾)
//   [4] 路由层     PointerRouter  (map<pointer, routes>)
//                  pointer_signal_resolver  (离散事件专用)
//   [5] 仲裁层     GestureArenaManager  (map<pointer, _GestureArena>)
//                  GestureArenaTeam     (可选：识别器组团)
//   [6] 识别层     GestureRecognizer 基类
//                  OneSequence / PrimaryPointer 中间基类
//                  Tap / DoubleTap / LongPress / Drag / ...
//
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// [2] GestureBinding
// ---------------------------------------------------------------------------
// 定位   ：全局唯一的装配者，没有它整个系统不成立
// 组织   ：单例（GestureBinding.instance），持有三个子模块
// 责任   ：① 接收引擎事件
//          ② Down 时 hitTest
//          ③ 分发路由
//          ④ Down 结束 close、Up 结束 sweep
// 边界   ：不认识任何具体手势。它不知道 tap/drag 是什么，
//          只知道"有个 pointer 事件要派发"
// 持有   ：_hitTests: map<pointer, HitTestResult>
//          pointerRouter
//          gestureArena
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// [3] HitTestResult.path
// ---------------------------------------------------------------------------
// 定位   ：一次 Down 的"谁可能关心"名单，从最内层到最外层
// 组织   ：一个数组
//          add() 顺序 = 视觉层级（子在前，父在后，binding 自己最后 add）
// 责任   ：提供"哪些 target 想订阅这个 pointer"
// 边界   ：只对 Down 事件算一次，全程复用
//          （binding.dart:345-359 缓存整个交互期）
// 关键   ：binding 是 path 的最后一个 entry，所以它总能最后一个处理事件——
//          这就是"识别器先看到 down，再看到 close"的物理基础
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// [4] PointerRouter
// ---------------------------------------------------------------------------
// 定位   ：事件分发器，把事件按 pointer 分给订阅者
// 组织   ：map<pointer, map<route_id, handler>>
// 责任   ：addRoute(pointer, route) / removeRoute / route(event)
// 边界   ：不知道事件内容，只按 pointer id 分；
//          不认识识别器，不认识竞技场
// 重入   ：分发期间允许删路由
//          （pointer_router.dart:119-134 用 _dispatchMap 快照）
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// [4b] PointerSignalResolver
// ---------------------------------------------------------------------------
// 定位   ：离散事件（滚轮等）的专用仲裁
// 组织   ：独立于 GestureArena 的轻量机制
// 责任   ：事件分发期间记第一个登记者，分发结束立即回调
// 边界   ：不做 hold，不做多轮——离散事件是瞬时的，没有"过程"
// 动机   ：pointer_signal_resolver.dart:19-29 明确解释：
//          "信号是即时的，不像连续事件那样参与竞技场"
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// [5] GestureArenaManager
// ---------------------------------------------------------------------------
// 定位   ：竞技场容器
// 组织   ：map<pointer, _GestureArena> —— 每个 pointer 一个独立竞技场
// 责任   ：转发 add / close / sweep / hold / release 给对应竞技场
// 边界   ：不认识识别器类型，只认 GestureArenaMember 抽象
// 隔离   ：两个 pointer 有两个竞技场，裁决完全独立（官方测试 D1-D3）
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// [5] _GestureArena（单竞技场）
// ---------------------------------------------------------------------------
// 定位   ：一个指针的裁决状态机
// 组织   ：5 个字段
//            members[]        : 成员列表，插入序
//            isOpen           : 名单是否封口
//            isHeld           : 是否被冻结（禁止 sweep）
//            hasPendingSweep  : 被推迟的 sweep 意图
//            eagerWinner      : 开区间内的暂存胜者
// 责任   ：三条终止路径
//            _resolveByDefault : 剩 1 个成员 → 自动胜出
//            _resolveInFavorOf : 有人表态 → 立即裁决
//            _sweep            : 强制裁决（插入序最早者胜）
// 边界   ：不认识手势语义。只知道 acceptGesture / rejectGesture 两个回调
// 核心   ：60 行左右，见 arena.dart:221-282
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// [5] GestureArenaTeam（可选）
// ---------------------------------------------------------------------------
// 定位   ：把多个识别器当成一个成员参与竞技场
// 组织   ：实现 GestureArenaMember，内部维护自己的 _members 和 _winner
// 责任   ：内部跑一遍 accept/reject，对外只暴露一个 entry
// 边界   ：对外是成员，对内是竞技场 —— 自相似
// 动机   ：team.dart:108-129 原文——
//          Slider 里的 drag 和 tap 不该互相竞争，
//          因为"竞争的唯一产物是延迟"
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// [6] GestureRecognizer（基类）
// ---------------------------------------------------------------------------
// 定位   ：识别器抽象
// 组织   ：同时实现 GestureArenaMember（成员）+ 事件处理接口
// 责任   ：① 接收 pointer 事件
//          ② 决定是否加入竞技场
//          ③ 通过 entry 表态
//          ④ 被通知 accept/reject
// 边界   ：只持有 entry，不持有竞技场。
//          它知道"我要表态"，不知道"竞技场怎么裁决"
// 接口   ：addAllowedPointer（入场）
//          handleEvent（处理）
//          acceptGesture / rejectGesture
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// [6] OneSequenceGestureRecognizer（中间基类）
// ---------------------------------------------------------------------------
// 定位   ：处理"一个 pointer 一个序列"的识别器通用逻辑
// 组织   ：持有 Map<int, GestureArenaEntry> _entries —— 多指针入口
// 责任   ：管理 entry 生命周期
//          startTrackingPointer / stopTrackingPointer
// 边界   ：不处理事件语义，只处理"入场/离场"的簿记
// 关键   ：这是多指针支持的核心——
//          一个识别器可以有 N 个 pointer 的 entry
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// [6] PrimaryPointerGestureRecognizer（中间基类）
// ---------------------------------------------------------------------------
// 定位   ："只关心主指针"的识别器基类
// 组织   ：继承 OneSequence，加 primaryPointer + deadline + Timer
// 责任   ：① 维护 deadline，到期触发 didExceedDeadline
//          ② 检查 slop，超限触发 didExceedSlop
//          ③ _checkUp / _checkDown 幂等守卫
// 边界   ：不知道具体手势，只提供"超时/超距/收尾"三个钩子
// 关键   ：_checkX 从两个方向被调用——事件侧和裁决侧。
//          这是"两相会合"模式的实现载体
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// [6] 具体识别器（Tap / DoubleTap / LongPress / Drag / ...）
// ---------------------------------------------------------------------------
// 定位   ：手势语义的唯一定义者
// 组织   ：继承合适的中间基类，
//          重写 handleEvent / acceptGesture / rejectGesture
// 责任   ：① 判断"这是不是我的手势"
//          ② 通过 entry 表态
//          ③ 产出回调
// 边界   ：不操作竞技场 —— 只能 entry.resolve() / entry.hold()
//          （通过 binding 间接）
// 时机   ：必须满足"竞技场赢 + 事件到位"两个条件才能触发用户回调
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// 一次交互的完整时序
// ---------------------------------------------------------------------------
//
//  引擎事件 PointerDown
//     │
//     ▼
//  GestureBinding._handlePointerEventImmediately
//     │  [1] hitTest 得到 path，缓存到 _hitTests[pointer]
//     │  [2] for entry in path: entry.handleEvent(event)   // 识别器在此 addPointer
//     │  [3] pointerRouter.route(event)                    // 已入场识别器收事件
//     │  [4] gestureArena.close(pointer)                   // 名单封口
//     ▼
//  识别器 handleEvent
//     │  ├─ addAllowedPointer → gestureArena.add(pointer, this) → entry
//     │  └─ 状态机推进，可能 entry.resolve(accepted/rejected)
//     ▼
//  竞技场 _resolve / _tryToResolveArena
//     │  ├─ 剩 1 个 → scheduleMicrotask(_resolveByDefault)
//     │  ├─ 有 eagerWinner → _resolveInFavorOf
//     │  └─ 都不满足 → 静默等待
//     ▼
//  识别器 acceptGesture / rejectGesture
//     │  └─ 满足"会合条件"后触发用户回调
//
//  ──────────────────────────────
//
//  引擎事件 PointerUp
//     │
//     ▼
//  GestureBinding
//     │  [1] 分发事件
//     │  [2] gestureArena.sweep(pointer)   // 强制终结
//     │  [3] 清理 _hitTests
//     ▼
//  竞技场 do_sweep（受 isHeld 影响）
//     │  ├─ held → 记 hasPendingSweep，返回
//     │  └─ 否则 → members.first 胜
//
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// 五条不可违背的边界
// ---------------------------------------------------------------------------
//  1. 竞技场不认识手势语义 —— 它只有 GestureArenaMember，
//     只调 acceptGesture / rejectGesture
//  2. 识别器不认识竞技场内部 —— 它只有 entry，只能 resolve / hold
//  3. PointerRouter 不认识事件内容 —— 只按 pointer id 分发
//  4. HitTest 不认识手势 —— 只提供"谁在路径上"
//  5. GestureBinding 不认识具体识别器 —— 只做装配和调度
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// 五条关键不变量（可背）
// ---------------------------------------------------------------------------
//  #  │ 不变量                                                │ 出处
// ────┼───────────────────────────────────────────────────────┼──────────────
//  1  │ 每个成员在每个竞技场里，                              │ arena.dart
//     │ acceptGesture 和 rejectGesture 恰好收到一次           │ :24-28
// ────┼───────────────────────────────────────────────────────┼──────────────
//  2  │ 已解决的竞技场再次 resolve 是无副作用的               │ arena.dart
//     │                                                       │ :50-51
// ────┼───────────────────────────────────────────────────────┼──────────────
//  3  │ 裁决不早于名单封口                                    │ arena.dart
//     │ （close 之前 accept 只缓存为 eagerWinner）            │ :229-234
// ────┼───────────────────────────────────────────────────────┼──────────────
//  4  │ hold 只推迟 sweep，不推迟成员主动 resolve             │ arena.dart
//     │                                                       │ :157-161
// ────┼───────────────────────────────────────────────────────┼──────────────
//  5  │ 每个 pointer 一个独立竞技场，裁决互不干扰             │ arena.dart
//     │                                                       │ :112
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// Flutter 与移植版 C++ 的对照（诊断用）
// ---------------------------------------------------------------------------
//  Flutter 概念                   │ C++ 移植版           │ 判定
// ─────────────────────────────────┼──────────────────────┼────────
//  GestureBinding（单例装配）      │ GestureEngine（成员）│ ✅ 语义等价
//  PointerRouter                   │ 无（for 循环）       │ ❌ 缺
//  HitTestResult.path              │ 无                   │ ❌ 缺
//  GestureArenaManager（map）      │ 同                   │ ✅
//  _GestureArena（5字段+3路径）    │ 同                   │ ✅
//  GestureArenaMember              │ ArenaMember          │ ✅
//  GestureArenaEntry               │ Entry                │ ✅
//  OneSequenceRecognizer           │ 单 entry             │ ❌ 缺多指针
//  PrimaryPointerRecognizer        │ on_tick 轮询         │ ⚠️ 路径不同
//  PointerSignalResolver           │ 直接 push            │ ❌ 缺
//  GestureArenaTeam                │ 无                   │ ⚠️ 未需要
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// 一句话总结
// ---------------------------------------------------------------------------
//  Flutter 的手势系统 = 一个 Binding（装配）+ 一个 Router（分发）
//                       + 一个 Arena（裁决）+ N 个 Recognizer（识别）。
//
//  四者边界严格：
//    Router 不管内容
//    Arena  不管语义
//    Recognizer 不管其他识别器
//    Binding 不管具体手势
//
//  竞技场只是这四者之一，且它的边界最小——只有 296 行，
//  只知道"谁赢谁输"。真正的复杂度分布在识别器层（N 个具体实现）
//  和路由层（多指针管理），竞技场是其中最简单、最稳定、
//  最不该膨胀的那一块。
// ═══════════════════════════════════════════════════════════════════════════

/*
  [1] 原始事件          Pointer Down / Move / Up
        │
        │  识别器不停地"消费"原始事件序列，在内部推断
        ▼
  [2] 候选事件          识别器算出"我认出来了，这是 Click / Drag / ..."
        │                ← 这一步是识别器主动的、很早就完成的
        │                ← 但它不直接发出去，它先"表态"
        │
        │  entry.resolve(true)  → 竞技场裁决
        ▼
  [3] 放行 / 拒绝       竞技场说 yes / no
        │
        │  只有 y es 时，识别器才把它早已算好的候选事件真正 emit
        ▼
  [4] 真正的事件产出    用户回调
*/