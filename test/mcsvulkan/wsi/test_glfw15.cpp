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
using mcs::vulkan::meta::template_args;

using mcs::vulkan::runtime_type_v;
using mcs::vulkan::ring_buffer;

// ═══════════════════════════════════════════════════════════════════════
// 【原样保留】事件系统
// ═══════════════════════════════════════════════════════════════════════
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

    template <class Obj, static_string name>
    struct invoke_type_info
    {
        constexpr static bool in_function = find_function(^^Obj, name).has_value();
        constexpr static bool in_data_member =
            find_nonstatic_data_member(^^Obj, name).has_value();

        template <class... Args>
        static consteval auto can_noexcept_invoke()
        {
            // NOTE: 默认反射的是当前的类型，而不是从父类继承下来的
            if constexpr (requires(Obj &&obj, Args &&...args) {
                              {
                                  obj.template invoke<name>(std::forward<Args>(args)...)
                              } noexcept;
                          })
                return true;
            else if constexpr (in_function || in_data_member)
            {
                constexpr auto m = in_function ? *find_function(^^Obj, name)
                                               : *find_nonstatic_data_member(^^Obj, name);
                return requires(Obj &&obj, Args &&...args) {
                    { obj.[:m:](std::forward<Args>(args)...) } noexcept;
                } || requires(Obj &&obj, Args &&...args) {
                    { obj.template[:m:](std::forward<Args>(args)...) } noexcept;
                };
            }
            else
                return false;
        }
    };

    template <static_string name, typename Obj, typename... Ts>
    static constexpr void dispach_invoke(Obj *ptr, std::tuple<Ts...> *, const void *type,
                                         void *p, Extra &&...e) noexcept
        requires(
            (invoke_type_info<Obj, name>::template can_noexcept_invoke<Ts, Extra...>() ||
             ...))
    {
        using Info = invoke_type_info<Obj, name>;
        using mcs::vulkan::runtime_type_v;
        auto &obj = *ptr;
        auto invoke = [&]<typename... Args>(Args &&...args) noexcept {
            if constexpr (requires() {
                              {
                                  obj.template invoke<name>(std::forward<Args>(args)...)
                              } noexcept;
                          })
                obj.template invoke<name>(std::forward<Args>(args)...);
            else
            {
                constexpr auto m = Info::in_function
                                       ? *find_function(^^Obj, name)
                                       : *find_nonstatic_data_member(^^Obj, name);
                if constexpr (requires() {
                                  { obj.[:m:](std::forward<Args>(args)...) } noexcept;
                              })
                    obj.[:m:](std::forward<Args>(args)...);
                else
                    obj.template[:m:](std::forward<Args>(args)...);
            }
        };
        ((Info::template can_noexcept_invoke<Ts, Extra...>() &&
          type == runtime_type_v<Ts> &&
          (invoke(*static_cast<Ts *>(p), std::forward<Extra>(e)...), true)) ||
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
//    引擎只规定"帧 → claim_state"，帧和识别器都由本案例自己写（见下）。
// ═══════════════════════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════════════
//  矩阵裁决引擎 —— 已内联进本文件（不再是 include，直接在这里改）
//    这一整段原本是 intent/v3/src/uhi_matrix.cpp 的第 1–9 节（引擎，不含 demo main）。
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

namespace intent
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
    enum class claim_state : std::uint8_t
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
    //   arbiter → 识别器（裁决之后，只可能调一个）：
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
    namespace recognizer
    {
        // 识别器只做三件事：吃帧、过时间、投递/退休。
        // 裁决器只吃 claim_state 数组，不认识 Frame、payload、span。
        template <class M, class Frame>
        concept can_feed = requires(M &m, const Frame &f, time_point t) {
            { m.feed(f, t) } noexcept -> std::same_as<claim_state>;
        };

        template <class M>
        concept protocol = std::default_initializable<M> && requires(M &m, time_point t) {
            typename M::consume_t; // 我处理哪些 input_frame change
            { m.tick(t) } noexcept -> std::same_as<claim_state>;
            { m.win() } noexcept -> std::same_as<claim_state>;
            { m.lose() } noexcept -> std::same_as<claim_state>;
        };
    } // namespace recognizer

    // 一条观测记录（定长、可拷贝、不分配）
    struct observation
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
        std::array<observation, capacity> buf{};
        std::size_t n = 0, dropped_n = 0;

        constexpr void push(const observation &e) noexcept
        {
            if (n < capacity)
                buf[n++] = e;
            else
                ++dropped_n;
        }
        constexpr std::span<const observation> events() const noexcept
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

    // 无观测版本：编译期被 if constexpr 剪掉，不产生任何记录。
    struct null_trace
    {
    };

    // ═══════════════════════════════════════════════════════════════════
    // 1. plan：编译期的声明表（L1 同态律）
    //      kinds  : 每片叶子的名字 / 回调名
    //      awaits : (i,j) —— i 是 j 的抽象版
    //      excl   : (i,j) —— i 与 j 不能共存
    // ═══════════════════════════════════════════════════════════════════
    struct kind_desc
    {
        static_string name{};    // 日志标签：不参与查找/分发，不要求唯一
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
    template <recognizer::protocol Fsm>
    struct leaf
    {
        using protocol = Fsm; // 叶子协议：我的状态机类型（反射靠它认出叶子）
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
    //    name 只是日志标签，不参与查找/分发，所以**不要求唯一**；
    //    fn_name 是回调名，也不查重 —— 多个 kind 共用一个回调是合法用法。
    //    真正的结构约束是“同一个 FSM 类型不能在同一棵树里出现两次”，
    //    那个在 shape 里查。
    // ═══════════════════════════════════════════════════════════════════
    consteval void validate(const plan_data &p)
    {
        const std::size_t n = p.kinds.size(); // 多少个 kind：这是长度

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

        // awaits 无环检查（拓扑排序 / Kahn）。
        //
        // 语义：awaits 里的 (i, j) 读作「i 在等 j」。
        // 有环 ⇔ 存在一组节点互相等待（死锁），细化关系不成立 → throw。
        //
        // 做法（三句话）：
        //   1. indeg[i] = 「i 还欠几个人没等到」（不是图论入度，名字别较真）
        //   2. 反复找 indeg == 0 的节点（谁也不欠 → 自由），完成它，
        //      再让所有「在等它」的节点少欠一个（--indeg）
        //   3. 某一轮一个自由的都没找到 → 剩下的互相等 → 有环
        //
        // 复杂度 O(n² · |awaits|)：每完成一个节点就重扫整张 awaits 表。
        // max_kinds = 64 的注释里说「爆点在 validate 的 Kahn」就是指这里。
        std::vector<std::size_t> indeg(n, 0); // indeg[i] = i 还欠几个"等"
        for (auto [i, j] : p.awaits)
            ++indeg[i]; // i 等 j：i 欠一个

        std::vector<bool> done(n, false);     // done[i]  = i 是否已完成
        for (std::size_t left = n; left > 0;) // left = 还没完成的节点数
        {
            bool prog = false; // 本轮是否至少完成了一个
            for (index_t i = 0; i < n; ++i)
                if (!done[i] && indeg[i] == 0) // 不欠任何人了 → 自由
                {
                    done[i] = true;
                    --left;
                    prog = true;
                    // 完成 i 之后，所有"在等 i"的 x 都少欠一个。
                    // (x, y) 且 y == i 就是「x 在等 i」。
                    for (auto [x, y] : p.awaits)
                        if (y == i)
                            --indeg[x];
                    // 注意：内层 for 不 break，本轮内新变自由的节点会顺手处理。
                }
            if (!prog) // 一轮下来没人自由 → 死锁 → 有环
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
    //      advance       第 i 位 = "本拍被这一帧推进过"
    //
    //    ── 为什么是它：裁决的每一步都是集合运算，一一对应 ──
    //      blocked = ∪ 行(活着的)        →  for j: blocked |= beats[j]
    //      win     = ready ∖ blocked     →  ready & ~blocked
    //      lose    = ∪ 行(赢家) ∩ live   →  for i: lose |= beats[i]; lose &= live
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
        //   advance.has(i)        → 本帧推进了这一列吗
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
    //    一个类型说清几件事，全部是编译期：
    //      n              : 几片叶子
    //      fsms           : 它们的状态机元组（声明序）
    //      consume_t      : 所有 leaf consume_t 的去重并集
    //      consume_of<I>  : 第 I 片叶子自己的 consume_t（不去重）
    //      unique_machines: 同一个 FSM 类型没有出现两次
    //
    //    "这棵树认不认识帧 F"不在这里另开一个成员：用 advance_mask<Tree,F>() 回答，
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

        // 按声明序收集叶子（叶子 = 声明了 protocol 的节点）
        consteval std::vector<std::meta::info> leaf_infos(std::meta::info node)
        {
            constexpr auto ctx = std::meta::access_context::current();
            std::vector<std::meta::info> out;
            for (auto m : std::meta::members_of(node, ctx))
                if (std::meta::has_identifier(m) &&
                    std::meta::identifier_of(m) == "protocol")
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
            static constexpr std::size_t leaf_count = sizeof...(Ls);
            using recognizers = std::tuple<typename Ls::protocol...>;

            // 同一个 FSM 类型出现两次 ⇒ 两个无法区分的 claim；这是结构错误。
            // name 是日志标签，不承担这个约束。
            static constexpr bool unique_machines = []() consteval {
                constexpr std::size_t N = sizeof...(Ls);
                std::array<std::meta::info, N> m{^^typename Ls::protocol...};
                for (std::size_t i = 0; i < N; ++i)
                    for (std::size_t j = i + 1; j < N; ++j)
                        if (m[i] == m[j])
                            return false;
                return true;
            }();
            static_assert(unique_machines, "同一个 FSM 类型不能在同一棵树里出现两次");

            // 所有 leaf 的 consume_t 并集，按第一次出现的顺序去重。
            static consteval auto consume_args()
            {
                std::vector<std::meta::info> out;
                auto add = [&out]<class L>() {
                    for (auto a : template_args<typename L::protocol::consume_t>())
                    {
                        bool seen = false;
                        for (auto b : out)
                            if (a == b)
                            {
                                seen = true;
                                break;
                            }
                        if (!seen)
                            out.push_back(a);
                    }
                };
                (add.template operator()<Ls>(), ...);
                return out;
            }

            using consume_t =
                typename[:std::meta::substitute(^^std::tuple, consume_args()):];
            // consume_t 只用于编译期校验（关系两端有没有共同 change）；
            // 它不是运行期路由表，路由由 feed(frame<T>) 的重载完成。

            // 第 I 个 leaf 自己的 consume_t：不去重，用来和 I 对账。
            template <std::size_t I>
            using consume_of = typename std::tuple_element_t<I, recognizers>::consume_t;
        };

        template <class Tree>
        using shape_of = typename[:std::meta::substitute(^^shape, leaf_infos(^^Tree)):];

        // T 是否在 Tuple 的模板实参里
        template <class T, class Tuple>
        static consteval bool in_tuple() noexcept
        {
            for (auto a : template_args<Tuple>())
                if (std::meta::dealias(a) == std::meta::dealias(^^T))
                    return true;
            return false;
        }

    } // namespace detail

    // ═══════════════════════════════════════════════════════════════════
    // 4.3 advance_mask<Tree, Frame>()：这一帧会**推进**哪些列
    //
    //    裁剪的全部规则只有一句：
    //      有 feed(Frame) 重载的列推进，其余列不动。
    //    判据是 fsm::can_step，编译期算成 bitset；运行期没有 tag/映射表。
    //
    //    没推进的列保留上一次 report[i]，但它们**仍然参与裁决**。
    //    所以 position 帧只推进 drag，click 的活 claim 还在 report 里；
    //    drag 赢的时候照样能把 click lose 掉。
    // ═══════════════════════════════════════════════════════════════════
    template <class Tree, class Frame>
    consteval auto advance_mask() noexcept
    {
        using Shape = typename detail::shape_of<Tree>;
        using Recognizers = Shape::recognizers;
        constexpr std::size_t S = Shape::leaf_count;
        bset<S> m{};
        template for (constexpr auto I : std::ranges::views::indices(S))
        {
            if constexpr (recognizer::can_feed<std::tuple_element_t<I, Recognizers>,
                                               Frame>)
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
        using shape = detail::shape_of<Tree>;
        // 所有 leaf 的 consume_t 去重并集；运行期裁决只读 beats，不读它。
        using change_types = typename shape::consume_t;
        static constexpr std::size_t S = shape::leaf_count;
        static constexpr std::size_t C = std::tuple_size_v<change_types>;

        // 上限先断言：S 超过 max_kinds 就别往下算了（理由见 max_kinds 的注释）
        static_assert(S >= 1, "一片叶子都没有的树没有意义");
        static_assert(S <= max_kinds,
                      "叶子太多：上限见 intent::max_kinds（=64）。"
                      "位图放得下，是 validate 的 Kahn 会撞编译期运算上限");
        static_assert(C >= 1, "change_types 不能为空");

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

            // 每个 kind 消费哪些 change：来自它自己的 consume_t（不去重）。
            // consumes 只在编译期用，不参与运行期裁决；运行期路由是
            // feed(frame<T>) 的重载。这里只回答一条边两端有没有共同 change。
            std::array<bset<C>, S> consumes{};
            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                using owned = typename shape::template consume_of<I>;
                template for (constexpr auto J : std::ranges::views::indices(C))
                {
                    using Ch = std::tuple_element_t<J, change_types>;
                    if constexpr (detail::in_tuple<Ch, owned>())
                        consumes[I].set(J);
                }
                if (consumes[I].none())
                    throw "kind 不消费任何 input_frame change";
            }

            // 关系两端必须能相遇：否则这条边是数据/实现不匹配，编译期就拒绝。
            for (index_t i = 0; i < S; ++i)
                for (index_t j = 0; j < S; ++j)
                    if (m[i].has(j) && (consumes[i] & consumes[j]).none())
                        throw "beats 跨 change：关系两端没有共同变化类型";
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
    };

    // ═══════════════════════════════════════════════════════════════════
    // 6. 裁决器：一个纯函数
    //
    //    输入：report[N]（每个识别器最近一次上报的 step_t）
    //          beats[N]（编译期静态表）
    //    输出：win / lose 两个位图
    //
    //    它不知道 Frame、arena_event、payload、span、识别器类型，
    //    也不调用任何识别器。调用 win/lose 是 arbiter 的事。
    // ═══════════════════════════════════════════════════════════════════
    template <std::size_t S>
    struct decision
    {
        bset<S> win{}, lose{}; // 不相交；win 是反链
    };

    // 裁决 = 在 live claim 里取 ready 的极大元。四步：
    //
    //   ① live   = report != idle      （hold + ready；hold 仍然挡人）
    //      ready  = report == ready     （这一拍想说话）
    //
    //   ② blocked = ∪ beats[j], j∈live
    //      任何活着的 claim 打得赢的全体。hold 也要算进来：
    //      一个还没 ready 的 superior 仍然压住下面的人。
    //
    //   ③ win = ready ∖ blocked
    //      真正能说话的。beats 是严格偏序，所以 win 一定是反链
    //      （两个赢家不可能互相打得赢）。
    //
    //   ④ lose = (∪ beats[i], i∈win) ∩ live ∖ win
    //      被赢家压掉的活 claim；先让位，再放行。
    //
    //    没进 win/lose 的 ready 是 pending：被 hold 的 superior 挡住，
    //    report 保持 ready，下一拍继续申辩。
    template <std::size_t S>
    constexpr decision<S> arbitrate(const std::array<claim_state, S> &report,
                                    const std::array<bset<S>, S> &beats) noexcept
    {
        // ① 谁还活着 / 谁这一拍想说话
        bset<S> live{}, ready{};
        for (index_t i = 0; i < S; ++i)
        {
            if (report[i] != claim_state::idle)
                live.set(i);
            if (report[i] == claim_state::ready)
                ready.set(i);
        }

        // ② 活着的 claim 能压住谁
        bset<S> blocked{};
        for (index_t j = 0; j < S; ++j)
            if (live.has(j))
                blocked |= beats[j];

        // ③ ready 里的极大元
        const bset<S> win = ready & ~blocked;

        // ④ 赢家的猎物：仍然活着、但被赢家压掉的 claim
        bset<S> lose{};
        for (index_t i = 0; i < S; ++i)
            if (win.has(i))
                lose |= beats[i];
        lose &= live & ~win;

        return {win, lose};
    }

    // ═══════════════════════════════════════════════════════════════════
    // 9. arbiter：识别器和裁决器的唯一接线处
    //
    //    它拥有识别器和 report（最近一次上报）；每拍：
    //      1. 裁剪：只推进能接受该 Frame 的列
    //      2. 裁决：arbitrate(report, law::beats) -> decision
    //      3. 落地：先 lose（让位），再 win（放行）
    //
    //    feed/tick 各有两个版本：
    //      feed(frame, now)                  主 ui：不观测
    //      feed<Frame, Trace>(frame, now, t) 子类 debug 用：Trace 是模板参数
    //    Trace 不是指针，不进 arbiter 的成员/ABI，也没有虚函数。
    // ═══════════════════════════════════════════════════════════════════
    template <class Tree>
    struct arbiter
    {
        using shape = detail::shape_of<Tree>;
        using law = law_of<Tree>;
        static constexpr std::size_t S = shape::leaf_count;
        using Recognizers = typename shape::recognizers;

        Recognizers recognizers{};
        std::array<claim_state, S> reports{}; // 最近一次上报；裁决器唯一输入

        // 延迟绑定：只用叶子的类型（名字 + 载荷类型），不用对象。
        template <class Arena>
        constexpr void bind(Arena &a) noexcept
        {
            template for (constexpr auto I : std::ranges::views::indices(S))
                std::get<I>(recognizers)
                    .bind(a);
        }

        // 无观测版本
        template <class Frame>
        constexpr void feed(const Frame &f, time_point now) noexcept
        {
            null_trace t{};
            feed<Frame, null_trace>(f, now, t);
        }

        // 带 Trace 模板参数的版本：由 debug_arbiter 调用
        template <class Frame, class Trace>
        constexpr void feed(const Frame &f, time_point now, Trace &t) noexcept
        {
            constexpr bset<S> advance = advance_mask<Tree, Frame>();
            if constexpr (advance.none())
                return; // 没有列接受这个 Frame：不推进，也不再裁决

            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                if constexpr (advance.has(static_cast<index_t>(I)))
                    reports[I] = std::get<I>(recognizers).feed(f, now);
            }
            resolve(now, t);
        }

        // 无观测版本
        constexpr void tick(time_point now) noexcept
        {
            null_trace t{};
            tick<null_trace>(now, t);
        }

        // 带 Trace 模板参数的版本：由 debug_arbiter 调用
        template <class Trace>
        constexpr void tick(time_point now, Trace &t) noexcept
        {
            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                reports[I] = std::get<I>(recognizers).tick(now);
            }
            resolve(now, t);
        }

      private:
        // report -> verdict -> 落地。先让位，再放行。
        template <class Trace>
        constexpr void resolve(time_point now, Trace &t) noexcept
        {
            const auto d = arbitrate(reports, law::beats);

            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                if (d.lose.has(I))
                    deliver<I>(false, now, t);
            }
            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                if (d.win.has(I))
                    deliver<I>(true, now, t);
            }
        }

        // 唯一副作用点：win/lose 返回“裁决之后我是什么状态”，
        // 立刻写回 report；下一拍不会拿旧的 ready 再判一次。
        template <std::size_t Flat, class Trace>
        constexpr void deliver(bool won, time_point at, Trace &t) noexcept
        {
            auto &m = std::get<Flat>(recognizers);
            reports[Flat] = won ? m.win() : m.lose();

            if constexpr (!std::is_same_v<Trace, null_trace>)
                t.push({law::names[Flat].view(),
                        won ? verdict::accepted : verdict::superseded, at});
        }
    };

    // ═══════════════════════════════════════════════════════════════════
    // 10. debug_ui：ui + 观测
    //
    //   只覆盖 feed/tick 的无观测版本，显式把 trace 作为模板参数传下去。
    //   主 arbiter 没有 trace 成员、没有虚函数；删掉 debug_ui，第 1–9 节照样跑。
    // ═══════════════════════════════════════════════════════════════════
    template <class Tree, class Trace = trace>
    struct debug_arbiter : arbiter<Tree>
    {
        using law = law_of<Tree>;
        using base = arbiter<Tree>;

        Trace obs{};

        template <class Frame>
        constexpr void feed(const Frame &f, time_point now) noexcept
        {
            base::template feed<Frame, Trace>(f, now, obs);
        }

        constexpr void tick(time_point now) noexcept
        {
            base::template tick<Trace>(now, obs);
        }

        template <class Self>
        constexpr decltype(auto) events(this Self &&self) noexcept
            requires(requires() { std::forward_like<Self>(self.obs).events(); })
        {
            return std::forward_like<Self>(self.obs).events();
        }
        template <class Self>
        constexpr void clear_events(this Self &&self) noexcept
            requires(requires() { std::forward_like<Self>(self.obs).clear(); })
        {
            std::forward_like<Self>(self.obs).clear();
        }
        template <class Self>
        constexpr decltype(auto) dropped(this Self &&self) noexcept
            requires(requires() { std::forward_like<Self>(self.obs).dropped(); })
        {
            return std::forward_like<Self>(self.obs).dropped();
        }
    };

} // namespace intent

