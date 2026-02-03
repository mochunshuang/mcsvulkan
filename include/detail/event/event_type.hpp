#pragma once

#include <cstdint>
#include <limits>
#include <format>

namespace mcs::vulkan::event
{
    constexpr auto UNDEFINED_double = (std::numeric_limits<double>::max)(); // NOLINT
    constexpr auto UNDEFINED_int = (std::numeric_limits<int>::max)();       // NOLINT

    enum class MouseButtons : std::uint8_t
    {
        eUNDEFINED,
        eMOUSE_BUTTON_1,
        eMOUSE_BUTTON_2,
        eMOUSE_BUTTON_3,
        eMOUSE_BUTTON_4,
        eMOUSE_BUTTON_5,
        eMOUSE_BUTTON_6,
        eMOUSE_BUTTON_7,
        eMOUSE_BUTTON_8,
        // 别名化，8bit的映射罢了
        eMOUSE_BUTTON_LAST,
        eMOUSE_BUTTON_LEFT,
        eMOUSE_BUTTON_RIGHT,
        eMOUSE_BUTTON_MIDDLE,

        eSIZE // NOTE: 数组需要
    };

    enum class Action : std::uint8_t
    {
        eUNDEFINED,
        eRELEASE, // 释放
        ePRESS,   // 按下
        eREPEAT   // 重复（长按）
    };

    enum class Key : std::uint8_t
    {
        eUNDEFINED,
        // 特殊键
        eUNKNOWN,

        // 可打印字符键 - 空格和标点
        eSPACE,
        eAPOSTROPHE,    // '
        eCOMMA,         // ,
        eMINUS,         // -
        ePERIOD,        // .
        eSLASH,         // /
        eSEMICOLON,     // ;
        eEQUAL,         // =
        eLEFT_BRACKET,  // [
        eBACKSLASH,     // '\'
        eRIGHT_BRACKET, // ]
        eGRAVE_ACCENT,  // `

        // 数字键
        eKEY_0,
        eKEY_1,
        eKEY_2,
        eKEY_3,
        eKEY_4,
        eKEY_5,
        eKEY_6,
        eKEY_7,
        eKEY_8,
        eKEY_9,

        // 字母键
        eA,
        eB,
        eC,
        eD,
        eE,
        eF,
        eG,
        eH,
        eI,
        eJ,
        eK,
        eL,
        eM,
        eN,
        eO,
        eP,
        eQ,
        eR,
        eS,
        eT,
        eU,
        eV,
        eW,
        eX,
        eY,
        eZ,

        // 世界键（非美式布局特殊键）
        eWORLD_1,
        eWORLD_2,

        // 编辑与导航键
        eESCAPE,
        eENTER,
        eTAB,
        eBACKSPACE,
        eINSERT,
        eDELETE,
        eRIGHT,
        eLEFT,
        eDOWN,
        eUP,
        ePAGE_UP,
        ePAGE_DOWN,
        eHOME,
        eEND,

        // 锁定键
        eCAPS_LOCK,
        eSCROLL_LOCK,
        eNUM_LOCK,

        // 系统键
        ePRINT_SCREEN,
        ePAUSE,
        eMENU,

        // 功能键 F1-F25
        eF1,
        eF2,
        eF3,
        eF4,
        eF5,
        eF6,
        eF7,
        eF8,
        eF9,
        eF10,
        eF11,
        eF12,
        eF13,
        eF14,
        eF15,
        eF16,
        eF17,
        eF18,
        eF19,
        eF20,
        eF21,
        eF22,
        eF23,
        eF24,
        eF25,

        // 小键盘键
        eKP_0,
        eKP_1,
        eKP_2,
        eKP_3,
        eKP_4,
        eKP_5,
        eKP_6,
        eKP_7,
        eKP_8,
        eKP_9,
        eKP_DECIMAL,  // .
        eKP_DIVIDE,   // /
        eKP_MULTIPLY, // *
        eKP_SUBTRACT, // -
        eKP_ADD,      // +
        eKP_ENTER,
        eKP_EQUAL, // =

        // 修饰键
        eLEFT_SHIFT,
        eLEFT_CONTROL,
        eLEFT_ALT,
        eLEFT_SUPER, // Windows键 / Command键
        eRIGHT_SHIFT,
        eRIGHT_CONTROL,
        eRIGHT_ALT,
        eRIGHT_SUPER,

