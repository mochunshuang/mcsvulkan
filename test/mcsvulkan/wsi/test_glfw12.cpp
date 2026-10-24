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

// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
//  库：矩阵裁决引擎（单文件，整块拷进来）
//
//    它自带一个 demo main；改个名字搬进来，不参与本文件的链接。
//    引擎只规定"帧 → step_t"，帧和识别器都由本案例自己写（见下）。
// ═══════════════════════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════════════
//  矩阵裁决引擎 —— 已内联进本文件（不再是 include，直接在这里改）
//    这一整段原本是 uhi/v3/src/uhi_matrix.cpp 的第 1–9 节（引擎，不含 demo main）。
//    改动顺序建议：先动 stage 的投递口，再动 fsm 协议，最后动矩阵。
// ═══════════════════════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════════════
//  uhi_matrix.cpp —— 单文件、自包含（矩阵路线原型）
//
//  目标（00-goal）：一串原始输入进去，每个通道每次交互至多一个语义事件出来，
//                   且恰好播报一次。
//
//  路线：把"谁该赢"降维成**一张矩阵**，让裁决变成矩阵上的自然选择。
//
//      beats[i] = 第 i 行 = i 打得赢谁      ← 编译期一次算好，运行期只读
//      两条来源，声明里早就说过了：
//        refines<A,B> ：B 的叶子打败 A 的叶子（更具体的赢）
//        one_of<R...> ：先声明的打败后声明的（互斥对唯一的平局规则）
//      传递闭包一跑 ⇒ 矩阵自带传递性 ⇒ 对角线为空 ⇒ 严格偏序
//      ⇒ 极大元唯一 ⇒ **选择与顺序无关**。这就是自然选择。
//
//  运行期只有一个坐标系（声明序）和一列现场：
//      alive / pending / begin / end
//  一条律：活着的、有结论的、没人能打赢的 ⇒ 赢家；赢家吃掉它能打赢的；
//          重选到不动点。跑完就是一个 accepted 位图 + 一列 Effect。
//
//  没有槽位序参与裁决 ⇒ 没有反拓扑；只有一个坐标系 ⇒ 没有 table_to_decl。
//
//  编译：g++ -std=c++26 -freflection -O2 uhi_matrix.cpp -o uhi_matrix -lstdc++exp
// ═══════════════════════════════════════════════════════════════════════════
#include <array>
#include <cassert>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <meta>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <limits>
#include <vector>

#include "../../../include/detail/meta/make_aggregate.hpp"

namespace uhi
{
    using mcs::vulkan::meta::static_string;
    using time_point = std::chrono::milliseconds;

    // 下标类型，一处可换。
    //   约定：**位置 / 编号 / 扫描计数**用它；**长度与容量**（kinds.size()、
    //   数组 extent、位图字数）仍用 std::size_t —— 后者是"多少个"，不是"第几个"。
    //   想压窄（std::uint32_t / std::uint16_t）只改这一行；改完还写着
    //   std::size_t 的地方，恰好就是"长度 → 下标"需要复核的边界。
    using index_t = std::size_t;

    // ═══════════════════════════════════════════════════════════════════
    // 0. 词汇
    // ═══════════════════════════════════════════════════════════════════

    // 识别器对仲裁层唯一能说的话
    enum class step_t : std::uint8_t
    {
        idle,    // 我没有假设
        active,  // 我有一个进行中的假设
        done,    // 我的假设成立
        aborted, // 我放弃
        hold     // 只有时间过去了，状态不变
    };

    enum class verdict : std::uint8_t
    {
        accepted,  // 赢
        superseded // 让位
    };

    // ═══════════════════════════════════════════════════════════════════
    // 0.1 FSM：引擎只问这五个问题（答案全在这里，别处不再探成员）
    //
    //   一个识别器和引擎的**全部**接触面就是这五条，多一条都没有：
    //     ① 一帧喂给你，你说什么？        ← 必须
    //     ② 只有时间过去，你说什么？      ← 可选（默认：什么都没发生）
    //     ③ 清掉你的内部状态              ← 可选（默认：什么也不做）
    //     ④ 你交出的载荷                  ← 可选（默认：无载荷）
    //     ⑤ 你吃哪种帧                    ← 必须
    //
    //   为什么 ⑤ 是必须的：它不进任何运行期数据（通道是 scope_of 按 ① 算的），
    //   它的用处是在**定义类型那一刻**检查你：
    //     · concept machine 靠它把 ① 的签名钉住（否则要等第一次 feed 才发现）
    //     · validate 靠它查"关系两端同通道"（mouse 的 kind 不该和 key 的 kind 比高下）
    // ═══════════════════════════════════════════════════════════════════
    namespace fsm
    {
        // ⑤ 你吃哪种帧
        template <class M>
        concept has_frame = requires { typename M::frame_type; };

        template <class M>
        using frame_t = typename M::frame_type;

        // ① 一帧说一句话：必须，正好回 step_t，且 noexcept
        //   （stage 的整条判决链是 noexcept 的，会抛的 step 只会变成 terminate，
        //     所以在编译期就拦住，而不是等运行期）
        template <class M, class Frame>
        concept can_step = requires(M &m, const Frame &f, time_point t) {
            { m.feed(f, t) } noexcept -> std::same_as<step_t>;
        };

        template <class M, class Frame>
        constexpr step_t feed(M &m, const Frame &f,
                              time_point t) noexcept(noexcept(m.feed(f, t)))
        {
            return m.feed(f, t);
        }

        // ② 只过时间：可选；没有就是"时间对我没影响"。
        //    注意 has_/ok 成对：有 tick 但会抛 ⇒ ok 为假 ⇒ 编译不过，
        //    而不是被当成"没有 tick"悄悄忽略。
        template <class M>
        concept has_tick = requires(M &m, time_point t) { m.tick(t); };

        template <class M>
        concept tick_ok = !has_tick<M> || requires(M &m, time_point t) {
            { m.tick(t) } noexcept; // 只要求能调 + noexcept；返回什么由 fsm 自己定
        };

        template <class M>
        constexpr void tick(M &m, time_point t) noexcept
        {
            if constexpr (has_tick<M>)
                m.tick(t);
        }

        // ③ 清状态：可选；没有就是"我没状态可清"；有就必须 noexcept
        template <class M>
        concept has_reset = requires(M &m) { m.reset(); };

        template <class M>
        concept reset_ok = !has_reset<M> || requires(M &m) {
            { m.reset() } noexcept;
        };

        template <class M>
        constexpr void reset(M &m) noexcept
        {
            if constexpr (has_reset<M>)
                m.reset();
        }

        // ④ 载荷：可选
        template <class M>
        concept can_payload = requires(const M &m) { m.payload; };

        template <class M>
        using payload_t =
            std::remove_cvref_t<decltype(std::declval<const M &>().payload)>;

        template <class M>
        constexpr auto payload(const M &m) noexcept -> const payload_t<M> &
        {
            return m.payload;
        }

        // 一个 FSM = 默认构造 + 说了吃哪种帧 + 那种帧能 step（noexcept）
        //            + 可选的 tick / reset 若存在也必须 noexcept
        template <class M>
        concept machine = std::default_initializable<M> && has_frame<M> &&
                          can_step<M, frame_t<M>> && tick_ok<M> && reset_ok<M>;
    } // namespace fsm

    // 判决的两种结果，打印用（和 event 做邻居）
    constexpr std::string_view to_string(verdict v) noexcept
    {
        return v == verdict::accepted ? "acc" : "sup";
    }

    // 一条观测记录（定长、可拷贝、不分配）
    struct event
    {
        std::string_view name{};
        verdict v{};
        time_point at{};    // 判决时刻
        time_point begin{}; // 交互开始
        time_point end{};   // 交互结束
    };

    // ── 观测：只写不读，不参与判决。整块可换 ──
    //    trace    ：定长记录，满了计数丢弃（不覆盖、不分配）
    //    no_trace ：生产环境不记，零字段零开销
    struct trace
    {
        static constexpr std::size_t capacity = 64;
        std::array<event, capacity> buf{};
        std::size_t n = 0, dropped_n = 0;
        bool in_feed_;

        constexpr void push(const event &e) noexcept
        {
            if (n < capacity)
                buf[n++] = e;
            else
                ++dropped_n;
        }
        constexpr std::span<const event> events() const noexcept
        {
            return {buf.data(), n};
        }
        constexpr void clear() noexcept
        {
            n = 0;
        }
        constexpr std::size_t dropped() const noexcept
        {
            return dropped_n;
        }
    };

    struct no_trace
    {
    };

    namespace detail
    {
        // 重入闸：只有断言用，feed/tick 各一行
        struct feed_guard
        {
            bool *flag = nullptr;
            explicit feed_guard(bool *f) : flag(f)
            {
                assert(!*flag && "feed/tick 不可重入：handler 里不要再喂");
                *flag = true;
            }
            constexpr ~feed_guard() noexcept
            {
                *flag = false;
            }
            feed_guard(const feed_guard &) = delete;
            feed_guard &operator=(const feed_guard &) = delete;
        };

        template <class T>
        consteval static_string type_name()
        {
            return static_string{std::define_static_string(
                std::string{std::meta::display_string_of(^^T)})};
        }
    } // namespace detail

    // ═══════════════════════════════════════════════════════════════════
    // 1. plan：编译期的声明表（L1 同态律）
    //      kinds  : 每片叶子的名字 / 通道 / 兜底
    //      awaits : (i,j) —— i 是 j 的抽象版
    //      excl   : (i,j) —— i 与 j 不能共存
    // ═══════════════════════════════════════════════════════════════════
    struct kind_desc
    {
        static_string name{};    // kind 的身份：日志、蓝图、连通块都用它（要求唯一）
        static_string fn_name{}; // 回调的名字：invoke<fn_name>，可以多个 kind 共用一个
        static_string frame{};
        std::chrono::nanoseconds hold{}; // 兜底时限：是一段时间，不是一个数

        friend constexpr bool operator==(const kind_desc &, const kind_desc &) = default;
    };

    struct plan_data
    {
        std::vector<kind_desc> kinds;
        // 下标一律 index_t：它只可能来自 kinds.size() 或循环计数，
        // 负数不是"约定上不该有"，而是根本产生不出来 —— 也没有 -1 哨兵的需求。
        std::vector<std::pair<index_t, index_t>> awaits;
        std::vector<std::pair<index_t, index_t>> excl;

        // 拼接：b 追加到a, b 的所有编号 + base （换坐标系）
        friend consteval plan_data operator+(plan_data a, const plan_data &b)
        {
            const index_t base = static_cast<index_t>(a.kinds.size());
            a.kinds.append_range(b.kinds);
            for (auto [i, j] : b.awaits)
                a.awaits.push_back({i + base, j + base});
            for (auto [i, j] : b.excl)
                a.excl.push_back({i + base, j + base});
            return a;
        }
    };

