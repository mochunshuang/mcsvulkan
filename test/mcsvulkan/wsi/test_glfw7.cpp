#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <optional>
#include <print>
#include <chrono>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
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

using mcs::vulkan::event::keyboard_event;
using mcs::vulkan::event::scroll_event;
using mcs::vulkan::event::mousebutton_event;
using mcs::vulkan::event::position2d_event;
using mcs::vulkan::event::cursor_enter_event;

using mcs::vulkan::event::Key;
using mcs::vulkan::event::MouseButtons;
using mcs::vulkan::event::ModifierKey;
using mcs::vulkan::event::Action;

using mcs::vulkan::meta::make_aggregate;
using mcs::vulkan::meta::field;
using mcs::vulkan::meta::method;
using mcs::vulkan::meta::static_string;
using mcs::vulkan::meta::name_spec;
using mcs::vulkan::meta::class_utils;

using mcs::vulkan::runtime_type_v;
using mcs::vulkan::ring_buffer;

template <typename Obj, typename P>
concept can_invoke = requires(Obj *ptr, position2d_event pos,
                              std::chrono::steady_clock::time_point time_point, P p) {
    requires requires {
        { ptr->template invoke<"input_event">(pos, time_point, p) } noexcept;
    } || requires {
        { ptr->input_event(pos, time_point, p) } noexcept;
    };
};

namespace dispach
{
    template <class T>
    static void onPosition2d(void *self, position2d_event pos,
                             std::chrono::steady_clock::time_point time_point,
                             position2d_event e) noexcept
    {
        static_cast<T *>(self)->forward(pos, time_point, e);
    }
    template <class T>
    static void onKeyboard(void *self, position2d_event pos,
                           std::chrono::steady_clock::time_point time_point,
                           keyboard_event e) noexcept
    {
        static_cast<T *>(self)->forward(pos, time_point, e);
    }
    template <class T>
    static void onMousebutton(void *self, position2d_event pos,
                              std::chrono::steady_clock::time_point time_point,
                              mousebutton_event e) noexcept
    {
        static_cast<T *>(self)->forward(pos, time_point, e);
    }
}; // namespace dispach

struct dynamic_pointer
{
    using Fn = void (*)(void *ptr, position2d_event pos,
                        std::chrono::steady_clock::time_point tp, const void *type,
                        void *p) noexcept;

    template <class Dispach, class Agg>
    static constexpr dynamic_pointer make() noexcept
    {
        return dynamic_pointer{[](void *ptr, position2d_event pos,
                                  std::chrono::steady_clock::time_point tp,
                                  const void *type, void *p) noexcept {
            auto *agg = static_cast<std::decay_t<Agg> *>(ptr);
            Dispach::dispach(agg, pos, tp, type, p);
        }};
    }

    template <typename P>
        requires(!std::is_reference_v<P> && std::is_same_v<P, std::remove_cv_t<P>>)
    constexpr void dispach(position2d_event pos,
                           std::chrono::steady_clock::time_point time_point, void *ptr,
                           P p) noexcept
    {
        void *volatile vp = &p; // volatile 指针，阻止编译器追踪其指向对象的类型
        fn(ptr, pos, time_point, runtime_type_v<P>, vp);
    }

    dynamic_pointer() = default;

  private:
    Fn fn{};
    explicit constexpr dynamic_pointer(Fn f) noexcept : fn(f) {}
};

struct hardware_layer_input
{
    using enable_forword =
        std::tuple<keyboard_event, mousebutton_event, position2d_event>;
    constexpr explicit hardware_layer_input(glfw_forward &input) : input_{input}
    {
        input_.subscribe<glfw_forward::position2d_change_fn>(
            this, dispach::onPosition2d<hardware_layer_input>);
        input_.subscribe<glfw_forward::keyboard_change_fn>(
            this, dispach::onKeyboard<hardware_layer_input>);
        input_.subscribe<glfw_forward::mousebutton_change_fn>(
            this, dispach::onMousebutton<hardware_layer_input>);
    }
    constexpr ~hardware_layer_input() noexcept
    {
        input_.unsubscribe<glfw_forward::position2d_change_fn>(
            this, dispach::onPosition2d<hardware_layer_input>);
        input_.unsubscribe<glfw_forward::keyboard_change_fn>(
            this, dispach::onKeyboard<hardware_layer_input>);
        input_.unsubscribe<glfw_forward::mousebutton_change_fn>(
            this, dispach::onMousebutton<hardware_layer_input>);
    }
    hardware_layer_input(const hardware_layer_input &) = delete;
    hardware_layer_input(hardware_layer_input &&) = delete;
    hardware_layer_input &operator=(const hardware_layer_input &) = delete;
    hardware_layer_input &operator=(hardware_layer_input &&) = delete;

    constexpr void forward(position2d_event pos,
                           std::chrono::steady_clock::time_point time_point,
                           auto e) noexcept
    {
        if (receiver_ == nullptr)
            return;
        fn_.dispach(pos, time_point, receiver_, e);
    }

    template <typename Obj>
    constexpr void setReceiver(Obj *obj) noexcept
    {
        receiver_ = obj;
        fn_ = dynamic_pointer::make<hardware_layer_input, Obj>();
    }

    template <typename Obj, typename... Ts>
    static constexpr void input_event(Obj *ptr, position2d_event pos,
                                      std::chrono::steady_clock::time_point time_point,
                                      const void *type, void *p) noexcept
    {
        auto invoke = [&](auto &e) noexcept {
            if constexpr (requires() {
                              ptr->template invoke<"input_event">(pos, time_point, e);
                          })
                ptr->template invoke<"input_event">(pos, time_point, e);
            else if constexpr (requires() { ptr->input_event(pos, time_point, e); })
                ptr->input_event(pos, time_point, e);
        };
        ((can_invoke<Obj, Ts> && type == runtime_type_v<Ts> &&
          (invoke(*static_cast<Ts *>(p)), true)) ||
         ...);
    }
    template <typename Obj>
    static constexpr void dispach(Obj *ptr, position2d_event pos,
                                  std::chrono::steady_clock::time_point time_point,
                                  const void *type, void *p) noexcept
    {
        [&]<typename... Ts>(std::tuple<Ts...>) {
            input_event<Obj, Ts...>(ptr, pos, time_point, type, p);
        }(enable_forword{});
    }

  private:
    void *receiver_{};
    dynamic_pointer fn_;
    glfw_forward &input_;
};

// NOTE: 识别器
struct recognize
{
};
// NOTE:
struct arena
{
};

struct ui_gen
{

    //

    struct padding_result
    {
    };

    struct recognize_item
    {

        ui_gen *reciver;
    };

    void setResult(padding_result result)
    {
        // NOTE:
    }

    padding_result padding_result;
    recognize_item root;
};

template <class Agg, name_spec... Info>
struct result_awaiter
{
    using result_variant = std::variant<std::monostate, typename[:Info.info:]...>;

    static consteval auto unique_type()
    {
        constexpr std::array<name_spec, sizeof...(Info)> input{Info...};
        for (std::size_t i = 0; i < input.size(); ++i)
        {
            auto spec_i = input[i];
            for (std::size_t j = i + 1; j < input.size(); ++j)
            {
                auto spec_j = input[j];
                if (spec_i.info == spec_j.info)
                    return false;
            }
        }
        return true;
    };

    using IndexStoreType = uint8_t;
    struct empty_type
    {
        constexpr empty_type(IndexStoreType) {}
    };
    static_assert(sizeof(empty_type) == 1);
    using IndexType = std::conditional_t<unique_type(), empty_type, IndexStoreType>;
    static constexpr IndexType invalid_idx =
        static_cast<IndexType>(std::numeric_limits<IndexStoreType>::max());
    static_assert(sizeof...(Info) < std::numeric_limits<IndexStoreType>::max());

    struct Dispach
    {
        template <static_string fn_name, class T>
        void invoke(void *obj, T &&value) noexcept
            requires(requires() {
                {
                    static_cast<Agg *>(obj)->template invoke<fn_name>(
                        std::forward<T>(value))
                } noexcept;
            })
        {
            static_cast<Agg *>(obj)->template invoke<fn_name>(std::forward<T>(value));
        }
    };
    constexpr void resume() noexcept
    {
        if constexpr (unique_type())
        {
            auto index = result.index();
            if (index == 0 || obj == nullptr)
                return;
            template for (constexpr auto I : std::views::iota(0u, sizeof...(Info)))
            {
                constexpr name_spec spec = Info...[I];
                if (I == index - 1)
                {
                    dispach.template invoke<spec.name>(
                        obj, std::move(std::get<I + 1>(result)));
                    result = std::monostate{};
                    return;
                }
            }
        }
        else
        {
            if (idx == invalid_idx || obj == nullptr || result.index() == 0)
                return;
            template for (constexpr auto I :
                          std::views::indices(sizeof...(Info) + 1) | std::views::drop(1))
            {
                constexpr name_spec spec = Info...[I - 1];
                if (I == idx)
                {
                    dispach.template invoke<spec.name>(obj,
                                                       std::move(std::get<I>(result)));

                    result = std::monostate{};
                    idx = ~0;
                    return;
                }
            }
        }
    }
    void *obj;
    [[no_unique_address]] IndexType idx{invalid_idx};
    [[no_unique_address]] Dispach dispach{};
    [[no_unique_address]] result_variant result{};

    constexpr explicit result_awaiter(Agg &p) noexcept : obj{&p} {}

    template <typename T>
    constexpr void setResult(T &&value) noexcept(
        noexcept(result = std::forward<T>(value)))
        requires(requires() { result = std::forward<T>(value); })
    {
        result = std::forward<T>(value);
    }

    template <std::size_t I, typename T>
    constexpr void setResult(T &&value) noexcept(
        noexcept(result.template emplace<I>(std::forward<T>(value))))
        requires requires { result.template emplace<I>(std::forward<T>(value)); }
    {
        result.template emplace<I>(std::forward<T>(value));
        idx = static_cast<IndexType>(I);
    }
};

