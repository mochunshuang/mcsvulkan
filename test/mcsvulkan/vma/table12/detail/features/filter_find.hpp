#pragma once
// ============================================================================
// table12/detail/features/filter_find.hpp — FilterFeature（列过滤）/ FindFeature（查找）
//
//   纯移动自 detail/features.hpp（G3）；不改行为、不改顺序。
//   依赖：../common.hpp。
// ============================================================================

#include "../common.hpp"

namespace table::detail
{
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
            Outcome handle(Context &c, const E &x)
            {
                if constexpr (std::is_same_v<E, Press>)
                return command::handled(press(c, x));
                else if constexpr (std::is_same_v<E, KeyPress>)
                return command::handled(key(c, x));
                return command::handled({});
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
            Outcome handle(Context &c, const E &x)
            {
                if constexpr (std::is_same_v<E, KeyPress>)
                return command::handled(key(c, x));
                else if constexpr (std::is_same_v<E, Press>)
                {
                    // 点别处 = 退出查找（让这次点击继续走正常的表内分派）
                    if (c.v.find.mode)
                    {
                        c.v.find.mode = false;
                        c.v.touch();
                    }
                }
                return command::handled({});
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
} // namespace table::detail
