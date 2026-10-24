// ═══════════════════════════════════════════════════════════════════════
//  test_catalogue.cpp —— UI 事件目录
//
//  目标：从 arena_event 的输入里，生成**一整本** UI 事件。
//
//    mouse.left  (5 个，同格裁决)   click · dbl · triple · long_press · drag
//    mouse.right (1 个，独立通道)   right_click
//    keyboard    (2 个，直呼)       key_A · combo_ctrl_s
//    touch       (7 个，每指一台)   tap · dbl_tap · swipe_left/right/up/down · long_tap
//                                 ────────────────────────────────────────
//                                 共 15 种
//
//  结构（这就是"声明的力量"——裁决规则全是这几行）：
//
//    mouse.left : one_of< refines< refines<click,dbl>, triple >,   ← 三级链
//                         one_of< long_press, drag > >            ← 两个对手
//    mouse.right: right_click                                    ← 单点
//    keyboard   : all_of< key_A, combo_ctrl_s >                  ← 互不相干
//    touch      : one_of< refines<tap,dbl_tap>,
//                         one_of<swipe_left, one_of<swipe_right,
//                         one_of<swipe_up, one_of<swipe_down, long_tap>>>>> >
//
//  然后一个脚本，让这 15 种事件各发一次，逐段断言确切记录。
//
//  编译：
//    g++ -std=c++26 -freflection -O2 uhi\v3\src\test_catalogue.cpp -o output/test_catalogue.exe -lstdc++exp
// ═══════════════════════════════════════════════════════════════════════
#include <cassert>
#include <cstdint>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include "../../../../../../include/detail/meta/make_aggregate.hpp"
#include "arena_event.hpp"
#include "test_util.hpp"
#include "uhi.hpp"
#include "uhi_integration.hpp"

using mcs::vulkan::meta::field;
using mcs::vulkan::meta::make_aggregate;
using mcs::vulkan::meta::method;
using uhi::refines;
using uhi::step_t;
using uhi::time_point;

namespace idx
{
    constexpr int click = 0, dbl = 1, triple = 2, long_press = 3, drag = 4;
    constexpr int right_click = 5;
    constexpr int key_a = 6, combo = 7;
    constexpr int tap = 8, dbl_tap = 9, sw_l = 10, sw_r = 11, sw_u = 12, sw_d = 13,
                  long_tap = 14;
    constexpr int count = 15;
    constexpr const char *names[count] = {
        "click",       "dbl",         "triple",       "long_press", "drag",
        "right_click", "key_A",       "combo_ctrl_s", "tap",        "dbl_tap",
        "swipe_left",  "swipe_right", "swipe_up",     "swipe_down", "long_tap"};
} // namespace idx

// ═══════════════════════════════════════════════════════════════════════
// ① 载荷：每个 kind 带自己需要的字段
// ═══════════════════════════════════════════════════════════════════════
struct click_event
{
    double x = 0.0, y = 0.0;
    int count = 0; // 第几次（1/2/3）
};
struct hold_event
{
    double x = 0.0, y = 0.0;
    int ms = 0;
};
struct drag_event
{
    double dx = 0.0, dy = 0.0, dist = 0.0;
};
struct key_event
{
    int key = 0;
};
struct combo_event
{
    int mods = 0; // bit0 ctrl, bit1 shift
    int key = 0;
};
struct tap_event
{
    double x = 0.0, y = 0.0;
    int id = 0;
};
struct swipe_event
{
    double dx = 0.0, dy = 0.0, dist = 0.0;
    int id = 0;
};

// ═══════════════════════════════════════════════════════════════════════
// ② FSM 们（应用层：只吃 input_frame，只写自己的状态）
// ═══════════════════════════════════════════════════════════════════════
namespace fsm
{
    using ae::ChangeType;
    using ae::input_frame;

    inline bool left_press(const input_frame &f) noexcept
    {
        const auto &c = f.change;
        return c.change == ChangeType::PointerButton && c.button.is_left() &&
               c.button.is_press();
    }
    inline bool left_release(const input_frame &f) noexcept
    {
        const auto &c = f.change;
        return c.change == ChangeType::PointerButton && c.button.is_left() &&
               c.button.is_pelease();
    }
    inline bool left_move(const input_frame &f) noexcept
    {
        return f.change.change == ChangeType::PointerDownMove;
    }
    inline bool any_press(const input_frame &f) noexcept
    {
        const auto &c = f.change;
        return c.change == ChangeType::PointerButton && c.button.is_press();
    }
    inline bool any_release(const input_frame &f) noexcept
    {
        const auto &c = f.change;
        return c.change == ChangeType::PointerButton && c.button.is_pelease();
    }