// ═══════════════════════════════════════════════════════════════════════════
//  arena_event：GLFW 与识别器之间的跨平台中间层
//    event_context：变化前仍成立的全部状态（keys / buttons / modifier / pointer）
//    frame<T>：context + 本帧变化 T（T 是具体的 change 类型）
//  FSM 只看 input_frame::frame<T>，不看 GLFW 事件。
// ═══════════════════════════════════════════════════════════════════════════
struct input_frame
{
    enum class PointerDevice : std::uint8_t
    {
        Unknown,
        Touch,
        Mouse,
        Stylus,
        InvertedStylus,
        Trackpad
    };

    struct PointerId
    {
        constexpr static std::uint32_t Invalid = static_cast<std::uint32_t>(~0);
        std::uint32_t value{Invalid};
    };

    using Key = mcs::vulkan::event::Key;
    using scroll_event = mcs::vulkan::event::scroll_event;
    using cursor_enter_event = mcs::vulkan::event::cursor_enter_event;

    enum class MouseButton : std::uint8_t
    {
        eUNDEFINED = 0xFF,
        eMOUSE_BUTTON_1 = 0,
        eMOUSE_BUTTON_2 = 1,
        eMOUSE_BUTTON_3 = 2,
        eMOUSE_BUTTON_4 = 3,
        eMOUSE_BUTTON_5 = 4,
        eMOUSE_BUTTON_6 = 5,
        eMOUSE_BUTTON_7 = 6,
        eMOUSE_BUTTON_8 = 7,
        eMOUSE_BUTTON_LAST = eMOUSE_BUTTON_8,
        eMOUSE_BUTTON_LEFT = eMOUSE_BUTTON_1,
        eMOUSE_BUTTON_RIGHT = eMOUSE_BUTTON_2,
        eMOUSE_BUTTON_MIDDLE = eMOUSE_BUTTON_3,
        eSIZE = static_cast<std::uint8_t>(eMOUSE_BUTTON_LAST) + 1
    };

