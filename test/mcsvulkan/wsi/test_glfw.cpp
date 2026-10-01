#include <cassert>
#include <exception>
#include <iostream>
#include <print>
#include <chrono>

// UI 层（ui/*.hpp）用到的标准库
#include <algorithm>
#include <any>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "head.hpp"

// UI 层：来源层 → 识别层 → 裁决层 → 订阅层（控件树 + 可在运行时替换的裁决层）
#include "ui/ui_widget.hpp"

// 无窗口自测：13 个确定性用例，断言整条链的语义
#include "ui/self_test.hpp"

using surface = mcs::vulkan::wsi::glfw::Window;

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr auto TITLE = "test_my_triangle";

using mcs::vulkan::input::glfw_input;
using mcs::vulkan::input::glfw_window_state;
using mcs::vulkan::input::glfw_forward;

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

namespace ui = mcs::vulkan::ui;

namespace
{
    // ══════════════════════════════════════════════════════════════════════
    // 演示程序：控件树（命中测试）+ 每个控件绑定的识别器 + 各自重写的虚函数
    //
    //   来源层：glfw_input 的三条回调通道（键盘 / 鼠标键 / 滚轮）+ 每帧轮询光标位置
    //   识别层：控件在构造里 add_recognizer<...>() 绑上自己关心的手势
    //   裁决层：ui_input 按命中链选择裁决层（编辑器区域自带另一套）
    //   订阅层：胜出结构落到 on_tap / on_drag_* / on_shortcut ... 上，改状态
    //
    // 三个“同一个动作、不同结果”的对照：
    //   1) 右键点空白 vs 右键点编辑器：命中链不同 → 虚函数不同 → 菜单不同
    //   2) 右键拖空白 vs 右键拖编辑器：同一形状的命中链，但裁决层不同 → 赢家不同
    //      （桌面把候选特异性写到 25；默认按特异性裁决时它会盖住编辑器，
    //        而编辑器区域换成 deepest_first_arena —— 谁离用户近谁说了算）
    //   3) 单击/双击/长按：同一个槽位上的强/弱候选竞争，最终只有一个胜出
    // ══════════════════════════════════════════════════════════════════════

    // ── 按钮：单击点亮/熄灭，长按复位，hover 高亮 ──
    class button_widget : public ui::widget
    {
      public:
        button_widget() : widget{"button", {40, 40, 160, 48}}
        {
            add_recognizer<ui::tap_recognizer>(ui::kGesture);
            add_recognizer<ui::long_press_recognizer>(ui::kGesture);
        }

        bool on_tap(const ui::tap_gesture &g) override
        {
            if (g.button != MouseButtons::eMOUSE_BUTTON_LEFT)
                return false;
            if (g.count != 1)
            {
                // 双击/三击：按钮选择“消费但不处理” —— 顺带演示返回值 = 是否已消费
                std::println("[state] button 双击被消费（控件自己不处理，也不往上传）");
                return true;
            }
            active_ = !active_;
            std::println("[state] button.active = {}  {}", active_, ui::pos_str(g.pos));
            return true;
        }

        bool on_long_press(const ui::long_press_gesture &g) override
        {
            if (g.button != MouseButtons::eMOUSE_BUTTON_LEFT)
                return false;
            active_ = false;
            std::println("[state] button 被长按 → 复位 active=false");
            return true;
        }

        void on_hover_enter() override
        {
            ui::widget::on_hover_enter();
            std::println("[state] button.hover = true");
        }

        void on_hover_leave() override
        {
            ui::widget::on_hover_leave();
            std::println("[state] button.hover = false");
        }

      private:
        bool active_{false};
    };

    // ── 画布：左键拖动移动物体（捕获后的 update 流），Ctrl+滚轮缩放，双击复位 ──
    class canvas_widget : public ui::widget
    {
      public:
        canvas_widget() : widget{"canvas", {230, 40, 530, 220}}
        {
            add_recognizer<ui::tap_recognizer>(ui::kGesture);
            add_recognizer<ui::drag_recognizer>(ui::kGesture);
            add_recognizer<ui::zoom_recognizer>();
        }

