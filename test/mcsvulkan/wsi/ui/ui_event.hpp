#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// UI 事件层：词汇表 + 订阅层 + 运行时命令表
//
//   来源层(硬件) ──► 识别层(候选) ──► 裁决层(竞技场) ──► 订阅层(虚函数)
//                                                        ▲
//                                                本文件定义这一层的语言
//
//   input_sample    来源层样本（识别层的输入）
//   gesture         胜出结构（裁决层的输出）：编译期已知类型，std::visit 直接落到指定虚函数
//   gesture_target  订阅层：一组虚函数，控件只重写自己关心的那几个
//   custom_gesture + command_registry
//                   运行时逃生口：名字(string) + 参数(std::any)，给菜单/脚本/插件用
//
// 为什么核心用强类型，而不是全都 string + std::any：
//   手势的种类、优先级、等待窗口是算法本体，编译期定下来才能被检查、才能 static_assert、
//   才能直接调用（"编译期期望的函数被调用"）；真正需要运行时灵活的只有“控件/命令”那一侧
//   —— 控件千差万别、命令名可能来自脚本或配置。所以强类型走到底，只在 custom_gesture
//   这条支线上开运行时分发，两者不是二选一，而是各管一段。
// ═══════════════════════════════════════════════════════════════════════════

#include <any>
#include <chrono>
#include <cstdint>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

namespace mcs::vulkan::ui
{
    using event_clock = std::chrono::steady_clock;
    using event_time = event_clock::time_point;
    using milliseconds = std::chrono::milliseconds;

    // 追踪输出开关（裁决过程 / 路由决策 / 胜出交付）：运行时开关，默认开。
    // 自测会把它关掉，避免裁决日志淹没断言结果；交互演示时保持打开才好观察。
    inline bool trace_input = true;

    class widget; // 控件树（ui_widget.hpp）：命中测试与冒泡

    // ═══ 来源层样本：只描述硬件发生了什么，不含任何手势含义 ═══
    namespace raw
    {
        struct pointer_down
        {
            event::MouseButtons button;
        };
        struct pointer_up
        {
            event::MouseButtons button;
        };
        struct pointer_move
        {
            double dx;
            double dy;
        };
        struct wheel
        {
            double dx;
            double dy;
        };
        struct key_down
        {
            event::Key key;
            int scancode;
        };
        struct key_repeat
        {
            event::Key key;
            int scancode;
        };
        struct key_up
        {
            event::Key key;
            int scancode;
        };

        using payload = std::variant<pointer_down, pointer_up, pointer_move, wheel,
                                     key_down, key_repeat, key_up>;
    } // namespace raw

    struct input_sample
    {
        event_time time{};
        event::position2d_event pos{};
        event::ModifierKey mods{event::ModifierKey::None()};
        raw::payload payload{};
    };

    template <class T>
    [[nodiscard]] const T *as(const raw::payload &p) noexcept
    {
        return std::get_if<T>(&p);
    }

    // ═══ 事件公共头：谁、何时、在哪、带什么修饰键 ═══
    struct event_context
    {
        event_time time{};
        event::position2d_event pos{};
        event::ModifierKey mods{event::ModifierKey::None()};
    };

    [[nodiscard]] inline event_context ctx_of(const input_sample &s) noexcept
    {
        return event_context{s.time, s.pos, s.mods};
    }

    // ── 层0：原始事件（无歧义，直通派发，不参与竞争） ──
    struct pointer_event : event_context
    {
        event::MouseButtons button{event::MouseButtons::eUNDEFINED};
        event::Action action{event::Action::eUNDEFINED};
    };

    struct pointer_move_event : event_context
    {
        double dx{0.0};
        double dy{0.0};
    };

    struct key_event : event_context
    {
        event::Key key{event::Key::eUNDEFINED};
        event::Action action{event::Action::eUNDEFINED};
        int scancode{event::UNDEFINED_int};
    };

    struct wheel_event : event_context
    {
        double dx{0.0};
        double dy{0.0};
    };

    // ── 层1：语义手势（裁决层的产物，同一物理动作只能有一个胜出） ──
    struct tap_gesture : event_context
    {
        event::MouseButtons button{event::MouseButtons::eUNDEFINED};
        int count{1}; // 1=单击 2=双击 3=三击
        milliseconds held{};
    };

