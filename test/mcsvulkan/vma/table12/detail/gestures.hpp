#pragma once
// ============================================================================
// table12/detail/gestures.hpp — gesture fallback：没被 feature 认领的命令
//
//   hit_zone 是“几何命中 + 语义覆盖（菜单 / 过滤按钮）”的唯一实现；
//   gesture 是“命令 → 手势语义（选择 / 拖拽 / 滚动）”的唯一实现。
//
//   两条链只在这里汇合：hit 得到的 Zone 与命令携带的 Target 是同一套词。
//
//   依赖：features。只被 session 使用。
// ============================================================================

#include "features.hpp"

namespace table::detail
{
        inline void note_click(State &v, Interaction &ix, const Press &c) noexcept
        {
            const bool near_last =
                !c.shift && ix.now - ix.last_click_t < 0.40 &&
                (c.x - ix.last_click_pos.x) * (c.x - ix.last_click_pos.x) +
                        (c.y - ix.last_click_pos.y) * (c.y - ix.last_click_pos.y) <
                    25.0F;
            ix.click_count =
                near_last ? std::min(std::max(ix.click_count, 1) + 1, 3) : 1;
            ix.last_click_t = ix.now;
            ix.last_click_pos = {c.x, c.y};
        }

        template <class E>
        inline Outcome gesture(const Table &d, State &v, Interaction &ix, const Theme &p,
                               const Layout &g, AppRouter &sy, const Font &ft,
                               const E &c)
        {
            Context tc{d, v, ix, p, g, ft, false, {}};
            {
                using T = E;
                if constexpr (std::is_same_v<T, Press>)
                {
                    const Zone cz = c.z;
                    const Target tgt =
                        c.t.kind != Part::none ? c.t : target_of(d, {}, cz);
                    const Zone z = tgt.to_zone();
                    ix.pointer = {c.x, c.y};
                    ix.left = true;
                    ix.drag_start_x = c.x;
                    ix.drag_start_y = c.y;
                    ix.header_moved = false;
                    const bool dbl = c.dbl || ix.click_count >= 2;
                    if (v.edit.active() && z.kind == Part::cell && v.edit.row == z.row &&
                        v.edit.col == z.col)
                        return {};
                    ix.drag = DragMode::none;
                    ix.drag_target = tgt;
                    visit_owner(
                        to_owner(tgt),
                        overloaded{
                            [&](const cell_owner &) {
                                sy.get<SelectionFeature>().set_or_extend(tc, tgt, c.shift);
                                ix.drag = DragMode::select;
                                if (dbl)
                                {
                                    sy.get<EditFeature>().begin(tc, tgt);
                                    if (ix.click_count >= 3)
                                        EditFeature::select_all(v);
                                    else
                                        sy.get<EditFeature>().select_word(tc, c.x);
                                }
                            },
                            [&](const check_owner &) {
                                sy.get<ChecksFeature>().toggle(tc, tgt, c.shift);
                            },
                            [&](const check_all_owner &) { sy.get<ChecksFeature>().toggle_all(tc); },
                            [&](const header_owner &h) {
                                if (h.col_index < 0)
                                    return;
                                ix.drag = DragMode::header;
                                sy.get<ReorderFeature>().begin(tc, tgt, c.x);
                            },
                            [&](const sep_owner &h) {
                                if (h.col_index < 0)
                                    return;
                                if (ix.click_count >= 2)
                                {
                                    autofit_column(tc, h.col_index);
                                    return;
                                }
                                ix.drag = DragMode::col_w;
                                sy.get<ResizeFeature>().begin(tc, tgt, c.x);
                            },
                            [&](const filter_owner &h) {
                                if (h.col_index >= 0)
                                    sy.get<FilterFeature>().begin(tc, tgt);
                            },
                            [&](const bar_owner &b) {
                                if (b.kind == Part::vbar_thumb)
                                {
                                    ix.drag = DragMode::vthumb;
                                    sy.get<ScrollFeature>().begin(tc, 0, v.scroll.y, c.y);
                                }
                                else if (b.kind == Part::hbar_thumb)
                                {
                                    ix.drag = DragMode::hthumb;
                                    sy.get<ScrollFeature>().begin(tc, 1, v.scroll.x, c.x);
                                }
                                else if (b.kind == Part::vbar_track)
                                {
                                    // 点轨道 = 翻一页（点在滑块上方往上，下方往下）
                                    const float page = std::max(g.body.h * 0.9F, g.step);
                                    const float max_sy =
                                        std::max(0.0F, g.content_h - g.body.h);
                                    v.scroll.y = std::clamp(
                                        v.scroll.y + (c.y < g.vthumb.y ? -page : page),
                                        0.0F, max_sy);
                                    v.touch();
                                }
                                else if (b.kind == Part::hbar_track)
                                {
                                    const float page =
                                        std::max(g.scrollv.w * 0.9F, 10.0F);
                                    const float max_sx = std::max(
                                        0.0F, g.content_w - g.frozen_w - g.scrollv.w);
                                    v.scroll.x = std::clamp(
                                        v.scroll.x + (c.x < g.hthumb.x ? -page : page),
                                        0.0F, max_sx);
                                    v.touch();
                                }
                            },
                            [&](const edge_owner &eo) {
                                ix.drag = DragMode::table_size;
                                ix.trs_edge = eo.kind;
                                sy.get<TableResizeFeature>().begin(
                                    tc, c.x, c.y,
                                    v.cols.user_w > 0 ? v.cols.user_w : g.box.w,
                                    v.cols.user_h > 0 ? v.cols.user_h : g.box.h);
                            },
                            [&](const menu_owner &) { sy.get<MenuFeature>().item(tc, tgt); },
                            [&](const frame_owner &) { sy.get<SelectionFeature>().clear(tc); },
                            [&](const group_owner &o) {
                                sy.get<SelectionFeature>().all_of_group(tc, o.col_index);
                                ix.drag = DragMode::none;
                            },
                            [&](const none_owner &) { sy.get<SelectionFeature>().clear(tc); }});
                    v.touch();
                }
                else if constexpr (std::is_same_v<T, Drag>)
                {
                    ix.pointer = {c.x, c.y};
                    ix.pointer = {c.x, c.y};
                    visit_drag(
                        ix.drag,
                        overloaded{[&](const DragSelect &) {
                                       sy.get<SelectionFeature>().drag_extend(tc, c.x, c.y);
                                   },
                                   [&](const DragColW &) { sy.get<ResizeFeature>().move(tc, c.x); },
                                   [&](const DragVThumb &) { sy.get<ScrollFeature>().move(tc, c.y); },
                                   [&](const DragHThumb &) { sy.get<ScrollFeature>().move(tc, c.x); },
                                   [&](const DragTableSize &) {
                                       sy.get<TableResizeFeature>().move(tc, c.x, c.y);
                                   },
                                   [&](const DragHeader &) { sy.get<ReorderFeature>().move(tc, c.x); },
                                   [&](const DragNone &) {}});
                    v.touch();
                }
                else if constexpr (std::is_same_v<T, Release>)
                {
                    ix.left = false;
                    if (ix.drag == DragMode::header)
                    {
                        const int sc = sy.get<ReorderFeature>().end(tc);
                        if (sc >= 0)
                        {
                            v.sort.toggle(sc);
                            sy.get<SelectionFeature>().clear(tc); // 排序后行位置变了，选区语义失效
                            v.touch();
                        }
                    }
                    if (ix.drag == DragMode::col_w)
                        sy.get<ResizeFeature>().end(tc);
                    if (ix.drag == DragMode::vthumb || ix.drag == DragMode::hthumb)
                        sy.get<ScrollFeature>().end(tc);
                    if (ix.drag == DragMode::table_size)
                        sy.get<TableResizeFeature>().end(tc);
                    ix.drag = DragMode::none;
                    v.touch();
                }
                else if constexpr (std::is_same_v<T, KeyPress>)
                {
                    switch (c.k)
                    {
                    case Key::up:
                        sy.get<SelectionFeature>().move(tc, -1, 0, c.shift);
                        break;
                    case Key::down:
                        sy.get<SelectionFeature>().move(tc, 1, 0, c.shift);
                        break;
                    case Key::left:
                        sy.get<SelectionFeature>().move(tc, 0, -1, c.shift);
                        break;
                    case Key::right:
                        sy.get<SelectionFeature>().move(tc, 0, 1, c.shift);
                        break;
                    case Key::home:
                        if (c.ctrl)
                            sy.get<SelectionFeature>().to(tc,
                                            g.view_rows.empty() ? 0 : g.view_rows.front(),
                                            0, c.shift);
                        else
                            sy.get<SelectionFeature>().to(tc, v.sel.cur_r, 0, c.shift);
                        break;
                    case Key::end:
                        if (c.ctrl)
                            sy.get<SelectionFeature>().to(tc,
                                            g.view_rows.empty() ? d.rows() - 1
                                                                : g.view_rows.back(),
                                            d.cols_count() - 1, c.shift);
                        else
                            sy.get<SelectionFeature>().to(tc, v.sel.cur_r, d.cols_count() - 1, c.shift);
                        break;
                    case Key::pgup:
                    case Key::pgdn: {
                        const int page = std::max(
                            1, static_cast<int>(g.body.h / std::max(g.step, 1.0F)) - 1);
                        sy.get<SelectionFeature>().move(tc, c.k == Key::pgup ? -page : page, 0,
                                          c.shift);
                        break;
                    }
                    case Key::enter:
                        sy.get<SelectionFeature>().move(tc, c.shift ? -1 : 1, 0, false);
                        break;
                    case Key::f2:
                        sy.get<EditFeature>().begin(tc, v.sel.cur_r, v.sel.cur_c);
                        break;
                    case Key::tab: {
                        // 浏览器式 tab 顺序：行末换到下一行行首，行首 Shift+Tab 回上一行行末
                        const int ncol = g.col_count();
                        if (ncol <= 0)
                            break;
                        const int pos = std::max(0, g.order_pos(v.sel.cur_r));
                        int cp = g.pos_of_col(v.sel.cur_c);
                        if (cp < 0)
                            cp = 0;
                        cp += c.shift ? -1 : 1;
                        int rp = pos;
                        if (cp >= ncol)
                        {
                            cp = 0;
                            rp = std::min(rp + 1,
                                          static_cast<int>(g.view_rows.size()) - 1);
                        }
                        else if (cp < 0)
                        {
                            cp = ncol - 1;
                            rp = std::max(rp - 1, 0);
                        }
                        if (!g.view_rows.empty())
                            sy.get<SelectionFeature>().to(tc, g.view_rows[static_cast<std::size_t>(rp)],
                                            g.col_index[static_cast<std::size_t>(cp)],
                                            false);
                        break;
                    }
                    case Key::del:
                    case Key::backspace:
                        sy.get<ClipboardFeature>().remove(tc);
                        break;
                    case Key::space:
                        if (v.sel.cur_r >= 0)
                            sy.get<ChecksFeature>().toggle(tc, v.sel.cur_r, c.shift);
                        break;
                    case Key::a:
                        if (c.ctrl)
                            sy.get<SelectionFeature>().all(tc);
                        break;
                    case Key::c:
                    case Key::x:
                        if (c.ctrl)
                            sy.get<ClipboardFeature>().copy(tc, c.k == Key::x);
                        break;
                    case Key::z:
                    case Key::y:
                        if (c.ctrl)
                            sy.get<UndoFeature>().run(tc, c.k == Key::y);
                        break;
                    case Key::f:
                        if (c.ctrl)
                            sy.get<FindFeature>().toggle(tc);
                        break;
                    default:
                        break;
                    }
                    v.touch();
                }
            }
            return {};
        }