        eSIZE // NOTE: 数组需要
    };

    /**
     * @brief 修饰键标志位封装类
     *
     * 提供类型安全的修饰键操作，支持位运算和组合检查。
     */
    class ModifierKey
    {
      public:
        // 内部枚举定义
        using store_type = std::uint8_t;
        enum Value : store_type
        {
            NONE = 0,
            SHIFT = 0x01,
            CONTROL = 0x02,
            ALT = 0x04,
            SUPER = 0x08,
            CAPS_LOCK = 0x10,
            NUM_LOCK = 0x20
        };

        template <typename OutputIt>
        [[nodiscard]] OutputIt format_to(OutputIt out) const // NOLINT
        {
            if (empty())
            {
                return std::format_to(out, "NONE");
            }
            // NOLINTNEXTLINE
            constexpr std::pair<const char *, Value> KEYS[] = {
                {"SHIFT", SHIFT}, {"CONTROL", CONTROL},     {"ALT", ALT},
                {"SUPER", SUPER}, {"CAPS_LOCK", CAPS_LOCK}, {"NUM_LOCK", NUM_LOCK}};

            bool first = true;
            for (const auto &[name, value] : KEYS)
            {
                if (has(value))
                {
                    if (!first)
                    {
                        out = std::format_to(out, "|");
                    }
                    out = std::format_to(out, "{}", name);
                    first = false;
                }
            }
            return out;
        }

      private:
        store_type value_;

      public:
        // 构造函数
        constexpr ModifierKey() noexcept : value_(NONE) {}
        constexpr explicit ModifierKey(Value v) noexcept
            : value_(static_cast<store_type>(v))
        {
        }
        constexpr explicit ModifierKey(store_type v) noexcept : value_(v) {}

        // 获取原始值（用于与GLFW等库交互）
        [[nodiscard]] constexpr store_type raw_data() const noexcept // NOLINT
        {
            return value_;
        }

        // 检查是否包含特定修饰键
        [[nodiscard]] constexpr bool has(Value key) const noexcept
        {
            return (value_ & static_cast<store_type>(key)) != 0;
        }

        // 检查是否包含所有指定的修饰键
        [[nodiscard]] constexpr bool hasAll(ModifierKey other) const noexcept
        {
            return (value_ & other.value_) == other.value_;
        }

        // 检查是否包含任意指定的修饰键
        [[nodiscard]] constexpr bool hasAny(ModifierKey other) const noexcept
        {
            return (value_ & other.value_) != 0;
        }

        // 检查是否为空
        [[nodiscard]] constexpr bool empty() const noexcept
        {
            return value_ == NONE;
        }

        // 清除所有修饰键
        constexpr void clear() noexcept
        {
            value_ = NONE;
        }

        // 添加修饰键
        constexpr void add(Value key) noexcept
        {
            value_ |= static_cast<store_type>(key);
        }

        // 移除修饰键
        constexpr void remove(Value key) noexcept
        {
            value_ &= ~static_cast<store_type>(key);
        }

        // 运算符重载
        constexpr ModifierKey operator|(Value key) const noexcept
        {
            return ModifierKey(value_ | static_cast<store_type>(key));
        }

        constexpr ModifierKey operator|(ModifierKey other) const noexcept
        {
            return ModifierKey(value_ | other.value_);
        }

        constexpr ModifierKey &operator|=(Value key) noexcept
        {
            value_ |= static_cast<store_type>(key);
            return *this;
        }

        constexpr ModifierKey &operator|=(ModifierKey other) noexcept
        {
            value_ |= other.value_;
            return *this;
        }

        constexpr ModifierKey operator&(Value key) const noexcept
        {
            return ModifierKey(value_ & static_cast<store_type>(key));
        }

        constexpr ModifierKey operator&(ModifierKey other) const noexcept
        {
            return ModifierKey(value_ & other.value_);
        }

        constexpr bool operator==(ModifierKey other) const noexcept
        {
            return value_ == other.value_;
        }

        constexpr bool operator!=(ModifierKey other) const noexcept
        {
            return value_ != other.value_;
        }
        constexpr bool operator==(store_type v) const noexcept
        {
            return static_cast<store_type>(value_) == v;
        }
        constexpr bool operator!=(store_type v) const noexcept
        {
            return static_cast<store_type>(value_) != v;
        }

