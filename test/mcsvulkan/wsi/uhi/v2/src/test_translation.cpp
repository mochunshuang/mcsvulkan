// ═══════════════════════════════════════════════════════════════════════
//  test_translation.cpp —— 不需要人手：把 GLFW **可能送来的每一个值**都跑一遍
//
//  GLFW 没有事件注入 API，但 GLFW 能送来的东西是**有限且已知**的：
//    Key     0 .. eSIZE-1  × Action{eUNDEFINED, eRELEASE, ePRESS, eREPEAT}
//    Buttons 0 .. eSIZE-1  × 同样四种 Action
//    position2d：{有/无历史位置} × {有/无按键按下}
//    其余事件类型 ⇒ ChangeType::None
//  所以无需窗口、无需人手，就能穷举 arena_event 的**翻译层**。
//
//  这一层正是"GLFW 回调 → arena_event"的接缝，也是唯一没法靠
//  glfw_post 覆盖的地方。
//
//  编译：
//    g++ -std=c++26 -freflection -O0 -g uhi\v2\src\test_translation.cpp -o output/test_translation.exe -lstdc++exp
// ═══════════════════════════════════════════════════════════════════════
#include <cstdint>
#include <print>

#include "arena_event.hpp"

namespace event = mcs::vulkan::event;

static int cases = 0;
static int bad   = 0;

inline void check(bool ok, const char *what)
{
    ++cases;
    if (ok) return;
    ++bad;
    std::print(stderr, "FAIL: {}\n", what);
}

constexpr auto A_UNDEF = event::Action::eUNDEFINED;
constexpr auto A_REL   = event::Action::eRELEASE;
constexpr auto A_PRESS = event::Action::ePRESS;
constexpr auto A_REP   = event::Action::eREPEAT;

inline bool expect_press(event::Action a) noexcept { return a == A_PRESS || a == A_REP; }

// ═══════════════════════════════════════════════════════════════════════
// ① 键盘：每个 Key × 每个 Action
//      · change 是 Key
//      · key 原样透传
//      · press/repeat ⇒ Press，release/undefined ⇒ Release
//      · apply 之后 context 的按住状态跟着变
// ═══════════════════════════════════════════════════════════════════════
void keyboard_matrix()
{
    const int n = std::to_underlying(event::Key::eSIZE);
    for (int k = 0; k < n; ++k)
    {
        for (auto a : {A_UNDEF, A_REL, A_PRESS, A_REP})
        {
            ae::event_context ctx{};
            const auto       e  = event::keyboard_event{static_cast<event::Key>(k), a};
            const auto       ch = ae::build_change(ctx, e);

            check(ch.change == ae::ChangeType::Key, "keyboard: change==Key");
            check(ch.key.key == static_cast<event::Key>(k), "keyboard: key 透传");
            check(ch.key.is_press() == expect_press(a), "keyboard: press/repeat 归一");

            auto c2 = ctx;
            ae::apply_change(c2, ch);
            check(c2.keys[k] == (expect_press(a) ? ae::HeldState::Down : ae::HeldState::Up),
                  "keyboard: 按住状态");
        }
    }
    std::println("键盘矩阵 : {} keys × 4 actions", n);
}