    enum class HeldState : std::uint8_t
    {
        Up,
        Down
    };
    enum class Transition : std::uint8_t
    {
        Press,
        Release
    };
    enum class PointerState : std::uint8_t
    {
        Outside,
        Hover,
        Down,
        DownMove
    };

    struct event_context
    {
        std::array<HeldState, std::to_underlying(Key::eSIZE)> keys{};
        std::array<HeldState, static_cast<std::size_t>(MouseButton::eSIZE)> buttons{};
        ModifierKey modifier{};

        PointerState pointer_state = PointerState::Outside;
        PointerDevice pointer_device = PointerDevice::Unknown;
        double pointer_x = 0.0;
        double pointer_y = 0.0;
        bool pointer_position_valid = false;

        constexpr decltype(auto) get_keyboard_event(this auto &&self, Key k) noexcept
        {
            return std::forward_like<decltype(self)>(self.keys[std::to_underlying(k)]);
        }

        constexpr decltype(auto) get_mousebutton_event(this auto &&self,
                                                       MouseButton b) noexcept
        {
            return std::forward_like<decltype(self)>(self.buttons[std::to_underlying(b)]);
        }

        constexpr bool any_button_down() const noexcept
        {
            for (auto s : buttons)
                if (s == HeldState::Down)
                    return true;
            return false;
        }
    };

