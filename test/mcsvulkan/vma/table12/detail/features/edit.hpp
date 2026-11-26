#pragma once
// ============================================================================
// table12/detail/features/edit.hpp — EditFeature / ClipboardFeature / UndoFeature
//
//   纯移动自 detail/features.hpp（G3）；不改行为、不改顺序。
//   依赖：../common.hpp。
// ============================================================================

#include "../common.hpp"

namespace table::detail
{
        struct EditFeature
        {
            static int pos_of(const Layout &g, int row)
            {
                for (int i = 0; i < static_cast<int>(g.view_rows.size()); ++i)
                    if (g.view_rows[static_cast<std::size_t>(i)] == row)
                        return i;
                return -1;
            }

            static void start(const Table &d, State &v, int r, int c)
            {
                if (r < 0 || c < 0)
                    return;
                v.edit.row = r;
                v.edit.col = c;
                v.edit.ref = d.cell_id(r, c);
                v.edit.buf = std::string(d.text(r, c));
                v.edit.orig = v.edit.buf;
                v.edit.caret = static_cast<int>(v.edit.buf.size());
                v.edit.anchor = v.edit.caret;
                v.edit.undo.clear();
                v.edit.redo.clear();
                v.edit.compose.clear();
                v.touch();
            }

            static Outcome commit(Context &c)
            {
        Outcome o;
                auto &e = c.state.edit;
                if (!e.active())
                return o;
                if (e.buf != e.orig)
                {
                    const auto id = e.ref.row;
                    if (c.trace)
                        std::println("[commit] row={} col={} id={} text={}", e.row,
                                     e.col, id.v, e.buf);
                    if (true)
                        command::push_effect(o.effects, SetCell{id, e.col, e.buf});
                    c.state.history.undo.push_back({{id, e.col, e.orig, e.buf}});
                    c.state.history.redo.clear();
                }
                e = {};
                c.state.touch();
            return o;
            }

            static void replace_sel(State &v, std::string_view s)
            {
                auto &e = v.edit;
                const int lo = e.sel_lo(), hi = e.sel_hi();
                e.buf.replace(static_cast<std::size_t>(lo),
                              static_cast<std::size_t>(hi - lo), s);
                e.caret = lo + static_cast<int>(s.size());
                e.anchor = e.caret;
            }

            static int caret_at(const Context &c, float x)
            {
                const auto &e = c.state.edit;
                const int pos = c.layout.pos_of_col(e.col);
                if (pos < 0)
                    return 0;
                const float left = c.layout.col_x_at(pos) + c.theme.cell_pad;
                int best = 0;
                float bestd = 1.0e30F;
                std::size_t i = 0;
                while (true)
                {
                    const float w =
                        text_width(c.font, std::string_view(e.buf).substr(0, i));
                    const float d = std::abs((left + w) - x);
                    if (d < bestd)
                    {
                        bestd = d;
                        best = static_cast<int>(i);
                    }
                    if (i >= e.buf.size())
                        break;
                    char32_t cp = 0;
                    i += std::max<std::size_t>(
                        utf8_decode(std::string_view(e.buf).substr(i), cp), 1);
                }
                return best;
            }

            Outcome paste(Context &c, std::string text)
            {
        Outcome o;
                auto &v = c.state;
                if (v.edit.active())
                {
                    snapshot(v, c.interaction, false);
                    for (char &ch : text)
                        if (ch == '\n' || ch == '\r' || ch == '\t')
                            ch = ' ';
                    replace_sel(v, text);
                }
                else if (v.sel.cur_r >= 0 && v.sel.cur_c >= 0)
                {
                    const int r = v.sel.cur_r;
                    const int col = v.sel.cur_c;
                    std::vector<State::undo_change> batch;
                    std::size_t line = 0;
                    int dr = 0;
                    while (line <= text.size())
                    {
                        const std::size_t nl = text.find('\n', line);
                        const std::size_t end =
                            nl == std::string::npos ? text.size() : nl;
                        const std::string_view ln{text.data() + line, end - line};
                        std::size_t p = 0;
                        int dc = 0;
                        while (p <= ln.size())
                        {
                            const std::size_t tab = ln.find('\t', p);
                            const std::size_t e2 =
                                tab == std::string_view::npos ? ln.size() : tab;
                            const int rr = r + dr;
                            const int cc = col + dc;
                            if (rr >= 0 && rr < c.model.rows() && cc >= 0 &&
                                cc < c.model.cols_count())
                            {
                                const auto old = c.model.text(rr, cc);
                                const auto id = c.model.ids[static_cast<std::size_t>(rr)];
                                std::string val{ln.substr(p, e2 - p)};
                                command::push_effect(o.effects, SetCell{id, cc, val});
                                batch.push_back({id, cc, std::string(old), val});
                            }
                            ++dc;
                            if (tab == std::string_view::npos)
                                break;
                            p = tab + 1;
                        }
                        ++dr;
                        if (nl == std::string::npos)
                            break;
                        line = nl + 1;
                    }
                    if (!batch.empty())
                    {
                        v.history.undo.push_back(std::move(batch));
                        v.history.redo.clear();
                    }
                }
                v.touch();
                o.handled = true;
                return o;
            return o;
            }