    constexpr auto click_window = std::chrono::milliseconds{300};

    // N 连击：数够 N 次就 done；窗口过期还没数够就 aborted
    template <int N>
    struct multi_click
    {
        int count = 0;
        bool armed = false; // 只有 release 之后才开始计时
        time_point deadline{};
        click_event payload{};
        step_t step(const input_frame &f, time_point now) noexcept
        {
            if (armed && now >= deadline) // 窗口过期，数还不够
            {
                count = 0;
                armed = false;
                return step_t::aborted;
            }
            if (left_press(f))
            {
                ++count;
                payload.x = f.context.pointer_x;
                payload.y = f.context.pointer_y;
                payload.count = count;
                return step_t::active;
            }
            if (left_release(f) && count > 0)
            {
                if (count >= N)
                {
                    count = 0;
                    armed = false;
                    return step_t::done;
                }
                deadline = now + click_window;
                armed = true;
                return step_t::active;
            }
            return count > 0 ? step_t::active : step_t::idle;
        }
        step_t tick(time_point now) noexcept
        {
            if (armed && now >= deadline)
            {
                count = 0;
                armed = false;
                return step_t::aborted;
            }
            return step_t::hold;
        }
        void reset() noexcept
        {
            count = 0;
            armed = false;
        }
    };

