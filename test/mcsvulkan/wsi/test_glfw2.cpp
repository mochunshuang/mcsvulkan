#include <cassert>
#include <cstdint>
#include <exception>
#include <iostream>
#include <print>
#include <chrono>
#include <span>
#include <vector>

#include "head.hpp"

// NOLINTBEGIN
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

// NOTE: 排序 交给 实现的竞技场 确定即可。\

using mcs::vulkan::meta::make_aggregate;
using mcs::vulkan::meta::field;
using mcs::vulkan::meta::method;
using mcs::vulkan::meta::static_string;

using parameter_list = std::variant<int, double>;
struct dynamic_pointer
{
    using Fn = void (*)(void *ptr, parameter_list) noexcept;

    template <class Agg, static_string hover_fn_name>
        requires(requires(Agg &agg, parameter_list p) {
            agg.template invoke<hover_fn_name>(p);
        })
    static constexpr dynamic_pointer make() noexcept
    {
        return dynamic_pointer{[](void *ptr, parameter_list p) noexcept {
            auto *agg = static_cast<std::decay_t<Agg> *>(ptr);
            agg->template invoke<hover_fn_name>(p);
        }};
    }
    constexpr void dispach(void *ptr, parameter_list p) noexcept
    {
        fn(ptr, std::move(p));
    }

    dynamic_pointer() = delete;

  private:
    Fn fn;
    explicit constexpr dynamic_pointer(Fn f) noexcept : fn(f) {}
};

enum GestureRecognizerState
{
    undefined,
    confirm,  // 确定比赛环境符合要求
    possible, // 已收到 pointer，正在观察，还没裁决
    accepted, // 赢了
    rejected, // 输了
};

// NOTE: 决斗者： 必须可以和其他人，竞争，竞争的地方在竞技场。 必须和其他决斗者 能选出裁判
// NOTE: 决斗者 必须 知道 和谁决斗。  决斗者们 自己选出 胜利者。 被动性写出规则
struct fighter
{
    static_string name;
    GestureRecognizerState state;

    // 是否参与当前多人决斗：找是否存在
    virtual bool confirm(std::span<fighter *> opponents) noexcept = 0;
};

// NOTE: 裁决算法： 由决定者们 共同生成规则。每一帧，检查，根据规则，选出决斗者。主动性
struct arbiter
{
    void processing(std::span<fighter *> opponents) noexcept
    {
        //
    }
};

// 竞技场
// 由成员 + 裁决
// 决斗成员 +  裁判 一定选出胜者
//  选出胜者之后，直接生成胜利事件
struct arena
{
    enum class GestureRecognizerState : uint8_t
    {
        undefined,
        processing, // 确定比赛环境符合要求
        success,
        failure,
    };

    // NOTE: 1. 构造初始化，就完成封闭的决斗环境。中途不允许加入其他人员
    // NOTE: 2. 可以有 候补
    // NOTE: 3. 决斗比赛过程【每一帧检查】，可以选出淘汰者 或 胜利者
    // NOTE: 4. 胜利者必须得到决斗成员的全部认可
    bool confirm() {}

    void run()
    {
        // NOTE: 干嘛呢？
    }

  private:
    std::vector<fighter *> fighters_;
    arbiter *arbiter_;
};

struct success_result
{
    std::chrono::steady_clock::time_point successTime;  // 完成时间
    std::chrono::steady_clock::time_point acceptedTime; //接受时间
};

struct hardware_layer_input
{
    // NOTE: 谁来承接？ 肯定是竞技场。
    constexpr explicit hardware_layer_input(glfw_forward &input, surface &window)
        : input_{input}, window_{window}
    {
        input_.subscribe<glfw_forward::keyboard_change_fn>(
            this, hardware_layer_input::onKeyboard);
    }
    ~hardware_layer_input() noexcept
    {
        input_.unsubscribe<glfw_forward::keyboard_change_fn>(
            this, hardware_layer_input::onKeyboard);
    }
    hardware_layer_input(const hardware_layer_input &) = delete;
    hardware_layer_input(hardware_layer_input &&) = delete;
    hardware_layer_input &operator=(const hardware_layer_input &) = delete;
    hardware_layer_input &operator=(hardware_layer_input &&) = delete;

    static void onKeyboard(void *self, position2d_event,
                           std::chrono::steady_clock::time_point,
                           keyboard_event e) noexcept
    {

        std::println("onKeyboard cursor");
    }
    // NOTE: 转发给 需要这个事件的 竞技场。 按竞技场 先进行 物理切割，缩小范围。
    // NOTE: 竞技场的 UI识别器 共享用一个信息，然后胜利者，进行最终的事件生成
    // NOTE: 竞技场的胜利，是否可以编译期确定。可以的。必须互斥，必须可以阻碍胜利
    // NOTE: 竞技场的成员，可以互相感知吗？应该是是需要的，必须有名字

  private:
    glfw_forward &input_;
    surface &window_;

    // NOTE: 光标的icon 可以改变
    static void changeCursor(void *self, position2d_event,
                             std::chrono::steady_clock::time_point,
                             keyboard_event e) noexcept
    {
        if (!e.press())
            return; // 只认按下，repeat/release 直接跳过

        // 光标池：0 = NULL（默认箭头），1~10 懒创建
        static GLFWcursor *pool[11] = {};
        if (!pool[1])
            pool[1] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
        if (!pool[2])
            pool[2] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
        if (!pool[3])
            pool[3] = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
        if (!pool[4])
            pool[4] = glfwCreateStandardCursor(GLFW_POINTING_HAND_CURSOR);
        if (!pool[5])
            pool[5] = glfwCreateStandardCursor(GLFW_RESIZE_EW_CURSOR);
        if (!pool[6])
            pool[6] = glfwCreateStandardCursor(GLFW_RESIZE_NS_CURSOR);
        if (!pool[7])
            pool[7] = glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR);
        if (!pool[8])
            pool[8] = glfwCreateStandardCursor(GLFW_RESIZE_NESW_CURSOR);
        if (!pool[9])
            pool[9] = glfwCreateStandardCursor(GLFW_RESIZE_ALL_CURSOR);
        if (!pool[10])
            pool[10] = glfwCreateStandardCursor(GLFW_NOT_ALLOWED_CURSOR);

        // 循环递增
        static std::size_t idx = 0;
        idx = (idx + 1) % std::size(pool);

        auto *s = static_cast<hardware_layer_input *>(self);
        glfwSetCursor(s->window_.data(), pool[idx]); // s->window_ 需要是 GLFWwindow*

        std::println("onKeyboard cursor idx={}", idx);
    }
};

struct input_operator
{
    //
};

int main()
try
{

    surface window{};
    window.setup({.width = WIDTH, .height = HEIGHT}, TITLE); // NOLINT
    auto input = glfw_input{};
    // NOTE: 订阅有状态。变化的状态

    auto forward = glfw_forward{};
    hardware_layer_input h{forward, window};

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
} // NOLINTEND