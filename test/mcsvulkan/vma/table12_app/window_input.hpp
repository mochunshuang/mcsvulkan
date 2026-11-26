#pragma once
// ============================================================================
// table12_app/window_input.hpp — window 层的输入链（只放定义）
//
//   迁移分块（依赖决定顺序）：
//     alpha : trace + on_hover
//     beta' : 自由函数（input 链 + 模型 API + cancel_modal）
//     gamma': Binding::on_* 成员定义组
//
//   依赖：host.hpp（Binding/Host）。ui_dao / InputCtx / hoverPool /
//   pointer_target(window_draw) 由 demo 在本头之前提供；本头只 include host.hpp。
//   DAG：shader_abi/glue/platform/encode/window_draw -> host -> window_input。
// ============================================================================

#include "host.hpp"

namespace table::window
{
// ── 前向声明：成员定义与自由函数互相引用，先声明后定义（顺序无关） ────
struct ui_signature;
inline table::Target pointer_target(Binding &s, float x, float y);
inline void set_pointer(Binding &s, double wx, double wy) noexcept;
template <class Cmd>
inline void input_dispatch(Binding &s, const Cmd &cmd);
inline void click_dispatch(Binding &s, double x, double y);
inline bool cancel_modal(Binding &s) noexcept;
inline bool cancel_modal(Host &h) noexcept;

// ── 诊断开关（环境变量只活在这一层；核心不读 env、不做 I/O） ──────────
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
    struct ui_signature
    {
        int r0{}, c0{}, r1{}, c1{}, cur_r{}, cur_c{};
        std::uint64_t checks{};
        friend bool operator==(const ui_signature &, const ui_signature &) = default;
    };

    inline ui_signature signature_of(const Binding &s) noexcept
    {
        ui_signature g{s.session.state.sel.r0,
                       s.session.state.sel.c0,
                       s.session.state.sel.r1,
                       s.session.state.sel.c1,
                       s.session.state.sel.cur_r,
                       s.session.state.sel.cur_c,
                       0};
        std::uint64_t h = 1469598103934665603ULL;
        for (const auto v : s.session.state.checks.on)
            h = (h ^ v) * 1099511628211ULL;
        g.checks = h;
        return g;
    }

    inline void notify_change(Binding &s)
    {
        const auto sig = signature_of(s);
        const bool sel = !s.cb_valid || sig.r0 != s.cb_r0 || sig.c0 != s.cb_c0 ||
                         sig.r1 != s.cb_r1 || sig.c1 != s.cb_c1 ||
                         sig.cur_r != s.cb_cur_r || sig.cur_c != s.cb_cur_c;
        const bool chk = !s.cb_valid || sig.checks != s.cb_checks;
        if (sel)
        {
            s.changes.selection = true;
            if (s.cb.on_selection_changed)
                s.cb.on_selection_changed();
        }
        if (chk)
        {
            s.changes.checks = true;
            if (s.cb.on_checks_changed)
                s.cb.on_checks_changed();
        }
        s.cb_r0 = sig.r0;
        s.cb_c0 = sig.c0;
        s.cb_r1 = sig.r1;
        s.cb_c1 = sig.c1;
        s.cb_cur_r = sig.cur_r;
        s.cb_cur_c = sig.cur_c;
        s.cb_checks = sig.checks;
        s.cb_valid = true;
    }

    inline void apply_effects(Binding &s, const table::command::Outcome &out)
    {
        // G2a 起 Binding 组合 Session：传 session 成员（不再有基类继承）
        const bool model_changed = table::session::apply_effects(s.session, out);
        for (const auto &e : out.effects)
            if (const auto *p = std::get_if<table::command::WriteClipboard>(&e))
                table_glue::write_clipboard(s.win, p->text);
        if (model_changed)
        {
            s.changes.model = true;
            if (s.cb.on_model_changed)
                s.cb.on_model_changed();
        }
    }

    inline void set_pointer(Binding &s, double wx, double wy) noexcept
    {
        s.wx = static_cast<float>(wx);
        s.wy = static_cast<float>(wy);
        s.mx = s.wx * s.sx;
        s.my = s.wy * s.sy;
    }