        // NOLINTBEGIN
        static constexpr ModifierKey None() noexcept
        {
            return ModifierKey(NONE);
        }
        static constexpr ModifierKey Shift() noexcept
        {
            return ModifierKey(SHIFT);
        }
        static constexpr ModifierKey Control() noexcept
        {
            return ModifierKey(CONTROL);
        }
        static constexpr ModifierKey Alt() noexcept
        {
            return ModifierKey(ALT);
        }
        static constexpr ModifierKey Super() noexcept
        {
            return ModifierKey(SUPER);
        }
        static constexpr ModifierKey CapsLock() noexcept
        {
            return ModifierKey(CAPS_LOCK);
        }
        static constexpr ModifierKey NumLock() noexcept
        {
            return ModifierKey(NUM_LOCK);
        }

        // 常用组合
        static constexpr ModifierKey CtrlShift() noexcept
        {
            return Control() | Shift();
        }
        static constexpr ModifierKey CtrlAlt() noexcept
        {
            return Control() | Alt();
        }
        static constexpr ModifierKey CtrlAltShift() noexcept
        {
            return Control() | Alt() | Shift();
        }
        // NOLINTEND
    };

    // 全局运算符重载
    constexpr ModifierKey operator|(ModifierKey::Value lhs,
                                    ModifierKey::Value rhs) noexcept
    {
        return ModifierKey(lhs) | rhs;
    }

    struct keyboard_event
    {
        using key_store_type = std::uint8_t;
        Key key{Key::eUNDEFINED};                      // NOLINT
        Action action{Action::eUNDEFINED};             // NOLINT
        ModifierKey modifier_key{ModifierKey::None()}; // NOLINT
        int scancode{UNDEFINED_int};                   // NOLINT

        static_assert(sizeof(key) == sizeof(std::uint8_t));
        [[nodiscard]] bool press() const noexcept
        {
            return action == Action::ePRESS;
        }
        [[nodiscard]] bool release() const noexcept
        {
            return action == Action::eRELEASE;
        }
        [[nodiscard]] bool repeat() const noexcept
        {
            return action == Action::eREPEAT;
        }
        [[nodiscard]] bool isModifier(ModifierKey status) const noexcept
        {
            return status == modifier_key;
        }
        friend constexpr bool operator==(const keyboard_event &a,
                                         const keyboard_event &b) noexcept = default;
    };

    struct mousebutton_event
    {
        using key_store_type = std::uint8_t;

        MouseButtons button{MouseButtons::eUNDEFINED}; // NOLINT
        Action action{Action::eUNDEFINED};             // NOLINT
        ModifierKey modifier_key{ModifierKey::None()}; // NOLINT

        static_assert(sizeof(button) == sizeof(std::uint8_t));

        [[nodiscard]] bool press() const noexcept
        {
            return action == Action::ePRESS;
        }
        [[nodiscard]] bool release() const noexcept
        {
            return action == Action::eRELEASE;
        }
        [[nodiscard]] bool repeat() const noexcept
        {
            return action == Action::eREPEAT;
        }
        [[nodiscard]] bool isModifier(ModifierKey status) const noexcept
        {
            return status == modifier_key;
        }
        friend constexpr bool operator==(const mousebutton_event &a,
                                         const mousebutton_event &b) noexcept = default;
    };

    struct scroll_event
    {
        double xoffset{UNDEFINED_double};
        double yoffset{UNDEFINED_double};
        friend constexpr bool operator==(const scroll_event &a,
                                         const scroll_event &b) noexcept = default;
    };

    struct position2d_event
    {
        double xpos{UNDEFINED_double};
        double ypos{UNDEFINED_double};
        friend constexpr bool operator==(const position2d_event &a,
                                         const position2d_event &b) noexcept = default;
    };

    struct cursor_enter_event
    {
        bool value{};
        friend constexpr bool operator==(const cursor_enter_event &a,
                                         const cursor_enter_event &b) noexcept = default;
    };

}; // namespace mcs::vulkan::event

namespace std
{
    template <>
    struct formatter<mcs::vulkan::event::ModifierKey>
    {
        static constexpr auto parse(std::format_parse_context &ctx) noexcept
        {
            return ctx.begin();
        }

        static auto format(const mcs::vulkan::event::ModifierKey &key,
                           std::format_context &ctx)
        {
            return key.format_to(ctx.out());
        }
    };