// 指针事件是原始输入，手势事件是对这些原始输入的语义解读，键盘/焦点事件处理非指针输入，而通知和语义事件则分别服务于组件间通信和平台辅助功能。

namespace event
{
    // ═══════════════════════════════════════════════════════════════
    // 输入事件的语义模型
    // ═══════════════════════════════════════════════════════════════
    //
    // 输入事件的三阶：
    //   [一] 物理阶    平台原始信号
    //   [二] 规范阶    平台无关的输入事件
    //   [三] 语义阶    从规范阶序列解读出的用户意图
    //
    // 跨平台能力的落点：阶 [二] 是稳定边界。
    // 应用只对接 [二] 和 [三]，永不对接 [一]。

    // 指针设备类型。不是"鼠标/触摸屏"的分类，而是"手感"的分类——
    // slop 阈值、精度、是否支持压力都由此决定。
    enum class PointerDeviceKind : std::uint8_t
    {
        eTOUCH,
        eMOUSE,
        eSTYLUS,
        eINVERTED_STYLUS,
        eTRACKPAD,
        eUNKNOWN,
    };

    struct DeviceInput
    {
        PointerDeviceKind kind;
    };

    // ─────────────────────────────────────────────────────────────
    // 阶 [二]·指针族
    //
    // 核心思想：**事件是状态快照，不是变化通知。**
    //
    // 平台原始信号是"位置变了"和"按钮变了"两条独立的流。指针事
    // 件把二者合并，每一个事件都自带完整上下文——哪个指针、在哪、
    // 按着什么、何时、什么设备。订阅者只看一个事件就够，不需要在
    // 自己内部维护状态副本。
    //
    // 一个指针 = 一次交互主体。它有从出现到消失的完整生命周期。
    // 事件类型表达的不是"阶段"，而是"这个主体此刻发生了什么事实"。
    //
    // 取消是一等公民。中断不是异常，是正常事实。没有它，被外力
    // 打断的交互会让状态机永久卡住。

    struct PointerDownEvent : DeviceInput
    {
    }; // 从未接触到接触

    struct PointerMoveEvent : DeviceInput
    {

    }; // 接触状态下的移动

    struct PointerUpEvent : DeviceInput
    {
    }; // 从接触到未接触

    struct PointerCancelEvent : DeviceInput
    {
    }; // 交互作废，后续事件不可信

    struct PointerHoverEvent : DeviceInput
    {
    }; // 未接触状态下的移动

    // 指针在设备跟踪范围内的出现与消失。
    // 比 Down/Up 更外层——Down 之前必有 Added，Up 之后未必立即
    // Removed（可能继续 Hover）。
    struct PointerAddedEvent : DeviceInput
    {
    };
    struct PointerRemovedEvent : DeviceInput
    {
    };

    // 指针进入 / 离开某个目标对象。
    // 与 Added/Removed 不同：这是目标对象级的边界，需要命中测试
    // 才能合成，不是平台原生事件。
    struct PointerEnterEvent : DeviceInput
    {
    };
    struct PointerExitEvent : DeviceInput
    {
    };

    // 离散信号。不改变指针状态，不参与手势序列。
    // 与接触类事件的关键区别：它是一次性的，不是过程的一部分。
    struct PointerScrollEvent : DeviceInput
    {
    };
    struct PointerScaleEvent : DeviceInput
    {
    };

    // 触摸板多指平移 / 缩放的复合序列。
    // 有状态：Start → Update → End。
    struct PointerPanZoomStartEvent : DeviceInput
    {
    };
    struct PointerPanZoomUpdateEvent : DeviceInput
    {
    };
    struct PointerPanZoomEndEvent : DeviceInput
    {
    };

    // ─────────────────────────────────────────────────────────────
    // 阶 [三]·手势族
    //
    // 手势是**结论**，不是输入。识别器订阅指针序列，从中推断用户
    // 意图，产出零个或多个手势事件。
    //
    // 分工边界：
    //   指针层不懂手势含义；
    //   手势层不懂平台细节。
    // 因此同一个识别器可跨平台工作。

    // ── 点击 ──
    // 一次短促、未越 slop、完整闭合的接触。
    // 判据不是"down 与 up 位置相等"，而是"down 到 up 期间任何时刻
    // 相对起点的位移都未超 slop"。一旦超限，永久出局。
    // slop 由设备决定——触摸 18px、鼠标 1px。手感是设备属性，不是
    // 常量。
    struct TapDownEvent
    {
    };
    struct TapUpEvent
    {
    };
    struct TapEvent
    {
    };
    struct TapCancelEvent
    {
    };

    // ── 双击 ──
    // 独立意图，与单击互斥。
    // 实现上单击不"输给"双击，而是被暂缓：双击 hold 住竞技场，
    // 超时无第二击才释放给单击。宁可让用户等，也不发出可能被撤销
    // 的假回调。
    struct DoubleTapEvent
    {
    };

    // ── 长按 ──
    // 在原地持续接触超过阈值时间。与点击互斥。
    struct LongPressEvent
    {
    };

    // ── 拖动 ──
    // 接触状态下的持续移动，有始有终。
    // 三个轴变体共享 Start/Update/End 结构。轴在首次超阈值时确定，
    // 本次拖动不再改轴。
    struct VerticalDragStartEvent
    {
    };
    struct VerticalDragUpdateEvent
    {
    };
    struct VerticalDragEndEvent
    {
    };

    struct HorizontalDragStartEvent
    {
    };
    struct HorizontalDragUpdateEvent
    {
    };
    struct HorizontalDragEndEvent
    {
    };

    struct PanStartEvent
    {
    };
    struct PanUpdateEvent
    {
    };
    struct PanEndEvent
    {
    };

    // ─────────────────────────────────────────────────────────────
    // 阶 [二]·键盘族
    //
    // 语义是**物理按键状态的变化**。不关心布局——按 A 键得到的
    // 是"A 被按下"，不是"字母 a 产生"。后者属于文本族。
    //
    // 与指针族的唯一交叉是"修饰键快照"——它是修饰键状态的一次
    // 采样，被指针事件携带，不是键盘事件本身。

    struct KeyDownEvent
    {
    };
    struct KeyRepeatEvent
    {
    };
    struct KeyUpEvent
    {
    };

    // ─────────────────────────────────────────────────────────────
    // 阶 [二]·文本族
    //
    // 语义是**布局后的可见字符输入**。
    //
    // 与键盘族的区别：按下 Shift+A 产生一个键盘事件（A 键按下）
    // 和一个文本事件（字符 'A'）。IME 输入、emoji、组合键都只产
    // 生文本事件，不产生键盘事件。反之，功能键（F1、方向键）只
    // 产生键盘事件，不产生文本事件。
    //
    // 二者不是一一对应。

    struct CharEvent
    {
    };

    // ─────────────────────────────────────────────────────────────
    // 阶 [二]·拖放族
    //
    // 语义是**外部数据传入**。不携带屏幕坐标——它是"数据到了"，
    // 不是"指针做了什么"。
    // 与指针拖动完全不同的概念：一个来自操作系统文件管理器，一个
    // 来自应用内部的指针序列。

    struct DropEvent
    {
    };

    // ─────────────────────────────────────────────────────────────
    // 阶 [二]·窗口族
    //
    // 语义是**承载输入的环境状态变化**。
    // 它不描述"输入"，描述"输入可能发生或不再可能发生"。
    // 例如：窗口失焦意味着后续键盘输入不再路由到此应用。

    struct WindowFocusEvent
    {
    }; // 应用是否在前台
    struct WindowPosEvent
    {
    }; // 窗口在屏幕中的位置
    struct WindowSizeEvent
    {
    }; // 窗口尺寸（逻辑）
    struct WindowIconifyEvent
    {
    }; // 最小化
    struct WindowMaximizeEvent
    {
    }; // 最大化
    struct FramebufferSizeEvent
    {
    }; // 绘制区尺寸（物理像素）
    struct ContentScaleEvent
    {
    }; // DPI 缩放

}; // namespace event

struct glfw_arena
{
};

template <typename... Extra>
struct proxy_class_invoke
{
    static consteval std::optional<std::meta::info> find_function(std::meta::info cls,
                                                                  static_string name)
    {
        return class_utils::find_member(
            cls, name,
            [](std::meta::info m) -> bool {
                return std::meta::is_function(m) || std::meta::is_function_template(m);
            },
            std::meta::access_context::current());
    }
    static consteval std::optional<std::meta::info> find_nonstatic_data_member(
        std::meta::info cls, static_string name)
    {
        return class_utils::find_member(
            cls, name,
            [](std::meta::info m) -> bool {
                return std::meta::is_nonstatic_data_member(m);
            },
            std::meta::access_context::current());
    }

    template <static_string fn_name, class T>
    static consteval std::meta::info resolve_callable()
    {
        if (auto f = find_function(^^T, fn_name); f.has_value())
            return *f;
        if (auto d = find_nonstatic_data_member(^^T, fn_name); d.has_value())
            return *d;
        return std::meta::info{};
    }

    // ========== can_noexcept_invoke ==========
    template <static_string fn_name, class Obj, class... Args>
    static constexpr bool can_noexcept_invoke() noexcept
    {
        using T = std::remove_cvref_t<Obj>;

        // 路径 A：聚合体的 invoke<fn_name>(args...) —— 和 dispach_invoke 的第一个分支对齐
        if constexpr (requires(T &o, Args &&...as) {
                          {
                              o.template invoke<fn_name>(std::forward<Args>(as)...)
                          } noexcept;
                      })
            return true;

        // 路径 B：直接成员调用
        constexpr auto callable = resolve_callable<fn_name, T>();
        if constexpr (callable == std::meta::info{})
            return false;
        else
            return requires(T &o, Args &&...as) {
                { o.[:callable:](std::forward<Args>(as)...) } noexcept;
            } || requires(T &o, Args &&...as) {
                { o.template[:callable:](std::forward<Args>(as)...) } noexcept;
            };
    }

