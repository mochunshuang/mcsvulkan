#pragma once
// ============================================================================
// table12/detail/layout_passes.hpp — LayoutComposer：把 Layout 拆成 pass
//
//   每个 pass 只做一件事、只写 Layout（visible 会把 scroll 钳制回写 State）；
//   pass 之间不互相调用，顺序只写在 kLayoutPasses 里：
//
//     reconcile → frame → columns → metrics → rows → frozen → visible → bars → edges
//
//   局部量（bar / max_sy / max_sx）在需要的 pass 里就地重算：它们是 Theme+Layout
//   的纯函数，重算比让 pass 之间共享隐式状态更安全。
//
//   solve_columns / ensure_metrics 是列宽与行高的纯算法（不写 State，只写 Layout）。
//   derive_* 是“语义选择”的小工具（goto / move / begin_edit / in_sel），只写 State。
//
//   依赖：features（AppRouter）。只被 session 使用。
// ============================================================================

#include "features.hpp"

namespace table::detail
{

// ── 列宽：可见列序 + 固定/权重宽度分配（纯函数） ─────────────────────

        inline void solve_columns(const Table &d, const State &v, const Theme &p,
                                  float avail, Layout &g)
        {
            std::vector<int> vis;
            vis.reserve(v.cols.order.size());
            for (const int dc : v.cols.order)
            {
                if (dc < 0 || dc >= d.cols_count())
                    continue;
                if (static_cast<std::size_t>(dc) < v.cols.hidden.size() &&
                    v.cols.hidden[static_cast<std::size_t>(dc)] != 0)
                    continue;
                vis.push_back(dc);
            }
            const int n = static_cast<int>(vis.size());
            g.col_index.resize(static_cast<std::size_t>(n));
            g.col_x.resize(static_cast<std::size_t>(n));
            g.col_w.resize(static_cast<std::size_t>(n));
            float fixed = 0, weight = 0;
            for (int i = 0; i < n; ++i)
            {
                const int dc = vis[static_cast<std::size_t>(i)];
                g.col_index[static_cast<std::size_t>(i)] = dc;
                const auto &def = d.cols[static_cast<std::size_t>(dc)];
                const bool user = v.cols.user[static_cast<std::size_t>(dc)] != 0;
                const float cw =
                    std::clamp(user ? v.cols.w[static_cast<std::size_t>(dc)] : def.w,
                               def.min_w, def.max_w);
                g.col_w[static_cast<std::size_t>(i)] = cw;
                if (!user && def.weight > 0)
                    weight += def.weight;
                else
                    fixed += cw;
            }
            const float rest = avail - fixed;
            if (weight > 0 && rest > 0)
                for (int i = 0; i < n; ++i)
                {
                    const int dc = g.col_index[static_cast<std::size_t>(i)];
                    const auto &def = d.cols[static_cast<std::size_t>(dc)];
                    if (v.cols.user[static_cast<std::size_t>(dc)] != 0 || def.weight <= 0)
                        continue;
                    g.col_w[static_cast<std::size_t>(i)] =
                        std::clamp(rest * (def.weight / weight), def.min_w, def.max_w);
                }
            float x = 0;
            for (int i = 0; i < n; ++i)
            {
                g.col_x[static_cast<std::size_t>(i)] = x;
                x += g.col_w[static_cast<std::size_t>(i)];
            }
        }

// ── 行高：wrap 文本按行数算高度（revision 键失效才重算） ──────────────