        inline Zone hit_test(const Layout &g, float x, float y)
        {
            Zone best{};
            const auto test = [&](const Rect &r, const Zone &z) {
                if (x >= r.x && x < r.x1() && y >= r.y && y < r.y1())
                    best = z;
            };
            test(g.box, {Part::frame});
            if (g.group_h > 0)
            {
                for (const auto &sp : g.groups)
                {
                    const float x0 =
                        std::max(g.col_x_at(sp.first_pos), g.clip_x0(sp.first_pos));
                    const float x1 =
                        std::min(g.col_x_at(sp.last_pos) +
                                     g.col_w[static_cast<std::size_t>(sp.last_pos)],
                                 g.clip_x1(sp.last_pos));
                    if (x >= x0 && x < x1 && y >= g.group.y && y < g.group.y1())
                        best = {Part::header_group, -1, sp.first_col};
                }
            }
            for (int i = 0; i < g.col_count(); ++i)
            {
                const int dc = g.col_index[static_cast<std::size_t>(i)];
                test(clip_x({g.col_x_at(i), g.col_header.y,
                             g.col_w[static_cast<std::size_t>(i)], g.col_header.h},
                            g.clip_x0(i), g.clip_x1(i)),
                     {Part::header, -1, dc});
            }
            test(g.gutter_head, {Part::check_head});
            for (int ri = 0; ri < g.row_count(); ++ri)
            {
                const int dr = g.row_index[static_cast<std::size_t>(ri)];
                const float yy = g.row_y[static_cast<std::size_t>(ri)];
                const float hh = g.row_h[static_cast<std::size_t>(ri)];
                for (int ci = 0; ci < g.col_count(); ++ci)
                {
                    const int dc = g.col_index[static_cast<std::size_t>(ci)];
                    Rect cr = clip_x(
                        {g.col_x_at(ci), yy, g.col_w[static_cast<std::size_t>(ci)], hh},
                        g.clip_x0(ci), g.clip_x1(ci));
                    cr = intersect(cr, g.body);
                    test(cr, {Part::cell, dr, dc});
                }
                if (g.gutter_w > 0)
                    test({g.gutter_body.x, yy, g.gutter_w, hh}, {Part::check, dr, -1});
            }
            for (int i = 1; i < g.col_count(); ++i)
            {
                const int dc = g.col_index[static_cast<std::size_t>(i - 1)];
                const float xx = g.col_x_at(i);
                test(clip_x({xx - 3, g.col_header.y, 6, g.col_header.h}, g.clip_x0(i),
                            g.clip_x1(i)),
                     {Part::col_sep, -1, dc});
            }
            if (g.vbar)
            {
                test(g.vtrack, {Part::vbar_track});
                test(g.vthumb, {Part::vbar_thumb});
            }
            if (g.hbar)
            {
                test(g.htrack, {Part::hbar_track});
                test(g.hthumb, {Part::hbar_thumb});
            }
            test(g.edge_n, {Part::edge_n});
            test(g.edge_w, {Part::edge_w});
            test(g.edge_ne, {Part::edge_ne});
            test(g.edge_nw, {Part::edge_nw});
            test(g.edge_se, {Part::edge_se});
            test(g.edge_sw, {Part::edge_sw});
            return best;
        }