    // ========== invoke_member ==========
    template <static_string fn_name, class Obj, class... Args>
        requires(can_noexcept_invoke<fn_name, std::remove_cvref_t<Obj>, Args...>())
    static constexpr decltype(auto) invoke_member(Obj &obj, Args &&...args) noexcept
    {
        using T = std::remove_cvref_t<Obj>;
        constexpr auto m = resolve_callable<fn_name, T>();

        if constexpr (requires { obj.[:m:](std::forward<Args>(args)...); })
            return obj.[:m:](std::forward<Args>(args)...);
        else if constexpr (requires { obj.template[:m:](std::forward<Args>(args)...); })
            return obj.template[:m:](std::forward<Args>(args)...);
        else
            static_assert(sizeof(Obj) == 0,
                          "invoke_member: member exists but not callable "
                          "with the given arguments");
    }

    using invoke_type = void (*)(void *ptr, const void *type, void *data,
                                 Extra...) noexcept;
    constexpr void invoke(void *ptr, const void *type, void *data, Extra... e) noexcept
    {
        assert(invoke_ != nullptr);
        invoke_(ptr, type, data, e...);
    }

    proxy_class_invoke() = default;
    constexpr explicit proxy_class_invoke(invoke_type invoke) noexcept : invoke_{invoke}
    {
    }

    template <static_string name, typename Obj, typename... Ts>
    static constexpr void dispach_invoke(Obj *ptr, std::tuple<Ts...> *, const void *type,
                                         void *p, Extra... e) noexcept
    {
        using mcs::vulkan::runtime_type_v;
        auto invoke = [&](auto &d) noexcept {
            if constexpr (requires() { ptr->template invoke<name>(d, e...); })
                ptr->template invoke<name>(d, e...); //NOTE: 内置的聚合类型
            else if constexpr (requires() { invoke_member<name>(*ptr, d, e...); })
                invoke_member<name>(*ptr, d, e...); //NOTE: 普通类型
            else
            {
                // std::println("→ branch C (SKIP!)"); // ← 如果打到这里，根因坐实
            }
        };
        ((can_noexcept_invoke<name, Obj, Ts, Extra...> && type == runtime_type_v<Ts> &&
          (invoke(*static_cast<Ts *>(p)), true)) ||
         ...);
    }

    template <static_string callable_name, typename data_tuple, class T>
    static constexpr auto build() noexcept
    {
        using object_type = std::remove_cvref_t<T>;
        return proxy_class_invoke{invoke_type{
            [](void *ptr, const void *type, void *data, Extra... args) noexcept {
                auto *obj = static_cast<object_type *>(ptr);
                dispach_invoke<callable_name, object_type>(
                    obj, static_cast<data_tuple *>(nullptr), type, data, args...);
            }}};
    }

  private:
    invoke_type invoke_{};
};

// 指针事件有队列且不丢弃；手势识别器在竞争期间临时缓存事件，胜出则保留、失败则丢弃；
// 手势事件本身没有队列，是识别器处理指针事件时同步产生的。

//NOTE: "订阅者只看一个事件就够"是两个承诺：一个是事件带完整当前状态（结构体该有的字段），
//NOTE: 一个是订阅者不需要任何跨事件状态（错——识别器必须维护自己的推理状态）。
/*
两个东西，必须分开
名称	谁维护	内容
A	当前状态	生成端（唯一一份）	指针此刻在哪、按着什么、何时、什么设备
B	推理状态	每个识别器（各自一份）	起点、按下时长、上次点击时刻
A 被事件携带。B 不在事件里，也不可能在事件里——它是每个识别器私有的、跨事件累积的。
// NOTE: 因为上下文信息，必须是交给 识别器，按需需要。不应该提升到 translation。 translation 的工作应该简单
*/
struct glfw_input_translation
{
    // surface::pollEvents(); 会产生事件，并且提供

    using time_point_type = std::chrono::steady_clock::time_point;

    // raw_event
    using keyboard_event = mcs::vulkan::event::keyboard_event;
    using mousebutton_event = mcs::vulkan::event::mousebutton_event;
    using scroll_event = mcs::vulkan::event::scroll_event;
    using position2d_event = mcs::vulkan::event::position2d_event;
    using cursor_enter_event = mcs::vulkan::event::cursor_enter_event;

    using char_event = mcs::vulkan::event::char_event;
    using drop_event = mcs::vulkan::event::drop_event;

    using window_focus_event = mcs::vulkan::event::window_focus_event;
    using window_pos_event = mcs::vulkan::event::window_pos_event;
    using window_size_event = mcs::vulkan::event::window_size_event;
    using window_iconify_event = mcs::vulkan::event::window_iconify_event;
    using window_maximize_event = mcs::vulkan::event::window_maximize_event;
    using framebuffer_size_event = mcs::vulkan::event::framebuffer_size_event;

    using data_tuple =
        std::tuple<keyboard_event, mousebutton_event, scroll_event, position2d_event,
                   cursor_enter_event, char_event, drop_event, window_focus_event,
                   window_pos_event, window_size_event, window_iconify_event,
                   window_maximize_event, framebuffer_size_event>;
    using proxy_class_type = proxy_class_invoke<>;
    struct receiver
    {
        constexpr receiver() noexcept = default;
        receiver(void *receiver, proxy_class_type fn) noexcept
            : receiver_{receiver}, fn_{fn}
        {
        }
        template <typename T>
        constexpr void invoke(T data) noexcept
        {
            if (receiver_ == nullptr)
                return;
            using mcs::vulkan::runtime_type_v;
            fn_.invoke(receiver_, runtime_type_v<T>, &data);
        }

      private:
        void *receiver_{};
        proxy_class_type fn_;
    };
    template <typename... event_type>
    struct SubscriptionForwarder
    {
        using callback_type = void (*)(void *self, event_type...);

        struct value_type
        {
            void *ctx;
            callback_type callback;
            bool valid;
        };
        SubscriptionForwarder() = default;
        SubscriptionForwarder(const SubscriptionForwarder &) = delete;
        SubscriptionForwarder(SubscriptionForwarder &&) = delete;
        SubscriptionForwarder &operator=(const SubscriptionForwarder &) = delete;
        SubscriptionForwarder &operator=(SubscriptionForwarder &&) = delete;
        ~SubscriptionForwarder() = default;

        constexpr void subscribe(void *ctx, callback_type cb)
        {
            if (distributing_)
            {
                pendingAdd_.push_back({ctx, cb, true});
                return;
            }
            callbacks_.push_back({ctx, cb, true});
        }

        constexpr void unsubscribe(void *ctx, callback_type cb)
        {
            // 正式列表：标记失效 + 打脏标记
            for (auto &item : callbacks_)
                if (item.ctx == ctx && item.callback == cb)
                {
                    item.valid = false;
                    hasInvalid_ = true;
                }

            // 待加入列表：分发中 subscribe 又立刻 unsubscribe
            for (auto &item : pendingAdd_)
                if (item.ctx == ctx && item.callback == cb)
                {
                    item.valid = false;
                    hasInvalid_ = true;
                }
        }

        constexpr void distribute(event_type... event)
        {
            // 只有最外层才有资格 apply pending / 清理，内层只遍历。处理递归。
            const bool outer = !distributing_; // NOLINT
            if (outer)
                distributing_ = true;

            for (const auto &item : callbacks_)
                if (item.valid)
                    (item.callback)(item.ctx, event...);

            if (outer)
            {
                distributing_ = false;

                // 1) 提交本层分发期间新增的订阅（通常为空）
                if (!pendingAdd_.empty())
                {
                    callbacks_.insert(callbacks_.end(),
                                      std::make_move_iterator(pendingAdd_.begin()),
                                      std::make_move_iterator(pendingAdd_.end()));
                    pendingAdd_.clear();
                }

                // 2) 只有真的出现过 unsubscribe 才清理（常态下完全跳过 O(n)）
                if (hasInvalid_)
                {
                    std::erase_if(callbacks_,
                                  [](const value_type &v) noexcept { return !v.valid; });
                    hasInvalid_ = false;
                }
            }
        }

      private:
        std::vector<value_type> callbacks_;
        std::vector<value_type> pendingAdd_;
        bool distributing_ = false;
        bool hasInvalid_ = false;
    };

    struct forward_object
    {
        // ── 转发中心代理：每个 event_type 一个，不做任何状态存储
        SubscriptionForwarder<position2d_event> cursorPosNotify_;
        SubscriptionForwarder<keyboard_event> keyboardNotify_;
        SubscriptionForwarder<mousebutton_event> mousebuttonNotify_;
        SubscriptionForwarder<scroll_event> scrollNotify_;
        SubscriptionForwarder<cursor_enter_event> cursorEnterNotify_;

        SubscriptionForwarder<char_event> charNotify_;
        SubscriptionForwarder<drop_event> dropNotify_;

        SubscriptionForwarder<window_focus_event> focusNotify_;
        SubscriptionForwarder<window_pos_event> posNotify_;
        SubscriptionForwarder<window_size_event> sizeNotify_;
        SubscriptionForwarder<window_iconify_event> iconifyNotify_;
        SubscriptionForwarder<window_maximize_event> maximizeNotify_;
        SubscriptionForwarder<framebuffer_size_event> fbSizeNotify_;

        // ────────────────────────────────────────────────────────
        // 对外 API：subscribe / unsubscribe
        // ────────────────────────────────────────────────────────
        template <typename event_type>
        constexpr void subscribe(void *ctx,
                                 SubscriptionForwarder<event_type>::callback_type cb)
            requires(requires() { notifier_of<event_type>(*this).subscribe(ctx, cb); })
        {
            notifier_of<event_type>(*this).subscribe(ctx, cb);
        }

        template <typename event_type>
        constexpr void unsubscribe(void *ctx,
                                   SubscriptionForwarder<event_type>::callback_type cb)
            requires(requires() { notifier_of<event_type>(*this).unsubscribe(ctx, cb); })
        {
            notifier_of<event_type>(*this).unsubscribe(ctx, cb);
        }

