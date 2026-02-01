#pragma once

#include <cstdint>
#include <limits>
#include <format>

namespace mcs::vulkan::event
{
    constexpr auto UNDEFINED_double = std::numeric_limits<double>::max(); // NOLINT
    constexpr auto UNDEFINED_int = std::numeric_limits<int>::max();       // NOLINT

    enum class MouseButtons : std::uint8_t
    {
        UNDEFINED,
        MOUSE_BUTTON_1,
        MOUSE_BUTTON_2,
        MOUSE_BUTTON_3,
        MOUSE_BUTTON_4,
        MOUSE_BUTTON_5,
        MOUSE_BUTTON_6,
        MOUSE_BUTTON_7,
        MOUSE_BUTTON_8,
        // 别名化，8bit的映射罢了
        MOUSE_BUTTON_LAST,
        MOUSE_BUTTON_LEFT,
        MOUSE_BUTTON_RIGHT,
        MOUSE_BUTTON_MIDDLE,

        SIZE // NOTE: 数组需要
    };

    enum class Action : std::uint8_t
    {
        UNDEFINED,
        RELEASE, // 释放
        PRESS,   // 按下
        REPEAT   // 重复（长按）
    };

    enum class Key : std::uint8_t
    {
        UNDEFINED,
        // 特殊键
        UNKNOWN,

        // 可打印字符键 - 空格和标点
        SPACE,
        APOSTROPHE,    // '
        COMMA,         // ,
        MINUS,         // -
        PERIOD,        // .
        SLASH,         // /
        SEMICOLON,     // ;
        EQUAL,         // =
        LEFT_BRACKET,  // [
        BACKSLASH,     // '\'
        RIGHT_BRACKET, // ]
        GRAVE_ACCENT,  // `

        // 数字键
        KEY_0,
        KEY_1,
        KEY_2,
        KEY_3,
        KEY_4,
        KEY_5,
        KEY_6,
        KEY_7,
        KEY_8,
        KEY_9,

        // 字母键
        A,
        B,
        C,
        D,
        E,
        F,
        G,
        H,
        I,
        J,
        K,
        L,
        M,
        N,
        O,
        P,
        Q,
        R,
        S,
        T,
        U,
        V,
        W,
        X,
        Y,
        Z,

        // 世界键（非美式布局特殊键）
        WORLD_1,
        WORLD_2,

        // 编辑与导航键
        ESCAPE,
        ENTER,
        TAB,
        BACKSPACE,
        INSERT,
        DELETE,
        RIGHT,
        LEFT,
        DOWN,
        UP,
        PAGE_UP,
        PAGE_DOWN,
        HOME,
        END,

        // 锁定键
        CAPS_LOCK,
        SCROLL_LOCK,
        NUM_LOCK,

        // 系统键
        PRINT_SCREEN,
        PAUSE,
        MENU,

        // 功能键 F1-F25
        F1,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,
        F13,
        F14,
        F15,
        F16,
        F17,
        F18,
        F19,
        F20,
        F21,
        F22,
        F23,
        F24,
        F25,

        // 小键盘键
        KP_0,
        KP_1,
        KP_2,
        KP_3,
        KP_4,
        KP_5,
        KP_6,
        KP_7,
        KP_8,
        KP_9,
        KP_DECIMAL,  // .
        KP_DIVIDE,   // /
        KP_MULTIPLY, // *
        KP_SUBTRACT, // -
        KP_ADD,      // +
        KP_ENTER,
        KP_EQUAL, // =

        // 修饰键
        LEFT_SHIFT,
        LEFT_CONTROL,
        LEFT_ALT,
        LEFT_SUPER, // Windows键 / Command键
        RIGHT_SHIFT,
        RIGHT_CONTROL,
        RIGHT_ALT,
        RIGHT_SUPER,

        SIZE // NOTE: 数组需要
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
        Key key{Key::UNDEFINED};                       // NOLINT
        Action action{Action::UNDEFINED};              // NOLINT
        ModifierKey modifier_key{ModifierKey::None()}; // NOLINT
        int scancode{UNDEFINED_int};                   // NOLINT

