#include <array>
#include <bit>
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
struct type_erased_invoker
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

    type_erased_invoker() = default;
    constexpr explicit type_erased_invoker(invoke_type invoke) noexcept : invoke_{invoke}
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
    static constexpr void dispatch_invoke(Obj *ptr, std::tuple<Ts...> *, const void *type,
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
        return type_erased_invoker{invoke_type{
            [](void *ptr, const void *type, void *data, Extra... args) noexcept {
                auto *obj = static_cast<object_type *>(ptr);
                dispatch_invoke<callable_name, object_type>(
                    obj, static_cast<data_tuple *>(nullptr), type, data, args...);
            }}};
    }

  private:
    invoke_type invoke_{};
};

template <class Handler>
class multicast_sink
{
  public:
    using index_t = std::uint32_t;
    static constexpr index_t npos = ~index_t{0};

    struct handle
    {
        index_t slot = npos;
        std::uint64_t gen = 0;
        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return slot != npos;
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return valid();
        }
    };

    class scoped_binding
    {
        multicast_sink *owner_ = nullptr;
        handle h_{};

      public:
        scoped_binding() noexcept = default;

        scoped_binding(multicast_sink &o, handle hh) noexcept : owner_(&o), h_(hh) {}

        constexpr scoped_binding(scoped_binding &&other) noexcept
            : owner_(std::exchange(other.owner_, nullptr)),
              h_(std::exchange(other.h_, {}))
        {
        }
        constexpr scoped_binding &operator=(scoped_binding &&other) noexcept
        {
            if (this != &other)
            {
                release();
                owner_ = std::exchange(other.owner_, nullptr);
                h_ = std::exchange(other.h_, {});
            }
            return *this;
        }
        scoped_binding(const scoped_binding &) = delete;
        scoped_binding &operator=(const scoped_binding &) = delete;

        constexpr ~scoped_binding() noexcept
        {
            release();
        }

        constexpr void release() noexcept
        {
            if (owner_ && h_.valid())
                owner_->unbind(h_);
            owner_ = nullptr;
            h_ = {};
        }
        constexpr void activate() noexcept
        {
            if (owner_ && h_.valid())
                owner_->activate(h_);
        }
        constexpr void deactivate() noexcept
        {
            if (owner_ && h_.valid())
                owner_->deactivate(h_);
        }
        constexpr bool valid() const noexcept
        {
            return owner_ && h_.valid();
        }
        constexpr handle get_handle() const noexcept
        {
            return h_;
        }
    };

    constexpr multicast_sink() = default;
    multicast_sink(const multicast_sink &) = delete;
    multicast_sink &operator=(const multicast_sink &) = delete;

    // 可能抛 bad_alloc（slots_ realloc）
    constexpr handle bind(void *obj, Handler handler)
    {
        index_t idx;
        if (distributing_ || free_head_ == npos)
        {
            idx = static_cast<index_t>(slots_.size());
            slot s;
            s.obj = obj;
            s.handler = handler;
            s.gen = 1;
            s.occupied = true;
            s.active = true;
            slots_.push_back(s);
        }
        else
        {
            idx = free_head_;
            auto &s = slots_[idx];
            free_head_ = s.free_next;
            s.free_next = npos;
            s.obj = obj;
            s.handler = handler;
            s.gen += 1;
            s.occupied = true;
            s.active = true;
        }
        slots_[idx].active_pos = static_cast<index_t>(active_list_.size());
        active_list_.push_back(idx);
        return {idx, slots_[idx].gen};
    }

    // 分发外：swap-remove active_list_ + 进 free_head_。分发中：只标 occupied。
    constexpr void unbind(handle h) noexcept
    {
        if (h.slot >= slots_.size())
            return;
        auto &s = slots_[h.slot];
        if (!s.occupied || s.gen != h.gen)
            return;
        s.occupied = false;
        s.obj = nullptr;
        s.handler = {};
        s.free_next = free_head_;
        free_head_ = h.slot;
        if (!distributing_)
            swap_remove_active_(h.slot);
        else
            dirty_ = true;
    }

    // ★ attach 只做 sink 层封装：obj + handler，无类型擦除
    scoped_binding attach(void *obj, Handler handler)
    {
        return scoped_binding{*this, bind(obj, std::move(handler))};
    }
    void attach(scoped_binding &out, void *obj, Handler handler)
    {
        out = attach(obj, std::move(handler));
    }

    constexpr void activate(handle h) noexcept
    {
        if (h.slot >= slots_.size())
            return;
        auto &s = slots_[h.slot];
        if (!s.occupied || s.gen != h.gen)
            return;
        if (s.active)
            return;
        s.active = true;
        if (s.active_pos == npos)
        {
            s.active_pos = static_cast<index_t>(active_list_.size());
            active_list_.push_back(h.slot);
        }
    }

    constexpr void deactivate(handle h) noexcept
    {
        if (h.slot >= slots_.size())
            return;
        auto &s = slots_[h.slot];
        if (!s.occupied || s.gen != h.gen)
            return;
        if (!s.active)
            return;
        s.active = false;
        if (!distributing_)
            swap_remove_active_(h.slot);
        else
            dirty_ = true;
    }

    template <class... Args>
    constexpr void dispatch(Args &&...args) noexcept
    {
        const bool outer = !distributing_;
        if (outer)
        {
            distributing_ = true;
            n_snapshot_ = static_cast<index_t>(active_list_.size());
        }
        for (index_t i = 0; i < n_snapshot_; ++i)
        {
            index_t idx = active_list_[i];
            auto &s = slots_[idx];
            if (!s.occupied || !s.active)
                continue;
            s.handler.invoke(s.obj, std::forward<Args>(args)...);
        }
        if (outer)
        {
            distributing_ = false;
            if (dirty_)
            {
                compact_active_();
                dirty_ = false;
            }
        }
    }

    [[nodiscard]] constexpr const Handler *handler_of(handle h) const noexcept
    {
        if (h.slot >= slots_.size())
            return nullptr;
        const auto &s = slots_[h.slot];
        if (!s.occupied || s.gen != h.gen)
            return nullptr;
        return &s.handler;
    }
    [[nodiscard]] constexpr void *context_of(handle h) const noexcept
    {
        if (h.slot >= slots_.size())
            return nullptr;
        const auto &s = slots_[h.slot];
        if (!s.occupied || s.gen != h.gen)
            return nullptr;
        return s.obj;
    }

    template <class... Args>
    constexpr void invoke(handle h, Args &&...args) const noexcept
    {
        if (h.slot >= slots_.size())
            return;
        const auto &s = slots_[h.slot];
        if (!s.occupied || s.gen != h.gen)
            return;
        s.handler.invoke(s.obj, std::forward<Args>(args)...);
    }

    [[nodiscard]] constexpr std::size_t slot_count() const noexcept
    {
        return slots_.size();
    }
    [[nodiscard]] constexpr std::size_t live_count() const noexcept
    {
        return active_list_.size();
    }
    [[nodiscard]] constexpr std::size_t active_list_size() const noexcept
    {
        return active_list_.size();
    }

    constexpr void reserve(std::size_t n)
    {
        slots_.reserve(n);
    }

  private:
    struct slot
    {
        void *obj = nullptr;
        Handler handler{};
        index_t free_next = npos;
        index_t active_pos = npos;
        std::uint64_t gen = 0;
        bool occupied = false;
        bool active = true;
    };

    std::vector<slot> slots_;
    index_t free_head_ = npos;
    std::vector<index_t> active_list_;
    index_t n_snapshot_ = 0;
    bool distributing_ = false;
    bool dirty_ = false;

    constexpr void swap_remove_active_(index_t idx) noexcept
    {
        auto &s = slots_[idx];
        index_t p = s.active_pos;
        if (p == npos)
            return;
        index_t last = static_cast<index_t>(active_list_.size() - 1);
        if (p != last)
        {
            index_t moved = active_list_[last];
            active_list_[p] = moved;
            slots_[moved].active_pos = p;
        }
        active_list_.pop_back();
        s.active_pos = npos;
    }

    constexpr void compact_active_() noexcept
    {
        index_t write = 0;
        for (index_t read = 0; read < active_list_.size(); ++read)
        {
            index_t idx = active_list_[read];
            auto &s = slots_[idx];
            if (s.occupied && s.active)
            {
                active_list_[write] = idx;
                s.active_pos = write;
                ++write;
            }
            else
            {
                s.active_pos = npos;
            }
        }
        active_list_.resize(write);
    }
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
    using proxy_class_type = type_erased_invoker<>;

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
    static constexpr void subscribe_all(void *ptr, std::tuple<Events...> *)
    {
        (event_dispatcher<Events>::instance().subscribe(ptr, &forwarding<Events>), ...);
    }
    template <typename... Events>
    static constexpr void unsubscribe_all(void *ptr, std::tuple<Events...> *) noexcept
    {
        static_assert((noexcept(event_dispatcher<Events>::instance().unsubscribe(
                           ptr, &forwarding<Events>)) &&
                       ...),
                      "~class() need noexcept is true");
        (event_dispatcher<Events>::instance().unsubscribe(ptr, &forwarding<Events>), ...);
    }
    constexpr explicit glfw_input_translation()
    {
        subscribe_all(this, static_cast<data_tuple *>(nullptr));
    }
    constexpr ~glfw_input_translation() noexcept
    {
        unsubscribe_all(this, static_cast<data_tuple *>(nullptr));
    }
    glfw_input_translation(const glfw_input_translation &) = delete;
    glfw_input_translation(glfw_input_translation &&) = delete;
    glfw_input_translation &operator=(const glfw_input_translation &) = delete;
    glfw_input_translation &operator=(glfw_input_translation &&) = delete;

    using multicast_t = multicast_sink<proxy_class_type>;
    using handle = multicast_t::handle;
    using scoped_binding = multicast_t::scoped_binding;

    template <typename event_type>
    constexpr static void forwarding(void *self, event_type e) noexcept
    {
        auto *impl = static_cast<glfw_input_translation *>(self);
        using mcs::vulkan::runtime_type_v;
        impl->sinks_.dispatch(runtime_type_v<event_type>, &e);
    }

    // ★ 类型擦除在这里发生：Obj → (void*, proxy_class_type)
    template <typename Obj>
        requires(requires() {
            proxy_class_type::build<"input_event", data_tuple, Obj>();
        })
    constexpr scoped_binding attach(Obj &obj)
    {
        return sinks_.attach(&obj,
                             proxy_class_type::build<"input_event", data_tuple, Obj>());
    }

    template <typename Obj>
        requires(requires() {
            proxy_class_type::build<"input_event", data_tuple, Obj>();
        })
    constexpr void attach(scoped_binding &out, Obj &obj)
    {
        out = attach(obj);
    }
    constexpr void reserve(std::size_t n)
    {
        sinks_.reserve(n);
    }

  private:
    multicast_t sinks_;
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
        template <class M, class Self, class Frame>
        concept can_feed = requires(M &m, Self &self, const Frame &f, time_point t) {
            { m.feed(self, f, t) } noexcept -> std::same_as<claim_state>;
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
    template <class Fsm>
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
        // 只遍历置位：O(popcount)，不是 O(S)
        template <class Fn>
        constexpr void for_each(Fn &&f) const noexcept
        {
            for (std::size_t k = 0; k < W; ++k)
            {
                auto x = w[k];
                while (x)
                {
                    const auto b = std::countr_zero(x);
                    const auto i = static_cast<index_t>(k * 64 + b);
                    if (i < static_cast<index_t>(S))
                        f(i);
                    x &= x - 1;
                }
            }
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
    template <class Tree, class Self, class Frame>
    consteval auto advance_mask() noexcept
    {
        using Shape = typename detail::shape_of<Tree>;
        using Recognizers = Shape::recognizers;
        constexpr std::size_t S = Shape::leaf_count;
        bset<S> m{};
        template for (constexpr auto I : std::ranges::views::indices(S))
        {
            if constexpr (recognizer::can_feed<std::tuple_element_t<I, Recognizers>, Self,
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
        // ① 一次扫，建 live / ready；这是局部量，不保存为成员
        bset<S> live{}, ready{};
        for (index_t i = 0; i < S; ++i)
        {
            if (report[i] != claim_state::idle)
                live.set(i);
            if (report[i] == claim_state::ready)
                ready.set(i);
        }
        if (ready.none())
            return {}; // 没 ready 就没 win，没 win 就没 lose

        // ② 只遍历 live 的置位
        bset<S> blocked{};
        live.for_each([&](index_t j) noexcept { blocked |= beats[j]; });

        // ③ ready 里的极大元
        const bset<S> win = ready & ~blocked;

        // ④ 只遍历 win 的置位
        bset<S> lose{};
        win.for_each([&](index_t i) noexcept { lose |= beats[i]; });
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
        std::array<claim_state, S> reports{}; // 最近一次上报；唯一状态源

        // 无观测版本
        template <class Self, class Frame>
        constexpr void feed(Self &self, const Frame &f, time_point now) noexcept
        {
            null_trace t{};
            feed<Self, Frame, null_trace>(self, f, now, t);
        }

        // 带 Trace 模板参数的版本：由 debug_arbiter 调用
        template <class Self, class Frame, class Trace>
        constexpr void feed(Self &self, const Frame &f, time_point now, Trace &t) noexcept
        {
            constexpr bset<S> advance = advance_mask<Tree, Self, Frame>();
            if constexpr (advance.none())
                return; // 没有列接受这个 Frame：不推进，也不再裁决

            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                if constexpr (advance.has(static_cast<index_t>(I)))
                    reports[I] = std::get<I>(recognizers).feed(self, f, now);
            }
            resolve(self, now, t);
        }

        // 无观测版本
        template <class Self>
        constexpr void tick(Self &self, time_point now) noexcept
        {
            null_trace t{};
            tick<Self, null_trace>(self, now, t);
        }

        // 带 Trace 模板参数的版本：由 debug_arbiter 调用
        template <class Self, class Trace>
        constexpr void tick(Self &self, time_point now, Trace &t) noexcept
        {
            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                reports[I] = std::get<I>(recognizers).tick(self, now);
            }
            resolve(self, now, t);
        }

      private:
        // report -> verdict -> 落地。先让位，再放行。
        template <class Self, class Trace>
        constexpr void resolve(Self &self, time_point now, Trace &t) noexcept
        {
            const auto d = arbitrate(reports, law::beats);

            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                if (d.lose.has(I))
                    deliver<I>(self, false, now, t);
            }
            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                if (d.win.has(I))
                    deliver<I>(self, true, now, t);
            }
        }

        // 唯一副作用点：win/lose 返回“裁决之后我是什么状态”，
        // 立刻写回 report；下一拍不会拿旧的 ready 再判一次。
        template <std::size_t Flat, class Self, class Trace>
        constexpr void deliver(Self &self, bool won, time_point at, Trace &t) noexcept
        {
            auto &m = std::get<Flat>(recognizers);
            reports[Flat] = won ? m.win(self) : m.lose(self);

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

        template <class Self, class Frame>
        constexpr void feed(Self &self, const Frame &f, time_point now) noexcept
        {
            base::template feed<Self, Frame, Trace>(self, f, now, obs);
        }

        template <class Self>
        constexpr void tick(Self &self, time_point now) noexcept
        {
            base::template tick<Self, Trace>(self, now, obs);
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
// ═══════════════════════════════════════════════════════════════════════
//  gesture —— 识别器库（无继承；Trait 是 config + data + handler）
// ═══════════════════════════════════════════════════════════════════════
namespace gesture
{
    using intent::claim_state;
    using intent::time_point;

    struct none_payload
    {
        friend constexpr bool operator==(none_payload, none_payload) = default;
    };

    template <class T>
    inline constexpr bool is_none_payload = std::is_same_v<T, none_payload>;

    namespace state
    {
        constexpr claim_state latch(claim_state &cur, claim_state s) noexcept
        {
            if (cur == claim_state::ready && s != claim_state::idle)
                s = claim_state::ready;
            cur = s;
            return s;
        }
        constexpr claim_state hold(claim_state &cur) noexcept
        {
            return latch(cur, claim_state::hold);
        }
        constexpr claim_state idle(claim_state &cur) noexcept
        {
            return latch(cur, claim_state::idle);
        }
        constexpr claim_state ready(claim_state &cur) noexcept
        {
            return latch(cur, claim_state::ready);
        }
        constexpr claim_state keep(claim_state &cur) noexcept
        {
            return latch(cur, cur);
        }
    } // namespace state

    // 非局部 base：name / fn_name / policy / payload_t 都在库里
    template <class Payload, static_string Name, static_string Fn, class Policy>
    struct trait_base
    {
        using payload_t = Payload;
        static constexpr auto name = Name;
        static constexpr auto fn_name = Fn;
        using policy = Policy;
    };

    // 数据槽：只有 data；none_payload 时为空
    template <class Trait, bool = is_none_payload<typename Trait::payload_t>>
    struct slot;

    template <class Trait>
    struct slot<Trait, false>
    {
        using payload_t = typename Trait::payload_t;
        [[no_unique_address]] payload_t data{};

        template <class Event>
        constexpr void collect(time_point t, const Event &e) noexcept
        {
            Trait::collect(t, data, e);
        }
    };

    template <class Trait>
    struct slot<Trait, true>
    {
        template <class... Args>
        constexpr void collect(Args &&...) noexcept
        {
        }
    };

    template <class Policy>
    struct press_watch
    {
        bool down{};
        time_point down_at{};
        double down_x{};
        double down_y{};

        constexpr int operator()(const input_frame::frame<input_frame::button_change> &f,
                                 time_point now) noexcept
        {
            const auto &b = f.change;
            if (b.button != Policy::button)
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
            if (!down)
                return 0;
            down = false;
            const bool quick = now - down_at <= time_point{Policy::tap_max_ms};
            const bool still =
                std::hypot(f.context.pointer_x - down_x, f.context.pointer_y - down_y) <=
                Policy::slop_px;
            return (quick && still) ? 2 : -1;
        }

        constexpr void clear() noexcept
        {
            down = false;
        }
    };

    // ── 语义事件 ────────────────────────────────────────────────────
    struct click_event
    {
        double x{}, y{};
        time_point at{};
    };
    struct multi_click_event
    {
        double x{}, y{};
        int count{};
        time_point at{};
    };
    enum class drag_phase : std::uint8_t
    {
        begin,
        move,
        end
    };
    struct drag_event
    {
        drag_phase phase{};
        double x{}, y{}, dx{}, dy{};
        double total_dx{}, total_dy{};
        time_point at{};
        ModifierKey modifier{};
    };
    struct wheel_event
    {
        int delta{};
        time_point at{};
    };
    struct key_event
    {
        Key key{};
        input_frame::Transition transition{};
        time_point at{};
    };

    // ── click_template ──────────────────────────────────────────────
    template <class Trait>
    struct click_template
    {
        using policy = typename Trait::policy;
        static constexpr auto name = Trait::name;
        static constexpr auto fn_name = Trait::fn_name;
        using consume_t = input_frame::changes<input_frame::button_change>;

        claim_state cur{};
        press_watch<policy> watch{};
        slot<Trait> in{};

        template <class Self>
        constexpr claim_state feed(
            Self &self, const input_frame::frame<input_frame::button_change> &f,
            time_point now) noexcept
        {
            switch (watch(f, now))
            {
            case 1:
                return state::hold(cur);
            case 2:
                in.collect(now,
                           click_event{f.context.pointer_x, f.context.pointer_y, now});
                return state::ready(cur);
            case -1:
                return state::idle(cur);
            default:
                return watch.down ? state::hold(cur) : state::keep(cur);
            }
        }

        template <class Self>
        constexpr claim_state tick(Self &, time_point) noexcept
        {
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state win(Self &self) noexcept
        {
            self.template invoke<fn_name>(in.data);
            cur = claim_state::idle;
            return cur;
        }

        template <class Self>
        constexpr claim_state lose(Self &) noexcept
        {
            watch.clear();
            cur = claim_state::idle;
            return cur;
        }
    };

    // ── multi_click_template ────────────────────────────────────────
    template <class Trait>
    struct multi_click_template
    {
        using policy = typename Trait::policy;
        static constexpr auto name = Trait::name;
        static constexpr auto fn_name = Trait::fn_name;
        using consume_t = input_frame::changes<input_frame::button_change>;

        claim_state cur{};
        press_watch<policy> watch{};
        slot<Trait> in{};
        int clicks{};
        time_point last{};
        bool armed{};

        template <class Self>
        constexpr claim_state feed(
            Self &self, const input_frame::frame<input_frame::button_change> &f,
            time_point now) noexcept
        {
            if (armed && now - last > time_point{policy::gap_ms})
            {
                reset_chain();
                (void)state::idle(cur);
            }
            switch (watch(f, now))
            {
            case 1:
                return state::hold(cur);
            case 2: {
                ++clicks;
                armed = true;
                last = now;
                if (clicks >= policy::count)
                {
                    const int count = clicks;
                    reset_chain();
                    in.collect(now, multi_click_event{f.context.pointer_x,
                                                      f.context.pointer_y, count, now});
                    return state::ready(cur);
                }
                return state::hold(cur);
            }
            case -1:
                reset_chain();
                return state::idle(cur);
            default:
                return (armed || clicks > 0 || watch.down) ? state::hold(cur)
                                                           : state::keep(cur);
            }
        }

        template <class Self>
        constexpr claim_state tick(Self &, time_point now) noexcept
        {
            if (armed && now - last > time_point{policy::gap_ms})
            {
                reset_chain();
                return state::idle(cur);
            }
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state win(Self &self) noexcept
        {
            self.template invoke<fn_name>(in.data);
            reset_chain();
            cur = claim_state::idle;
            return cur;
        }

        template <class Self>
        constexpr claim_state lose(Self &) noexcept
        {
            reset_chain();
            cur = claim_state::idle;
            return cur;
        }

        constexpr void reset_chain() noexcept
        {
            clicks = 0;
            armed = false;
            watch.clear();
        }
    };

    // ── drag_template ───────────────────────────────────────────────
    template <class Trait>
    struct drag_template
    {
        using policy = typename Trait::policy;
        static constexpr auto name = Trait::name;
        static constexpr auto fn_name = Trait::fn_name;
        using consume_t =
            input_frame::changes<input_frame::button_change, input_frame::move_change>;

        claim_state cur{};
        slot<Trait> in{};
        double down_x{}, down_y{}, last_x{}, last_y{};
        bool down{}, dragging{}, finished_{};

        template <class Self>
        constexpr claim_state feed(
            Self &self, const input_frame::frame<input_frame::button_change> &f,
            time_point now) noexcept
        {
            if (f.change.button != policy::button)
                return state::keep(cur);
            if (f.change.transition == input_frame::Transition::Press)
            {
                if (f.change.repeat)
                    return state::keep(cur);
                if (cur != claim_state::idle)
                    (void)state::idle(cur);
                down = true;
                dragging = false;
                down_x = last_x = f.context.pointer_x;
                down_y = last_y = f.context.pointer_y;
                return state::hold(cur);
            }
            down = false;
            if (!dragging)
                return state::idle(cur);
            dragging = false;
            finished_ = true;
            in.collect(
                now, drag_event{drag_phase::end, f.context.pointer_x, f.context.pointer_y,
                                0.0, 0.0, f.context.pointer_x - down_x,
                                f.context.pointer_y - down_y, now, f.context.modifier});
            return state::ready(cur);
        }

        template <class Self>
        constexpr claim_state feed(Self &self,
                                   const input_frame::frame<input_frame::move_change> &f,
                                   time_point now) noexcept
        {
            if (!down)
                return state::keep(cur);
            const auto &m = f.change;
            if (!dragging)
            {
                if (std::hypot(m.x - down_x, m.y - down_y) <= policy::slop_px)
                {
                    last_x = m.x;
                    last_y = m.y;
                    return state::hold(cur);
                }
                dragging = true;
                last_x = m.x;
                last_y = m.y;
                in.collect(now,
                           drag_event{drag_phase::begin, m.x, m.y, 0.0, 0.0, m.x - down_x,
                                      m.y - down_y, now, f.context.modifier});
                return state::ready(cur);
            }
            const double dx = m.x - last_x;
            const double dy = m.y - last_y;
            last_x = m.x;
            last_y = m.y;
            in.collect(now, drag_event{drag_phase::move, m.x, m.y, dx, dy, m.x - down_x,
                                       m.y - down_y, now, f.context.modifier});
            return state::ready(cur);
        }

        template <class Self>
        constexpr claim_state tick(Self &, time_point) noexcept
        {
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state win(Self &self) noexcept
        {
            self.template invoke<fn_name>(in.data);
            cur = finished_ ? claim_state::idle : claim_state::hold;
            finished_ = false;
            return cur;
        }

        template <class Self>
        constexpr claim_state lose(Self &) noexcept
        {
            down = false;
            dragging = false;
            finished_ = false;
            cur = claim_state::idle;
            return cur;
        }
    };

    // ── hover_template（none） ─────────────────────────────────────
    template <class Trait>
    struct hover_template
    {
        static constexpr auto name = Trait::name;
        static constexpr auto fn_name = Trait::fn_name;
        using consume_t = input_frame::changes<input_frame::cursor_enter_event>;

        claim_state cur{};
        slot<Trait> in{};

        template <class Self>
        constexpr claim_state feed(
            Self &self, const input_frame::frame<input_frame::cursor_enter_event> &f,
            time_point) noexcept
        {
            if (f.change.value)
                return state::ready(cur);
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state tick(Self &, time_point) noexcept
        {
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state win(Self &self) noexcept
        {
            self.template invoke<fn_name>();
            cur = claim_state::idle;
            return cur;
        }

        template <class Self>
        constexpr claim_state lose(Self &) noexcept
        {
            cur = claim_state::idle;
            return cur;
        }
    };

    // ── wheel_template ─────────────────────────────────────────────
    template <class Trait>
    struct wheel_template
    {
        static constexpr auto name = Trait::name;
        static constexpr auto fn_name = Trait::fn_name;
        using consume_t = input_frame::changes<scroll_event>;

        claim_state cur{};
        slot<Trait> in{};

        template <class Self>
        constexpr claim_state feed(Self &self, const input_frame::frame<scroll_event> &f,
                                   time_point now) noexcept
        {
            in.collect(now, wheel_event{static_cast<int>(f.change.yoffset), now});
            return state::ready(cur);
        }

        template <class Self>
        constexpr claim_state tick(Self &, time_point) noexcept
        {
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state win(Self &self) noexcept
        {
            self.template invoke<fn_name>(in.data);
            cur = claim_state::idle;
            return cur;
        }

        template <class Self>
        constexpr claim_state lose(Self &) noexcept
        {
            cur = claim_state::idle;
            return cur;
        }
    };

    // ── key_template（none） ───────────────────────────────────────
    template <class Trait>
    struct key_template
    {
        using policy = typename Trait::policy;
        static constexpr auto name = Trait::name;
        static constexpr auto fn_name = Trait::fn_name;
        using consume_t = input_frame::changes<input_frame::key_change>;

        claim_state cur{};
        slot<Trait> in{};

        template <class Self>
        constexpr claim_state feed(Self &self,
                                   const input_frame::frame<input_frame::key_change> &f,
                                   time_point) noexcept
        {
            if (f.change.transition == input_frame::Transition::Press &&
                !f.change.repeat && f.change.key == policy::key)
                return state::ready(cur);
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state tick(Self &, time_point) noexcept
        {
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state win(Self &self) noexcept
        {
            self.template invoke<fn_name>();
            cur = claim_state::idle;
            return cur;
        }

        template <class Self>
        constexpr claim_state lose(Self &) noexcept
        {
            cur = claim_state::idle;
            return cur;
        }
    };

    // ── key_observer_template ──────────────────────────────────────
    template <class Trait>
    struct key_observer_template
    {
        static constexpr auto name = Trait::name;
        static constexpr auto fn_name = Trait::fn_name;
        using consume_t = input_frame::changes<input_frame::key_change>;

        claim_state cur{};
        slot<Trait> in{};

        template <class Self>
        constexpr claim_state feed(Self &self,
                                   const input_frame::frame<input_frame::key_change> &f,
                                   time_point now) noexcept
        {
            if (f.change.repeat)
                return state::keep(cur);
            in.collect(now, key_event{f.change.key, f.change.transition, now});
            return state::ready(cur);
        }

        template <class Self>
        constexpr claim_state tick(Self &, time_point) noexcept
        {
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state win(Self &self) noexcept
        {
            self.template invoke<fn_name>(in.data);
            cur = claim_state::idle;
            return cur;
        }

        template <class Self>
        constexpr claim_state lose(Self &) noexcept
        {
            cur = claim_state::idle;
            return cur;
        }
    };

    // ── chord_template ─────────────────────────────────────────────
    template <class Trait>
    struct chord_template
    {
        static constexpr auto name = Trait::name;
        static constexpr auto fn_name = Trait::fn_name;
        using consume_t = input_frame::changes<input_frame::key_change>;

        claim_state cur{};
        slot<Trait> in{};

        template <class Self>
        constexpr claim_state feed(Self &self,
                                   const input_frame::frame<input_frame::key_change> &f,
                                   time_point now) noexcept
        {
            if (auto e = Trait::match(f.change, now))
            {
                in.collect(now, *e);
                return state::ready(cur);
            }
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state tick(Self &, time_point) noexcept
        {
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state win(Self &self) noexcept
        {
            self.template invoke<fn_name>(in.data);
            cur = claim_state::idle;
            return cur;
        }

        template <class Self>
        constexpr claim_state lose(Self &) noexcept
        {
            cur = claim_state::idle;
            return cur;
        }
    };
} // namespace gesture

// ═══════════════════════════════════════════════════════════════════════
//  mcs::app —— 数据 + Trait + tree + arena
// ═══════════════════════════════════════════════════════════════════════
namespace mcs::app
{
    using intent::time_point;

    inline constexpr std::int64_t click_max_ms = 250;
    inline constexpr std::int64_t dbl_gap_ms = 300;
    inline constexpr std::int64_t triple_gap_ms = 250;
    inline constexpr double drag_slop_px = 4.0;

    namespace policy
    {
        struct click
        {
            static constexpr auto button = input_frame::MouseButton::eMOUSE_BUTTON_LEFT;
            static constexpr std::int64_t tap_max_ms = mcs::app::click_max_ms;
            static constexpr double slop_px = mcs::app::drag_slop_px;
        };
        struct multi
        {
            static constexpr auto button = input_frame::MouseButton::eMOUSE_BUTTON_LEFT;
            static constexpr std::int64_t tap_max_ms = mcs::app::click_max_ms;
            static constexpr double slop_px = mcs::app::drag_slop_px;
            static constexpr int count = 2;
            static constexpr std::int64_t gap_ms = mcs::app::dbl_gap_ms;
        };
        struct multi3
        {
            static constexpr auto button = input_frame::MouseButton::eMOUSE_BUTTON_LEFT;
            static constexpr std::int64_t tap_max_ms = mcs::app::click_max_ms;
            static constexpr double slop_px = mcs::app::drag_slop_px;
            static constexpr int count = 3;
            static constexpr std::int64_t gap_ms = mcs::app::triple_gap_ms;
        };
        struct drag
        {
            static constexpr auto button = input_frame::MouseButton::eMOUSE_BUTTON_LEFT;
            static constexpr double slop_px = mcs::app::drag_slop_px;
        };
        struct hover
        {
        };
        struct wheel
        {
        };
        struct key_a
        {
            static constexpr auto key = Key::eA;
        };
        struct key_c
        {
            static constexpr auto key = Key::eC;
        };
        struct shortcut
        {
        };
    } // namespace policy

    namespace detail
    {
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

        struct shortcut_event
        {
            std::array<command, 2> items{};
            int n = 0;
        };

        struct counters
        {
            int click{};
            int double_click{};
            int triple_click{};
            int hover{};
            int wheel{};
            int key_a{};
            int key_c{};
            int shortcut{};
            int any_key{};
            int commands{};
            int drag_begin{};
            int drag_move{};
            int drag_end{};
            double x{}, y{};
            double total_dx{}, total_dy{};
            double move_x{}, move_y{}, move_dx{}, move_dy{};
            int delta{};
            friend constexpr bool operator==(const counters &,
                                             const counters &) = default;
        };

        inline time_point now_ms() noexcept
        {
            static const auto t0 = std::chrono::steady_clock::now();
            return std::chrono::duration_cast<time_point>(
                std::chrono::steady_clock::now() - t0);
        }

        template <class Self>
        inline time_point now_of(Self &self) noexcept
        {
            return self.manual_clock ? self.now : now_ms();
        }

        constexpr std::string_view to_string(intent::verdict v) noexcept
        {
            return v == intent::verdict::accepted ? "acc" : "sup";
        }

        template <class App>
        inline void report(const App &app) noexcept
        {
            for (const auto &e : app.arbiter.events())
            {
                if (e.name == "drag" || e.name == "hover")
                    continue;
                std::println("  [t={:>6}ms] {:<18} {:<3}", e.at.count(), e.name,
                             to_string(e.v));
            }
        }
    } // namespace detail

    // ── arena（局部 Trait：只写 payload + collect）────────────────
    inline auto make_arena()
    {
        struct click_payload
        {
            double x{}, y{};
            time_point at{};
        };
        struct multi_click_payload
        {
            double x{}, y{};
            int count{};
            time_point at{};
        };
        struct drag_payload
        {
            gesture::drag_phase phase{};
            double x{}, y{}, dx{}, dy{};
            double total_dx{}, total_dy{};
            ModifierKey modifier{};
            time_point at{};
        };
        struct wheel_payload
        {
            int delta{};
            time_point at{};
        };
        struct any_key_payload
        {
            Key key{};
            input_frame::Transition transition{};
            time_point at{};
        };

        // name / fn_name / policy / payload_t 都在 base；局部只写 payload + collect
        struct Trait_click : gesture::trait_base<click_payload, static_string{"click"},
                                                 static_string{"on_click"}, policy::click>
        {
            static void collect(time_point, payload_t &d,
                                const gesture::click_event &e) noexcept
            {
                d = {e.x, e.y, e.at};
            }
        };

        struct Trait_double_click
            : gesture::trait_base<multi_click_payload, static_string{"double_click"},
                                  static_string{"on_double_click"}, policy::multi>
        {
            static void collect(time_point, payload_t &d,
                                const gesture::multi_click_event &e) noexcept
            {
                d = {e.x, e.y, e.count, e.at};
            }
        };

        struct Trait_triple_click
            : gesture::trait_base<multi_click_payload, static_string{"triple_click"},
                                  static_string{"on_triple_click"}, policy::multi3>
        {
            static void collect(time_point, payload_t &d,
                                const gesture::multi_click_event &e) noexcept
            {
                d = {e.x, e.y, e.count, e.at};
            }
        };

        struct Trait_drag : gesture::trait_base<drag_payload, static_string{"drag"},
                                                static_string{"on_drag"}, policy::drag>
        {
            static void collect(time_point, payload_t &d,
                                const gesture::drag_event &e) noexcept
            {
                d = {e.phase,    e.x,        e.y,        e.dx, e.dy,
                     e.total_dx, e.total_dy, e.modifier, e.at};
            }
        };

        struct Trait_hover
            : gesture::trait_base<gesture::none_payload, static_string{"hover"},
                                  static_string{"on_hover"}, policy::hover>
        {
        };

        struct Trait_wheel : gesture::trait_base<wheel_payload, static_string{"wheel"},
                                                 static_string{"on_wheel"}, policy::wheel>
        {
            static void collect(time_point, payload_t &d,
                                const gesture::wheel_event &e) noexcept
            {
                d = {e.delta, e.at};
            }
        };

        struct Trait_key_a
            : gesture::trait_base<gesture::none_payload, static_string{"key_a"},
                                  static_string{"on_key_a"}, policy::key_a>
        {
        };

        struct Trait_key_c
            : gesture::trait_base<gesture::none_payload, static_string{"key_c"},
                                  static_string{"on_key_c"}, policy::key_c>
        {
        };

        struct Trait_shortcut
            : gesture::trait_base<detail::shortcut_event, static_string{"shortcut"},
                                  static_string{"on_shortcut"}, policy::shortcut>
        {
            static std::optional<payload_t> match(const input_frame::key_change &c,
                                                  time_point) noexcept
            {
                if (c.transition != input_frame::Transition::Press || c.repeat)
                    return std::nullopt;
                const bool ctrl = c.modifier.hasAll(ModifierKey::Control());
                const bool shift = c.modifier.hasAll(ModifierKey::Shift());
                const bool alt = c.modifier.hasAll(ModifierKey::Alt());
                if (!ctrl || alt)
                    return std::nullopt;
                payload_t r{};
                if (c.key == Key::eC)
                {
                    r.items[0] =
                        shift ? detail::command::copy_plain : detail::command::copy;
                    r.n = 1;
                    return r;
                }
                if (c.key == Key::eS)
                {
                    r.items[0] = detail::command::save;
                    r.items[1] = detail::command::notify;
                    r.n = 2;
                    return r;
                }
                if (c.key == Key::eV)
                {
                    r.items[0] = detail::command::paste;
                    r.n = 1;
                    return r;
                }
                return std::nullopt;
            }
            static void collect(time_point, payload_t &d, const payload_t &e) noexcept
            {
                d = e;
            }
        };

        struct Trait_any_key
            : gesture::trait_base<any_key_payload, static_string{"any_key"},
                                  static_string{"on_any_key"}, policy::hover>
        {
            static void collect(time_point, payload_t &d,
                                const gesture::key_event &e) noexcept
            {
                d = {e.key, e.transition, e.at};
            }
        };

        using tree = intent::all_of<
            intent::refines<
                intent::leaf<gesture::click_template<Trait_click>>,
                intent::all_of<intent::leaf<gesture::drag_template<Trait_drag>>,
                               intent::refines<intent::leaf<gesture::multi_click_template<
                                                   Trait_double_click>>,
                                               intent::leaf<gesture::multi_click_template<
                                                   Trait_triple_click>>>>>,
            intent::leaf<gesture::hover_template<Trait_hover>>,
            intent::leaf<gesture::wheel_template<Trait_wheel>>,
            intent::refines<intent::leaf<gesture::key_template<Trait_key_c>>,
                            intent::leaf<gesture::chord_template<Trait_shortcut>>>,
            intent::leaf<gesture::key_template<Trait_key_a>>,
            intent::leaf<gesture::key_observer_template<Trait_any_key>>>;

        return make_aggregate<"arena">(
            field<"click">(Trait_click::payload_t{}),
            field<"double_click">(Trait_double_click::payload_t{}),
            field<"triple_click">(Trait_triple_click::payload_t{}),
            field<"drag">(Trait_drag::payload_t{}),
            field<"wheel">(Trait_wheel::payload_t{}),
            field<"shortcut">(Trait_shortcut::payload_t{}),
            field<"key_event">(Trait_any_key::payload_t{}),
            field<"arbiter">(intent::debug_arbiter<tree>{}),
            field<"counters">(detail::counters{}),
            field<"input_state">(input_frame::event_context{}),
            field<"now">(time_point{}), field<"manual_clock">(false),
            field<"keep_trace">(false), field<"verbose">(true), field<"frames">(0u),
            field<"input_binding">(glfw_input_translation::scoped_binding{}),

            method<"on_click">([](auto &self, const Trait_click::payload_t &e) noexcept {
                ++self.counters.click;
                self.click = e;
                self.counters.x = e.x;
                self.counters.y = e.y;
                if (self.verbose)
                    std::println("  >>> 单击    (x={:.1f}, y={:.1f})", e.x, e.y);
            }),
            method<"on_double_click">(
                [](auto &self, const Trait_double_click::payload_t &e) noexcept {
                    ++self.counters.double_click;
                    self.double_click = e;
                    self.counters.x = e.x;
                    self.counters.y = e.y;
                    if (self.verbose)
                        std::println("  >>> 双击    (x={:.1f}, y={:.1f}, count={})", e.x,
                                     e.y, e.count);
                }),
            method<"on_triple_click">(
                [](auto &self, const Trait_triple_click::payload_t &e) noexcept {
                    ++self.counters.triple_click;
                    self.triple_click = e;
                    self.counters.x = e.x;
                    self.counters.y = e.y;
                    if (self.verbose)
                        std::println("  >>> 三击    (x={:.1f}, y={:.1f}, count={})", e.x,
                                     e.y, e.count);
                }),
            method<"on_drag">([](auto &self, const Trait_drag::payload_t &e) noexcept {
                self.drag = e;
                switch (e.phase)
                {
                case gesture::drag_phase::begin:
                    ++self.counters.drag_begin;
                    if (self.verbose)
                        std::println("  >>> 拖拽·开始 (起点 {:.1f},{:.1f})", e.x, e.y);
                    break;
                case gesture::drag_phase::move:
                    ++self.counters.drag_move;
                    self.counters.move_x = e.x;
                    self.counters.move_y = e.y;
                    self.counters.move_dx = e.dx;
                    self.counters.move_dy = e.dy;
                    if (self.verbose)
                        std::println(
                            "  >>> 拖拽·移动 (到 {:.1f},{:.1f}; d={:.1f},{:.1f})", e.x,
                            e.y, e.dx, e.dy);
                    break;
                case gesture::drag_phase::end:
                    ++self.counters.drag_end;
                    self.counters.x = e.x;
                    self.counters.y = e.y;
                    self.counters.total_dx = e.total_dx;
                    self.counters.total_dy = e.total_dy;
                    if (self.verbose)
                        std::println(
                            "  >>> 拖拽·释放 (到 {:.1f},{:.1f}; 总位移 {:.1f},{:.1f})",
                            e.x, e.y, e.total_dx, e.total_dy);
                    break;
                }
            }),
            method<"on_hover">([](auto &self) noexcept {
                ++self.counters.hover;
                if (self.verbose)
                    std::println("  >>> 悬停    进入窗口");
            }),
            method<"on_wheel">([](auto &self, const Trait_wheel::payload_t &e) noexcept {
                ++self.counters.wheel;
                self.wheel = e;
                self.counters.delta = e.delta;
                if (self.verbose)
                    std::println("  >>> 滚轮    (delta={})", e.delta);
            }),
            method<"on_key_a">([](auto &self) noexcept {
                ++self.counters.key_a;
                if (self.verbose)
                    std::println("  >>> 键盘 A");
            }),
            method<"on_key_c">([](auto &self) noexcept {
                ++self.counters.key_c;
                if (self.verbose)
                    std::println("  >>> 键盘 C");
            }),
            method<"on_shortcut">(
                [](auto &self, const Trait_shortcut::payload_t &e) noexcept {
                    ++self.counters.shortcut;
                    self.shortcut = e;
                    self.counters.commands += e.n;
                    if (self.verbose)
                    {
                        std::print("  >>> 快捷键");
                        for (int i = 0; i < e.n; ++i)
                            std::print(" {}", detail::command_name(e.items[i]));
                        std::println("");
                    }
                }),
            method<"on_any_key">([](auto &self,
                                    const Trait_any_key::payload_t &e) noexcept {
                ++self.counters.any_key;
                self.key_event = e;
                if (self.verbose)
                    std::println("  >>> HUD key={} {}", static_cast<int>(e.key),
                                 e.transition == input_frame::Transition::Press ? "down"
                                                                                : "up");
            }),

            method<"tick">([](auto &self) noexcept {
                const auto now = detail::now_of(self);
                self.now = now;
                self.arbiter.tick(self, now);
                if (self.verbose)
                    detail::report(self);
                if (!self.keep_trace)
                    self.arbiter.clear_events();
                ++self.frames;
            }),

            method<"input_event">([](auto &self, auto e) noexcept {
                const auto now = detail::now_of(self);
                self.now = now;
                using frame_t = decltype(input_frame::build_change(self.input_state, e));
                if constexpr (!std::is_void_v<frame_t>)
                {
                    auto f = input_frame::build_change(self.input_state, e);
                    self.arbiter.feed(self, f, now);
                    input_frame::apply_change(self.input_state, f);
                }
            }));
    }

    inline int selftest()
    {
        auto arena = make_arena();
        arena.manual_clock = true;
        arena.keep_trace = true;
        arena.verbose = false;

        int fails = 0;
        auto check = [&](std::string_view what, bool ok) {
            std::println("  {} {}", ok ? "✓" : "✗", what);
            if (!ok)
                ++fails;
        };

        auto key = [&](Key k, Action a, ModifierKey mods, std::int64_t ms) {
            arena.now = time_point{ms};
            arena.invoke<"input_event">(keyboard_event{k, a, mods});
        };
        auto move_to = [&](double x, double y, std::int64_t ms) {
            arena.now = time_point{ms};
            arena.invoke<"input_event">(position2d_event{x, y});
        };
        auto press = [&](std::int64_t ms) {
            arena.now = time_point{ms};
            arena.invoke<"input_event">(mousebutton_event{
                MouseButtons::eMOUSE_BUTTON_LEFT, Action::ePRESS, ModifierKey::None()});
        };
        auto release = [&](std::int64_t ms) {
            arena.now = time_point{ms};
            arena.invoke<"input_event">(mousebutton_event{
                MouseButtons::eMOUSE_BUTTON_LEFT, Action::eRELEASE, ModifierKey::None()});
        };
        auto tick = [&](std::int64_t ms) {
            arena.now = time_point{ms};
            arena.invoke<"tick">();
        };
        auto count = [&](std::string_view name, std::int64_t at) {
            int n = 0;
            for (const auto &e : arena.arbiter.events())
                if (e.name == name && e.v == intent::verdict::accepted &&
                    e.at.count() == at)
                    ++n;
            return n;
        };

        move_to(100, 100, 900);
        press(1000);
        release(1050);
        tick(1400);
        check("单击 -> on_click", arena.counters.click == 1);
        check("单击的数据在 arena.click", arena.click.x == 100 && arena.click.y == 100);

        move_to(200, 200, 1900);
        press(2000);
        release(2020);
        press(2100);
        release(2120);
        tick(2400);
        check("双击 -> on_double_click", arena.counters.double_click == 1);
        check("双击的数据在 arena.double_click", arena.double_click.count == 2);
        check("双击时单击不重复", arena.counters.click == 1);

        move_to(300, 300, 2900);
        press(3000);
        release(3020);
        press(3100);
        release(3120);
        press(3200);
        release(3220);
        tick(3220);
        check("三击 -> on_triple_click", arena.counters.triple_click == 1);
        check("三击的数据在 arena.triple_click", arena.triple_click.count == 3);

        move_to(10, 10, 3900);
        press(4000);
        move_to(20, 20, 4010);
        move_to(30, 30, 4020);
        release(4030);
        check("拖拽 begin/move/end", arena.counters.drag_begin == 1 &&
                                         arena.counters.drag_move == 1 &&
                                         arena.counters.drag_end == 1);
        check("拖拽的数据在 arena.drag",
              arena.drag.total_dx == 20.0 && arena.drag.total_dy == 20.0);

        arena.now = time_point{5000};
        arena.invoke<"input_event">(scroll_event{0.0, 1.0});
        check("滚轮 -> on_wheel", arena.counters.wheel == 1);
        check("滚轮的数据在 arena.wheel", arena.wheel.delta == 1);

        arena.now = time_point{6000};
        arena.invoke<"input_event">(cursor_enter_event{true});
        check("进入窗口 -> on_hover", arena.counters.hover == 1);

        key(Key::eC, Action::ePRESS, ModifierKey::None(), 7000);
        check("plain C -> on_key_c", arena.counters.key_c == 1);
        check("plain C -> HUD 同时 emit",
              count("key_c", 7000) == 1 && count("any_key", 7000) == 1);
        check("HUD 的数据在 arena.key_event", arena.key_event.key == Key::eC);

        key(Key::eLEFT_CONTROL, Action::ePRESS, ModifierKey::Control(), 7100);
        key(Key::eC, Action::ePRESS, ModifierKey::Control(), 7120);
        check("Ctrl+C -> on_shortcut", arena.counters.shortcut == 1);
        check("Ctrl+C -> key_c 被压掉", count("key_c", 7120) == 0);
        check("Ctrl+C -> shortcut + HUD",
              count("shortcut", 7120) == 1 && count("any_key", 7120) == 1);
        check("Ctrl+C 只带一个 command", arena.counters.commands == 1);
        check("快捷键的数据在 arena.shortcut", arena.shortcut.n == 1);
        key(Key::eC, Action::eRELEASE, ModifierKey::Control(), 7130);
        key(Key::eLEFT_CONTROL, Action::eRELEASE, ModifierKey::None(), 7140);

        key(Key::eLEFT_CONTROL, Action::ePRESS, ModifierKey::Control(), 7150);
        key(Key::eLEFT_SHIFT, Action::ePRESS, ModifierKey::CtrlShift(), 7160);
        key(Key::eC, Action::ePRESS, ModifierKey::CtrlShift(), 7170);
        check("Ctrl+Shift+C -> copy_plain",
              count("shortcut", 7170) == 1 && arena.counters.shortcut == 2);
        check("Ctrl+Shift+C -> key_c 被压掉", count("key_c", 7170) == 0);
        key(Key::eC, Action::eRELEASE, ModifierKey::CtrlShift(), 7180);
        key(Key::eLEFT_SHIFT, Action::eRELEASE, ModifierKey::Control(), 7190);
        key(Key::eLEFT_CONTROL, Action::eRELEASE, ModifierKey::None(), 7200);

        key(Key::eLEFT_CONTROL, Action::ePRESS, ModifierKey::Control(), 7210);
        key(Key::eS, Action::ePRESS, ModifierKey::Control(), 7220);
        check("Ctrl+S -> 一个 shortcut，两个 command",
              count("shortcut", 7220) == 1 && arena.counters.commands == 4);
        key(Key::eS, Action::eRELEASE, ModifierKey::Control(), 7230);
        key(Key::eLEFT_CONTROL, Action::eRELEASE, ModifierKey::None(), 7240);

        const int before = arena.counters.shortcut;
        key(Key::eS, Action::ePRESS, ModifierKey::None(), 7250);
        check("plain S -> 没有快捷键，只有 HUD",
              arena.counters.shortcut == before && count("shortcut", 7250) == 0);

        key(Key::eA, Action::ePRESS, ModifierKey::None(), 7300);
        check("plain A -> on_key_a + HUD",
              arena.counters.key_a == 1 && count("any_key", 7300) == 1);

        std::println("自检：{} 条失败", fails);
        return fails == 0 ? 0 : 1;
    }
} // namespace mcs::app

namespace app = mcs::app;

// ====================================== 测试用例 =======================================
#include <cstdio>
#include <cstdlib>

#define CHECK(c)                                                 \
    do                                                           \
    {                                                            \
        if (!(c))                                                \
        {                                                        \
            std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); \
            std::exit(1);                                        \
        }                                                        \
    } while (0)

struct mock_handler
{
    using fn_t = void (*)(void *, int);
    fn_t fn = nullptr;
    constexpr void invoke(void *obj, const void *, void *data) const noexcept
    {
        if (fn)
            fn(obj, *static_cast<const int *>(data));
    }
    template <class Obj>
    static constexpr mock_handler build() noexcept
    {
        return {[](void *p, int v) noexcept { static_cast<Obj *>(p)->on_event(v); }};
    }
};

struct Counter
{
    int calls = 0;
    int last = -1;
    void on_event(int v) noexcept
    {
        ++calls;
        last = v;
    }
};

struct Probe
{
    int calls = 0;
    void (*hook)(Probe *, int) = nullptr;
    void on_event(int v) noexcept
    {
        ++calls;
        if (hook)
            hook(this, v);
    }
};

using sink_t = multicast_sink<mock_handler>;

static void dispatch_int(sink_t &s, int v)
{
    s.dispatch(static_cast<const void *>(nullptr), static_cast<void *>(&v));
}

// ════════════════════════════════════════════════════════════════════
// 第一组：sink 原语层（bind / unbind / activate / deactivate / dispatch / handle）
// ════════════════════════════════════════════════════════════════════

// ---- 1) 1000 slot 里只有 10 live：dispatch 只遍历 live ----
void test_dispatch_complexity()
{
    sink_t s;
    s.reserve(2048);
    std::vector<Counter> cs(1000);
    std::vector<sink_t::handle> hs;
    hs.reserve(1000);
    for (int i = 0; i < 1000; ++i)
        hs.push_back(s.bind(&cs[i], mock_handler::build<Counter>()));

    // 只留前 10 个
    for (int i = 10; i < 1000; ++i)
        s.unbind(hs[i]);

    CHECK(s.slot_count() == 1000);
    CHECK(s.live_count() == 10);
    CHECK(s.active_list_size() == 10);

    dispatch_int(s, 5);
    for (int i = 0; i < 10; ++i)
        CHECK(cs[i].calls == 1);
    for (int i = 10; i < 1000; ++i)
        CHECK(cs[i].calls == 0);
}

// ---- 2) 反复 bind/unbind：active_list 恒等于 N_live ----
void test_active_list_stays_compact()
{
    sink_t s;
    std::vector<Counter> pool(100);
    sink_t::handle prev{};
    for (int round = 0; round < 100; ++round)
    {
        if (prev.valid())
            s.unbind(prev);
        prev = s.bind(&pool[round], mock_handler::build<Counter>());
        CHECK(s.active_list_size() == 1);
    }
    s.unbind(prev);
    CHECK(s.active_list_size() == 0);
    CHECK(s.slot_count() <= 100);
}

// ---- 3) UI 模式：打开 5 个窗口，关掉，循环 ----
void test_ui_pattern()
{
    sink_t s;
    s.reserve(32);
    std::vector<Counter> windows(50);

    for (int cycle = 0; cycle < 20; ++cycle)
    {
        std::vector<sink_t::handle> hs;
        for (int i = 0; i < 5; ++i)
            hs.push_back(
                s.bind(&windows[cycle * 5 % 50 + i], mock_handler::build<Counter>()));
        CHECK(s.active_list_size() == 5);
        dispatch_int(s, cycle);
        for (auto h : hs)
            s.unbind(h);
        CHECK(s.active_list_size() == 0);
    }
    CHECK(s.slot_count() <= 32);
}

// ---- 4) 分发中 deactivate / unbind：本轮跳过 ----
void test_mutate_in_dispatch()
{
    sink_t s;
    Probe a, b, c;
    (void)s.bind(&a, mock_handler::build<Probe>());
    auto hb = s.bind(&b, mock_handler::build<Probe>());
    auto hc = s.bind(&c, mock_handler::build<Probe>());

    static sink_t *gs = nullptr;
    static sink_t::handle g_hb{}, g_hc{};
    static bool fired = false;
    gs = &s;
    g_hb = hb;
    g_hc = hc;
    fired = false;
    a.hook = [](Probe *, int) {
        if (!fired)
        {
            gs->unbind(g_hc);
            gs->deactivate(g_hb);
            fired = true;
        }
    };

    dispatch_int(s, 0);
    CHECK(a.calls == 1 && b.calls == 0 && c.calls == 0);
    CHECK(s.active_list_size() == 1); // 分发结束 compact 后只剩 a

    a.hook = nullptr;
    dispatch_int(s, 0);
    CHECK(b.calls == 0 && c.calls == 0);
}

// ---- 5) 分发中 activate：下轮生效 ----
void test_activate_in_dispatch()
{
    sink_t s;
    Probe a, c;
    (void)s.bind(&a, mock_handler::build<Probe>());
    auto hc = s.bind(&c, mock_handler::build<Probe>());
    s.deactivate(hc);
    CHECK(s.active_list_size() == 1);

    static sink_t *gs = nullptr;
    static sink_t::handle g_hc{};
    static bool fired = false;
    gs = &s;
    g_hc = hc;
    fired = false;
    a.hook = [](Probe *, int) {
        if (!fired)
        {
            gs->activate(g_hc);
            fired = true;
        }
    };

    dispatch_int(s, 0);
    CHECK(a.calls == 1);
    CHECK(c.calls == 0); // 本轮快照外

    a.hook = nullptr;
    dispatch_int(s, 0);
    CHECK(c.calls == 1); // 下轮生效
}

// ---- 6) 分发外 activate：O(1) 立即生效 ----
void test_activate_outside_dispatch()
{
    sink_t s;
    Counter a;
    auto ha = s.bind(&a, mock_handler::build<Counter>());
    s.deactivate(ha);
    CHECK(s.active_list_size() == 0);
    s.activate(ha);
    CHECK(s.active_list_size() == 1);
    dispatch_int(s, 42);
    CHECK(a.calls == 1 && a.last == 42);
}

// ---- 7) 空 handle：全 no-op ----
void test_invalid_handles()
{
    sink_t s;
    Counter a;
    (void)s.bind(&a, mock_handler::build<Counter>());

    sink_t::handle empty{};
    s.activate(empty);
    s.deactivate(empty);
    s.unbind(empty);
    CHECK(s.active_list_size() == 1);

    dispatch_int(s, 0);
    CHECK(a.calls == 1);
}

// ---- 8) 槽位复用：旧 handle 永久 no-op ----
void test_slot_recycle()
{
    sink_t s;
    Counter a, b;
    auto ha = s.bind(&a, mock_handler::build<Counter>());
    s.unbind(ha);
    auto hb = s.bind(&b, mock_handler::build<Counter>());
    CHECK(hb.slot == ha.slot);
    CHECK(hb.gen > ha.gen);

    s.unbind(ha);
    s.activate(ha);
    dispatch_int(s, 0);
    CHECK(a.calls == 0 && b.calls == 1);
}

// ---- 9) 分发中 bind：本轮快照外 ----
void test_bind_in_dispatch()
{
    sink_t s;
    s.reserve(8);
    Probe a, late;
    (void)s.bind(&a, mock_handler::build<Probe>());

    static sink_t *gs = nullptr;
    static Probe *gl = nullptr;
    static bool fired = false;
    gs = &s;
    gl = &late;
    fired = false;
    a.hook = [](Probe *, int) {
        if (!fired)
        {
            (void)gs->bind(gl, mock_handler::build<Probe>());
            fired = true;
        }
    };

    dispatch_int(s, 0);
    CHECK(a.calls == 1 && late.calls == 0);

    a.hook = nullptr;
    dispatch_int(s, 0);
    CHECK(late.calls == 1);
}

// ---- 10) 混合模式压力 ----
void test_stress()
{
    sink_t s;
    s.reserve(64);
    std::vector<Counter> pool(64);
    std::vector<sink_t::handle> live;
    int dispatched = 0;

    for (int round = 0; round < 1000; ++round)
    {
        auto h = s.bind(&pool[round % 64], mock_handler::build<Counter>());
        live.push_back(h);
        if (live.size() > 8)
        {
            s.unbind(live.front());
            live.erase(live.begin());
        }
        if (!live.empty() && (round & 1))
            s.deactivate(live.back());
        else if (!live.empty())
            s.activate(live.back());

        dispatch_int(s, round);
        ++dispatched;
        CHECK(s.active_list_size() <= live.size());
    }
    CHECK(dispatched == 1000);
    CHECK(s.slot_count() <= 64);
}

// ════════════════════════════════════════════════════════════════════
// 第二组：scoped_binding 层（RAII）
// ════════════════════════════════════════════════════════════════════

// ---- 11) 基础：析构自动 unbind ----
void test_scoped_binding_basic()
{
    sink_t s;
    Counter a;
    {
        auto b = s.attach(&a, mock_handler::build<Counter>());
        CHECK(b.valid());
        CHECK(s.active_list_size() == 1);
        dispatch_int(s, 1);
        CHECK(a.calls == 1);
    } // b 析构 → unbind
    CHECK(s.active_list_size() == 0);
    dispatch_int(s, 1);
    CHECK(a.calls == 1); // 不再被调用
}

// ---- 12) 空语义：所有操作 no-op ----
void test_scoped_binding_empty()
{
    sink_t s;
    sink_t::scoped_binding empty;
    CHECK(!empty.valid());
    empty.activate();   // no-op
    empty.deactivate(); // no-op
    empty.release();    // no-op
    CHECK(empty.get_handle().slot == sink_t::npos);
    CHECK(s.active_list_size() == 0);
}

// ---- 13) 移动构造：接管所有权，源变空 ----
void test_scoped_binding_move_construct()
{
    sink_t s;
    Counter a;
    {
        sink_t::scoped_binding b1 = s.attach(&a, mock_handler::build<Counter>());
        CHECK(b1.valid());
        sink_t::scoped_binding b2 = std::move(b1);
        CHECK(!b1.valid()); // 源变空
        CHECK(b2.valid());
        CHECK(s.active_list_size() == 1);
    } // b2 析构 → unbind；b1 析构 no-op
    CHECK(s.active_list_size() == 0);
}

// ---- 14) activate/deactivate 与 RAII 正交 ----
void test_scoped_binding_toggle_then_release()
{
    sink_t s;
    Counter a;
    {
        auto b = s.attach(&a, mock_handler::build<Counter>());
        b.deactivate();
        CHECK(s.active_list_size() == 0);
        dispatch_int(s, 1);
        CHECK(a.calls == 0);

        b.activate();
        dispatch_int(s, 1);
        CHECK(a.calls == 1);

        b.deactivate(); // 再休止
        // 作用域结束：析构 → unbind 不看 active
    }
    CHECK(s.active_list_size() == 0);
    dispatch_int(s, 1);
    CHECK(a.calls == 1); // 之后不再被调用
}

// ---- 15) release 提前释放；析构 no-op ----
void test_scoped_binding_release_early()
{
    sink_t s;
    Counter a;
    auto b = s.attach(&a, mock_handler::build<Counter>());
    CHECK(b.valid());
    b.release();
    CHECK(!b.valid());
    CHECK(s.active_list_size() == 0);
    dispatch_int(s, 1);
    CHECK(a.calls == 0);
    b.release(); // 重复 release：no-op
    // b 析构 no-op
}

// ---- 16) 多个 scoped_binding 并存，独立控制 ----
void test_scoped_binding_multiple()
{
    sink_t s;
    Counter a, b, c;
    auto ba = s.attach(&a, mock_handler::build<Counter>());
    auto bb = s.attach(&b, mock_handler::build<Counter>());
    auto bc = s.attach(&c, mock_handler::build<Counter>());
    CHECK(s.active_list_size() == 3);
    dispatch_int(s, 1);
    CHECK(a.calls == 1 && b.calls == 1 && c.calls == 1);

    bb.release(); // 只释放 b
    CHECK(s.active_list_size() == 2);
    dispatch_int(s, 2);
    CHECK(a.calls == 2 && b.calls == 1 && c.calls == 2);
}

// ---- 17) 移动赋值：先释放旧绑定再接管 ----
void test_scoped_binding_move_assign()
{
    sink_t s;
    Counter a, b;
    auto ba = s.attach(&a, mock_handler::build<Counter>());
    auto bb = s.attach(&b, mock_handler::build<Counter>());
    CHECK(s.active_list_size() == 2);

    ba = std::move(bb); // ba 先 release(a)，再接管 b
    CHECK(!bb.valid());
    CHECK(ba.valid());
    CHECK(s.active_list_size() == 1);

    dispatch_int(s, 1);
    CHECK(a.calls == 0); // a 已解绑
    CHECK(b.calls == 1);
}

// ---- 18) out 参数版 attach ----
void test_scoped_binding_attach_out()
{
    sink_t s;
    Counter a;
    sink_t::scoped_binding b;
    s.attach(b, &a, mock_handler::build<Counter>());
    CHECK(b.valid());
    CHECK(s.active_list_size() == 1);
    dispatch_int(s, 1);
    CHECK(a.calls == 1);
    // b 析构时自动 unbind
}

// ════════════════════════════════════════════════════════════════════
// 汇总入口
// ════════════════════════════════════════════════════════════════════
int multicast_sink_test()
{
    // sink 原语层
    test_dispatch_complexity();
    test_active_list_stays_compact();
    test_ui_pattern();
    test_mutate_in_dispatch();
    test_activate_in_dispatch();
    test_activate_outside_dispatch();
    test_invalid_handles();
    test_slot_recycle();
    test_bind_in_dispatch();
    test_stress();

    // scoped_binding 层
    test_scoped_binding_basic();
    test_scoped_binding_empty();
    test_scoped_binding_move_construct();
    test_scoped_binding_toggle_then_release();
    test_scoped_binding_release_early();
    test_scoped_binding_multiple();
    test_scoped_binding_move_assign();
    test_scoped_binding_attach_out();

    std::printf("All multicast_sink + scoped_binding tests passed.\n");
    return 0;
}

int main(int argc, char **argv)
try
{
    multicast_sink_test();
    std::setvbuf(stdout, nullptr, _IONBF,
                 0); // 重定向时也立刻看得到输出

    if (argc > 1 && std::string_view{argv[1]} == "--selftest")
        return app::selftest();

    surface window{};
    window.setup({.width = WIDTH, .height = HEIGHT}, TITLE);

    glfw_input_translation input;   // 先声明（后析构）
    auto arena = app::make_arena(); // 后声明（先析构）
    input.attach(arena.input_binding, arena);

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