        // 类型 → notifier 的唯一映射点：新增事件类型时，只改这里
        template <typename Event>
        static constexpr auto &notifier_of(auto &self) noexcept // NOLINT
        {
            if constexpr (std::is_same_v<Event, position2d_event>)
                return self.cursorPosNotify_;
            else if constexpr (std::is_same_v<Event, keyboard_event>)
                return self.keyboardNotify_;
            else if constexpr (std::is_same_v<Event, mousebutton_event>)
                return self.mousebuttonNotify_;
            else if constexpr (std::is_same_v<Event, scroll_event>)
                return self.scrollNotify_;
            else if constexpr (std::is_same_v<Event, cursor_enter_event>)
                return self.cursorEnterNotify_;
            else if constexpr (std::is_same_v<Event, char_event>)
                return self.charNotify_;
            else if constexpr (std::is_same_v<Event, drop_event>)
                return self.dropNotify_;
            else if constexpr (std::is_same_v<Event, window_focus_event>)
                return self.focusNotify_;
            else if constexpr (std::is_same_v<Event, window_pos_event>)
                return self.posNotify_;
            else if constexpr (std::is_same_v<Event, window_size_event>)
                return self.sizeNotify_;
            else if constexpr (std::is_same_v<Event, window_iconify_event>)
                return self.iconifyNotify_;
            else if constexpr (std::is_same_v<Event, window_maximize_event>)
                return self.maximizeNotify_;
            else if constexpr (std::is_same_v<Event, framebuffer_size_event>)
                return self.fbSizeNotify_;
            else
                static_assert(false, "notifier_of: bad event_type");
        }
    };

    // NOTE: 每发送一次更新一次: 突出瞬时，没有上下信息
    // NOTE: GLFW 的回调：在主线程上，在 glfwPollEvents() 内部同步调用。一次一个，串行。回调返回后才处理下一个事件。
    struct snapshot_data
    {
        time_point_type time_point;

        ModifierKey modifier{ModifierKey::None()}; //NOTE: 特殊键

        //  NOTE: 鼠标，按钮，滚轮，鼠标位置
        using event_type =
            std::variant<MouseButtons, Key, scroll_event, position2d_event>;
        event_type event;
    };

    template <typename event_type>
    constexpr static void forwarding(void *self, event_type e) noexcept
    {
        auto *impl = static_cast<glfw_input_translation *>(self);
        impl->receiver_.invoke(e);
    }

    template <typename event_type>
    struct event_dispatcher
    {
        constexpr static auto &instance() noexcept
        {
            namespace event = mcs::vulkan::event;
            if constexpr (std::is_same_v<keyboard_event, event_type>)
                return event::keyboard_event_dispatcher::instance();
            else if constexpr (std::is_same_v<mousebutton_event, event_type>)
                return event::mousebutton_event_dispatcher::instance();
            else if constexpr (std::is_same_v<scroll_event, event_type>)
                return event::scroll_event_dispatcher::instance();
            else if constexpr (std::is_same_v<position2d_event, event_type>)
                return event::position2d_event_dispatcher::instance();
            else if constexpr (std::is_same_v<cursor_enter_event, event_type>)
                return event::cursor_enter_event_dispatcher::instance();

            else if constexpr (std::is_same_v<char_event, event_type>)
                return event::char_event_dispatcher::instance();
            else if constexpr (std::is_same_v<drop_event, event_type>)
                return event::drop_event_dispatcher::instance();

            else if constexpr (std::is_same_v<window_focus_event, event_type>)
                return event::window_focus_event_dispatcher::instance();
            else if constexpr (std::is_same_v<window_pos_event, event_type>)
                return event::window_pos_event_dispatcher::instance();
            else if constexpr (std::is_same_v<window_size_event, event_type>)
                return event::window_size_event_dispatcher::instance();
            else if constexpr (std::is_same_v<window_iconify_event, event_type>)
                return event::window_iconify_event_dispatcher::instance();
            else if constexpr (std::is_same_v<window_maximize_event, event_type>)
                return event::window_maximize_event_dispatcher::instance();
            else if constexpr (std::is_same_v<framebuffer_size_event, event_type>)
                return event::framebuffer_size_event_dispatcher::instance();
            else
                static_assert(false, "not find dispatcher for this event_type");
        }
    };
    template <typename... Events>
    static constexpr void subscribe_all(void *ptr)
    {
        (event_dispatcher<Events>::instance().subscribe(ptr, &forwarding<Events>), ...);
    }
    template <typename... Events>
    static constexpr void unsubscribe_all(void *ptr) noexcept
    {
        static_assert((noexcept(event_dispatcher<Events>::instance().unsubscribe(
                           ptr, &forwarding<Events>)) &&
                       ...),
                      "~class() need noexcept is true");
        (event_dispatcher<Events>::instance().unsubscribe(ptr, &forwarding<Events>), ...);
    }

    // NOTE: 生成 阶 [二]·指针族
    constexpr explicit glfw_input_translation()
    {
        subscribe_all<keyboard_event, mousebutton_event, scroll_event, position2d_event,
                      cursor_enter_event, char_event, drop_event, window_focus_event,
                      window_pos_event, window_size_event, window_iconify_event,
                      window_maximize_event, framebuffer_size_event>(this);
    }
    constexpr ~glfw_input_translation() noexcept
    {
        unsubscribe_all<keyboard_event, mousebutton_event, scroll_event, position2d_event,
                        cursor_enter_event, char_event, drop_event, window_focus_event,
                        window_pos_event, window_size_event, window_iconify_event,
                        window_maximize_event, framebuffer_size_event>(this);
    }
    glfw_input_translation(const glfw_input_translation &) = delete;
    glfw_input_translation(glfw_input_translation &&) = delete;
    glfw_input_translation &operator=(const glfw_input_translation &) = delete;
    glfw_input_translation &operator=(glfw_input_translation &&) = delete;

    template <typename event_type>
    constexpr void subscribe(SubscriptionForwarder<event_type>::callback_type cb)
    {
    }

    template <typename event_type>
    constexpr void unsubscribe(SubscriptionForwarder<event_type>::callback_type cb)
    {
    }
    // distribute

    template <typename Obj>
        requires(requires() {
            proxy_class_type::build<"input_event", data_tuple, Obj>();
        })
    constexpr void setReceiver(Obj &obj) noexcept
    {
        receiver_ = {&obj, proxy_class_type::build<"input_event", data_tuple, Obj>()};
    }

  private:
    receiver receiver_;
    cursor_enter_event cursorEnter_;
    ModifierKey modifier_;
};
struct glfw_gestures
{
};

// 1. 第一个接受（accept）或最后一个不拒绝（reject）的成员获胜
//  1. 阶段一：入场与关闭： 当手指按下（PointerDownEvent），找人，广播 来参赛
//  2. 阶段二：竞争与裁决： GestureArenaEntry.resolve() 方法主动声明自己的结果（胜利或认输）
//      主动退出（Self-ejection）：在后续的 PointerMoveEvent 中，识别器会根据自身规则判断是否该退出。
//                                 例如，当手指移动距离超过阈值（kTouchSlop），TapGestureRecognizer 会判定这不是一次点击，
//                                  于是调用 resolve(GestureDisposition.rejected) 主动认输并退出竞争
//      被动胜出与强制清扫（Sweep）：
//           被动胜出：  当其他竞争者都主动退出后，最后一个留在场上的识别器自动获胜，被调用 acceptGesture()，从而触发其手势回调
//           强制清扫：如果在手指抬起（PointerUpEvent）时竞技场仍未决出胜者（例如所有识别器都在观望），
//                      GestureArenaManager 会执行 sweep()，强制将胜利授予第一个加入竞技场的成员，以避免交互死锁
//  3. 阶段三：结果通知：每个成员在它加入的每个竞技场中，acceptGesture 或 rejectGesture 两者必被调用其一

class GestureArenaManager
{
  public:
    enum class Stage : std::uint8_t
    {
        broadcast,
    };

    enum class PointerStatus : std::uint8_t
    {
        Hover,
        Down,
        Up,
        DownMove,
    };
    enum class KeyStatus : std::uint8_t
    {
        eRELEASE, // 释放
        ePRESS,   // 按下
    };

    enum class MouseButton : std::uint8_t
    {
        eUNDEFINED = 0xFF, //NOTE: 特别映射
        eMOUSE_BUTTON_1 = 0,
        eMOUSE_BUTTON_2 = 1,
        eMOUSE_BUTTON_3 = 2,
        eMOUSE_BUTTON_4 = 3,
        eMOUSE_BUTTON_5 = 4,
        eMOUSE_BUTTON_6 = 5,
        eMOUSE_BUTTON_7 = 6,
        eMOUSE_BUTTON_8 = 7,

        // NOTE: 别名
        eMOUSE_BUTTON_LAST = eMOUSE_BUTTON_8,   // = 7
        eMOUSE_BUTTON_LEFT = eMOUSE_BUTTON_1,   // = 0
        eMOUSE_BUTTON_RIGHT = eMOUSE_BUTTON_2,  // = 1
        eMOUSE_BUTTON_MIDDLE = eMOUSE_BUTTON_3, // = 2

        eSIZE = static_cast<std::uint8_t>(eMOUSE_BUTTON_LAST) + 1
    };
    static_assert(std::to_underlying(MouseButton::eSIZE) == 8);
    static_assert(std::to_underlying(MouseButton{}) == 0);

    [[nodiscard]] static constexpr auto gen_mouse_button(
        glfw_input_translation::mousebutton_event e) noexcept
    {
        using form = decltype(e.button);
        using to = MouseButton;

        switch (e.button)
        {
        case form::eMOUSE_BUTTON_LEFT:
            return to::eMOUSE_BUTTON_LEFT;
        case form::eMOUSE_BUTTON_RIGHT:
            return to::eMOUSE_BUTTON_RIGHT;
        case form::eMOUSE_BUTTON_MIDDLE:
            return to::eMOUSE_BUTTON_MIDDLE;
        case form::eMOUSE_BUTTON_4:
            return to::eMOUSE_BUTTON_4;
        case form::eMOUSE_BUTTON_5:
            return to::eMOUSE_BUTTON_5;
        case form::eMOUSE_BUTTON_6:
            return to::eMOUSE_BUTTON_6;
        case form::eMOUSE_BUTTON_7:
            return to::eMOUSE_BUTTON_7;
        case form::eMOUSE_BUTTON_LAST:
            return to::eMOUSE_BUTTON_LAST;
        default:
            return to::eUNDEFINED;
        }
    }
    using mouse_button_value_type = std::underlying_type_t<MouseButtons>;

