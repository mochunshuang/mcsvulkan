#pragma once
// ============================================================================
// table12_app/window_input_effects.hpp — input effects：apply_effects / notify_* / 模型 API / cancel_modal
//
//   Outcome -> Model 与回调通知；ui_signature 供 notify_change 去重。
// ============================================================================

#include "host.hpp"

namespace table::window
{
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


    inline void notify_model(Binding &s)
    {
        s.changes.model = true;
        s.session.state.touch();
        if (s.cb.on_model_changed)
            s.cb.on_model_changed();
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
// ── Binding::on_* / handle_event / advance 成员定义（唯一副本） ──────
} // namespace table::window
