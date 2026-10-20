#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  uhi_integration.hpp —— uhi 与 mcs::vulkan::event 的集成层
//
//  它只回答三个问题：
//    ① 平台事件集合是什么      → uhi::platform_events
//    ② arena 要满足什么契约     → 有一个 method<"input_event">，能收下集合里每一种
//    ③ 谁来接分发器            → uhi::subscribe(arena)，返回 RAII 句柄
//
//  于是"集成接口"只有两个名字：
//      uhi::subscribe(arena)        // 外部输入 → arena
//      method<"input_event">        // arena 自己的入口
//  名字是固定的，类型是自由的：arena 怎么写、字段叫什么，这里都不关心。
// ═══════════════════════════════════════════════════════════════════════
#include <cassert>
#include <cstdint>
#include <optional>
#include <tuple>
#include <type_traits>

#include "uhi.hpp"

#include "../../../../../../include/detail/event/cursor_enter_event_dispatcher.hpp"
#include "../../../../../../include/detail/event/cursor_pos_event_dispatcher.hpp"
#include "../../../../../../include/detail/event/keyboard_event_dispatcher.hpp"
#include "../../../../../../include/detail/event/mousebutton_event_dispatcher.hpp"
#include "../../../../../../include/detail/event/window_size_event_dispatcher.hpp"
#include "../../../../../../include/detail/meta/class_utils.hpp"
#include "../../../../../../include/detail/utils/runtime_type.hpp"

namespace uhi
{
    namespace pev   = mcs::vulkan::event; // 平台事件（不能叫 event：uhi::event 是记录类型）

