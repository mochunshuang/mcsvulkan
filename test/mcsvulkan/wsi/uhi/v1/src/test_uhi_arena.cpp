// ═══════════════════════════════════════════════════════════════════════
//  test_uhi_arena.cpp —— 直接集成 arena_event
//
//  和 test_uhi.cpp 的差别只有一个：帧是真的 arena_event::input_frame，
//  由 build_change / apply_change 产出；ui 是 arena 的**字段**，所以 bind 时
//  self 就是 arena 本身，不需要外部变量。
//
//    auto arena = make_arena<tree>();
//    arena.bind();                        // ui.bind(self)
//    arena.input_event(e, t);             // 真事件进来
//    assert(arena.clicks == 1);
//
//  编译：
//    g++ -std=c++26 -freflection -O0 -g uhi\v1\src\test_uhi_arena.cpp -o output/test_uhi_arena.exe -lstdc++exp
// ═══════════════════════════════════════════════════════════════════════
#include <cassert>
#include <chrono>
#include <cstdint>
#include <print>
#include <string>
#include <string_view>

#include "uhi.hpp"

#include "../../../../../../include/detail/event/event_type.hpp"
#include "../../../../../../include/detail/meta/make_aggregate.hpp"

using namespace std::chrono_literals;
using mcs::vulkan::meta::field;
using mcs::vulkan::meta::make_aggregate;
using mcs::vulkan::meta::method;
using uhi::kind_t;
using uhi::leaf;
using uhi::refines;
using uhi::source_t;
using uhi::step_t;
using uhi::time_point;

// ═══════════════════════════════════════════════════════════════════════
// ① arena_event —— 从 test_glfw12 原样搬来的输入层（只裁掉没用到的事件）
// ═══════════════════════════════════════════════════════════════════════
// clang-format off
namespace arena_event
{
    using Key = mcs::vulkan::event::Key;

    enum class MouseButton : std::uint8_t
    { eUNDEFINED = 0xFF, eMOUSE_BUTTON_1 = 0, eMOUSE_BUTTON_LEFT = 0,
      eMOUSE_BUTTON_RIGHT = 1, eMOUSE_BUTTON_MIDDLE = 2, eSIZE = 3 };

    enum class HeldState : std::uint8_t { Up, Down };
    enum class Transition : std::uint8_t { Press, Release };

    struct event_context
    {
        std::array<HeldState, std::to_underlying(Key::eSIZE)> keys{};
        std::array<HeldState, static_cast<std::size_t>(MouseButton::eSIZE)> buttons{};
        double pointer_x = 0.0, pointer_y = 0.0;
        bool   pointer_position_valid = false;

        [[nodiscard]] bool any_button_down() const noexcept
        {
            for (auto s : buttons) if (s == HeldState::Down) return true;
            return false;
        }
    };

    enum class ChangeType : std::uint8_t { None, Key, PointerButton, PointerDownMove, PointerUpMove, CursorEnter };

    struct key_change
    {
        Key key{}; Transition transition{};
        [[nodiscard]] bool is_press() const noexcept { return transition == Transition::Press; }
    };
    struct button_change
    {
        MouseButton button{}; Transition transition{};
        [[nodiscard]] bool is_press() const noexcept { return transition == Transition::Press; }
        [[nodiscard]] bool is_pelease() const noexcept { return transition == Transition::Release; }
        [[nodiscard]] bool is_left() const noexcept { return button == MouseButton::eMOUSE_BUTTON_LEFT; }
    };
    struct move_change { double x = 0, y = 0, dx = 0, dy = 0; };

    struct change_event
    {
        ChangeType change = ChangeType::None;
        time_point time_point{};
        key_change key{};
        button_change button{};
        move_change move{};
    };

    struct input_frame { event_context context; change_event change; };

    inline MouseButton map_button(mcs::vulkan::event::MouseButtons b) noexcept
    {
        switch (b)
        {
        case mcs::vulkan::event::MouseButtons::eMOUSE_BUTTON_LEFT:   return MouseButton::eMOUSE_BUTTON_LEFT;
        case mcs::vulkan::event::MouseButtons::eMOUSE_BUTTON_RIGHT:  return MouseButton::eMOUSE_BUTTON_RIGHT;
        case mcs::vulkan::event::MouseButtons::eMOUSE_BUTTON_MIDDLE: return MouseButton::eMOUSE_BUTTON_MIDDLE;
        default: return MouseButton::eUNDEFINED;
        }
    }