    // 叶子 = 纯类型：两个名字 + 状态机类型 + 吃哪种帧 + 兜底。
    //   它不持有状态机：状态机住在 ui 的扁平元组里。
    //
    //   两个名字各管一摊，不耦合：
    //     Name   —— kind 的身份。进日志、蓝图、报错；要求唯一（它是"谁"）。
    //     FnName —— 回调的名字。sink 只会做 invoke<FnName>(payload)；
    //               多个 kind 可以共用同一个回调（比如都是 on_click，
    //               各自带不同 payload，由回调自己 if constexpr 分流）。
    //   不写 FnName 时就等于 Name：最简单的用法不用提它。
    template <fsm::machine Fsm>
    struct leaf
    {
        using machine = Fsm; // 叶子协议：我的状态机类型（反射靠它认出叶子）
        static constexpr auto name = Fsm::name;       // kind 的身份
        static constexpr auto fn_name = Fsm::fn_name; // 回调的名字（invoke<fn_name>）
        static constexpr auto hold_ms = Fsm::hold_ms;

        // 兜底时限用无符号毫秒：不可能为负这件事写在类型里
        static constexpr std::chrono::nanoseconds hold =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::milliseconds{static_cast<std::int64_t>(hold_ms)});

        static consteval plan_data plan()
        {
            return plan_data{
                .kinds = {{name, fn_name, detail::type_name<fsm::frame_t<Fsm>>(), hold}},
                .awaits = {},
                .excl = {}};
        }
    };

    // 并行：不加任何关系
    template <class... Rs>
    struct all_of
    {
        using children_t = std::tuple<Rs...>; // 组合子协议（仅类型）

        static consteval plan_data plan()
        {
            plan_data p;
            ((p = p + Rs::plan()), ...);
            return p;
        }
    };

    // 互斥：任意两个不同子树之间填满 excl
    template <class... Rs>
    struct one_of
    {
        using children_t = std::tuple<Rs...>;

        static consteval plan_data plan()
        {
            constexpr std::size_t K = sizeof...(Rs); // 几个孩子：这是"多少个"
            std::array<plan_data, K> parts{};
            index_t k = 0;
            ((parts[k++] = Rs::plan()), ...);

            plan_data p;
            std::array<index_t, K + 1> off{};
            for (std::size_t i = 0; i < K; ++i)
            {
                off[i + 1] = off[i] + static_cast<index_t>(parts[i].kinds.size());
                p = p + parts[i];
            }
            for (index_t a = 0; a < K; ++a)
                for (index_t b = a + 1; b < K; ++b)
                    for (index_t x = off[a]; x < off[a + 1]; ++x)
                        for (index_t y = off[b]; y < off[b + 1]; ++y)
                            p.excl.push_back({x, y});
            return p;
        }
    };

    // 细化：A 里每片叶子都等 B 里每片叶子的失败
    template <class A, class B>
    struct refines
    {
        using children_t = std::tuple<A, B>;

        static consteval plan_data plan()
        {
            auto pa = A::plan();
            auto pb = B::plan();
            const index_t na = static_cast<index_t>(pa.kinds.size());
            auto p = pa + pb; // NOTE: 先简单拼接，再在下面添加边
            // NOTE: A 的元素  全部 等 B 的元素
            for (index_t i = 0; i < na; ++i)
                for (index_t j = na; j < static_cast<index_t>(p.kinds.size()); ++j)
                    p.awaits.push_back({i, j});
            return p;
        }
    };

    // ═══════════════════════════════════════════════════════════════════
    // 2. validate：坏结构编译不过（00-goal 第 3 条）
    //    ① 无自环 ② 关系两端同通道 ③ 细化×互斥不矛盾 ④ awaits 无环
    //
    //    查重只查 kind 的 name（它是身份：日志、蓝图、报错都靠它区分）。
    //    fn_name（回调名）**不查重** —— 多个 kind 共用一个回调是合法用法。
    // ═══════════════════════════════════════════════════════════════════
    consteval void validate(const plan_data &p)
    {
        const std::size_t n = p.kinds.size(); // 多少个 kind：这是长度
        auto kind = [&](index_t i) -> const kind_desc & {
            return p.kinds[i];
        };
        auto same_frame = [&](index_t i, index_t j) {
            return kind(i).frame.view() == kind(j).frame.view();
        };

        for (index_t i = 0; i < n; ++i)
            for (index_t j = i + 1; j < n; ++j)
                if (kind(i).name.view() == kind(j).name.view())
                    throw "dup kind name";

        for (auto [i, j] : p.awaits)
        {
            if (i == j)
                throw "awaits self-loop";
            if (!same_frame(i, j))
                throw "awaits cross-frame";
        }
        for (auto [i, j] : p.excl)
        {
            if (i == j)
                throw "excl self-loop";
            if (!same_frame(i, j))
                throw "excl cross-frame";
        }
        for (auto [i, j] : p.awaits)
            for (auto [x, y] : p.excl)
                if ((i == x && j == y) || (i == y && j == x))
                    throw "refines x excl conflict";

        // awaits 无环：按等待数做 Kahn。计数用 size_t，用 done 表代替 -1 哨兵。
        std::vector<std::size_t> indeg(n, 0); // 等待数：计数，不是下标
        std::vector<bool> done(n, false);
        for (auto [i, j] : p.awaits)
            ++indeg[i];
        for (std::size_t left = n; left > 0;)
        {
            bool prog = false;
            for (index_t i = 0; i < n; ++i)
                if (!done[i] && indeg[i] == 0)
                {
                    done[i] = true;
                    --left;
                    prog = true;
                    for (auto [x, y] : p.awaits)
                        if (y == i)
                            --indeg[x];
                }
            if (!prog)
                throw "refines cycle";
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // 3. bset：一张位图，也是这套语义里唯一的"集合"
    //
    //    ── 位 i 的含义由**使用位置**决定，不在类型里 ──
    //      law::beats[i]   第 j 位 = "i 打得赢 j"      （行 = 我赢谁）
    //      alive         第 i 位 = "第 i 个 kind 活着（有假设或已有结论）"
    //      pending       第 i 位 = "有结论但被更强的挡着，挂起中"
    //      done          第 i 位 = "本帧有结论"
    //      scope         第 i 位 = "本帧被喂到过（属于这条通道）"
    //
    //    ── 为什么是它：裁决的每一步都是集合运算，一一对应 ──
    //      blocked = ∪ 行(活着的)        →  for j: blocked |= beats[j]
    //      winners = cand ∖ blocked      →  cand & ~blocked
    //      prey    = ∪ 行(赢家) ∩ alive  →  for i: prey |= beats[i]; prey &= alive
    //    "谁赢 i"没有单独存：用行并回答（∪行(j) 就是"能赢这些人的全体"），
    //    所以 blocked 只需要行，不需要转置表。
    //
    //    ── 关于 64：S 没有 64 这个上限 ──
    //      位数 = ⌈S/64⌉ 个字，64 只是一个字的宽度（S=70 就是两个字，S=6 是一个字）。
    //      代价是平方的：beats 占 S×⌈S/64⌉×8 字节；每帧 blocked 要 S×⌈S/64⌉ 次字或；
    //      编译期 closure 是 S²×⌈S/64⌉。先撞墙的其实不是矩阵，是 validate 的 Kahn
    //      （它每个节点重扫一遍 awaits 列表；S=200 会撞 GCC 的 constexpr 运算上限）。
    //
    //    ── 约定（改代码时请保持）──
    //      · 只由 set/clr 产生，合法区间永远是 [0,S)
    //      · ~ 只在 [0,S) 内闭合（尾字抹零）⇒ 任何 x & ~y 都不需要前提
    //      · 判空用 none()；不要用 !has(0) 代替
    //      · | 是并、& 是交、差集写 x & ~y（不是 x ∖ y）
    // ═══════════════════════════════════════════════════════════════════
    template <std::size_t S>
    struct bset
    {
        static constexpr std::size_t W = (S + 63) / 64;
        std::array<std::uint64_t, W> w{};

        constexpr void set(index_t i) noexcept
        {
            w[i / 64] |= std::uint64_t{1} << (i % 64);
        }
        constexpr void clr(index_t i) noexcept
        {
            w[i / 64] &= ~(std::uint64_t{1} << (i % 64));
        }
        // "这个集合里有 i 吗"：读一位，不写任何东西。
        // 同一个函数在三种角色上意思不同，由上下文给：
        //   law::beats[i].has(j)  → i 打得赢 j 吗
        //   alive.has(i)          → 第 i 个 kind 还活着吗
        //   scope.has(i)          → 本帧喂到这一列了吗
        constexpr bool has(index_t i) const noexcept
        {
            return (w[i / 64] >> (i % 64)) & 1;
        }
        // 空吗？"没人能赢"、"没有结论"、"表清空" 都用它
        constexpr bool none() const noexcept
        {
            for (auto x : w)
                if (x)
                    return false;
            return true;
        }
        constexpr bset &operator|=(const bset &b) noexcept
        {
            for (std::size_t k = 0; k < W; ++k)
                w[k] |= b.w[k];
            return *this;
        }
        constexpr bset &operator&=(const bset &b) noexcept
        {
            for (std::size_t k = 0; k < W; ++k)
                w[k] &= b.w[k];
            return *this;
        }
        friend constexpr bset operator|(bset a, const bset &b) noexcept
        {
            return a |= b;
        }
        friend constexpr bset operator&(bset a, const bset &b) noexcept
        {
            return a &= b;
        }
        friend constexpr bset operator~(bset a) noexcept
        {
            for (std::size_t k = 0; k < W; ++k)
                a.w[k] = ~a.w[k];
            // 补集只在 [0,S) 里闭合：高位一律抹掉，
            // 调用方就不用替它记着"左边那个操作数是干净的"
            if constexpr (S % 64 != 0)
                a.w[W - 1] &= (std::uint64_t{1} << (S % 64)) - 1;
            return a;
        }
        constexpr bool operator==(const bset &) const noexcept = default;
    };
    // ═══════════════════════════════════════════════════════════════════
    // 4. 树的形状（shape）—— 树只是类型，不产生任何对象
    //
    //    一个类型说清三件事，全部是编译期：
    //      n          : 几片叶子
    //      fsms       : 它们的状态机元组（声明序）
    //      each_leaf  : 按声明序把 (下标 I, 叶子类型) 交给调用方（bind 用）
    //                   I 是编译期常量，和 sinks[] / 矩阵行号是同一个 i
    //
    //    "这棵树认不认识帧 F"不在这里另开一个成员：用 scope_of<Tree,F>() 回答，
    //    它就是"哪些列认识 F"的位图，空就是不认识（见下面 4.3）。
    // ═══════════════════════════════════════════════════════════════════
    namespace detail
    {
        // 组合子 = 声明了成员类型别名 children_t 的类。
        // 子节点 = children_t（std::tuple<Rs...>）的模板实参。
        consteval std::vector<std::meta::info> child_types(std::meta::info node)
        {
            if (!std::meta::is_class_type(node))
                return {};

            constexpr auto ctx = std::meta::access_context::current();
            for (auto m : std::meta::members_of(node, ctx))
            {
                if (!std::meta::has_identifier(m) ||
                    std::meta::identifier_of(m) != "children_t")
                    continue;
                if (!std::meta::is_type_alias(m))
                    continue;

                auto tup = std::meta::dealias(m); // std::tuple<Rs...>
                std::vector<std::meta::info> out;
                for (auto a : std::meta::template_arguments_of(tup))
                    if (std::meta::is_type(a))
                        out.push_back(std::meta::remove_cvref(a));
                return out;
            }
            return {};
        }

        // 按声明序收集叶子（叶子 = 声明了 machine 的节点）
        consteval std::vector<std::meta::info> leaf_infos(std::meta::info node)
        {
            constexpr auto ctx = std::meta::access_context::current();
            std::vector<std::meta::info> out;
            for (auto m : std::meta::members_of(node, ctx))
                if (std::meta::has_identifier(m) &&
                    std::meta::identifier_of(m) == "machine")
                {
                    out.push_back(node);
                    return out;
                }

            for (auto c : child_types(node))
                for (auto x : leaf_infos(c))
                    out.push_back(x);
            return out;
        }

        template <class... Ls>
        struct shape
        {
            static constexpr std::size_t n = sizeof...(Ls);
            using fsms = std::tuple<typename Ls::machine...>;

            template <class F>
            static constexpr void each_leaf(F &&f)
            {
                [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                    (f.template operator()<Is, Ls>(), ...);
                }(std::make_index_sequence<sizeof...(Ls)>{});
            }
        };

        template <class Tree>
        using shape_of = typename[:std::meta::substitute(^^shape, leaf_infos(^^Tree)):];
    } // namespace detail

    // 结构的公开自查数字
    template <class Tree>
    inline constexpr std::size_t leaf_count_v = detail::shape_of<Tree>::n;

    // ═══════════════════════════════════════════════════════════════════
    // 4.3 scope_of<Tree, Frame>()：这一帧会喂到哪些列
    //
    //    通道不是运行期问出来的，是**按帧类型在编译期算出来的**：
    //    哪片叶子的 machine 能接受 Frame，就置哪一位。
    //    settle 的 scope 就是这个位图；本帧没走到的列，一个位都不动。
    //    （tick 没有帧类型，用 stage::all()：时间对所有列一起走。）
    // ═══════════════════════════════════════════════════════════════════
    template <class Tree, class Frame>
    consteval bset<leaf_count_v<Tree>> scope_of() noexcept
    {
        using fsms_t = typename detail::shape_of<Tree>::fsms;
        constexpr std::size_t S = leaf_count_v<Tree>;
        bset<S> m{};
        template for (constexpr auto I : std::ranges::views::indices(S))
        {
            if constexpr (fsm::can_step<std::tuple_element_t<I, fsms_t>, Frame>)
                m.set(static_cast<index_t>(I));
        }
        return m;
    }

    // ═══════════════════════════════════════════════════════════════════
    // 4.2 一片树最多多少片叶子：硬上限 64
    //
    //   64 不是"位图装不下"——位图是 ⌈S/64⌉ 个字，任意 S 都放得下。
    //   它是**当前实现的编译期上限**，实测出来的：
    //     稠密 refines（A 侧 S/2 × B 侧 S/2，边数 = S²/4）
    //       S=70 → 能编译
    //       S=96 → 'constexpr' operation count exceeds limit of 33554432
    //     爆点不在矩阵（closure 只要 S²×⌈S/64⌉），在 validate 的 Kahn：
    //     它每处理一个节点就把整个 awaits 列表重扫一遍，约 O(n²·E)。
    //
    //   所以这里写死 64（一个 uint64 位图正好装下），宁可编译期一句人话报错，
    //   也不要让人撞一屏模板展开。要放宽：先把 validate 的 Kahn 改成 O(n+E)，
    //   再把这个数调大。
    // ═══════════════════════════════════════════════════════════════════
    inline constexpr std::size_t max_kinds = 64;

    // 上限与索引类型的关系（两者必须一起看）：
    //   S ≤ max_kinds，而 max_kinds 必须塞得进 index_t。
    //   现在 index_t = size_t（64 位）、max_kinds = 64，远够。
    //   哪天要把 index_t 收窄成 uint8/uint16，这条断言会先拦住；
    //   改 max_kinds 时同理。两者是一个"对"，不该分开动。
    static_assert(max_kinds <= std::numeric_limits<index_t>::max(),
                  "max_kinds 超过 index_t 的容量：索引类型和上限必须一起改");

    // ═══════════════════════════════════════════════════════════════════
    // 4.5 传递闭包（位行 Warshall）：i→k、k→j ⇒ i→j
    //     单独拎出来，是因为它可以被单独喂数据测试（见 test_matrix_series.cpp B4）。
    //     在 law_of::beats 里它跑一次；测试里可以用一条手搭的 3 链看它确实补边。
    // ═══════════════════════════════════════════════════════════════════
    template <std::size_t S>
    consteval std::array<bset<S>, S> closure(std::array<bset<S>, S> m)
    {
        for (std::size_t k = 0; k < S; ++k)
            for (std::size_t i = 0; i < S; ++i)
                if (m[i].has(k))
                    m[i] |= m[k];
        return m;
    }

    // ═══════════════════════════════════════════════════════════════════
    // 5. 法则：一张矩阵说完所有裁决
    //
    //    beats[i] = 第 i 行 = "i 打得赢谁"。两条来源，都是声明里已经说过的：
    //      refines<A,B> ：B 的每片叶子打败 A 的每片叶子（更具体的赢）
    //      one_of<R...> ：先声明的打败后声明的（互斥对之间唯一的平局规则）
    //    再跑一遍传递闭包，矩阵自带传递性 —— 运行期不再推导任何东西。
    //
    //    构造完断言对角线为空 ⇒ 矩阵是**严格偏序** ⇒ 极大元唯一
    //    ⇒ 下面的选择律与"谁先被看"无关。这就是自然选择。
    // ═══════════════════════════════════════════════════════════════════
    template <class Tree>
    struct law_of
    {
        static constexpr std::size_t S = leaf_count_v<Tree>;

        // 上限先断言：S 超过 max_kinds 就别往下算了（理由见 max_kinds 的注释）
        static_assert(S >= 1, "一片叶子都没有的树没有意义");
        static_assert(S <= max_kinds,
                      "叶子太多：上限见 uhi::max_kinds（=64）。"
                      "位图放得下，是 validate 的 Kahn 会撞编译期运算上限");

        // 行：i 打得赢谁
        static constexpr std::array<bset<S>, S> beats = []() consteval {
            std::array<bset<S>, S> m{};
            const auto p = Tree::plan();
            validate(p);                 // 无自环 / 边两端同通道 / awaits 无环
            for (auto [a, b] : p.awaits) // a 等 b ⇒ b 赢 a
                m[b].set(a);
            for (auto [a, b] : p.excl) // 互斥对是无序的：谁先声明谁赢
            {
                if (b < a)
                    std::swap(a, b); // 让 a 永远是"先声明的那一个"
                m[a].set(b);         // 于是直接写：先声明的赢
            }
            m = closure<S>(m); // 传递闭包（位行 Warshall）
            for (index_t i = 0; i < S; ++i)
                if (m[i].has(i))
                    throw "beats 有环：refines / one_of 互相指向，结构不成立";
            // L2：连通块内必须同通道。旧版靠"不同格是不同对象"把它做成**结构**保证；
            // 矩阵版所有 kind 在一张表里，所以这里至少把它焊成编译期**检查**保证：
            // 跨通道的边一旦漏出来，直接编译不过，而不是等运行期串台。
            for (index_t i = 0; i < S; ++i)
                for (index_t j = 0; j < S; ++j)
                    if (m[i].has(j) && p.kinds[i].frame.view() != p.kinds[j].frame.view())
                        throw "beats 跨通道：矩阵里出现了不同通道之间的边";
            return m;
        }();

        // 每槽的名字（播报用）—— 和矩阵同一个下标。
        //   static_string 本体只有一个 const char*（8 B），string_view 是 16 B；
        //   名字很短、播报又稀疏，view() 时那次 strlen 可忽略，换来这张表减半。
        static constexpr std::array<static_string, S> names = []() consteval {
            std::array<static_string, S> r{};
            const auto p = Tree::plan();
            for (index_t i = 0; i < S; ++i)
                r[i] = p.kinds[i].name;
            return r;
        }();

        // 兜底时限：活这么久还没落地（没结论，或有结论一直被挡），就不是它
        static constexpr std::array<std::chrono::nanoseconds, S> hold = []() consteval {
            std::array<std::chrono::nanoseconds, S> r{};
            const auto p = Tree::plan();
            for (index_t i = 0; i < S; ++i)
                r[i] = p.kinds[i].hold;
            return r;
        }();

        // ── 连通块："格"不再是结构，只是矩阵上的连通块 ──
        //    两个人之间**只要有一条 beats 边**（哪个方向都算），就是同一块。
        //    例：beats[dbl]={click}、beats[triple]={click,dbl}
        //        ⇒ click-dbl、click-triple、dbl-triple 都连着 ⇒ 三人在一块。
        //    和名字是否唯一无关（那是 validate 的 dup 检查）。
        //
        //    算法就是并查集：parent[] 是一棵棵树，root(x) 顺着往上找组代表；
        //    有一条边就把两人的组合并。最后"自己是自己代表"的个数 = 块数。
        static constexpr std::size_t cell_count = []() consteval {
            std::array<index_t, S> parent{};
            for (index_t i = 0; i < S; ++i)
                parent[i] = i; // 一开始一人一组
            auto root = [&](index_t x) {
                while (parent[x] != x)
                    x = parent[x] = parent[parent[x]]; // 顺手把路径压平
                return x;
            };
            for (index_t i = 0; i < S; ++i)
                for (index_t j = 0; j < S; ++j)
                    if (beats[i].has(j) && root(i) != root(j))
                        parent[root(i)] = root(j); // 有边 ⇒ 两组并成一组
            std::size_t n = 0;                     // 块数 = 代表自己的那些组
            for (index_t i = 0; i < S; ++i)
                if (root(i) == i)
                    ++n;
            return n;
        }();
    };

    // ═══════════════════════════════════════════════════════════════════
    // 5.5 裁决代数：三个纯函数，只吃位图
    //
    //      blocked = ∪ 行(活着的 ∧ 本帧)
    //      winners = 候选 ∖ blocked
    //      prey    = ∪ 行(winners) ∧ 活着的 ∧ 本帧 ∖ winners
    //
    //    "自然选择"就是这三行。它们不含时间、不含状态、不碰回调，
    //    所以可以脱离树、脱离 ui、脱离帧，单独喂位图测（见 test B10）。
    //    stage 只负责按帧的节奏把它们串起来。
    // ═══════════════════════════════════════════════════════════════════
    template <class Tree>
    using tree_bset = bset<leaf_count_v<Tree>>;

    // 谁被挡住：活着的、本帧看得见的那些人的行并起来
    template <class Tree>
    constexpr tree_bset<Tree> blocked_by(const tree_bset<Tree> &alive,
                                         const tree_bset<Tree> &scope) noexcept
    {
        using law = law_of<Tree>;
        tree_bset<Tree> b{};
        for (index_t j = 0; j < law::S; ++j)
            if (alive.has(j) && scope.has(j))
                b |= law::beats[j];
        return b;
    }

    // 谁能落地：候选里没被挡住的那些（矩阵是严格偏序 ⇒ 这就是极大元集）
    template <class Tree>
    constexpr tree_bset<Tree> winners_of(const tree_bset<Tree> &cand,
                                         const tree_bset<Tree> &alive,
                                         const tree_bset<Tree> &scope) noexcept
    {
        return cand & ~blocked_by<Tree>(alive, scope);
    }

    // 谁被吃掉：赢家的行 ∧ 活着的 ∧ 本帧 ∖ 赢家自己
    template <class Tree>
    constexpr tree_bset<Tree> prey_of(const tree_bset<Tree> &winners,
                                      const tree_bset<Tree> &alive,
                                      const tree_bset<Tree> &scope) noexcept
    {
        using law = law_of<Tree>;
        tree_bset<Tree> p{};
        for (index_t i = 0; i < law::S; ++i)
            if (winners.has(i))
                p |= law::beats[i];
        p &= alive & scope;
        p &= ~winners;
        return p;
    }

    // ═══════════════════════════════════════════════════════════════════
    // 6. 现场表：一列一个 kind。运行期唯一的可变状态。
    //      alive    ：活着（有假设，或已有结论还没落地）
    //      pending  ：有结论、但被更强的挡着（挂起，下一帧再试）
    //      begin    ：第一次活着的时刻（active 或 done 都算）。**参与裁决**：
    //                 兜底（hold）从它开始算 —— "活着这么久还没落地，就不是它"。
    //                 它同时是事件跨度的起点，所以一列两个用途。
    //      end      ：第一次有结论的时刻。挂起（pending）期间不覆盖；
    //                 只用于播报，不参与裁决。
    //    一次交互 = 第一次活着 → 第一次有结论。重新武装、被挡、再完成，
    //    跨度不变，变的只有播报时刻 at（R3/R4 各钉一条）。
    //
    //    scope 是"这一帧算哪几条通道"。一个鼠标帧不该顺手结算键盘的挂起候选；
    //    旧版靠"不同格是不同对象"做到这件事，这里靠 scope 做到同一件事：
    //    本帧看不见的列，一个位都不动。
    //
    //    四个字段都是 public：想看表就直接看（test_matrix_series.cpp 每帧打一份）。
    // ═══════════════════════════════════════════════════════════════════
    template <class Tree>
    struct stage
    {
        using law = law_of<Tree>;
        static constexpr std::size_t S = law::S;

        bset<S> alive{}, pending{};
        std::array<time_point, S> begin{}, end{};

        // 全部通道（tick 用：时间对所有通道一起走）
        static consteval bset<S> all() noexcept
        {
            bset<S> m{};
            for (index_t i = 0; i < S; ++i)
                m.set(i);
            return m;
        }

        // 【新】裁决不再写在 stage 里：stage 只保留"对外展示"的四列（alive/pending/begin/end），
        //        它们是 run_arbiter 每帧从各 fsm 现问现算出来的投影，不是权威状态。
    };

    // ═══════════════════════════════════════════════════════════════════
    // 7. sink：{arena, thunk} 两字。构造、载荷探测、thunk 都在它自己身上
    // ═══════════════════════════════════════════════════════════════════
    struct sink
    {
        void *self = nullptr;
        void (*fn)(void *, const void *) noexcept = nullptr;

        constexpr void operator()(const void *p) const noexcept
        {
            if (fn)
                fn(self, p);
        }

        // "这个叶子交不交载荷 / 交的是什么类型"：问 fsm 那一节，不在这里探成员
        template <class Leaf>
        static constexpr bool has_payload = fsm::can_payload<typename Leaf::machine>;

        template <class Leaf>
        using payload_t = fsm::payload_t<typename Leaf::machine>;

        // 回调满足两个条件才许绑：能这么调 + 是 noexcept。
        //   两件事一条断言说清，两个分支共用（payload / 空参）。
        template <class Arena, class Leaf, class... Args>
        static constexpr bool callable_noexcept =
            requires(Arena &a, Args &&...args) {
                a.template invoke<Leaf::fn_name>(std::forward<Args>(args)...);
            } &&
            noexcept(std::declval<Arena &>().template invoke<Leaf::fn_name>(
                std::declval<Args>()...));

        template <class Arena, class Leaf>
        static void call(void *self, const void *p) noexcept
        {
            Arena &a = *static_cast<Arena *>(self);
            if constexpr (has_payload<Leaf>)
            {
                static_assert(
                    callable_noexcept<Arena, Leaf, const payload_t<Leaf> &>,
                    "on_<fn_name> 必须是可调且 noexcept 的：回调是无异常的函数指针");
                a.template invoke<Leaf::fn_name>(
                    *static_cast<const payload_t<Leaf> *>(p));
            }
            else
            {
                assert(p == nullptr && "空参，为什么还传递参数过来?");
                static_assert(
                    callable_noexcept<Arena, Leaf>,
                    "on_<fn_name> 必须是可调且 noexcept 的：回调是无异常的函数指针");
                a.template invoke<Leaf::fn_name>();
            }
        }

        template <class Arena, class Leaf>
        static sink build(Arena &a) noexcept
        {
            return sink{&a, &call<Arena, Leaf>};
        }
    };

    // ═══════════════════════════════════════════════════════════════════
    // 9. ui：纯功能。一个坐标系装下全部
    //      fsms[i] / 现场第 i 位 / sinks[i] / names[i] / beats[i]
    //      全是同一个 i = 声明序。没有第二套编号，就不需要映射表。
    //
    //    ui 只管判决：喂帧、过时间、把赢家交给 sink。
    //    它**不**记录任何东西，也不打印任何东西 —— 观测和调试都在 debug_ui 里（第 10 节）。
    //    想记录时，feed/tick 多收一个 trace*，它就顺手记一份；不给就不记。
    // ═══════════════════════════════════════════════════════════════════
    template <class Tree>
    struct ui
    {
        using shape = detail::shape_of<Tree>;
        using law = law_of<Tree>;
        static constexpr std::size_t S = shape::n;
        using fsms_t = typename shape::fsms;

        fsms_t fsms{};               // 识别器（声明序）
        stage<Tree> st{};            // 现场表
        std::array<sink, S> sinks{}; // 落地表

        // 判决落地（唯一副作用点）。Flat 是编译期常量。
        //   t 非空 ⇒ 顺带记一条事件（观测是"要就给、不要就零"）
        template <std::size_t Flat>
        constexpr void on_verdict(bool accepted, time_point b, time_point e,
                                  time_point at, trace *t) noexcept
        {
            const void *p = nullptr;
            if constexpr (fsm::can_payload<std::tuple_element_t<Flat, fsms_t>>)
                p = static_cast<const void *>(&fsm::payload(std::get<Flat>(fsms)));
            if (accepted)
                sinks[Flat](p); // 载荷在 reset 之前取
            if (t != nullptr)
                t->push(event{law::names[Flat].view(),
                              accepted ? verdict::accepted : verdict::superseded, at, b,
                              e});
        }

        template <std::size_t Flat>
        constexpr void reset_kind() noexcept
        {
            fsm::reset(std::get<Flat>(fsms)); // 没有 reset 的什么都不做
        }

        // 延迟绑定：只用叶子的类型（名字 + 载荷类型），不用对象。
        //   I 是编译期常量，不是运行期计数器：下标就是每个函数指针本身的地址偏移。
        template <class Arena>
        constexpr void bind(Arena &a) noexcept
        {
            shape::each_leaf([&]<std::size_t I, class Leaf>() noexcept {
                sinks[I] = sink::template build<Arena, Leaf>(a);
            });
        }

        // ── 输入：推进一帧 ──
        // 通道在编译期就定死：哪些列认识 Frame，就是哪些列会被喂到。
        template <class Frame>
        constexpr void feed(const Frame &f, time_point now, trace *obs = nullptr) noexcept
        {
            constexpr bset<S> scope = scope_of<Tree, Frame>();
            static_assert(
                !scope.none(),
                "这棵树里没有任何识别器认识这个帧类型（检查 machine::frame_type）");

            // 只把帧投给能吃的列（编译期裁好）；状态由 fsm 自己保存。
            //   hold = "时间过去了/这帧与我无关，状态不变"，不能拿它覆盖上一帧的
            //   active/done：否则挂起的候选（例如等 dbl 窗口的 click）会被抹掉。
            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                if constexpr (scope.has(static_cast<index_t>(I)))
                {
                    auto &m = std::get<I>(fsms);
                    const step_t s = m.feed(f, now);
                    if (s != step_t::hold)
                        m.cur = s;
                }
            }
            run_arbiter(now, obs);
        }

        // 只过了时间：让所有识别器看一眼时钟，然后重新裁决一次
        void tick(time_point now, trace *obs = nullptr) noexcept
        {
            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                if constexpr (requires { std::get<I>(fsms).tick(now); })
                {
                    std::get<I>(fsms).tick(now); // fsm 自己改 cur
                }
            }
            run_arbiter(now, obs);
        }

      private:
        // ── 裁决：只问、只放行。stage 里不存任何 fsm 状态 ──────────────
        void run_arbiter(time_point now, trace *obs) noexcept
        {
            std::array<step_t, S> sig{};
            template for (constexpr auto I : std::ranges::views::indices(S)) sig[I] =
                std::get<I>(fsms).step(); // ① 现问

            // ①.5 收票：aborted = 放弃。必须复位识别器并退场；
            //     旧版把它当"非 idle ⇒ 还在场"，于是超时的 dbl/triple
            //     （或没拖起来的 drag_*）永远用 beats 行挡着 click。
            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                if (sig[I] == step_t::aborted)
                {
                    reset_kind<I>();
                    std::get<I>(fsms).cur = step_t::idle;
                    sig[I] = step_t::idle;
                    st.begin[I] = time_point{};
                }
            }

            const bset<S> was_alive = st.alive;

            // ② 在场 = active / done。idle、aborted、hold 都不算。
            //    第一次在场的列把 begin 定在 now（必须赶在播报之前）。
            bset<S> in_play{}, want{};
            for (index_t i = 0; i < S; ++i)
            {
                if (sig[i] == step_t::active || sig[i] == step_t::done)
                {
                    in_play.set(i);
                    if (!was_alive.has(i))
                        st.begin[i] = now;
                }
                else if (sig[i] == step_t::idle)
                {
                    st.begin[i] = time_point{}; // 没有假设了：下一次交互重新起算
                }
                if (sig[i] == step_t::done)
                    want.set(i);
            }
            bset<S> blocked{};
            for (index_t j = 0; j < S; ++j)
                if (in_play.has(j))
                    blocked |= law::beats[j];

            bset<S> allowed = want & ~blocked;
            // 被压住的两类：想发没发成的；以及"赢家能打赢的、还在场的"（让位）
            bset<S> prey{};
            for (index_t i = 0; i < S; ++i)
                if (allowed.has(i))
                    prey |= law::beats[i];
            prey &= in_play & ~allowed;
            // 被挡住的候选**不**播报也不复位：它的 cur 停在 done，下一帧继续申辩
            // （这就是 pending：状态在 fsm 自己身上，引擎不存）
            const bset<S> parked = want & blocked;
            const bset<S> suppressed = prey;

            // ③ 放行 / 不放行（外部接口不变：仍走 on_verdict + sink）
            // 先播让位的，再播赢家（和旧引擎的顺序一致）
            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                if (suppressed.has(I))
                {
                    on_verdict<I>(false, st.begin[I], now, now, obs);
                    reset_kind<I>();
                    std::get<I>(fsms).cur = step_t::idle;
                    sig[I] = step_t::idle;
                    in_play.clr(I); // 已退场：不要留在投影里
                    st.begin[I] = time_point{};
                }
            }
            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                if (allowed.has(I))
                {
                    on_verdict<I>(true, st.begin[I], now, now, obs);
                    reset_kind<I>(); // 说过就退场（等价于旧 kill）
                    std::get<I>(fsms).cur = step_t::idle;
                    sig[I] = step_t::idle;
                    in_play.clr(I); // 已退场：不要留在投影里
                    st.begin[I] = time_point{};
                }
            }

            // ④ 投影：只给外面看（测试与打印用），不是权威状态
            st.alive = in_play;
            st.pending = parked & ~suppressed & ~allowed;
        }
    };

    // ═══════════════════════════════════════════════════════════════════
    // 10. debug_ui：ui + 观测 + 给人看的东西
    //
    //   这一层是"用户代码"：记录（trace）、打印（dump）、蓝图（blueprint）、
    //   十进制（put_num）。删掉它，第 1–9 节照样编译、照样跑 —— 这就是隔离。
    //   它不改变判决：只是把 ui 产生的判决顺手抄一份，再翻译成人话。
    // ═══════════════════════════════════════════════════════════════════
    template <class Tree>
    struct debug_ui : ui<Tree>
    {
        using law = law_of<Tree>;
        static constexpr std::size_t S = law::S;
        using ui_type = ui<Tree>;

        trace obs{};

        // 转发：喂一帧的同时记一份
        template <class Frame>
        constexpr void feed(const Frame &f, time_point now) noexcept
        {
            if constexpr (requires() { detail::feed_guard{&obs.in_feed_}; })
                detail::feed_guard guard{&obs.in_feed_};
            ui_type::feed(f, now, &obs);
        }

        void tick(time_point now) noexcept
        {
            if constexpr (requires() { detail::feed_guard{&obs.in_feed_}; })
                detail::feed_guard guard{&obs.in_feed_};
            ui_type::tick(now, &obs);
        }

        std::span<const event> events() const noexcept
        {
            return obs.events();
        }
        void clear_events() noexcept
        {
            obs.clear();
        }
        std::size_t dropped() const noexcept
        {
            return obs.dropped();
        }

        // ── 下面全是"只给人看"的：不进判决，可以整块删掉 ──
        // 十进制打印（consteval 里没有 std::to_string）
        template <class Int> // 下标是 size_t、时限是 uint64_t，都只打印量值
        static consteval void put_num(std::string &out, Int v)
        {
            if (v >= 10)
                put_num(out, v / 10);
            out += static_cast<char>('0' + v % 10);
        }

        // 蓝图：把矩阵印出来。裁决顺序不是"隐性知识"，它就是这张表。
        static consteval static_string blueprint()
        {
            constexpr std::size_t N = law::S;
            std::string out = "kinds:";
            for (index_t i = 0; i < N; ++i)
            {
                out += ' ';
                put_num(out, i);
                out += '=';
                out += law::names[i].view();
                if (law::hold[i] != std::chrono::nanoseconds::zero())
                {
                    out += "(hold ";
                    put_num(out,
                            static_cast<std::uint64_t>(
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    law::hold[i])
                                    .count()));
                    out += "ms)";
                }
            }
            out += "\nbeats: 行 = 赢家，列 = 被吃（x 表示这一行打得赢那一列）\n";
            for (index_t i = 0; i < N; ++i)
            {
                out += ' ';
                put_num(out, i);
                out += " ";
                out += law::names[i].view();
                for (index_t j = 0; j < N; ++j)
                    out += law::beats[i].has(j) ? 'x' : '.';
                out += '\n';
            }
            return static_string{std::define_static_string(out)};
        }
    };

} // namespace uhi

