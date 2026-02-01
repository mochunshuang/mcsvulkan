#pragma once

#include <cstddef>

#define GLFW_INCLUDE_VULKAN // REQUIRED only for GLFW CreateWindowSurface.
#include <GLFW/glfw3.h>

#include "../event/event_type.hpp"

namespace mcs::vulkan::wsi::glfw
{
    namespace input
    {
        // NOTE: bug 的来源。控制键是可以组合的
        //  static constexpr auto mappingModifierKey(int mod) noexcept // NOLINT
        //  {
        //      switch (mod)
        //      {
        //      case GLFW_MOD_SHIFT:
        //          return event::ModifierKey(event::ModifierKey::SHIFT);
        //      case GLFW_MOD_CONTROL:
        //          return event::ModifierKey(event::ModifierKey::CONTROL);
        //      case GLFW_MOD_ALT:
        //          return event::ModifierKey(event::ModifierKey::ALT);
        //      case GLFW_MOD_CAPS_LOCK:
        //          return event::ModifierKey(event::ModifierKey::CAPS_LOCK);
        //      case GLFW_MOD_NUM_LOCK:
        //          return event::ModifierKey(event::ModifierKey::NUM_LOCK);
        //      default:
        //          return event::ModifierKey::None();
        //      }
        //  }

        // NOLINTNEXTLINE
        static constexpr event::MouseButtons mappingMouseButton(int button) noexcept
        {
            static_assert(GLFW_MOUSE_BUTTON_1 == GLFW_MOUSE_BUTTON_LEFT);
            switch (button)
            {
            case GLFW_MOUSE_BUTTON_LEFT:
                return event::MouseButtons::MOUSE_BUTTON_LEFT;
            case GLFW_MOUSE_BUTTON_RIGHT:
                return event::MouseButtons::MOUSE_BUTTON_RIGHT;
            case GLFW_MOUSE_BUTTON_MIDDLE:
                return event::MouseButtons::MOUSE_BUTTON_MIDDLE;
            // case GLFW_MOUSE_BUTTON_1:
            //     return event::MouseButtons::MOUSE_BUTTON_1;
            // case GLFW_MOUSE_BUTTON_2:
            //     return event::MouseButtons::MOUSE_BUTTON_2;
            // case GLFW_MOUSE_BUTTON_3:
            //     return event::MouseButtons::MOUSE_BUTTON_3;
            case GLFW_MOUSE_BUTTON_4:
                return event::MouseButtons::MOUSE_BUTTON_4;
            case GLFW_MOUSE_BUTTON_5:
                return event::MouseButtons::MOUSE_BUTTON_5;
            case GLFW_MOUSE_BUTTON_6:
                return event::MouseButtons::MOUSE_BUTTON_6;
            case GLFW_MOUSE_BUTTON_7:
                return event::MouseButtons::MOUSE_BUTTON_7;
                // case GLFW_MOUSE_BUTTON_8:
            case GLFW_MOUSE_BUTTON_LAST:
                return event::MouseButtons::MOUSE_BUTTON_LAST;
            default:
                return event::MouseButtons::UNDEFINED;
            }
            static_assert(GLFW_MOUSE_BUTTON_LAST == GLFW_MOUSE_BUTTON_8,
                          "使用命名映射要保证合法");
        }

        static constexpr event::Action mappingAction(int action) noexcept // NOLINT
        {
            switch (action)
            {
            case GLFW_PRESS:
                return event::Action::PRESS;
            case GLFW_RELEASE:
                return event::Action::RELEASE;
            case GLFW_REPEAT:
                return event::Action::REPEAT;
            default:
                return event::Action::UNDEFINED;
            }
        }

