#pragma once
// ============================================================================
// table12/detail/features/selection.hpp — SelectionFeature / ChecksFeature / ScrollFeature
//
//   纯移动自 detail/features.hpp（G3）；不改行为、不改顺序。
//   依赖：../common.hpp。
// ============================================================================

#include "../common.hpp"

namespace table::detail
{
        struct SelectionFeature
        {
            [[nodiscard]] static bool in_range(const State &v, int r, int c) noexcept
            {
                return v.sel.r0 >= 0 && v.sel.c0 >= 0 &&
                       r >= std::min(v.sel.r0, v.sel.r1) &&
                       r <= std::max(v.sel.r0, v.sel.r1) &&
                       c >= std::min(v.sel.c0, v.sel.c1) &&
                       c <= std::max(v.sel.c0, v.sel.c1);
            }

            static void sync_cells(const Table &d, State &v)
            {
                v.sel.cur_cell = d.cell_id(v.sel.cur_r, v.sel.cur_c);
                v.sel.r0_cell = d.cell_id(v.sel.r0, v.sel.c0);
                v.sel.r1_cell = d.cell_id(v.sel.r1, v.sel.c1);
            }

            static void anchor(State &v, int r, int c)
            {
                v.sel.r0 = v.sel.r1 = v.sel.cur_r = r;
                v.sel.c0 = v.sel.c1 = v.sel.cur_c = c;
                v.touch();
            }
            void set(Context &c, const Target &t)
            {
                set(c, t.row_index, t.col_index);
            }
            void set(Context &c, int r, int col)
            {
                anchor(c.state, r, col);
                sync_cells(c.model, c.state);
            }

            // 单击 = anchor；Shift+单击 = 从 anchor 扩到目标（Excel/Finder 语义）
            void set_or_extend(Context &c, const Target &t, bool shift)
            {
                if (shift && c.state.sel.r0 >= 0)
                    extend(c, t.row_index, t.col_index);
                else
                    set(c, t);
            }

            // 拖拽框选：指针出体区时按一行步长自动滚，并把目标夹到首/末可见行列
            void drag_extend(Context &c, float x, float y)
            {
                auto &v = c.state;
                const auto &g = c.layout;
                const float max_sy = std::max(0.0F, g.content_h - g.body.h);
                if (y < g.body.y && v.scroll.y > 0.0F)
                    v.scroll.y = std::max(0.0F, v.scroll.y - g.step);
                else if (y > g.body.y1())
                    v.scroll.y = std::min(max_sy, v.scroll.y + g.step);
                const float max_sx =
                    std::max(0.0F, g.content_w - g.frozen_w - g.scrollv.w);
                if (x < g.scrollv.x && v.scroll.x > 0.0F)
                    v.scroll.x = std::max(0.0F, v.scroll.x - 40.0F);
                else if (x > g.scrollv.x1())
                    v.scroll.x = std::min(max_sx, v.scroll.x + 40.0F);
                int r = g.row_at(y);
                int cc = g.col_at(x);
                if (r < 0 && !g.view_rows.empty())
                    r = y < g.body.y ? g.view_rows.front() : g.view_rows.back();
                if (cc < 0 && g.col_count() > 0)
                    cc = x < g.scrollv.x ? g.col_index.front() : g.col_index.back();
                if (r >= 0 && cc >= 0)
                    extend(c, r, cc);
                v.touch();
            }

            // 点分组头 = 选中这一组的全部行
            void all_of_group(Context &c, int first_col)
            {
                auto &v = c.state;
                for (const auto &sp : c.layout.groups)
                {
                    if (sp.first_col != first_col)
                        continue;
                    if (c.layout.view_rows.empty())
                        return;
                    v.sel.r0 = c.layout.view_rows.front();
                    v.sel.r1 = c.layout.view_rows.back();
                    v.sel.c0 = sp.first_col;
                    v.sel.c1 = sp.last_col;
                    v.sel.cur_r = v.sel.r1;
                    v.sel.cur_c = v.sel.c1;
                    sync_cells(c.model, v);
                    v.touch();
                    return;
                }
            }