        static_assert(sizeof(key) == sizeof(std::uint8_t));
        [[nodiscard]] bool press() const noexcept
        {
            return action == Action::PRESS;
        }
        [[nodiscard]] bool release() const noexcept
        {
            return action == Action::RELEASE;
        }
        [[nodiscard]] bool repeat() const noexcept
        {
            return action == Action::REPEAT;
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

        MouseButtons button{MouseButtons::UNDEFINED};  // NOLINT
        Action action{Action::UNDEFINED};              // NOLINT
        ModifierKey modifier_key{ModifierKey::None()}; // NOLINT

        static_assert(sizeof(button) == sizeof(std::uint8_t));

        [[nodiscard]] bool press() const noexcept
        {
            return action == Action::PRESS;
        }
        [[nodiscard]] bool release() const noexcept
        {
            return action == Action::RELEASE;
        }
        [[nodiscard]] bool repeat() const noexcept
        {
            return action == Action::REPEAT;
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
            case MouseButtons::UNDEFINED:
                return std::format_to(ctx.out(), "UNDEFINED");
            case MouseButtons::MOUSE_BUTTON_1:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_1");
            case MouseButtons::MOUSE_BUTTON_2:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_2");
            case MouseButtons::MOUSE_BUTTON_3:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_3");
            case MouseButtons::MOUSE_BUTTON_4:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_4");
            case MouseButtons::MOUSE_BUTTON_5:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_5");
            case MouseButtons::MOUSE_BUTTON_6:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_6");
            case MouseButtons::MOUSE_BUTTON_7:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_7");
            case MouseButtons::MOUSE_BUTTON_8:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_8");
            case MouseButtons::MOUSE_BUTTON_LEFT:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_LEFT");
            case MouseButtons::MOUSE_BUTTON_RIGHT:
                return std::format_to(ctx.out(), "MOUSE_BUTTON_RIGHT");
            case MouseButtons::MOUSE_BUTTON_MIDDLE:
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
            case Action::UNDEFINED:
                return std::format_to(ctx.out(), "UNDEFINED");
            case Action::RELEASE:
                return std::format_to(ctx.out(), "RELEASE");
            case Action::PRESS:
                return std::format_to(ctx.out(), "PRESS");
            case Action::REPEAT:
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
            case Key::UNDEFINED:
                return std::format_to(ctx.out(), "UNDEFINED");
            case Key::UNKNOWN:
                return std::format_to(ctx.out(), "UNKNOWN");

            // 可打印字符键 - 空格和标点
            case Key::SPACE:
                return std::format_to(ctx.out(), "SPACE");
            case Key::APOSTROPHE:
                return std::format_to(ctx.out(), "APOSTROPHE");
            case Key::COMMA:
                return std::format_to(ctx.out(), "COMMA");
            case Key::MINUS:
                return std::format_to(ctx.out(), "MINUS");
            case Key::PERIOD:
                return std::format_to(ctx.out(), "PERIOD");
            case Key::SLASH:
                return std::format_to(ctx.out(), "SLASH");
            case Key::SEMICOLON:
                return std::format_to(ctx.out(), "SEMICOLON");
            case Key::EQUAL:
                return std::format_to(ctx.out(), "EQUAL");
            case Key::LEFT_BRACKET:
                return std::format_to(ctx.out(), "LEFT_BRACKET");
            case Key::BACKSLASH:
                return std::format_to(ctx.out(), "BACKSLASH");
            case Key::RIGHT_BRACKET:
                return std::format_to(ctx.out(), "RIGHT_BRACKET");
            case Key::GRAVE_ACCENT:
                return std::format_to(ctx.out(), "GRAVE_ACCENT");

            // 数字键
            case Key::KEY_0:
                return std::format_to(ctx.out(), "KEY_0");
            case Key::KEY_1:
                return std::format_to(ctx.out(), "KEY_1");
            case Key::KEY_2:
                return std::format_to(ctx.out(), "KEY_2");
            case Key::KEY_3:
                return std::format_to(ctx.out(), "KEY_3");
            case Key::KEY_4:
                return std::format_to(ctx.out(), "KEY_4");
            case Key::KEY_5:
                return std::format_to(ctx.out(), "KEY_5");
            case Key::KEY_6:
                return std::format_to(ctx.out(), "KEY_6");
            case Key::KEY_7:
                return std::format_to(ctx.out(), "KEY_7");
            case Key::KEY_8:
                return std::format_to(ctx.out(), "KEY_8");
            case Key::KEY_9:
                return std::format_to(ctx.out(), "KEY_9");

            // 字母键
            case Key::A:
                return std::format_to(ctx.out(), "A");
            case Key::B:
                return std::format_to(ctx.out(), "B");
            case Key::C:
                return std::format_to(ctx.out(), "C");
            case Key::D:
                return std::format_to(ctx.out(), "D");
            case Key::E:
                return std::format_to(ctx.out(), "E");
            case Key::F:
                return std::format_to(ctx.out(), "F");
            case Key::G:
                return std::format_to(ctx.out(), "G");
            case Key::H:
                return std::format_to(ctx.out(), "H");
            case Key::I:
                return std::format_to(ctx.out(), "I");
            case Key::J:
                return std::format_to(ctx.out(), "J");
            case Key::K:
                return std::format_to(ctx.out(), "K");
            case Key::L:
                return std::format_to(ctx.out(), "L");
            case Key::M:
                return std::format_to(ctx.out(), "M");
            case Key::N:
                return std::format_to(ctx.out(), "N");
            case Key::O:
                return std::format_to(ctx.out(), "O");
            case Key::P:
                return std::format_to(ctx.out(), "P");
            case Key::Q:
                return std::format_to(ctx.out(), "Q");
            case Key::R:
                return std::format_to(ctx.out(), "R");
            case Key::S:
                return std::format_to(ctx.out(), "S");
            case Key::T:
                return std::format_to(ctx.out(), "T");
            case Key::U:
                return std::format_to(ctx.out(), "U");
            case Key::V:
                return std::format_to(ctx.out(), "V");
            case Key::W:
                return std::format_to(ctx.out(), "W");
            case Key::X:
                return std::format_to(ctx.out(), "X");
            case Key::Y:
                return std::format_to(ctx.out(), "Y");
            case Key::Z:
                return std::format_to(ctx.out(), "Z");

            // 世界键
            case Key::WORLD_1:
                return std::format_to(ctx.out(), "WORLD_1");
            case Key::WORLD_2:
                return std::format_to(ctx.out(), "WORLD_2");

            // 编辑与导航键
            case Key::ESCAPE:
                return std::format_to(ctx.out(), "ESCAPE");
            case Key::ENTER:
                return std::format_to(ctx.out(), "ENTER");
            case Key::TAB:
                return std::format_to(ctx.out(), "TAB");
            case Key::BACKSPACE:
                return std::format_to(ctx.out(), "BACKSPACE");
            case Key::INSERT:
                return std::format_to(ctx.out(), "INSERT");
            case Key::DELETE:
                return std::format_to(ctx.out(), "DELETE");
            case Key::RIGHT:
                return std::format_to(ctx.out(), "RIGHT");
            case Key::LEFT:
                return std::format_to(ctx.out(), "LEFT");
            case Key::DOWN:
                return std::format_to(ctx.out(), "DOWN");
            case Key::UP:
                return std::format_to(ctx.out(), "UP");
            case Key::PAGE_UP:
                return std::format_to(ctx.out(), "PAGE_UP");
            case Key::PAGE_DOWN:
                return std::format_to(ctx.out(), "PAGE_DOWN");
            case Key::HOME:
                return std::format_to(ctx.out(), "HOME");
            case Key::END:
                return std::format_to(ctx.out(), "END");

            // 锁定键
            case Key::CAPS_LOCK:
                return std::format_to(ctx.out(), "CAPS_LOCK");
            case Key::SCROLL_LOCK:
                return std::format_to(ctx.out(), "SCROLL_LOCK");
            case Key::NUM_LOCK:
                return std::format_to(ctx.out(), "NUM_LOCK");

            // 系统键
            case Key::PRINT_SCREEN:
                return std::format_to(ctx.out(), "PRINT_SCREEN");
            case Key::PAUSE:
                return std::format_to(ctx.out(), "PAUSE");
            case Key::MENU:
                return std::format_to(ctx.out(), "MENU");

            // 功能键
            case Key::F1:
                return std::format_to(ctx.out(), "F1");
            case Key::F2:
                return std::format_to(ctx.out(), "F2");
            case Key::F3:
                return std::format_to(ctx.out(), "F3");
            case Key::F4:
                return std::format_to(ctx.out(), "F4");
            case Key::F5:
                return std::format_to(ctx.out(), "F5");
            case Key::F6:
                return std::format_to(ctx.out(), "F6");
            case Key::F7:
                return std::format_to(ctx.out(), "F7");
            case Key::F8:
                return std::format_to(ctx.out(), "F8");
            case Key::F9:
                return std::format_to(ctx.out(), "F9");
            case Key::F10:
                return std::format_to(ctx.out(), "F10");
            case Key::F11:
                return std::format_to(ctx.out(), "F11");
            case Key::F12:
                return std::format_to(ctx.out(), "F12");
            case Key::F13:
                return std::format_to(ctx.out(), "F13");
            case Key::F14:
                return std::format_to(ctx.out(), "F14");
            case Key::F15:
                return std::format_to(ctx.out(), "F15");
            case Key::F16:
                return std::format_to(ctx.out(), "F16");
            case Key::F17:
                return std::format_to(ctx.out(), "F17");
            case Key::F18:
                return std::format_to(ctx.out(), "F18");
            case Key::F19:
                return std::format_to(ctx.out(), "F19");
            case Key::F20:
                return std::format_to(ctx.out(), "F20");
            case Key::F21:
                return std::format_to(ctx.out(), "F21");
            case Key::F22:
                return std::format_to(ctx.out(), "F22");
            case Key::F23:
                return std::format_to(ctx.out(), "F23");
            case Key::F24:
                return std::format_to(ctx.out(), "F24");
            case Key::F25:
                return std::format_to(ctx.out(), "F25");

            // 小键盘键
            case Key::KP_0:
                return std::format_to(ctx.out(), "KP_0");
            case Key::KP_1:
                return std::format_to(ctx.out(), "KP_1");
            case Key::KP_2:
                return std::format_to(ctx.out(), "KP_2");
            case Key::KP_3:
                return std::format_to(ctx.out(), "KP_3");
            case Key::KP_4:
                return std::format_to(ctx.out(), "KP_4");
            case Key::KP_5:
                return std::format_to(ctx.out(), "KP_5");
            case Key::KP_6:
                return std::format_to(ctx.out(), "KP_6");
            case Key::KP_7:
                return std::format_to(ctx.out(), "KP_7");
            case Key::KP_8:
                return std::format_to(ctx.out(), "KP_8");
            case Key::KP_9:
                return std::format_to(ctx.out(), "KP_9");
            case Key::KP_DECIMAL:
                return std::format_to(ctx.out(), "KP_DECIMAL");
            case Key::KP_DIVIDE:
                return std::format_to(ctx.out(), "KP_DIVIDE");
            case Key::KP_MULTIPLY:
                return std::format_to(ctx.out(), "KP_MULTIPLY");
            case Key::KP_SUBTRACT:
                return std::format_to(ctx.out(), "KP_SUBTRACT");
            case Key::KP_ADD:
                return std::format_to(ctx.out(), "KP_ADD");
            case Key::KP_ENTER:
                return std::format_to(ctx.out(), "KP_ENTER");
            case Key::KP_EQUAL:
                return std::format_to(ctx.out(), "KP_EQUAL");

            // 修饰键
            case Key::LEFT_SHIFT:
                return std::format_to(ctx.out(), "LEFT_SHIFT");
            case Key::LEFT_CONTROL:
                return std::format_to(ctx.out(), "LEFT_CONTROL");
            case Key::LEFT_ALT:
                return std::format_to(ctx.out(), "LEFT_ALT");
            case Key::LEFT_SUPER:
                return std::format_to(ctx.out(), "LEFT_SUPER");
            case Key::RIGHT_SHIFT:
                return std::format_to(ctx.out(), "RIGHT_SHIFT");
            case Key::RIGHT_CONTROL:
                return std::format_to(ctx.out(), "RIGHT_CONTROL");
            case Key::RIGHT_ALT:
                return std::format_to(ctx.out(), "RIGHT_ALT");
            case Key::RIGHT_SUPER:
                return std::format_to(ctx.out(), "RIGHT_SUPER");

            case Key::SIZE:
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