    template <class Event>
    auto build_change(const event_context &ctx, const Event &e) noexcept -> change_event
    {
        using T = std::remove_cvref_t<Event>;
        change_event ch{};
        ch.time_point = uhi::clock::now();
        if constexpr (std::is_same_v<T, mcs::vulkan::event::keyboard_event>)
        {
            ch.change = ChangeType::Key;
            ch.key    = {e.key, (e.press() || e.repeat()) ? Transition::Press : Transition::Release};
        }
        else if constexpr (std::is_same_v<T, mcs::vulkan::event::mousebutton_event>)
        {
            ch.change = ChangeType::PointerButton;
            ch.button = {map_button(e.button), (e.press() || e.repeat()) ? Transition::Press : Transition::Release};
        }
        else if constexpr (std::is_same_v<T, mcs::vulkan::event::position2d_event>)
        {
            ch.change = ctx.any_button_down() ? ChangeType::PointerDownMove : ChangeType::PointerUpMove;
            ch.move   = {e.xpos, e.ypos, 0.0, 0.0};
            if (ctx.pointer_position_valid)
            {
                ch.move.dx = e.xpos - ctx.pointer_x;
                ch.move.dy = e.ypos - ctx.pointer_y;
            }
        }
        return ch;
    }

    inline void apply_change(event_context &ctx, const change_event &ch) noexcept
    {
        switch (ch.change)
        {
        case ChangeType::Key:
            ctx.keys[std::to_underlying(ch.key.key)] =
                ch.key.transition == Transition::Press ? HeldState::Down : HeldState::Up;
            break;
        case ChangeType::PointerButton:
            ctx.buttons[std::to_underlying(ch.button.button)] =
                ch.button.transition == Transition::Press ? HeldState::Down : HeldState::Up;
            break;
        case ChangeType::PointerDownMove:
        case ChangeType::PointerUpMove:
            ctx.pointer_x = ch.move.x; ctx.pointer_y = ch.move.y;
            ctx.pointer_position_valid = true;
            break;
        default: break;
        }
    }
}
// clang-format on

// 库唯一需要知道的事：这一帧发生在什么时候
template <>
struct uhi::frame_traits<arena_event::input_frame>
{
    static uhi::time_point when(const arena_event::input_frame &f) noexcept
    {
        return f.change.time_point;
    }
};

namespace ae = arena_event;

// ═══════════════════════════════════════════════════════════════════════
// ② 手势（应用层）：只吃 input_frame，只写自己的状态
// ═══════════════════════════════════════════════════════════════════════
struct click_event
{
    double x = 0.0;
};

struct click_fsm
{
    bool        s = false;
    click_event payload{};
    step_t      step(const ae::input_frame &f, time_point) noexcept
    {
        const auto &c = f.change;
        if (c.change != ae::ChangeType::PointerButton || !c.button.is_left())
            return s ? step_t::active : step_t::idle;
        if (!s && c.button.is_press())
        {
            s         = true;
            payload.x = f.context.pointer_x; // 按下时的位置
            return step_t::active;
        }
        if (s && c.button.is_pelease())
        {
            s = false;
            return step_t::done;
        }
        return s ? step_t::active : step_t::idle;
    }
    void reset() noexcept { s = false; }
};

struct dbl_fsm
{
    enum class st : std::uint8_t { idle, first_press, wait_second, second_press };
    st         s = st::idle;
    time_point deadline{};
    static constexpr auto window = 300ms;

    step_t step(const ae::input_frame &f, time_point now) noexcept
    {
        if (s == st::wait_second && now >= deadline) { s = st::idle; return step_t::aborted; }
        const auto &c = f.change;
        if (c.change != ae::ChangeType::PointerButton || !c.button.is_left())
            return s == st::idle ? step_t::idle : step_t::active;
        switch (s)
        {
        case st::idle:
            if (c.button.is_press()) { s = st::first_press; return step_t::active; }
            return step_t::idle;
        case st::first_press:
            if (c.button.is_pelease()) { s = st::wait_second; deadline = now + window; }
            return step_t::active;
        case st::wait_second:
            if (c.button.is_press()) s = st::second_press;
            return step_t::active;
        case st::second_press:
            if (c.button.is_pelease()) { s = st::idle; return step_t::done; }
            return step_t::active;
        }
        return step_t::idle;
    }
    void reset() noexcept { s = st::idle; }
};

