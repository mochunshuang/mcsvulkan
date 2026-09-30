#include <cassert>
#include <exception>
#include <iostream>
#include <print>
#include <chrono>

#include "../head.hpp"

using Instance = mcs::vulkan::Instance;
using create_instance = mcs::vulkan::tool::create_instance;
using create_debugger = mcs::vulkan::tool::create_debugger;
using mcs::vulkan::vkMakeVersion;
using mcs::vulkan::vkApiVersion;

using mcs::vulkan::tool::enable_intance_build;

using mcs::vulkan::raii_vulkan;

using surface = mcs::vulkan::wsi::glfw::Window;

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr auto TITLE = "test_my_triangle";

using mcs::vulkan::input::glfw_input;
using mcs::vulkan::input::glfw_window_state;

namespace
{
    struct char_drop_test
    {
        char_drop_test() noexcept
        {
            mcs::vulkan::event::char_event_dispatcher::instance().subscribe(
                this, &char_drop_test::on_char);
            mcs::vulkan::event::drop_event_dispatcher::instance().subscribe(
                this, &char_drop_test::on_drop);
        }
        ~char_drop_test() noexcept
        {
            mcs::vulkan::event::char_event_dispatcher::instance().unsubscribe(
                this, &char_drop_test::on_char);
            mcs::vulkan::event::drop_event_dispatcher::instance().unsubscribe(
                this, &char_drop_test::on_drop);
        }
        char_drop_test(const char_drop_test &) = delete;
        char_drop_test(char_drop_test &&) = delete;
        char_drop_test &operator=(const char_drop_test &) = delete;
        char_drop_test &operator=(char_drop_test &&) = delete;

        // ── char 回调 ─────────────────────────────
        static void on_char(void * /*self*/, mcs::vulkan::event::char_event e) noexcept
        {
            std::println("[char] U+{:04X}", e.codepoint);
        }

        // ── drop 回调 ─────────────────────────────
        static void on_drop(void * /*self*/, mcs::vulkan::event::drop_event e) noexcept
        {
            std::println("[drop] count={}", e.count);
            for (int i = 0; i < e.count; ++i)
                std::println("  path[{}] = {}", i,
                             e.paths[i] != nullptr ? e.paths[i] : "<null>");
        }
    };
} // namespace

using mcs::vulkan::event::keyboard_event;
using mcs::vulkan::event::scroll_event;
using mcs::vulkan::event::mousebutton_event;
using mcs::vulkan::event::position2d_event;
using mcs::vulkan::event::cursor_enter_event;

using mcs::vulkan::event::Key;
using mcs::vulkan::event::MouseButtons;
using mcs::vulkan::event::ModifierKey;
using mcs::vulkan::event::Action;

template <Key key>
struct key_press
{
    constexpr auto operator()(const glfw_input &input) const noexcept
    {
        return input.isKeyPressed(key);
    }
};
template <Key key>
struct key_repeat
{
    constexpr auto operator()(const glfw_input &input) const noexcept
    {
        return input.isKeyRepeat(key);
    }
};
template <Key key>
struct key_press_or_repeat
{
    constexpr auto operator()(const glfw_input &input) const noexcept
    {
        return input.isKeyPressedOrRepeat(key);
    }
};
template <Key key, ModifierKey::Value... m>
struct key_has_modifiers
{
    constexpr auto operator()(const glfw_input &input) const noexcept
    {
        const auto &event = input.get_keyboard_event(key);
        return event.template hasModifiers<m...>();
    }
};

enum class key_status : std::uint8_t
{
    ePRESS,
    eRELEASE,
    eREPEAT,
    ePRESS_OR_REPEAT,
};
template <key_status s, ModifierKey::Value... m>
static constexpr auto key_probe(const glfw_input &input, Key key) noexcept
{
    using enum key_status;
    const auto &event = input.get_keyboard_event(key);

    bool status = {};
    if constexpr (s == ePRESS)
        status = event.press();
    else if constexpr (s == eRELEASE)
        status = event.release();
    else if constexpr (s == eREPEAT)
        status = event.repeat();
    else
        status = event.press() || event.repeat();
    bool modifier = sizeof...(m) == 0 ? true : event.template hasModifiers<m...>();
    return status && modifier;
}