        // 唯一拾取入口：菜单覆盖层 → 几何 → 过滤按钮；不再用 frame.zones 二次覆盖
        [[nodiscard]] inline Zone hit_zone(const Table &d, const Layout &g, const State &v, float x,
                            float y, TableId tid)
        {
            if (v.menu.open)
            {
                for (int i = 0; i < menu_item_count; ++i)
                {
                    const Rect r = menu_item_rect(v, i);
                    if (x >= r.x && x < r.x1() && y >= r.y && y < r.y1())
                        return {Part::menu_item, -1, i, tid};
                }
                const Rect mb = menu_box_rect(v);
                if (x >= mb.x && x < mb.x1() && y >= mb.y && y < mb.y1())
                    return {};
            }
            Zone z = hit_test(g, x, y);
            z.table = tid;
            for (int i = 0; i < g.col_count(); ++i)
            {
                const int dc = g.col_index[static_cast<std::size_t>(i)];
                if (dc < 0 || dc >= d.cols_count() ||
                    !d.cols[static_cast<std::size_t>(dc)].filterable)
                    continue;
                const Rect hr =
                    clip_x({g.col_x_at(i), g.col_header.y,
                            g.col_w[static_cast<std::size_t>(i)], g.col_header.h},
                           g.clip_x0(i), g.clip_x1(i));
                if (hr.w <= 24)
                    continue;
                // 漏斗的可点区域比图标大一圈，但停在分隔条热区之前（否则列宽拖不动）
                const Rect btn{hr.x1() - 24, hr.y, 19, hr.h};
                if (x >= btn.x && x < btn.x1() && y >= btn.y && y < btn.y1())
                    return {Part::filter_btn, -1, dc, tid};
            }
            return z;
        }


        // ── 唯一命中入口的底层实现：几何 + 语义覆盖 → Target ───────────
        [[nodiscard]] inline Target hit_target(const Table &d, const Layout &g,
                                               const State &v, float x, float y,
                                               TableId tid = {})
        {
            return model::target_of(d, tid, hit_zone(d, g, v, x, y, tid));
        }
} // namespace table::detail