// ═══════════════════════════════════════════════════════════════════════════

#include <cmath>
#include <format>

// ═══════════════════════════════════════════════════════════════════════════
//  本案例：glfw 事件 → 一帧 → 矩阵 → 回调
//
//    glfw 回调
//      └─ glfw_input_translation            （不动）
//           └─ arena.input_event(e)         ← 平台事件（本案例只写这一层翻译）
//                └─ ui.feed(app::mouse_frame / app::kb_frame, now)
//                     └─ 矩阵裁决（库内部，位运算）
//                          └─ on_click / on_dbl / on_triple
//                             on_drag（开始/移动/释放 三个 kind 共用一个回调）
//                             on_hover / on_wheel / on_key_A
//
//    时间：主循环每转一圈调一次 arena.tick()。挂起的候选靠它落地或超时。
// ═══════════════════════════════════════════════════════════════════════════
namespace app
{
    // ── fsm 自己存"我对裁决器说的话"（表格坐标），引擎不再存副本 ──────
    struct fsm_state
    {
        uhi::step_t cur{uhi::step_t::idle};
        uhi::step_t step() const noexcept
        {
            return cur;
        }
    };

    // ── 帧：本案例自己的输入单位。引擎不规定帧长什么样 ──────────────
    struct mouse_frame
    {
        enum what_t
        {
            press,
            release,
            move,
            wheel,
            enter,
            leave
        } what = move;
        double x = 0, y = 0;
        int delta = 0;
    };
    struct kb_frame
    {
        int key = 0;
        bool press = false;
    };

