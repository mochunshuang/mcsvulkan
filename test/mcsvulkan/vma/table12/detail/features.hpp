#pragma once
// ============================================================================
// table12/detail/features.hpp — feature 孤岛（命令 / 行 / 绘制三段行为）
//
//   每个 Feature 是孤岛：只认 Context（或 PaintContext）和命令，
//   不认其他 feature，不认 host/window/GPU。顺序只写在 AppRouter 里。
//
//   AppRouter 是 CommandRouter：feature 的有序组合，四处顺序 = 行为（F7）：
//     handle : 15 个 feature 的短路链（第一个认领的生效）
//     rows   : filter → sort
//     paint  : 同序
//     text   : filter → find → edit
//
//   依赖：command（schema）。只被 session 使用，公共头不 include。
// ============================================================================

#include "common.hpp"

namespace table::detail
{
        // 列宽自适应：标题与所有单元格里最宽的一行 + 内边距（菜单 Fit 与双击分隔条共用）
        inline void autofit_column(Context &c, int col) noexcept
        {
            auto &v = c.v;
            if (col < 0 || col >= c.d.cols_count() ||
                static_cast<std::size_t>(col) >= v.cols.w.size())
                return;
            float w = text_width(c.ft, c.d.cols[static_cast<std::size_t>(col)].title);
            for (int r = 0; r < c.d.rows(); ++r)
                w = std::max(w, text_width(c.ft, c.d.text(r, col)));
            const auto &def = c.d.cols[static_cast<std::size_t>(col)];
            v.cols.w[static_cast<std::size_t>(col)] =
                std::clamp(w + 2.0F * c.p.cell_pad + 24.0F, def.min_w, def.max_w);
            v.cols.user[static_cast<std::size_t>(col)] = 1;
            v.touch();
        }

        // 列操作：菜单项与（将来的）右键命令共用同一批实现
        inline void reset_sort(State &v) noexcept
        {
            v.sort.col = -1;
            v.sort.dir = 0;
        }
        inline void hide_column(Context &c, int col) noexcept
        {
            auto &v = c.v;
            int visible = 0;
            for (const auto h : v.cols.hidden)
                visible += h == 0 ? 1 : 0;
            if (visible <= 1 || col < 0 ||
                static_cast<std::size_t>(col) >= v.cols.hidden.size())
                return;
            v.cols.hidden[static_cast<std::size_t>(col)] = 1;
            if (static_cast<std::size_t>(col) < v.filter.text.size())
                v.filter.text[static_cast<std::size_t>(col)].clear();
            v.touch();
        }
        inline void show_all_columns(Context &c) noexcept
        {
            std::fill(c.v.cols.hidden.begin(), c.v.cols.hidden.end(), 0);
            c.v.touch();
        }

        struct SortFeature
        {
            bool toggle(Context &c, int col)
            {
                c.v.sort.toggle(col);
                c.v.touch();
                return true;
            }
            template <class E>
            bool handle(Context &, const E &)
            {
                return false;
            }
            void rows(Context &c, std::vector<row_idx> &r)
            {
                if (c.v.sort.dir == 0 || c.v.sort.col < 0)
                    return;
                const int cc = c.v.sort.col;
                const bool desc = c.v.sort.dir == 2;
                std::stable_sort(r.begin(), r.end(), [&](int a, int b) {
                    const auto x = c.d.text(a, cc);
                    const auto y = c.d.text(b, cc);
                    return desc ? x > y : x < y;
                });
            }

            void paint(const PaintContext &c, DrawList &fr) const
            {
                if (c.v.sort.dir == 0 || c.v.sort.col < 0)
                    return;
                const int pos = c.g.pos_of_col(c.v.sort.col);
                if (pos < 0)
                    return;
                const Rect hr =
                    clip_x({c.g.col_x_at(pos), c.g.col_header.y,
                            c.g.col_w[static_cast<std::size_t>(pos)], c.g.col_header.h},
                           c.g.clip_x0(pos), c.g.clip_x1(pos));
                if (hr.empty())
                    return;
                fr.draw.add_shape(Stream::chrome,
                                  {hr.x1() - 12, hr.y + hr.h * 0.5F - 3, 8, 6},
                                  c.p.header_text, 1, {}, 0,
                                  zone_key({Part::header, -1, c.v.sort.col}));
            }
        };

        // 筛选：语义命令 + 行 stage + 列头漏斗（状态在 State.filter）
        struct FilterFeature
        {
            bool begin(Context &c, const Target &t)
            {
                return begin(c, t.col_index);
            }
            bool begin(Context &c, int col)
            {
                auto &v = c.v;
                v.filter.edit = col;
                v.filter.compose.clear();
                v.filter.input = static_cast<std::size_t>(col) < v.filter.text.size()
                                     ? v.filter.text[static_cast<std::size_t>(col)]
                                     : std::string{};
                v.filter.orig = v.filter.input;
                v.touch();
                return true;
            }
            bool set(Context &c, int col, std::string text)
            {
                auto &f = c.v.filter.text;
                if (static_cast<std::size_t>(c.d.cols_count()) > f.size())
                    f.resize(static_cast<std::size_t>(c.d.cols_count()));
                if (col >= 0 && static_cast<std::size_t>(col) < f.size())
                    f[static_cast<std::size_t>(col)] = std::move(text);
                c.v.touch();
                return true;
            }
            bool clear(Context &c, int col)
            {
                auto &f = c.v.filter.text;
                if (col < 0)
                    f.assign(static_cast<std::size_t>(c.d.cols_count()), {});
                else if (static_cast<std::size_t>(col) < f.size())
                    f[static_cast<std::size_t>(col)].clear();
                c.v.touch();
                return true;
            }
            static void sync_live(State &v) noexcept
            {
                if (v.filter.edit < 0)
                    return;
                const auto col = static_cast<std::size_t>(v.filter.edit);
                if (col < v.filter.text.size())
                    v.filter.text[col] = v.filter.input;
            }
            bool text(Context &c, std::string_view s)
            {
                if (c.v.filter.edit < 0)
                    return false;
                c.v.filter.input.append(s);
                c.v.filter.compose.clear();
                sync_live(c.v); // 边打边过滤，而不是等 Enter
                c.v.touch();
                return true;
            }
            [[nodiscard]] static bool active(const State &v, int col) noexcept
            {
                return col >= 0 && static_cast<std::size_t>(col) < v.filter.text.size() &&
                       !v.filter.text[static_cast<std::size_t>(col)].empty();
            }

            template <class E>
            bool handle(Context &c, const E &x)
            {
                if constexpr (std::is_same_v<E, Press>)
                    return press(c, x);
                else if constexpr (std::is_same_v<E, KeyPress>)
                    return key(c, x);
                return false;
            }
            bool press(Context &c, const Press &pr)
            {
                auto &v = c.v;
                if (v.filter.edit < 0)
                    return false;
                if (pr.z.kind != Part::filter_btn)
                {
                    if (static_cast<std::size_t>(v.filter.edit) < v.filter.text.size())
                        v.filter.text[static_cast<std::size_t>(v.filter.edit)] =
                            v.filter.input;
                    v.filter.edit = -1;
                    v.filter.orig.clear();
                    v.touch();
                }
                return false;
            }
            bool key(Context &c, const KeyPress &k)
            {
                auto &v = c.v;
                if (v.filter.edit < 0)
                    return false;
                v.filter.compose.clear();
                if (k.k == Key::esc)
                {
                    // 取消：还原进入编辑前的筛选串
                    if (static_cast<std::size_t>(v.filter.edit) < v.filter.text.size())
                        v.filter.text[static_cast<std::size_t>(v.filter.edit)] =
                            v.filter.orig;
                    v.filter.input = v.filter.orig;
                    v.filter.orig.clear();
                    v.filter.edit = -1;
                }
                else if (k.k == Key::enter || k.k == Key::tab)
                {
                    if (static_cast<std::size_t>(v.filter.edit) < v.filter.text.size())
                        v.filter.text[static_cast<std::size_t>(v.filter.edit)] =
                            v.filter.input;
                    v.filter.orig.clear();
                    v.filter.edit = -1;
                }
                else if (k.k == Key::backspace && !v.filter.input.empty())
                {
                    const std::size_t q = utf8_prev(std::string_view(v.filter.input),
                                                    v.filter.input.size());
                    v.filter.input.erase(q);
                    sync_live(v);
                }
                else if (k.k == Key::del)
                {
                    v.filter.input.clear();
                    sync_live(v);
                }
                v.touch();
                return true;
            }