            void extend(Context &c, int r, int cc)
            {
                auto &v = c.state;
                if (v.sel.r0 < 0)
                    anchor(v, r, cc);
                else
                {
                    v.sel.r1 = r;
                    v.sel.c1 = cc;
                    v.sel.cur_r = r;
                    v.sel.cur_c = cc;
                    v.touch();
                }
                sync_cells(c.model, v);
            }

            void move(Context &c, int dr, int dc, bool extend_sel)
            {
                auto &v = c.state;
                if (v.sel.cur_r < 0)
                {
                    if (!c.layout.view_rows.empty())
                        anchor(v, c.layout.view_rows.front(), 0);
                    return;
                }
                int nr = v.sel.cur_r;
                int nc = v.sel.cur_c;
                if (dc != 0)
                {
                    // 横向只走可见列（隐藏列不参与键盘导航）
                    int cp = c.layout.pos_of_col(nc);
                    if (cp < 0)
                        cp = 0;
                    const int np =
                        std::clamp(cp + dc, 0, std::max(c.layout.col_count() - 1, 0));
                    if (c.layout.col_count() > 0)
                        nc = c.layout.col_index[static_cast<std::size_t>(np)];
                }
                else
                {
                    int pos = 0;
                    for (int i = 0; i < static_cast<int>(c.layout.view_rows.size()); ++i)
                        if (c.layout.view_rows[static_cast<std::size_t>(i)] == nr)
                        {
                            pos = i;
                            break;
                        }
                    const int np = std::clamp(pos + dr, 0,
                                              static_cast<int>(c.layout.view_rows.size()) - 1);
                    nr = c.layout.view_rows[static_cast<std::size_t>(np)];
                }
                if (!extend_sel)
                    anchor(v, nr, nc);
                else
                {
                    v.sel.r1 = nr;
                    v.sel.c1 = nc;
                    v.sel.cur_r = nr;
                    v.sel.cur_c = nc;
                    v.touch();
                }
                sync_cells(c.model, v);
                ensure_visible(v, c.layout, c.theme, nr, nc);
            }

            void to(Context &c, int r, int cc, bool extend_sel)
            {
                auto &v = c.state;
                // Home/End 落在可见列的首/末列，而不是可能被隐藏的数据列
                if (c.layout.col_count() > 0)
                {
                    if (cc <= 0)
                        cc = c.layout.col_index.front();
                    else if (cc >= c.model.cols_count() - 1)
                        cc = c.layout.col_index.back();
                }
                if (!extend_sel)
                    anchor(v, r, cc);
                else
                {
                    v.sel.r1 = r;
                    v.sel.c1 = cc;
                    v.sel.cur_r = r;
                    v.sel.cur_c = cc;
                    v.touch();
                }
                sync_cells(c.model, v);
                ensure_visible(v, c.layout, c.theme, r, cc);
            }

            void all(Context &c)
            {
                auto &v = c.state;
                if (c.layout.view_rows.empty())
                    return;
                v.sel.r0 = c.layout.view_rows.front();
                v.sel.r1 = c.layout.view_rows.back();
                v.sel.c0 = c.layout.col_count() > 0 ? c.layout.col_index.front() : 0;
                v.sel.c1 =
                    c.layout.col_count() > 0 ? c.layout.col_index.back() : c.model.cols_count() - 1;
                v.sel.cur_r = v.sel.r1;
                v.sel.cur_c = v.sel.c1;
                sync_cells(c.model, v);
                v.touch();
            }

            void clear(Context &c)
            {
                auto &v = c.state;
                v.sel.r0 = v.sel.c0 = v.sel.r1 = v.sel.c1 = -1;
                v.sel.cur_r = v.sel.cur_c = -1;
                v.sel.cur_cell = v.sel.r0_cell = v.sel.r1_cell = {};
                v.touch();
            }

            template <class E>
            Outcome handle(Context &, const E &)
            {
                return command::handled({});
            }
            void rows(Context &, std::vector<row_idx> &) {}