    template <>
    struct formatter<mcs::vulkan::event::MouseButtons>
    {
        using MouseButtons = mcs::vulkan::event::MouseButtons;
        static constexpr auto parse(std::format_parse_context &ctx) noexcept
        {
            return ctx.begin();
        }

        static auto format(MouseButtons button, std::format_context &ctx)
        {
            switch (button)
            {
            case MouseButtons::eUNDEFINED:
                return std::format_to(ctx.out(), "UNDEFINED");
            case MouseButtons::eMOUSE_BUTTON_1:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_1");
            case MouseButtons::eMOUSE_BUTTON_2:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_2");
            case MouseButtons::eMOUSE_BUTTON_3:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_3");
            case MouseButtons::eMOUSE_BUTTON_4:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_4");
            case MouseButtons::eMOUSE_BUTTON_5:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_5");
            case MouseButtons::eMOUSE_BUTTON_6:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_6");
            case MouseButtons::eMOUSE_BUTTON_7:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_7");
            case MouseButtons::eMOUSE_BUTTON_8:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_8");
            case MouseButtons::eMOUSE_BUTTON_LEFT:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_LEFT");
            case MouseButtons::eMOUSE_BUTTON_RIGHT:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_RIGHT");
            case MouseButtons::eMOUSE_BUTTON_MIDDLE:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_MIDDLE");
            default:
                return std::format_to(ctx.out(), "UNKNOWN");
            }
        }
    };

    template <>
    struct formatter<mcs::vulkan::event::Action>
    {
        using Action = mcs::vulkan::event::Action;
        static constexpr auto parse(std::format_parse_context &ctx) noexcept
        {
            return ctx.begin();
        }

        static auto format(Action action, std::format_context &ctx)
        {
            switch (action)
            {
            case Action::eUNDEFINED:
                return std::format_to(ctx.out(), "UNDEFINED");
            case Action::eRELEASE:
                return std::format_to(ctx.out(), "RELEASE");
            case Action::ePRESS:
                return std::format_to(ctx.out(), "PRESS");
            case Action::eREPEAT:
                return std::format_to(ctx.out(), "REPEAT");
            default:
                return std::format_to(ctx.out(), "UNKNOWN");
            }
        }
    };

    // 为 Key 添加完整的 formatter
    template <>
    struct formatter<mcs::vulkan::event::Key>
    {
        using Key = mcs::vulkan::event::Key;
        static constexpr auto parse(std::format_parse_context &ctx) noexcept
        {
            return ctx.begin();
        }