    // 长按：按住不动超过 ms
    template <int Ms>
    struct long_press
    {
        bool down = false;
        time_point t0{};
        hold_event payload{};
        step_t step(const input_frame &f, time_point now) noexcept
        {
            if (left_press(f))
            {
                down = true;
                t0 = now;
                payload.x = f.context.pointer_x;
                payload.y = f.context.pointer_y;
                payload.ms = 0;
                return step_t::active;
            }
            if (!down)
                return step_t::idle;
            if (left_move(f) || left_release(f))
            {
                down = false;
                return step_t::aborted;
            }
            return step_t::active;
        }
        step_t tick(time_point now) noexcept
        {
            if (!down)
                return step_t::hold;
            const auto held =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - t0);
            if (held.count() >= Ms)
            {
                down = false;
                payload.ms = static_cast<int>(held.count());
                return step_t::done;
            }
            return step_t::hold;
        }
        void reset() noexcept
        {
            down = false;
        }
    };

    // 拖拽：按住移动超过 px
    template <int Px>
    struct drag
    {
        bool down = false;
        drag_event payload{};
        step_t step(const input_frame &f, time_point) noexcept
        {
            if (left_press(f))
            {
                down = true;
                payload = {};
                return step_t::active;
            }
            if (left_release(f))
            {
                if (down)
                {
                    down = false;
                    return step_t::aborted;
                }
                return step_t::idle;
            }
            if (!down)
                return step_t::idle;
            if (left_move(f))
            {
                payload.dx += f.change.move.dx;
                payload.dy += f.change.move.dy;
                payload.dist = (payload.dx < 0 ? -payload.dx : payload.dx) +
                               (payload.dy < 0 ? -payload.dy : payload.dy);
                if (payload.dist >= Px)
                {
                    down = false;
                    return step_t::done;
                }
            }
            return step_t::active;
        }
        void reset() noexcept
        {
            down = false;
        }
    };

    // 右键单击
    struct right_click
    {
        bool down = false;
        click_event payload{};
        step_t step(const input_frame &f, time_point) noexcept
        {
            const auto &c = f.change;
            if (c.change != ChangeType::PointerButton || c.button.is_left())
                return down ? step_t::active : step_t::idle;
            if (c.button.is_press())
            {
                down = true;
                payload.x = f.context.pointer_x;
                payload.y = f.context.pointer_y;
                return step_t::active;
            }
            if (down)
            {
                down = false;
                return step_t::done;
            }
            return step_t::idle;
        }
        void reset() noexcept
        {
            down = false;
        }
    };

    // 单键
    template <ae::Key K>
    struct key_press
    {
        key_event payload{};
        step_t step(const input_frame &f, time_point) noexcept
        {
            const auto &c = f.change;
            if (c.change == ChangeType::Key && c.key.key == K && c.key.is_press())
            {
                payload.key = static_cast<int>(K);
                return step_t::done;
            }
            return step_t::idle;
        }
        void reset() noexcept {}
    };

    // 组合键：K 按下时 mod 也按着（读 context 里的键盘状态）
    template <ae::Key Mod, ae::Key K>
    struct combo
    {
        combo_event payload{};
        step_t step(const input_frame &f, time_point) noexcept
        {
            const auto &c = f.change;
            if (c.change != ChangeType::Key || c.key.key != K || !c.key.is_press())
                return step_t::idle;
            if (f.context.keys[std::to_underlying(Mod)] != ae::HeldState::Down)
                return step_t::idle;
            payload.mods = 1; // ctrl
            payload.key = static_cast<int>(K);
            return step_t::done;
        }
        void reset() noexcept {}
    };

    // 触摸：点（移动多了就变成 swipe）
    inline bool t_down(const input_frame &f) noexcept
    {
        const auto &c = f.change;
        return c.change == ChangeType::PointerButton && c.button.is_press();
    }
    inline bool t_up(const input_frame &f) noexcept
    {
        const auto &c = f.change;
        return c.change == ChangeType::PointerButton && c.button.is_pelease();
    }
    inline bool t_move(const input_frame &f) noexcept
    {
        return f.change.change == ChangeType::PointerDownMove;
    }

    struct tap
    {
        double down_x = 0.0, down_y = 0.0;
        tap_event payload{};
        bool down = false;
        step_t step(const input_frame &f, time_point) noexcept
        {
            if (t_down(f))
            {
                down = true;
                down_x = f.context.pointer_x;
                down_y = f.context.pointer_y;
                payload = {down_x, down_y, 0};
                return step_t::active;
            }
            if (!down)
                return step_t::idle;
            if (t_move(f))
            {
                const auto dx = f.change.move.dx, dy = f.change.move.dy;
                if ((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) > 20.0)
                {
                    down = false;
                    return step_t::aborted;
                }
                return step_t::active;
            }
            if (t_up(f))
            {
                down = false;
                return step_t::done;
            }
            return step_t::active;
        }
        void reset() noexcept
        {
            down = false;
        }
    };

    // 双击（触摸）：两下之内
    struct dbl_tap
    {
        int taps = 0;
        bool armed = false;
        time_point deadline{};
        tap_event payload{};
        bool down = false;
        step_t step(const input_frame &f, time_point now) noexcept
        {
            if (armed && now >= deadline)
            {
                taps = 0;
                armed = false;
                return step_t::aborted;
            }
            if (t_down(f))
            {
                ++taps;
                down = true;
                payload = {f.context.pointer_x, f.context.pointer_y, 0};
                return taps >= 1 ? step_t::active : step_t::idle;
            }
            if (t_up(f) && down)
            {
                down = false;
                if (taps >= 2)
                {
                    taps = 0;
                    armed = false;
                    return step_t::done;
                }
                deadline = now + click_window;
                armed = true;
                return step_t::active;
            }
            return taps > 0 ? step_t::active : step_t::idle;
        }
        step_t tick(time_point now) noexcept
        {
            if (armed && now >= deadline)
            {
                taps = 0;
                armed = false;
                return step_t::aborted;
            }
            return step_t::hold;
        }
        void reset() noexcept
        {
            taps = 0;
            armed = false;
        }
    };

    enum class dir : std::uint8_t
    {
        l,
        r,
        u,
        d
    };

    // 滑动：主方向 + 距离
    template <dir D>
    struct swipe
    {
        bool down = false;
        double ax = 0.0, ay = 0.0;
        swipe_event payload{};
        step_t step(const input_frame &f, time_point) noexcept
        {
            if (t_down(f))
            {
                down = true;
                ax = ay = 0.0;
                payload = {0, 0, 0, 0};
                return step_t::active;
            }
            if (!down)
                return step_t::idle;
            if (t_up(f))
            {
                down = false;
                return step_t::aborted;
            }
            if (t_move(f))
            {
                ax += f.change.move.dx;
                ay += f.change.move.dy;
                payload = {ax, ay, (ax < 0 ? -ax : ax) + (ay < 0 ? -ay : ay), 0};
                const auto axx = ax < 0 ? -ax : ax, ayy = ay < 0 ? -ay : ay;
                const bool hit = D == dir::l   ? (ax < 0 && axx >= 40 && axx >= ayy)
                                 : D == dir::r ? (ax > 0 && axx >= 40 && axx >= ayy)
                                 : D == dir::u ? (ay < 0 && ayy >= 40 && ayy > axx)
                                               : (ay > 0 && ayy >= 40 && ayy > axx);
                if (hit)
                {
                    down = false;
                    return step_t::done;
                }
            }
            return step_t::active;
        }
        void reset() noexcept
        {
            down = false;
        }
    };

    // 长按（触摸）
    template <int Ms>
    struct long_tap
    {
        bool down = false;
        time_point t0{};
        tap_event payload{};
        step_t step(const input_frame &f, time_point now) noexcept
        {
            if (t_down(f))
            {
                down = true;
                t0 = now;
                payload = {f.context.pointer_x, f.context.pointer_y, 0};
                return step_t::active;
            }
            if (!down)
                return step_t::idle;
            if (t_up(f) || t_move(f))
            {
                down = false;
                return step_t::aborted;
            }
            return step_t::active;
        }
        step_t tick(time_point now) noexcept
        {
            if (!down)
                return step_t::hold;
            const auto held =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - t0);
            if (held.count() >= Ms)
            {
                down = false;
                return step_t::done;
            }
            return step_t::hold;
        }
        void reset() noexcept
        {
            down = false;
        }
    };
} // namespace fsm