        static constexpr event::Key mappingKey(int key) noexcept // NOLINT
        {
            switch (key)
            {
            // 特殊键
            case GLFW_KEY_UNKNOWN:
                return event::Key::UNKNOWN;

            // 可打印字符键 - 空格和标点
            case GLFW_KEY_SPACE:
                return event::Key::SPACE;
            case GLFW_KEY_APOSTROPHE:
                return event::Key::APOSTROPHE;
            case GLFW_KEY_COMMA:
                return event::Key::COMMA;
            case GLFW_KEY_MINUS:
                return event::Key::MINUS;
            case GLFW_KEY_PERIOD:
                return event::Key::PERIOD;
            case GLFW_KEY_SLASH:
                return event::Key::SLASH;
            case GLFW_KEY_SEMICOLON:
                return event::Key::SEMICOLON;
            case GLFW_KEY_EQUAL:
                return event::Key::EQUAL;
            case GLFW_KEY_LEFT_BRACKET:
                return event::Key::LEFT_BRACKET;
            case GLFW_KEY_BACKSLASH:
                return event::Key::BACKSLASH;
            case GLFW_KEY_RIGHT_BRACKET:
                return event::Key::RIGHT_BRACKET;
            case GLFW_KEY_GRAVE_ACCENT:
                return event::Key::GRAVE_ACCENT;

            // 数字键
            case GLFW_KEY_0:
                return event::Key::KEY_0;
            case GLFW_KEY_1:
                return event::Key::KEY_1;
            case GLFW_KEY_2:
                return event::Key::KEY_2;
            case GLFW_KEY_3:
                return event::Key::KEY_3;
            case GLFW_KEY_4:
                return event::Key::KEY_4;
            case GLFW_KEY_5:
                return event::Key::KEY_5;
            case GLFW_KEY_6:
                return event::Key::KEY_6;
            case GLFW_KEY_7:
                return event::Key::KEY_7;
            case GLFW_KEY_8:
                return event::Key::KEY_8;
            case GLFW_KEY_9:
                return event::Key::KEY_9;

            // 字母键
            case GLFW_KEY_A:
                return event::Key::A;
            case GLFW_KEY_B:
                return event::Key::B;
            case GLFW_KEY_C:
                return event::Key::C;
            case GLFW_KEY_D:
                return event::Key::D;
            case GLFW_KEY_E:
                return event::Key::E;
            case GLFW_KEY_F:
                return event::Key::F;
            case GLFW_KEY_G:
                return event::Key::G;
            case GLFW_KEY_H:
                return event::Key::H;
            case GLFW_KEY_I:
                return event::Key::I;
            case GLFW_KEY_J:
                return event::Key::J;
            case GLFW_KEY_K:
                return event::Key::K;
            case GLFW_KEY_L:
                return event::Key::L;
            case GLFW_KEY_M:
                return event::Key::M;
            case GLFW_KEY_N:
                return event::Key::N;
            case GLFW_KEY_O:
                return event::Key::O;
            case GLFW_KEY_P:
                return event::Key::P;
            case GLFW_KEY_Q:
                return event::Key::Q;
            case GLFW_KEY_R:
                return event::Key::R;
            case GLFW_KEY_S:
                return event::Key::S;
            case GLFW_KEY_T:
                return event::Key::T;
            case GLFW_KEY_U:
                return event::Key::U;
            case GLFW_KEY_V:
                return event::Key::V;
            case GLFW_KEY_W:
                return event::Key::W;
            case GLFW_KEY_X:
                return event::Key::X;
            case GLFW_KEY_Y:
                return event::Key::Y;
            case GLFW_KEY_Z:
                return event::Key::Z;

            // 世界键
            case GLFW_KEY_WORLD_1:
                return event::Key::WORLD_1;
            case GLFW_KEY_WORLD_2:
                return event::Key::WORLD_2;

            // 编辑与导航键
            case GLFW_KEY_ESCAPE:
                return event::Key::ESCAPE;
            case GLFW_KEY_ENTER:
                return event::Key::ENTER;
            case GLFW_KEY_TAB:
                return event::Key::TAB;
            case GLFW_KEY_BACKSPACE:
                return event::Key::BACKSPACE;
            case GLFW_KEY_INSERT:
                return event::Key::INSERT;
            case GLFW_KEY_DELETE:
                return event::Key::DELETE;
            case GLFW_KEY_RIGHT:
                return event::Key::RIGHT;
            case GLFW_KEY_LEFT:
                return event::Key::LEFT;
            case GLFW_KEY_DOWN:
                return event::Key::DOWN;
            case GLFW_KEY_UP:
                return event::Key::UP;
            case GLFW_KEY_PAGE_UP:
                return event::Key::PAGE_UP;
            case GLFW_KEY_PAGE_DOWN:
                return event::Key::PAGE_DOWN;
            case GLFW_KEY_HOME:
                return event::Key::HOME;
            case GLFW_KEY_END:
                return event::Key::END;

            // 锁定键
            case GLFW_KEY_CAPS_LOCK:
                return event::Key::CAPS_LOCK;
            case GLFW_KEY_SCROLL_LOCK:
                return event::Key::SCROLL_LOCK;
            case GLFW_KEY_NUM_LOCK:
                return event::Key::NUM_LOCK;

            // 系统键
            case GLFW_KEY_PRINT_SCREEN:
                return event::Key::PRINT_SCREEN;
            case GLFW_KEY_PAUSE:
                return event::Key::PAUSE;
            case GLFW_KEY_MENU:
                return event::Key::MENU;

            // 功能键 F1-F25
            case GLFW_KEY_F1:
                return event::Key::F1;
            case GLFW_KEY_F2:
                return event::Key::F2;
            case GLFW_KEY_F3:
                return event::Key::F3;
            case GLFW_KEY_F4:
                return event::Key::F4;
            case GLFW_KEY_F5:
                return event::Key::F5;
            case GLFW_KEY_F6:
                return event::Key::F6;
            case GLFW_KEY_F7:
                return event::Key::F7;
            case GLFW_KEY_F8:
                return event::Key::F8;
            case GLFW_KEY_F9:
                return event::Key::F9;
            case GLFW_KEY_F10:
                return event::Key::F10;
            case GLFW_KEY_F11:
                return event::Key::F11;
            case GLFW_KEY_F12:
                return event::Key::F12;
            case GLFW_KEY_F13:
                return event::Key::F13;
            case GLFW_KEY_F14:
                return event::Key::F14;
            case GLFW_KEY_F15:
                return event::Key::F15;
            case GLFW_KEY_F16:
                return event::Key::F16;
            case GLFW_KEY_F17:
                return event::Key::F17;
            case GLFW_KEY_F18:
                return event::Key::F18;
            case GLFW_KEY_F19:
                return event::Key::F19;
            case GLFW_KEY_F20:
                return event::Key::F20;
            case GLFW_KEY_F21:
                return event::Key::F21;
            case GLFW_KEY_F22:
                return event::Key::F22;
            case GLFW_KEY_F23:
                return event::Key::F23;
            case GLFW_KEY_F24:
                return event::Key::F24;
            case GLFW_KEY_F25:
                return event::Key::F25;

            // 小键盘键
            case GLFW_KEY_KP_0:
                return event::Key::KP_0;
            case GLFW_KEY_KP_1:
                return event::Key::KP_1;
            case GLFW_KEY_KP_2:
                return event::Key::KP_2;
            case GLFW_KEY_KP_3:
                return event::Key::KP_3;
            case GLFW_KEY_KP_4:
                return event::Key::KP_4;
            case GLFW_KEY_KP_5:
                return event::Key::KP_5;
            case GLFW_KEY_KP_6:
                return event::Key::KP_6;
            case GLFW_KEY_KP_7:
                return event::Key::KP_7;
            case GLFW_KEY_KP_8:
                return event::Key::KP_8;
            case GLFW_KEY_KP_9:
                return event::Key::KP_9;
            case GLFW_KEY_KP_DECIMAL:
                return event::Key::KP_DECIMAL;
            case GLFW_KEY_KP_DIVIDE:
                return event::Key::KP_DIVIDE;
            case GLFW_KEY_KP_MULTIPLY:
                return event::Key::KP_MULTIPLY;
            case GLFW_KEY_KP_SUBTRACT:
                return event::Key::KP_SUBTRACT;
            case GLFW_KEY_KP_ADD:
                return event::Key::KP_ADD;
            case GLFW_KEY_KP_ENTER:
                return event::Key::KP_ENTER;
            case GLFW_KEY_KP_EQUAL:
                return event::Key::KP_EQUAL;

            // 修饰键
            case GLFW_KEY_LEFT_SHIFT:
                return event::Key::LEFT_SHIFT;
            case GLFW_KEY_LEFT_CONTROL:
                return event::Key::LEFT_CONTROL;
            case GLFW_KEY_LEFT_ALT:
                return event::Key::LEFT_ALT;
            case GLFW_KEY_LEFT_SUPER:
                return event::Key::LEFT_SUPER;
            case GLFW_KEY_RIGHT_SHIFT:
                return event::Key::RIGHT_SHIFT;
            case GLFW_KEY_RIGHT_CONTROL:
                return event::Key::RIGHT_CONTROL;
            case GLFW_KEY_RIGHT_ALT:
                return event::Key::RIGHT_ALT;
            case GLFW_KEY_RIGHT_SUPER:
                return event::Key::RIGHT_SUPER;

            default:
                return event::Key::UNDEFINED;
            }
        }

    }; // namespace input

}; // namespace mcs::vulkan::wsi::glfw