    struct long_press_gesture : event_context
    {
        event::MouseButtons button{event::MouseButtons::eUNDEFINED};
        milliseconds held{};
    };

    enum class drag_phase : std::uint8_t
    {
        eBEGIN,
        eUPDATE,
        eEND,
    };

    struct drag_gesture : event_context
    {
        event::MouseButtons button{event::MouseButtons::eUNDEFINED};
        event::position2d_event from{};
        double total_dx{0.0};
        double total_dy{0.0};
        milliseconds duration{};
        drag_phase phase{drag_phase::eBEGIN};
    };

    struct shortcut_gesture : event_context
    {
        event::Key key{event::Key::eUNDEFINED};
    };

    struct zoom_gesture : event_context
    {
        double delta{0.0};
    };

    // 运行时逃生口：命令名 + 任意参数
    struct custom_gesture : event_context
    {
        std::string name{};
        std::any args{};
    };

    using gesture =
        std::variant<pointer_event, pointer_move_event, key_event, wheel_event,
                     tap_gesture, long_press_gesture, drag_gesture, shortcut_gesture,
                     zoom_gesture, custom_gesture>;

    // ═══ 订阅层：控件重写自己关心的虚函数 ═══
    // 返回值 = 是否已消费；false 表示交回裁决/路由层继续向父控件冒泡。
    class gesture_target
    {
      public:
        virtual ~gesture_target() = default;

        // 原始层
        virtual bool on_pointer_down(const pointer_event &)
        {
            return false;
        }
        virtual bool on_pointer_up(const pointer_event &)
        {
            return false;
        }
        virtual bool on_pointer_move(const pointer_move_event &)
        {
            return false;
        }
        virtual bool on_key(const key_event &)
        {
            return false;
        }
        virtual bool on_wheel(const wheel_event &)
        {
            return false;
        }

        // 语义层（候选胜出后才可能被调用）
        virtual bool on_tap(const tap_gesture &)
        {
            return false;
        }
        virtual bool on_long_press(const long_press_gesture &)
        {
            return false;
        }
        virtual bool on_drag_begin(const drag_gesture &)
        {
            return false;
        }
        virtual bool on_drag_update(const drag_gesture &)
        {
            return false;
        }
        virtual bool on_drag_end(const drag_gesture &)
        {
            return false;
        }
        virtual bool on_shortcut(const shortcut_gesture &)
        {
            return false;
        }
        virtual bool on_zoom(const zoom_gesture &)
        {
            return false;
        }

        // 运行时命令（custom_gesture）；默认不处理，可以继续冒泡
        virtual bool on_command(const custom_gesture &)
        {
            return false;
        }

        // 让通用层能回到控件树（冒泡用）；非控件目标返回 nullptr
        [[nodiscard]] virtual widget *as_widget() noexcept
        {
            return nullptr;
        }
    };

    // ── 运行时命令表：string 名字 → 处理函数，参数用 std::any 统一携带 ──
    // 只在 custom_gesture 上生效，不在手势热路径里做字符串比较。
    class command_registry
    {
      public:
        using handler = std::function<bool(gesture_target &, const std::any &)>;

        static command_registry &instance() noexcept
        {
            static command_registry registry;
            return registry;
        }

        void add(std::string name, handler fn)
        {
            handlers_.insert_or_assign(std::move(name), std::move(fn));
        }

        [[nodiscard]] bool has(std::string_view name) const
        {
            return handlers_.find(std::string{name}) != handlers_.end();
        }

        [[nodiscard]] bool invoke(gesture_target &target, std::string_view name,
                                  const std::any &args) const
        {
            const auto it = handlers_.find(std::string{name});
            if (it == handlers_.end())
                return false;
            return (it->second)(target, args);
        }

      private:
        command_registry() = default;
        std::unordered_map<std::string, handler> handlers_{};
    };