    // ── 参数：一个地方调 ────────────────────────────────────────────
    inline constexpr std::int64_t click_max_ms = 250;  // 按下到松开，超时不算单击
    inline constexpr std::int64_t dbl_gap_ms = 300;    // 双击：两击之间的空档
    inline constexpr std::int64_t triple_gap_ms = 250; // 三击：每档更严一点
    inline constexpr double drag_slop_px = 4.0;        // 越过它就算拖拽

    // ── 复用的"一次按下→松开算不算一次有效单击"：限时 + 限位 ────────
    struct press_watch
    {
        uhi::time_point down_at{};
        double down_x = 0, down_y = 0;
        bool down = false;

        //  0 = 无关帧   1 = 按下   2 = 有效松开   -1 = 无效松开
        int operator()(const mouse_frame &f, uhi::time_point now) noexcept
        {
            switch (f.what)
            {
            case mouse_frame::press:
                down = true;
                down_at = now;
                down_x = f.x;
                down_y = f.y;
                return 1;
            case mouse_frame::release: {
                if (!down)
                    return 0;
                down = false;
                const bool quick = now - down_at <= uhi::time_point{click_max_ms};
                const bool still = std::hypot(f.x - down_x, f.y - down_y) <= drag_slop_px;
                return (quick && still) ? 2 : -1;
            }
            default:
                return 0;
            }
        }
        void reset() noexcept
        {
            down = false;
        }
    };