struct drag_fsm
{
    bool   armed = false;
    double acc = 0.0;
    step_t step(const ae::input_frame &f, time_point) noexcept
    {
        const auto &c = f.change;
        if (c.change == ae::ChangeType::PointerButton && c.button.is_left())
        {
            if (c.button.is_press()) { armed = true; acc = 0.0; return step_t::active; }
            if (c.button.is_pelease()) { armed = false; return step_t::aborted; }
        }
        if (!armed) return step_t::idle;
        if (c.change == ae::ChangeType::PointerDownMove)
        {
            acc += (c.move.dx < 0 ? -c.move.dx : c.move.dx) +
                   (c.move.dy < 0 ? -c.move.dy : c.move.dy);
            return acc > 12.0 ? step_t::done : step_t::active;
        }
        return step_t::active;
    }
    void reset() noexcept { armed = false; acc = 0.0; }
};

struct key_fsm
{
    step_t step(const ae::input_frame &f, time_point) noexcept
    {
        const auto &c = f.change;
        if (c.change == ae::ChangeType::Key && c.key.key == ae::Key::eA && c.key.is_press())
            return step_t::done;
        return step_t::idle;
    }
    void reset() noexcept {}
};

// ═══════════════════════════════════════════════════════════════════════
// ③ 结构
// ═══════════════════════════════════════════════════════════════════════
using mouse_src = source_t<"mouse.left">;
using key_src   = source_t<"keyboard">;

using g_click = leaf<kind_t<"click", click_event>, mouse_src, click_fsm>;
using g_dbl   = leaf<kind_t<"dbl">, mouse_src, dbl_fsm>;
using g_drag  = leaf<kind_t<"drag">, mouse_src, drag_fsm>;
using g_key   = leaf<kind_t<"key_A">, key_src, key_fsm>;

using tree_click = g_click;
using tree_refine = refines<g_click, g_dbl>;
using tree_excl   = uhi::one_of<g_click, g_drag>;
using tree_mixed  = uhi::all_of<uhi::one_of<refines<g_click, g_dbl>, g_drag>, g_key>;

// ═══════════════════════════════════════════════════════════════════════
// ④ 蓝图自检
// ═══════════════════════════════════════════════════════════════════════
static_assert(uhi::ui<tree_mixed>::blueprint.view() ==
              "relations: click->dbl click~drag dbl~drag\n"
              "cells: mouse.left{arbitrate:dbl,drag,click} keyboard{direct:key_A}");
static_assert(uhi::ui<tree_click>::blueprint.view() ==
              "relations: (none)\ncells: mouse.left{direct:click}");

// feed 的类型裁剪：认识的帧才进得来 —— 这就是"丢弃不能投递的类型"
static_assert(uhi::frame_like<ae::input_frame>);
static_assert(!uhi::frame_like<int>);
static_assert(!uhi::frame_like<mcs::vulkan::event::keyboard_event>);

// ═══════════════════════════════════════════════════════════════════════
// ⑤ arena：ui 是它的字段，于是 bind 时 self 就是 arena
// ═══════════════════════════════════════════════════════════════════════
template <class Tree>
inline auto make_arena()
{
    return make_aggregate<"arena">(
        field<"ctx">(ae::event_context{}),
        field<"ui">(uhi::ui<Tree>{}),
        field<"clicks">(int{}),
        field<"dbls">(int{}),
        field<"drags">(int{}),
        field<"keys">(int{}),
        field<"click_x">(double{}),

        // 带载荷的 kind：handler 收 const click_event&
        method<"on_click">([](auto &self, const click_event &e) noexcept {
            ++self.clicks;
            self.click_x = e.x;
        }),
        // 不带载荷的 kind：handler 只收 self
        method<"on_dbl">([](auto &self) noexcept { ++self.dbls; }),
        method<"on_drag">([](auto &self) noexcept { ++self.drags; }),
        method<"on_key_A">([](auto &self) noexcept { ++self.keys; }),

        // 绑定：ui 是成员，self 就是 arena
        method<"bind">([](auto &self) noexcept { self.ui.bind(self); }),

        // 真事件进来：build_change → 喂 ui → apply_change
        method<"input_event">([](auto &self, auto e, time_point t) noexcept {
            auto ch = ae::build_change(self.ctx, e);
            if (ch.change == ae::ChangeType::None) return;
            ch.time_point = t;
            self.ui.feed(ae::input_frame{self.ctx, ch});
            ae::apply_change(self.ctx, ch);
        }));
}

inline time_point at(int ms) { return time_point{} + std::chrono::milliseconds{ms}; }

// 平台事件 → arena 的 input_event（这是 arena 的入口，不是 uhi 的）
template <class Arena, class Event>
void send(Arena &a, const Event &e, int ms)
{
    a.template invoke<"input_event">(e, at(ms));
}