            bool begin(Context &c, const Target &t)
            {
                return begin(c, t.row_index, t.col_index);
            }
            bool begin(Context &c, int r, int col)
            {
                start(c.model, c.state, r, col);
                return true;
            }

            bool compose(Context &c, std::string text)
            {
                if (c.state.edit.active())
                {
                    c.state.edit.compose = std::move(text);
                    c.state.touch();
                }
                return true;
            }

            bool text(Context &c, std::string_view s)
            {
                auto &v = c.state;
                if (v.edit.active())
                {
                    snapshot(v, c.interaction, true);
                    replace_sel(v, s);
                }
                else if (v.sel.cur_r >= 0 && v.sel.cur_c >= 0)
                {
                    start(c.model, v, v.sel.cur_r, v.sel.cur_c);
                    v.edit.buf.clear();
                    v.edit.caret = 0;
                    v.edit.anchor = 0;
                    replace_sel(v, s);
                }
                v.touch();
                return true;
            }

            static void snapshot(State &v, Interaction &ix, bool typing = false)
            {
                auto &e = v.edit;
                // 连续输入（0.8s 内）合并成一步 undo，和浏览器的习惯一致
                if (typing && e.last_push_typing && ix.now - e.last_push_t < 0.8)
                {
                    e.redo.clear();
                    return;
                }
                e.undo.push_back(e.buf);
                if (e.undo.size() > 64)
                    e.undo.erase(e.undo.begin());
                e.redo.clear();
                e.last_push_t = ix.now;
                e.last_push_typing = typing;
            }
            static void select_all(State &v) noexcept
            {
                v.edit.anchor = 0;
                v.edit.caret = static_cast<int>(v.edit.buf.size());
                v.touch();
            }
            void select_word(Context &c, float x)
            {
                auto &e = c.state.edit;
                e.caret = caret_at(c, x);
                const std::string_view sv{e.buf};
                const auto caret = static_cast<std::size_t>(
                    std::clamp(e.caret, 0, static_cast<int>(e.buf.size())));
                std::size_t probe = caret;
                if (probe >= sv.size() || !is_word_byte(sv[probe]))
                    probe = probe > 0 ? utf8_prev(sv, probe) : 0;
                std::size_t lo = probe;
                while (lo > 0 && is_word_byte(sv[utf8_prev(sv, lo)]))
                    lo = utf8_prev(sv, lo);
                std::size_t hi = probe;
                while (hi < sv.size() && is_word_byte(sv[hi]))
                    hi = utf8_next(sv, hi);
                if (hi <= lo)
                    hi = std::min(sv.size(), utf8_next(sv, lo));
                e.anchor = static_cast<int>(lo);
                e.caret = static_cast<int>(hi);
                c.state.touch();
            }

