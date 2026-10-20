#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  arena_event.hpp —— 从 test_glfw12.cpp 搬来的输入层（v2 两个测试共用）
//
//  只保留会用到的事件；`build_change` 对不认识的事件返回 ChangeType::None，
//  这就是"不认识的类型安静丢掉"的那一层。
// ═══════════════════════════════════════════════════════════════════════
#include <array>
#include <cstdint>
#include <type_traits>

#include "uhi.hpp"

#include "../../../../../../include/detail/event/event_type.hpp"

// clang-format off
namespace arena_event
{
    using Key = mcs::vulkan::event::Key;

    enum class MouseButton : std::uint8_t
    {
        eMOUSE_BUTTON_LEFT   = 0,
        eMOUSE_BUTTON_RIGHT  = 1,
        eMOUSE_BUTTON_MIDDLE = 2,
        eUNDEFINED           = 0xFF, // 映射不到：**绝不能拿去当下标**
        eSIZE                = 3
    };

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

    enum class ChangeType : std::uint8_t { None, Key, PointerButton, PointerDownMove, PointerUpMove };

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
        uhi::time_point time_point{};
        key_change key{}; button_change button{}; move_change move{};
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

    template <class E>
    change_event build_change(const event_context &ctx, const E &e) noexcept
    {
        using T = std::remove_cvref_t<E>;
        change_event ch{};
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
            if (ctx.pointer_position_valid) { ch.move.dx = e.xpos - ctx.pointer_x; ch.move.dy = e.ypos - ctx.pointer_y; }
        }
        return ch; // 其它类型 ⇒ ChangeType::None
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
            // 映射不到的按钮（侧键 4..8 等）没有槽位，直接丢弃。
            // 少了这一句 ctx.buttons[0xFF] 就是越界写——GLFW 真的会送这些键。
            if (ch.button.button == MouseButton::eUNDEFINED) break;
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