enum class mouse_button_status : std::uint8_t
{
    ePRESS,
    eRELEASE
};
template <mouse_button_status s, ModifierKey::Value... m>
static constexpr auto mouse_button_probe(const glfw_input &input,
                                         MouseButtons key) noexcept
{
    using enum mouse_button_status;
    const auto &event = input.get_mousebutton_event(key);
    bool status = {};
    if constexpr (s == ePRESS)
        status = event.press();
    else
        status = event.release();
    bool modifier = sizeof...(m) == 0 ? true : event.template hasModifiers<m...>();
    return status && modifier;
}

// ─────────────────────────────────────────────────────────────
// 编译期手势参数：不占运行时内存，不可被意外修改
// ─────────────────────────────────────────────────────────────
struct gesture_config
{
    std::chrono::milliseconds click_max_time{250};       // 单击最长时长
    std::chrono::milliseconds multi_click_interval{500}; // 多击最大间隔
    std::chrono::milliseconds long_press_time{500};      // 长按阈值

    double click_max_move = 4.0;       // 单击最大位移
    double multi_click_max_dist = 4.0; // 多击位置漂移容忍
    double long_press_max_move = 4.0;  // 长按期间最大位移
    double drag_min_dist = 4.0;        // 触发拖拽的最小位移
};
inline constexpr gesture_config kGesture{};

class event_manager
{
  public:
    explicit event_manager(glfw_input &input) noexcept : input_{input}
    {
        input_.subscribe<glfw_input ::keyboard_change_fn>(this,
                                                          &event_manager::on_keyboard);
        input_.subscribe<glfw_input ::mousebutton_change_fn>(
            this, &event_manager::on_mousebutton);
        input_.subscribe<glfw_input ::scroll_event_change_fn>(this,
                                                              &event_manager::on_scroll);
    }

    ~event_manager() noexcept
    {
        input_.unsubscribe<glfw_input ::keyboard_change_fn>(this,
                                                            &event_manager::on_keyboard);
        input_.unsubscribe<glfw_input ::mousebutton_change_fn>(
            this, &event_manager::on_mousebutton);
        input_.unsubscribe<glfw_input ::scroll_event_change_fn>(
            this, &event_manager::on_scroll);
    }

    event_manager(const event_manager &) = delete;
    event_manager(event_manager &&) = delete;
    event_manager &operator=(const event_manager &) = delete;
    event_manager &operator=(event_manager &&) = delete;

  private:
    struct button_tracker
    {
        bool down = false;
        position2d_event down_pos{};
        std::chrono::steady_clock::time_point down_time{};

        std::uint32_t click_count = 0;
        position2d_event last_click_pos{};
        std::chrono::steady_clock::time_point last_click_time{};
    };

    // 编译期工具：平方，省一次开方
    static constexpr double sq(double v) noexcept
    {
        return v * v;
    }
    static constexpr double dist_sq(position2d_event a, position2d_event b) noexcept
    {
        return sq(a.xpos - b.xpos) + sq(a.ypos - b.ypos);
    }

    // ── 回调 ────────────────────────────────────────────────
    static void on_keyboard(void * /*self*/, position2d_event,
                            std::chrono::steady_clock::time_point,
                            keyboard_event e) noexcept
    {
        std::println("[keyboard] {}", e);
    }

    static void on_scroll(void * /*self*/, position2d_event,
                          std::chrono::steady_clock::time_point, scroll_event e) noexcept
    {
        std::println("[scroll] x={} y={}", e.xoffset, e.yoffset);
    }

