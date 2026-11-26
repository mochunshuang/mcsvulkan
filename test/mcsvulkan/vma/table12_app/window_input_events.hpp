#pragma once
// ============================================================================
// table12_app/window_input_events.hpp — input events：trace/on_hover + 指针链路 + Binding::on_* 成员定义
//
//   on_* 保持自由函数签名（arbiter trait 需要）：(Binding&, payload)。
// ============================================================================

#include "window_input_effects.hpp"
#include "window_input_text.hpp"

namespace table::window
{
// window_draw.hpp 提供（Host::on_pick 链路）
inline table::Target pointer_target(Binding &s, float x, float y);

template <class... A>
inline void trace(std::format_string<A...> fmt, A &&...a)
{
    static const bool on = std::getenv("TABLE_TRACE") != nullptr;
    if (on)
        std::println(fmt, std::forward<A>(a)...);
}

// ── GPU hover 回调（hoverPool().bind(&on_hover)；诊断用） ─────────────
inline void on_hover(picking_result r, bool enter, InputCtx &,
                     ui_new::ScreenWidget *screen) noexcept
{
    if (screen == nullptr || screen->table_host_ctx == nullptr)
    {
        trace("[hover] dropped: no screen/host");
        return;
    }
    auto &h = *screen->table_host_ctx;
    if (!h.accept_pick(r))
        return;   // the only frame-level renderVersion comparison
    h.on_pick(r); // diagnostics: update pick/last_pick only
    trace("[hover] {} key={:#x}", enter ? "ENTER" : "LEAVE", r.key.entity_index);
}

// ── 自由函数（从 demo cpp 搬入，唯一副本） ───────────────────────────

    inline void set_pointer(Binding &s, double wx, double wy) noexcept
    {
        s.wx = static_cast<float>(wx);
        s.wy = static_cast<float>(wy);
        s.mx = s.wx * s.sx;
        s.my = s.wy * s.sy;
    }


    template <class Cmd>
    inline void input_dispatch(Binding &s, const Cmd &cmd)
    {
        const auto out = table::session::dispatch(s.session, table::command::Command{cmd});
        apply_effects(s, out);
        notify_change(s);
        ++s.inputs;
    }