            void rows(Context &c, std::vector<row_idx> &r)
            {
                const auto &f = c.v.filter.text;
                if (f.empty())
                    return;
                std::vector<row_idx> keep;
                keep.reserve(r.size());
                for (const int row : r)
                {
                    bool ok = true;
                    for (std::size_t col = 0; col < f.size() && ok; ++col)
                        if (!f[col].empty())
                            ok = contains(c.d.text(row, static_cast<int>(col)), f[col]);
                    if (ok)
                        keep.push_back(row);
                }
                r.swap(keep);
            }

            void paint(const PaintContext &c, DrawList &fr) const
            {
                if (c.v.filter.edit >= 0)
                {
                    const int pos = c.g.pos_of_col(c.v.filter.edit);
                    if (pos >= 0)
                    {
                        Rect hr = clip_x({c.g.col_x_at(pos), c.g.col_header.y,
                                          c.g.col_w[static_cast<std::size_t>(pos)],
                                          c.g.col_header.h},
                                         c.g.clip_x0(pos), c.g.clip_x1(pos));
                        if (!hr.empty())
                        {
                            const std::string shown =
                                c.v.filter.input + c.v.filter.compose;
                            fr.draw.add_shape(
                                Stream::overlay, hr, {0.06F, 0.08F, 0.12F, 1.0F},
                                c.p.cell_radius, c.p.accent, 1.2F,
                                zone_key({Part::filter_btn, -1, c.v.filter.edit}));
                            fr.draw.add_text(
                                Stream::overlay,
                                {hr.x + 6, hr.y, std::max(hr.w - 12, 1.0F), hr.h},
                                c.p.text, hr.y + hr.h * 0.5F + c.ft.px * 0.30F, shown, 0);
                            const float tw = text_width(c.ft, shown);
                            fr.draw.add_shape(Stream::overlay,
                                              {hr.x + 6 + tw, hr.y + 6, 1.5F,
                                               std::max(hr.h - 12, 1.0F)},
                                              c.p.accent, 0, {}, 0, 0);
                        }
                    }
                    return;
                }
                for (int i = 0; i < c.g.col_count(); ++i)
                {
                    const int dc = c.g.col_index[static_cast<std::size_t>(i)];
                    if (dc < 0 || dc >= c.d.cols_count() ||
                        !c.d.cols[static_cast<std::size_t>(dc)].filterable)
                        continue;
                    const Rect hr =
                        clip_x({c.g.col_x_at(i), c.g.col_header.y,
                                c.g.col_w[static_cast<std::size_t>(i)], c.g.col_header.h},
                               c.g.clip_x0(i), c.g.clip_x1(i));
                    if (hr.w < 30)
                        continue;
                    const float x = hr.x1() - 16;
                    const float y = hr.y + hr.h * 0.5F - 4;
                    const auto color =
                        active(c.v, dc) ? c.p.accent
                                        : glm::vec4(c.p.header_text.r, c.p.header_text.g,
                                                    c.p.header_text.b, 0.5F);
                    fr.draw.add_shape(Stream::chrome, {x, y, 10, 2}, color, 1, {}, 0,
                                      zone_key({Part::header, -1, dc}));
                    fr.draw.add_shape(Stream::chrome, {x + 2, y + 3, 6, 2}, color, 1, {},
                                      0, zone_key({Part::header, -1, dc}));
                    fr.draw.add_shape(Stream::chrome, {x + 4, y + 6, 2, 2}, color, 1, {},
                                      0, zone_key({Part::header, -1, dc}));
                }
            }

            static bool contains(std::string_view h, std::string_view n) noexcept
            {
                if (n.empty())
                    return true;
                if (h.size() < n.size())
                    return false;
                const auto eq = [](char a, char b) {
                    if (a >= 'A' && a <= 'Z')
                        a = static_cast<char>(a - 'A' + 'a');
                    if (b >= 'A' && b <= 'Z')
                        b = static_cast<char>(b - 'A' + 'a');
                    return a == b;
                };
                for (std::size_t i = 0; i + n.size() <= h.size(); ++i)
                {
                    std::size_t k = 0;
                    while (k < n.size() && eq(h[i + k], n[k]))
                        ++k;
                    if (k == n.size())
                        return true;
                }
                return false;
            }
        };