        inline void ensure_metrics(const Table &d, const State &v, const Theme &p,
                                   Layout &g, const Font &ft)
        {
            float key = static_cast<float>(d.rows()) * 7.0F +
                        static_cast<float>(d.cols_count()) * 131.0F + p.row_h * 17.0F +
                        (p.wrap ? 977.0F : 0.0F);
            for (const float w : v.cols.w)
                key += w * 0.013F;
            if (key == g.row_h_key &&
                g.row_h_data.size() == static_cast<std::size_t>(d.rows()))
                return;
            g.row_h_key = key;
            g.row_h_data.assign(static_cast<std::size_t>(d.rows()), p.row_h);
            bool any_wrap = p.wrap;
            for (const auto &c : d.cols)
                any_wrap = any_wrap || c.wrap;
            if (!any_wrap)
                return;
            std::vector<std::string_view> lines;
            for (int r = 0; r < d.rows(); ++r)
            {
                float h = p.row_h;
                for (int c = 0; c < d.cols_count(); ++c)
                {
                    if (!d.cols[static_cast<std::size_t>(c)].wrap && !p.wrap)
                        continue;
                    const int pos = g.pos_of_col(c);
                    const float cw =
                        pos >= 0 ? g.col_w[static_cast<std::size_t>(pos)] : 100.0F;
                    wrap_lines(ft, d.text(r, c), std::max(cw - 2 * p.cell_pad, 4.0F),
                               lines);
                    h = std::max(
                        h, static_cast<float>(lines.size()) * (ft.px * ft.line_h) + 8.0F);
                }
                g.row_h_data[static_cast<std::size_t>(r)] = h;
            }
        }

// ── LayoutContext：layout pass 的唯一上下文 ───────────────────────────
struct LayoutContext
{
    const Table &model;
    State &state;
    Interaction &interaction;
    const Theme &theme;
    const Rect &viewport;
    const Font &font;
    AppRouter &sy;
    RowPipeline &pipe;
    Layout &layout;
};

// ── pass_reconcile：State 与模型对齐：列/行增删后只补差量，不整份 reset

inline void pass_reconcile(LayoutContext &c)
{
            if (c.state.cols.order.size() != static_cast<std::size_t>(c.model.cols_count()) ||
                c.state.checks.on.size() != static_cast<std::size_t>(c.model.rows()))
                c.state.reconcile(c.model);
            c.layout = Layout{};
            c.layout.version = c.state.revision;
}

// ── pass_frame：外框：box/header/body/group/col_header/gutter（bar 由 Theme 决定）

inline void pass_frame(LayoutContext &c)
{
            float w = c.state.cols.user_w > 0 ? c.state.cols.user_w : c.viewport.w;
            float h = c.state.cols.user_h > 0 ? c.state.cols.user_h : c.viewport.h;
            const float maxw = std::max(c.theme.min_w, std::min(c.theme.max_w, c.viewport.w));
            const float maxh = std::max(c.theme.min_h, std::min(c.theme.max_h, c.viewport.h));
            w = std::clamp(w, c.theme.min_w, maxw);
            h = std::clamp(h, c.theme.min_h, maxh);
            c.layout.box = {c.viewport.x, c.viewport.y, w, h};
            c.layout.step = c.theme.row_h + c.theme.row_gap;

            const float bar = std::max(c.theme.bar, 0.0F);
            c.layout.group_h = c.model.any_group() ? std::max(c.theme.group_h, 0.0F) : 0.0F;
            const float header_h = c.layout.group_h + c.theme.header_h;
            c.layout.header = {c.layout.box.x + 1, c.layout.box.y + 1, c.layout.box.w - 2 - bar, header_h};
            c.layout.body = {c.layout.header.x, c.layout.header.y + header_h, c.layout.header.w,
                      c.layout.box.h - 2 - header_h - bar};
            if (c.layout.body.h < 0)
                c.layout.body.h = 0;
            c.layout.group = {c.layout.header.x, c.layout.header.y, c.layout.header.w, c.layout.group_h};
            c.layout.col_header = {c.layout.header.x, c.layout.header.y + c.layout.group_h, c.layout.header.w, c.theme.header_h};

            c.layout.gutter_w = c.theme.row_select ? std::max(c.theme.select_w, 0.0F) : 0.0F;
            if (c.layout.gutter_w > c.layout.body.w * 0.5F)
                c.layout.gutter_w = 0;
            c.layout.gutter_head = {c.layout.col_header.x, c.layout.col_header.y, c.layout.gutter_w, c.layout.col_header.h};
            c.layout.gutter_body = {c.layout.body.x, c.layout.body.y, c.layout.gutter_w, c.layout.body.h};
}

// ── pass_columns：列：可见列序 + 宽度（solve_columns）+ 分组带 + content_w

inline void pass_columns(LayoutContext &c)
{
            solve_columns(c.model, c.state, c.theme, std::max(c.layout.body.w - c.layout.gutter_w, 1.0F), c.layout);
            // 分组带：连续、同名 group 的可见列算一段；命中/整组选择都读这张表
            c.layout.groups.clear();
            {
                int start = -1;
                std::string_view cur;
                const auto flush = [&](int end) {
                    if (start >= 0 && end >= start)
                        c.layout.groups.push_back({start, end,
                                            c.layout.col_index[static_cast<std::size_t>(start)],
                                            c.layout.col_index[static_cast<std::size_t>(end)]});
                };
                for (int i = 0; i < c.layout.col_count(); ++i)
                {
                    const auto &name =
                        c.model.cols[static_cast<std::size_t>(
                                   c.layout.col_index[static_cast<std::size_t>(i)])]
                            .group;
                    if (name.empty())
                    {
                        flush(i - 1);
                        start = -1;
                        cur = {};
                        continue;
                    }
                    if (start < 0 || name != cur)
                    {
                        flush(i - 1);
                        start = i;
                        cur = name;
                    }
                }
                flush(c.layout.col_count() - 1);
            }
            c.layout.content_w = c.layout.col_count() > 0
                              ? c.layout.col_x.back() + c.layout.col_w.back() - c.layout.col_x.front()
                              : 0.0F;
}

// ── pass_metrics：行高缓存：wrap 时按文本行数算 row_h_data（revision 键）

inline void pass_metrics(LayoutContext &c)
{
            ensure_metrics(c.model, c.state, c.theme, c.layout, c.font);
}

// ── pass_rows：行 stage：filter → sort 得到 view_rows/row_off/content_h

inline void pass_rows(LayoutContext &c)
{
            c.pipe.base(c.model);
            {
                Context ctx{c.model, c.state, c.interaction, c.theme, c.layout, c.font, false, {}};
                c.sy.rows(ctx, c.pipe.buf);
            }
            c.layout.view_rows.assign(c.pipe.buf.begin(), c.pipe.buf.end());
            // 当前格被筛掉：清选区（ability7 语义），避免编辑/勾选落到不可见行
            if (c.state.sel.cur_r >= 0 && std::find(c.layout.view_rows.begin(), c.layout.view_rows.end(),
                                              c.state.sel.cur_r) == c.layout.view_rows.end())
            {
                c.state.sel = {};
                c.state.touch();
            }
            c.layout.row_off.assign(c.layout.view_rows.size() + 1, 0.0F);
            float acc = 0;
            for (std::size_t i = 0; i < c.layout.view_rows.size(); ++i)
            {
                c.layout.row_off[i] = acc;
                acc += c.layout.row_h_data[static_cast<std::size_t>(c.layout.view_rows[i])] + c.theme.row_gap;
            }
            c.layout.row_off.back() = acc;
            c.layout.content_h = c.layout.view_rows.empty() ? 0.0F : acc - c.theme.row_gap;
}

// ── pass_frozen：冻结列：frozen_view / scrollv（水平滚动只在 scrollv 里）

inline void pass_frozen(LayoutContext &c)
{
            const float bx = c.layout.body.x + c.layout.gutter_w;
            const float bw = std::max(c.layout.body.w - c.layout.gutter_w, 0.0F);
            c.layout.frozen = std::clamp(c.state.cols.frozen >= 0 ? c.state.cols.frozen : c.theme.frozen, 0,
                                  c.layout.col_count());
            if (c.layout.frozen > 0)
            {
                c.layout.frozen_w = std::min(
                    c.layout.col_x[static_cast<std::size_t>(c.layout.frozen - 1)] +
                        c.layout.col_w[static_cast<std::size_t>(c.layout.frozen - 1)] - c.layout.col_x.front(),
                    bw * 0.8F);
                c.layout.frozen_view = {bx, c.layout.body.y, c.layout.frozen_w, c.layout.body.h};
                c.layout.scrollv = {bx + c.layout.frozen_w, c.layout.body.y, std::max(bw - c.layout.frozen_w, 0.0F),
                             c.layout.body.h};
            }
            else
            {
                c.layout.frozen_w = 0;
                c.layout.frozen_view = {bx, c.layout.body.y, 0, c.layout.body.h};
                c.layout.scrollv = {bx, c.layout.body.y, bw, c.layout.body.h};
            }
}

// ── pass_visible：滚动量钳制（回写 State）+ 可见行：row_index/row_y/row_h

inline void pass_visible(LayoutContext &c)
{
    const float bar = std::max(c.theme.bar, 0.0F);
            c.layout.vbar = bar > 0 && c.layout.content_h > c.layout.body.h + 0.5F;
            c.layout.hbar = bar > 0 && c.layout.content_w - c.layout.frozen_w > c.layout.scrollv.w + 0.5F;
            const float max_sy = std::max(0.0F, c.layout.content_h - c.layout.body.h);
            const float max_sx = std::max(0.0F, c.layout.content_w - c.layout.frozen_w - c.layout.scrollv.w);
            c.state.scroll.y = std::clamp(c.state.scroll.y, 0.0F, max_sy);
            c.state.scroll.x = std::clamp(c.state.scroll.x, 0.0F, max_sx);
            c.layout.sx = c.state.scroll.x;
            c.layout.sy = c.state.scroll.y;

            if (!c.layout.view_rows.empty() && c.layout.body.h > 0)
            {
                const auto it =
                    std::upper_bound(c.layout.row_off.begin(), c.layout.row_off.end(), c.layout.sy);
                int first = std::clamp(static_cast<int>(it - c.layout.row_off.begin()) - 1, 0,
                                       static_cast<int>(c.layout.view_rows.size()) - 1);
                float y = c.layout.body.y + c.layout.row_off[static_cast<std::size_t>(first)] - c.layout.sy;
                for (int i = first; i < static_cast<int>(c.layout.view_rows.size()); ++i)
                {
                    const int dr = c.layout.view_rows[static_cast<std::size_t>(i)];
                    const float rh = c.layout.row_h_data[static_cast<std::size_t>(dr)];
                    if (y + rh > c.layout.body.y && y < c.layout.body.y1())
                    {
                        const float y0 = std::max(y, c.layout.body.y);
                        const float y1 = std::min(y + rh, c.layout.body.y1());
                        if (y1 - y0 > 0.5F)
                        {
                            c.layout.row_index.push_back(dr);
                            c.layout.row_y.push_back(y0);
                            c.layout.row_h.push_back(y1 - y0);
                        }
                    }
                    y += rh + c.theme.row_gap;
                    if (y >= c.layout.body.y1())
                        break;
                }
            }
}

// ── pass_bars：滚动条轨道/滑块 + 角落（只算矩形，不画）

inline void pass_bars(LayoutContext &c)
{
    // bar / max_sy / max_sx 是 Theme+几何的纯函数：就地重算，不跨 pass 共享状态
    const float bar = std::max(c.theme.bar, 0.0F);
    const float max_sy = std::max(0.0F, c.layout.content_h - c.layout.body.h);
    const float max_sx =
        std::max(0.0F, c.layout.content_w - c.layout.frozen_w - c.layout.scrollv.w);
            if (bar > 0)
            {
                const float tx = c.layout.box.x1() - bar - 1;
                const float ty = c.layout.body.y + 2;
                const float th = std::max(c.layout.body.h - 4, 1.0F);
                c.layout.vtrack = {tx, ty, bar, th};
                if (c.layout.vbar)
                {
                    const float ratio =
                        std::clamp(c.layout.body.h / std::max(c.layout.content_h, 1.0F), 0.06F, 1.0F);
                    const float hgt = std::max(th * ratio, 18.0F);
                    const float t = max_sy > 0 ? c.layout.sy / max_sy : 0.0F;
                    c.layout.vthumb = {tx, ty + (th - hgt) * t, bar, hgt};
                }
                const float hx = c.layout.body.x + 2;
                const float hy = c.layout.box.y1() - bar - 1;
                const float hw = std::max(c.layout.body.w - 4, 1.0F);
                c.layout.htrack = {hx, hy, hw, bar};
                if (c.layout.hbar)
                {
                    const float ratio =
                        std::clamp(c.layout.scrollv.w / std::max(c.layout.content_w - c.layout.frozen_w, 1.0F),
                                   0.06F, 1.0F);
                    const float wid = std::max(hw * ratio, 18.0F);
                    const float t = max_sx > 0 ? c.layout.sx / max_sx : 0.0F;
                    c.layout.hthumb = {hx + (hw - wid) * t, hy, wid, bar};
                }
                c.layout.corner = {c.layout.htrack.x1(), c.layout.vtrack.y, bar, bar};
            }
}

// ── pass_edges：边缘把手：n/w/ne/nw/se/sw + 形状断言（F3 坐标空间）

inline void pass_edges(LayoutContext &c)
{
    const float bar = std::max(c.theme.bar, 0.0F);
            const float e = c.theme.edge_hit;
            const float C = std::max(bar + 2.0F, e + 4.0F);
            const auto &fr = c.layout.box;
            c.layout.edge_n = {fr.x + C, fr.y - e, std::max(fr.w - 2 * C, 1.0F), 2 * e};
            float wh = fr.h - 2 * C;
            if (c.layout.hbar)
                wh = std::min(wh, c.layout.htrack.y - (fr.y + C));
            c.layout.edge_w = {fr.x - e, fr.y + C, 2 * e, std::max(wh, 1.0F)};
            c.layout.edge_nw = {fr.x - e, fr.y - e, C, C};
            c.layout.edge_ne = {fr.x1() - C, fr.y - e, C, C};
            c.layout.edge_sw = {fr.x - e, fr.y1() - C, C, C};
            c.layout.edge_se = {fr.x1() - C, fr.y1() - C, C, C};

            assert(c.layout.header.x == c.layout.body.x && c.layout.header.w == c.layout.body.w);
            assert(c.layout.col_header.h == c.theme.header_h);
            assert(c.layout.header.h == c.layout.group_h + c.theme.header_h);
}

// 编译期 pass 列表：每个元素是独立 lambda 类型（无函数指针、无运行时 type-erasure）
inline constexpr auto kLayoutPasses = std::tuple{
    [](LayoutContext &c) { pass_reconcile(c); },
    [](LayoutContext &c) { pass_frame(c); },
    [](LayoutContext &c) { pass_columns(c); },
    [](LayoutContext &c) { pass_metrics(c); },
    [](LayoutContext &c) { pass_rows(c); },
    [](LayoutContext &c) { pass_frozen(c); },
    [](LayoutContext &c) { pass_visible(c); },
    [](LayoutContext &c) { pass_bars(c); },
    [](LayoutContext &c) { pass_edges(c); },
};

// ── 唯一入口：layout passes 的有序组合 ────────────────────────────────
inline void compose_layout(const Table &d, State &v, Interaction &ix, const Theme &p,
                           Layout &g, const Rect &viewport, const Font &ft,
                           AppRouter &sy, RowPipeline &pipe)
{
    LayoutContext c{d, v, ix, p, viewport, ft, sy, pipe, g};
    std::apply([&](auto... pass) { (pass(c), ...); }, kLayoutPasses);
}

// ── 语义选择工具：只写 State，不碰 Model ─────────────────────────────