        bool on_drag_begin(const ui::drag_gesture &g) override
        {
            if (g.button != MouseButtons::eMOUSE_BUTTON_LEFT)
                return false;
            grab_x_ = offset_x_;
            grab_y_ = offset_y_;
            std::println("[state] canvas 拖动开始 from={}", ui::pos_str(g.from));
            return true;
        }

        bool on_drag_update(const ui::drag_gesture &g) override
        {
            if (g.button != MouseButtons::eMOUSE_BUTTON_LEFT)
                return false;
            offset_x_ = grab_x_ + g.total_dx;
            offset_y_ = grab_y_ + g.total_dy;
            std::println("[state] canvas 拖动 → offset=({:.0f},{:.0f})", offset_x_,
                         offset_y_);
            return true;
        }

        bool on_drag_end(const ui::drag_gesture &g) override
        {
            if (g.button != MouseButtons::eMOUSE_BUTTON_LEFT)
                return false;
            std::println("[state] canvas 拖动结束 offset=({:.0f},{:.0f})", offset_x_,
                         offset_y_);
            return true;
        }

        bool on_tap(const ui::tap_gesture &g) override
        {
            if (g.button != MouseButtons::eMOUSE_BUTTON_LEFT)
                return false;
            if (g.count != 2)
            {
                std::println("[state] canvas 单击 → 选中物体 {}", ui::pos_str(g.pos));
                return true;
            }
            offset_x_ = 0.0;
            offset_y_ = 0.0;
            zoom_ = 1.0;
            std::println("[state] canvas 双击 → 复位");
            return true;
        }

        bool on_zoom(const ui::zoom_gesture &g) override
        {
            zoom_ *= g.delta > 0.0 ? 1.1 : 0.9;
            std::println("[state] canvas 缩放 → zoom={:.2f}", zoom_);
            return true;
        }

      private:
        double offset_x_{0.0};
        double offset_y_{0.0};
        double grab_x_{0.0};
        double grab_y_{0.0};
        double zoom_{1.0};
    };

    // ── 编辑器：自带裁决层（命中链越深越优先）+ 双击选词 + 右键拖动列选择 ──
    class editor_widget : public ui::widget
    {
      public:
        editor_widget() : widget{"editor", {40, 300, 720, 260}}
        {
            add_recognizer<ui::tap_recognizer>(ui::kGesture);
            add_recognizer<ui::long_press_recognizer>(ui::kGesture);
            add_recognizer<ui::drag_recognizer>(ui::kGesture);
            add_recognizer<ui::shortcut_recognizer>();
        }

        // 这一句就是“命中链决定裁决层”：编辑器区域里，谁离用户近谁说了算
        [[nodiscard]] ui::gesture_arena *arena_override() noexcept override
        {
            return &arena_;
        }

        bool on_tap(const ui::tap_gesture &g) override
        {
            if (g.button == MouseButtons::eMOUSE_BUTTON_RIGHT)
            {
                // 走运行时分发：命令名（string）+ 参数（std::any）
                return ui::command_registry::instance().invoke(*this, "editor.show_menu",
                                                               std::any{g.pos});
            }
            if (g.button == MouseButtons::eMOUSE_BUTTON_LEFT && g.count == 2)
            {
                std::println("[state] editor 双击 → 选中一个词");
                return true;
            }
            return false;
        }

        bool on_drag_begin(const ui::drag_gesture &g) override
        {
            if (g.button != MouseButtons::eMOUSE_BUTTON_RIGHT)
                return false;
            column_select_ = true;
            std::println("[state] editor 右键拖动 → 列选择开始");
            return true;
        }