            void paint(const PaintContext &c, DrawList &fr) const
            {
                if (c.state.sel.r0 < 0)
                    return;
                const int r0 = std::min(c.state.sel.r0, c.state.sel.r1);
                const int r1 = std::max(c.state.sel.r0, c.state.sel.r1);
                const int c0 = std::min(c.state.sel.c0, c.state.sel.c1);
                const int c1 = std::max(c.state.sel.c0, c.state.sel.c1);

                // 列头高亮
                for (int i = 0; i < c.layout.col_count(); ++i)
                {
                    const int dc = c.layout.col_index[static_cast<std::size_t>(i)];
                    if (dc < c0 || dc > c1)
                        continue;
                    const Rect hr =
                        clip_x({c.layout.col_x_at(i), c.layout.col_header.y,
                                c.layout.col_w[static_cast<std::size_t>(i)], c.layout.col_header.h},
                               c.layout.clip_x0(i), c.layout.clip_x1(i));
                    if (!hr.empty())
                        fr.draw.add_shape(Stream::content, hr, {1, 1, 1, 0.05F}, 0, {}, 0,
                                          0);
                }

                for (int ri = 0; ri < c.layout.row_count(); ++ri)
                {
                    const int dr = c.layout.row_index[static_cast<std::size_t>(ri)];
                    if (dr < r0 || dr > r1)
                        continue;
                    for (int ci = 0; ci < c.layout.col_count(); ++ci)
                    {
                        const int dc = c.layout.col_index[static_cast<std::size_t>(ci)];
                        if (dc < c0 || dc > c1)
                            continue;
                        if (c.state.edit.active() && c.state.edit.row == dr && c.state.edit.col == dc)
                            continue;
                        Rect cr = clip_x({c.layout.col_x_at(ci),
                                          c.layout.row_y[static_cast<std::size_t>(ri)],
                                          c.layout.col_w[static_cast<std::size_t>(ci)],
                                          c.layout.row_h[static_cast<std::size_t>(ri)]},
                                         c.layout.clip_x0(ci), c.layout.clip_x1(ci));
                        cr = intersect(cr, c.layout.body);
                        if (cr.empty())
                            continue;
                        fr.draw.add_shape(Stream::content, cr, c.theme.sel, c.theme.cell_radius,
                                          {}, 0, 0);
                        if (dr == c.state.sel.cur_r && dc == c.state.sel.cur_c)
                            fr.draw.add_shape(Stream::content, cr, {0, 0, 0, 0}, 0,
                                              c.theme.accent, 1.5F, 0);
                    }
                }

                // 选区外框
                float y0 = 1e30F, y1 = -1e30F, x0 = 1e30F, x1 = -1e30F;
                for (int ri = 0; ri < c.layout.row_count(); ++ri)
                {
                    const int dr = c.layout.row_index[static_cast<std::size_t>(ri)];
                    if (dr >= r0 && dr <= r1)
                    {
                        y0 = std::min(y0, c.layout.row_y[static_cast<std::size_t>(ri)]);
                        y1 = std::max(y1, c.layout.row_y[static_cast<std::size_t>(ri)] +
                                              c.layout.row_h[static_cast<std::size_t>(ri)]);
                    }
                }
                for (int i = 0; i < c.layout.col_count(); ++i)
                {
                    const int dc = c.layout.col_index[static_cast<std::size_t>(i)];
                    if (dc < c0 || dc > c1)
                        continue;
                    const float l = std::max(c.layout.col_x_at(i), c.layout.clip_x0(i));
                    const float r =
                        std::min(c.layout.col_x_at(i) + c.layout.col_w[static_cast<std::size_t>(i)],
                                 c.layout.clip_x1(i));
                    if (r > l)
                    {
                        x0 = std::min(x0, l);
                        x1 = std::max(x1, r);
                    }
                }
                if (y1 > y0 && x1 > x0)
                {
                    x0 = std::max(x0, c.layout.body.x);
                    x1 = std::min(x1, c.layout.body.x1());
                    y0 = std::max(y0, c.layout.body.y);
                    y1 = std::min(y1, c.layout.body.y1());
                    fr.draw.add_shape(Stream::content, {x0, y0, x1 - x0, 2}, c.theme.accent,
                                      0, {}, 0, 0);
                    fr.draw.add_shape(Stream::content, {x0, y1 - 2, x1 - x0, 2},
                                      c.theme.accent, 0, {}, 0, 0);
                    fr.draw.add_shape(Stream::content, {x0, y0, 2, y1 - y0}, c.theme.accent,
                                      0, {}, 0, 0);
                    fr.draw.add_shape(Stream::content, {x1 - 2, y0, 2, y1 - y0},
                                      c.theme.accent, 0, {}, 0, 0);
                }
            }
        };