inline void expect(std::string_view got, std::string_view want, const char *where)
{
    if (got == want) return;
    std::print(stderr, "{}:\n  got  [{}]\n  want [{}]\n", where, got, want);
    assert(false && "unexpected trace");
}

// 便捷：按键 / 鼠标
constexpr auto L = mcs::vulkan::event::MouseButtons::eMOUSE_BUTTON_LEFT;
using kb = mcs::vulkan::event::keyboard_event;
using mb = mcs::vulkan::event::mousebutton_event;
using p2 = mcs::vulkan::event::position2d_event;
constexpr auto PRESS = mcs::vulkan::event::Action::ePRESS;
constexpr auto RELEASE = mcs::vulkan::event::Action::eRELEASE;

// ═══════════════════════════════════════════════════════════════════════
// ⑥ 场景
// ═══════════════════════════════════════════════════════════════════════

// S1：真 input_frame，一个 click。位置从 context 传到 payload 再传到 arena。
void s1_click()
{
    auto arena = make_arena<tree_click>();
    arena.invoke<"bind">();
    send(arena, p2{42.0, 0.0}, 0); // 先移动，让 position_valid
    send(arena, mb{L, PRESS}, 10);
    send(arena, mb{L, RELEASE}, 60);

    auto s = uhi::to_string(arena.ui.events());
    expect(s, "click:acc@60", "S1");
    assert(arena.clicks == 1 && arena.click_x == 42.0);
    std::println("S1 click          {}   → clicks={} click_x={}", s, arena.clicks, arena.click_x);
}

// S2：细化。click 让路给 dbl，300ms 后才落地。
void s2_refine()
{
    auto arena = make_arena<tree_refine>();
    arena.invoke<"bind">();
    send(arena, p2{0.0, 0.0}, 0);
    send(arena, mb{L, PRESS}, 10);
    send(arena, mb{L, RELEASE}, 60);
    assert(arena.ui.events().empty()); // 还在等 dbl
    send(arena, p2{0.0, 0.0}, 359);    // 只是时间推进（位置不变）
    assert(arena.ui.events().empty());
    send(arena, p2{0.0, 0.0}, 360);    // dbl 窗口过期 → click 落地
    auto s = uhi::to_string(arena.ui.events());
    expect(s, "click:acc@360", "S2");
    assert(arena.clicks == 1);
    std::println("S2 refine         {}   → clicks={}", s, arena.clicks);
}

// S3：互斥。拖拽一赢，click 让位。
void s3_drag()
{
    auto arena = make_arena<tree_excl>();
    arena.invoke<"bind">();
    send(arena, p2{0.0, 0.0}, 0);
    send(arena, mb{L, PRESS}, 10);
    send(arena, p2{100.0, 0.0}, 70); // dx = 100 → drag 赢
    auto s = uhi::to_string(arena.ui.events());
    expect(s, "click:sup@70, drag:acc@70", "S3");
    assert(arena.clicks == 0 && arena.drags == 1);
    std::println("S3 drag           {}   → drags={} clicks={}", s, arena.drags, arena.clicks);
}

// S4：混合。键盘立刻发，鼠标那一格还在等 dbl。
void s4_mixed()
{
    auto arena = make_arena<tree_mixed>();
    arena.invoke<"bind">();
    send(arena, p2{0.0, 0.0}, 0);
    send(arena, mb{L, PRESS}, 10);
    send(arena, kb{ae::Key::eA, PRESS}, 20); // 另一个 cell ⇒ 立刻
    auto s = uhi::to_string(arena.ui.events());
    expect(s, "key_A:acc@20", "S4a");
    assert(arena.keys == 1 && arena.clicks == 0);

    send(arena, mb{L, RELEASE}, 60);
    send(arena, p2{0.0, 0.0}, 360); // dbl 过期 → click
    s = uhi::to_string(arena.ui.events());
    expect(s, "key_A:acc@20, click:acc@360", "S4b");
    assert(arena.clicks == 1 && arena.keys == 1);
    std::println("S4 mixed          {}   → clicks={} keys={}", s, arena.clicks, arena.keys);
}

// ═══════════════════════════════════════════════════════════════════════
int main()
{
    std::println("=== arena_event 集成（ui 是 arena 的字段）===");
    std::println("{}\n", uhi::describe<tree_mixed>().view());

    s1_click();
    s2_refine();
    s3_drag();
    s4_mixed();
    std::println("=== 四个集成场景全部通过 ===");
    return 0;
}
