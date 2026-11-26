#pragma once
// ============================================================================
// table12_app/host.hpp — window::Binding / window::Host（窗口 layer）
//
//   Binding：一张表在窗口里的呈现（组合 session::Session，不继承）
//   Host   ：一个窗口的宿主（输入路由 / 焦点 / z 序 / 帧号 / picking 回读）
//
//   这一层只做窗口级的事：不计算几何、不改 Model/State（只调 session 的门）。
//   ⚠ app 头：依赖 demo 的框架类型（ui_new / intent / gesture / input_frame /
//   GLFW / ui_dao），必须在这些定义之后 include（见 PLAN §4.5 DAG）。
// ============================================================================

#include "../table12/session.hpp"
#include "glue.hpp"
#include "shader_abi.hpp"

namespace table::window
{
namespace ev = mcs::vulkan::event;

// ABI 别名（不依赖 demo cpp 的 using）
using picking_result = table::platform::PickingResult;
using render_version_t = table::platform::RenderVersion16;

// Binding 在 Host 之前定义，所以 Host 先前置声明
struct Binding;
struct Host;

// 由 window_draw.hpp 提供（Host::on_pick 的落点）；先声明，定义在后面
void on_pick(Binding &binding, const table::platform::PickingResult &r);

    namespace arena_input
    {
        using ::intent::time_point;

        // 语义事件的 payload：只装“识别器给的量”，不装 target
        struct click_payload
        {
            double x{}, y{};
            time_point at{};
        };
        struct drag_payload
        {
            gesture::drag_phase phase{};
            double x{}, y{}, dx{}, dy{};
            double total_dx{}, total_dy{};
            ModifierKey modifier{};
            time_point at{};
        };
        struct wheel_payload
        {
            int delta{};
            time_point at{};
        };
        struct key_payload
        {
            Key key{};
            input_frame::Transition transition{};
            time_point at{};
            bool repeat{};
        };
        struct char_payload
        {
            unsigned codepoint{};
            time_point at{};
        };

        struct none_policy
        {
        };
        // 表的点击不限制按住时长（只有 4px 的抖动阈值）；双击节奏由 core 判定
        struct click_policy
        {
            static constexpr auto button = input_frame::MouseButton::eMOUSE_BUTTON_LEFT;
            static constexpr std::int64_t tap_max_ms = 60000;
            static constexpr double slop_px = mcs::app::drag_slop_px;
        };
        struct right_click_policy
        {
            static constexpr auto button = input_frame::MouseButton::eMOUSE_BUTTON_RIGHT;
            static constexpr std::int64_t tap_max_ms = 60000;
            static constexpr double slop_px = mcs::app::drag_slop_px;
        };

        // Trait 只写 collect（payload 怎么从语义事件来）；
        // 坐标有效性 / press-release / slop / repeat 全在 input_frame + gesture。
        struct Trait_press : gesture::trait_base<click_payload, static_string{"press"},
                                                 static_string{"on_press"}, click_policy>
        {
            static void collect(time_point, payload_t &d,
                                const gesture::click_event &e) noexcept
            {
                d = {e.x, e.y, e.at};
            }
        };
        struct Trait_right_click
            : gesture::trait_base<click_payload, static_string{"right_click"},
                                  static_string{"on_right_click"}, right_click_policy>
        {
            static void collect(time_point, payload_t &d,
                                const gesture::click_event &e) noexcept
            {
                d = {e.x, e.y, e.at};
            }
        };
        struct Trait_drag
            : gesture::trait_base<drag_payload, static_string{"drag"},
                                  static_string{"on_drag"}, mcs::app::policy::drag>
        {
            static void collect(time_point, payload_t &d,
                                const gesture::drag_event &e) noexcept
            {
                d.phase = e.phase;
                d.x = e.x;
                d.y = e.y;
                d.dx = e.dx;
                d.dy = e.dy;
                d.total_dx = e.total_dx;
                d.total_dy = e.total_dy;
                d.modifier = e.modifier;
                d.at = e.at;
            }
        };
        struct Trait_wheel
            : gesture::trait_base<wheel_payload, static_string{"wheel"},
                                  static_string{"on_wheel"}, mcs::app::policy::wheel>
        {
            static void collect(time_point, payload_t &d,
                                const gesture::wheel_event &e) noexcept
            {
                d = {e.delta, e.at};
            }
        };
        struct Trait_key : gesture::trait_base<key_payload, static_string{"key"},
                                               static_string{"on_key"}, none_policy>
        {
            static void collect(time_point, payload_t &d,
                                const gesture::key_event &e) noexcept
            {
                d = {e.key, e.transition, e.at, e.repeat};
            }
        };