        bool on_drag_update(const ui::drag_gesture &g) override
        {
            if (!column_select_)
                return false;
            std::println("[state] editor 列选择 d=({:.0f},{:.0f})", g.total_dx,
                         g.total_dy);
            return true;
        }

        bool on_drag_end(const ui::drag_gesture &g) override
        {
            if (!column_select_)
                return false;
            column_select_ = false;
            std::println("[state] editor 列选择结束");
            return true;
        }

        bool on_shortcut(const ui::shortcut_gesture &g) override
        {
            std::println("[state] editor 快捷键 {}+{}", g.mods, g.key);
            return true;
        }

      private:
        ui::deepest_first_arena arena_{};
        bool column_select_{false};
    };

    // ── 桌面（根）：右键菜单、右键拖动移动窗口（特异性故意写到 25）、左键框选 ──
    class desktop_widget : public ui::widget
    {
      public:
        desktop_widget() : widget{"desktop", {0, 0, WIDTH, HEIGHT}}
        {
            add_recognizer<ui::tap_recognizer>(ui::kGesture);
            add_recognizer<ui::drag_recognizer>(ui::kGesture, 25); // 25 > 编辑器的 20
            add_recognizer<ui::shortcut_recognizer>();
        }

        bool on_tap(const ui::tap_gesture &g) override
        {
            if (g.button == MouseButtons::eMOUSE_BUTTON_RIGHT)
                return ui::command_registry::instance().invoke(*this, "desktop.show_menu",
                                                               std::any{g.pos});
            return false;
        }

        bool on_drag_begin(const ui::drag_gesture &g) override
        {
            if (g.button != MouseButtons::eMOUSE_BUTTON_RIGHT)
                return false;
            moving_ = true;
            std::println("[state] desktop 右键拖动 → 移动窗口开始");
            return true;
        }

        bool on_drag_update(const ui::drag_gesture &g) override
        {
            if (!moving_)
                return false;
            window_x_ += g.total_dx - moved_dx_;
            window_y_ += g.total_dy - moved_dy_;
            moved_dx_ = g.total_dx;
            moved_dy_ = g.total_dy;
            std::println("[state] desktop 窗口位置 = ({:.0f},{:.0f})", window_x_,
                         window_y_);
            return true;
        }

        bool on_drag_end(const ui::drag_gesture &g) override
        {
            if (!moving_)
                return false;
            moving_ = false;
            moved_dx_ = 0.0;
            moved_dy_ = 0.0;
            std::println("[state] desktop 移动窗口结束");
            return true;
        }

        bool on_shortcut(const ui::shortcut_gesture &g) override
        {
            std::println("[state] desktop 快捷键 {}+{}", g.mods, g.key);
            return true;
        }

        // 原始按键也走这条链：F1 切换“强制默认裁决层”，用来对比裁决规则
        bool on_key(const ui::key_event &g) override
        {
            if (g.key != Key::eF1 || g.action != Action::ePRESS || force_flag_ == nullptr)
                return false;
            *force_flag_ = !*force_flag_;
            std::println("[state] F1 → 强制默认裁决层 = {}（现在右键拖动编辑器看看）",
                         *force_flag_);
            return true;
        }

        void bind_force_flag(bool *flag) noexcept
        {
            force_flag_ = flag;
        }

      private:
        bool moving_{false};
        double window_x_{0.0};
        double window_y_{0.0};
        double moved_dx_{0.0};
        double moved_dy_{0.0};
        bool *force_flag_{nullptr};
    };

    // ══ 应用装配：控件树 + 命令表，写在 main 之外（“写死注入”） ══
    class demo_app
    {
      public:
        demo_app()
        {
            root_.add(button_);
            root_.add(canvas_);
            root_.add(editor_);
            root_.bind_force_flag(&force_default_arena_);
            register_commands();
        }

        [[nodiscard]] ui::widget &root() noexcept
        {
            return root_;
        }

        [[nodiscard]] bool force_default_arena() const noexcept
        {
            return force_default_arena_;
        }

