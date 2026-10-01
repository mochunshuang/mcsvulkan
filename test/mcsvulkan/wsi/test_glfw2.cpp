#include <cassert>
#include <exception>
#include <iostream>
#include <print>
#include <chrono>

#include "head.hpp"

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

/*
⏱️ 竞技场裁决的延迟：确实存在，但并非总是发生
竞技场裁决的延迟主要来自延迟裁决（Delayed Winning） 机制。当多个手势识别器竞争时，为了准确区分用户意图，系统不会立即宣布胜者，而是等待更多事件或超时。

最典型的场景是单击与双击共存。当 GestureDetector 同时监听了 onTap 和 onDoubleTap 时，单击事件会有约 200-300ms 的延迟。这是因为系统必须等待一段时间，确认用户不会进行第二次点击，才能最终判定为单击。类似地，LongPressGestureRecognizer 需要等待 500ms 内是否有 PointerUpEvent 来决定是否退出竞争。

但请注意，这种延迟并非总是发生：

如果只监听 onTap 而不监听 onDoubleTap，则没有延迟。

对于拖动（Drag） 手势，识别器一旦检测到位移超过阈值（如 18px），会立即宣告胜利，几乎无延迟。

对于滚动等高频操作，识别器会迅速做出裁决，不会等待超时。

所以，你感知到的“毫秒级延迟”是特定手势组合下的必然代价，而非事件管理机制的系统性开销。
*/
/*
🏆 竞技场的破局思路：从“被动传播”到“主动裁决”
Flutter 的手势竞技场引入了一种“竞争上岗”的机制，将手势处理从“事件传播”问题，转变为了“意图裁决”问题。

建立竞争机制：当手指触碰屏幕（PointerDownEvent），所有命中该位置的 Widget 所关联的手势识别器（如 HorizontalDragGestureRecognizer、VerticalDragGestureRecognizer）都会被注册到同一个“竞技场”中，成为竞争者。

收集“证据”并裁决：竞技场不会立即下发事件，而是让所有识别器根据后续的触摸移动“收集证据”。例如，水平拖动识别器会等待水平位移超过阈值（如 18px），才向竞技场“宣布胜利”。

胜者通吃，强制淘汰：一旦某个识别器被裁决为胜者（accepted），竞技场会立即关闭，并向所有其他失败的识别器发送 rejected 信号，强制它们退出，不再处理后续事件。

核心优势在于：这套机制可以动态地、基于事实（用户的实际滑动方向）来裁决意图，而不是像 Web 那样基于预设的、静态的规则。它从根本上解决了“用户本想垂直滚动列表，却因手指轻微倾斜而被横向轮播图抢走手势”的“手势抢夺”问题。
*/

/*
⚖️ 为什么 Flutter 不直接复用 Web 的事件系统？
这背后是 Flutter 架构选择的必然结果。

Flutter 的核心是自绘引擎，它不依赖平台的原生控件，而是通过 Skia/Impeller 直接在画布上绘制所有 UI。这意味着，它无法像 Web 那样，将事件处理“委托”给浏览器内核。

既然没有“浏览器”这个中间层，Flutter 就必须自己实现一整套从指针事件到手势语义的完整管道。手势竞技场就是这套管道中用于解决冲突的核心组件。同时，这也是为了保证跨平台手势体验的一致性。如果依赖各平台的原生事件系统，iOS 和 Android 的手势识别差异会直接导致 App 行为不一致，而竞技场机制在 Flutter 框架层面统一了裁决逻辑，抹平了这种差异。
*/

/*
// NOTE: 1. 首先现有场景：hover 到 一块区域： 得到已注册的竞技场id。注意: Widget 构造形成结构的时候，必须注册好。
// NOTE: 2. 竞技场的注册和弹出是 栈的结构： 栈保证了 后加入的 可以获得 之前的基础默认功能。
// NOTE: 3. 因此：必须传入 current_arena_id 【可以注册一个，可以使用默认已加入的】来激活 谁来处理 硬件输入
// NOTE: 4. 得到 arena 我们就拿到算法了，就可以当普通的函数处理了。
// NOTE: 5. arena 处理 input 硬件输入，会生成 UI 的事件，得到准确能表达用户意图的事件是目标，错了就无法回头
// NOTE: 6. 根据 事件类型 + 上下文信息， 必须准确调用到 目标函数
// NOTE: 7. arena 必须告知能【处理/裁决】 那些 事件。不能默认应该往上交付。都不能交付，丢弃 并打印
// NOTE: 8. 不应该管理额外的内存，经量纯算法，没有内存管理。
// NOTE: 9. 上下文对象是 承接 事件和函数 管理的，是动态的，上下文一次性的，最好没有堆内存开销/varaint 的 明显有限范围，可以保证
*/

// NOTE: 排序 交给 实现的竞技场 确定即可。
int main()
try
{

    surface window{};
    window.setup({.width = WIDTH, .height = HEIGHT}, TITLE); // NOLINT
    auto input = glfw_input{};
    // NOTE: 订阅有状态。变化的状态
    event_manager eventManager{input};

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