        static auto format(Key key, std::format_context &ctx)
        {
            switch (key)
            {
            // 特殊键
            case Key::eUNDEFINED:
                return std::format_to(ctx.out(), "UNDEFINED");
            case Key::eUNKNOWN:
                return std::format_to(ctx.out(), "UNKNOWN");

            // 可打印字符键 - 空格和标点
            case Key::eSPACE:
                return std::format_to(ctx.out(), "SPACE");
            case Key::eAPOSTROPHE:
                return std::format_to(ctx.out(), "APOSTROPHE");
            case Key::eCOMMA:
                return std::format_to(ctx.out(), "COMMA");
            case Key::eMINUS:
                return std::format_to(ctx.out(), "MINUS");
            case Key::ePERIOD:
                return std::format_to(ctx.out(), "PERIOD");
            case Key::eSLASH:
                return std::format_to(ctx.out(), "SLASH");
            case Key::eSEMICOLON:
                return std::format_to(ctx.out(), "SEMICOLON");
            case Key::eEQUAL:
                return std::format_to(ctx.out(), "EQUAL");
            case Key::eLEFT_BRACKET:
                return std::format_to(ctx.out(), "LEFT_BRACKET");
            case Key::eBACKSLASH:
                return std::format_to(ctx.out(), "BACKSLASH");
            case Key::eRIGHT_BRACKET:
                return std::format_to(ctx.out(), "RIGHT_BRACKET");
            case Key::eGRAVE_ACCENT:
                return std::format_to(ctx.out(), "GRAVE_ACCENT");

            // 数字键
            case Key::eKEY_0:
                return std::format_to(ctx.out(), "KEY_0");
            case Key::eKEY_1:
                return std::format_to(ctx.out(), "KEY_1");
            case Key::eKEY_2:
                return std::format_to(ctx.out(), "KEY_2");
            case Key::eKEY_3:
                return std::format_to(ctx.out(), "KEY_3");
            case Key::eKEY_4:
                return std::format_to(ctx.out(), "KEY_4");
            case Key::eKEY_5:
                return std::format_to(ctx.out(), "KEY_5");
            case Key::eKEY_6:
                return std::format_to(ctx.out(), "KEY_6");
            case Key::eKEY_7:
                return std::format_to(ctx.out(), "KEY_7");
            case Key::eKEY_8:
                return std::format_to(ctx.out(), "KEY_8");
            case Key::eKEY_9:
                return std::format_to(ctx.out(), "KEY_9");

            // 字母键
            case Key::eA:
                return std::format_to(ctx.out(), "A");
            case Key::eB:
                return std::format_to(ctx.out(), "B");
            case Key::eC:
                return std::format_to(ctx.out(), "C");
            case Key::eD:
                return std::format_to(ctx.out(), "D");
            case Key::eE:
                return std::format_to(ctx.out(), "E");
            case Key::eF:
                return std::format_to(ctx.out(), "F");
            case Key::eG:
                return std::format_to(ctx.out(), "G");
            case Key::eH:
                return std::format_to(ctx.out(), "H");
            case Key::eI:
                return std::format_to(ctx.out(), "I");
            case Key::eJ:
                return std::format_to(ctx.out(), "J");
            case Key::eK:
                return std::format_to(ctx.out(), "K");
            case Key::eL:
                return std::format_to(ctx.out(), "L");
            case Key::eM:
                return std::format_to(ctx.out(), "M");
            case Key::eN:
                return std::format_to(ctx.out(), "N");
            case Key::eO:
                return std::format_to(ctx.out(), "O");
            case Key::eP:
                return std::format_to(ctx.out(), "P");
            case Key::eQ:
                return std::format_to(ctx.out(), "Q");
            case Key::eR:
                return std::format_to(ctx.out(), "R");
            case Key::eS:
                return std::format_to(ctx.out(), "S");
            case Key::eT:
                return std::format_to(ctx.out(), "T");
            case Key::eU:
                return std::format_to(ctx.out(), "U");
            case Key::eV:
                return std::format_to(ctx.out(), "V");
            case Key::eW:
                return std::format_to(ctx.out(), "W");
            case Key::eX:
                return std::format_to(ctx.out(), "X");
            case Key::eY:
                return std::format_to(ctx.out(), "Y");
            case Key::eZ:
                return std::format_to(ctx.out(), "Z");

            // 世界键
            case Key::eWORLD_1:
                return std::format_to(ctx.out(), "WORLD_1");
            case Key::eWORLD_2:
                return std::format_to(ctx.out(), "WORLD_2");

            // 编辑与导航键
            case Key::eESCAPE:
                return std::format_to(ctx.out(), "ESCAPE");
            case Key::eENTER:
                return std::format_to(ctx.out(), "ENTER");
            case Key::eTAB:
                return std::format_to(ctx.out(), "TAB");
            case Key::eBACKSPACE:
                return std::format_to(ctx.out(), "BACKSPACE");
            case Key::eINSERT:
                return std::format_to(ctx.out(), "INSERT");
            case Key::eDELETE:
                return std::format_to(ctx.out(), "DELETE");
            case Key::eRIGHT:
                return std::format_to(ctx.out(), "RIGHT");
            case Key::eLEFT:
                return std::format_to(ctx.out(), "LEFT");
            case Key::eDOWN:
                return std::format_to(ctx.out(), "DOWN");
            case Key::eUP:
                return std::format_to(ctx.out(), "UP");
            case Key::ePAGE_UP:
                return std::format_to(ctx.out(), "PAGE_UP");
            case Key::ePAGE_DOWN:
                return std::format_to(ctx.out(), "PAGE_DOWN");
            case Key::eHOME:
                return std::format_to(ctx.out(), "HOME");
            case Key::eEND:
                return std::format_to(ctx.out(), "END");

            // 锁定键
            case Key::eCAPS_LOCK:
                return std::format_to(ctx.out(), "CAPS_LOCK");
            case Key::eSCROLL_LOCK:
                return std::format_to(ctx.out(), "SCROLL_LOCK");
            case Key::eNUM_LOCK:
                return std::format_to(ctx.out(), "NUM_LOCK");

            // 系统键
            case Key::ePRINT_SCREEN:
                return std::format_to(ctx.out(), "PRINT_SCREEN");
            case Key::ePAUSE:
                return std::format_to(ctx.out(), "PAUSE");
            case Key::eMENU:
                return std::format_to(ctx.out(), "MENU");

            // 功能键
            case Key::eF1:
                return std::format_to(ctx.out(), "F1");
            case Key::eF2:
                return std::format_to(ctx.out(), "F2");
            case Key::eF3:
                return std::format_to(ctx.out(), "F3");
            case Key::eF4:
                return std::format_to(ctx.out(), "F4");
            case Key::eF5:
                return std::format_to(ctx.out(), "F5");
            case Key::eF6:
                return std::format_to(ctx.out(), "F6");
            case Key::eF7:
                return std::format_to(ctx.out(), "F7");
            case Key::eF8:
                return std::format_to(ctx.out(), "F8");
            case Key::eF9:
                return std::format_to(ctx.out(), "F9");
            case Key::eF10:
                return std::format_to(ctx.out(), "F10");
            case Key::eF11:
                return std::format_to(ctx.out(), "F11");
            case Key::eF12:
                return std::format_to(ctx.out(), "F12");
            case Key::eF13:
                return std::format_to(ctx.out(), "F13");
            case Key::eF14:
                return std::format_to(ctx.out(), "F14");
            case Key::eF15:
                return std::format_to(ctx.out(), "F15");
            case Key::eF16:
                return std::format_to(ctx.out(), "F16");
            case Key::eF17:
                return std::format_to(ctx.out(), "F17");
            case Key::eF18:
                return std::format_to(ctx.out(), "F18");
            case Key::eF19:
                return std::format_to(ctx.out(), "F19");
            case Key::eF20:
                return std::format_to(ctx.out(), "F20");
            case Key::eF21:
                return std::format_to(ctx.out(), "F21");
            case Key::eF22:
                return std::format_to(ctx.out(), "F22");
            case Key::eF23:
                return std::format_to(ctx.out(), "F23");
            case Key::eF24:
                return std::format_to(ctx.out(), "F24");
            case Key::eF25:
                return std::format_to(ctx.out(), "F25");

            // 小键盘键
            case Key::eKP_0:
                return std::format_to(ctx.out(), "KP_0");
            case Key::eKP_1:
                return std::format_to(ctx.out(), "KP_1");
            case Key::eKP_2:
                return std::format_to(ctx.out(), "KP_2");
            case Key::eKP_3:
                return std::format_to(ctx.out(), "KP_3");
            case Key::eKP_4:
                return std::format_to(ctx.out(), "KP_4");
            case Key::eKP_5:
                return std::format_to(ctx.out(), "KP_5");
            case Key::eKP_6:
                return std::format_to(ctx.out(), "KP_6");
            case Key::eKP_7:
                return std::format_to(ctx.out(), "KP_7");
            case Key::eKP_8:
                return std::format_to(ctx.out(), "KP_8");
            case Key::eKP_9:
                return std::format_to(ctx.out(), "KP_9");
            case Key::eKP_DECIMAL:
                return std::format_to(ctx.out(), "KP_DECIMAL");
            case Key::eKP_DIVIDE:
                return std::format_to(ctx.out(), "KP_DIVIDE");
            case Key::eKP_MULTIPLY:
                return std::format_to(ctx.out(), "KP_MULTIPLY");
            case Key::eKP_SUBTRACT:
                return std::format_to(ctx.out(), "KP_SUBTRACT");
            case Key::eKP_ADD:
                return std::format_to(ctx.out(), "KP_ADD");
            case Key::eKP_ENTER:
                return std::format_to(ctx.out(), "KP_ENTER");
            case Key::eKP_EQUAL:
                return std::format_to(ctx.out(), "KP_EQUAL");

            // 修饰键
            case Key::eLEFT_SHIFT:
                return std::format_to(ctx.out(), "LEFT_SHIFT");
            case Key::eLEFT_CONTROL:
                return std::format_to(ctx.out(), "LEFT_CONTROL");
            case Key::eLEFT_ALT:
                return std::format_to(ctx.out(), "LEFT_ALT");
            case Key::eLEFT_SUPER:
                return std::format_to(ctx.out(), "LEFT_SUPER");
            case Key::eRIGHT_SHIFT:
                return std::format_to(ctx.out(), "RIGHT_SHIFT");
            case Key::eRIGHT_CONTROL:
                return std::format_to(ctx.out(), "RIGHT_CONTROL");
            case Key::eRIGHT_ALT:
                return std::format_to(ctx.out(), "RIGHT_ALT");
            case Key::eRIGHT_SUPER:
                return std::format_to(ctx.out(), "RIGHT_SUPER");

            case Key::eSIZE:
                return std::format_to(ctx.out(), "SIZE");
            default:
                return std::format_to(ctx.out(), "KEY_{}", static_cast<int>(key));
            }
        }
    };