            template <class E>
            Outcome handle(Context &c, const E &x)
            {
                Outcome o;
                auto &v = c.state;
                auto &ix = c.interaction;
                using T = E;
                if constexpr (std::is_same_v<T, Press>)
                {
                    if (v.edit.active())
                    {
                        if (x.z.kind == Part::cell && x.z.row == v.edit.row &&
                            x.z.col == v.edit.col)
                        {
                            if (ix.click_count >= 2)
                            {
                                if (ix.click_count >= 3)
                                    select_all(v);
                                else
                                    select_word(c, x.x);
                            }
                            else
                            {
                                const int caret = caret_at(c, x.x);
                                v.edit.caret = caret;
                                if (!x.shift)
                                    v.edit.anchor = caret;
                                v.touch();
                            }
                        }
                        else
                            o.merge(commit(c));
                    }
                    return {}; // 让 core 继续处理按下
                }

                else if constexpr (std::is_same_v<T, Drag>)
                {
                    if (!v.edit.active())
                return o;
                    const Target tgt =
                        x.t.kind != Part::none ? x.t : target_of(c.model, {}, x.t.to_zone());
                    bool in_row = false;
                    if (tgt.kind == Part::cell)
                        in_row = tgt.row_index == v.edit.row;
                    else
                        for (int i = 0; i < c.layout.row_count(); ++i)
                        {
                            if (c.layout.row_index[static_cast<std::size_t>(i)] != v.edit.row)
                                continue;
                            const float y0 = c.layout.row_y[static_cast<std::size_t>(i)];
                            const float y1 = y0 + c.layout.row_h[static_cast<std::size_t>(i)];
                            in_row = x.y >= y0 && x.y < y1;
                            break;
                        }
                    if (in_row)
                    {
                        v.edit.caret = caret_at(c, x.x);
                        v.touch();
                        return Outcome::handled_now(); // 编辑框内拖动 = 选文本，不是选单元格
                    }
                return o;
                }
                else if constexpr (std::is_same_v<T, KeyPress>)
                {
                    if (!v.edit.active())
                return o;
                    auto &e = v.edit;
                    switch (x.k)
                    {
                    case Key::esc:
                        e = {};
                        break;
                    case Key::enter: {
                        const int row = e.row;
                        const int col = e.col;
                        o.merge(commit(c));
                        const int pos = pos_of(c.layout, row);
                        if (pos + 1 < static_cast<int>(c.layout.view_rows.size()))
                        {
                            const int nr =
                                c.layout.view_rows[static_cast<std::size_t>(pos + 1)];
                            v.sel.r0 = v.sel.r1 = v.sel.cur_r = nr;
                            v.sel.c0 = v.sel.c1 = v.sel.cur_c = col;
                            v.touch();
                            ensure_visible(v, c.layout, c.theme, nr, col);
                        }
                        break;
                    }
                    case Key::tab: {
                        o.merge(commit(c));
                        const int nc = std::clamp(v.sel.cur_c + (x.shift ? -1 : 1), 0,
                                                  c.model.cols_count() - 1);
                        v.sel.cur_c = v.sel.c0 = v.sel.c1 = nc;
                        v.touch();
                        ensure_visible(v, c.layout, c.theme, v.sel.cur_r, nc);
                        break;
                    }
                    case Key::backspace:
                        snapshot(v, c.interaction, true);
                        if (e.has_sel())
                            replace_sel(v, {});
                        else if (e.caret > 0)
                        {
                            const auto sv = std::string_view(e.buf);
                            const std::size_t q =
                                x.ctrl ? utf8_word_left(sv,
                                                        static_cast<std::size_t>(e.caret))
                                       : utf8_prev(sv, static_cast<std::size_t>(e.caret));
                            e.buf.erase(q, static_cast<std::size_t>(e.caret) - q);
                            e.caret = static_cast<int>(q);
                            e.anchor = e.caret;
                        }
                        break;
                    case Key::del:
                        snapshot(v, c.interaction, true);
                        if (e.has_sel())
                            replace_sel(v, {});
                        else if (e.caret < static_cast<int>(e.buf.size()))
                        {
                            const auto sv = std::string_view(e.buf);
                            const std::size_t q =
                                x.ctrl ? utf8_word_right(
                                             sv, static_cast<std::size_t>(e.caret))
                                       : utf8_next(sv, static_cast<std::size_t>(e.caret));
                            e.buf.erase(static_cast<std::size_t>(e.caret),
                                        q - static_cast<std::size_t>(e.caret));
                        }
                        break;
                    case Key::left:
                        e.caret =
                            e.has_sel()
                                ? e.sel_lo()
                                : static_cast<int>(
                                      x.ctrl
                                          ? utf8_word_left(
                                                std::string_view(e.buf),
                                                static_cast<std::size_t>(e.caret))
                                          : utf8_prev(std::string_view(e.buf),
                                                      static_cast<std::size_t>(e.caret)));
                        if (!x.shift)
                            e.anchor = e.caret;
                        break;
                    case Key::right:
                        e.caret =
                            e.has_sel()
                                ? e.sel_hi()
                                : static_cast<int>(
                                      x.ctrl
                                          ? utf8_word_right(
                                                std::string_view(e.buf),
                                                static_cast<std::size_t>(e.caret))
                                          : utf8_next(std::string_view(e.buf),
                                                      static_cast<std::size_t>(e.caret)));
                        if (!x.shift)
                            e.anchor = e.caret;
                        break;
                    case Key::home:
                        e.caret = 0;
                        if (!x.shift)
                            e.anchor = 0;
                        break;
                    case Key::end:
                        e.caret = static_cast<int>(e.buf.size());
                        if (!x.shift)
                            e.anchor = e.caret;
                        break;
                    case Key::a:
                        if (x.ctrl)
                        {
                            e.anchor = 0;
                            e.caret = static_cast<int>(e.buf.size());
                        }
                        break;
                    case Key::c:
                        if (x.ctrl)
                            command::push_effect(o.effects, WriteClipboard{
                                e.has_sel()
                                    ? e.buf.substr(static_cast<std::size_t>(e.sel_lo()),
                                                   static_cast<std::size_t>(e.sel_hi() -
                                                                            e.sel_lo()))
                                    : e.buf});
                        break;
                    case Key::x:
                        if (x.ctrl)
                        {
                            if (e.has_sel())
                                command::push_effect(o.effects, WriteClipboard{e.buf.substr(
                                    static_cast<std::size_t>(e.sel_lo()),
                                    static_cast<std::size_t>(e.sel_hi() - e.sel_lo()))});
                            snapshot(v, c.interaction, true);
                            if (e.has_sel())
                                replace_sel(v, {});
                        }
                        break;
                    case Key::z:
                        if (x.ctrl && !e.undo.empty())
                        {
                            e.redo.push_back(e.buf);
                            e.buf = e.undo.back();
                            e.undo.pop_back();
                            e.caret = static_cast<int>(e.buf.size());
                            e.anchor = e.caret;
                            e.last_push_typing = false;
                        }
                        break;
                    case Key::y:
                        if (x.ctrl && !e.redo.empty())
                        {
                            e.undo.push_back(e.buf);
                            e.buf = e.redo.back();
                            e.redo.pop_back();
                            e.caret = static_cast<int>(e.buf.size());
                            e.anchor = e.caret;
                            e.last_push_typing = false;
                        }
                        break;
                    default:
                        break;
                    }
                    v.touch();
                return command::merged(std::move(o), Outcome::handled_now());
                }
                return command::merged(std::move(o), command::handled({}));
            }