    struct key_change
    {
        Key key{};
        Transition transition{};
        bool repeat{};
        ModifierKey modifier{};
    };

    struct button_change
    {
        MouseButton button{};
        Transition transition{};
        bool repeat{};
        ModifierKey modifier{};
    };

    struct move_change
    {
        double x = 0.0, y = 0.0;
        double dx = 0.0, dy = 0.0;
    };

    struct char_change
    {
        unsigned codepoint = 0;
    };

    template <class T>
    struct frame
    {
        event_context context;
        T change;
    };

    // NOTE: 枚举转发到下游的全部类型
    template <class T>
    static consteval bool is_change()
    {
        return std::same_as<T, key_change> || std::same_as<T, button_change> ||
               std::same_as<T, move_change> ||
               std::same_as<T, mcs::vulkan::event::scroll_event> ||
               std::same_as<T, mcs::vulkan::event::cursor_enter_event> ||
               std::same_as<T, char_change>;
    }

    template <class... Ts>
    struct changes_checked
    {
        static_assert((is_change<Ts>() && ...),
                      "consume_t 只允许 input_frame 的 change 类型");
        using type = std::tuple<Ts...>;
    };

    template <class... Ts>
    using changes = typename changes_checked<Ts...>::type;

    template <class PlatformButton>
    static constexpr MouseButton map_button(PlatformButton b) noexcept
    {
        using P = PlatformButton;
        switch (b)
        {
        case P::eMOUSE_BUTTON_LEFT:
            return MouseButton::eMOUSE_BUTTON_LEFT;
        case P::eMOUSE_BUTTON_RIGHT:
            return MouseButton::eMOUSE_BUTTON_RIGHT;
        case P::eMOUSE_BUTTON_MIDDLE:
            return MouseButton::eMOUSE_BUTTON_MIDDLE;
        case P::eMOUSE_BUTTON_4:
            return MouseButton::eMOUSE_BUTTON_4;
        case P::eMOUSE_BUTTON_5:
            return MouseButton::eMOUSE_BUTTON_5;
        case P::eMOUSE_BUTTON_6:
            return MouseButton::eMOUSE_BUTTON_6;
        case P::eMOUSE_BUTTON_7:
            return MouseButton::eMOUSE_BUTTON_7;
        case P::eMOUSE_BUTTON_8:
            return MouseButton::eMOUSE_BUTTON_8;
        case P::eMOUSE_BUTTON_LAST:
            return MouseButton::eMOUSE_BUTTON_LAST;
        default:
            return MouseButton::eUNDEFINED;
        }
    }

    template <class Event>
    static constexpr auto build_change(const event_context &ctx, const Event &e) noexcept
    {
        using T = std::remove_cvref_t<Event>;

        if constexpr (std::same_as<T, mcs::vulkan::event::keyboard_event>)
        {
            return frame<key_change>{ctx, key_change{e.key,
                                                     (e.press() || e.repeat())
                                                         ? Transition::Press
                                                         : Transition::Release,
                                                     e.repeat(), e.modifier_key}};
        }
        else if constexpr (std::same_as<T, mcs::vulkan::event::mousebutton_event>)
        {
            return frame<button_change>{ctx, button_change{map_button(e.button),
                                                           (e.press() || e.repeat())
                                                               ? Transition::Press
                                                               : Transition::Release,
                                                           e.repeat(), e.modifier_key}};
        }
        else if constexpr (std::same_as<T, mcs::vulkan::event::position2d_event>)
        {
            const double dx = ctx.pointer_position_valid ? e.xpos - ctx.pointer_x : 0.0;
            const double dy = ctx.pointer_position_valid ? e.ypos - ctx.pointer_y : 0.0;
            return frame<move_change>{ctx, move_change{e.xpos, e.ypos, dx, dy}};
        }
        else if constexpr (std::same_as<T, mcs::vulkan::event::scroll_event>)
        {
            return frame<mcs::vulkan::event::scroll_event>{ctx, e};
        }
        else if constexpr (std::same_as<T, mcs::vulkan::event::cursor_enter_event>)
        {
            return frame<mcs::vulkan::event::cursor_enter_event>{ctx, e};
        }
        else if constexpr (std::same_as<T, mcs::vulkan::event::char_event>)
        {
            return frame<char_change>{ctx, char_change{e.codepoint}};
        }
        // 其余事件没有 return：auto 推导为 void，调用方不转发。
    }