        inline bool in_sel(const State &v, int r, int c) noexcept
        {
            return v.sel.r0 >= 0 && v.sel.c0 >= 0 && r >= std::min(v.sel.r0, v.sel.r1) &&
                   r <= std::max(v.sel.r0, v.sel.r1) &&
                   c >= std::min(v.sel.c0, v.sel.c1) && c <= std::max(v.sel.c0, v.sel.c1);
        }

        inline void select_anchor(State &v, int r, int c)
        {
            v.sel.r0 = v.sel.r1 = v.sel.cur_r = r;
            v.sel.c0 = v.sel.c1 = v.sel.cur_c = c;
            v.touch();
        }

        inline int view_pos_of(const Layout &g, int data_row)
        {
            for (int i = 0; i < static_cast<int>(g.view_rows.size()); ++i)
                if (g.view_rows[static_cast<std::size_t>(i)] == data_row)
                    return i;
            return 0;
        }

        inline void goto_cell(State &v, int r, int c, bool extend)
        {
            if (r < 0 || c < 0)
                return;
            if (!extend)
                select_anchor(v, r, c);
            else
            {
                v.sel.r1 = r;
                v.sel.c1 = c;
                v.sel.cur_r = r;
                v.sel.cur_c = c;
                v.touch();
            }
        }

        inline void move_sel(State &v, const Layout &g, int dr, int dc, bool extend)
        {
            if (v.sel.cur_r < 0)
            {
                if (!g.view_rows.empty())
                    select_anchor(v, g.view_rows.front(), 0);
                return;
            }
            if (dc != 0)
            {
                goto_cell(v, v.sel.cur_r, std::max(0, v.sel.cur_c + dc), extend);
                return;
            }
            const int pos = view_pos_of(g, v.sel.cur_r);
            const int np =
                std::clamp(pos + dr, 0, static_cast<int>(g.view_rows.size()) - 1);
            goto_cell(v, g.view_rows[static_cast<std::size_t>(np)], v.sel.cur_c, extend);
        }

        inline void begin_edit(const Table &d, State &v, int r, int c)
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
        }

} // namespace table::detail