            void rows(Context &, std::vector<row_idx> &) {}

            void paint(const PaintContext &c, DrawList &fr) const
            {
                const auto &e = c.state.edit;
                if (!e.active())
                    return;
                int ri = -1, ci = -1;
                for (int i = 0; i < c.layout.row_count(); ++i)
                    if (c.layout.row_index[static_cast<std::size_t>(i)] == e.row)
                        ri = i;
                for (int i = 0; i < c.layout.col_count(); ++i)
                    if (c.layout.col_index[static_cast<std::size_t>(i)] == e.col)
                        ci = i;
                if (ri < 0 || ci < 0)
                    return;
                Rect cr =
                    clip_x({c.layout.col_x_at(ci), c.layout.row_y[static_cast<std::size_t>(ri)],
                            c.layout.col_w[static_cast<std::size_t>(ci)],
                            c.layout.row_h[static_cast<std::size_t>(ri)]},
                           c.layout.clip_x0(ci), c.layout.clip_x1(ci));
                cr = intersect(cr, c.layout.body);
                if (cr.empty())
                    return;
                fr.draw.add_shape(Stream::overlay, cr, {0.06F, 0.08F, 0.12F, 1.0F},
                                  c.theme.cell_radius, {}, 0, 0);
                const float inner = std::max(cr.w - 2 * c.theme.cell_pad, 1.0F);
                const float pre = text_width(
                    c.font,
                    std::string_view(e.buf).substr(0, static_cast<std::size_t>(e.caret)));
                const float off = pre > inner ? pre - inner + 2.0F : 0.0F;
                if (e.has_sel())
                {
                    const float lo =
                        text_width(c.font, std::string_view(e.buf).substr(0, e.sel_lo()));
                    const float hi =
                        text_width(c.font, std::string_view(e.buf).substr(0, e.sel_hi()));
                    if (hi > lo)
                        fr.draw.add_shape(
                            Stream::overlay,
                            {cr.x + c.theme.cell_pad + lo - off, cr.y + 4, hi - lo,
                             std::max(cr.h - 8, 1.0F)},
                            {c.theme.accent.r, c.theme.accent.g, c.theme.accent.b, 0.35F}, 0, {}, 0,
                            0);
                }
                const Rect tx = intersect(
                    {cr.x + c.theme.cell_pad - off, cr.y, inner + off, cr.h}, c.layout.body);
                fr.draw.add_text(Stream::overlay, tx, c.theme.text,
                                 cr.y + cr.h * 0.5F + c.font.px * 0.30F, e.buf, 0);
                const float cx = std::clamp(cr.x + c.theme.cell_pad + pre - off, cr.x + 1,
                                            cr.x + cr.w - 2);
                fr.draw.add_shape(Stream::overlay,
                                  {cx, cr.y + 4, 1.5F, std::max(cr.h - 8, 1.0F)},
                                  c.theme.accent, 0, {}, 0, 0);
                if (!e.compose.empty())
                {
                    const float cw = text_width(c.font, e.compose);
                    fr.draw.add_text(Stream::overlay,
                                     {cx + 2, cr.y, std::max(cw + 4, 1.0F), cr.h},
                                     {c.theme.accent.r, c.theme.accent.g, c.theme.accent.b, 0.9F},
                                     cr.y + cr.h * 0.5F + c.font.px * 0.30F, e.compose, 0);
                    fr.draw.add_shape(Stream::overlay,
                                      {cx + 2, cr.y + cr.h - 4, std::max(cw, 1.0F), 1},
                                      c.theme.accent, 0, {}, 0, 0);
                }
                fr.draw.add_shape(Stream::overlay, cr, {0, 0, 0, 0}, 0, c.theme.accent, 1.5F,
                                  0);
            }
        };