    // ═══════════════════════════════════════════════════════════════════
    // 反射调用助手（从 test_glfw12.cpp 原样搬来，只换了命名空间）
    // ═══════════════════════════════════════════════════════════════════
    struct invoke_member_help
    {
        static consteval std::optional<std::meta::info> find_function(std::meta::info cls,
                                                                     static_string name)
        {
            return mcs::vulkan::meta::class_utils::find_member(
                cls, name,
                [](std::meta::info m) -> bool {
                    return std::meta::is_function(m) || std::meta::is_function_template(m);
                },
                std::meta::access_context::current());
        }
        static consteval std::optional<std::meta::info> find_nonstatic_data_member(
            std::meta::info cls, static_string name)
        {
            return mcs::vulkan::meta::class_utils::find_member(
                cls, name,
                [](std::meta::info m) -> bool {
                    return std::meta::is_nonstatic_data_member(m);
                },
                std::meta::access_context::current());
        }
        template <static_string fn_name, class T>
        static consteval std::meta::info resolve_callable()
        {
            if (auto f = find_function(^^T, fn_name); f.has_value()) return *f;
            if (auto d = find_nonstatic_data_member(^^T, fn_name); d.has_value()) return *d;
            return std::meta::info{};
        }
        template <static_string fn_name, class Obj, class... Args>
        static constexpr bool can_noexcept_invoke() noexcept
        {
            using T = std::remove_cvref_t<Obj>;
            if constexpr (requires(T &o, Args &&...as) {
                              { o.template invoke<fn_name>(std::forward<Args>(as)...) } noexcept;
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
                static_assert(sizeof(Obj) == 0, "invoke_member: member exists but not callable");
        }
    };

    template <typename... Extra>
    struct proxy_class_invoke : invoke_member_help
    {
        using invoke_type =
            void (*)(void *ptr, const void *type, void *data, Extra... e) noexcept;

        constexpr void invoke(void *ptr, const void *type, void *data, Extra... e) noexcept
        {
            assert(invoke_ != nullptr);
            invoke_(ptr, type, data, e...);
        }
        proxy_class_invoke() = default;
        constexpr explicit proxy_class_invoke(invoke_type invoke) noexcept : invoke_{invoke} {}

        template <static_string name, typename Obj, typename... Ts>
        static constexpr void dispach_invoke(Obj *ptr, std::tuple<Ts...> *, const void *type,
                                             void *p, Extra... e) noexcept
        {
            auto call = [&](auto &d) noexcept {
                if constexpr (requires() { ptr->template invoke<name>(d, e...); })
                    ptr->template invoke<name>(d, e...);
                else if constexpr (requires() { invoke_member<name>(*ptr, d, e...); })
                    invoke_member<name>(*ptr, d, e...);
            };
            ((can_noexcept_invoke<name, Obj, Ts, Extra...>() && type == mcs::vulkan::runtime_type_v<Ts> &&
              (call(*static_cast<Ts *>(p)), true)) ||
             ...);
        }

        template <static_string name, typename Tuple, class T>
        static constexpr auto build() noexcept
        {
            using object_type = std::remove_cvref_t<T>;
            return proxy_class_invoke{invoke_type{
                [](void *ptr, const void *type, void *data, Extra... args) noexcept {
                    dispach_invoke<name, object_type>(static_cast<object_type *>(ptr),
                                                      static_cast<Tuple *>(nullptr), type, data,
                                                      args...);
                }}};
        }

      private:
        invoke_type invoke_{};
    };

    // ═══════════════════════════════════════════════════════════════════
    // ① 平台事件集合：集成层接受的全部输入类型
    // ═══════════════════════════════════════════════════════════════════
    using platform_events =
        std::tuple<pev::keyboard_event, pev::mousebutton_event, pev::position2d_event,
                   pev::cursor_enter_event, pev::window_size_event>;

    // ═══════════════════════════════════════════════════════════════════
    // ② 契约检查：arena 的 input_event 能收下集合里每一种（且 noexcept）
    //    —— 这就是 dispach_invoke 的判据，能满足才谈得上"投递到得了"
    // ═══════════════════════════════════════════════════════════════════
    template <class Arena, class Tuple = platform_events>
    struct accepts_events;
    template <class Arena, class... Ts>
    struct accepts_events<Arena, std::tuple<Ts...>>
        : std::bool_constant<(invoke_member_help::can_noexcept_invoke<"input_event", Arena, Ts>() &&
                              ...)>
    {
    };
    template <class Arena>
    inline constexpr bool accepts_platform_events_v = accepts_events<Arena>::value;

    // ═══════════════════════════════════════════════════════════════════
    // ③ 订阅：RAII。构造时把 arena 接到所有平台分发器上，析构时摘掉。
    //    唯一的固定名：method<"input_event">
    // ═══════════════════════════════════════════════════════════════════
    template <class Arena>
    class subscription
    {
      public:
        explicit subscription(Arena &a)
            : arena_{&a},
              proxy_{proxy_class_invoke<>::template build<"input_event", platform_events,
                                                          Arena>()}
        {
            pev::keyboard_event_dispatcher::instance().subscribe(this, &on<pev::keyboard_event>);
            pev::mousebutton_event_dispatcher::instance().subscribe(this, &on<pev::mousebutton_event>);
            pev::cursor_pos_event_dispatcher::instance().subscribe(this, &on<pev::position2d_event>);
            pev::cursor_enter_event_dispatcher::instance().subscribe(this, &on<pev::cursor_enter_event>);
            pev::window_size_event_dispatcher::instance().subscribe(this, &on<pev::window_size_event>);
        }
        ~subscription()
        {
            pev::keyboard_event_dispatcher::instance().unsubscribe(this, &on<pev::keyboard_event>);
            pev::mousebutton_event_dispatcher::instance().unsubscribe(this, &on<pev::mousebutton_event>);
            pev::cursor_pos_event_dispatcher::instance().unsubscribe(this, &on<pev::position2d_event>);
            pev::cursor_enter_event_dispatcher::instance().unsubscribe(this, &on<pev::cursor_enter_event>);
            pev::window_size_event_dispatcher::instance().unsubscribe(this, &on<pev::window_size_event>);
        }
        subscription(const subscription &) = delete;
        subscription &operator=(const subscription &) = delete;
        subscription(subscription &&) = delete;
        subscription &operator=(subscription &&) = delete;

      private:
        template <class E>
        static void on(void *self, E e) noexcept
        {
            auto *s = static_cast<subscription *>(self);
            s->proxy_.invoke(s->arena_, mcs::vulkan::runtime_type_v<E>, &e); // ← 类型擦除 + 反射派发
        }

        Arena *arena_{};
        proxy_class_invoke<> proxy_{};
    };

    template <class Arena>
    [[nodiscard]] subscription<Arena> subscribe(Arena &a)
    {
        static_assert(accepts_platform_events_v<Arena>,
                      "arena 必须提供 method<\"input_event\">，且接受 platform_events 里的每一种");
        return subscription<Arena>{a};
    }
} // namespace uhi