    static void on_mousebutton(void *self, position2d_event pos,
                               std::chrono::steady_clock::time_point t,
                               mousebutton_event e) noexcept
    {
        auto &s = static_cast<event_manager *>(self)
                      ->trackers_[static_cast<std::size_t>(e.button)];

        // ── 按下：仅记录起点 ─────────────────────────────
        if (e.press())
        {
            s.down = true;
            s.down_pos = pos;
            s.down_time = t;
            return;
        }
        if (!e.release() || !s.down)
            return;

        s.down = false;

        const auto held = t - s.down_time;
        const double moved2 = dist_sq(pos, s.down_pos);

        // ── 1. 长按：够久 + 几乎没动 ─────────────────────
        if (held >= kGesture.long_press_time &&
            moved2 <= sq(kGesture.long_press_max_move))
        {
            std::println("[long_press] button={}", static_cast<int>(e.button));
            s.click_count = 0;
            return;
        }

        // ── 2. 拖拽：位移够大 ────────────────────────────
        if (moved2 >= sq(kGesture.drag_min_dist))
        {
            std::println("[drag_end] button={}", static_cast<int>(e.button));
            s.click_count = 0;
            return;
        }

        // ── 3. 点击 / 多击 ──────────────────────────────
        if (held <= kGesture.click_max_time && moved2 <= sq(kGesture.click_max_move))
        {
            const bool chained =
                s.click_count > 0 &&
                (t - s.last_click_time) <= kGesture.multi_click_interval &&
                dist_sq(pos, s.last_click_pos) <= sq(kGesture.multi_click_max_dist);

            s.click_count = chained ? s.click_count + 1u : 1u;
            s.last_click_time = t;
            s.last_click_pos = pos;

            std::println("[click] button={} count={}", static_cast<int>(e.button),
                         s.click_count);
        }
    }

    std::array<button_tracker, static_cast<std::size_t>(MouseButtons::eSIZE)> trackers_{};
    glfw_input &input_;
};