        // 列宽：拖分隔条只发 begin/move/end，宽度写 State.col_w
        struct ClipboardFeature
        {
            static Outcome clear_sel(Context &c)
            {
        Outcome o;
                auto &v = c.state;
                if (v.sel.r0 < 0)
                return o;
                std::vector<State::undo_change> batch;
                for (int r = std::min(v.sel.r0, v.sel.r1);
                     r <= std::max(v.sel.r0, v.sel.r1); ++r)
                    for (int cc = std::min(v.sel.c0, v.sel.c1);
                         cc <= std::max(v.sel.c0, v.sel.c1); ++cc)
                    {
                        const auto old = c.model.text(r, cc);
                        if (old.empty())
                            continue;
                        const auto id = c.model.ids[static_cast<std::size_t>(r)];
                        command::push_effect(o.effects, SetCell{id, cc, {}});
                        batch.push_back({id, cc, std::string(old), {}});
                    }
                if (!batch.empty())
                {
                    v.history.undo.push_back(std::move(batch));
                    v.history.redo.clear();
                    v.touch();
                }
            return o;
            }

            Outcome copy(Context &c, bool cut)
            {
        Outcome o;
                auto &v = c.state;
                if (v.sel.r0 < 0)
                {
                o.handled = true;
                return o;
                }
                std::string tsv;
                for (int r = std::min(v.sel.r0, v.sel.r1);
                     r <= std::max(v.sel.r0, v.sel.r1); ++r)
                {
                    for (int cc = std::min(v.sel.c0, v.sel.c1);
                         cc <= std::max(v.sel.c0, v.sel.c1); ++cc)
                    {
                        if (cc > std::min(v.sel.c0, v.sel.c1))
                            tsv += '\t';
                        tsv += c.model.text(r, cc);
                    }
                    tsv += '\n';
                }
                command::push_effect(o.effects, WriteClipboard{std::move(tsv)});
                if (cut)
                    o.merge(clear_sel(c));
                o.handled = true;
                return o;
            return o;
            }
            Outcome remove(Context &c)
            {
                Outcome o;
                o.merge(clear_sel(c));
                o.handled = true;
                return o;
            }
            template <class E>
            Outcome handle(Context &, const E &)
            {
                Outcome o;
                return command::merged(std::move(o), command::handled({}));
            }
            void rows(Context &, std::vector<row_idx> &) {}
            void paint(const PaintContext &, DrawList &) const {}
        };

        // 撤销 / 重做：只消费 State.undo / State.redo
        struct UndoFeature
        {
            Outcome run(Context &c, bool redo)
            {
        Outcome o;
                auto &v = c.state;
                if (false)
                {
                o.handled = true;
                return o;
                }
                auto &from = redo ? v.history.redo : v.history.undo;
                auto &to = redo ? v.history.undo : v.history.redo;
                if (from.empty())
                {
                o.handled = true;
                return o;
                }
                auto batch = std::move(from.back());
                from.pop_back();
                for (const auto &ch : batch)
                    command::push_effect(o.effects, 
                        SetCell{ch.id, ch.col, redo ? ch.after : ch.before});
                to.push_back(std::move(batch));
                v.touch();
                o.handled = true;
                return o;
            return o;
            }
            template <class E>
            Outcome handle(Context &, const E &)
            {
                Outcome o;
                return command::merged(std::move(o), command::handled({}));
            }
            void rows(Context &, std::vector<row_idx> &) {}
            void paint(const PaintContext &, DrawList &) const {}
        };

        // 查找：Ctrl+F，输入，Enter 下一个，Esc 关闭
} // namespace table::detail