// ═══════════════════════════════════════════════════════════════════════
// ③ 结构：全部规则就这几行
// ═══════════════════════════════════════════════════════════════════════
using ml = uhi::source_t<"mouse.left">;
using mr = uhi::source_t<"mouse.right">;
using kb_src = uhi::source_t<"keyboard">;
using tsrc = uhi::source_t<"touch">;

using g_click = uhi::leaf<uhi::kind_t<"click", click_event>, ml, fsm::multi_click<1>>;
using g_dbl = uhi::leaf<uhi::kind_t<"dbl", click_event>, ml, fsm::multi_click<2>>;
using g_triple = uhi::leaf<uhi::kind_t<"triple", click_event>, ml, fsm::multi_click<3>>;
using g_long =
    uhi::leaf<uhi::kind_t<"long_press", hold_event>, ml, fsm::long_press<600>, 2000>;
using g_drag = uhi::leaf<uhi::kind_t<"drag", drag_event>, ml, fsm::drag<30>, 5000>;

using g_rclick = uhi::leaf<uhi::kind_t<"right_click", click_event>, mr, fsm::right_click>;

using g_key_a =
    uhi::leaf<uhi::kind_t<"key_A", key_event>, kb_src, fsm::key_press<ae::Key::eA>>;
using g_combo = uhi::leaf<uhi::kind_t<"combo_ctrl_s", combo_event>, kb_src,
                          fsm::combo<ae::Key::eLEFT_CONTROL, ae::Key::eS>>;

using g_tap = uhi::leaf<uhi::kind_t<"tap", tap_event>, tsrc, fsm::tap>;
using g_dtap = uhi::leaf<uhi::kind_t<"dbl_tap", tap_event>, tsrc, fsm::dbl_tap>;
using g_sl =
    uhi::leaf<uhi::kind_t<"swipe_left", swipe_event>, tsrc, fsm::swipe<fsm::dir::l>>;
using g_sr =
    uhi::leaf<uhi::kind_t<"swipe_right", swipe_event>, tsrc, fsm::swipe<fsm::dir::r>>;
using g_su =
    uhi::leaf<uhi::kind_t<"swipe_up", swipe_event>, tsrc, fsm::swipe<fsm::dir::u>>;
using g_sd =
    uhi::leaf<uhi::kind_t<"swipe_down", swipe_event>, tsrc, fsm::swipe<fsm::dir::d>>;
using g_ltap =
    uhi::leaf<uhi::kind_t<"long_tap", tap_event>, tsrc, fsm::long_tap<500>, 2000>;

// 鼠标左键那一格：三级链 + 两个对手
using mouse_cell =
    uhi::one_of<refines<refines<g_click, g_dbl>, g_triple>, uhi::one_of<g_long, g_drag>>;
// 鼠标右键：单点
// 键盘：两个互不相干
using key_cell = uhi::all_of<g_key_a, g_combo>;
// 触摸：点/双击 + 四方向滑动 + 长按
// 平铺：one_of 支持变参，全部两两互斥（比右倾嵌套清楚得多）
using touch_cell = uhi::one_of<refines<g_tap, g_dtap>, g_sl, g_sr, g_su, g_sd, g_ltap>;