    // 为 keyboard_event 添加 formatter
    template <>
    struct formatter<mcs::vulkan::event::keyboard_event>
    {
        using keyboard_event = mcs::vulkan::event::keyboard_event;
        static constexpr auto parse(std::format_parse_context &ctx) noexcept
        {
            return ctx.begin();
        }

        static auto format(const keyboard_event &event, std::format_context &ctx)
        {
            return std::format_to(
                ctx.out(),
                "keyboard_event{{key={}, action={}, modifier={}, scancode={}}}",
                event.key, event.action, event.modifier_key, event.scancode);
        }
    };

    // 为 mousebutton_event 添加 formatter
    template <>
    struct formatter<mcs::vulkan::event::mousebutton_event>
    {
        using mousebutton_event = mcs::vulkan::event::mousebutton_event;
        static constexpr auto parse(std::format_parse_context &ctx) noexcept
        {
            return ctx.begin();
        }

        static auto format(const mousebutton_event &event, std::format_context &ctx)
        {
            return std::format_to(
                ctx.out(), "mousebutton_event{{button={}, action={}, modifier={}}}",
                event.button, event.action, event.modifier_key);
        }
    };

    template <>
    struct formatter<mcs::vulkan::event::scroll_event>
    {
        using scroll_event = mcs::vulkan::event::scroll_event;
        static constexpr auto parse(std::format_parse_context &ctx) noexcept
        {
            return ctx.begin();
        }