    template <class T>
    static constexpr void apply_change(event_context &ctx, const frame<T> &f) noexcept
    {
        using U = std::remove_cvref_t<T>;

        if constexpr (std::same_as<U, key_change>)
        {
            ctx.keys[std::to_underlying(f.change.key)] =
                f.change.transition == Transition::Press ? HeldState::Down
                                                         : HeldState::Up;
            ctx.modifier = f.change.modifier;
        }
        else if constexpr (std::same_as<U, button_change>)
        {
            ctx.buttons[std::to_underlying(f.change.button)] =
                f.change.transition == Transition::Press ? HeldState::Down
                                                         : HeldState::Up;
            ctx.modifier = f.change.modifier;

            if (f.change.transition == Transition::Press)
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
        }
        else if constexpr (std::same_as<U, move_change>)
        {
            ctx.pointer_x = f.change.x;
            ctx.pointer_y = f.change.y;
            ctx.pointer_position_valid = true;
            ctx.pointer_state =
                ctx.any_button_down() ? PointerState::DownMove : PointerState::Hover;
            if (ctx.pointer_device == PointerDevice::Unknown)
                ctx.pointer_device = PointerDevice::Mouse;
        }
        else if constexpr (std::same_as<U, mcs::vulkan::event::cursor_enter_event>)
        {
            if (f.change.value)
            {
                ctx.pointer_device = PointerDevice::Mouse;
                ctx.pointer_state =
                    ctx.any_button_down() ? PointerState::Down : PointerState::Hover;
            }
            else
            {
                ctx.pointer_device = PointerDevice::Unknown;
                ctx.pointer_state = PointerState::Outside;
                ctx.pointer_position_valid = false;
            }
        }
        // scroll / char：不改变 ctx
    }
};

// ═══════════════════════════════════════════════════════════════════════════

#include <cmath>
#include <format>