        // 直接复用 gesture 的现成识别器：click / right_click / drag / wheel /
        // key_observer。双击/三击不另设识别器：core 的 note_click 在同一次
        // click 里按时间/位置累计（单击不会被“等待双击”吞掉）。
        using table_tree =
            ::intent::all_of<::intent::leaf<gesture::click_template<Trait_press>>,
                           ::intent::leaf<gesture::click_template<Trait_right_click>>,
                           ::intent::leaf<gesture::drag_template<Trait_drag>>,
                           ::intent::leaf<gesture::wheel_template<Trait_wheel>>,
                           ::intent::leaf<gesture::key_observer_template<Trait_key>>>;

        using table_arbiter = ::intent::debug_arbiter<table_tree>;
    } // namespace arena_input

    struct Binding
    {
        // ---- 核心：真值/意图/派生缓存都在 Session（组合，不是继承）
        table::session::Session session;

        // ---- input/draw merge point (window side)
        table::Target pointer{}; // current pointer target (from session::hit)
        picking_result pick{};   // GPU readback (verification only)
        std::uint32_t pick_version{0};

        // ---- input arena：input_frame -> arbiter -> gesture -> on_*
        arena_input::table_arbiter arbiter{};
        input_frame::event_context input_state{};
        ::intent::time_point now{};
        bool esc_cancelled{false}; // on_key 成功取消了 modal，主循环据此吞掉关窗

        // ---- 宿主回调（模型/选择/勾选变化）
        struct callbacks
        {
            std::function<void()> on_model_changed;
            std::function<void()> on_selection_changed;
            std::function<void()> on_checks_changed;
        };
        callbacks cb{};

        // 上一次已通知的选区/勾选快照（去重：输入、筛选、模型都走同一处）
        int cb_r0{-1}, cb_c0{-1}, cb_r1{-1}, cb_c1{-1}, cb_cur_r{-1}, cb_cur_c{-1};
        std::uint64_t cb_checks{0};
        bool cb_valid{false};

        // 唯一输入入口：任何平台事件都从这里进 arena
        template <class E>
        void handle_event(const E &e);
        void advance(double seconds);

        // 给库里的识别器用（drag_template 等按 fn_name 调成员）
        template <static_string Name, class... A>
        constexpr decltype(auto) invoke(A &&...a)
        {
            constexpr auto m = *type_erased_invoker<>::find_function(^^Binding, Name);
            return (this->[:m:])(std::forward<A>(a)...);
        }

        // arena 的 on_*（由 gesture 识别器 win 通过反射调用）
        void on_press(const arena_input::click_payload &e);
        void on_right_click(const arena_input::click_payload &e);
        void on_drag(const arena_input::drag_payload &e);
        void on_wheel(const arena_input::wheel_payload &e);
        void on_key(const arena_input::key_payload &e);
        void on_char(const arena_input::char_payload &e);
        void handle_key_event(const ev::keyboard_event &e);

        // ---- host
        ui_new::ScreenWidget *screen{};
        ui_new::Widget *owner{};
        GLFWwindow *win{};
        float mx{0}, my{0}; // pointer in owner coordinates
        float wx{0}, wy{0}; // raw window cursor
        float sx{1}, sy{1}; // window logical -> owner coordinates
        float vw{1}, vh{1};

        // ---- window host wiring (host lives as long as the window; a table
        // may be destroyed before the window)
        Host *host{nullptr};   // only for ~Binding / detach
        bool keyboard_focus{true};   // set by the host; headless default true
        std::uint64_t draw_serial{}; // mark_drawn dedup: drawn this frame?