    inline void click_dispatch(Binding &s, double x, double y)
    {
        if (!s.input_state.pointer_position_valid && s.win != nullptr)
        {
            double cx = 0, cy = 0;
            glfwGetCursorPos(s.win, &cx, &cy);
            x = cx;
            y = cy;
        }
        set_pointer(s, x, y);
        const auto tgt = pointer_target(s, s.mx, s.my);
        if (std::getenv("TABLE_INPUTLOG") != nullptr)
            std::println("[click] at=({:.1f},{:.1f}) kind={} row={} col={}", s.mx, s.my,
                         table::part_name(tgt.kind), tgt.row_index, tgt.col_index);
        const bool shift = s.input_state.modifier.shift();
        const bool ctrl = s.input_state.modifier.control();
        input_dispatch(
            s, table::command::Press{s.mx, s.my, tgt.to_zone(), false, shift, ctrl, tgt});
        input_dispatch(s, table::command::Release{s.mx, s.my, s.session.interaction.drag_target});
    }


inline void Binding::on_press(const arena_input::click_payload &e)
    {
        click_dispatch(*this, e.x, e.y);
    }

inline void Binding::on_right_click(const arena_input::click_payload &e)
    {
        set_pointer(*this, e.x, e.y);
        const auto tgt = pointer_target(*this, mx, my);
        table::session::open_menu(session, tgt.to_zone().col, mx, my);
    }

inline void Binding::on_drag(const arena_input::drag_payload &e)
    {
        switch (e.phase)
        {
        case gesture::drag_phase::begin: {
            // 按下点 = 当前点 - 总位移：先补一发 press，再发 drag
            set_pointer(*this, e.x - e.total_dx, e.y - e.total_dy);
            session.interaction.last_click_t = -1.0e9; // 拖拽的 press 不参与双击判定
            const auto tgt = pointer_target(*this, mx, my);
            input_dispatch(*this, table::command::Press{mx, my, tgt.to_zone(), false,
                                                      e.modifier.shift(),
                                                      e.modifier.control(), tgt});
            set_pointer(*this, e.x, e.y);
            input_dispatch(*this, table::command::Drag{mx, my, session.interaction.drag_target});
            break;
        }
        case gesture::drag_phase::move:
            set_pointer(*this, e.x, e.y);
            input_dispatch(*this, table::command::Drag{mx, my, session.interaction.drag_target});
            break;
        case gesture::drag_phase::end:
            set_pointer(*this, e.x, e.y);
            input_dispatch(*this, table::command::Release{mx, my, session.interaction.drag_target});
            break;
        }
    }

inline void Binding::on_wheel(const arena_input::wheel_payload &e)
    {
        if (!input_state.pointer_position_valid && win != nullptr)
        {
            double cx = 0, cy = 0;
            glfwGetCursorPos(win, &cx, &cy);
            set_pointer(*this, cx, cy);
        }
        else
            set_pointer(*this, input_state.pointer_x, input_state.pointer_y);
        if (std::getenv("TABLE_INPUTLOG") != nullptr)
            std::println("[wheel] delta={} at=({:.1f},{:.1f})", e.delta, mx, my);
        // 只滚指针下面的表；Shift 或悬停横向条 = 横向滚动
        if (mx < session.layout.box.x || mx >= session.layout.box.x1() || my < session.layout.box.y ||
            my >= session.layout.box.y1())
            return;
        table::session::wheel(session, 0.0F, static_cast<float>(e.delta),
                              input_state.modifier.shift());
        ++inputs;
    }

inline void Binding::on_key(const arena_input::key_payload &e)
    {
        if (e.transition != input_frame::Transition::Press)
            return;
        // 只有导航键接受 repeat；Ctrl+V / F2 / 字母等只认首次 press
        const bool nav = e.key == ev::Key::eUP || e.key == ev::Key::eDOWN ||
                         e.key == ev::Key::eLEFT || e.key == ev::Key::eRIGHT ||
                         e.key == ev::Key::eHOME || e.key == ev::Key::eEND ||
                         e.key == ev::Key::ePAGE_UP || e.key == ev::Key::ePAGE_DOWN;
        if (e.repeat && !nav)
            return;
        if (!keyboard_focus)
            return;
        const auto key_down = [&](ev::Key k) {
            return input_state.get_keyboard_event(k) == input_frame::HeldState::Down;
        };
        const bool ctrl = input_state.modifier.control() ||
                          key_down(ev::Key::eLEFT_CONTROL) ||
                          key_down(ev::Key::eRIGHT_CONTROL);
        const bool shift = input_state.modifier.shift() ||
                           key_down(ev::Key::eLEFT_SHIFT) ||
                           key_down(ev::Key::eRIGHT_SHIFT);
        if (std::getenv("TABLE_INPUTLOG") != nullptr)
            std::println("[in] key={} repeat={} sel=({},{})", e.key, e.repeat,
                         session.state.sel.cur_r, session.state.sel.cur_c);
        if (e.key == ev::Key::eV && ctrl)
        {
            const auto out =
                table::session::paste_text(session, table_glue::read_clipboard(win));
            apply_effects(*this, out);
            ++inputs;
            return;
        }
        if (e.key == ev::Key::eESCAPE)
        {
            if (cancel_modal(*this))
                esc_cancelled = true;
            return;
        }
        if (const auto k = map_key(e.key))
            input_dispatch(
                *this,
                table::command::KeyPress{*k, shift, ctrl,
                                  table::model::target_of(
                                      session.model, session.id,
                                      table::Zone{table::Part::cell, session.state.sel.cur_r,
                                                     session.state.sel.cur_c, session.id})});
    }

inline void Binding::on_char(const arena_input::char_payload &e)
    {
        std::string text;
        encode_utf8(e.codepoint, text);
        if (std::getenv("TABLE_INPUTLOG") != nullptr)
            std::println("[char] cp={:#x} filter={} find={} edit={}", e.codepoint,
                         session.state.filter.edit, session.state.find.mode, session.state.edit.active());
        const auto out = table::session::feed_text(session, text);
        apply_effects(*this, out);
    }

inline void Binding::handle_key_event(const ev::keyboard_event &e)
    {
        now = mcs::app::now_ms();
        session.interaction.now = static_cast<double>(now.count()) / 1000.0;
        auto f = input_frame::build_change(input_state, e);
        arbiter.feed(*this, f, now);
        input_frame::apply_change(input_state, f);
    }

template <class E>
    inline void Binding::handle_event(const E &e)
    {
        if constexpr (std::same_as<E, ev::window_focus_event>)
        {
            if (!e.focused)
            {
                session.interaction.left = false;
                session.interaction.drag = table::intent::DragMode::none;
                session.state.menu.open = false;
                session.state.touch();
            }
            return;
        }
        else
        {
            now = mcs::app::now_ms();
            session.interaction.now = static_cast<double>(now.count()) / 1000.0;
            if constexpr (std::same_as<E, ev::char_event>)
            {
                // 文本不是手势：直接进 on_char（不经过 arbiter）
                on_char(arena_input::char_payload{e.codepoint, now});
                return;
            }
            if constexpr (std::same_as<E, ev::keyboard_event>)
            {
                handle_key_event(e);
                return;
            }
            using frame_t = decltype(input_frame::build_change(input_state, e));
            if constexpr (!std::is_void_v<frame_t>)
            {
                auto f = input_frame::build_change(input_state, e);
                arbiter.feed(*this, f, now);
                input_frame::apply_change(input_state, f);
            }
        }
    }

inline void Binding::advance(double /*seconds*/)
    {
        // 表的输入/悬停/双击全部用同一个时钟（now_ms），不混 GLFW elapsed
        now = mcs::app::now_ms();
        session.interaction.now = static_cast<double>(now.count()) / 1000.0;
        if (!session.interaction.left)
        {
            // Pointer coordinates stay current for every table (picking
            // diagnostics need them); only hover writes are gated below.
            set_pointer(*this, input_state.pointer_x, input_state.pointer_y);
            // hover 是帧派生态（pointer + geometry），不是手写输入分支
            if (host != nullptr)
            {
                // CPU hover z-order gate: only host.hovered may write
                // ix.hover; all other tables are cleared. Overlap is
                // resolved by the topmost entry of draw_order.
                if (host->hovered == this)
                {
                    session.interaction.pointer = {mx, my};
                    table::session::update_hover(session, pointer_target(*this, mx, my));
                }
                else
                {
                    table::session::update_hover(session, {}); // clear old hover/tooltip
                }
            }
            else
            {
                // headless single table: keep pointer -> hit -> hover
                session.interaction.pointer = {mx, my};
                table::session::update_hover(session, pointer_target(*this, mx, my));
            }
        }
        arbiter.tick(*this, now);
        notify_change(*this);
    }

} // namespace table::window