    // NOTE: 必须在这定义,事件的全部结构体. 然后 识别器使用我们这里定义的结构体
    // NOTE: 至少希望.内存的大小一致,跨平台,就算不行.名字,也必须有接口的约束效果
    struct mouse_button_event
    {
        [[nodiscard]] constexpr bool is_left() const noexcept
        {
            return value_ == std::to_underlying(MouseButtons::eMOUSE_BUTTON_LEFT);
        }
        [[nodiscard]] constexpr bool is_right() const noexcept
        {
            return value_ == std::to_underlying(MouseButtons::eMOUSE_BUTTON_RIGHT);
        }
        [[nodiscard]] constexpr bool is_middle() const noexcept
        {
            return value_ == std::to_underlying(MouseButtons::eMOUSE_BUTTON_MIDDLE);
        }
        [[nodiscard]] constexpr auto value() const noexcept
        {
            return value_;
        }

      private:
        mouse_button_value_type value_;
    };

    template <typename event_type>
    static consteval bool requires_type()
    {
        return []<class... Ts>(std::tuple<Ts...> *) {
            return (std::is_same_v<Ts, event_type> || ...);
        }(static_cast<glfw_input_translation::data_tuple *>(nullptr));
    }
    template <typename event_type>
        requires(requires_type<event_type>())
    constexpr void input_event(event_type e) noexcept
    {
        // NOTE: 第一步确定  鼠标  处于 哪个地盘？让后接下来的手势事件 应该和这个地盘的 识别器强关联
        // NOTE: 键盘 不是 手势 事件，应该怎么样？我觉得这也是竞技场确定
        // NOTE: 第一步是 竞技场的建立，竞技场内部的 成员，可用是竞技场本身，本来就是递归无效的

        // NOTE: 确定竞技场建立完毕后，数据广播即可，满足就达到。谁先一定OK，剩下的，全部扫空

        // NOTE: 文字流应该是唯一的，对不对？对的。因此。GestureArenaManager 弄一个文字流

        // NOTE: 快捷键 也是 看 地区的？应该是的。快捷键当普通的按键处理就好了

        // NOTE: 鼠标和键盘高度没有任何问题了

        // NOTE: 要从 手势事件中，开始和结束。
        if constexpr (std::is_same_v<glfw_input_translation::mousebutton_event,
                                     event_type>)
        {
            mouse_button_event{gen_mouse_button(e)};
            // NOTE: 按下和抬起，确定开始和结束？
            if (glfw_input_translation::mousebutton_event{e}.press())
            {
                event::PointerDownEvent down{event::PointerDeviceKind::eMOUSE};
                pointerStatus_ = PointerStatus::Down;
            }
            else
            {
                event::PointerUpEvent up{event::PointerDeviceKind::eMOUSE};
                pointerStatus_ = PointerStatus::Up;
            }
        }
        else if (std::is_same_v<glfw_input_translation::position2d_event, event_type>)
        {
            // NOTE: 光标移动的过程，可能触发 事件感知区域，这个时候，追加事件感知器吗？

            if (pointerStatus_ == PointerStatus::Up)
            {
                pointerStatus_ = PointerStatus::Hover;
                event::PointerHoverEvent hover{event::PointerDeviceKind::eMOUSE};
            }
            else if (pointerStatus_ == PointerStatus::Down)
            {
                pointerStatus_ = PointerStatus::DownMove;
                event::PointerMoveEvent move{event::PointerDeviceKind::eMOUSE};
            }
        }
        else if (std::is_same_v<glfw_input_translation::keyboard_event, event_type>)
        {
            // 键盘
            modifier_ = glfw_input_translation::keyboard_event{e}.modifier_key;
        }
        else if (std::is_same_v<glfw_input_translation::scroll_event, event_type>)
        {
            // 鼠标中间滚轮
        }
        else if (std::is_same_v<glfw_input_translation::cursor_enter_event, event_type>)
        {
            // 窗口/进入退出
        }
        else if (std::is_same_v<glfw_input_translation::char_event, event_type>)
        {
            // NOTE: 英文周键盘事件,也走 char_event
            // 输入法的字符
        }
        else if (std::is_same_v<glfw_input_translation::drop_event, event_type>)
        {
            // 曹操系统 drop
        }
        else
        {
            // 窗口事件
        }
    }

  private:
    PointerStatus pointerStatus_;
    // NOTE: 控制键.当作状态就好了. 不严格检查 left_ctrl 还是 right_ctrl . 简化一下.这样如果快捷键 一个 键,会非常简单
    ModifierKey modifier_{ModifierKey::None()};

    // NOTE: 需要捕获: 历史记录吗?
};

void test() {}

struct gestures
{
    using result = std::variant<std::monostate, int, double>;
    std::move_only_function<int(void *, void *) noexcept> step_0;
    std::move_only_function<double(void *, void *) noexcept> step_1;
    result result_;

    constexpr void resume() noexcept
    {
        // ？
        if (result_.index() == 0)
            return;
    }
};
// NOTE: 落了下乘。

using Clock = std::chrono::steady_clock;
using time_point = Clock::time_point;

// ═══════════════════════════════════════════════════════════════════════
// 识别器状态机
// ═══════════════════════════════════════════════════════════════════════
enum class RecogState : std::uint8_t
{
    Idle,     // 未参与——不在竞技场，不跟踪任何 pointer
    Possible, // 已入场——在竞技场中竞争，尚未赢
    Accepted, // 已赢——后续事件直接由识别器处理，不再走竞技场
};

// ═══════════════════════════════════════════════════════════════════════
// 手势枚举与产出事件
// ═══════════════════════════════════════════════════════════════════════
enum class MouseGesture : std::uint8_t
{
    Click,
    DoubleClick,
    LongPressStart,
    LongPressMoveUpdate,
    LongPressEnd,
    DragStart,
    DragUpdate,
    DragEnd,
    ScrollUp,
    ScrollDown,
    ScrollLeft,
    ScrollRight,
};

struct GestureEvent
{
    MouseGesture kind;
    MouseButtons button = MouseButtons::eUNDEFINED;
    ModifierKey modifier = ModifierKey::None();
    double x{}, y{};
    double x0{}, y0{};
};

namespace std
{
    template <>
    struct formatter<MouseGesture>
    {
        static constexpr auto parse(std::format_parse_context &ctx) noexcept
        {
            return ctx.begin();
        }
        static auto format(MouseGesture g, std::format_context &ctx)
        {
            auto n = [&] {
                switch (g)
                {
                case MouseGesture::Click:
                    return "Click";
                case MouseGesture::DoubleClick:
                    return "DoubleClick";
                case MouseGesture::LongPressStart:
                    return "LongPressStart";
                case MouseGesture::LongPressMoveUpdate:
                    return "LongPressMoveUpdate";
                case MouseGesture::LongPressEnd:
                    return "LongPressEnd";
                case MouseGesture::DragStart:
                    return "DragStart";
                case MouseGesture::DragUpdate:
                    return "DragUpdate";
                case MouseGesture::DragEnd:
                    return "DragEnd";
                case MouseGesture::ScrollUp:
                    return "ScrollUp";
                case MouseGesture::ScrollDown:
                    return "ScrollDown";
                case MouseGesture::ScrollLeft:
                    return "ScrollLeft";
                case MouseGesture::ScrollRight:
                    return "ScrollRight";
                }
                return "?";
            }();
            return std::format_to(ctx.out(), "{}", n);
        }
    };
} // namespace std

// ═══════════════════════════════════════════════════════════════════════
// 竞技场成员票据
// ═══════════════════════════════════════════════════════════════════════
struct ArenaMember
{
    void *self = nullptr;
    void (*accept)(void *, std::vector<GestureEvent> &) noexcept = nullptr;
    void (*reject)(void *) noexcept = nullptr;

    [[nodiscard]] bool empty() const noexcept
    {
        return self == nullptr;
    }
    bool operator==(const ArenaMember &o) const noexcept
    {
        return self == o.self;
    }
};

template <typename T>
constexpr ArenaMember make_member(T *obj) noexcept
{
    return ArenaMember{obj,
                       [](void *s, std::vector<GestureEvent> &out) noexcept {
                           static_cast<T *>(s)->template invoke<"accept_gesture">(out);
                       },
                       [](void *s) noexcept {
                           static_cast<T *>(s)->template invoke<"reject_gesture">();
                       }};
}