        // 编辑：语义命令 + 改写 data 的 Effect（状态在 State.edit）
        struct ChecksFeature
        {
            bool toggle(Context &c, const Target &t, bool shift)
            {
                return toggle(c, t.row_index, shift);
            }
            bool toggle(Context &c, int row, bool shift)
            {
                auto &v = c.state;
                if (row < 0 || static_cast<std::size_t>(row) >= v.checks.on.size())
                    return true;
                const bool on = v.checks.on[static_cast<std::size_t>(row)] == 0;
                if (shift && v.checks.last >= 0)
                {
                    // 区间按“可见顺序”走：排序/筛选后也只勾选当前看得到的行
                    const int pl = c.layout.order_pos(v.checks.last);
                    const int pr = c.layout.order_pos(row);
                    if (pl >= 0 && pr >= 0)
                    {
                        const int lo = std::min(pl, pr);
                        const int hi = std::max(pl, pr);
                        for (int i = lo; i <= hi; ++i)
                        {
                            const int r = c.layout.view_rows[static_cast<std::size_t>(i)];
                            v.checks.on[static_cast<std::size_t>(r)] = on ? 1 : 0;
                            v.checks.ids[static_cast<std::size_t>(r)] =
                                c.model.ids[static_cast<std::size_t>(r)];
                        }
                    }
                    else
                    {
                        v.checks.on[static_cast<std::size_t>(row)] = on ? 1 : 0;
                        v.checks.ids[static_cast<std::size_t>(row)] =
                            c.model.ids[static_cast<std::size_t>(row)];
                    }
                }
                else
                {
                    v.checks.on[static_cast<std::size_t>(row)] = on ? 1 : 0;
                    v.checks.ids[static_cast<std::size_t>(row)] =
                        c.model.ids[static_cast<std::size_t>(row)];
                }
                v.checks.last = row;
                v.touch();
                return true;
            }
            bool toggle_all(Context &c)
            {
                auto &v = c.state;
                int on = 0;
                for (const int r : c.layout.view_rows)
                    on += v.checks.on[static_cast<std::size_t>(r)] != 0;
                const bool all = !c.layout.view_rows.empty() &&
                                 on == static_cast<int>(c.layout.view_rows.size());
                for (const int r : c.layout.view_rows)
                {
                    v.checks.on[static_cast<std::size_t>(r)] = all ? 0 : 1;
                    v.checks.ids[static_cast<std::size_t>(r)] =
                        c.model.ids[static_cast<std::size_t>(r)];
                }
                v.touch();
                return true;
            }
            template <class E>
            Outcome handle(Context &, const E &)
            {
                return command::handled({});
            }
            void rows(Context &, std::vector<row_idx> &) {}
            void paint(const PaintContext &c, DrawList &fr) const
            {
                if (c.layout.gutter_w <= 0)
                    return;
                fr.draw.add_shape(Stream::chrome, c.layout.gutter_head, c.theme.header_bg, {},
                                  {1, 1, 1, 0.08F}, 1.0F, zone_key({Part::check_head}));
                int total = 0, on = 0;
                for (const int r : c.layout.view_rows)
                {
                    ++total;
                    on += c.state.checks.on[static_cast<std::size_t>(r)] != 0;
                }
                const bool all = total > 0 && on == total;
                const bool Part = on > 0 && !all;
                const Rect hb{c.layout.gutter_head.x + (c.layout.gutter_head.w - 14) * 0.5F,
                              c.layout.gutter_head.y + (c.layout.gutter_head.h - 14) * 0.5F, 14,
                              14};
                if (all)
                    fr.draw.add_shape(Stream::chrome, hb, c.theme.accent, 3, {}, 0,
                                      zone_key({Part::check_head}));
                else
                    fr.draw.add_shape(Stream::chrome, hb, {0, 0, 0, 0}, 3,
                                      {glm::vec3(c.theme.accent), 0.55F}, 1.2F,
                                      zone_key({Part::check_head}));
                if (Part)
                    fr.draw.add_shape(Stream::chrome,
                                      {hb.x + 3, hb.y + hb.h * 0.5F - 1, hb.w - 6, 2},
                                      c.theme.accent, 1, {}, 0, zone_key({Part::check_head}));

                for (int ri = 0; ri < c.layout.row_count(); ++ri)
                {
                    const int dr = c.layout.row_index[static_cast<std::size_t>(ri)];
                    const Rect gr{c.layout.gutter_body.x,
                                  c.layout.row_y[static_cast<std::size_t>(ri)], c.layout.gutter_w,
                                  c.layout.row_h[static_cast<std::size_t>(ri)]};
                    const Rect bx{gr.x + (gr.w - 14) * 0.5F, gr.y + (gr.h - 14) * 0.5F,
                                  14, 14};
                    const auto zk = zone_key({Part::check, dr, -1});
                    if (c.state.checks.on[static_cast<std::size_t>(dr)] != 0)
                        fr.draw.add_shape(Stream::chrome, bx, c.theme.accent, 3, {}, 0, zk);
                    else
                        fr.draw.add_shape(Stream::chrome, bx, {0, 0, 0, 0}, 3,
                                          {glm::vec3(c.theme.accent), 0.45F}, 1.2F, zk);
                }
            }
        };