        // ~Binding is only declared here: Host is still incomplete.
        // The definition follows Host (see ~Binding below).
        ~Binding();
        Binding() = default;
        Binding(const Binding &) = delete;
        Binding &operator=(const Binding &) = delete;
        Binding(Binding &&) = default;
        Binding &operator=(Binding &&) = default;

        // ---- draw statistics (tests / verification)
        struct draw_info
        {
            std::uint32_t version{};
            std::uint32_t shapes{};
            std::uint32_t texts{};
            std::uint32_t zoned{};
            std::uint32_t instances{};
        };
        draw_info info{};
        std::uint32_t inputs{};
        // 输入产生的语义变化（宿主可以据此回调；draw 不读它）
        struct change_flags
        {
            bool selection{};
            bool checks{};
            bool model{};
        };
        change_flags changes{};
    };

    struct Host
    {
        std::vector<Binding *> tables;     // attach order (focused etc.)
        std::vector<Binding *> draw_order; // this frame's draw order (z)
        Binding *focused{};
        Binding *pressed{};
        Binding *hovered{};
        glfw_input_translation::scoped_binding binding{};
        bool input_attached{};
        double pointer_x{}, pointer_y{};
        bool pointer_inside{true};
        std::uint64_t frame_serial{0};
        picking_result last_pick{};
        // 诊断/ABI 配置：环境变量与全局计数器都只活在这里（核心不认识）
        std::uint32_t hover_fn{~0U};
        std::uint32_t next_table_id{1};
        bool trace{false};

        std::uint32_t allocate_table_id() noexcept
        {
            return next_table_id++;
        }

        ~Host();

        // Construction-time attach: id must be 1..31 (target_key holds 5 bits)
        void attach(Binding &a)
        {
            if (a.session.id.value == 0 || a.session.id.value > 31)
                throw std::runtime_error(std::format(
                    "Host::attach: table_id {} out of range 1..31", a.session.id.value));
            if (find(a.session.id) != nullptr)
                throw std::runtime_error(
                    std::format("Host::attach: duplicate table_id {}", a.session.id.value));
            a.host = this;
            tables.push_back(&a);
            if (!input_attached)
            {
                inputTranslation().attach(binding, *this);
                input_attached = true;
            }
            if (focused == nullptr)
                set_focus(&a);
            else
                a.keyboard_focus = (focused == &a);
            // Before the first render: seed a draw order in attach order
            // (begin_frame clears it every frame and mark_drawn rebuilds it).
            bool present = false;
            for (auto *t : draw_order)
                present = present || (t == &a);
            if (!present)
                draw_order.push_back(&a);
        }

        void detach(Binding &a) noexcept
        {
            std::erase(tables, &a);
            std::erase(draw_order, &a);
            if (focused == &a)
            {
                focused = nullptr;
                // Promote a survivor, otherwise every remaining table keeps
                // keyboard_focus == false and keyboard/char input dies until
                // the user clicks again.
                if (!tables.empty())
                    set_focus(tables.front());
            }
            if (pressed == &a)
                pressed = nullptr;
            if (hovered == &a)
                hovered = nullptr;
            a.host = nullptr;
            a.keyboard_focus = true;
        }

        Binding *find(table::TableId id) const noexcept
        {
            for (auto *a : tables)
                if (a != nullptr && a->session.id == id)
                    return a;
            return nullptr;
        }

        // window logical -> screen (same scale set_pointer applies), then hit
        // the last frame's draw_order from top to bottom. geom.box already
        // includes the owner's offset, so no extra offset is added here.
        Binding *table_at(double wx, double wy) const noexcept
        {
            for (auto it = draw_order.rbegin(); it != draw_order.rend(); ++it)
            {
                Binding *a = *it;
                if (a == nullptr)
                    continue;
                const float lx = static_cast<float>(wx) * a->sx;
                const float ly = static_cast<float>(wy) * a->sy;
                const auto &b = a->session.layout.box;
                if (b.w > 0.0F && b.h > 0.0F && lx >= b.x && lx < b.x1() && ly >= b.y &&
                    ly < b.y1())
                    return a;
            }
            return nullptr;
        }

        void set_focus(Binding *a) noexcept
        {
            focused = a;
            for (auto *t : tables)
                if (t != nullptr)
                    t->keyboard_focus = (t == a);
        }