// ═══════════════════════════════════════════════════════════════════════
// main
// ═══════════════════════════════════════════════════════════════════════
int main()
try
{
    surface window{};
    window.setup({.width = WIDTH, .height = HEIGHT}, TITLE); // NOLINT

    glfw_input_translation input;

    static constexpr auto DBL_WINDOW = std::chrono::milliseconds(250);
    static constexpr auto LONG_PRESS = std::chrono::milliseconds(500);
    static constexpr double DRAG_SLOP_SQ = 16.0;

    // ══════════════════════════════════════════════════════
    // 识别器 1：单击
    //   Idle  --PRESS-->         Possible（入场，挂 pending）
    //   Possible --tick 超窗-->  resolve(true)  --accept--> 产出 Click, 回 Idle
    //   Possible --第二击/长按--> resolve(false) --reject--> 回 Idle
    // ══════════════════════════════════════════════════════
    auto click_recognizer = make_aggregate<"ClickRecognizer">(
        field<"state">(RecogState::Idle), field<"pending">(std::optional<GestureEvent>{}),
        field<"press_time">(std::optional<time_point>{}),
        field<"pending_time">(std::optional<time_point>{}), field<"press_x">(0.0),
        field<"press_y">(0.0),

        method<"input_event">(
            []<class Arena, class E>(auto &&self, Arena &arena,
                                     std::vector<GestureEvent> & /*out*/, int pointer,
                                     E e) noexcept
                requires(std::same_as<E, mousebutton_event>)
            {
                if (e.press())
                {
                    // 已经在 Possible 状态——这是第二击，我不是单击
                    if (self.state == RecogState::Possible)
                    {
                        self.state = RecogState::Idle;
                        self.pending.reset();
                        self.pending_time.reset();
                        self.press_time.reset();
                        arena.resolve(arena, pointer, make_member(&self), false);
                        return;
                    }
                    self.state = RecogState::Possible;
                    self.press_x = arena.cursor_x;
                    self.press_y = arena.cursor_y;
                    self.press_time = Clock::now();
                    self.pending =
                        GestureEvent{MouseGesture::Click, e.button, e.modifier_key};
                    arena.add(arena, pointer, make_member(&self));
                    return;
                }

                // RELEASE
                if (self.state != RecogState::Possible || !self.press_time)
                    return;
                auto dur = Clock::now() - *self.press_time;
                self.press_time.reset();
                if (dur >= LONG_PRESS)
                {
                    // 长按——我不是单击
                    self.state = RecogState::Idle;
                    self.pending.reset();
                    self.pending_time.reset();
                    arena.resolve(arena, pointer, make_member(&self), false);
                    return;
                }
                // 短按：进入挂起，等 tick 超窗
                self.pending_time = Clock::now();
            }),

            method<"tick">([](auto &&self, auto &arena) noexcept {
                if (self.state == RecogState::Possible && self.pending &&
                    self.pending_time && Clock::now() - *self.pending_time > DBL_WINDOW)
                {
                    self.pending_time.reset();
                    arena.resolve(arena, 0, make_member(&self), true);
                }
            }),

            method<"accept_gesture">(
                [](auto &&self, std::vector<GestureEvent> &out) noexcept {
                    if (self.pending)
                    {
                        auto g = *self.pending;
                        g.x = self.press_x;
                        g.y = self.press_y;
                        out.push_back(g);
                        self.pending.reset();
                    }
                    self.pending_time.reset();
                    self.state = RecogState::Idle; // 单击一次性，立即回 Idle
                }),

            method<"reject_gesture">([](auto &&self) noexcept {
                self.pending.reset();
                self.pending_time.reset();
                self.press_time.reset();
                self.state = RecogState::Idle;
            }));

    // ══════════════════════════════════════════════════════
    // 识别器 2：双击
    //
    //   关键：first_tap_time / first_tap_button 是"记忆"，
    //   accept/reject 时不清除，只在"新一轮开始"或"超窗覆盖"时才写。
    // ══════════════════════════════════════════════════════
    auto dbl_recognizer = make_aggregate<"DoubleClickRecognizer">(
        field<"state">(RecogState::Idle), field<"pending">(std::optional<GestureEvent>{}),
        field<"first_tap_time">(std::optional<time_point>{}), // ← 记忆
        field<"first_tap_button">(MouseButtons::eUNDEFINED),  // ← 记忆
        field<"press_x">(0.0), field<"press_y">(0.0),

        method<"input_event">([]<class Arena>(auto &&self, Arena &arena,
                                              std::vector<GestureEvent> & /*out*/,
                                              int pointer,
                                              const mousebutton_event &e) noexcept {
            if (!e.press())
                return;
            auto now = Clock::now();

            // ── 第二击检查 ──
            // 只有当"我还在竞场里（Possible）"且"上一次按下在窗口内、同键"时才认第二击。
            // 已 Idle（上一轮已结束）则走新一轮，而不是误判。
            if (self.state == RecogState::Possible && self.first_tap_time &&
                (now - *self.first_tap_time < DBL_WINDOW) &&
                self.first_tap_button == e.button)
            {
                arena.resolve(arena, pointer, make_member(&self), true);
                return;
            }

            // ── 开新一轮 ──
            // 若上一轮还在 Possible（罕见，比如 click 尚未 tick 超时），先弃权
            if (self.state == RecogState::Possible)
            {
                self.pending.reset();
                arena.resolve(arena, pointer, make_member(&self), false);
            }

            self.state = RecogState::Possible;
            self.first_tap_time = now; // 记忆：新一轮的第一次按下
            self.first_tap_button = e.button;
            self.press_x = arena.cursor_x;
            self.press_y = arena.cursor_y;
            self.pending =
                GestureEvent{MouseGesture::DoubleClick, e.button, e.modifier_key};
            arena.add(arena, pointer, make_member(&self));
        }),

        method<"tick">([](auto &&, auto &) noexcept {}),

        method<"accept_gesture">(
            [](auto &&self, std::vector<GestureEvent> &out) noexcept {
                if (self.pending)
                {
                    auto g = *self.pending;
                    g.x = self.press_x;
                    g.y = self.press_y;
                    out.push_back(g);
                    self.pending.reset();
                }
                self.state = RecogState::Idle;
                // ★ first_tap_time / first_tap_button 保留——下一次 PRESS 用它判断
            }),

        method<"reject_gesture">([](auto &&self) noexcept {
            self.pending.reset();
            self.state = RecogState::Idle;
            // ★ first_tap_time / first_tap_button 保留
        }));

    // ══════════════════════════════════════════════════════
    // 识别器 3：长按（三段式）
    //   Idle      --PRESS-->              Possible（入场）
    //   Possible  --tick ≥500ms-->        resolve(true) --accept--> LongPressStart, -> Accepted
    //   Possible  --MOVE 超 slop/早松手--> resolve(false) --reject--> 回 Idle
    //   Accepted  --MOVE-->               产出 LongPressMoveUpdate
    //   Accepted  --RELEASE-->            产出 LongPressEnd, 回 Idle
    // ══════════════════════════════════════════════════════
    auto longpress_recognizer = make_aggregate<"LongPressRecognizer">(
        field<"state">(RecogState::Idle), field<"pending">(std::optional<GestureEvent>{}),
        field<"press_time">(std::optional<time_point>{}),
        field<"press_button">(MouseButtons::eUNDEFINED),
        field<"press_mod">(ModifierKey::None()), field<"press_x">(0.0),
        field<"press_y">(0.0), field<"cursor_x">(0.0), field<"cursor_y">(0.0),

        method<"input_event">(
            []<class Arena, class E>(auto &&self, Arena &arena,
                                     std::vector<GestureEvent> &out, int pointer,
                                     E e) noexcept
                requires(std::same_as<E, mousebutton_event> ||
                         std::same_as<E, position2d_event>)
            {
                if constexpr (std::same_as<E, position2d_event>)
                {
                    self.cursor_x = e.xpos;
                    self.cursor_y = e.ypos;
                }

                // ── Accepted：直接产出 Update/End，不再走竞技场 ──
                if (self.state == RecogState::Accepted)
                {
                    if constexpr (std::same_as<E, mousebutton_event>)
                    {
                        if (!e.release())
                            return;
                        GestureEvent g{};
                        g.kind = MouseGesture::LongPressEnd;
                        g.button = self.press_button;
                        g.modifier = self.press_mod;
                        g.x = self.cursor_x;
                        g.y = self.cursor_y;
                        g.x0 = self.press_x;
                        g.y0 = self.press_y;
                        out.push_back(g);

                        self.state = RecogState::Idle;
                        self.press_button = MouseButtons::eUNDEFINED;
                        self.press_mod = ModifierKey::None();
                    }
                    else
                    {
                        GestureEvent g{};
                        g.kind = MouseGesture::LongPressMoveUpdate;
                        g.button = self.press_button;
                        g.modifier = self.press_mod;
                        g.x = self.cursor_x;
                        g.y = self.cursor_y;
                        g.x0 = self.press_x;
                        g.y0 = self.press_y;
                        out.push_back(g);
                    }
                    return;
                }

                // ── Idle：只响应 PRESS ──
                if (self.state == RecogState::Idle)
                {
                    if constexpr (std::same_as<E, mousebutton_event>)
                    {
                        if (!e.press())
                            return;
                        self.state = RecogState::Possible;
                        self.press_time = Clock::now();
                        self.press_button = e.button;
                        self.press_mod = e.modifier_key;
                        self.press_x = arena.cursor_x;
                        self.press_y = arena.cursor_y;
                        self.pending = GestureEvent{MouseGesture::LongPressStart,
                                                    e.button, e.modifier_key};
                        arena.add(arena, pointer, make_member(&self));
                    }
                    return;
                }

                // ── Possible：竞技场阶段 ──
                if constexpr (std::same_as<E, mousebutton_event>)
                {
                    if (e.release() && self.press_time)
                    {
                        // 没到超时就松手 → 弃权
                        self.press_time.reset();
                        self.pending.reset();
                        self.state = RecogState::Idle;
                        arena.resolve(arena, pointer, make_member(&self), false);
                    }
                }
                else // position2d
                {
                    if (self.press_time)
                    {
                        double dx = self.cursor_x - self.press_x;
                        double dy = self.cursor_y - self.press_y;
                        if (dx * dx + dy * dy > DRAG_SLOP_SQ)
                        {
                            self.press_time.reset();
                            self.pending.reset();
                            self.state = RecogState::Idle;
                            arena.resolve(arena, pointer, make_member(&self), false);
                        }
                    }
                }
            }),

            method<"tick">([](auto &&self, auto &arena) noexcept {
                if (self.state == RecogState::Possible && self.press_time &&
                    Clock::now() - *self.press_time >= LONG_PRESS)
                {
                    self.press_time.reset();
                    arena.resolve(arena, 0, make_member(&self), true);
                }
            }),

            method<"accept_gesture">(
                [](auto &&self, std::vector<GestureEvent> &out) noexcept {
                    if (self.pending)
                    {
                        auto g = *self.pending;
                        g.x = self.press_x;
                        g.y = self.press_y;
                        out.push_back(g);
                        self.pending.reset();
                    }
                    self.state = RecogState::Accepted; // 保持——继续产出 Update/End
                }),

            method<"reject_gesture">([](auto &&self) noexcept {
                self.pending.reset();
                self.press_time.reset();
                self.state = RecogState::Idle;
            }));

    // ══════════════════════════════════════════════════════
    // 识别器 4：拖拽（三段式）
    //   Idle      --PRESS-->              Possible（入场）
    //   Possible  --MOVE 超 slop-->       resolve(true) --accept--> DragStart, -> Accepted
    //   Possible  --RELEASE 早-->         resolve(false) --reject--> 回 Idle
    //   Accepted  --MOVE-->               产出 DragUpdate
    //   Accepted  --RELEASE-->            产出 DragEnd, 回 Idle
    // ══════════════════════════════════════════════════════
    auto drag_recognizer = make_aggregate<"DragRecognizer">(
        field<"state">(RecogState::Idle), field<"pending">(std::optional<GestureEvent>{}),
        field<"press_time">(std::optional<time_point>{}),
        field<"origin">(std::pair<double, double>{0.0, 0.0}),
        field<"cursor">(std::pair<double, double>{0.0, 0.0}),
        field<"drag_button">(MouseButtons::eUNDEFINED),
        field<"drag_mod">(ModifierKey::None()),

        method<"input_event">(
            []<class Arena, class E>(auto &&self, Arena &arena,
                                     std::vector<GestureEvent> &out, int pointer,
                                     E e) noexcept
                requires(std::same_as<E, mousebutton_event> ||
                         std::same_as<E, position2d_event>)
            {
                if constexpr (std::same_as<E, position2d_event>)
                {
                    self.cursor = {e.xpos, e.ypos};
                }

                // ── Accepted：直接产出 Update/End ──
                if (self.state == RecogState::Accepted)
                {
                    if constexpr (std::same_as<E, mousebutton_event>)
                    {
                        if (!e.release())
                            return;
                        GestureEvent g{};
                        g.kind = MouseGesture::DragEnd;
                        g.button = self.drag_button;
                        g.modifier = self.drag_mod;
                        g.x = self.cursor.first;
                        g.y = self.cursor.second;
                        g.x0 = self.origin.first;
                        g.y0 = self.origin.second;
                        out.push_back(g);

                        self.state = RecogState::Idle;
                        self.drag_button = MouseButtons::eUNDEFINED;
                        self.drag_mod = ModifierKey::None();
                    }
                    else
                    {
                        GestureEvent g{};
                        g.kind = MouseGesture::DragUpdate;
                        g.button = self.drag_button;
                        g.modifier = self.drag_mod;
                        g.x = self.cursor.first;
                        g.y = self.cursor.second;
                        g.x0 = self.origin.first;
                        g.y0 = self.origin.second;
                        out.push_back(g);
                    }
                    return;
                }

                // ── Idle：只响应 PRESS ──
                if (self.state == RecogState::Idle)
                {
                    if constexpr (std::same_as<E, mousebutton_event>)
                    {
                        if (!e.press())
                            return;
                        self.state = RecogState::Possible;
                        self.press_time = Clock::now();
                        self.origin = {arena.cursor_x, arena.cursor_y};
                        self.drag_button = e.button;
                        self.drag_mod = e.modifier_key;
                        self.pending = GestureEvent{MouseGesture::DragStart, e.button,
                                                    e.modifier_key};
                        arena.add(arena, pointer, make_member(&self));
                    }
                    return;
                }

                // ── Possible：竞技场阶段 ──
                if constexpr (std::same_as<E, mousebutton_event>)
                {
                    if (e.release() && self.press_time)
                    {
                        self.press_time.reset();
                        self.pending.reset();
                        self.state = RecogState::Idle;
                        arena.resolve(arena, pointer, make_member(&self), false);
                    }
                }
                else
                {
                    if (self.press_time)
                    {
                        double dx = self.cursor.first - self.origin.first;
                        double dy = self.cursor.second - self.origin.second;
                        if (dx * dx + dy * dy > DRAG_SLOP_SQ)
                        {
                            self.press_time.reset();
                            arena.resolve(arena, pointer, make_member(&self), true);
                        }
                    }
                }
            }),

            method<"tick">([](auto &&, auto &) noexcept {}),

            method<"accept_gesture">(
                [](auto &&self, std::vector<GestureEvent> &out) noexcept {
                    if (self.pending)
                    {
                        auto g = *self.pending;
                        g.kind = MouseGesture::DragStart;
                        g.x = self.cursor.first;
                        g.y = self.cursor.second;
                        g.x0 = self.origin.first;
                        g.y0 = self.origin.second;
                        out.push_back(g);
                        self.pending.reset();
                    }
                    self.state = RecogState::Accepted;
                }),

            method<"reject_gesture">([](auto &&self) noexcept {
                self.pending.reset();
                self.press_time.reset();
                self.state = RecogState::Idle;
            }));

    // ══════════════════════════════════════════════════════
    // 竞技场
    // ══════════════════════════════════════════════════════
    auto arena = make_aggregate<"MouseGestureArena">(
        field<"click">(click_recognizer), field<"dbl">(dbl_recognizer),
        field<"longpress">(longpress_recognizer), field<"drag">(drag_recognizer),

        field<"arena_members">(std::vector<ArenaMember>{}),
        field<"eager_winner">(ArenaMember{}), field<"is_open">(true),
        field<"is_held">(false), field<"has_pending_sweep">(false),

        field<"output">(std::vector<GestureEvent>{}),

        field<"cursor_x">(0.0), field<"cursor_y">(0.0), field<"press_x">(0.0),
        field<"press_y">(0.0), field<"press_button">(MouseButtons::eUNDEFINED),
        field<"press_mod">(ModifierKey::None()),

        method<"add">([](auto &&self, int /*pointer*/, ArenaMember m) noexcept {
            if (!self.is_open)
                return;
            self.arena_members.push_back(m);
        }),

        method<"resolve">(
            [](auto &&self, int pointer, ArenaMember m, bool accepted) noexcept {
                if (accepted)
                {
                    if (self.is_open)
                    {
                        if (self.eager_winner.empty())
                            self.eager_winner = m;
                    }
                    else
                    {
                        self.resolve_in_favor_of(self, pointer, m);
                    }
                }
                else
                {
                    auto &v = self.arena_members;
                    v.erase(std::remove_if(v.begin(), v.end(),
                                           [&m](const ArenaMember &x) noexcept {
                                               return x.self == m.self;
                                           }),
                            v.end());
                    if (m.reject)
                        m.reject(m.self);
                    if (!self.is_open)
                        self.try_to_resolve(self, pointer);
                }
            }),

        method<"close">([](auto &&self, int pointer) noexcept {
            if (!self.is_open)
                return;
            self.is_open = false;
            self.try_to_resolve(self, pointer);
        }),

        method<"try_to_resolve">([](auto &&self, int pointer) noexcept {
            if (self.arena_members.size() == 1)
            {
                self.resolve_by_default(self, pointer, self.arena_members[0]);
            }
            else if (self.arena_members.empty())
            {
                self.reset_arena(self);
            }
            else if (!self.eager_winner.empty())
            {
                ArenaMember w = self.eager_winner;
                self.resolve_in_favor_of(self, pointer, w);
            }
        }),

        method<"resolve_by_default">(
            [](auto &&self, int /*pointer*/, ArenaMember only) noexcept {
                if (only.accept)
                    only.accept(only.self, self.output);
                self.reset_arena(self);
            }),

        method<"resolve_in_favor_of">(
            [](auto &&self, int /*pointer*/, ArenaMember winner) noexcept {
                for (ArenaMember &m : self.arena_members)
                {
                    if (m.self != winner.self && m.reject)
                        m.reject(m.self);
                }
                if (winner.accept)
                    winner.accept(winner.self, self.output);
                self.reset_arena(self);
            }),

        method<"sweep">([](auto &&self, int pointer) noexcept {
            if (self.is_held)
            {
                self.has_pending_sweep = true;
                return;
            }
            if (!self.arena_members.empty())
            {
                ArenaMember first = self.arena_members[0];
                for (std::size_t i = 1; i < self.arena_members.size(); ++i)
                {
                    ArenaMember &m = self.arena_members[i];
                    if (m.reject)
                        m.reject(m.self);
                }
                if (first.accept)
                    first.accept(first.self, self.output);
            }
            self.reset_arena(self);
            (void)pointer;
        }),

        method<"hold">(
            [](auto &&self, int /*pointer*/) noexcept { self.is_held = true; }),

        method<"release">([](auto &&self, int pointer) noexcept {
            self.is_held = false;
            if (self.has_pending_sweep)
            {
                self.has_pending_sweep = false;
                self.sweep(self, pointer);
            }
        }),

        method<"reset_arena">([](auto &&self) noexcept {
            self.arena_members.clear();
            self.eager_winner = {};
            self.is_open = true;
            self.is_held = false;
            self.has_pending_sweep = false;
        }),

        method<"input_event">(
            []<class E>(auto &&self, E e) noexcept
                requires(std::same_as<E, mousebutton_event> ||
                         std::same_as<E, position2d_event> ||
                         std::same_as<E, scroll_event>)
            {
                if constexpr (std::same_as<E, scroll_event>)
                {
                    GestureEvent g{};
                    if (e.yoffset > 0)
                        g.kind = MouseGesture::ScrollUp;
                    else if (e.yoffset < 0)
                        g.kind = MouseGesture::ScrollDown;
                    else if (e.xoffset > 0)
                        g.kind = MouseGesture::ScrollRight;
                    else if (e.xoffset < 0)
                        g.kind = MouseGesture::ScrollLeft;
                    else
                        return;
                    g.x = e.xoffset;
                    g.y = e.yoffset;
                    self.output.push_back(g);
                }
                else if constexpr (std::same_as<E, mousebutton_event>)
                {
                    if (e.press())
                    {
                        self.press_x = self.cursor_x;
                        self.press_y = self.cursor_y;
                        self.press_button = e.button;
                        self.press_mod = e.modifier_key;
                    }
                    self.click.input_event(self.click, self, self.output, 0, e);
                    self.dbl.input_event(self.dbl, self, self.output, 0, e);
                    self.longpress.input_event(self.longpress, self, self.output, 0, e);
                    self.drag.input_event(self.drag, self, self.output, 0, e);

                    if (e.press())
                        self.close(self, 0);
                }
                else
                {
                    self.cursor_x = e.xpos;
                    self.cursor_y = e.ypos;
                    self.longpress.input_event(self.longpress, self, self.output, 0, e);
                    self.drag.input_event(self.drag, self, self.output, 0, e);
                }
            }),

            method<"tick">([](auto &&self) noexcept {
                self.click.tick(self.click, self);
                self.dbl.tick(self.dbl, self);
                self.longpress.tick(self.longpress, self);
                self.drag.tick(self.drag, self);
            }),

            method<"drain">([](auto &&self, auto &&fn) noexcept {
                for (const auto &g : self.output)
                    fn(g);
                self.output.clear();
            }));

    input.setReceiver(arena);

    while (window.shouldClose() == 0)
    {
        surface::pollEvents();

        arena.tick(arena);

        arena.drain(arena, [](const GestureEvent &g) {
            switch (g.kind)
            {
            case MouseGesture::Click:
            case MouseGesture::DoubleClick:
            case MouseGesture::LongPressStart:
                std::println("[gesture] {} button={} at ({:.0f},{:.0f}) mod={}", g.kind,
                             g.button, g.x, g.y, g.modifier);
                break;

            case MouseGesture::LongPressMoveUpdate:
            case MouseGesture::LongPressEnd:
            case MouseGesture::DragStart:
            case MouseGesture::DragUpdate:
            case MouseGesture::DragEnd:
                std::println(
                    "[gesture] {} button={} from ({:.0f},{:.0f}) to ({:.0f},{:.0f})",
                    g.kind, g.button, g.x0, g.y0, g.x, g.y);
                break;

            case MouseGesture::ScrollUp:
            case MouseGesture::ScrollDown:
            case MouseGesture::ScrollLeft:
            case MouseGesture::ScrollRight:
                std::println("[gesture] {} delta=({:+.2f},{:+.2f})", g.kind, g.x, g.y);
                break;
            }
        });
    }

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}