        // 滚动：命令 + 双滚动条绘制
        struct ScrollFeature
        {
            bool begin(Context &c, int axis, float base, float pos)
            {
                c.state.scroll.axis = axis;
                c.state.scroll.base = base;
                c.state.scroll.pos0 = pos;
                c.state.touch();
                return true;
            }
            bool move(Context &c, float pos)
            {
                auto &v = c.state;
                if (v.scroll.axis == 0)
                {
                    const float max_sy = std::max(0.0F, c.layout.content_h - c.layout.body.h);
                    const float travel = std::max(c.layout.vtrack.h - c.layout.vthumb.h, 1.0F);
                    v.scroll.y = std::clamp(v.scroll.base +
                                                (pos - v.scroll.pos0) * (max_sy / travel),
                                            0.0F, max_sy);
                }
                else
                {
                    const float max_sx =
                        std::max(0.0F, c.layout.content_w - c.layout.frozen_w - c.layout.scrollv.w);
                    const float travel = std::max(c.layout.htrack.w - c.layout.hthumb.w, 1.0F);
                    v.scroll.x = std::clamp(v.scroll.base +
                                                (pos - v.scroll.pos0) * (max_sx / travel),
                                            0.0F, max_sx);
                }
                v.touch();
                return true;
            }
            bool end(Context &c)
            {
                c.state.scroll.axis = 0;
                c.state.touch();
                return true;
            }
            template <class E>
            Outcome handle(Context &, const E &)
            {
                return command::handled({});
            }
            void rows(Context &, std::vector<row_idx> &) {}
            void paint(const PaintContext &c, DrawList &fr) const
            {
                if (c.layout.vbar)
                {
                    fr.draw.add_shape(Stream::chrome, c.layout.vtrack, c.theme.bar_track,
                                      c.theme.bar * 0.5F, {}, 0,
                                      zone_key({Part::vbar_track}));
                    const bool hot = c.interaction.hover.kind == Part::vbar_thumb ||
                                     c.interaction.drag == DragMode::vthumb;
                    fr.draw.add_shape(Stream::chrome, c.layout.vthumb,
                                      hot ? c.theme.bar_thumb_hot : c.theme.bar_thumb,
                                      c.theme.bar * 0.5F, {}, 0,
                                      zone_key({Part::vbar_thumb}));
                }
                if (c.layout.hbar)
                {
                    fr.draw.add_shape(Stream::chrome, c.layout.htrack, c.theme.bar_track,
                                      c.theme.bar * 0.5F, {}, 0,
                                      zone_key({Part::hbar_track}));
                    const bool hot = c.interaction.hover.kind == Part::hbar_thumb ||
                                     c.interaction.drag == DragMode::hthumb;
                    fr.draw.add_shape(Stream::chrome, c.layout.hthumb,
                                      hot ? c.theme.bar_thumb_hot : c.theme.bar_thumb,
                                      c.theme.bar * 0.5F, {}, 0,
                                      zone_key({Part::hbar_thumb}));
                }
            }
        };

        // 整表缩放：命令（无绘制）
} // namespace table::detail