        static auto format(const scroll_event &event, std::format_context &ctx)
        {
            return std::format_to(ctx.out(),
                                  "scroll_event{{xoffset={:.2f}, yoffset={:.2f}}}",
                                  event.xoffset, event.yoffset);
        }
    };

    template <>
    struct formatter<mcs::vulkan::event::position2d_event>
    {
        using position2d_event = mcs::vulkan::event::position2d_event;
        static constexpr auto parse(std::format_parse_context &ctx) noexcept
        {
            return ctx.begin();
        }

        static auto format(const position2d_event &event, std::format_context &ctx)
        {
            return std::format_to(ctx.out(),
                                  "position2d_event{{xpos={:.2f}, ypos={:.2f}}}",
                                  event.xpos, event.ypos);
        }
    };

    template <>
    struct formatter<mcs::vulkan::event::cursor_enter_event>
    {
        using cursor_enter_event = mcs::vulkan::event::cursor_enter_event;
        static constexpr auto parse(std::format_parse_context &ctx) noexcept
        {
            return ctx.begin();
        }

        static auto format(const cursor_enter_event &event, std::format_context &ctx)
        {
            return std::format_to(ctx.out(), "cursor_enter_event{{entered={}}}",
                                  event.value);
        }
    };

}; // namespace std

constexpr std::ostream &operator<<(std::ostream &os,
                                   const mcs::vulkan::event::keyboard_event &event)
{
    os << std::format("{}", event);
    return os;
}

constexpr std::ostream &operator<<(std::ostream &os,
                                   const mcs::vulkan::event::mousebutton_event &event)
{
    os << std::format("{}", event);
    return os;
}
constexpr std::ostream &operator<<(std::ostream &os,
                                   const mcs::vulkan::event::scroll_event &event)
{
    os << std::format("{}", event);
    return os;
}

constexpr std::ostream &operator<<(std::ostream &os,
                                   const mcs::vulkan::event::position2d_event &event)
{
    os << std::format("{}", event);
    return os;
}
constexpr std::ostream &operator<<(std::ostream &os,
                                   const mcs::vulkan::event::cursor_enter_event &event)
{
    os << std::format("{}", event);
    return os;
}