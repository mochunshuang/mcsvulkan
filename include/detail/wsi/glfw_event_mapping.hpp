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
                return event::MouseButtons::eMOUSE_BUTTON_LEFT;
            case GLFW_MOUSE_BUTTON_RIGHT:
                return event::MouseButtons::eMOUSE_BUTTON_RIGHT;
            case GLFW_MOUSE_BUTTON_MIDDLE:
                return event::MouseButtons::eMOUSE_BUTTON_MIDDLE;
            // case GLFW_MOUSE_BUTTON_1:
            //     return event::MouseButtons::eMOUSE_BUTTON_1;
            // case GLFW_MOUSE_BUTTON_2:
            //     return event::MouseButtons::eMOUSE_BUTTON_2;
            // case GLFW_MOUSE_BUTTON_3:
            //     return event::MouseButtons::eMOUSE_BUTTON_3;
            case GLFW_MOUSE_BUTTON_4:
                return event::MouseButtons::eMOUSE_BUTTON_4;
            case GLFW_MOUSE_BUTTON_5:
                return event::MouseButtons::eMOUSE_BUTTON_5;
            case GLFW_MOUSE_BUTTON_6:
                return event::MouseButtons::eMOUSE_BUTTON_6;
            case GLFW_MOUSE_BUTTON_7:
                return event::MouseButtons::eMOUSE_BUTTON_7;
                // case GLFW_MOUSE_BUTTON_8:
            case GLFW_MOUSE_BUTTON_LAST:
                return event::MouseButtons::eMOUSE_BUTTON_LAST;
            default:
                return event::MouseButtons::eUNDEFINED;
            }
            static_assert(GLFW_MOUSE_BUTTON_LAST == GLFW_MOUSE_BUTTON_8,
                          "使用命名映射要保证合法");
        }

        static constexpr event::Action mappingAction(int action) noexcept // NOLINT
        {
            switch (action)
            {
            case GLFW_PRESS:
                return event::Action::ePRESS;
            case GLFW_RELEASE:
                return event::Action::eRELEASE;
            case GLFW_REPEAT:
                return event::Action::eREPEAT;
            default:
                return event::Action::eUNDEFINED;
            }
        }

        static constexpr event::Key mappingKey(int key) noexcept // NOLINT
        {
            switch (key)
            {
            // 特殊键
            case GLFW_KEY_UNKNOWN:
                return event::Key::eUNKNOWN;

            // 可打印字符键 - 空格和标点
            case GLFW_KEY_SPACE:
                return event::Key::eSPACE;
            case GLFW_KEY_APOSTROPHE:
                return event::Key::eAPOSTROPHE;
            case GLFW_KEY_COMMA:
                return event::Key::eCOMMA;
            case GLFW_KEY_MINUS:
                return event::Key::eMINUS;
            case GLFW_KEY_PERIOD:
                return event::Key::ePERIOD;
            case GLFW_KEY_SLASH:
                return event::Key::eSLASH;
            case GLFW_KEY_SEMICOLON:
                return event::Key::eSEMICOLON;
            case GLFW_KEY_EQUAL:
                return event::Key::eEQUAL;
            case GLFW_KEY_LEFT_BRACKET:
                return event::Key::eLEFT_BRACKET;
            case GLFW_KEY_BACKSLASH:
                return event::Key::eBACKSLASH;
            case GLFW_KEY_RIGHT_BRACKET:
                return event::Key::eRIGHT_BRACKET;
            case GLFW_KEY_GRAVE_ACCENT:
                return event::Key::eGRAVE_ACCENT;

            // 数字键
            case GLFW_KEY_0:
                return event::Key::eKEY_0;
            case GLFW_KEY_1:
                return event::Key::eKEY_1;
            case GLFW_KEY_2:
                return event::Key::eKEY_2;
            case GLFW_KEY_3:
                return event::Key::eKEY_3;
            case GLFW_KEY_4:
                return event::Key::eKEY_4;
            case GLFW_KEY_5:
                return event::Key::eKEY_5;
            case GLFW_KEY_6:
                return event::Key::eKEY_6;
            case GLFW_KEY_7:
                return event::Key::eKEY_7;
            case GLFW_KEY_8:
                return event::Key::eKEY_8;
            case GLFW_KEY_9:
                return event::Key::eKEY_9;

            // 字母键
            case GLFW_KEY_A:
                return event::Key::eA;
            case GLFW_KEY_B:
                return event::Key::eB;
            case GLFW_KEY_C:
                return event::Key::eC;
            case GLFW_KEY_D:
                return event::Key::eD;
            case GLFW_KEY_E:
                return event::Key::eE;
            case GLFW_KEY_F:
                return event::Key::eF;
            case GLFW_KEY_G:
                return event::Key::eG;
            case GLFW_KEY_H:
                return event::Key::eH;
            case GLFW_KEY_I:
                return event::Key::eI;
            case GLFW_KEY_J:
                return event::Key::eJ;
            case GLFW_KEY_K:
                return event::Key::eK;
            case GLFW_KEY_L:
                return event::Key::eL;
            case GLFW_KEY_M:
                return event::Key::eM;
            case GLFW_KEY_N:
                return event::Key::eN;
            case GLFW_KEY_O:
                return event::Key::eO;
            case GLFW_KEY_P:
                return event::Key::eP;
            case GLFW_KEY_Q:
                return event::Key::eQ;
            case GLFW_KEY_R:
                return event::Key::eR;
            case GLFW_KEY_S:
                return event::Key::eS;
            case GLFW_KEY_T:
                return event::Key::eT;
            case GLFW_KEY_U:
                return event::Key::eU;
            case GLFW_KEY_V:
                return event::Key::eV;
            case GLFW_KEY_W:
                return event::Key::eW;
            case GLFW_KEY_X:
                return event::Key::eX;
            case GLFW_KEY_Y:
                return event::Key::eY;
            case GLFW_KEY_Z:
                return event::Key::eZ;

            // 世界键
            case GLFW_KEY_WORLD_1:
                return event::Key::eWORLD_1;
            case GLFW_KEY_WORLD_2:
                return event::Key::eWORLD_2;

            // 编辑与导航键
            case GLFW_KEY_ESCAPE:
                return event::Key::eESCAPE;
            case GLFW_KEY_ENTER:
                return event::Key::eENTER;
            case GLFW_KEY_TAB:
                return event::Key::eTAB;
            case GLFW_KEY_BACKSPACE:
                return event::Key::eBACKSPACE;
            case GLFW_KEY_INSERT:
                return event::Key::eINSERT;
            case GLFW_KEY_DELETE:
                return event::Key::eDELETE;
            case GLFW_KEY_RIGHT:
                return event::Key::eRIGHT;
            case GLFW_KEY_LEFT:
                return event::Key::eLEFT;
            case GLFW_KEY_DOWN:
                return event::Key::eDOWN;
            case GLFW_KEY_UP:
                return event::Key::eUP;
            case GLFW_KEY_PAGE_UP:
                return event::Key::ePAGE_UP;
            case GLFW_KEY_PAGE_DOWN:
                return event::Key::ePAGE_DOWN;
            case GLFW_KEY_HOME:
                return event::Key::eHOME;
            case GLFW_KEY_END:
                return event::Key::eEND;

            // 锁定键
            case GLFW_KEY_CAPS_LOCK:
                return event::Key::eCAPS_LOCK;
            case GLFW_KEY_SCROLL_LOCK:
                return event::Key::eSCROLL_LOCK;
            case GLFW_KEY_NUM_LOCK:
                return event::Key::eNUM_LOCK;

            // 系统键
            case GLFW_KEY_PRINT_SCREEN:
                return event::Key::ePRINT_SCREEN;
            case GLFW_KEY_PAUSE:
                return event::Key::ePAUSE;
            case GLFW_KEY_MENU:
                return event::Key::eMENU;

            // 功能键 F1-F25
            case GLFW_KEY_F1:
                return event::Key::eF1;
            case GLFW_KEY_F2:
                return event::Key::eF2;
            case GLFW_KEY_F3:
                return event::Key::eF3;
            case GLFW_KEY_F4:
                return event::Key::eF4;
            case GLFW_KEY_F5:
                return event::Key::eF5;
            case GLFW_KEY_F6:
                return event::Key::eF6;
            case GLFW_KEY_F7:
                return event::Key::eF7;
            case GLFW_KEY_F8:
                return event::Key::eF8;
            case GLFW_KEY_F9:
                return event::Key::eF9;
            case GLFW_KEY_F10:
                return event::Key::eF10;
            case GLFW_KEY_F11:
                return event::Key::eF11;
            case GLFW_KEY_F12:
                return event::Key::eF12;
            case GLFW_KEY_F13:
                return event::Key::eF13;
            case GLFW_KEY_F14:
                return event::Key::eF14;
            case GLFW_KEY_F15:
                return event::Key::eF15;
            case GLFW_KEY_F16:
                return event::Key::eF16;
            case GLFW_KEY_F17:
                return event::Key::eF17;
            case GLFW_KEY_F18:
                return event::Key::eF18;
            case GLFW_KEY_F19:
                return event::Key::eF19;
            case GLFW_KEY_F20:
                return event::Key::eF20;
            case GLFW_KEY_F21:
                return event::Key::eF21;
            case GLFW_KEY_F22:
                return event::Key::eF22;
            case GLFW_KEY_F23:
                return event::Key::eF23;
            case GLFW_KEY_F24:
                return event::Key::eF24;
            case GLFW_KEY_F25:
                return event::Key::eF25;

            // 小键盘键
            case GLFW_KEY_KP_0:
                return event::Key::eKP_0;
            case GLFW_KEY_KP_1:
                return event::Key::eKP_1;
            case GLFW_KEY_KP_2:
                return event::Key::eKP_2;
            case GLFW_KEY_KP_3:
                return event::Key::eKP_3;
            case GLFW_KEY_KP_4:
                return event::Key::eKP_4;
            case GLFW_KEY_KP_5:
                return event::Key::eKP_5;
            case GLFW_KEY_KP_6:
                return event::Key::eKP_6;
            case GLFW_KEY_KP_7:
                return event::Key::eKP_7;
            case GLFW_KEY_KP_8:
                return event::Key::eKP_8;
            case GLFW_KEY_KP_9:
                return event::Key::eKP_9;
            case GLFW_KEY_KP_DECIMAL:
                return event::Key::eKP_DECIMAL;
            case GLFW_KEY_KP_DIVIDE:
                return event::Key::eKP_DIVIDE;
            case GLFW_KEY_KP_MULTIPLY:
                return event::Key::eKP_MULTIPLY;
            case GLFW_KEY_KP_SUBTRACT:
                return event::Key::eKP_SUBTRACT;
            case GLFW_KEY_KP_ADD:
                return event::Key::eKP_ADD;
            case GLFW_KEY_KP_ENTER:
                return event::Key::eKP_ENTER;
            case GLFW_KEY_KP_EQUAL:
                return event::Key::eKP_EQUAL;

            // 修饰键
            case GLFW_KEY_LEFT_SHIFT:
                return event::Key::eLEFT_SHIFT;
            case GLFW_KEY_LEFT_CONTROL:
                return event::Key::eLEFT_CONTROL;
            case GLFW_KEY_LEFT_ALT:
                return event::Key::eLEFT_ALT;
            case GLFW_KEY_LEFT_SUPER:
                return event::Key::eLEFT_SUPER;
            case GLFW_KEY_RIGHT_SHIFT:
                return event::Key::eRIGHT_SHIFT;
            case GLFW_KEY_RIGHT_CONTROL:
                return event::Key::eRIGHT_CONTROL;
            case GLFW_KEY_RIGHT_ALT:
                return event::Key::eRIGHT_ALT;
            case GLFW_KEY_RIGHT_SUPER:
                return event::Key::eRIGHT_SUPER;

            default:
                return event::Key::eUNDEFINED;
            }
        }

    }; // namespace input

}; // namespace mcs::vulkan::wsi::glfw