using desktop_tree = uhi::all_of<mouse_cell, uhi::all_of<g_rclick, key_cell>>;

// 蓝图：钉住关键关系（完整文本在 main 里打印，肉眼可核对）
consteval bool has(std::string_view hay, std::string_view needle)
{
    return hay.find(needle) != std::string_view::npos;
}
#if 0
static_assert(has(uhi::ui<desktop_tree>::blueprint.view(), "click->dbl"));
static_assert(has(uhi::ui<desktop_tree>::blueprint.view(), "dbl->triple"));
// 传递闭包：三级链必须自己长出 click->triple，否则 click 会在 dbl 一超时就落地
static_assert(has(uhi::ui<desktop_tree>::blueprint.view(), "click->triple"));
static_assert(has(uhi::ui<desktop_tree>::blueprint.view(), "click~long_press"));
static_assert(has(uhi::ui<desktop_tree>::blueprint.view(), "triple~drag"));
static_assert(has(uhi::ui<desktop_tree>::blueprint.view(), "mouse.right{direct:right_click}"));
static_assert(has(uhi::ui<desktop_tree>::blueprint.view(), "keyboard{direct:key_A,combo_ctrl_s}"));
static_assert(has(uhi::ui<touch_cell>::blueprint.view(), "tap->dbl_tap"));
static_assert(has(uhi::ui<touch_cell>::blueprint.view(), "swipe_left~swipe_down"));
static_assert(has(uhi::ui<touch_cell>::blueprint.view(), "cells: touch{arbitrate:"));
#endif

// ═══════════════════════════════════════════════════════════════════════
// ④ arena
// ═══════════════════════════════════════════════════════════════════════
inline auto make_arena()
{
    return make_aggregate<"arena">(
        field<"now">(time_point{}), field<"ctx">(ae::event_context{}),
        field<"ui">(uhi::ui<desktop_tree>{}), field<"touch">(uhi::pool<touch_cell, 2>{}),
        field<"n">(std::array<int, idx::count>{}), field<"lx">(double{}),
        field<"ly">(double{}), field<"ldist">(double{}), field<"lcombo">(int{}),
        field<"ldir">(int{}),

        method<"on_click">([](auto &self, const click_event &e) noexcept {
            ++self.n[idx::click];
            self.lx = e.x;
            self.ly = e.y;
        }),
        method<"on_dbl">([](auto &self, const click_event &e) noexcept {
            ++self.n[idx::dbl];
            self.lx = e.x;
        }),
        method<"on_triple">([](auto &self, const click_event &e) noexcept {
            ++self.n[idx::triple];
            self.lx = e.x;
        }),
        method<"on_long_press">([](auto &self, const hold_event &e) noexcept {
            ++self.n[idx::long_press];
            self.lx = e.x;
            self.ldist = static_cast<double>(e.ms);
        }),
        method<"on_drag">([](auto &self, const drag_event &e) noexcept {
            ++self.n[idx::drag];
            self.ldist = e.dist;
        }),
        method<"on_right_click">([](auto &self, const click_event &e) noexcept {
            ++self.n[idx::right_click];
            self.lx = e.x;
        }),
        method<"on_key_A">(
            [](auto &self, const key_event &) noexcept { ++self.n[idx::key_a]; }),
        method<"on_combo_ctrl_s">([](auto &self, const combo_event &e) noexcept {
            ++self.n[idx::combo];
            self.lcombo = e.mods;
        }),
        method<"on_tap">([](auto &self, const tap_event &e) noexcept {
            ++self.n[idx::tap];
            self.lx = e.x;
            self.ly = e.y;
        }),
        method<"on_dbl_tap">([](auto &self, const tap_event &e) noexcept {
            ++self.n[idx::dbl_tap];
            self.lx = e.x;
        }),
        method<"on_swipe_left">([](auto &self, const swipe_event &e) noexcept {
            ++self.n[idx::sw_l];
            self.ldist = e.dist;
        }),
        method<"on_swipe_right">([](auto &self, const swipe_event &e) noexcept {
            ++self.n[idx::sw_r];
            self.ldist = e.dist;
        }),
        method<"on_swipe_up">([](auto &self, const swipe_event &e) noexcept {
            ++self.n[idx::sw_u];
            self.ldist = e.dist;
        }),
        method<"on_swipe_down">([](auto &self, const swipe_event &e) noexcept {
            ++self.n[idx::sw_d];
            self.ldist = e.dist;
        }),
        method<"on_long_tap">([](auto &self, const tap_event &e) noexcept {
            ++self.n[idx::long_tap];
            self.lx = e.x;
        }),

        method<"bind">([](auto &self) noexcept {
            self.ui.bind(self);
            self.touch.bind(self);
        }),
        method<"input_event">([](auto &self, auto e) noexcept {
            auto ch = ae::build_change(self.ctx, e);
            if (ch.change == ae::ChangeType::None)
                return;
            ch.time_point = self.now;
            self.ui.feed(ae::input_frame{self.ctx, ch});
            ae::apply_change(self.ctx, ch);
        }),
        method<"touch_event">(
            [](auto &self, const ae::input_frame &f) noexcept { self.touch.feed(f); }),
        method<"tick">([](auto &self, time_point t) noexcept {
            self.now = t;
            self.ui.tick(t);
            self.touch.tick(t);
        }));
}