    // ── 单击 ────────────────────────────────────────────────────────
    template <static_string Name, std::uint64_t HoldMs = 0, static_string FnName = Name>
    struct single_click : fsm_state
    {
        using frame_type = mouse_frame;
        static constexpr auto name = Name;
        static constexpr auto hold_ms = HoldMs;
        static constexpr auto fn_name = FnName;

        struct payload_t
        {
            double x = 0, y = 0;
        } payload{};

        press_watch watch{};

        uhi::step_t feed(const mouse_frame &f, uhi::time_point now) noexcept
        {
            switch (watch(f, now))
            {
            case 1:
                return uhi::step_t::active;
            case 2:
                payload.x = f.x;
                payload.y = f.y;
                return uhi::step_t::done;
            case -1:
                return uhi::step_t::aborted;
            default:
                return watch.down ? uhi::step_t::active : uhi::step_t::idle;
            }
        }
        void reset() noexcept
        {
            watch.reset();
        }
    };

    // ── N 连击：连续 N 次有效单击，每两击之间独立计时 ────────────────
    //    所以间隔不必相同：只要每一档都 ≤ GapMs 就算。
    //    窗口一过就 aborted —— 把机会让给更简单的解释（单击）。
    template <int N, std::int64_t GapMs, static_string Name, std::uint64_t HoldMs = 0,
              static_string FnName = Name>
    struct click_chain : fsm_state
    {
        using frame_type = mouse_frame;
        static constexpr auto name = Name;
        static constexpr auto hold_ms = HoldMs;
        static constexpr auto fn_name = FnName;