    // ═══ 胜出结构 → 指定虚函数 ═══
    // 编译期展开成直接调用：没有字符串比较、没有 any_cast，编译器能查漏。
    [[nodiscard]] inline bool deliver_to(gesture_target &target, const gesture &g)
    {
        return std::visit(
            [&target](const auto &value) -> bool {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, pointer_event>)
                {
                    return value.action == event::Action::ePRESS
                               ? target.on_pointer_down(value)
                               : target.on_pointer_up(value);
                }
                else if constexpr (std::is_same_v<T, pointer_move_event>)
                    return target.on_pointer_move(value);
                else if constexpr (std::is_same_v<T, key_event>)
                    return target.on_key(value);
                else if constexpr (std::is_same_v<T, wheel_event>)
                    return target.on_wheel(value);
                else if constexpr (std::is_same_v<T, tap_gesture>)
                    return target.on_tap(value);
                else if constexpr (std::is_same_v<T, long_press_gesture>)
                    return target.on_long_press(value);
                else if constexpr (std::is_same_v<T, drag_gesture>)
                {
                    switch (value.phase)
                    {
                    case drag_phase::eBEGIN:
                        return target.on_drag_begin(value);
                    case drag_phase::eUPDATE:
                        return target.on_drag_update(value);
                    case drag_phase::eEND:
                        return target.on_drag_end(value);
                    }
                    return false;
                }
                else if constexpr (std::is_same_v<T, shortcut_gesture>)
                    return target.on_shortcut(value);
                else if constexpr (std::is_same_v<T, zoom_gesture>)
                    return target.on_zoom(value);
                else
                {
                    // 运行时分发：控件自己的 on_command 优先，没人接就查命令表
                    return target.on_command(value) ||
                           command_registry::instance().invoke(target, value.name,
                                                               value.args);
                }
            },
            g);
    }

    // ═══ 打印（验收用） ═══
    [[nodiscard]] inline std::string pos_str(const event::position2d_event &p)
    {
        return std::format("({:.0f},{:.0f})", p.xpos, p.ypos);
    }

    [[nodiscard]] inline const char *tap_name(int count) noexcept
    {
        switch (count)
        {
        case 1:
            return "click";
        case 2:
            return "double_click";
        case 3:
            return "triple_click";
        default:
            return "multi_click";
        }
    }

    [[nodiscard]] inline const char *phase_name(drag_phase p) noexcept
    {
        switch (p)
        {
        case drag_phase::eBEGIN:
            return "begin";
        case drag_phase::eUPDATE:
            return "update";
        case drag_phase::eEND:
            return "end";
        }
        return "?";
    }

    [[nodiscard]] inline std::string describe(const gesture &g)
    {
        return std::visit(
            [](const auto &v) -> std::string {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, pointer_event>)
                    return std::format("pointer_{} {} {}",
                                       v.action == event::Action::ePRESS ? "down" : "up",
                                       v.button, pos_str(v.pos));
                else if constexpr (std::is_same_v<T, pointer_move_event>)
                    return std::format("pointer_move d=({:.0f},{:.0f}) {}", v.dx, v.dy,
                                       pos_str(v.pos));
                else if constexpr (std::is_same_v<T, key_event>)
                    return std::format("key {} {} mods={}", v.key, v.action, v.mods);
                else if constexpr (std::is_same_v<T, wheel_event>)
                    return std::format("wheel dx={:.2f} dy={:.2f} {}", v.dx, v.dy,
                                       pos_str(v.pos));
                else if constexpr (std::is_same_v<T, tap_gesture>)
                    return std::format("{} {} count={} held={}ms {}", tap_name(v.count),
                                       v.button, v.count, v.held.count(), pos_str(v.pos));
                else if constexpr (std::is_same_v<T, long_press_gesture>)
                    return std::format("long_press {} held={}ms {}", v.button,
                                       v.held.count(), pos_str(v.pos));
                else if constexpr (std::is_same_v<T, drag_gesture>)
                    return std::format("drag_{} {} from={} d=({:.0f},{:.0f}) {}ms",
                                       phase_name(v.phase), v.button, pos_str(v.from),
                                       v.total_dx, v.total_dy, v.duration.count());
                else if constexpr (std::is_same_v<T, shortcut_gesture>)
                    return std::format("shortcut {}+{}", v.mods, v.key);
                else if constexpr (std::is_same_v<T, zoom_gesture>)
                    return std::format("zoom delta={:.2f} {}", v.delta, pos_str(v.pos));
                else
                    return std::format("command \"{}\" {}", v.name, pos_str(v.pos));
            },
            g);
    }

} // namespace mcs::vulkan::ui