// ═══════════════════════════════════════════════════════════════════════
// ② 鼠标键：每个 Button × 每个 Action
//      · 左/右/中 映射到 arena_event 的槽位
//      · 其余（侧键 4..8 等）⇒ eUNDEFINED，apply 必须**丢弃**而不是越界写
// ═══════════════════════════════════════════════════════════════════════
void mouse_matrix()
{
    const int n = std::to_underlying(event::MouseButtons::eSIZE);
    for (int b = 0; b < n; ++b)
    {
        const auto btn = static_cast<event::MouseButtons>(b);
        for (auto a : {A_UNDEF, A_REL, A_PRESS, A_REP})
        {
            ae::event_context ctx{};
            const auto       e  = event::mousebutton_event{btn, a};
            const auto       ch = ae::build_change(ctx, e);

            check(ch.change == ae::ChangeType::PointerButton, "mouse: change==PointerButton");
            check(ch.button.is_press() == expect_press(a), "mouse: press/repeat 归一");

            // 映射：只有左/右/中有槽位
            const bool mapped = btn == event::MouseButtons::eMOUSE_BUTTON_LEFT ||
                                btn == event::MouseButtons::eMOUSE_BUTTON_RIGHT ||
                                btn == event::MouseButtons::eMOUSE_BUTTON_MIDDLE;
            check((ch.button.button != ae::MouseButton::eUNDEFINED) == mapped,
                  "mouse: 映射表");

            auto c2 = ctx;
            ae::apply_change(c2, ch); // ← 未映射的按钮在这里曾被写成 buttons[0xFF]
            check(c2.any_button_down() == (mapped && expect_press(a)),
                  "mouse: 按住状态（未映射必须丢弃）");
            check(c2.pointer_x == 0.0 && c2.pointer_y == 0.0,
                  "mouse: 不能踩坏 context 的其它字段");
        }
    }
    std::println("鼠标矩阵 : {} buttons × 4 actions", n);
}

// ═══════════════════════════════════════════════════════════════════════
// ③ 移动：{有/无历史位置} × {有/无按键按下}
//      · 有按键 ⇒ PointerDownMove，否则 PointerUpMove
//      · 有历史位置才有 dx/dy
//      · apply 之后位置有效
// ═══════════════════════════════════════════════════════════════════════
void position_matrix()
{
    for (bool valid : {false, true})
        for (bool down : {false, true})
        {
            ae::event_context ctx{};
            ctx.pointer_position_valid = valid;
            ctx.pointer_x              = 10.0;
            ctx.pointer_y              = 20.0;
            ctx.buttons[0] = down ? ae::HeldState::Down : ae::HeldState::Up;

            const auto e  = event::position2d_event{40.0, 60.0};
            const auto ch = ae::build_change(ctx, e);

            check(ch.change == (down ? ae::ChangeType::PointerDownMove
                                     : ae::ChangeType::PointerUpMove),
                  "move: down/up 分类");
            check(ch.move.x == 40.0 && ch.move.y == 60.0, "move: 绝对位置透传");
            check(valid ? (ch.move.dx == 30.0 && ch.move.dy == 40.0)
                        : (ch.move.dx == 0.0 && ch.move.dy == 0.0),
                  "move: 增量取决于历史位置");

            ae::apply_change(ctx, ch);
            check(ctx.pointer_x == 40.0 && ctx.pointer_y == 60.0, "move: 位置被更新");
            check(ctx.pointer_position_valid, "move: 之后位置有效");
        }
    std::println("移动矩阵 : 2 × 2");
}

// ═══════════════════════════════════════════════════════════════════════
// ④ 不参与手势的事件：全部 ⇒ ChangeType::None
// ═══════════════════════════════════════════════════════════════════════
void unrelated_types()
{
    ae::event_context ctx{};
    check(ae::build_change(ctx, event::cursor_enter_event{}).change == ae::ChangeType::None,
          "cursor_enter ⇒ None");
    check(ae::build_change(ctx, event::window_size_event{}).change == ae::ChangeType::None,
          "window_size ⇒ None");
    check(ae::build_change(ctx, event::char_event{}).change == ae::ChangeType::None,
          "char ⇒ None");
    check(ae::build_change(ctx, event::scroll_event{}).change == ae::ChangeType::None,
          "scroll ⇒ None");
    check(ae::build_change(ctx, event::drop_event{}).change == ae::ChangeType::None,
          "drop ⇒ None");
    std::println("其它类型 : 5 种 ⇒ None");
}

// ═══════════════════════════════════════════════════════════════════════
int main()
{
    std::println("=== GLFW 可能送来的每一个值（不需要人手）===");
    keyboard_matrix();
    mouse_matrix();
    position_matrix();
    unrelated_types();
    std::println("=== {} 项检查，失败 {} ===", cases, bad);
    return bad == 0 ? 0 : 1;
}