        struct payload_t
        {
            double x = 0, y = 0;
            int count = 0;
        } payload{};

        press_watch watch{};
        int clicks = 0;         // 已经完成的单击数
        uhi::time_point last{}; // 上一次松开的时刻
        bool armed = false;     // 正在窗口里等下一击

        uhi::step_t feed(const mouse_frame &f, uhi::time_point now) noexcept
        {
            if (armed && now - last > uhi::time_point{GapMs})
            {
                clicks = 0;
                armed = false;
                watch.reset();
                return uhi::step_t::aborted;
            }

            switch (watch(f, now))
            {
            case 1:
                return uhi::step_t::active;
            case 2:
                ++clicks;
                armed = true;
                last = now;
                payload.x = f.x;
                payload.y = f.y;
                payload.count = clicks;
                if (clicks >= N)
                {
                    clicks = 0;
                    armed = false;
                    return uhi::step_t::done;
                }
                return uhi::step_t::active;
            case -1:
                clicks = 0;
                armed = false;
                return uhi::step_t::aborted;
            default:
                return (armed || clicks > 0) ? uhi::step_t::active : uhi::step_t::idle;
            }
        }
        // 时间窗只靠"喂帧"是过不去的：必须由 tick 推动，否则永远不放弃
        // tick 自己改 cur：窗口过期就退场。引擎只负责"调用"，不负责"解释返回值"
        void tick(uhi::time_point now) noexcept
        {
            if (armed && now - last > uhi::time_point{GapMs})
            {
                clicks = 0;
                armed = false;
                watch.reset();
                this->cur = uhi::step_t::aborted;
            }
        }
        void reset() noexcept
        {
            clicks = 0;
            armed = false;
            watch.reset();
        }
    };

    // ═══════════════════════════════════════════════════════════════
    //  拖拽：拆成三段，每段一个 kind（一个 kind 只会播报一次）
    //    on_drag_begin  按下 → 第一次越过阈值          一次
    //    on_drag_move   越界之后每一次移动             每帧一次
    //    on_drag_end    越界之后松手                   一次
    //  三个 kind 共用同一个回调 on_drag（fn_name），靠 payload.phase 分流。
    // ═══════════════════════════════════════════════════════════════
    struct drag_payload
    {
        enum class phase_t
        {
            begin,
            move,
            end
        } phase{phase_t::begin};

        double x{}, y{};               // 当前点
        double dx{}, dy{};             // 相对上一帧
        double total_dx{}, total_dy{}; // 相对按下点
    };

    // 三段共用的记账：按下点 / 上一帧位置 / 是否还按着
    struct drag_track
    {
        double down_x = 0, down_y = 0;
        double last_x = 0, last_y = 0;
        bool down = false;

        void press(double x, double y) noexcept
        {
            down = true;
            down_x = last_x = x;
            down_y = last_y = y;
        }
        void last(double x, double y) noexcept
        {
            last_x = x;
            last_y = y;
        }
        void release() noexcept
        {
            down = false;
        }
        [[nodiscard]] bool past_slop(double x, double y) const noexcept
        {
            return std::hypot(x - down_x, y - down_y) > drag_slop_px;
        }
        [[nodiscard]] double total_dx(double x) const noexcept
        {
            return x - down_x;
        }
        [[nodiscard]] double total_dy(double y) const noexcept
        {
            return y - down_y;
        }
        [[nodiscard]] drag_payload make(drag_payload::phase_t ph, double x, double y,
                                        double dx, double dy) const noexcept
        {
            return {ph, x, y, dx, dy, total_dx(x), total_dy(y)};
        }
    };

    // 开始：一次按下只报一次
    template <static_string Name, std::uint64_t HoldMs = 0, static_string FnName = Name>
    struct drag_begin : fsm_state
    {
        using frame_type = mouse_frame;
        static constexpr auto name = Name;
        static constexpr auto hold_ms = HoldMs;
        static constexpr auto fn_name = FnName;

        drag_payload payload{};
        drag_track t{};
        bool begun = false;

        uhi::step_t feed(const mouse_frame &f, uhi::time_point) noexcept
        {
            switch (f.what)
            {
            case mouse_frame::press:
                t.press(f.x, f.y);
                begun = false;
                return uhi::step_t::active;
            case mouse_frame::move:
                if (!t.down)
                    return uhi::step_t::idle;
                if (!begun && t.past_slop(f.x, f.y))
                {
                    begun = true;
                    payload = t.make(drag_payload::phase_t::begin, f.x, f.y, 0, 0);
                    t.last(f.x, f.y);
                    return uhi::step_t::done;
                }
                t.last(f.x, f.y);
                return uhi::step_t::active;
            default: // release / wheel / enter / leave
                t.release();
                return uhi::step_t::aborted;
            }
        }
        // 注意：begin 只清"按着"，不在清"已经报过"——不然下一次移动又报一遍
        void reset() noexcept
        {
            t.down = false;
        }
    };

    // 移动：越界之后每一次移动都报（流式）
    //   注意判据是"已经进入拖拽"这个**闩**，不是"这一帧离按下点够远"——
    //   否则拖回原处那一下就不报了。
    template <static_string Name, std::uint64_t HoldMs = 0, static_string FnName = Name>
    struct drag_move : fsm_state
    {
        using frame_type = mouse_frame;
        static constexpr auto name = Name;
        static constexpr auto hold_ms = HoldMs;
        static constexpr auto fn_name = FnName;

        drag_payload payload{};
        drag_track t{};
        bool dragging = false; // 越过一次阈值就置位，直到松手

        uhi::step_t feed(const mouse_frame &f, uhi::time_point) noexcept
        {
            switch (f.what)
            {
            case mouse_frame::press:
                t.press(f.x, f.y);
                dragging = false;
                return uhi::step_t::active;
            case mouse_frame::move: {
                if (!t.down)
                    return uhi::step_t::idle;
                if (!dragging)
                {
                    if (!t.past_slop(f.x, f.y))
                    {
                        t.last(f.x, f.y);
                        return uhi::step_t::active;
                    }
                    dragging = true;
                }
                payload = t.make(drag_payload::phase_t::move, f.x, f.y, f.x - t.last_x,
                                 f.y - t.last_y);
                t.last(f.x, f.y);
                return uhi::step_t::done;
            }
            default:
                t.release();
                dragging = false;
                return uhi::step_t::aborted;
            }
        }
        // 没有 reset()：它的状态是"这次拖拽还在进行"，不是"这一帧报过"
        // （每次播报都会被 kill+reset，若在这里清记账，下一次移动就不认识自己在拖了）
    };

    // 释放：越界之后松手才报（没拖过就不报）
    template <static_string Name, std::uint64_t HoldMs = 0, static_string FnName = Name>
    struct drag_end : fsm_state
    {
        using frame_type = mouse_frame;
        static constexpr auto name = Name;
        static constexpr auto hold_ms = HoldMs;
        static constexpr auto fn_name = FnName;

        drag_payload payload{};
        drag_track t{};
        bool past = false;

        uhi::step_t feed(const mouse_frame &f, uhi::time_point) noexcept
        {
            switch (f.what)
            {
            case mouse_frame::press:
                t.press(f.x, f.y);
                past = false;
                return uhi::step_t::active;
            case mouse_frame::move:
                if (!t.down)
                    return uhi::step_t::idle;
                if (t.past_slop(f.x, f.y))
                    past = true;
                t.last(f.x, f.y);
                return uhi::step_t::active;
            case mouse_frame::release:
                t.release();
                if (!past)
                    return uhi::step_t::aborted;
                past = false;
                payload = t.make(drag_payload::phase_t::end, f.x, f.y, 0, 0);
                return uhi::step_t::done;
            default:
                return uhi::step_t::hold;
            }
        }
        void reset() noexcept
        {
            t = {};
            past = false;
        }
    };

    // ── 悬停：指针进入窗口时报一次（离开不算事件）──────────────────
    template <static_string Name, std::uint64_t HoldMs = 0, static_string FnName = Name>
    struct hover : fsm_state
    {
        using frame_type = mouse_frame;
        static constexpr auto name = Name;
        static constexpr auto hold_ms = HoldMs;
        static constexpr auto fn_name = FnName;

        uhi::step_t feed(const mouse_frame &f, uhi::time_point) noexcept
        {
            return f.what == mouse_frame::enter ? uhi::step_t::done : uhi::step_t::hold;
        }
    };

    // ── 滚轮：一格一次（delta 交给载荷）────────────────────────────
    template <static_string Name, std::uint64_t HoldMs = 0, static_string FnName = Name>
    struct wheel : fsm_state
    {
        using frame_type = mouse_frame;
        static constexpr auto name = Name;
        static constexpr auto hold_ms = HoldMs;
        static constexpr auto fn_name = FnName;

        struct payload_t
        {
            int delta = 0;
        } payload{};

        uhi::step_t feed(const mouse_frame &f, uhi::time_point) noexcept
        {
            if (f.what != mouse_frame::wheel)
                return uhi::step_t::hold;
            payload.delta = f.delta;
            return uhi::step_t::done;
        }
    };

    // ── 键盘 A ─────────────────────────────────────────────────────
    template <static_string Name, std::uint64_t HoldMs = 0, static_string FnName = Name>
    struct key_a : fsm_state
    {
        using frame_type = kb_frame;
        static constexpr auto name = Name;
        static constexpr auto hold_ms = HoldMs;
        static constexpr auto fn_name = FnName;

        uhi::step_t feed(const kb_frame &f, uhi::time_point) noexcept
        {
            return (f.press && f.key == 'A') ? uhi::step_t::done : uhi::step_t::hold;
        }
    };

    // ── 叶子：name 是 kind 的身份（进日志）；最后一个参数是回调名 ───
    //    拖拽三段名字不同（日志能分开），但回调名都叫 on_drag（共用一个处理器）。
    using l_click = uhi::leaf<single_click<"on_click">>;
    using l_dbl = uhi::leaf<click_chain<2, dbl_gap_ms, "on_dbl">>;
    using l_triple = uhi::leaf<click_chain<3, triple_gap_ms, "on_triple">>;
    using l_drag_begin = uhi::leaf<drag_begin<"on_drag_begin", 0, "on_drag">>;
    using l_drag_move = uhi::leaf<drag_move<"on_drag_move", 0, "on_drag">>;
    using l_drag_end = uhi::leaf<drag_end<"on_drag_end", 0, "on_drag">>;
    using l_hover = uhi::leaf<hover<"on_hover">>;
    using l_wheel = uhi::leaf<wheel<"on_wheel">>;
    using l_key = uhi::leaf<key_a<"on_key_A">>;