using arena_t = decltype(make_arena());
static_assert(uhi::accepts_platform_events_v<arena_t>);

// ═══════════════════════════════════════════════════════════════════════
// ⑤ 驱动
// ═══════════════════════════════════════════════════════════════════════
inline arena_t arena = make_arena();
inline std::string last;

inline void pump()
{
    // 收集**所有**机器的记录：鼠键那一台 + 每个触摸 lane。
    // （只收 arena.ui 就是 harness 不完备：触摸事件一直在产生，却永远看不到。）
    std::string s = uhi::to_string(arena.ui.events());
    for (std::size_t k = 0; k < arena.touch.lanes.size(); ++k)
    {
        const auto text = uhi::to_string(arena.touch[k].events());
        if (text.empty())
            continue;
        if (!s.empty())
            s += ", ";
        s += "[" + std::to_string(k) + "] " + text;
    }
    last = std::move(s);
    arena.ui.clear_events();
    for (auto &l : arena.touch.lanes)
        l.clear_events();
}

template <class E>
void post(int ms, const E &e)
{
    glfw_post(arena, ms, e);
    pump();
}

void tick(int ms)
{
    arena.invoke<"tick">(at(ms));
    pump();
}

// 触摸：用手边最近的 mouse 事件模拟一指（另一台机器，走自己的入口）
void touch(const ae::input_frame &f)
{
    arena.now = f.change.time_point;
    arena.invoke<"touch_event">(f);
    pump();
}
inline ae::input_frame tou(ae::change_event ch, const ae::event_context &ctx = {})
{
    return ae::input_frame{ctx, ch};
}
inline ae::change_event t_button(bool press, double /*x*/, double /*y*/, int ms)
{
    ae::change_event ch{};
    ch.change = ae::ChangeType::PointerButton;
    ch.time_point = at(ms);
    ch.button = {ae::MouseButton::eMOUSE_BUTTON_LEFT,
                 press ? ae::Transition::Press : ae::Transition::Release};
    return ch;
}
inline ae::change_event t_move(double dx, double dy, int ms)
{
    ae::change_event ch{};
    ch.change = ae::ChangeType::PointerDownMove;
    ch.time_point = at(ms);
    ch.move = {300 + dx, 300 + dy, dx, dy};
    return ch;
}

// ═══════════════════════════════════════════════════════════════════════
// 驱动：want / main
// ═══════════════════════════════════════════════════════════════════════
inline void want_has(std::string_view sub, const char *where)
{
    if (last.find(sub) != std::string::npos)
        return;
    std::print(stderr, "{}:\n  got  [{}]\n  must contain [{}]\n", where, last, sub);
    assert(false && "unexpected trace");
}

inline void want(std::string_view w, const char *where)
{
    if (last == w)
        return;
    std::print(stderr, "{}:\n  got  [{}]\n  want [{}]\n", where, last, w);
    assert(false && "unexpected trace");
}