        // 选择：语义命令 + 绘制（状态在 State 的 sel_*/cur_*）
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
                anchor(c.v, r, col);
                sync_cells(c.d, c.v);
            }

            // 单击 = anchor；Shift+单击 = 从 anchor 扩到目标（Excel/Finder 语义）
            void set_or_extend(Context &c, const Target &t, bool shift)
            {
                if (shift && c.v.sel.r0 >= 0)
                    extend(c, t.row_index, t.col_index);
                else
                    set(c, t);
            }

            // 拖拽框选：指针出体区时按一行步长自动滚，并把目标夹到首/末可见行列
            void drag_extend(Context &c, float x, float y)
            {
                auto &v = c.v;
                const auto &g = c.g;
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
                auto &v = c.v;
                for (const auto &sp : c.g.groups)
                {
                    if (sp.first_col != first_col)
                        continue;
                    if (c.g.view_rows.empty())
                        return;
                    v.sel.r0 = c.g.view_rows.front();
                    v.sel.r1 = c.g.view_rows.back();
                    v.sel.c0 = sp.first_col;
                    v.sel.c1 = sp.last_col;
                    v.sel.cur_r = v.sel.r1;
                    v.sel.cur_c = v.sel.c1;
                    sync_cells(c.d, v);
                    v.touch();
                    return;
                }
            }

            void extend(Context &c, int r, int cc)
            {
                auto &v = c.v;
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
                sync_cells(c.d, v);
            }

            void move(Context &c, int dr, int dc, bool extend_sel)
            {
                auto &v = c.v;
                if (v.sel.cur_r < 0)
                {
                    if (!c.g.view_rows.empty())
                        anchor(v, c.g.view_rows.front(), 0);
                    return;
                }
                int nr = v.sel.cur_r;
                int nc = v.sel.cur_c;
                if (dc != 0)
                {
                    // 横向只走可见列（隐藏列不参与键盘导航）
                    int cp = c.g.pos_of_col(nc);
                    if (cp < 0)
                        cp = 0;
                    const int np =
                        std::clamp(cp + dc, 0, std::max(c.g.col_count() - 1, 0));
                    if (c.g.col_count() > 0)
                        nc = c.g.col_index[static_cast<std::size_t>(np)];
                }
                else
                {
                    int pos = 0;
                    for (int i = 0; i < static_cast<int>(c.g.view_rows.size()); ++i)
                        if (c.g.view_rows[static_cast<std::size_t>(i)] == nr)
                        {
                            pos = i;
                            break;
                        }
                    const int np = std::clamp(pos + dr, 0,
                                              static_cast<int>(c.g.view_rows.size()) - 1);
                    nr = c.g.view_rows[static_cast<std::size_t>(np)];
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
                sync_cells(c.d, v);
                ensure_visible(v, c.g, c.p, nr, nc);
            }

            void to(Context &c, int r, int cc, bool extend_sel)
            {
                auto &v = c.v;
                // Home/End 落在可见列的首/末列，而不是可能被隐藏的数据列
                if (c.g.col_count() > 0)
                {
                    if (cc <= 0)
                        cc = c.g.col_index.front();
                    else if (cc >= c.d.cols_count() - 1)
                        cc = c.g.col_index.back();
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
                sync_cells(c.d, v);
                ensure_visible(v, c.g, c.p, r, cc);
            }

            void all(Context &c)
            {
                auto &v = c.v;
                if (c.g.view_rows.empty())
                    return;
                v.sel.r0 = c.g.view_rows.front();
                v.sel.r1 = c.g.view_rows.back();
                v.sel.c0 = c.g.col_count() > 0 ? c.g.col_index.front() : 0;
                v.sel.c1 =
                    c.g.col_count() > 0 ? c.g.col_index.back() : c.d.cols_count() - 1;
                v.sel.cur_r = v.sel.r1;
                v.sel.cur_c = v.sel.c1;
                sync_cells(c.d, v);
                v.touch();
            }

            void clear(Context &c)
            {
                auto &v = c.v;
                v.sel.r0 = v.sel.c0 = v.sel.r1 = v.sel.c1 = -1;
                v.sel.cur_r = v.sel.cur_c = -1;
                v.sel.cur_cell = v.sel.r0_cell = v.sel.r1_cell = {};
                v.touch();
            }

            template <class E>
            bool handle(Context &, const E &)
            {
                return false;
            }
            void rows(Context &, std::vector<row_idx> &) {}

            void paint(const PaintContext &c, DrawList &fr) const
            {
                if (c.v.sel.r0 < 0)
                    return;
                const int r0 = std::min(c.v.sel.r0, c.v.sel.r1);
                const int r1 = std::max(c.v.sel.r0, c.v.sel.r1);
                const int c0 = std::min(c.v.sel.c0, c.v.sel.c1);
                const int c1 = std::max(c.v.sel.c0, c.v.sel.c1);

                // 列头高亮
                for (int i = 0; i < c.g.col_count(); ++i)
                {
                    const int dc = c.g.col_index[static_cast<std::size_t>(i)];
                    if (dc < c0 || dc > c1)
                        continue;
                    const Rect hr =
                        clip_x({c.g.col_x_at(i), c.g.col_header.y,
                                c.g.col_w[static_cast<std::size_t>(i)], c.g.col_header.h},
                               c.g.clip_x0(i), c.g.clip_x1(i));
                    if (!hr.empty())
                        fr.draw.add_shape(Stream::content, hr, {1, 1, 1, 0.05F}, 0, {}, 0,
                                          0);
                }

                for (int ri = 0; ri < c.g.row_count(); ++ri)
                {
                    const int dr = c.g.row_index[static_cast<std::size_t>(ri)];
                    if (dr < r0 || dr > r1)
                        continue;
                    for (int ci = 0; ci < c.g.col_count(); ++ci)
                    {
                        const int dc = c.g.col_index[static_cast<std::size_t>(ci)];
                        if (dc < c0 || dc > c1)
                            continue;
                        if (c.v.edit.active() && c.v.edit.row == dr && c.v.edit.col == dc)
                            continue;
                        Rect cr = clip_x({c.g.col_x_at(ci),
                                          c.g.row_y[static_cast<std::size_t>(ri)],
                                          c.g.col_w[static_cast<std::size_t>(ci)],
                                          c.g.row_h[static_cast<std::size_t>(ri)]},
                                         c.g.clip_x0(ci), c.g.clip_x1(ci));
                        cr = intersect(cr, c.g.body);
                        if (cr.empty())
                            continue;
                        fr.draw.add_shape(Stream::content, cr, c.p.sel, c.p.cell_radius,
                                          {}, 0, 0);
                        if (dr == c.v.sel.cur_r && dc == c.v.sel.cur_c)
                            fr.draw.add_shape(Stream::content, cr, {0, 0, 0, 0}, 0,
                                              c.p.accent, 1.5F, 0);
                    }
                }

                // 选区外框
                float y0 = 1e30F, y1 = -1e30F, x0 = 1e30F, x1 = -1e30F;
                for (int ri = 0; ri < c.g.row_count(); ++ri)
                {
                    const int dr = c.g.row_index[static_cast<std::size_t>(ri)];
                    if (dr >= r0 && dr <= r1)
                    {
                        y0 = std::min(y0, c.g.row_y[static_cast<std::size_t>(ri)]);
                        y1 = std::max(y1, c.g.row_y[static_cast<std::size_t>(ri)] +
                                              c.g.row_h[static_cast<std::size_t>(ri)]);
                    }
                }
                for (int i = 0; i < c.g.col_count(); ++i)
                {
                    const int dc = c.g.col_index[static_cast<std::size_t>(i)];
                    if (dc < c0 || dc > c1)
                        continue;
                    const float l = std::max(c.g.col_x_at(i), c.g.clip_x0(i));
                    const float r =
                        std::min(c.g.col_x_at(i) + c.g.col_w[static_cast<std::size_t>(i)],
                                 c.g.clip_x1(i));
                    if (r > l)
                    {
                        x0 = std::min(x0, l);
                        x1 = std::max(x1, r);
                    }
                }
                if (y1 > y0 && x1 > x0)
                {
                    x0 = std::max(x0, c.g.body.x);
                    x1 = std::min(x1, c.g.body.x1());
                    y0 = std::max(y0, c.g.body.y);
                    y1 = std::min(y1, c.g.body.y1());
                    fr.draw.add_shape(Stream::content, {x0, y0, x1 - x0, 2}, c.p.accent,
                                      0, {}, 0, 0);
                    fr.draw.add_shape(Stream::content, {x0, y1 - 2, x1 - x0, 2},
                                      c.p.accent, 0, {}, 0, 0);
                    fr.draw.add_shape(Stream::content, {x0, y0, 2, y1 - y0}, c.p.accent,
                                      0, {}, 0, 0);
                    fr.draw.add_shape(Stream::content, {x1 - 2, y0, 2, y1 - y0},
                                      c.p.accent, 0, {}, 0, 0);
                }
            }
        };

        // 编辑：语义命令 + 改写 data 的 Effect（状态在 State.edit）
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

            static void commit(Context &c)
            {
                auto &e = c.v.edit;
                if (!e.active())
                    return;
                if (e.buf != e.orig)
                {
                    const auto id = e.ref.row;
                    if (c.trace)
                        std::println("[commit] row={} col={} id={} text={}", e.row,
                                     e.col, id.v, e.buf);
                    if (c.fx)
                        c.fx->push_back(SetCell{id, e.col, e.buf});
                    c.v.history.undo.push_back({{id, e.col, e.orig, e.buf}});
                    c.v.history.redo.clear();
                }
                e = {};
                c.v.touch();
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
                const auto &e = c.v.edit;
                const int pos = c.g.pos_of_col(e.col);
                if (pos < 0)
                    return 0;
                const float left = c.g.col_x_at(pos) + c.p.cell_pad;
                int best = 0;
                float bestd = 1.0e30F;
                std::size_t i = 0;
                while (true)
                {
                    const float w =
                        text_width(c.ft, std::string_view(e.buf).substr(0, i));
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

            bool paste(Context &c, std::string text)
            {
                auto &v = c.v;
                if (v.edit.active())
                {
                    snapshot(v, c.ix, false);
                    for (char &ch : text)
                        if (ch == '\n' || ch == '\r' || ch == '\t')
                            ch = ' ';
                    replace_sel(v, text);
                }
                else if (v.sel.cur_r >= 0 && v.sel.cur_c >= 0 && c.fx)
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
                            if (rr >= 0 && rr < c.d.rows() && cc >= 0 &&
                                cc < c.d.cols_count())
                            {
                                const auto old = c.d.text(rr, cc);
                                const auto id = c.d.ids[static_cast<std::size_t>(rr)];
                                std::string val{ln.substr(p, e2 - p)};
                                c.fx->push_back(SetCell{id, cc, val});
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
                return true;
            }

            bool begin(Context &c, const Target &t)
            {
                return begin(c, t.row_index, t.col_index);
            }
            bool begin(Context &c, int r, int col)
            {
                start(c.d, c.v, r, col);
                return true;
            }

            bool compose(Context &c, std::string text)
            {
                if (c.v.edit.active())
                {
                    c.v.edit.compose = std::move(text);
                    c.v.touch();
                }
                return true;
            }

            bool text(Context &c, std::string_view s)
            {
                auto &v = c.v;
                if (v.edit.active())
                {
                    snapshot(v, c.ix, true);
                    replace_sel(v, s);
                }
                else if (v.sel.cur_r >= 0 && v.sel.cur_c >= 0)
                {
                    start(c.d, v, v.sel.cur_r, v.sel.cur_c);
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
                auto &e = c.v.edit;
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
                c.v.touch();
            }

            template <class E>
            bool handle(Context &c, const E &x)
            {
                auto &v = c.v;
                auto &ix = c.ix;
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
                            commit(c);
                    }
                    return false; // 让 core 继续处理按下
                }

                else if constexpr (std::is_same_v<T, Drag>)
                {
                    if (!v.edit.active())
                        return false;
                    const Target tgt =
                        x.t.kind != Part::none ? x.t : target_of(c.d, {}, x.t.to_zone());
                    bool in_row = false;
                    if (tgt.kind == Part::cell)
                        in_row = tgt.row_index == v.edit.row;
                    else
                        for (int i = 0; i < c.g.row_count(); ++i)
                        {
                            if (c.g.row_index[static_cast<std::size_t>(i)] != v.edit.row)
                                continue;
                            const float y0 = c.g.row_y[static_cast<std::size_t>(i)];
                            const float y1 = y0 + c.g.row_h[static_cast<std::size_t>(i)];
                            in_row = x.y >= y0 && x.y < y1;
                            break;
                        }
                    if (in_row)
                    {
                        v.edit.caret = caret_at(c, x.x);
                        v.touch();
                        return true; // 编辑框内拖动 = 选文本，不是选单元格
                    }
                    return false;
                }
                else if constexpr (std::is_same_v<T, KeyPress>)
                {
                    if (!v.edit.active())
                        return false;
                    auto &e = v.edit;
                    switch (x.k)
                    {
                    case Key::esc:
                        e = {};
                        break;
                    case Key::enter: {
                        const int row = e.row;
                        const int col = e.col;
                        commit(c);
                        const int pos = pos_of(c.g, row);
                        if (pos + 1 < static_cast<int>(c.g.view_rows.size()))
                        {
                            const int nr =
                                c.g.view_rows[static_cast<std::size_t>(pos + 1)];
                            v.sel.r0 = v.sel.r1 = v.sel.cur_r = nr;
                            v.sel.c0 = v.sel.c1 = v.sel.cur_c = col;
                            v.touch();
                            ensure_visible(v, c.g, c.p, nr, col);
                        }
                        break;
                    }
                    case Key::tab: {
                        commit(c);
                        const int nc = std::clamp(v.sel.cur_c + (x.shift ? -1 : 1), 0,
                                                  c.d.cols_count() - 1);
                        v.sel.cur_c = v.sel.c0 = v.sel.c1 = nc;
                        v.touch();
                        ensure_visible(v, c.g, c.p, v.sel.cur_r, nc);
                        break;
                    }
                    case Key::backspace:
                        snapshot(v, c.ix, true);
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
                        snapshot(v, c.ix, true);
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
                        if (x.ctrl && c.fx)
                            c.fx->push_back(WriteClipboard{
                                e.has_sel()
                                    ? e.buf.substr(static_cast<std::size_t>(e.sel_lo()),
                                                   static_cast<std::size_t>(e.sel_hi() -
                                                                            e.sel_lo()))
                                    : e.buf});
                        break;
                    case Key::x:
                        if (x.ctrl)
                        {
                            if (e.has_sel() && c.fx)
                                c.fx->push_back(WriteClipboard{e.buf.substr(
                                    static_cast<std::size_t>(e.sel_lo()),
                                    static_cast<std::size_t>(e.sel_hi() - e.sel_lo()))});
                            snapshot(v, c.ix, true);
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
                    return true;
                }
                return false;
            }

            void rows(Context &, std::vector<row_idx> &) {}

            void paint(const PaintContext &c, DrawList &fr) const
            {
                const auto &e = c.v.edit;
                if (!e.active())
                    return;
                int ri = -1, ci = -1;
                for (int i = 0; i < c.g.row_count(); ++i)
                    if (c.g.row_index[static_cast<std::size_t>(i)] == e.row)
                        ri = i;
                for (int i = 0; i < c.g.col_count(); ++i)
                    if (c.g.col_index[static_cast<std::size_t>(i)] == e.col)
                        ci = i;
                if (ri < 0 || ci < 0)
                    return;
                Rect cr =
                    clip_x({c.g.col_x_at(ci), c.g.row_y[static_cast<std::size_t>(ri)],
                            c.g.col_w[static_cast<std::size_t>(ci)],
                            c.g.row_h[static_cast<std::size_t>(ri)]},
                           c.g.clip_x0(ci), c.g.clip_x1(ci));
                cr = intersect(cr, c.g.body);
                if (cr.empty())
                    return;
                fr.draw.add_shape(Stream::overlay, cr, {0.06F, 0.08F, 0.12F, 1.0F},
                                  c.p.cell_radius, {}, 0, 0);
                const float inner = std::max(cr.w - 2 * c.p.cell_pad, 1.0F);
                const float pre = text_width(
                    c.ft,
                    std::string_view(e.buf).substr(0, static_cast<std::size_t>(e.caret)));
                const float off = pre > inner ? pre - inner + 2.0F : 0.0F;
                if (e.has_sel())
                {
                    const float lo =
                        text_width(c.ft, std::string_view(e.buf).substr(0, e.sel_lo()));
                    const float hi =
                        text_width(c.ft, std::string_view(e.buf).substr(0, e.sel_hi()));
                    if (hi > lo)
                        fr.draw.add_shape(
                            Stream::overlay,
                            {cr.x + c.p.cell_pad + lo - off, cr.y + 4, hi - lo,
                             std::max(cr.h - 8, 1.0F)},
                            {c.p.accent.r, c.p.accent.g, c.p.accent.b, 0.35F}, 0, {}, 0,
                            0);
                }
                const Rect tx = intersect(
                    {cr.x + c.p.cell_pad - off, cr.y, inner + off, cr.h}, c.g.body);
                fr.draw.add_text(Stream::overlay, tx, c.p.text,
                                 cr.y + cr.h * 0.5F + c.ft.px * 0.30F, e.buf, 0);
                const float cx = std::clamp(cr.x + c.p.cell_pad + pre - off, cr.x + 1,
                                            cr.x + cr.w - 2);
                fr.draw.add_shape(Stream::overlay,
                                  {cx, cr.y + 4, 1.5F, std::max(cr.h - 8, 1.0F)},
                                  c.p.accent, 0, {}, 0, 0);
                if (!e.compose.empty())
                {
                    const float cw = text_width(c.ft, e.compose);
                    fr.draw.add_text(Stream::overlay,
                                     {cx + 2, cr.y, std::max(cw + 4, 1.0F), cr.h},
                                     {c.p.accent.r, c.p.accent.g, c.p.accent.b, 0.9F},
                                     cr.y + cr.h * 0.5F + c.ft.px * 0.30F, e.compose, 0);
                    fr.draw.add_shape(Stream::overlay,
                                      {cx + 2, cr.y + cr.h - 4, std::max(cw, 1.0F), 1},
                                      c.p.accent, 0, {}, 0, 0);
                }
                fr.draw.add_shape(Stream::overlay, cr, {0, 0, 0, 0}, 0, c.p.accent, 1.5F,
                                  0);
            }
        };

        // 列宽：拖分隔条只发 begin/move/end，宽度写 State.col_w
        struct ResizeFeature
        {
            bool begin(Context &c, const Target &t, float x)
            {
                const int col = t.col_index;
                const float base =
                    col >= 0 && static_cast<std::size_t>(col) < c.v.cols.w.size()
                        ? c.v.cols.w[static_cast<std::size_t>(col)]
                        : 0.0F;
                return begin(c, col, x, base);
            }
            bool begin(Context &c, int col, float x, float base)
            {
                c.v.cols.resize_col = col;
                c.v.cols.resize_x0 = x;
                c.v.cols.resize_base = base;
                c.v.touch();
                return true;
            }
            bool move(Context &c, float x)
            {
                auto &v = c.v;
                if (v.cols.resize_col >= 0 &&
                    static_cast<std::size_t>(v.cols.resize_col) < v.cols.w.size())
                {
                    const auto &def =
                        c.d.cols[static_cast<std::size_t>(v.cols.resize_col)];
                    v.cols.w[static_cast<std::size_t>(v.cols.resize_col)] =
                        std::clamp(v.cols.resize_base + (x - v.cols.resize_x0), def.min_w,
                                   def.max_w);
                    v.cols.user[static_cast<std::size_t>(v.cols.resize_col)] = 1;
                    v.touch();
                }
                return true;
            }
            bool end(Context &c)
            {
                c.v.cols.resize_col = -1;
                c.v.touch();
                return true;
            }
            template <class E>
            bool handle(Context &, const E &)
            {
                return false;
            }
            void rows(Context &, std::vector<row_idx> &) {}
            void paint(const PaintContext &, DrawList &) const {}
        };

        // 列序：拖列头只发 begin/move/end，顺序写 State.order
        struct ReorderFeature
        {
            bool begin(Context &c, const Target &t, float x)
            {
                return begin(c, t.col_index, x);
            }
            bool begin(Context &c, int col, float x)
            {
                auto &v = c.v;
                auto &ix = c.ix;
                v.cols.reorder_from = c.g.pos_of_col(col);
                v.cols.reorder_to = v.cols.reorder_from;
                v.cols.reorder_col = col;
                v.cols.reorder_x0 = x;
                ix.header_moved = false;
                v.touch();
                return true;
            }
            bool move(Context &c, float x)
            {
                auto &v = c.v;
                auto &ix = c.ix;
                if (v.cols.reorder_from >= 0)
                {
                    if (std::abs(x - v.cols.reorder_x0) > 4.0F)
                        ix.header_moved = true;
                    const int to = c.g.pos_at(x);
                    if (to >= 0)
                        v.cols.reorder_to = to;
                    v.touch();
                }
                return true;
            }
            int end(Context &c)
            {
                auto &v = c.v;
                auto &ix = c.ix;
                int clicked = -1;
                if (ix.header_moved)
                    v.cols.move(v.cols.reorder_from, v.cols.reorder_to);
                else
                    clicked = v.cols.reorder_col;
                v.cols.reorder_from = v.cols.reorder_to = -1;
                v.cols.reorder_col = -1;
                v.touch();
                return clicked;
            }
            template <class E>
            bool handle(Context &, const E &)
            {
                return false;
            }
            void rows(Context &, std::vector<row_idx> &) {}

            void paint(const PaintContext &c, DrawList &fr) const
            {
                if (c.ix.drag != DragMode::header || c.v.cols.reorder_from < 0)
                    return;
                const int from = c.v.cols.reorder_from;
                if (from >= static_cast<int>(c.v.cols.order.size()))
                    return;
                const int col = c.v.cols.order[static_cast<std::size_t>(from)];
                const int pos = c.g.pos_of_col(col);
                if (pos < 0)
                    return;
                const float gw = c.g.col_w[static_cast<std::size_t>(pos)];
                const float gx = c.ix.pointer.x - gw * 0.5F;
                const Rect ghost{gx, c.g.col_header.y, gw, c.g.col_header.h};
                fr.draw.add_shape(
                    Stream::overlay, ghost,
                    {c.p.header_bg.r, c.p.header_bg.g, c.p.header_bg.b, 0.85F},
                    c.p.cell_radius, c.p.accent, 1.5F, 0);
                if (col < c.d.cols_count())
                    fr.draw.add_text(
                        Stream::overlay,
                        {ghost.x + c.p.cell_pad, ghost.y, gw - 2 * c.p.cell_pad, ghost.h},
                        c.p.header_text, ghost.y + ghost.h * 0.5F + c.ft.px * 0.30F,
                        c.d.cols[static_cast<std::size_t>(col)].title, 0);
                if (c.v.cols.reorder_to >= 0)
                {
                    const int to = c.v.cols.reorder_to;
                    const float xt =
                        to < c.g.col_count()
                            ? c.g.col_x_at(to)
                            : c.g.col_x_at(c.g.col_count() - 1) + c.g.col_w.back();
                    fr.draw.add_shape(
                        Stream::overlay,
                        {xt - 1, c.g.col_header.y, 2, c.g.body.y1() - c.g.col_header.y},
                        c.p.accent, 0, {}, 0, 0);
                }
            }
        };

        // 冻结列：只写 State.frozen；几何在 sync 里消费
        struct FreezeFeature
        {
            bool set(Context &c, int n)
            {
                c.v.cols.frozen = n;
                c.v.touch();
                return true;
            }
            template <class E>
            bool handle(Context &, const E &)
            {
                return false;
            }
            void rows(Context &, std::vector<row_idx> &) {}
            void paint(const PaintContext &, DrawList &) const {}
        };

        // 行勾选：命令 + 沟槽绘制
        struct ChecksFeature
        {
            bool toggle(Context &c, const Target &t, bool shift)
            {
                return toggle(c, t.row_index, shift);
            }
            bool toggle(Context &c, int row, bool shift)
            {
                auto &v = c.v;
                if (row < 0 || static_cast<std::size_t>(row) >= v.checks.on.size())
                    return true;
                const bool on = v.checks.on[static_cast<std::size_t>(row)] == 0;
                if (shift && v.checks.last >= 0)
                {
                    // 区间按“可见顺序”走：排序/筛选后也只勾选当前看得到的行
                    const int pl = c.g.order_pos(v.checks.last);
                    const int pr = c.g.order_pos(row);
                    if (pl >= 0 && pr >= 0)
                    {
                        const int lo = std::min(pl, pr);
                        const int hi = std::max(pl, pr);
                        for (int i = lo; i <= hi; ++i)
                        {
                            const int r = c.g.view_rows[static_cast<std::size_t>(i)];
                            v.checks.on[static_cast<std::size_t>(r)] = on ? 1 : 0;
                            v.checks.ids[static_cast<std::size_t>(r)] =
                                c.d.ids[static_cast<std::size_t>(r)];
                        }
                    }
                    else
                    {
                        v.checks.on[static_cast<std::size_t>(row)] = on ? 1 : 0;
                        v.checks.ids[static_cast<std::size_t>(row)] =
                            c.d.ids[static_cast<std::size_t>(row)];
                    }
                }
                else
                {
                    v.checks.on[static_cast<std::size_t>(row)] = on ? 1 : 0;
                    v.checks.ids[static_cast<std::size_t>(row)] =
                        c.d.ids[static_cast<std::size_t>(row)];
                }
                v.checks.last = row;
                v.touch();
                return true;
            }
            bool toggle_all(Context &c)
            {
                auto &v = c.v;
                int on = 0;
                for (const int r : c.g.view_rows)
                    on += v.checks.on[static_cast<std::size_t>(r)] != 0;
                const bool all = !c.g.view_rows.empty() &&
                                 on == static_cast<int>(c.g.view_rows.size());
                for (const int r : c.g.view_rows)
                {
                    v.checks.on[static_cast<std::size_t>(r)] = all ? 0 : 1;
                    v.checks.ids[static_cast<std::size_t>(r)] =
                        c.d.ids[static_cast<std::size_t>(r)];
                }
                v.touch();
                return true;
            }
            template <class E>
            bool handle(Context &, const E &)
            {
                return false;
            }
            void rows(Context &, std::vector<row_idx> &) {}
            void paint(const PaintContext &c, DrawList &fr) const
            {
                if (c.g.gutter_w <= 0)
                    return;
                fr.draw.add_shape(Stream::chrome, c.g.gutter_head, c.p.header_bg, {},
                                  {1, 1, 1, 0.08F}, 1.0F, zone_key({Part::check_head}));
                int total = 0, on = 0;
                for (const int r : c.g.view_rows)
                {
                    ++total;
                    on += c.v.checks.on[static_cast<std::size_t>(r)] != 0;
                }
                const bool all = total > 0 && on == total;
                const bool Part = on > 0 && !all;
                const Rect hb{c.g.gutter_head.x + (c.g.gutter_head.w - 14) * 0.5F,
                              c.g.gutter_head.y + (c.g.gutter_head.h - 14) * 0.5F, 14,
                              14};
                if (all)
                    fr.draw.add_shape(Stream::chrome, hb, c.p.accent, 3, {}, 0,
                                      zone_key({Part::check_head}));
                else
                    fr.draw.add_shape(Stream::chrome, hb, {0, 0, 0, 0}, 3,
                                      {glm::vec3(c.p.accent), 0.55F}, 1.2F,
                                      zone_key({Part::check_head}));
                if (Part)
                    fr.draw.add_shape(Stream::chrome,
                                      {hb.x + 3, hb.y + hb.h * 0.5F - 1, hb.w - 6, 2},
                                      c.p.accent, 1, {}, 0, zone_key({Part::check_head}));

                for (int ri = 0; ri < c.g.row_count(); ++ri)
                {
                    const int dr = c.g.row_index[static_cast<std::size_t>(ri)];
                    const Rect gr{c.g.gutter_body.x,
                                  c.g.row_y[static_cast<std::size_t>(ri)], c.g.gutter_w,
                                  c.g.row_h[static_cast<std::size_t>(ri)]};
                    const Rect bx{gr.x + (gr.w - 14) * 0.5F, gr.y + (gr.h - 14) * 0.5F,
                                  14, 14};
                    const auto zk = zone_key({Part::check, dr, -1});
                    if (c.v.checks.on[static_cast<std::size_t>(dr)] != 0)
                        fr.draw.add_shape(Stream::chrome, bx, c.p.accent, 3, {}, 0, zk);
                    else
                        fr.draw.add_shape(Stream::chrome, bx, {0, 0, 0, 0}, 3,
                                          {glm::vec3(c.p.accent), 0.45F}, 1.2F, zk);
                }
            }
        };

        // 滚动：命令 + 双滚动条绘制
        struct ScrollFeature
        {
            bool begin(Context &c, int axis, float base, float pos)
            {
                c.v.scroll.axis = axis;
                c.v.scroll.base = base;
                c.v.scroll.pos0 = pos;
                c.v.touch();
                return true;
            }
            bool move(Context &c, float pos)
            {
                auto &v = c.v;
                if (v.scroll.axis == 0)
                {
                    const float max_sy = std::max(0.0F, c.g.content_h - c.g.body.h);
                    const float travel = std::max(c.g.vtrack.h - c.g.vthumb.h, 1.0F);
                    v.scroll.y = std::clamp(v.scroll.base +
                                                (pos - v.scroll.pos0) * (max_sy / travel),
                                            0.0F, max_sy);
                }
                else
                {
                    const float max_sx =
                        std::max(0.0F, c.g.content_w - c.g.frozen_w - c.g.scrollv.w);
                    const float travel = std::max(c.g.htrack.w - c.g.hthumb.w, 1.0F);
                    v.scroll.x = std::clamp(v.scroll.base +
                                                (pos - v.scroll.pos0) * (max_sx / travel),
                                            0.0F, max_sx);
                }
                v.touch();
                return true;
            }
            bool end(Context &c)
            {
                c.v.scroll.axis = 0;
                c.v.touch();
                return true;
            }
            template <class E>
            bool handle(Context &, const E &)
            {
                return false;
            }
            void rows(Context &, std::vector<row_idx> &) {}
            void paint(const PaintContext &c, DrawList &fr) const
            {
                if (c.g.vbar)
                {
                    fr.draw.add_shape(Stream::chrome, c.g.vtrack, c.p.bar_track,
                                      c.p.bar * 0.5F, {}, 0,
                                      zone_key({Part::vbar_track}));
                    const bool hot = c.ix.hover.kind == Part::vbar_thumb ||
                                     c.ix.drag == DragMode::vthumb;
                    fr.draw.add_shape(Stream::chrome, c.g.vthumb,
                                      hot ? c.p.bar_thumb_hot : c.p.bar_thumb,
                                      c.p.bar * 0.5F, {}, 0,
                                      zone_key({Part::vbar_thumb}));
                }
                if (c.g.hbar)
                {
                    fr.draw.add_shape(Stream::chrome, c.g.htrack, c.p.bar_track,
                                      c.p.bar * 0.5F, {}, 0,
                                      zone_key({Part::hbar_track}));
                    const bool hot = c.ix.hover.kind == Part::hbar_thumb ||
                                     c.ix.drag == DragMode::hthumb;
                    fr.draw.add_shape(Stream::chrome, c.g.hthumb,
                                      hot ? c.p.bar_thumb_hot : c.p.bar_thumb,
                                      c.p.bar * 0.5F, {}, 0,
                                      zone_key({Part::hbar_thumb}));
                }
            }
        };

        // 整表缩放：命令（无绘制）
        struct TableResizeFeature
        {
            bool begin(Context &c, float x, float y, float w, float h)
            {
                c.ix.trs_x0 = x;
                c.ix.trs_y0 = y;
                c.ix.trs_w = w;
                c.ix.trs_h = h;
                c.v.touch();
                return true;
            }
            bool move(Context &c, float x, float y)
            {
                auto &v = c.v;
                auto &ix = c.ix;
                // 左/上把手：往左/上拖是放大，所以位移取反
                const Part e = ix.trs_edge;
                const bool left =
                    e == Part::edge_w || e == Part::edge_nw || e == Part::edge_sw;
                const bool top =
                    e == Part::edge_n || e == Part::edge_ne || e == Part::edge_nw;
                const bool use_x = e != Part::edge_n;
                const bool use_y = e != Part::edge_w;
                float dx = x - ix.trs_x0;
                float dy = y - ix.trs_y0;
                if (left)
                    dx = -dx;
                if (top)
                    dy = -dy;
                v.cols.user_w = std::max(ix.trs_w + (use_x ? dx : 0.0F), c.p.min_w);
                v.cols.user_h = std::max(ix.trs_h + (use_y ? dy : 0.0F), c.p.min_h);
                v.touch();
                return true;
            }
            bool end(Context &c)
            {
                c.v.touch();
                return true;
            }
            template <class E>
            bool handle(Context &, const E &)
            {
                return false;
            }
            void rows(Context &, std::vector<row_idx> &) {}
            void paint(const PaintContext &, DrawList &) const {}
        };

        // 剪贴板 / 删除：读 selection 切片，写 Effect + undo
        struct ClipboardFeature
        {
            static void clear_sel(Context &c)
            {
                auto &v = c.v;
                if (v.sel.r0 < 0 || !c.fx)
                    return;
                std::vector<State::undo_change> batch;
                for (int r = std::min(v.sel.r0, v.sel.r1);
                     r <= std::max(v.sel.r0, v.sel.r1); ++r)
                    for (int cc = std::min(v.sel.c0, v.sel.c1);
                         cc <= std::max(v.sel.c0, v.sel.c1); ++cc)
                    {
                        const auto old = c.d.text(r, cc);
                        if (old.empty())
                            continue;
                        const auto id = c.d.ids[static_cast<std::size_t>(r)];
                        c.fx->push_back(SetCell{id, cc, {}});
                        batch.push_back({id, cc, std::string(old), {}});
                    }
                if (!batch.empty())
                {
                    v.history.undo.push_back(std::move(batch));
                    v.history.redo.clear();
                    v.touch();
                }
            }

            bool copy(Context &c, bool cut)
            {
                auto &v = c.v;
                if (v.sel.r0 < 0 || !c.fx)
                    return true;
                std::string tsv;
                for (int r = std::min(v.sel.r0, v.sel.r1);
                     r <= std::max(v.sel.r0, v.sel.r1); ++r)
                {
                    for (int cc = std::min(v.sel.c0, v.sel.c1);
                         cc <= std::max(v.sel.c0, v.sel.c1); ++cc)
                    {
                        if (cc > std::min(v.sel.c0, v.sel.c1))
                            tsv += '\t';
                        tsv += c.d.text(r, cc);
                    }
                    tsv += '\n';
                }
                c.fx->push_back(WriteClipboard{std::move(tsv)});
                if (cut)
                    clear_sel(c);
                return true;
            }
            bool remove(Context &c)
            {
                clear_sel(c);
                return true;
            }
            template <class E>
            bool handle(Context &, const E &)
            {
                return false;
            }
            void rows(Context &, std::vector<row_idx> &) {}
            void paint(const PaintContext &, DrawList &) const {}
        };

        // 撤销 / 重做：只消费 State.undo / State.redo
        struct UndoFeature
        {
            bool run(Context &c, bool redo)
            {
                auto &v = c.v;
                if (!c.fx)
                    return true;
                auto &from = redo ? v.history.redo : v.history.undo;
                auto &to = redo ? v.history.undo : v.history.redo;
                if (from.empty())
                    return true;
                auto batch = std::move(from.back());
                from.pop_back();
                for (const auto &ch : batch)
                    c.fx->push_back(
                        SetCell{ch.id, ch.col, redo ? ch.after : ch.before});
                to.push_back(std::move(batch));
                v.touch();
                return true;
            }
            template <class E>
            bool handle(Context &, const E &)
            {
                return false;
            }
            void rows(Context &, std::vector<row_idx> &) {}
            void paint(const PaintContext &, DrawList &) const {}
        };

        // 查找：Ctrl+F，输入，Enter 下一个，Esc 关闭
        struct FindFeature
        {
            bool toggle(Context &c)
            {
                auto &v = c.v;
                v.find.mode = !v.find.mode;
                v.find.buf.clear();
                v.find.hit = -1;
                v.touch();
                return true;
            }
            bool close(Context &c)
            {
                c.v.find.mode = false;
                c.v.touch();
                return true;
            }
            bool next(Context &c)
            {
                c.v.find.hit = next_hit(c.d, c.g, c.v);
                c.v.touch();
                return true;
            }
            bool text(Context &c, std::string_view s)
            {
                if (!c.v.find.mode)
                    return false;
                c.v.find.buf.append(s);
                c.v.find.compose.clear();
                c.v.find.hit = first_hit(c.d, c.g, c.v);
                if (c.v.find.hit >= 0)
                    ensure_visible(c.v, c.g, c.p, c.v.find.hit, 0);
                c.v.touch();
                return true;
            }
            static bool match(const Table &d, int r, int c,
                              const std::string &needle)
            {
                return !needle.empty() && FilterFeature::contains(d.text(r, c), needle);
            }
            static int first_hit(const Table &d, const Layout &g, const State &v)
            {
                for (const int r : g.view_rows)
                    for (int c = 0; c < d.cols_count(); ++c)
                        if (match(d, r, c, v.find.buf))
                            return r;
                return -1;
            }
            static int next_hit(const Table &d, const Layout &g, const State &v)
            {
                if (v.find.buf.empty())
                    return -1;
                bool after = v.find.hit < 0;
                for (const int r : g.view_rows)
                {
                    if (!after)
                    {
                        if (r == v.find.hit)
                            after = true;
                        continue;
                    }
                    for (int c = 0; c < d.cols_count(); ++c)
                        if (match(d, r, c, v.find.buf))
                            return r;
                }
                for (const int r : g.view_rows)
                {
                    for (int c = 0; c < d.cols_count(); ++c)
                        if (match(d, r, c, v.find.buf))
                            return r;
                }
                return -1;
            }

            template <class E>
            bool handle(Context &c, const E &x)
            {
                if constexpr (std::is_same_v<E, KeyPress>)
                    return key(c, x);
                else if constexpr (std::is_same_v<E, Press>)
                {
                    // 点别处 = 退出查找（让这次点击继续走正常的表内分派）
                    if (c.v.find.mode)
                    {
                        c.v.find.mode = false;
                        c.v.touch();
                    }
                }
                return false;
            }
            bool key(Context &c, const KeyPress &k)
            {
                auto &v = c.v;
                if (!v.find.mode)
                    return false;
                v.find.compose.clear();
                if (k.k == Key::esc)
                    v.find.mode = false;
                else if (k.k == Key::backspace && !v.find.buf.empty())
                {
                    const std::size_t q =
                        utf8_prev(std::string_view(v.find.buf), v.find.buf.size());
                    v.find.buf.erase(q);
                    v.find.hit = first_hit(c.d, c.g, v);
                    if (v.find.hit >= 0)
                        ensure_visible(v, c.g, c.p, v.find.hit, 0);
                }
                else if (k.k == Key::enter)
                {
                    v.find.hit = next_hit(c.d, c.g, v);
                    if (v.find.hit >= 0)
                    {
                        v.sel.r0 = v.sel.r1 = v.sel.cur_r = v.find.hit;
                        v.sel.c0 = v.sel.c1 = v.sel.cur_c = 0;
                        ensure_visible(v, c.g, c.p, v.find.hit, 0);
                        v.touch();
                    }
                }
                v.touch();
                return true;
            }
            void rows(Context &, std::vector<row_idx> &) {}
            void paint(const PaintContext &c, DrawList &fr) const
            {
                if (c.v.find.mode)
                {
                    const float fy =
                        c.g.box.y >= 32.0F ? c.g.box.y - 28.0F : c.g.box.y + 4.0F;
                    const Rect bar{c.g.box.x + 8, fy, c.g.box.w - 16, 22};
                    fr.draw.add_shape(Stream::overlay, bar, {0.06F, 0.08F, 0.12F, 0.95F},
                                      4, c.p.accent, 1.0F, 0);
                    const std::string shown = c.v.find.buf + c.v.find.compose;
                    fr.draw.add_text(Stream::overlay,
                                     {bar.x + 8, bar.y, bar.w - 16, bar.h}, c.p.text,
                                     bar.y + bar.h * 0.5F + c.ft.px * 0.30F,
                                     shown.empty() ? std::string_view{"Find..."}
                                                   : std::string_view{shown},
                                     0);
                }
                if (c.v.find.buf.empty())
                    return;
                for (int ri = 0; ri < c.g.row_count(); ++ri)
                {
                    const int dr = c.g.row_index[static_cast<std::size_t>(ri)];
                    for (int ci = 0; ci < c.g.col_count(); ++ci)
                    {
                        const int dc = c.g.col_index[static_cast<std::size_t>(ci)];
                        if (!match(c.d, dr, dc, c.v.find.buf))
                            continue;
                        Rect cr = clip_x({c.g.col_x_at(ci),
                                          c.g.row_y[static_cast<std::size_t>(ri)],
                                          c.g.col_w[static_cast<std::size_t>(ci)],
                                          c.g.row_h[static_cast<std::size_t>(ri)]},
                                         c.g.clip_x0(ci), c.g.clip_x1(ci));
                        cr = intersect(cr, c.g.body);
                        if (cr.empty())
                            continue;
                        const float a = dr == c.v.find.hit ? 0.32F : 0.16F;
                        fr.draw.add_shape(Stream::content, cr,
                                          {c.p.accent.r, c.p.accent.g, c.p.accent.b, a},
                                          0, {}, 0, 0);
                    }
                }
            }
        };

        // tooltip：悬停 0.6s 后显示 note（无 note 用文本）
        struct TooltipFeature
        {
            template <class E>
            bool handle(Context &c, const E &)
            {
                return false;
            }
            void rows(Context &, std::vector<row_idx> &) {}
            void paint(const PaintContext &c, DrawList &fr) const
            {
                if (c.ix.hover.kind != Part::cell || c.ix.now - c.v.tip.hover_t < 0.6)
                    return;
                if (c.ix.left || c.ix.drag != DragMode::none)
                    return;
                const int r = c.ix.hover.row_index;
                const int col = c.ix.hover.col_index;
                if (r < 0 || col < 0 || r >= c.d.rows() || col >= c.d.cols_count())
                    return;
                const auto &Cell = c.d.at(r, col);
                std::string_view text = Cell.note.empty() ? std::string_view{Cell.text}
                                                          : std::string_view{Cell.note};
                if (text.empty())
                    return;
                const float tw = text_width(c.ft, text);
                const float bw = std::min(tw + 16.0F, 360.0F);
                const float bh = c.ft.px + 12.0F;
                const float bx =
                    std::min(std::max(c.ix.pointer.x + 14.0F, c.g.box.x + 4),
                             c.g.box.x1() - bw - 4);
                const float by =
                    std::min(std::max(c.ix.pointer.y + 14.0F, c.g.box.y + 4),
                             c.g.box.y1() - bh - 4);
                const Rect box{bx, by, bw, bh};
                fr.draw.add_shape(Stream::overlay, box, {0.05F, 0.06F, 0.09F, 0.97F}, 4,
                                  {1, 1, 1, 0.15F}, 1.0F, 0);
                fr.draw.add_text(Stream::overlay, {box.x + 8, box.y, box.w - 16, box.h},
                                 c.p.text, box.y + box.h * 0.5F + c.ft.px * 0.30F, text,
                                 0);
            }
        };

        // 菜单布局：paint 与 hit_test 共用，避免两套几何
        inline constexpr float menu_w = 150.0F;
        inline constexpr float menu_item_h = 24.0F;
        inline constexpr int menu_item_count = 4;
        [[nodiscard]] inline Rect menu_box_rect(const State &v) noexcept
        {
            return {v.menu.x, v.menu.y, menu_w,
                    menu_item_h * static_cast<float>(menu_item_count) + 8.0F};
        }
        [[nodiscard]] inline Rect menu_item_rect(const State &v, int i) noexcept
        {
            return {v.menu.x + 4.0F,
                    v.menu.y + 4.0F + menu_item_h * static_cast<float>(i), menu_w - 8.0F,
                    menu_item_h};
        }
        // 列菜单：Fit / Reset sort / Hide / Show all
        struct MenuFeature
        {
            static constexpr const char *kItems[4] = {"Fit Column", "Reset sort",
                                                      "Hide Column", "Show all"};

            bool open(Context &c, int col, float x, float y)
            {
                if (col < 0)
                    return false;
                auto &v = c.v;
                v.menu.open = true;
                v.menu.col = col;
                v.menu.x =
                    std::min(std::max(x, c.g.box.x + 4.0F), c.g.box.x1() - menu_w - 4.0F);
                v.menu.y = std::min(
                    std::max(y, c.g.box.y + 4.0F),
                    c.g.box.y1() - menu_item_h * static_cast<float>(menu_item_count) -
                        12.0F);
                v.menu.hover = -1;
                v.touch();
                return true;
            }

            bool item(Context &c, const Target &t)
            {
                return item(c, t.col_index);
            }
            bool item(Context &c, int index)
            {
                auto &v = c.v;
                if (v.menu.open && index >= 0)
                {
                    switch (index)
                    {
                    case 0: {
                        autofit_column(c, v.menu.col);
                        break;
                    }
                    case 1:
                        reset_sort(v);
                        break;
                    case 2: {
                        hide_column(c, v.menu.col);
                        break;
                    }
                    case 3:
                        show_all_columns(c);
                        break;
                    default:
                        break;
                    }
                }
                v.menu.open = false;
                v.touch();
                return true;
            }

            template <class E>
            bool handle(Context &c, const E &x)
            {
                auto &v = c.v;
                if constexpr (std::is_same_v<E, Press>)
                {
                    if (!v.menu.open)
                        return false;
                    if (x.z.kind != Part::menu_item)
                    {
                        v.menu.open = false;
                        v.touch();
                    }
                    return false;
                }
                else if constexpr (std::is_same_v<E, KeyPress>)
                {
                    if (!v.menu.open)
                        return false;
                    if (x.k == Key::esc)
                    {
                        v.menu.open = false;
                        v.touch();
                    }
                    return true;
                }
                return false;
            }
            void rows(Context &, std::vector<row_idx> &) {}
            void paint(const PaintContext &c, DrawList &fr) const
            {
                if (!c.v.menu.open)
                    return;
                const Rect box = menu_box_rect(c.v);
                fr.draw.add_shape(Stream::overlay, box, {0.07F, 0.08F, 0.12F, 0.98F}, 6,
                                  {1, 1, 1, 0.18F}, 1.0F, 0);
                for (int i = 0; i < menu_item_count; ++i)
                {
                    const Rect it = menu_item_rect(c.v, i);
                    if (c.v.menu.hover == i)
                        fr.draw.add_shape(Stream::overlay, it, {1, 1, 1, 0.06F}, 4, {}, 0,
                                          0);
                    fr.draw.add_text(Stream::overlay, {it.x + 10, it.y, it.w - 20, it.h},
                                     c.p.text, it.y + it.h * 0.5F + c.ft.px * 0.30F,
                                     kItems[i], 0);
                    fr.zones.push_back({it, Zone{Part::menu_item, -1, i},
                                        static_cast<std::uint16_t>(20 + i)});
                }
            }
        };

        // ═════════════════════════════════════════════════════════════════
        // AppRouter：feature 的有序组合（CommandRouter）
        //
        //   顺序 = 行为（F7 不动点），与 ability11 的 systems::handle 一致：
        //     filter → find → sort → resize → reorder → freeze → checks → scroll
        //            → table_resize → selection → edit → clipboard → undo → menu → tooltip
        //   行 stage：filter → sort（layout_passes 调用）
        //   绘制 stage：同序（paint_passes 调用）
        //   text 路径：filter → find → edit（char 不走 gesture）
        // ═════════════════════════════════════════════════════════════════
        struct AppRouter
        {
            std::tuple<FilterFeature, FindFeature, SortFeature, ResizeFeature,
                       ReorderFeature, FreezeFeature, ChecksFeature, ScrollFeature,
                       TableResizeFeature, SelectionFeature, EditFeature,
                       ClipboardFeature, UndoFeature, MenuFeature, TooltipFeature>
                features;

            template <class F>
            F &get() noexcept
            {
                return std::get<F>(features);
            }
            template <class F>
            const F &get() const noexcept
            {
                return std::get<F>(features);
            }
            // 短路：第一个认领的 feature 生效
            template <class E>
            bool handle(Context &c, const E &cmd)
            {
                bool handled = false;
                std::apply(
                    [&](auto &...f) {
                        std::initializer_list<int>{
                            (handled ? 0 : (handled = f.handle(c, cmd), 0))...};
                    },
                    features);
                return handled;
            }
            void rows(Context &c, std::vector<row_idx> &r)
            {
                get<FilterFeature>().rows(c, r);
                get<SortFeature>().rows(c, r);
            }
            void paint(const PaintContext &c, DrawList &fr) const
            {
                std::apply([&](const auto &...f) { (f.paint(c, fr), ...); }, features);
            }
            // char 路径：按当前焦点分发给 filter / find / edit
            bool text(Context &c, std::string_view s)
            {
                if (c.v.filter.edit >= 0)
                    return get<FilterFeature>().text(c, s);
                if (c.v.find.mode)
                    return get<FindFeature>().text(c, s);
                return get<EditFeature>().text(c, s);
            }
        };
} // namespace table::detail