        void begin_frame() noexcept
        {
            ++frame_serial;
            hovered = pointer_inside ? table_at(pointer_x, pointer_y) : nullptr;
            draw_order.clear();
        }

        void mark_drawn(Binding *a) noexcept
        {
            if (a == nullptr)
                return;
            if (a->draw_serial == frame_serial)
                return;
            a->draw_serial = frame_serial;
            draw_order.push_back(a);
        }

        // The only consumer of renderVersion: accepts this frame's picking only.
        bool accept_pick(const picking_result &r) noexcept
        {
            if (r.key.object_type == 0xFFFFFFFF)
                return false; // no hit, keep last_pick
            // render_version is 16 bit in the ABI; compare at the same width so
            // frame_serial wrap-around does not reject valid picking.
            if (r.render_version() !=
                render_version_t{static_cast<std::uint32_t>(frame_serial)})
                return false; // stale frame / another frame
            last_pick = r;
            return true;
        }

        // Picking already accepted by accept_pick: route by entity_index.table
        void on_pick(const picking_result &r)
        {
            const auto z = table::zone_from(r.key.entity_index);
            Binding *t = find(z.table);
            if (t != nullptr)
                table::window::on_pick(*t, r); // version already filtered by accept_pick
        }

        void clear_hover()
        {
            pointer_inside = false;
            if (hovered != nullptr)
                table::session::update_hover(hovered->session, {});
            hovered = nullptr;
        }

        // Cursor is window-level state: written once by the host, never per table
        void apply_cursor(GLFWwindow *win) noexcept
        {
            if (win == nullptr)
                return;
            if (hovered != nullptr)
                table_glue::apply_cursor(
                    win, table::cursor_for(hovered->session.interaction.hover.to_zone()));
            else
                table_glue::apply_cursor(win, table::Cursor::arrow);
        }

        template <class E>
        void route_event(const E &e) noexcept
        {
            using T = std::remove_cvref_t<E>;
            if constexpr (std::same_as<T, ev::position2d_event>)
            {
                pointer_inside = true;
                pointer_x = e.xpos;
                pointer_y = e.ypos;
                hovered = table_at(pointer_x, pointer_y);
                // keep every table's input_state.pointer identical; no clicks
                for (auto *t : tables)
                    if (t != nullptr)
                        t->handle_event(e);
            }
            else if constexpr (std::same_as<T, ev::mousebutton_event>)
            {
                if (e.press() || e.repeat())
                {
                    Binding *t = table_at(pointer_x, pointer_y);
                    if (t != nullptr)
                    {
                        set_focus(t);
                        pressed = t;
                        t->handle_event(e);
                    }
                }
                else
                {
                    // releasing outside still goes back to the pressed table
                    Binding *t =
                        pressed != nullptr ? pressed : table_at(pointer_x, pointer_y);
                    if (t != nullptr)
                        t->handle_event(e);
                    pressed = nullptr;
                }
            }
            else if constexpr (std::same_as<T, ev::scroll_event>)
            {
                if (Binding *t = table_at(pointer_x, pointer_y))
                    t->handle_event(e);
            }
            else if constexpr (std::same_as<T, ev::keyboard_event> ||
                               std::same_as<T, ev::char_event>)
            {
                if (focused != nullptr)
                    focused->handle_event(e);
            }
            else if constexpr (std::same_as<T, ev::cursor_enter_event>)
            {
                if (!e.value)
                    clear_hover();
                else
                    pointer_inside = true;
                for (auto *t : tables)
                    if (t != nullptr)
                        t->handle_event(e);
            }
            else if constexpr (std::same_as<T, ev::window_focus_event>)
            {
                for (auto *t : tables)
                    if (t != nullptr)
                        t->handle_event(e);
                if (!e.focused)
                {
                    clear_hover();
                    pressed = nullptr;
                }
            }
            // other window events are not routed by this task
        }
    };

    inline Binding::~Binding()
    {
        if (host != nullptr)
            host->detach(*this);
    }
    inline Host::~Host()
    {
        // host dies before its arenas: clear residual arena->host first, so a
        // later arena destructor does not call back into a dead host.
        for (auto *a : tables)
            if (a != nullptr)
                a->host = nullptr;
    }
} // namespace table::window