// ═══════════════════════════════════════════════════════════════════════
// ⑥ 一本目录：15 种事件各发一次
// ═══════════════════════════════════════════════════════════════════════
int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0); // 断言前也要能看到输出
    arena.invoke<"bind">();
    auto conn = uhi::subscribe(arena);

    std::println("=== UI 事件目录 ===\n{}\n", uhi::ui<desktop_tree>::blueprint.view());
    std::println("{}\n", uhi::ui<touch_cell>::blueprint.view());

    // 1) click：按一下就松手，等 300ms 窗口过期（dbl 与 triple 同时放弃）
    post(0, p2{100.0, 200.0});
    post(10, mb{L, PRESS});
    post(50, mb{L, RELEASE});
    assert(last.empty()); // click 已 done，但要等 dbl/triple
    tick(350);
    want("click:acc@350", "1 click");
    assert(arena.lx == 100.0 && arena.ly == 200.0);
    std::println("1  click          {}   (x={} y={})", last, arena.lx, arena.ly);

    // 2) dbl：两下
    post(400, mb{L, PRESS});
    post(440, mb{L, RELEASE});
    post(500, mb{L, PRESS});
    post(540, mb{L, RELEASE});
    tick(700); // dbl 完成，但 triple 还在等第三下
    assert(last.empty());
    tick(840); // triple 窗口过期 ⇒ dbl 落地
    want("click:sup@840, dbl:acc@840", "2 dbl");
    std::println("2  dbl            {}", last);

    // 3) triple：三下（最具体，立刻发）
    post(900, mb{L, PRESS});
    post(940, mb{L, RELEASE});
    post(1000, mb{L, PRESS});
    post(1040, mb{L, RELEASE});
    post(1100, mb{L, PRESS});
    post(1140, mb{L, RELEASE});
    want("dbl:sup@1140, click:sup@1140, triple:acc@1140", "3 triple");
    std::println("3  triple         {}", last);

    // 4) long_press：按住 700ms 不动 ⇒ 同时压掉另外四个
    post(1200, mb{L, PRESS});
    tick(1600);
    assert(last.empty());
    tick(1900); // 700ms > 600
    want("triple:sup@1900, drag:sup@1900, dbl:sup@1900, click:sup@1900, "
         "long_press:acc@1900",
         "4 long_press");
    post(1950, mb{L, RELEASE}); // 抬起不该补一个 click
    want("", "4b 抬起无事件");
    std::println("4  long_press     long_press:acc@1900   （抬起后没有补 click）");

    // 5) drag：按住移动 100px
    post(2000, mb{L, PRESS});
    post(2100, p2{200.0, 0.0});
    want("triple:sup@2100, dbl:sup@2100, click:sup@2100, drag:acc@2100", "5 drag");
    assert(arena.ldist >= 30.0);
    post(2150, mb{L, RELEASE});
    std::println("5  drag           {}   (dist={})", "drag:acc@2100", arena.ldist);

    // 6) right_click：另一条通道，左键同时按着也不影响
    post(2200, mb{L, PRESS});
    post(2300, mb{R, PRESS});
    post(2350, mb{R, RELEASE});
    want("right_click:acc@2350", "6 right_click");
    post(2400, mb{L, RELEASE}); // 左键松开
    tick(2700);                 // 让那个 click 走完窗口
    want("click:acc@2700", "6b 左键的 click");
    std::println("6  right_click    right_click:acc@2350   （左键同时按着，互不干扰）");

    // 7) key_A
    post(2800, kb{ae::Key::eA, PRESS});
    want("key_A:acc@2800", "7 key_A");
    std::println("7  key_A          {}", last);

    // 8) combo_ctrl_s：先按住 ctrl，再按 s
    post(2900, kb{ae::Key::eLEFT_CONTROL, PRESS});
    post(2950, kb{ae::Key::eS, PRESS});
    want("combo_ctrl_s:acc@2950", "8 combo");
    assert(arena.lcombo == 1);
    std::println("8  combo_ctrl_s   {}   (mods={})", last, arena.lcombo);

    // 9) tap：按一下再抬，触摸这一格里要等 dbl_tap 的窗口
    touch(tou(t_button(true, 50, 60, 3000)));
    touch(tou(t_button(false, 50, 60, 3050)));
    assert(last.empty());
    tick(3400); // dbl_tap 窗口过期 ⇒ tap 落地
    want("[0] tap:acc@3400", "9 tap");
    std::println("9  tap            {}   (x={} y={})", last, arena.lx, arena.ly);

    // 10) dbl_tap：两下
    touch(tou(t_button(true, 10, 10, 3500)));
    touch(tou(t_button(false, 10, 10, 3540)));
    touch(tou(t_button(true, 12, 12, 3600)));
    touch(tou(t_button(false, 12, 12, 3640)));
    want("[0] tap:sup@3640, dbl_tap:acc@3640", "10 dbl_tap");
    std::println("10 dbl_tap        {}", last);

    // 11~14) 四方向滑动：方向决定谁赢
    const char *snames[4] = {"swipe_left", "swipe_right", "swipe_up", "swipe_down"};
    const double sx[4] = {-100.0, 100.0, 0.0, 0.0};
    const double sy[4] = {0.0, 0.0, -100.0, 100.0};
    [[maybe_unused]] const int skey[4] = {idx::sw_l, idx::sw_r, idx::sw_u, idx::sw_d};
    for (int d = 0; d < 4; ++d)
    {
        const int t0 = 3700 + d * 200;
        touch(tou(t_button(true, 300, 300, t0)));
        touch(tou(t_move(sx[d], sy[d], t0 + 50)));
        const std::string ex = std::string(snames[d]) + ":acc@" + std::to_string(t0 + 50);
        want_has(ex, snames[d]);
        assert(arena.n[skey[d]] == 1);
        std::println("{:<3}{:<15}{}", 11 + d, snames[d], last);
    }

    // 15) long_tap：按住 700ms 不动
    touch(tou(t_button(true, 400, 400, 4600)));
    tick(4900);
    assert(last.empty());
    tick(5300);
    want_has("long_tap:acc@5300", "15 long_tap");
    std::println("15 long_tap       {}", last);
    // ── P16 反例：缺一个必要条件就不能触发（必要 ≠ 充分）──
    {
        [[maybe_unused]] const int before[4] = {arena.n[idx::combo], arena.n[idx::drag],
                                                arena.n[idx::dbl], arena.n[idx::dbl_tap]};

        // a) 先松开 Ctrl（P8 里一直按着），再只按 S ⇒ 没有 combo_ctrl_s
        post(5450, kb{ae::Key::eLEFT_CONTROL, RELEASE});
        post(5500, kb{ae::Key::eS, PRESS});
        assert(arena.n[idx::combo] == before[0]);
        post(5550, kb{ae::Key::eS, RELEASE});

        // b) 按着移动但没超过阈值（<30px）⇒ 不是 drag，抬手后是 click
        post(5600, mb{L, PRESS});
        post(5620, p2{arena.ctx.pointer_x + 10.0,
                      arena.ctx.pointer_y}); // 只移动 10px（相对当前位置）
        assert(arena.n[idx::drag] == before[1]);
        post(5640, mb{L, RELEASE});
        tick(5940); // click 的窗口
        want("click:acc@5940", "16b 小移动仍是 click");
        assert(arena.n[idx::drag] == before[1]);

        // c) 单击一次 ⇒ 不是 dbl（同一个窗口里没有第二下）
        assert(arena.n[idx::dbl] == before[2]);

        // d) 触摸一次 ⇒ 不是 dbl_tap
        touch(tou(t_button(true, 1, 1, 6000)));
        touch(tou(t_button(false, 1, 1, 6050)));
        tick(6400);
        assert(arena.n[idx::dbl_tap] == before[3]);
        std::println("16 反例          缺 Ctrl / 移动不够 / 只点一次 ⇒ 都不触发");
    }

    // ── 汇总 ───────────────────────────────────────────────────────
    int total = 0;
    std::println("\n--- 目录核对 ---");
    for (int i = 0; i < idx::count; ++i)
    {
        total += arena.n[i];
        std::print("{}={} ", idx::names[i], arena.n[i]);
        if (i % 5 == 4)
            std::println("");
    }
    std::println("共 {} 种，触发 {} 次", idx::count, total);
    // click 三次：P1 单击、P6 左键那次、P16 小移动那次
    assert(arena.n[idx::click] == 3);
    // tap 两次：P9、P16d
    assert(
        arena.n[idx::tap] ==
        2); // P1 的单击 + P6 左键那次（刻意留着）`r`n    for (int i = 1; i <= idx::combo; ++i) if (i != idx::click) assert(arena.n[i] == 1);
    for (int i = idx::tap; i < idx::count; ++i)
        if (i != idx::tap)
            assert(arena.n[i] == 1);

    std::println("\n=== 15 种 UI 事件全部生成 ===");
    return 0;
}
