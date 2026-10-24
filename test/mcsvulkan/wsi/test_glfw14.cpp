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
//    改动顺序建议：先动 ui 的投递口，再动 fsm 协议，最后动矩阵。
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
//  运行期只有一个坐标系（声明序）：
//      live / ready 位图 + 静态 beats
//  一条律：ready 的极大元 ⇒ 这一拍赢家；赢家吃掉它活着的后代；
//          其余 pending。跑完就是 accepted 位图 + win/lose 调用。
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
    //
    //   idle  —— 我没有 claim
    //   hold  —— 我有一个 claim，但这一帧没有事件（参与压制，不发射）
    //   ready —— 我有一个 claim，这一帧有事件（可以被允许发射）
    //
    // ready 不是“交互结束”，而是“这一帧有一个事件要发”。一个长 claim
    // （drag）可以在 hold/ready 之间来回很多次；非流式只是 ready 一次的特化。
    //
    // 没有 done / aborted / reset：帧不改变 claim 时，识别器把当前状态
    // 原样再说一遍；自己放弃就回 idle（放弃不是判决，不产生事件）。
    enum class step_t : std::uint8_t
    {
        idle,
        hold,
        ready
    };

    enum class verdict : std::uint8_t
    {
        accepted,  // 赢
        superseded // 让位
    };

    // ═══════════════════════════════════════════════════════════════════
    // 0.1 识别器 ↔ 裁决器：四个方法，两个方向
    //
    //   识别器 → session（每次 feed / tick 的返回值）：
    //     ① idle / hold / ready —— 我现在是什么
    //     ② 吃哪种帧            —— 编译期由 feed 重载决定；law_of 用它算 accepted-frame 集合
    //
    //   session → 识别器（裁决之后，只可能调一个）：
    //     ③ win()  —— 允许发射这一帧的事件；是否结束 claim 由识别器决定
    //     ④ lose() —— 更具体的赢了：丢掉并结束这条 claim
    //
    //   裁决器本身只看到 report[] 和静态表；它不调用识别器。
    //   调用 win/lose 的是 session。
    //
    //   没有 reset。识别器的内部状态只由它自己写；session 没有它的指针，
    //   也不在裁决之后替它"清一下"。win/lose 是消息，不是复位。
    //
    //   ── 识别器的义务（裁决器安全/活性证明依赖这六条）──────────────
    //   R1 一槽一条：同一时刻一个 kind 至多一条活着的 claim。
    //   R2 单调：ready 的 claim 在收到 win/lose 之前不能自己降回 idle/hold。
    //   R3 收到 win/lose 后，该 claim 必须结束；同一交互不得再次申报。
    //   R4 feed/tick 只读自己的状态、无其它副作用、noexcept；tick 只推进时间。
    //   R5 及时：一次交互从它的第一帧起就必须有 claim（hold），不能等
    //      更低优先级的解释已经赢了之后再补报。
    //   R6 事件消费：win 之后不得对同一个事件再次 ready；下一个 ready 必须
    //      是新事件。长 claim 可以多次 ready，但每次都是不同事件。
    // ═══════════════════════════════════════════════════════════════════
    namespace fsm
    {
        // 识别器只做三件事：吃帧、过时间、投递/退休。
        // 裁决器只吃 step_t 数组，不认识 Frame、payload、span。
        template <class M, class Frame>
        concept can_step = requires(M &m, const Frame &f, time_point t) {
            { m.feed(f, t) } noexcept -> std::same_as<step_t>;
        };

        template <class M>
        concept machine = std::default_initializable<M> && requires(M &m, time_point t) {
            typename M::consume_t; // 我吃哪些原始事件：Tree 的 frame_types 由它算
            { m.tick(t) } noexcept -> std::same_as<step_t>;
            { m.win() } noexcept -> std::same_as<step_t>;
            { m.lose() } noexcept -> std::same_as<step_t>;
        };
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
        time_point at{}; // 裁决时刻
    };

    // ── 观测：只写不读，不参与判决。整块可换 ──
    //    trace：定长记录，满了计数丢弃（不覆盖、不分配）
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
    } // namespace detail

    // ═══════════════════════════════════════════════════════════════════
    // 1. plan：编译期的声明表（L1 同态律）
    //      kinds  : 每片叶子的名字 / 回调名
    //      awaits : (i,j) —— i 是 j 的抽象版
    //      excl   : (i,j) —— i 与 j 不能共存
    // ═══════════════════════════════════════════════════════════════════
    struct kind_desc
    {
        static_string name{};    // kind 的身份：日志、蓝图都用它（要求唯一）
        static_string fn_name{}; // 回调的名字：invoke<fn_name>，可以多个 kind 共用一个

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

    // 叶子 = 纯类型：两个名字 + 状态机类型。
    //   它不持有状态机：状态机住在 ui 的扁平元组里。
    //
    //   两个名字各管一摊，不耦合：
    //     Name   —— kind 的身份。进日志、蓝图、报错；要求唯一（它是"谁"）。
    //     FnName —— 回调的名字。sink 只会做 invoke<FnName>(payload)；
    //               多个 kind 可以共用同一个回调。
    //   “能吃哪些帧”不再手写：由 feed 重载在 law_of 里编译期算出来，
    //   关系两端能不能相遇也由这个集合决定，不靠 channel 字符串。
    template <fsm::machine Fsm>
    struct leaf
    {
        using machine = Fsm; // 叶子协议：我的状态机类型（反射靠它认出叶子）
        static constexpr auto name = Fsm::name;       // kind 的身份
        static constexpr auto fn_name = Fsm::fn_name; // 回调的名字（invoke<fn_name>）
        using consume_t = Fsm::consume_t;

        static consteval plan_data plan()
        {
            return plan_data{.kinds = {{name, fn_name}}, .awaits = {}, .excl = {}};
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
            constexpr std::size_t K = sizeof...(Rs);
            std::array<plan_data, K> parts{}; //收集每个子树的 plan_data
            index_t k = 0;
            ((parts[k++] = Rs::plan()), ...);

            // 前缀和 off 与拼接 p
            plan_data p;
            std::array<index_t, K + 1> off{};
            for (std::size_t i = 0; i < K; ++i)
            {
                off[i + 1] = off[i] + static_cast<index_t>(parts[i].kinds.size());
                p = p + parts[i];
            }
            /*
            外层 (a, b)：枚举所有不同的子树对，并且强制 a < b（按声明序，a 在前）。
            内层 (x, y)：x 遍历子树 a 的所有叶子下标，y 遍历子树 b 的所有叶子下标。
            把每一对 (x, y) 加入 excl
            */
            // NOTE: 任意两个不同子树之间的叶子两两互斥
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
    //    ① 无自环 ② 细化×互斥不矛盾 ③ awaits 无环
    //
    //    “关系两端能不能相遇”不在这里：那要看 feed 重载，是
    //    law_of 里按 accepted-frame 集合做的编译期检查。
    //    查重只查 kind 的 name（它是身份）。
    //    fn_name（回调名）**不查重** —— 多个 kind 共用一个回调是合法用法。
    // ═══════════════════════════════════════════════════════════════════
    consteval void validate(const plan_data &p)
    {
        const std::size_t n = p.kinds.size(); // 多少个 kind：这是长度
        auto kind = [&](index_t i) -> const kind_desc & {
            return p.kinds[i];
        };

        for (index_t i = 0; i < n; ++i)
            for (index_t j = i + 1; j < n; ++j)
                if (kind(i).name.view() == kind(j).name.view())
                    throw "dup kind name";

        for (auto [i, j] : p.awaits)
            if (i == j)
                throw "awaits self-loop";
        for (auto [i, j] : p.excl)
            if (i == j)
                throw "excl self-loop";
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
    //      live          第 i 位 = "第 i 个 kind 有活着的 claim"
    //      ready         第 i 位 = "本拍有事件"
    //      scope         第 i 位 = "本拍被这一帧推进过"
    //
    //    ── 为什么是它：裁决的每一步都是集合运算，一一对应 ──
    //      blocked = ∪ 行(活着的)        →  for j: blocked |= beats[j]
    //      winners = cand ∖ blocked      →  cand & ~blocked
    //      prey    = ∪ 行(赢家) ∩ live   →  for i: prey |= beats[i]; prey &= live
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
        //   live.has(i)           → 第 i 个 kind 有活着的 claim 吗
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
        };

        template <class Tree>
        using shape_of = typename[:std::meta::substitute(^^shape, leaf_infos(^^Tree)):];

        // Frame 是否在输入域里：这是唯一允许进入裁决的帧集合。
        template <class T, class Tuple>
        struct in_tuple : std::false_type
        {
        };

        template <class T, class... Ts>
        struct in_tuple<T, std::tuple<Ts...>>
            : std::bool_constant<(std::is_same_v<T, Ts> || ...)>
        {
        };

        // 每个 FSM 的 consume_t 是唯一真源；Tree 的输入域是它们的并集。
        template <class... Ts>
        struct type_list
        {
        };

        template <class T, class L>
        struct contains;

        template <class T, class... Ts>
        struct contains<T, type_list<Ts...>>
            : std::bool_constant<(std::is_same_v<T, Ts> || ...)>
        {
        };

        template <class L, class T>
        struct push;

        template <class... Ts, class T>
        struct push<type_list<Ts...>, T>
        {
            using type = std::conditional_t<contains<T, type_list<Ts...>>::value,
                                            type_list<Ts...>, type_list<Ts..., T>>;
        };

        template <class L, class... Ts>
        struct merge_impl
        {
            using type = L;
        };

        template <class L, class T, class... Ts>
        struct merge_impl<L, T, Ts...>
        {
            using type = typename merge_impl<typename push<L, T>::type, Ts...>::type;
        };

        template <class L, class Tuple>
        struct merge_tuple;

        template <class L, class... Ts>
        struct merge_tuple<L, std::tuple<Ts...>>
        {
            using type = typename merge_impl<L, Ts...>::type;
        };

        template <class L, class... Tuples>
        struct merge_all
        {
            using type = L;
        };

        template <class L, class T, class... Ts>
        struct merge_all<L, T, Ts...>
        {
            using type =
                typename merge_all<typename merge_tuple<L, T>::type, Ts...>::type;
        };

        template <class L, class Fsms>
        struct union_consume;

        template <class L, class... Fs>
        struct union_consume<L, std::tuple<Fs...>>
        {
            using type = typename merge_all<L, typename Fs::consume_t...>::type;
        };

        template <template <class> class FrameOf, class L>
        struct map_frames;

        template <template <class> class FrameOf, class... Es>
        struct map_frames<FrameOf, type_list<Es...>>
        {
            using type = std::tuple<FrameOf<Es>...>;
        };

        template <class Tree>
        using events_of =
            typename union_consume<type_list<>, typename shape_of<Tree>::fsms>::type;

        template <class Tree, template <class> class FrameOf>
        using frames_of = typename map_frames<FrameOf, events_of<Tree>>::type;
    } // namespace detail

    // 结构的公开自查数字
    template <class Tree>
    inline constexpr std::size_t leaf_count_v = detail::shape_of<Tree>::n;

    // ═══════════════════════════════════════════════════════════════════
    // 4.3 scope_of<Tree, Frame>()：这一帧会**推进**哪些列
    //
    //    这一帧推进哪些列，是**按 feed 重载在编译期算出来的**：
    //    哪片叶子的 machine 有对应重载，就置哪一位。
    //    没走到的列不推进，session 保留它们上一次上报的 report[i]。
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
    template <class Tree, template <class> class FrameOf>
    struct law_of
    {
        using shape = detail::shape_of<Tree>;
        using fsms_t = typename shape::fsms;
        using frame_types = detail::frames_of<Tree, FrameOf>;
        static constexpr std::size_t S = leaf_count_v<Tree>;
        static constexpr std::size_t F = std::tuple_size_v<frame_types>;

        // 上限先断言：S 超过 max_kinds 就别往下算了（理由见 max_kinds 的注释）
        static_assert(S >= 1, "一片叶子都没有的树没有意义");
        static_assert(S <= max_kinds,
                      "叶子太多：上限见 uhi::max_kinds（=64）。"
                      "位图放得下，是 validate 的 Kahn 会撞编译期运算上限");
        static_assert(F >= 1, "frame_types 不能为空");

        // 行：i 打得赢谁
        static constexpr std::array<bset<S>, S> beats = []() consteval {
            std::array<bset<S>, S> m{};
            const auto p = Tree::plan();
            validate(p);                 // 无自环 / 细化×互斥 / awaits 无环
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

            // 每个 kind 能吃哪些 frame：不手写，直接问 feed 重载。
            // 两个 kind 能冲突 ⇔ 它们的 accepted-frame 集合有交集。
            std::array<bset<F>, S> frames{};
            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                using M = std::tuple_element_t<I, fsms_t>;
                template for (constexpr auto J : std::ranges::views::indices(F))
                {
                    using Fr = std::tuple_element_t<J, frame_types>;
                    if constexpr (fsm::can_step<M, Fr>)
                        frames[I].set(J);
                }
                if (frames[I].none())
                    throw "kind 不接受 frame_types 里的任何帧（检查 feed 重载）";
            }

            // 关系两端必须能相遇：否则这条边是数据/实现不匹配，编译期就拒绝。
            for (index_t i = 0; i < S; ++i)
                for (index_t j = 0; j < S; ++j)
                    if (m[i].has(j) && (frames[i] & frames[j]).none())
                        throw "beats 跨 frame：关系两端没有任何共同帧";
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

        // 连通块（cell）只是 beats 的无向连通分量：部分之间没有边，
        // 所以运行期不需要 cell 循环，全局 bitset 就是各块的乘积。
        // 它留在编译期/文档里，不作为运行期状态。
    };

    // ═══════════════════════════════════════════════════════════════════
    // 6. 裁决器：一个纯函数
    //
    //    输入：report[N]（每个识别器最近一次上报的 step_t）
    //          beats[N]（编译期静态表）
    //    输出：win / lose 两个位图
    //
    //    它不知道 Frame、input_context、payload、span、识别器类型，
    //    也不调用任何识别器。调用 win/lose 是 session 的事。
    // ═══════════════════════════════════════════════════════════════════
    template <std::size_t S>
    struct verdicts
    {
        bset<S> win{}, lose{};
    };

    template <std::size_t S>
    constexpr verdicts<S> arbitrate(const std::array<step_t, S> &report,
                                    const std::array<bset<S>, S> &beats) noexcept
    {
        bset<S> in_play{}, want{};
        for (index_t i = 0; i < S; ++i)
        {
            if (report[i] != step_t::idle)
                in_play.set(i);
            if (report[i] == step_t::ready)
                want.set(i);
        }

        bset<S> blocked{};
        for (index_t j = 0; j < S; ++j)
            if (in_play.has(j))
                blocked |= beats[j];

        const bset<S> win = want & ~blocked;

        bset<S> lose{};
        for (index_t i = 0; i < S; ++i)
            if (win.has(i))
                lose |= beats[i];
        lose &= in_play & ~win;

        return {win, lose};
    }

    // ═══════════════════════════════════════════════════════════════════
    // 9. session：识别器和裁决器的唯一接线处
    //
    //    它拥有识别器和 report（最近一次上报）；每拍：
    //      1. 把帧/时间交给能接受的识别器，更新 report
    //      2. 调纯函数 arbitrate(report, law::beats)
    //      3. 把 win/lose 交给识别器
    //
    //    识别器不认识表，裁决器不认识识别器；session 是组合根。
    // ═══════════════════════════════════════════════════════════════════
    template <class Tree, template <class> class FrameOf>
    struct ui
    {
        using shape = detail::shape_of<Tree>;
        using law = law_of<Tree, FrameOf>;
        using frame_types = typename law::frame_types;
        static constexpr std::size_t S = shape::n;
        using fsms_t = typename shape::fsms;

        fsms_t fsms{};
        std::array<step_t, S> report{}; // 最近一次上报；裁决器唯一输入

        // 延迟绑定：只用叶子的类型（名字 + 载荷类型），不用对象。
        template <class Arena>
        constexpr void bind(Arena &a) noexcept
        {
            template for (constexpr auto I : std::ranges::views::indices(S))
                std::get<I>(fsms)
                    .bind(a);
        }

        // 推进一帧：只有能接受这个 Frame 的识别器被推进，其余保留上一次上报。
        // 不在 frame_types 里的帧直接丢弃：它没有被编译期校验过，不能进裁决。
        template <class Frame>
        constexpr void feed(const Frame &f, time_point now, trace *obs = nullptr) noexcept
        {
            if constexpr (!detail::in_tuple<Frame, frame_types>::value)
                return;

            constexpr bset<S> scope = scope_of<Tree, Frame>();
            if constexpr (scope.none())
                return;

            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                if constexpr (scope.has(static_cast<index_t>(I)))
                    report[I] = std::get<I>(fsms).feed(f, now);
            }
            settle(now, obs);
        }

        // 推进时间：每个识别器都回答；挂起的 ready 重新申辩。
        void tick(time_point now, trace *obs = nullptr) noexcept
        {
            template for (constexpr auto I : std::ranges::views::indices(S)) report[I] =
                std::get<I>(fsms).tick(now);
            settle(now, obs);
        }

      private:
        void settle(time_point now, trace *t) noexcept
        {
            const auto v = arbitrate(report, law::beats);

            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                if (v.lose.has(I))
                    apply<I>(false, now, t);
            }
            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                if (v.win.has(I))
                    apply<I>(true, now, t);
            }
        }

        template <std::size_t Flat>
        constexpr void apply(bool won, time_point at, trace *t) noexcept
        {
            auto &m = std::get<Flat>(fsms);
            // win/lose 返回“裁决之后我是什么状态”，session 立刻刷新 report，
            // 这样下一拍不会拿旧的 ready 再判一次。
            report[Flat] = won ? m.win() : m.lose();
            if (t != nullptr)
                t->push({law::names[Flat].view(),
                         won ? verdict::accepted : verdict::superseded, at});
        }
    };

    // ═══════════════════════════════════════════════════════════════════
    // 10. debug_ui：ui + 观测
    //
    //   这一层是"用户代码"：记录 trace。删掉它，第 1–9 节照样编译、照样跑。
    //   它不改变判决：只是把 ui 产生的 verdict 顺手抄一份。
    // ═══════════════════════════════════════════════════════════════════
    template <class Tree, template <class> class FrameOf>
    struct debug_ui : ui<Tree, FrameOf>
    {
        using law = law_of<Tree, FrameOf>;
        static constexpr std::size_t S = law::S;
        using ui_type = ui<Tree, FrameOf>;

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
//                └─ ui.feed(input_context<Event>, now)
//                     └─ 每个识别器 feed/tick → idle/hold/ready
//                          └─ 矩阵裁决（库内部，位运算）
//                               ├─ ready 且未被挡 → win()  → 发射这一帧的事件
//                               └─ 被赢家压掉     → lose() → 丢弃这次 claim
//
//    流不是队列：drag 是**一个长 claim**，鼠标每动一帧它就 ready 一次，
//    引擎调一次 win()；claim 自己保持 hold。不需要队列，不需要三个 kind，
//    也不需要 while (win()) 造事件。
//
//    时间：主循环每转一圈调一次 arena.tick()。挂起的候选靠它落地或超时。
// ═══════════════════════════════════════════════════════════════════════════
namespace app
{
    // 输入层的帧：原始事件 + 当前光标。它属于 app，不属于 uhi。
    template <class Event>
    struct input_context
    {
        Event event;
        double x{}, y{};
    };

    // ── 识别器的全部状态：一条 claim 的三态 ──────────────────────────
    //    cur：这一拍我对裁决器说的话（idle / hold / ready）。
    //    win 只发射当前事件；claim 是否结束由识别器自己决定。
    struct fsm_state
    {
        uhi::step_t cur{uhi::step_t::idle};

        uhi::step_t claim(uhi::step_t s) noexcept
        {
            // 已经 ready、还没裁决的 claim，新帧不能把它降回 hold。
            if (cur == uhi::step_t::ready && s != uhi::step_t::idle)
                s = uhi::step_t::ready;
            cur = s;
            return s;
        }

        // 默认：时间不改变状态（窗口需要过期的识别器自己覆盖 tick）
        uhi::step_t tick(uhi::time_point) noexcept
        {
            return claim(cur);
        }

        // 长 claim 发射完一个事件：claim 继续活着，等下一个事件
        void consume_event() noexcept
        {
            if (cur == uhi::step_t::ready)
                cur = uhi::step_t::hold;
        }

        // 一次性手势/让位：claim 结束
        void settle_claim() noexcept
        {
            cur = uhi::step_t::idle;
        }
    };

    // ── 帧就是 input_context<Event>：原始事件 + 当前光标。
    //    过滤（左键/按键 A）和事件分派都在 FSM 内部。

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
        //  过滤（只认左键）在 FSM 内部，不在 input_event 里。
        int operator()(const input_context<mousebutton_event> &c,
                       uhi::time_point now) noexcept
        {
            const auto &e = c.event;
            if (e.button != MouseButtons::eMOUSE_BUTTON_LEFT)
                return 0;
            if (e.press())
            {
                down = true;
                down_at = now;
                down_x = c.x;
                down_y = c.y;
                return 1;
            }
            if (e.release())
            {
                if (!down)
                    return 0;
                down = false;
                const bool quick = now - down_at <= uhi::time_point{click_max_ms};
                const bool still = std::hypot(c.x - down_x, c.y - down_y) <= drag_slop_px;
                return (quick && still) ? 2 : -1;
            }
            return 0;
        }
        void reset() noexcept
        {
            down = false;
        }
    };

    // ── 单击 ────────────────────────────────────────────────────────
    template <static_string Name, static_string FnName = Name>
    struct single_click : fsm_state
    {
        static constexpr auto name = Name;
        static constexpr auto fn_name = FnName;
        using consume_t = std::tuple<mousebutton_event>;

        struct payload_t
        {
            double x = 0, y = 0;
        } payload{};

        press_watch watch{};
        template <class Event>
            requires(uhi::detail::in_tuple<Event, consume_t>::value)
        uhi::step_t feed(const input_context<Event> &c, uhi::time_point now) noexcept
        {
            switch (watch(c, now))
            {
            case 1:
                return claim(uhi::step_t::hold);
            case 2:
                if (cur != uhi::step_t::ready) // 挂起的那一条不拿新坐标覆盖
                {
                    payload.x = c.x;
                    payload.y = c.y;
                }
                return claim(uhi::step_t::ready);
            case -1: // 不是单击：这次交互作废
                return claim(uhi::step_t::idle);
            default:
                // 挂起中（ready）就继续申辩；否则按下算 hold，其余算 idle。
                return claim(watch.down ? uhi::step_t::hold : cur);
            }
        }
        void *receiver_{};
        using invoke_type = void (*)(void *self) noexcept;
        invoke_type invoke_{};
        template <class Receiver>
        constexpr void bind(Receiver &ptr) noexcept
        {
            receiver_ = &ptr;
            invoke_ = invoke_type{[](void *ptr) noexcept {
                auto *self = static_cast<single_click *>(ptr);
                static_cast<Receiver *>(self->receiver_)
                    ->template invoke<fn_name>(self->payload);
            }};
        }
        constexpr uhi::step_t win() noexcept
        {
            assert(invoke_ != nullptr);
            invoke_(this);
            settle_claim(); // 投递完，这条申报结束
            return cur;
        }
        uhi::step_t lose() noexcept
        {
            watch.reset(); // 被更具体的赢了：这次交互丢掉
            settle_claim();
            return cur;
        }
    };

    // ── N 连击：连续 N 次有效单击，每两击之间独立计时 ────────────────
    //    所以间隔不必相同：只要每一档都 ≤ GapMs 就算。
    //    窗口一过就回 idle —— 把机会让给更简单的解释（单击）。
    template <int N, std::int64_t GapMs, static_string Name, static_string FnName = Name>
    struct click_chain : fsm_state
    {
        static constexpr auto name = Name;
        static constexpr auto fn_name = FnName;
        using consume_t = std::tuple<mousebutton_event>;

        struct payload_t
        {
            double x = 0, y = 0;
            int count = 0;
        } payload{};

        press_watch watch{};
        int clicks = 0;         // 已经完成的单击数
        uhi::time_point last{}; // 上一次松开的时刻
        bool armed = false;     // 正在窗口里等下一击

        template <class Event>
            requires(uhi::detail::in_tuple<Event, consume_t>::value)
        uhi::step_t feed(const input_context<Event> &c, uhi::time_point now) noexcept
        {
            if (armed && now - last > uhi::time_point{GapMs})
            {
                // 窗口过了：这一次放弃，但**这一帧照常处理** ——
                // 它可能正是新交互的按下。先显式清掉旧申报。
                clicks = 0;
                armed = false;
                watch.reset();
                claim(uhi::step_t::idle);
            }

            switch (watch(c, now))
            {
            case 1:
                return claim(uhi::step_t::hold);
            case 2:
                ++clicks;
                armed = true;
                last = now;
                if (cur != uhi::step_t::ready) // 挂起的那一条保留原载荷/跨度
                {
                    payload.x = c.x;
                    payload.y = c.y;
                    payload.count = clicks;
                }
                if (clicks >= N)
                {
                    clicks = 0;
                    armed = false;
                    return claim(uhi::step_t::ready);
                }
                return claim(uhi::step_t::hold);
            case -1: // 这一次不是有效单击
                clicks = 0;
                armed = false;
                return claim(uhi::step_t::idle);
            default:
                // 按下、等着、挂起，都算在场；纯路过才是 idle。
                return claim((armed || clicks > 0 || watch.down) ? uhi::step_t::hold
                                                                 : cur);
            }
        }
        // 时间窗只靠"喂帧"是过不去的：必须由 tick 推动。
        // 但 tick 不再从旁边改 cur —— 它回同一句话，引擎照常裁决。
        uhi::step_t tick(uhi::time_point now) noexcept
        {
            if (armed && now - last > uhi::time_point{GapMs})
            {
                clicks = 0;
                armed = false;
                watch.reset();
                return claim(uhi::step_t::idle);
            }
            return claim(cur);
        }

        void *receiver_{};
        using invoke_type = void (*)(void *self) noexcept;
        invoke_type invoke_{};
        template <class Receiver>
        constexpr void bind(Receiver &ptr) noexcept
        {
            receiver_ = &ptr;
            invoke_ = invoke_type{[](void *ptr) noexcept {
                auto *self = static_cast<click_chain *>(ptr);
                static_cast<Receiver *>(self->receiver_)
                    ->template invoke<fn_name>(self->payload);
            }};
        }
        constexpr uhi::step_t win() noexcept
        {
            assert(invoke_ != nullptr);
            invoke_(this);
            clicks = 0;
            armed = false;
            watch.reset();
            settle_claim();
            return cur;
        }
        uhi::step_t lose() noexcept
        {
            clicks = 0;
            armed = false;
            watch.reset();
            settle_claim();
            return cur;
        }
    };

    // ═══════════════════════════════════════════════════════════════
    //  拖拽：一个 kind，一个长 claim，三个相位事件
    //    begin  第一次越过阈值
    //    move   之后每一次移动（每帧最多一个事件）
    //    end    松手
    //  同一个 on_drag 回调，payload.phase 分流。
    //
    //  长 claim：begin/move/end 都是同一个 claim 的 ready，win 只发射
    //  当前事件；claim 一直活着，直到 end 发完或被 lose。
    //  没有队列，也没有 streaming 标志。
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

    // 一个长 claim：begin/move/end 都是同一个 claim 的 ready。
    // 按钮事件给 press/release，position2d_event 给 move —— 两个 feed
    // 重载，不再有 frame_type 的单类型限制。
    template <static_string Name, static_string FnName = Name>
    struct drag : fsm_state
    {
        static constexpr auto name = Name;
        static constexpr auto fn_name = FnName;
        using consume_t = std::tuple<mousebutton_event, position2d_event>;

        drag_payload payload{};
        drag_track t{};
        bool dragging = false;
        bool ending = false;

        template <class Event>
            requires(uhi::detail::in_tuple<Event, consume_t>::value)
        uhi::step_t feed(const input_context<Event> &c, uhi::time_point now) noexcept
        {
            if constexpr (std::is_same_v<Event, mousebutton_event>)
            {
                if (c.event.button != MouseButtons::eMOUSE_BUTTON_LEFT)
                    return claim(cur);
                if (c.event.press())
                    return drag_start(c, now);
                if (c.event.release())
                    return drag_end(c, now);
                return claim(cur);
            }
            else
                return drag_move(c, now);
        }

        uhi::step_t drag_start(const input_context<mousebutton_event> &c,
                               uhi::time_point now) noexcept
        {
            if (cur != uhi::step_t::idle) // 上一条 claim 还没结束：先收掉
                settle_claim();
            t.press(c.x, c.y);
            dragging = false;
            ending = false;
            return claim(uhi::step_t::hold);
        }

        uhi::step_t drag_move(const input_context<position2d_event> &c,
                              uhi::time_point now) noexcept
        {
            const auto &e = c.event;
            if (!t.down)
                return claim(uhi::step_t::idle);
            if (!dragging)
            {
                if (!t.past_slop(e.xpos, e.ypos))
                {
                    t.last(e.xpos, e.ypos);
                    return claim(uhi::step_t::hold);
                }
                dragging = true;
                // 这一帧的坐标就是这一帧的事件。
                payload = t.make(drag_payload::phase_t::begin, e.xpos, e.ypos, 0, 0);
                t.last(e.xpos, e.ypos);
                return claim(uhi::step_t::ready);
            }
            payload = t.make(drag_payload::phase_t::move, e.xpos, e.ypos,
                             e.xpos - t.last_x, e.ypos - t.last_y);
            t.last(e.xpos, e.ypos);
            return claim(uhi::step_t::ready);
        }

        uhi::step_t drag_end(const input_context<mousebutton_event> &c,
                             uhi::time_point now) noexcept
        {
            t.release();
            if (!dragging)
                return claim(uhi::step_t::idle); // 没拖过：claim 结束
            dragging = false;
            ending = true;
            payload = t.make(drag_payload::phase_t::end, c.x, c.y, 0, 0);
            return claim(uhi::step_t::ready);
        }

        void *receiver_{};
        using invoke_type = void (*)(void *self) noexcept;
        invoke_type invoke_{};
        template <class Receiver>
        constexpr void bind(Receiver &ptr) noexcept
        {
            receiver_ = &ptr;
            invoke_ = invoke_type{[](void *ptr) noexcept {
                auto *self = static_cast<drag *>(ptr);
                static_cast<Receiver *>(self->receiver_)
                    ->template invoke<fn_name>(self->payload);
            }};
        }
        constexpr uhi::step_t win() noexcept
        {
            assert(invoke_ != nullptr);
            invoke_(this);
            if (ending)
            {
                ending = false;
                settle_claim(); // end 是最后一个事件
            }
            else
                consume_event(); // claim 继续，等下一个事件
            return cur;
        }
        uhi::step_t lose() noexcept
        {
            t = {};
            dragging = false;
            ending = false;
            settle_claim();
            return cur;
        }
    };

    // ── 悬停：指针进入窗口时报一次（离开不算事件）──────────────────
    template <static_string Name, static_string FnName = Name>
    struct hover : fsm_state
    {
        static constexpr auto name = Name;
        static constexpr auto fn_name = FnName;
        using consume_t = std::tuple<cursor_enter_event>;

        template <class Event>
            requires(uhi::detail::in_tuple<Event, consume_t>::value)
        uhi::step_t feed(const input_context<Event> &c, uhi::time_point now) noexcept
        {
            if (c.event.value)
                return claim(uhi::step_t::ready);
            return claim(cur); // 离开不产生事件
        }
        void *receiver_{};
        using invoke_type = void (*)(void *self) noexcept;
        invoke_type invoke_{};
        template <class Receiver>
        constexpr void bind(Receiver &ptr) noexcept
        {
            receiver_ = &ptr;
            invoke_ = invoke_type{[](void *ptr) noexcept {
                auto *self = static_cast<hover *>(ptr);
                static_cast<Receiver *>(self->receiver_)->template invoke<fn_name>();
            }};
        }
        constexpr uhi::step_t win() noexcept
        {
            assert(invoke_ != nullptr);
            invoke_(this);
            settle_claim();
            return cur;
        }
        uhi::step_t lose() noexcept
        {
            settle_claim();
            return cur;
        }
    };

    // ── 滚轮：一格一次（delta 交给载荷）────────────────────────────
    template <static_string Name, static_string FnName = Name>
    struct wheel : fsm_state
    {
        static constexpr auto name = Name;
        static constexpr auto fn_name = FnName;
        using consume_t = std::tuple<scroll_event>;

        struct payload_t
        {
            int delta = 0;
        } payload{};

        template <class Event>
            requires(uhi::detail::in_tuple<Event, consume_t>::value)
        uhi::step_t feed(const input_context<Event> &c, uhi::time_point now) noexcept
        {
            payload.delta = static_cast<int>(c.event.yoffset);
            return claim(uhi::step_t::ready);
        }
        void *receiver_{};
        using invoke_type = void (*)(void *self) noexcept;
        invoke_type invoke_{};
        template <class Receiver>
        constexpr void bind(Receiver &ptr) noexcept
        {
            receiver_ = &ptr;
            invoke_ = invoke_type{[](void *ptr) noexcept {
                auto *self = static_cast<wheel *>(ptr);
                static_cast<Receiver *>(self->receiver_)
                    ->template invoke<fn_name>(self->payload);
            }};
        }
        constexpr uhi::step_t win() noexcept
        {
            assert(invoke_ != nullptr);
            invoke_(this);
            settle_claim();
            return cur;
        }
        uhi::step_t lose() noexcept
        {
            settle_claim();
            return cur;
        }
    };

    // ── 键盘 A ─────────────────────────────────────────────────────
    template <static_string Name, static_string FnName = Name>
    struct key_a : fsm_state
    {
        static constexpr auto name = Name;
        static constexpr auto fn_name = FnName;
        using consume_t = std::tuple<keyboard_event>;

        template <class Event>
            requires(uhi::detail::in_tuple<Event, consume_t>::value)
        uhi::step_t feed(const input_context<Event> &c, uhi::time_point now) noexcept
        {
            if (c.event.press() && c.event.key == Key::eA)
                return claim(uhi::step_t::ready);
            return claim(cur); // 不改变申报
        }

        using invoke_type = void (*)(void *self) noexcept;
        void *receiver_{};
        invoke_type invoke_{};
        template <class Receiver>
        constexpr void bind(Receiver &ptr) noexcept
        {
            receiver_ = &ptr;
            invoke_ = invoke_type{[](void *self) noexcept {
                auto *m = static_cast<key_a *>(self);
                static_cast<Receiver *>(m->receiver_)->template invoke<fn_name>();
            }};
        }
        constexpr uhi::step_t win() noexcept
        {
            assert(invoke_ != nullptr);
            invoke_(this);
            settle_claim();
            return cur;
        }
        uhi::step_t lose() noexcept
        {
            settle_claim();
            return cur;
        }
    };

    // ── 叶子：name 是 kind 的身份（进日志）；最后一个参数是回调名 ───
    using l_click = uhi::leaf<single_click<"on_click">>;
    using l_dbl = uhi::leaf<click_chain<2, dbl_gap_ms, "on_dbl">>;
    using l_triple = uhi::leaf<click_chain<3, triple_gap_ms, "on_triple">>;
    using l_drag = uhi::leaf<drag<"on_drag">>;
    using l_hover = uhi::leaf<hover<"on_hover">>;
    using l_wheel = uhi::leaf<wheel<"on_wheel">>;
    using l_key = uhi::leaf<key_a<"on_key_A">>;

    // ── 树：只声明"谁比谁具体"，不写任何顺序 ───────────────────────
    //    refines<A, B> 读作"A 等 B" ⇒ **B 更具体，B 赢**（第二个参数才是细的）。
    //      拖拽 > 单击、双击 > 单击、三击 > 双击 > 单击
    //    拖拽和双击/三击之间没有边 —— 免得"刚点完一下"的挂起双击把拖拽挡住。
    using Tree = uhi::all_of<
        uhi::refines<l_click, uhi::all_of<l_drag, uhi::refines<l_dbl, l_triple>>>, //
        l_hover,                                                                   //
        l_wheel,                                                                   //
        l_key>;

    // 事件 -> Frame 的唯一映射。事件清单不写：Tree 里每个 FSM 的
    // consume_t 并集就是 frame_types；这里只声明“原始事件怎么包成 Frame”。
    template <class Event>
    using frame_of = input_context<Event>;

    using ui_type = uhi::debug_ui<Tree, frame_of>;

    // 回调打到哪儿：自检要靠它验证 sink 那一路确实通了
    struct tally
    {
        int click{}, dbl{}, triple{}, hover{}, wheel{}, key_a{};
        int drag_begin{}, drag_move{}, drag_end{};
        double x{}, y{}, dx{}, dy{}, total_dx{}, total_dy{};
        double move_x{}, move_y{}, move_dx{}, move_dy{};
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

    // 把这一帧的播报打出来（name = kind / verdict / at）
    //   拖拽的相位由 on_drag 回调打印，trace 里不重复刷屏；悬停同理。
    inline void report(const ui_type &u) noexcept
    {
        for (const auto &e : u.events())
        {
            if (e.name == "on_hover" || e.name == "on_drag")
                continue;
            std::println("  [t={:>6}ms] {:<14} {:<3}", e.at.count(), e.name,
                         uhi::to_string(e.v));
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
                    // 每一次 move 帧都是一条独立事件；坐标就是这一帧的坐标。
                    ++self.tally.drag_move;
                    self.tally.move_x = p.x;
                    self.tally.move_y = p.y;
                    self.tally.move_dx = p.dx;
                    self.tally.move_dy = p.dy;
                    std::println("  >>> 拖拽·移动 (到 {:.1f},{:.1f}; d={:.1f},{:.1f})",
                                 p.x, p.y, p.dx, p.dy);
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

            // 平台事件 → input_context<Event>：唯一的适配层。
            // 过滤（左键/按键 A）和事件分派都在 FSM 内部。
            method<"input_event">([]<class Self, class E>(Self &self, E e) noexcept {
                using T = std::remove_cvref_t<E>;
                const auto now = now_of(self);
                self.now = now;
                if constexpr (std::is_same_v<T, position2d_event>)
                {
                    self.cursor_x = e.xpos;
                    self.cursor_y = e.ypos;
                }
                self.ui.feed(frame_of<T>{e, self.cursor_x, self.cursor_y}, now);
            }));
    }

    // ═══════════════════════════════════════════════════════════════════
    //  自检：不建窗口，但**走和窗口模式同一个 arena**
    //    伪造平台事件 → arena.input_event → 翻译 → 矩阵 → sink → 回调
    //    所以它同时测到：make_aggregate 那层、bind、input_event、tick、载荷。
    // ═══════════════════════════════════════════════════════════════════
    inline int selftest()
    {
        return 1;
    }
} // namespace app

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