    inline void notify_model(Binding &s)
    {
        s.changes.model = true;
        s.session.state.touch();
        if (s.cb.on_model_changed)
            s.cb.on_model_changed();
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

    inline std::optional<table::command::Key> map_key(ev::Key k) noexcept
    {
        switch (k)
        {
        case ev::Key::eUP:
            return table::command::Key::up;
        case ev::Key::eDOWN:
            return table::command::Key::down;
        case ev::Key::eLEFT:
            return table::command::Key::left;
        case ev::Key::eRIGHT:
            return table::command::Key::right;
        case ev::Key::eHOME:
            return table::command::Key::home;
        case ev::Key::eEND:
            return table::command::Key::end;
        case ev::Key::ePAGE_UP:
            return table::command::Key::pgup;
        case ev::Key::ePAGE_DOWN:
            return table::command::Key::pgdn;
        case ev::Key::eENTER:
            return table::command::Key::enter;
        case ev::Key::eESCAPE:
            return table::command::Key::esc;
        case ev::Key::eTAB:
            return table::command::Key::tab;
        case ev::Key::eBACKSPACE:
            return table::command::Key::backspace;
        case ev::Key::eDELETE:
            return table::command::Key::del;
        case ev::Key::eSPACE:
            return table::command::Key::space;
        case ev::Key::eF2:
            return table::command::Key::f2;
        case ev::Key::eA:
            return table::command::Key::a;
        case ev::Key::eC:
            return table::command::Key::c;
        case ev::Key::eV:
            return table::command::Key::v;
        case ev::Key::eX:
            return table::command::Key::x;
        case ev::Key::eZ:
            return table::command::Key::z;
        case ev::Key::eY:
            return table::command::Key::y;
        case ev::Key::eF:
            return table::command::Key::f;
        default:
            return std::nullopt;
        }
    }

    inline void encode_utf8(std::uint32_t cp, std::string &out)
    {
        if (cp < 0x80)
            out += static_cast<char>(cp);
        else if (cp < 0x800)
        {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    inline table::RowId insert_row(Binding &s, int at = -1)
    {
        const auto id = s.session.model.insert_row(at);
        s.session.state.reconcile(s.session.model);
        notify_model(s);
        return id;
    }

    inline bool remove_row(Binding &s, int at)
    {
        if (!s.session.model.remove_row(at))
            return false;
        s.session.state.reconcile(s.session.model);
        notify_model(s);
        return true;
    }

    inline table::ColumnId insert_column(Binding &s, int at, table::model::Column c)
    {
        const auto id = s.session.model.insert_column(at, std::move(c));
        s.session.state.reconcile(s.session.model);
        notify_model(s);
        return id;
    }

    inline bool remove_column(Binding &s, int at)
    {
        if (!s.session.model.remove_column(at))
            return false;
        s.session.state.reconcile(s.session.model);
        notify_model(s);
        return true;
    }

    inline void set_cell(Binding &s, int row, int col, std::string text)
    {
        s.session.model.set_cell(row, col, std::move(text));
        notify_model(s);
    }

    inline bool cancel_modal(Binding &st) noexcept
    {
        auto &v = st.session.state;
        if (v.filter.edit >= 0)
        {
            const auto col = static_cast<std::size_t>(v.filter.edit);
            if (col < v.filter.text.size())
                v.filter.text[col] = v.filter.orig;
            v.filter.input = v.filter.orig;
            v.filter.orig.clear();
            v.filter.compose.clear();
            v.filter.edit = -1;
            v.touch();
            return true;
        }
        if (v.find.mode)
        {
            v.find.mode = false;
            v.find.compose.clear();
            v.touch();
            return true;
        }
        if (v.edit.active())
        {
            v.edit = {};
            v.touch();
            return true;
        }
        if (v.menu.open)
        {
            v.menu.open = false;
            v.touch();
            return true;
        }
        return false;
    }

    inline bool cancel_modal(Host &h) noexcept
    {
        return h.focused != nullptr && cancel_modal(*h.focused);
    }
} // namespace table::window