// ═══════════════════════════════════════════════════════════════════════════
//  本案例：glfw 事件 → 一帧 → 矩阵 → 回调
//
//    glfw 回调
//      └─ glfw_input_translation            （不动）
//           └─ arena.input_event(e)         ← 平台事件（本案例只写这层翻译）
//                └─ frame<T> { context, change }
//                     └─ ui.feed(frame<T>, now)
//                          └─ 每个识别器 feed/tick → idle/hold/ready
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
    // ── 参数：一个地方调 ────────────────────────────────────────────
    inline constexpr std::int64_t click_max_ms = 250;  // 按下到松开，超时不算单击
    inline constexpr std::int64_t dbl_gap_ms = 300;    // 双击：两击之间的空档
    inline constexpr std::int64_t triple_gap_ms = 250; // 三击：每档更严一点
    inline constexpr double drag_slop_px = 4.0;        // 越过它就算拖拽

    namespace detail
    {
        // ── 识别器的全部状态：一条 claim 的三态 ──────────────────────────
        //    cur：这一拍我对裁决器说的话（idle / hold / ready）。
        //    win 只发射当前事件；claim 是否结束由识别器自己决定。
        struct fsm_state
        {
            intent::claim_state cur{intent::claim_state::idle};

            intent::claim_state claim(intent::claim_state s) noexcept
            {
                // 已经 ready、还没裁决的 claim，新帧不能把它降回 hold。
                if (cur == intent::claim_state::ready && s != intent::claim_state::idle)
                    s = intent::claim_state::ready;
                cur = s;
                return s;
            }

            // 默认：时间不改变状态（窗口需要过期的识别器自己覆盖 tick）
            intent::claim_state tick(intent::time_point) noexcept
            {
                return claim(cur);
            }

            // 长 claim 发射完一个事件：claim 继续活着，等下一个事件
            void consume_event() noexcept
            {
                if (cur == intent::claim_state::ready)
                    cur = intent::claim_state::hold;
            }

            // 一次性手势/让位：claim 结束
            void settle_claim() noexcept
            {
                cur = intent::claim_state::idle;
            }
        };

        // ── 帧就是 input_frame::frame<T>：完整快照 + 本帧变化。
        //    过滤（左键/按键 A）和事件分派都在 FSM 内部。

        // ── 复用的"一次按下→松开算不算一次有效单击"：限时 + 限位 ────────
        struct press_watch
        {
            intent::time_point down_at{};
            double down_x = 0, down_y = 0;
            bool down = false;

            //  0 = 无关帧   1 = 按下   2 = 有效松开   -1 = 无效松开
            //  过滤（只认左键）在 FSM 内部，不在 input_event 里。
            int operator()(const input_frame::frame<input_frame::button_change> &f,
                           intent::time_point now) noexcept
            {
                const auto &b = f.change;
                if (b.button != input_frame::MouseButton::eMOUSE_BUTTON_LEFT)
                    return 0;
                if (b.transition == input_frame::Transition::Press)
                {
                    if (b.repeat)
                        return 0;
                    down = true;
                    down_at = now;
                    down_x = f.context.pointer_x;
                    down_y = f.context.pointer_y;
                    return 1;
                }
                if (b.transition == input_frame::Transition::Release)
                {
                    if (!down)
                        return 0;
                    down = false;
                    const bool quick = now - down_at <= intent::time_point{click_max_ms};
                    const bool still =
                        std::hypot(f.context.pointer_x - down_x,
                                   f.context.pointer_y - down_y) <= drag_slop_px;
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
            using consume_t = input_frame::changes<input_frame::button_change>;

            struct payload_t
            {
                double x = 0, y = 0;
            } payload{};

            press_watch watch{};
            intent::claim_state feed(
                const input_frame::frame<input_frame::button_change> &f,
                intent::time_point now) noexcept
            {
                switch (watch(f, now))
                {
                case 1:
                    return claim(intent::claim_state::hold);
                case 2:
                    if (cur != intent::claim_state::ready) // 挂起的那一条不拿新坐标覆盖
                    {
                        payload.x = f.context.pointer_x;
                        payload.y = f.context.pointer_y;
                    }
                    return claim(intent::claim_state::ready);
                case -1: // 不是单击：这次交互作废
                    return claim(intent::claim_state::idle);
                default:
                    // 挂起中（ready）就继续申辩；否则按下算 hold，其余算 idle。
                    return claim(watch.down ? intent::claim_state::hold : cur);
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
            constexpr intent::claim_state win() noexcept
            {
                assert(invoke_ != nullptr);
                invoke_(this);
                settle_claim(); // 投递完，这条申报结束
                return cur;
            }
            intent::claim_state lose() noexcept
            {
                watch.reset(); // 被更具体的赢了：这次交互丢掉
                settle_claim();
                return cur;
            }
        };

        // ── N 连击：连续 N 次有效单击，每两击之间独立计时 ────────────────
        //    所以间隔不必相同：只要每一档都 ≤ GapMs 就算。
        //    窗口一过就回 idle —— 把机会让给更简单的解释（单击）。
        template <int N, std::int64_t GapMs, static_string Name,
                  static_string FnName = Name>
        struct click_chain : fsm_state
        {
            static constexpr auto name = Name;
            static constexpr auto fn_name = FnName;
            using consume_t = input_frame::changes<input_frame::button_change>;

            struct payload_t
            {
                double x = 0, y = 0;
                int count = 0;
            } payload{};

            press_watch watch{};
            int clicks = 0;            // 已经完成的单击数
            intent::time_point last{}; // 上一次松开的时刻
            bool armed = false;        // 正在窗口里等下一击

            intent::claim_state feed(
                const input_frame::frame<input_frame::button_change> &f,
                intent::time_point now) noexcept
            {
                if (armed && now - last > intent::time_point{GapMs})
                {
                    // 窗口过了：这一次放弃，但**这一帧照常处理** ——
                    // 它可能正是新交互的按下。先显式清掉旧申报。
                    clicks = 0;
                    armed = false;
                    watch.reset();
                    claim(intent::claim_state::idle);
                }

                switch (watch(f, now))
                {
                case 1:
                    return claim(intent::claim_state::hold);
                case 2:
                    ++clicks;
                    armed = true;
                    last = now;
                    if (cur != intent::claim_state::ready) // 挂起的那一条保留原载荷/跨度
                    {
                        payload.x = f.context.pointer_x;
                        payload.y = f.context.pointer_y;
                        payload.count = clicks;
                    }
                    if (clicks >= N)
                    {
                        clicks = 0;
                        armed = false;
                        return claim(intent::claim_state::ready);
                    }
                    return claim(intent::claim_state::hold);
                case -1: // 这一次不是有效单击
                    clicks = 0;
                    armed = false;
                    return claim(intent::claim_state::idle);
                default:
                    // 按下、等着、挂起，都算在场；纯路过才是 idle。
                    return claim((armed || clicks > 0 || watch.down)
                                     ? intent::claim_state::hold
                                     : cur);
                }
            }
            // 时间窗只靠"喂帧"是过不去的：必须由 tick 推动。
            // 但 tick 不再从旁边改 cur —— 它回同一句话，引擎照常裁决。
            intent::claim_state tick(intent::time_point now) noexcept
            {
                if (armed && now - last > intent::time_point{GapMs})
                {
                    clicks = 0;
                    armed = false;
                    watch.reset();
                    return claim(intent::claim_state::idle);
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
            constexpr intent::claim_state win() noexcept
            {
                assert(invoke_ != nullptr);
                invoke_(this);
                clicks = 0;
                armed = false;
                watch.reset();
                settle_claim();
                return cur;
            }
            intent::claim_state lose() noexcept
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

    } // namespace detail

    // 对外的 FSM 就这四个名字；模板实现放在 detail。
    using click = detail::single_click<"on_click">;
    using double_click = detail::click_chain<2, dbl_gap_ms, "on_dbl">;
    using triple_click = detail::click_chain<3, triple_gap_ms, "on_triple">;

    // 一个长 claim：begin/move/end 都是同一个 claim 的 ready。
    // 按钮事件给 press/release，position2d_event 给 move —— 两个 feed
    // 重载，不再有 frame_type 的单类型限制。
    struct drag : detail::fsm_state
    {
        static constexpr auto name = static_string{"on_drag"};
        static constexpr auto fn_name = name;
        using consume_t =
            input_frame::changes<input_frame::button_change, input_frame::move_change>;
        using payload_t = detail::drag_payload;
        using track_t = detail::drag_track;
        using phase_t = payload_t::phase_t;

        payload_t payload{};
        track_t t{};
        bool dragging = false;
        bool ending = false;

        intent::claim_state feed(const input_frame::frame<input_frame::button_change> &f,
                                 intent::time_point now) noexcept
        {
            if (f.change.button != input_frame::MouseButton::eMOUSE_BUTTON_LEFT)
                return claim(cur);
            if (f.change.transition == input_frame::Transition::Press)
            {
                if (f.change.repeat)
                    return claim(cur);
                return drag_start(f, now);
            }
            return drag_end(f, now);
        }

        intent::claim_state feed(const input_frame::frame<input_frame::move_change> &f,
                                 intent::time_point now) noexcept
        {
            return drag_move(f, now);
        }

        intent::claim_state drag_start(
            const input_frame::frame<input_frame::button_change> &f,
            intent::time_point now) noexcept
        {
            if (cur != intent::claim_state::idle) // 上一条 claim 还没结束：先收掉
                settle_claim();
            t.press(f.context.pointer_x, f.context.pointer_y);
            dragging = false;
            ending = false;
            return claim(intent::claim_state::hold);
        }

        intent::claim_state drag_move(
            const input_frame::frame<input_frame::move_change> &f,
            intent::time_point now) noexcept
        {
            const auto &m = f.change;
            if (!t.down)
                return claim(intent::claim_state::idle);
            if (!dragging)
            {
                if (!t.past_slop(m.x, m.y))
                {
                    t.last(m.x, m.y);
                    return claim(intent::claim_state::hold);
                }
                dragging = true;
                // 这一帧的坐标就是这一帧的事件。
                payload = t.make(phase_t::begin, m.x, m.y, 0, 0);
                t.last(m.x, m.y);
                return claim(intent::claim_state::ready);
            }
            payload = t.make(phase_t::move, m.x, m.y, m.dx, m.dy);
            t.last(m.x, m.y);
            return claim(intent::claim_state::ready);
        }

        intent::claim_state drag_end(
            const input_frame::frame<input_frame::button_change> &f,
            intent::time_point now) noexcept
        {
            t.release();
            if (!dragging)
                return claim(intent::claim_state::idle); // 没拖过：claim 结束
            dragging = false;
            ending = true;
            payload =
                t.make(phase_t::end, f.context.pointer_x, f.context.pointer_y, 0, 0);
            return claim(intent::claim_state::ready);
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
        constexpr intent::claim_state win() noexcept
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
        intent::claim_state lose() noexcept
        {
            t = {};
            dragging = false;
            ending = false;
            settle_claim();
            return cur;
        }
    };

    // ── 悬停：指针进入窗口时报一次（离开不算事件）──────────────────
    struct hover : detail::fsm_state
    {
        static constexpr auto name = static_string{"on_hover"};
        static constexpr auto fn_name = name;
        using consume_t = input_frame::changes<input_frame::cursor_enter_event>;

        intent::claim_state feed(
            const input_frame::frame<input_frame::cursor_enter_event> &f,
            intent::time_point now) noexcept
        {
            if (f.change.value)
                return claim(intent::claim_state::ready);
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
        constexpr intent::claim_state win() noexcept
        {
            assert(invoke_ != nullptr);
            invoke_(this);
            settle_claim();
            return cur;
        }
        intent::claim_state lose() noexcept
        {
            settle_claim();
            return cur;
        }
    };

    // ── 滚轮：一格一次（delta 交给载荷）────────────────────────────
    struct wheel : detail::fsm_state
    {
        static constexpr auto name = static_string{"on_wheel"};
        static constexpr auto fn_name = name;
        using consume_t = input_frame::changes<input_frame::scroll_event>;

        struct payload_t
        {
            int delta = 0;
        } payload{};

        intent::claim_state feed(const input_frame::frame<input_frame::scroll_event> &f,
                                 intent::time_point now) noexcept
        {
            payload.delta = static_cast<int>(f.change.yoffset);
            return claim(intent::claim_state::ready);
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
        constexpr intent::claim_state win() noexcept
        {
            assert(invoke_ != nullptr);
            invoke_(this);
            settle_claim();
            return cur;
        }
        intent::claim_state lose() noexcept
        {
            settle_claim();
            return cur;
        }
    };

    // ── 键盘 A ─────────────────────────────────────────────────────
    struct key_a : detail::fsm_state
    {
        static constexpr auto name = static_string{"on_key_A"};
        static constexpr auto fn_name = name;
        using consume_t = input_frame::changes<input_frame::key_change>;

        intent::claim_state feed(const input_frame::frame<input_frame::key_change> &f,
                                 intent::time_point) noexcept
        {
            if (f.change.transition == input_frame::Transition::Press &&
                !f.change.repeat && f.change.key == Key::eA)
                return claim(intent::claim_state::ready);
            return claim(cur);
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
        constexpr intent::claim_state win() noexcept
        {
            assert(invoke_ != nullptr);
            invoke_(this);
            settle_claim();
            return cur;
        }
        intent::claim_state lose() noexcept
        {
            settle_claim();
            return cur;
        }
    };

    // ── 键盘 C：普通输入，和快捷键竞争 ───────────────────────────
    struct key_c : detail::fsm_state
    {
        static constexpr auto name = static_string{"on_key_C"};
        static constexpr auto fn_name = name;
        using consume_t = input_frame::changes<input_frame::key_change>;

        intent::claim_state feed(const input_frame::frame<input_frame::key_change> &f,
                                 intent::time_point) noexcept
        {
            if (f.change.transition == input_frame::Transition::Press &&
                !f.change.repeat && f.change.key == Key::eC)
                return claim(intent::claim_state::ready);
            return claim(cur);
        }

        using invoke_type = void (*)(void *self) noexcept;
        void *receiver_{};
        invoke_type invoke_{};
        template <class Receiver>
        constexpr void bind(Receiver &ptr) noexcept
        {
            receiver_ = &ptr;
            invoke_ = invoke_type{[](void *self) noexcept {
                auto *m = static_cast<key_c *>(self);
                static_cast<Receiver *>(m->receiver_)->template invoke<fn_name>();
            }};
        }
        constexpr intent::claim_state win() noexcept
        {
            assert(invoke_ != nullptr);
            invoke_(this);
            settle_claim();
            return cur;
        }
        intent::claim_state lose() noexcept
        {
            settle_claim();
            return cur;
        }
    };

    // ── 快捷键：一个识别器 + 一张表；一次匹配可带多个 command ─────
    enum class command : std::uint8_t
    {
        copy,
        copy_plain,
        paste,
        save,
        notify
    };

    constexpr std::string_view command_name(command c) noexcept
    {
        switch (c)
        {
        case command::copy:
            return "copy";
        case command::copy_plain:
            return "copy_plain";
        case command::paste:
            return "paste";
        case command::save:
            return "save";
        case command::notify:
            return "notify";
        }
        return "?";
    }

    struct shortcut : detail::fsm_state
    {
        static constexpr auto name = static_string{"on_shortcut"};
        static constexpr auto fn_name = name;
        using consume_t = input_frame::changes<input_frame::key_change>;

        struct payload_t
        {
            std::array<command, 2> items{};
            int n = 0;
        } payload{};

        intent::claim_state feed(const input_frame::frame<input_frame::key_change> &f,
                                 intent::time_point) noexcept
        {
            const auto &c = f.change;
            if (c.transition != input_frame::Transition::Press || c.repeat)
                return claim(cur);

            const bool ctrl = c.modifier.hasAll(ModifierKey::Control());
            const bool shift = c.modifier.hasAll(ModifierKey::Shift());
            const bool alt = c.modifier.hasAll(ModifierKey::Alt());
            if (!ctrl || alt)
                return claim(cur);

            if (c.key == Key::eC)
            {
                payload.items[0] = shift ? command::copy_plain : command::copy;
                payload.n = 1;
                return claim(intent::claim_state::ready);
            }
            if (c.key == Key::eS)
            {
                payload.items[0] = command::save;
                payload.items[1] = command::notify;
                payload.n = 2;
                return claim(intent::claim_state::ready);
            }
            if (c.key == Key::eV)
            {
                payload.items[0] = command::paste;
                payload.n = 1;
                return claim(intent::claim_state::ready);
            }
            return claim(cur);
        }

        using invoke_type = void (*)(void *self) noexcept;
        void *receiver_{};
        invoke_type invoke_{};
        template <class Receiver>
        constexpr void bind(Receiver &ptr) noexcept
        {
            receiver_ = &ptr;
            invoke_ = invoke_type{[](void *self) noexcept {
                auto *m = static_cast<shortcut *>(self);
                static_cast<Receiver *>(m->receiver_)
                    ->template invoke<fn_name>(m->payload);
            }};
        }
        constexpr intent::claim_state win() noexcept
        {
            assert(invoke_ != nullptr);
            invoke_(this);
            settle_claim();
            return cur;
        }
        intent::claim_state lose() noexcept
        {
            settle_claim();
            return cur;
        }
    };

    // ── 键盘 HUD：独立观察者，和动作/快捷键同帧并行 ──────────────
    struct key_hud : detail::fsm_state
    {
        static constexpr auto name = static_string{"on_key_HUD"};
        static constexpr auto fn_name = name;
        using consume_t = input_frame::changes<input_frame::key_change>;

        struct payload_t
        {
            Key key{};
            input_frame::Transition transition{};
        } payload{};

        intent::claim_state feed(const input_frame::frame<input_frame::key_change> &f,
                                 intent::time_point) noexcept
        {
            if (f.change.repeat)
                return claim(cur);
            payload = {f.change.key, f.change.transition};
            return claim(intent::claim_state::ready);
        }

        using invoke_type = void (*)(void *self) noexcept;
        void *receiver_{};
        invoke_type invoke_{};
        template <class Receiver>
        constexpr void bind(Receiver &ptr) noexcept
        {
            receiver_ = &ptr;
            invoke_ = invoke_type{[](void *self) noexcept {
                auto *m = static_cast<key_hud *>(self);
                static_cast<Receiver *>(m->receiver_)
                    ->template invoke<fn_name>(m->payload);
            }};
        }
        constexpr intent::claim_state win() noexcept
        {
            assert(invoke_ != nullptr);
            invoke_(this);
            settle_claim();
            return cur;
        }
        intent::claim_state lose() noexcept
        {
            settle_claim();
            return cur;
        }
    };

    // ── 叶子：name 是 kind 的身份（进日志）；最后一个参数是回调名 ───
    using l_click = intent::leaf<click>;
    using l_dbl = intent::leaf<double_click>;
    using l_triple = intent::leaf<triple_click>;
    using l_drag = intent::leaf<drag>;
    using l_hover = intent::leaf<hover>;
    using l_wheel = intent::leaf<wheel>;
    using l_key = intent::leaf<key_a>;
    using l_key_c = intent::leaf<key_c>;
    using l_shortcut = intent::leaf<shortcut>;
    using l_hud = intent::leaf<key_hud>;

    // ── 树：只声明"谁比谁具体"，不写任何顺序 ───────────────────────
    //    refines<A, B> 读作"A 等 B" ⇒ **B 更具体，B 赢**（第二个参数才是细的）。
    //      拖拽 > 单击、双击 > 单击、三击 > 双击 > 单击
    //    拖拽和双击/三击之间没有边 —— 免得"刚点完一下"的挂起双击把拖拽挡住。
    //
    //    快捷键 > 普通 C：Ctrl+C 时 shortcut 赢，key_c 让位。
    //    HUD 和它们都没有边：一个 C 事件可以同时产生 C/快捷键 + HUD 两个 emit。
    using Tree = intent::all_of<
        intent::refines<l_click,
                        intent::all_of<l_drag, intent::refines<l_dbl, l_triple>>>, //
        l_hover,                                                                   //
        l_wheel,                                                                   //
        intent::refines<l_key_c, l_shortcut>,                                      //
        l_key,                                                                     //
        l_hud>;

    // Frame 的裁剪由 FSM 的 feed(frame<T>) 重载完成；
    // ui 不需要 FrameOf，也不需要全局 frame_types。
    using ui_type = intent::debug_arbiter<Tree>;

    // 回调打到哪儿：自检要靠它验证 sink 那一路确实通了
    namespace detail
    {
        struct tally
        {
            int click{}, dbl{}, triple{}, hover{}, wheel{}, key_a{}, key_c{};
            int shortcut{}, hud{}, commands{};
            int drag_begin{}, drag_move{}, drag_end{};
            double x{}, y{}, dx{}, dy{}, total_dx{}, total_dy{};
            double move_x{}, move_y{}, move_dx{}, move_dy{};
            int delta{};

            friend constexpr bool operator==(const tally &, const tally &) = default;
        };
    } // namespace detail

    // 事件时刻：正常情况取墙上时钟；自检时用 arena.now 给的逻辑时间
    inline intent::time_point now_ms() noexcept
    {
        static const auto t0 = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<intent::time_point>(
            std::chrono::steady_clock::now() - t0);
    }
    template <class Self>
    inline intent::time_point now_of(Self &self) noexcept
    {
        return self.fake_clock ? self.now : now_ms();
    }

    // 判决的两种结果，打印用（和 event 做邻居）
    constexpr std::string_view to_string(intent::verdict v) noexcept
    {
        return v == intent::verdict::accepted ? "acc" : "sup";
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
                         to_string(e.v));
        }
    }

    // ── 唯一的 arena 构造：窗口模式和自检模式共用同一份 ────────────
    inline auto make_arena()
    {
        return make_aggregate<"arena">(
            field<"ui">(ui_type{}),          //
            field<"tally">(detail::tally{}), // 回调写这里，自检读这里
            field<"event_context">(input_frame::event_context{}), // 变化前的快照
            field<"now">(intent::time_point{}),                   // 逻辑时间
            field<"fake_clock">(false),                           // 自检：时间由测试喂
            field<"keep_events">(false), // 自检：tick 后不清空播报
            field<"frames">(0u),         //

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
                using pt = detail::drag_payload::phase_t;
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
            method<"on_key_C">([](auto &self) noexcept {
                ++self.tally.key_c;
                std::println("  >>> 键盘 C");
            }),
            method<"on_shortcut">([](auto &self, auto &&p) noexcept {
                ++self.tally.shortcut;
                self.tally.commands += p.n;
                std::print("  >>> 快捷键");
                for (int i = 0; i < p.n; ++i)
                    std::print(" {}", command_name(p.items[i]));
                std::println("");
            }),
            method<"on_key_HUD">([](auto &self, auto &&p) noexcept {
                ++self.tally.hud;
                std::println("  >>> HUD key={} {}", static_cast<int>(p.key),
                             p.transition == input_frame::Transition::Press ? "down"
                                                                            : "up");
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

            // 平台事件 → input_frame::frame<T>：唯一的适配层。
            // 过滤（左键/按键 A）和事件分派都在 FSM 内部。
            method<"input_event">([]<class Self, class E>(Self &self, E e) noexcept {
                const auto now = now_of(self);
                self.now = now;

                using frame_t =
                    decltype(input_frame::build_change(self.event_context, e));
                if constexpr (!std::is_void_v<frame_t>)
                {
                    auto f = input_frame::build_change(self.event_context, e);
                    self.ui.feed(f, now);
                    input_frame::apply_change(self.event_context, f);
                }
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
        arena.invoke<"bind">();

        int fails = 0;
        auto check = [&](std::string_view what, bool ok) {
            std::println("  {} {}", ok ? "✓" : "✗", what);
            if (!ok)
                ++fails;
        };
        auto key = [&](Key k, Action a, ModifierKey mods, std::int64_t ms) {
            arena.now = intent::time_point{ms};
            arena.invoke<"input_event">(keyboard_event{k, a, mods});
        };
        auto count = [&](std::string_view name, std::int64_t at) {
            int n = 0;
            for (const auto &e : arena.ui.events())
                if (e.name == name && e.v == intent::verdict::accepted &&
                    e.at.count() == at)
                    ++n;
            return n;
        };

        // 1) 普通 C：key_c + HUD 并行，两个 emit
        key(Key::eC, Action::ePRESS, ModifierKey::None(), 0);
        check("plain C -> on_key_C", arena.tally.key_c == 1);
        check("plain C -> HUD 同时 emit",
              count("on_key_C", 0) == 1 && count("on_key_HUD", 0) == 1);

        // 2) Ctrl+C：shortcut > key_c；HUD 仍然并行
        key(Key::eLEFT_CONTROL, Action::ePRESS, ModifierKey::Control(), 10);
        key(Key::eC, Action::ePRESS, ModifierKey::Control(), 20);
        check("Ctrl+C -> shortcut", arena.tally.shortcut == 1);
        check("Ctrl+C -> key_c 被压掉", count("on_key_C", 20) == 0);
        check("Ctrl+C -> shortcut + HUD 两个 emit",
              count("on_shortcut", 20) == 1 && count("on_key_HUD", 20) == 1);
        check("Ctrl+C 只带一个 command", arena.tally.commands == 1);
        key(Key::eC, Action::eRELEASE, ModifierKey::Control(), 30);
        key(Key::eLEFT_CONTROL, Action::eRELEASE, ModifierKey::None(), 40);

        // 3) Ctrl+Shift+C：更具体的快捷键
        key(Key::eLEFT_CONTROL, Action::ePRESS, ModifierKey::Control(), 45);
        key(Key::eLEFT_SHIFT, Action::ePRESS, ModifierKey::CtrlShift(), 46);
        key(Key::eC, Action::ePRESS, ModifierKey::CtrlShift(), 50);
        check("Ctrl+Shift+C -> copy_plain",
              count("on_shortcut", 50) == 1 && arena.tally.shortcut == 2);
        check("Ctrl+Shift+C -> key_c 被压掉", count("on_key_C", 50) == 0);
        key(Key::eC, Action::eRELEASE, ModifierKey::CtrlShift(), 55);
        key(Key::eLEFT_SHIFT, Action::eRELEASE, ModifierKey::Control(), 56);
        key(Key::eLEFT_CONTROL, Action::eRELEASE, ModifierKey::None(), 57);

        // 4) Ctrl+S：一次快捷键带两个 command（save + notify）
        key(Key::eLEFT_CONTROL, Action::ePRESS, ModifierKey::Control(), 60);
        key(Key::eS, Action::ePRESS, ModifierKey::Control(), 70);
        check("Ctrl+S -> 一个 shortcut，两个 command",
              count("on_shortcut", 70) == 1 && arena.tally.commands == 4);
        key(Key::eS, Action::eRELEASE, ModifierKey::Control(), 75);
        key(Key::eLEFT_CONTROL, Action::eRELEASE, ModifierKey::None(), 76);

        // 5) 普通 S：没有 key_s，也没有 shortcut；只有 HUD
        const int before = arena.tally.shortcut;
        key(Key::eS, Action::ePRESS, ModifierKey::None(), 90);
        check("plain S -> 没有快捷键，只有 HUD",
              arena.tally.shortcut == before && count("on_shortcut", 90) == 0);

        std::println("自检：{} 条失败", fails);
        return fails == 0 ? 0 : 1;
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

    std::println("就绪：单击 / 双击 / 三击 / 拖拽 / 滚轮 / A / C / Ctrl+C / Ctrl+Shift+C "
                 "/ Ctrl+S / HUD；关窗口退出。");
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