int main()
try
{
    using mcs::vulkan::check_vkresult;
    raii_vulkan ctx{};

    surface window{};
    window.setup({.width = WIDTH, .height = HEIGHT}, TITLE); // NOLINT

    /*
NOTE: 进入窗口和移除窗口的事件
cursorEnter: cursor_enter_event{entered=true}
cursorEnter: cursor_enter_event{entered=false}

NOTE: 鼠标点击click的过程
mouse: mousebutton_event{button=MOUSE_BUTTON_LEFT, action=PRESS, modifier=NONE}
mouse: mousebutton_event{button=MOUSE_BUTTON_LEFT, action=RELEASE, modifier=NONE}

NOTE: 键盘click的过程
key: keyboard_event{key=A, action=PRESS, modifier=NONE, scancode=30}
key: keyboard_event{key=A, action=RELEASE, modifier=NONE, scancode=30}

NOTE: 长按鼠标左键到松开的过程
mouse: mousebutton_event{button=MOUSE_BUTTON_LEFT, action=PRESS, modifier=NONE}
mouse: mousebutton_event{button=MOUSE_BUTTON_LEFT, action=RELEASE, modifier=NONE}

NOTE: 长按键盘按键
key: keyboard_event{key=B, action=PRESS, modifier=NONE, scancode=48}
key: keyboard_event{key=B, action=REPEAT, modifier=NONE, scancode=48}
............................... 重复 REPEAT........................
key: keyboard_event{key=B, action=RELEASE, modifier=NONE, scancode=48}

NOTE: 长按ctrl + A 。 总过程有几秒。 触发了： modifier 不为空。 可以知道 REPEAT 总是指向 最好按的
key: keyboard_event{key=LEFT_CONTROL, action=PRESS, modifier=CONTROL, scancode=29}
key: keyboard_event{key=LEFT_CONTROL, action=REPEAT, modifier=CONTROL, scancode=29}
.................. 重复 上一条 ..............................
key: keyboard_event{key=A, action=PRESS, modifier=CONTROL, scancode=30}
key: keyboard_event{key=A, action=REPEAT, modifier=CONTROL, scancode=30}
.................. 重复 上一条 ..............................
key: keyboard_event{key=A, action=RELEASE, modifier=CONTROL, scancode=30}
key: keyboard_event{key=LEFT_CONTROL, action=RELEASE, modifier=NONE, scancode=29}

NOTE: 长按ctrl + shift + A 。 可以看出 modifier 的值总是正确的。但是只有 | 总共按下了，如果需要顺序，需要状态机
key: keyboard_event{key=LEFT_CONTROL, action=PRESS, modifier=CONTROL, scancode=29}
key: keyboard_event{key=LEFT_CONTROL, action=REPEAT, modifier=CONTROL, scancode=29}
.................. 重复 上一条 ..............................
key: keyboard_event{key=LEFT_SHIFT, action=PRESS, modifier=SHIFT|CONTROL, scancode=42}
key: keyboard_event{key=A, action=PRESS, modifier=SHIFT|CONTROL, scancode=30} //NOTE: 两个按键按下的时间间隔太短是没有 REPEAT 的
key: keyboard_event{key=A, action=REPEAT, modifier=SHIFT|CONTROL, scancode=30}
key: keyboard_event{key=A, action=REPEAT, modifier=SHIFT|CONTROL, scancode=30}
.................. 重复 上一条 ..............................
key: keyboard_event{key=A, action=RELEASE, modifier=SHIFT|CONTROL, scancode=30}
key: keyboard_event{key=LEFT_SHIFT, action=RELEASE, modifier=CONTROL, scancode=42}
key: keyboard_event{key=LEFT_CONTROL, action=RELEASE, modifier=NONE, scancode=29}

*/
    auto input = glfw_input{};

    /*
// NOTE: 暂时不知道做什么
window_size_event: window_size_event{width=0, height=0}
window_pos_event: window_pos_event{x=560, y=240}
window_iconify_event: window_iconify_event{iconified=false}

window_maximize_event: window_maximize_event{maximized=false}
window_maximize_event: window_maximize_event{maximized=true}
*/
    auto inpwindow_stateut = glfw_window_state{};

    /*
// NOTE: 英文看起来没区别，但是中文 就不一样了。
// NOTE: 中文微软 输入法  打印： 你 = U+4F60，好 = U+597D。两个字，两个 char，中间没有任何 keyboard 事件对应。 没有 NIHAO空格的key事件
cursorEnter: cursor_enter_event{entered=true}
cursorPos: position2d_event{xpos=771.00, ypos=170.00}
[char] U+4F60
[char] U+597D

// NOTE: 英文下的 NIHAO
key: keyboard_event{key=N, action=PRESS, modifier=NONE, scancode=49}
[char] U+006E
key: keyboard_event{key=N, action=RELEASE, modifier=NONE, scancode=49}
key: keyboard_event{key=I, action=PRESS, modifier=NONE, scancode=23}
[char] U+0069
key: keyboard_event{key=I, action=RELEASE, modifier=NONE, scancode=23}
key: keyboard_event{key=H, action=PRESS, modifier=NONE, scancode=35}
[char] U+0068
key: keyboard_event{key=H, action=RELEASE, modifier=NONE, scancode=35}
key: keyboard_event{key=A, action=PRESS, modifier=NONE, scancode=30}
[char] U+0061
key: keyboard_event{key=A, action=RELEASE, modifier=NONE, scancode=30}
key: keyboard_event{key=O, action=PRESS, modifier=NONE, scancode=24}
[char] U+006F
key: keyboard_event{key=O, action=RELEASE, modifier=NONE, scancode=24}
key: keyboard_event{key=SPACE, action=PRESS, modifier=NONE, scancode=57}
[char] U+0020
key: keyboard_event{key=SPACE, action=RELEASE, modifier=NONE, scancode=57}

// NOTE: 按 ctrl + a 和 ctrl + c; 不会作为 字符回调使用。确实很智能
key: keyboard_event{key=LEFT_CONTROL, action=REPEAT, modifier=CONTROL, scancode=29}
key: keyboard_event{key=LEFT_CONTROL, action=REPEAT, modifier=CONTROL, scancode=29}
key: keyboard_event{key=A, action=PRESS, modifier=CONTROL, scancode=30}
key: keyboard_event{key=A, action=RELEASE, modifier=CONTROL, scancode=30}
key: keyboard_event{key=LEFT_CONTROL, action=RELEASE, modifier=NONE, scancode=29}
key: keyboard_event{key=LEFT_CONTROL, action=PRESS, modifier=CONTROL, scancode=29}
key: keyboard_event{key=C, action=PRESS, modifier=CONTROL, scancode=46}
key: keyboard_event{key=C, action=RELEASE, modifier=CONTROL, scancode=46}
key: keyboard_event{key=LEFT_CONTROL, action=RELEASE, modifier=NONE, scancode=29}

// NOTE: drop 确实： 拖动文件 被感知到了。 ctrl + 选中即可
cursorEnter: cursor_enter_event{entered=false}
window_focus_event: window_focus_event{focused=false}
cursorPos: position2d_event{xpos=357.00, ypos=284.00}
[drop] count=2
  path[0] = C:\Users\mcs\Documents\PhysicalDataModel_1.pdm
  path[1] = C:\Users\mcs\Documents\Full Physical Report.rtf
cursorEnter: cursor_enter_event{entered=true}

*/
    auto charDropTest = char_drop_test{};
    struct ordinary_ctrl_shift_a
    {
        constexpr auto operator()(const glfw_input &input) const noexcept
        {
            return (input.isKeyPressedOrRepeat(Key::eLEFT_CONTROL) ||
                    input.isKeyPressedOrRepeat(Key::eRIGHT_CONTROL)) &&
                   (input.isKeyPressedOrRepeat(Key::eLEFT_SHIFT) ||
                    input.isKeyPressedOrRepeat(Key::eRIGHT_SHIFT)) &&
                   input.isKeyPressedOrRepeat(Key::eA);
        }
    };
    struct ctrl_shift_a
    {
        constexpr auto operator()(const glfw_input &input) const noexcept
        {
            const auto &event = input.get_keyboard_event(Key::eA);
            return (event.press() || event.repeat()) &&
                   event.hasModifiers<ModifierKey::eSHIFT, ModifierKey::eCONTROL>();
        }
    };
    struct ctrl_shift_a_2
    {
        constexpr auto operator()(const glfw_input &input) const noexcept
        {
            return key_press_or_repeat<Key::eA>{}(input) &&
                   key_has_modifiers<Key::eA, ModifierKey::eSHIFT,
                                     ModifierKey::eCONTROL>{}(input);
        }
    };

    // NOTE: 订阅有状态。变化的状态
    event_manager eventManager{input};

    while (window.shouldClose() == 0)
    {
#if 0
        if (ordinary_ctrl_shift_a{}(input))
        {
            std::println("ordinary_ctrl_shift_a: ctrl_shift_a: true");
        }
        if (ctrl_shift_a{}(input))
        {
            std::println("ctrl_shift_a: ctrl_shift_a: true");
        }
        if (ctrl_shift_a_2{}(input))
        {
            std::println("ctrl_shift_a_2: ctrl_shift_a: true");
        }
        if (key_probe<key_status::ePRESS_OR_REPEAT, ModifierKey::eSHIFT,
                      ModifierKey::eCONTROL>(input, Key::eA))
        {
            std::println("key_probe: ctrl_shift_a: true");
        }
        if (mouse_button_probe<mouse_button_status::ePRESS>(
                input, MouseButtons::eMOUSE_BUTTON_LEFT))
        {
            std::println("mouse_button_probe: MOUSE_BUTTON_LEFT press");
        }
        // NOTE: 可能是BUG 因为旧值 不会被覆盖。 如果click 是一对 press + release ，release才触发信号，这可能就是BUG
        if (mouse_button_probe<mouse_button_status::eRELEASE>(
                input, MouseButtons::eMOUSE_BUTTON_LEFT))
        {
            std::println("mouse_button_probe: MOUSE_BUTTON_LEFT RELEASE");
        }
#endif
        surface::pollEvents();
    }

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}