    // ── 树：只声明"谁比谁具体"，不写任何顺序 ───────────────────────
    //    refines<A, B> 读作"A 等 B" ⇒ **B 更具体，B 赢**（第二个参数才是细的）。
    //      拖拽三段 > 单击、双击 > 单击、三击 > 双击 > 单击
    //    拖拽和双击/三击之间没有边 —— 免得"刚点完一下"的挂起双击把拖拽挡住。
    using Tree = uhi::all_of<
        uhi::refines<l_click, uhi::all_of<l_drag_begin, l_drag_move, l_drag_end,
                                          uhi::refines<l_dbl, l_triple>>>, //
        l_hover,                                                           //
        l_wheel,                                                           //
        l_key>;

    using ui_type = uhi::debug_ui<Tree>;

    // 回调打到哪儿：自检要靠它验证 sink 那一路确实通了
    struct tally
    {
        int click{}, dbl{}, triple{}, hover{}, wheel{}, key_a{};
        int drag_begin{}, drag_move{}, drag_end{};
        double x{}, y{}, dx{}, dy{}, total_dx{}, total_dy{};
        int delta{};

        friend constexpr bool operator==(const tally &, const tally &) = default;
    };

    // 事件时刻：正常情况取墙上时钟；自检时用 arena.now 给的逻辑时间
    inline uhi::time_point now_ms() noexcept
    {
        static const auto t0 = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<uhi::time_point>(
            std::chrono::steady_clock::now() - t0);
    }
    template <class Self>
    inline uhi::time_point now_of(Self &self) noexcept
    {
        return self.fake_clock ? self.now : now_ms();
    }

    inline constexpr std::string_view phase_name(drag_payload::phase_t p) noexcept
    {
        using pt = drag_payload::phase_t;
        return p == pt::begin ? "开始" : (p == pt::move ? "移动" : "释放");
    }

    // 把这一帧的播报打出来（跨度 = 第一次活着 → 第一次有结论；at = 落地时刻）
    //   移动和悬停是"流"，不逐帧刷屏（自检仍然读得到 events）
    inline void report(const ui_type &u) noexcept
    {
        for (const auto &e : u.events())
        {
            if (e.name == "on_hover" || e.name == "on_drag_move")
                continue;
            std::println("  [t={:>6}ms] {:<14} {:<3} span=({},{})", e.at.count(), e.name,
                         uhi::to_string(e.v), e.begin.count(), e.end.count());
        }
    }

    // ── 唯一的 arena 构造：窗口模式和自检模式共用同一份 ────────────
    inline auto make_arena()
    {
        return make_aggregate<"arena">(
            field<"ui">(ui_type{}),          //
            field<"tally">(tally{}),         // 回调写这里，自检读这里
            field<"cursor_x">(0.0),          //
            field<"cursor_y">(0.0),          //
            field<"now">(uhi::time_point{}), // 逻辑时间
            field<"fake_clock">(false),      // 自检：时间由测试喂
            field<"keep_events">(false),     // 自检：tick 后不清空播报
            field<"frames">(0u),             //

            // ── 赢家落地：参数个数要和载荷对得上 ──
            method<"on_click">([](auto &self, auto &&p) noexcept {
                ++self.tally.click;
                self.tally.x = p.x;
                self.tally.y = p.y;
                std::println("  >>> 单击    (x={:.1f}, y={:.1f})", p.x, p.y);
            }),
            method<"on_dbl">([](auto &self, auto &&p) noexcept {
                ++self.tally.dbl;
                self.tally.x = p.x;
                self.tally.y = p.y;
                std::println("  >>> 双击    (x={:.1f}, y={:.1f}, 共 {} 次)", p.x, p.y,
                             p.count);
            }),
            method<"on_triple">([](auto &self, auto &&p) noexcept {
                ++self.tally.triple;
                self.tally.x = p.x;
                self.tally.y = p.y;
                std::println("  >>> 三击    (x={:.1f}, y={:.1f}, 共 {} 次)", p.x, p.y,
                             p.count);
            }),
            // 拖拽三段共用的回调：p 用 auto&& 接，引擎按叶子的载荷类型转发
            method<"on_drag">([](auto &self, auto &&p) noexcept {
                using pt = drag_payload::phase_t;
                switch (p.phase)
                {
                case pt::begin:
                    ++self.tally.drag_begin;
                    std::println("  >>> 拖拽·开始 (起点 {:.1f},{:.1f})", p.x, p.y);
                    break;
                case pt::move:
                    ++self.tally.drag_move; // 移动是流：只计数，不刷屏
                    break;
                case pt::end:
                    ++self.tally.drag_end;
                    self.tally.x = p.x;
                    self.tally.y = p.y;
                    self.tally.total_dx = p.total_dx;
                    self.tally.total_dy = p.total_dy;
                    std::println("  >>> 拖拽·释放 (到 {:.1f},{:.1f}；总位移 "
                                 "{:.1f},{:.1f}；期间移动 "
                                 "{} 次)",
                                 p.x, p.y, p.total_dx, p.total_dy, self.tally.drag_move);
                    break;
                }
            }),
            method<"on_hover">([](auto &self) noexcept {
                ++self.tally.hover;
                std::println("  >>> 悬停    进入窗口");
            }),
            method<"on_wheel">([](auto &self, auto &&p) noexcept {
                ++self.tally.wheel;
                self.tally.delta = p.delta;
                std::println("  >>> 滚轮    (delta={})", p.delta);
            }),
            method<"on_key_A">([](auto &self) noexcept {
                ++self.tally.key_a;
                std::println("  >>> 键盘 A");
            }),

            // 绑定：把 sinks 接到上面那些回调上（只需要一次）
            method<"bind">([](auto &self) noexcept { self.ui.bind(self); }),

            // 时间推进 + 把这一帧的播报打出来
            method<"tick">([](auto &self) noexcept {
                const auto now = now_of(self);
                self.now = now;
                self.ui.tick(now);
                report(self.ui);
                if (!self.keep_events)
                    self.ui.clear_events();
                ++self.frames;
            }),

            // 平台事件 → 一帧：这是本案例唯一要写的适配层
            method<"input_event">([]<class Self, class E>(Self &self, E e) noexcept {
                using T = std::remove_cvref_t<E>;
                const auto now = now_of(self);
                self.now = now;

                if constexpr (std::is_same_v<T, mousebutton_event>)
                {
                    if (e.button != MouseButtons::eMOUSE_BUTTON_LEFT)
                        return;
                    if (e.press())
                        self.ui.feed(mouse_frame{mouse_frame::press, self.cursor_x,
                                                 self.cursor_y, 0},
                                     now);
                    else if (e.release())
                        self.ui.feed(mouse_frame{mouse_frame::release, self.cursor_x,
                                                 self.cursor_y, 0},
                                     now);
                }
                else if constexpr (std::is_same_v<T, position2d_event>)
                {
                    self.cursor_x = e.xpos;
                    self.cursor_y = e.ypos;
                    self.ui.feed(mouse_frame{mouse_frame::move, e.xpos, e.ypos, 0}, now);
                }
                else if constexpr (std::is_same_v<T, scroll_event>)
                {
                    self.ui.feed(mouse_frame{mouse_frame::wheel, self.cursor_x,
                                             self.cursor_y, static_cast<int>(e.yoffset)},
                                 now);
                }
                else if constexpr (std::is_same_v<T, cursor_enter_event>)
                {
                    self.ui.feed(
                        mouse_frame{e.value ? mouse_frame::enter : mouse_frame::leave,
                                    self.cursor_x, self.cursor_y, 0},
                        now);
                }
                else if constexpr (std::is_same_v<T, keyboard_event>)
                {
                    if (e.key != Key::eA)
                        return;
                    self.ui.feed(kb_frame{static_cast<int>('A'), e.press()}, now);
                }
                // 其余事件（char / drop / 窗口尺寸 …）与本案例无关
            }));
    }

