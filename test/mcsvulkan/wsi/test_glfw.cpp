#include <cassert>
#include <exception>
#include <iostream>
#include <print>

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

int main()
try
{
    using mcs::vulkan::check_vkresult;
    raii_vulkan ctx{};

    surface window{};
    window.setup({.width = WIDTH, .height = HEIGHT}, TITLE); // NOLINT

    constexpr auto APIVERSION = vkApiVersion(0, 1, 4, 0);
    auto enables = enable_intance_build{}
                       .enableDebugExtension()
                       .enableValidationLayer()
                       .enableSurfaceExtension<surface>();
    enables.check();
    Instance instance =
        create_instance{}
            .setCreateInfo(
                {.applicationInfo = {.pApplicationName = "Hello Triangle",
                                     .applicationVersion = vkMakeVersion(1, 0, 0),
                                     .pEngineName = "No Engine",
                                     .engineVersion = vkMakeVersion(1, 0, 0),
                                     // apiVersion必须是应用程序设计使用的Vulkan的最高版本
                                     .apiVersion = APIVERSION},
                 .enabledLayers = enables.enabledLayers(),
                 .enabledExtensions = enables.enabledExtensions()})
            .build();
    auto debuger = create_debugger{}
                       .setCreateInfo(create_debugger::defaultCreateInfo())
                       .build(instance);

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
    while (window.shouldClose() == 0)
    {
        surface::pollEvents();
    }

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}