        void describe_bindings()
        {
            std::println("── 控件树 / 绑定 ──────────────────────────────");
            describe(root_, 0);
            std::println("── 命令表（运行时分发） ──────────────────────");
            for (const auto *name : {"editor.show_menu", "desktop.show_menu"})
                std::println("   {:<18} {}", name,
                             ui::command_registry::instance().has(name) ? "已注册"
                                                                        : "缺失");
            std::println("─────────────────────────────────────────────");
        }

      private:
        static void register_commands()
        {
            auto &registry = ui::command_registry::instance();

            registry.add(
                "editor.show_menu", [](ui::gesture_target &target, const std::any &args) {
                    if (dynamic_cast<editor_widget *>(&target) == nullptr)
                        return false; // 不是编辑器：交回冒泡链
                    const auto pos = std::any_cast<position2d_event>(args);
                    std::println("[menu ] 编辑器菜单 @{}：重命名 / 重构 / 格式化",
                                 ui::pos_str(pos));
                    return true;
                });

            registry.add("desktop.show_menu",
                         [](ui::gesture_target &target, const std::any &args) {
                             if (dynamic_cast<desktop_widget *>(&target) == nullptr)
                                 return false;
                             const auto pos = std::any_cast<position2d_event>(args);
                             std::println("[menu ] 桌面菜单 @{}：新建文件夹 / 显示设置",
                                          ui::pos_str(pos));
                             return true;
                         });
        }

        static void describe(ui::widget &w, int indent)
        {
            const std::string pad(static_cast<std::size_t>(indent) * 2, ' ');
            std::println("{}{:<8} area=({:.0f},{:.0f}) {:.0f}x{:.0f}  识别器 {} 个{}",
                         pad, w.name(), w.area().x, w.area().y, w.area().w, w.area().h,
                         w.recognizers().size(),
                         w.arena_override() != nullptr ? "  [自带裁决层]" : "");
            for (auto *child : w.children())
                describe(*child, indent + 1);
        }

        desktop_widget root_{};
        button_widget button_{};
        canvas_widget canvas_{};
        editor_widget editor_{};
        bool force_default_arena_{false};
    };

    // main 之外的全局注入点
    demo_app &the_app() noexcept
    {
        static demo_app app{};
        return app;
    }

} // namespace

int main(int argc, char **argv)
try
{
    // 无窗口自测：整条管线 + 断言（不创建 Vulkan 实例与窗口）
#if 0
    return mcs::vulkan::ui::selftest::run() ? 0 : 1;
#endif

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
    // 装配：控件树在 main 之外注入（the_app），管线把硬件接到树上
    auto &app = the_app();
    auto input_forward = glfw_forward{};
    ui::ui_input pipeline{input_forward, app.root()};
    app.describe_bindings();
    std::println("提示：鼠标移动会按命中链切换裁决层；右键拖动编辑器(列选择) vs "
                 "右键拖动桌面(移动窗口)");
    std::println(
        "      按 F1 可强制所有区域用默认裁决层，再右键拖动编辑器，结果会变成“移动窗口”");

    while (window.shouldClose() == 0)
    {
        surface::pollEvents();

        // 光标位置：每帧采样一次，喂给同一条管线（hover 判定与拖动流都靠它）
        double cursor_x = 0.0;
        double cursor_y = 0.0;
        ::glfwGetCursorPos(window.data(), &cursor_x, &cursor_y);
        pipeline.push_cursor(position2d_event{cursor_x, cursor_y});

        // 运行时改裁决层：控件只翻一个标志，管线每帧同步（不需要反向持有管线）
        pipeline.set_force_default_arena(app.force_default_arena());

        pipeline.update(); // 没有新输入时也要裁决：长按到点 / 单击等待窗口过期
    }

    std::println("统计：裁决层切换 {} 次，未被处理的事件 {} 个", pipeline.arena_swaps(),
                 pipeline.unhandled());

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}