    // ═══════════════════════════════════════════════════════════════════
    //  自检：不建窗口，但**走和窗口模式同一个 arena**
    //    伪造平台事件 → arena.input_event → 翻译 → 矩阵 → sink → 回调
    //    所以它同时测到：make_aggregate 那层、bind、input_event、tick、载荷。
    // ═══════════════════════════════════════════════════════════════════
    inline int selftest()
    {
        auto arena = make_arena();
        arena.fake_clock = true;
        arena.keep_events = true;

        using kv = std::pair<std::string_view, uhi::verdict>;

        // 取走这一段的结果：事件 + 回调计数（回调里的载荷单独看）
        auto snap = [&] {
            std::vector<kv> ev;
            for (const auto &e : arena.ui.events())
                ev.push_back({e.name, e.v});
            arena.ui.clear_events();
            auto t = arena.tally;
            arena.tally = {};
            return std::pair{ev, t};
        };

        auto expect_ev = [](std::string_view what, const std::vector<kv> &got,
                            const std::vector<kv> &want) {
            const bool ok = (got == want);
            std::print("  {} {}\n     播报 ", ok ? "✓" : "✗", what);
            for (const auto &k : got)
                std::print("{}:{} ", k.first, uhi::to_string(k.second));
            if (!ok)
            {
                std::print(" 期望 ");
                for (const auto &k : want)
                    std::print("{}:{} ", k.first, uhi::to_string(k.second));
            }
            std::println("");
            return ok ? 0 : 1;
        };
        auto expect_n = [](std::string_view what, int got, int want) {
            const bool ok = (got == want);
            std::println("  {} {:<34} 回调 {} 次{}", ok ? "✓" : "✗", what, got,
                         ok ? "" : std::format("（期望 {}）", want));
            return ok ? 0 : 1;
        };
        auto expect_b = [](std::string_view what, bool ok) {
            std::println("  {} {}", ok ? "✓" : "✗", what);
            return ok ? 0 : 1;
        };

        // ── 造平台事件（和 glfw 回调送进来的东西同型）──
        const auto press = [&] {
            arena.invoke<"input_event">(mousebutton_event{
                .button = MouseButtons::eMOUSE_BUTTON_LEFT, .action = Action::ePRESS});
        };
        const auto release = [&] {
            arena.invoke<"input_event">(mousebutton_event{
                .button = MouseButtons::eMOUSE_BUTTON_LEFT, .action = Action::eRELEASE});
        };
        const auto move_to = [&](double x, double y) {
            arena.invoke<"input_event">(position2d_event{.xpos = x, .ypos = y});
        };
        const auto scroll = [&](double d) {
            arena.invoke<"input_event">(scroll_event{.yoffset = d});
        };
        const auto enter = [&] {
            arena.invoke<"input_event">(cursor_enter_event{.value = true});
        };
        const auto key_a = [&] {
            arena.invoke<"input_event">(
                keyboard_event{.key = Key::eA, .action = Action::ePRESS});
        };
        const auto set_time = [&](std::int64_t ms) {
            arena.now = uhi::time_point{ms};
        };
        const auto tick = [&] {
            arena.invoke<"tick">();
        };

        int fails = 0;
        std::println("自检（不建窗口，走同一个 arena）");

        // ⓪ 没 bind 之前：判决照跑（events 有），但 sink 是空的
        set_time(0);
        scroll(1.0); // 用滚轮：一帧就成立，不用等窗口
        {
            auto [ev, c] = snap();
            fails += expect_ev("⓪ 未 bind：判决仍然发生", ev,
                               {kv{"on_wheel", uhi::verdict::accepted}});
            fails += expect_n("⓪ 未 bind：回调没响", c.wheel, 0);
        }
        arena.invoke<"bind">(); // ← 从这一行起回调才存在

        // ① 单击：按下 100、松开 180 ⇒ 等双击(300)/三击(250)窗口过期才落地
        move_to(10.5, 20.5);
        set_time(100);
        press();
        set_time(180);
        release();
        {
            auto [ev, c] = snap();
            fails += expect_ev("① 松开当帧：被更具体的挡着", ev, {});
            fails += expect_n("① 回调还没叫", c.click, 0);
        }
        set_time(490);
        tick();
        {
            auto [ev, c] = snap();
            fails += expect_ev("① 490ms：单击落地", ev,
                               {kv{"on_click", uhi::verdict::accepted}});
            fails += expect_n("① 回调响了一次", c.click, 1);
            fails +=
                expect_b("① 载荷 x/y 传对了（10.5, 20.5）", c.x == 10.5 && c.y == 20.5);
        }

        // ② 双击：两击间隔不等
        set_time(600);
        press();
        set_time(660);
        release();
        set_time(780); // 间隔 120
        press();
        set_time(830);
        release();
        set_time(1130);
        tick();
        {
            auto [ev, c] = snap();
            fails += expect_ev("② 双击（间隔 120/170 不等）", ev,
                               {kv{"on_click", uhi::verdict::superseded},
                                kv{"on_dbl", uhi::verdict::accepted}});
            fails += expect_n("② 回调响了一次", c.dbl, 1);
        }

        // ③ 三击：三个间隔都不一样
        set_time(1300);
        press();
        set_time(1380);
        release();
        set_time(1450); // 间隔 70
        press();
        set_time(1530);
        release();
        set_time(1620); // 间隔 90
        press();
        set_time(1670);
        release();
        {
            auto [ev, c] = snap();
            fails += expect_ev("③ 三击（间隔 70/90/50 不等）", ev,
                               {kv{"on_click", uhi::verdict::superseded},
                                kv{"on_dbl", uhi::verdict::superseded},
                                kv{"on_triple", uhi::verdict::accepted}});
            fails += expect_n("③ 回调响了一次", c.triple, 1);
        }

        // ④ 拖拽：按下 → 越过阈值（开始 + 第一次移动）→ 松手（释放）
        set_time(1800);
        move_to(10, 10); // 先把光标放到已知位置
        press();
        set_time(1850);
        move_to(130, 30); // 越过阈值这一帧：开始 + 移动
        {
            auto [ev, c] = snap();
            fails += expect_ev("④ 越过阈值：开始 + 移动，单击让位", ev,
                               {kv{"on_click", uhi::verdict::superseded},
                                kv{"on_drag_begin", uhi::verdict::accepted},
                                kv{"on_drag_move", uhi::verdict::accepted}});
            fails += expect_n("④ 开始回调响了一次", c.drag_begin, 1);
            fails += expect_n("④ 移动回调响了一次", c.drag_move, 1);
        }
        set_time(1870);
        move_to(150, 40); // 拖拽中：只报移动
        {
            auto [ev, c] = snap();
            fails += expect_ev("④ 拖拽中：只有移动", ev,
                               {kv{"on_drag_move", uhi::verdict::accepted}});
            fails += expect_n("④ 移动回调又响一次", c.drag_move, 1);
        }
        set_time(1900);
        release(); // 越界后松手：释放
        {
            auto [ev, c] = snap();
            fails += expect_ev("④ 松手：释放", ev,
                               {kv{"on_drag_end", uhi::verdict::accepted}});
            fails += expect_n("④ 释放回调响了一次", c.drag_end, 1);
            fails += expect_b("④ 释放载荷：总位移 140,30",
                              c.total_dx == 140.0 && c.total_dy == 30.0);
        }

        // ⑤ 滚轮 / 悬停 / 键盘 A
        set_time(2000);
        scroll(1.0);
        {
            auto [ev, c] = snap();
            fails += expect_ev("⑤ 滚轮", ev, {kv{"on_wheel", uhi::verdict::accepted}});
            fails += expect_b("⑤ 载荷 delta=1", c.delta == 1);
        }
        set_time(2050);
        enter();
        {
            auto [ev, c] = snap();
            fails += expect_ev("⑤ 悬停（进入窗口）", ev,
                               {kv{"on_hover", uhi::verdict::accepted}});
            fails += expect_n("⑤ 回调响了一次", c.hover, 1);
        }
        set_time(2100);
        key_a();
        {
            auto [ev, c] = snap();
            fails += expect_ev("⑤ 键盘 A", ev, {kv{"on_key_A", uhi::verdict::accepted}});
            fails += expect_n("⑤ 回调响了一次", c.key_a, 1);
        }

        // ⑥ 慢按（400ms > 250ms）不算单击，也不该当成拖拽
        set_time(2200);
        press();
        set_time(2600);
        release();
        set_time(3000);
        tick();
        {
            auto [ev, c] = snap();
            fails += expect_ev("⑥ 慢按 400ms：不播", ev, {});
            fails += expect_n("⑥ 单击回调没响", c.click, 0);
            fails += expect_n("⑥ 拖拽回调没响", c.drag_begin + c.drag_end, 0);
        }

        // ⑦ 拖出去再拖回来：开始/移动/移动/释放，单击始终不报
        set_time(3100);
        move_to(10, 10);
        press();
        set_time(3150);
        move_to(200, 200); // 开始 + 移动
        set_time(3200);
        move_to(10, 10); // 移动（回到原处）
        set_time(3250);
        release(); // 释放（总位移 0）
        {
            auto [ev, c] = snap();
            fails += expect_ev("⑦ 拖出去再回来：开始/移动/移动/释放", ev,
                               {kv{"on_click", uhi::verdict::superseded},
                                kv{"on_drag_begin", uhi::verdict::accepted},
                                kv{"on_drag_move", uhi::verdict::accepted},
                                kv{"on_drag_move", uhi::verdict::accepted},
                                kv{"on_drag_end", uhi::verdict::accepted}});
            fails += expect_n("⑦ 单击回调没响", c.click, 0);
            fails += expect_n("⑦ 移动回调响了两次", c.drag_move, 2);
            fails += expect_b("⑦ 释放载荷：总位移回到 0,0",
                              c.total_dx == 0.0 && c.total_dy == 0.0);
        }

        std::println("自检：{} 条失败", fails);
        return fails == 0 ? 0 : 1;
    }
} // namespace app

// ═══════════════════════════════════════════════════════════════════════════
//  fms_template：fsm 侧协议的底座（你贴过来的那份，我把类型补全了）
//
//    出口在 fsm 自己手里：receiver（void*）+ invoke_（函数指针）
//    bind 是两阶段的第一阶段：接线（编译期检查 Receiver::invoke<fn_name> 的签名）
//    win() 是唯一输出：赢了才调，投递哪一种事件由 fms_status 决定
//    lose(who) 被压制；tick(now, cur_step, suppressed) 由裁决器每帧调用
//    feed(frame, now) 吃原始事件，返回的 step_t 就是填进裁决表格的那一格
// ═══════════════════════════════════════════════════════════════════════════
struct fms_template
{
    using static_string = mcs::vulkan::meta::static_string;

    static constexpr static_string fn_name = "fn_name";

    struct drag_start
    {
        int a;
    };
    struct drag_end
    {
        int a;
    };

    using invoke_type = void (*)(void *self) noexcept;

    void *receiver{};
    invoke_type invoke_{};
    int fms_status{};

    // 消费原始事件，并告知裁决器当前状态（= 表格里那一格）
    uhi::step_t feed(const app::mouse_frame &, uhi::time_point) noexcept
    {
        return uhi::step_t::idle;
    }

    // 裁决器每帧问一次：cur_step 是表格里的状态，suppressed 表示这一帧被压制
    void tick(uhi::time_point, uhi::step_t, bool /*是否被压制*/) noexcept
    {
        // 可能需要投递
        if (invoke_ != nullptr)
            invoke_(this);
    }

    // 两阶段初始化 · 第一阶段：接线
    template <class Receiver>
    void bind(Receiver &ptr) noexcept
    {
        receiver = &ptr;
        invoke_ = invoke_type{[](void *self) noexcept {
            auto *m = static_cast<fms_template *>(self);
            switch (m->fms_status)
            {
            case 0:
                static_cast<Receiver *>(m->receiver)
                    ->template invoke<fn_name>(drag_start{0});
                break;
            case 1:
                static_cast<Receiver *>(m->receiver)
                    ->template invoke<fn_name>(drag_end{0});
                break;
            default:
                break;
            }
        }};
    }

    // 得到裁决结果：赢了才投递
    void win() noexcept
    {
        assert(invoke_ != nullptr);
        invoke_(this);
    }
    // 被压制
    void lose(static_string /*who*/) noexcept {}
};

int main(int argc, char **argv)
try
{
    std::setvbuf(stdout, nullptr, _IONBF, 0); // 重定向时也立刻看得到输出

    if (argc > 1 && std::string_view{argv[1]} == "--selftest")
        return app::selftest();

    surface window{};
    window.setup({.width = WIDTH, .height = HEIGHT}, TITLE);

    auto arena = app::make_arena();
    arena.invoke<"bind">();

    glfw_input_translation input;
    input.setReceiver(arena); // glfw 事件 → arena.input_event

    std::println("就绪：单击 / 双击 / 三击 / 拖拽 / 滚轮 / 键盘 A；关窗口退出。");
    std::println("参数：点击 ≤ {}ms，双击空档 ≤ {}ms，三击空档 ≤ {}ms，拖拽阈值 {:.0f}px",
                 app::click_max_ms, app::dbl_gap_ms, app::triple_gap_ms,
                 app::drag_slop_px);

    while (window.shouldClose() == 0)
    {
        surface::pollEvents();
        arena.invoke<"tick">();
    }

    std::println("main done（tick 了 {} 次）", arena.frames);
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}