// ═══════════════════════════════════════════════════════════════════════════
// 鼠标手势识别系统 · 总结
// ═══════════════════════════════════════════════════════════════════════════
//
// 一、整个过程走了什么路
//
//   概念探索 → Dart 蓝图 → Flutter 源码验证 → C++ 移植 → 逐步修正 → 完整跑通
//
//   起点：只有 mousebutton_event / position2d_event / scroll_event
//         三种原始事件流
//   终点：C++ 版识别器 + 竞技场完整运行，输出与 Flutter 语义逐项对齐
//
// ───────────────────────────────────────────────────────────────────────────
//
// 二、核心概念：三个角色 + 一个协议
//
//   三个角色
//     · 识别器     纯状态机。只有自己的状态字段，不认识任何其他角色。
//                  4 个：click / dbl / longpress / drag
//     · 竞技场     裁决中心。管成员列表、eagerWinner、is_open。1 个。
//     · 成员票据   回调表（void* + accept/reject 函数指针）。
//                  每次 add 现场构造。
//
//   一个协议
//     识别器 ──通过参数 arena& ──→ 竞技场
//     竞技场 ──通过票据回调表 ──→ 识别器
//     识别器 ──完全不认识──→ 识别器
//
//   关键：识别器不持有任何外部指针。它通过
//         input_event(arena&, out&, pointer, e) 参数主动交互，
//         通过回调表被动接收 accept_gesture / reject_gesture。
//
//   裁决协议（从 Flutter GestureArena 照搬）
//     add                 入场
//     close               PRESS 分发完毕后封口，不再接受新成员
//     resolve(accepted)   主动申报胜出
//     resolve(rejected)   主动弃权
//     sweep               UP 后强制裁决，第一个成员胜出
//     hold / release      延迟 sweep
//     eagerWinner         开放期间申报的胜者，close 后生效
//     _resolveByDefault   成员只剩一个时自动胜出，不受 hold 影响
//
//   契约：每个成员最终恰好收到一次 accept 或 reject。
//
// ───────────────────────────────────────────────────────────────────────────
//
// 三、状态机（识别器的核心）
//
//   每个识别器用 RecogState 三态：
//
//              PRESS              accept
//     Idle ──────────▶ Possible ──────────▶ Accepted
//      ▲                 │                     │
//      │                 │ reject              │ 产出 End
//      └─────────────────┴─────────────────────┘
//
//     Idle       未参与          只响应 PRESS
//     Possible   已入场，竞争     更新判断，可 resolve
//     Accepted   已赢，继续跟踪   直接产出 Update / End
//
//   这是 Flutter GestureRecognizerState（ready / possible / accepted）
//   的直接本地化。替换了之前的 bool won——三态把"未入场"和"入场未赢"分开，
//   逻辑集中，无残留。
//
// ───────────────────────────────────────────────────────────────────────────
//
// 四、五类手势的完整形态
//
//   手势          事件序列                                        触发条件
//   ────────────  ──────────────────────────────────────────────  ─────────────────────────────
//   Click         Click                                           短按 + 250ms 内无第二击
//   DoubleClick   DoubleClick                                     250ms 内同键第二击
//   LongPress     LongPressStart → LongPressMoveUpdate* → End     按住 ≥500ms（Start 在超时那
//                                                                 一刻产出，不等松手）
//   Drag          DragStart → DragUpdate* → DragEnd               位移 > 4px
//   Scroll        ScrollUp / Down / Left / Right                  不进竞技场，直接产出
//
//   LongPress 和 Drag 都是三段式——Flutter 的
//   onXXXStart / onXXXUpdate / onXXXEnd 的本地化。
//
// ───────────────────────────────────────────────────────────────────────────
//
// 五、踩过的坑（避免重犯）
//
//   语义类
//     · LongPress 在 RELEASE 时产出
//         错。500ms 超时那一刻 didExceedDeadline 就 resolve(accepted)
//     · sweep 是唯一终结机制
//         错。_resolveByDefault 是独立路径，成员数变 1 时触发
//     · hold 影响所有裁决
//         错。hold 只阻止 sweep，不阻止 _resolveByDefault
//     · resolve 是同步的
//         部分路径走 scheduleMicrotask，Dart 测试需 await
//     · eagerWinner 不会失效
//         错。eager winner 在 close 前 reject 时必须清除引用
//
//   测试类
//     · expectAllContracts 检查未参与的成员
//         只检查实际加入竞技场的成员
//     · ArenaTester 引入隐藏状态
//         直接 GestureArenaManager + TestArenaMember 更稳
//     · 同步 test() 里等 microtask
//         断言必须在 microtask 之后
//     · 唯一成员断言 acceptRan == false
//         close 后必然 true，hold 不阻止
//
//   C++ 类
//     · clock 与 <ctime> 的 clock() 冲突
//         改用 Clock（大写）
//     · members 与 aggregate 基类静态成员冲突
//         arena 字段改名 arena_members
//     · auto 在 reflect 上下文推导错误
//         显式写 ArenaMember &m
//     · std::erase(v, m) 触发 operator==
//         手写 remove_if + m.self 比较
//     · formatter parse 不消费说明符
//         {:12} 直接 consteval 崩，parse 必须返回 }
//     · GCC -freflection 把模板 lambda 当 immediate function
//         删 resume 方法，直接从 arena.winner 读
//     · 双击 last_press 在 accept/reject 里被清
//         first_tap_time / first_tap_button 是跨裁决的记忆，不能清
//
// ───────────────────────────────────────────────────────────────────────────
//
// 六、对 C++ 的本地化决策
//
//   保留
//     · 竞技场按 pointer 隔离（Map<int, GestureArena>）
//     · close / sweep / hold / release / resolve 五件套
//     · eagerWinner 在 reject 时清除
//     · 每个成员恰好一次 accept/reject
//     · 唯一成员自动胜出，不受 hold 影响
//
//   本地化
//     · 同步 resolveInFavorOf 替代 scheduleMicrotask
//     · assert 检查 isOpen 边界
//     · std::unordered_map 替代 Map<int, _GestureArena>
//     · void* + 回调表替代 GestureArenaMember 接口
//     · 识别器不继承、不持指针、通过参数访问 arena
//
// ───────────────────────────────────────────────────────────────────────────
//
// 七、运行输出样例（完整手势家族）
//
//   [gesture] Click        button=MOUSE_BUTTON_LEFT   at (401,280) mod=NONE
//   [gesture] DoubleClick  button=MOUSE_BUTTON_LEFT   at (401,280) mod=NONE
//   [gesture] LongPressStart       button=LEFT at (497,278) mod=NONE
//   [gesture] LongPressMoveUpdate  button=LEFT from (497,278) to (496,278)
//   [gesture] LongPressMoveUpdate  button=LEFT from (497,278) to (495,278)
//   [gesture] LongPressEnd         button=LEFT from (497,278) to (494,278)
//   [gesture] DragStart    button=MOUSE_BUTTON_LEFT   from (363,236) to (370,236)
//   [gesture] DragUpdate   button=MOUSE_BUTTON_LEFT   from (363,236) to (375,237)
//   [gesture] DragEnd      button=MOUSE_BUTTON_LEFT   from (363,236) to (411,244)
//   [gesture] ScrollDown   delta=(+0.00,-1.00)
//   [gesture] ScrollUp     delta=(+0.00,+1.00)
//
// ───────────────────────────────────────────────────────────────────────────
//
// 八、未来扩展预留
//
//   · 多 pointer：把单 arena 改成 unordered_map<int, GestureArena>
//                  触摸场景需要，鼠标场景够用
//   · 树结构竞技场：GestureArena 加 parent / children 指针
//                    Flutter 的 GestureArenaTeam 是自相似的嵌套
//   · 新识别器：Pan / Scale / Flick
//              照抄 Drag 的三段式形状，只改判定逻辑
//   · 长按后的 Drag：LongPressAccepted 状态下继续接收 MOVE
//                    可按需扩展为 "LongPress 赢后允许 Drag 接管"
//   · SerialTap：不做单击 vs 双击裁决，改成一个识别器
//                把每次 tap 连 count 一起报出来
//
// ═══════════════════════════════════════════════════════════════════════════

// NOLINTEND