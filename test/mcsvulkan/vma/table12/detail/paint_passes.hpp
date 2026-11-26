#pragma once
// ============================================================================
// table12/detail/paint_passes.hpp — PaintComposer：把 DrawList 拆成 pass
//
//   每个 pass 只做一件事、只写 DrawList；pass 之间不互相调用，
//   顺序只写在 kPaintPasses 里（顺序 = 行为，golden DL / PICKCHECK 锁住）：
//
//     visual → frame → group_bands → header → rows → cells → separators
//            → zones → features → owners
//
//   zone 与 hit 用同一套几何：DrawList 的 Proxy stream 就是拾取代理，
//   所以 draw 和 picking 只在 TargetKey 上合并。
//
//   依赖：features（AppRouter）。只被 session 使用。
// ============================================================================

#include "features.hpp"

namespace table::detail
{
// zone_key 的短名：每个 pass 都要给 op 打 TargetKey
[[nodiscard]] inline std::uint32_t zk(const Zone &z) noexcept
{
    return zone_key(z);
}

// ── PaintContext：paint pass 的唯一上下文（只读 + DrawList 出口） ──────
struct DrawContext
{
    const Table &d;
    const State &v;
    const Interaction &ix;
    const Theme &p;
    const Layout &g;
    const Font &ft;
    const AppRouter &sy;
    DrawList &out;
    TableId tid{};
    std::string scratch; // fit_text 的临时缓冲（每个 pass 共用，避免分配）
};

// ── pass_visual：每格的可视标志（编辑中）：只写 out.visual，不产生 op

inline void pass_visual(DrawContext &c)
{
            c.out.visual.assign(static_cast<std::size_t>(c.g.row_count()) *
                                  static_cast<std::size_t>(c.g.col_count()),
                              0);
            for (int ri = 0; ri < c.g.row_count(); ++ri)
            {
                const int dr = c.g.row_index[static_cast<std::size_t>(ri)];
                for (int ci = 0; ci < c.g.col_count(); ++ci)
                {
                    const int dc = c.g.col_index[static_cast<std::size_t>(ci)];
                    std::uint8_t vis = 0;
                    if (c.v.edit.active() && c.v.edit.row == dr && c.v.edit.col == dc)
                        vis |= vis_editing;
                    c.out.visual[static_cast<std::size_t>(ri) *
                                   static_cast<std::size_t>(c.g.col_count()) +
                               static_cast<std::size_t>(ci)] = vis;
                }
            }
}

// ── pass_frame：外框：一张表的底与描边

inline void pass_frame(DrawContext &c)
{
    auto &dlist = c.out.draw;
            // 外框
            dlist.add_shape(Stream::chrome, c.g.box, c.p.bg, c.p.radius, {1, 1, 1, 0.10F}, 1.0F,
                         zk({Part::frame}));
}

// ── pass_group_bands：分组带：连续同名 group 的列，盖一层浅底 + 居中文案

inline void pass_group_bands(DrawContext &c)
{
    auto &dlist = c.out.draw;
            // 分组行
            if (c.g.group_h > 0)
            {
                int i = 0;
                while (i < c.g.col_count())
                {
                    const auto &def = c.d.cols[static_cast<std::size_t>(
                        c.g.col_index[static_cast<std::size_t>(i)])];
                    if (def.group.empty())
                    {
                        ++i;
                        continue;
                    }
                    int j = i;
                    while (j + 1 < c.g.col_count() &&
                           c.d.cols[static_cast<std::size_t>(
                                      c.g.col_index[static_cast<std::size_t>(j + 1)])]
                                   .group == def.group)
                        ++j;
                    for (int k = i; k <= j; ++k)
                    {
                        const Rect gr =
                            clip_x({c.g.col_x_at(k), c.g.group.y,
                                    c.g.col_w[static_cast<std::size_t>(k)], c.g.group.h},
                                   c.g.clip_x0(k), c.g.clip_x1(k));
                        if (gr.empty())
                            continue;
                        dlist.add_shape(Stream::chrome, gr,
                                     {glm::vec3(c.p.header_bg) * 0.84F, c.p.header_bg.a}, 0,
                                     {}, 0,
                                     zk({Part::header_group, -1,
                                         c.g.col_index[static_cast<std::size_t>(i)]}));
                    }
                    const bool froz = i < c.g.frozen;
                    const float base = c.g.body.x + c.g.gutter_w - (froz ? 0.0F : c.g.sx);
                    const float x0 = base + c.g.col_x[static_cast<std::size_t>(i)];
                    const float x1 = base + c.g.col_x[static_cast<std::size_t>(j)] +
                                     c.g.col_w[static_cast<std::size_t>(j)];
                    const Rect band =
                        froz
                            ? Rect{c.g.frozen_view.x, c.g.group.y, c.g.frozen_view.w, c.g.group.h}
                            : Rect{c.g.scrollv.x, c.g.group.y, c.g.scrollv.w, c.g.group.h};
                    const Rect tr = intersect({x0, c.g.group.y, x1 - x0, c.g.group.h}, band);
                    if (tr.w > 2)
                    {
                        const auto s = fit_text(c.ft, def.group, std::max(tr.w - 12, 1.0F),
                                                dlist.ar, c.scratch);
                        const float tw = text_width(c.ft, s);
                        dlist.add_text(Stream::chrome,
                                    {tr.x + std::max((tr.w - tw) * 0.5F, 0.0F), tr.y,
                                     std::max(tw, 1.0F), tr.h},
                                    {0.80F, 0.84F, 0.92F, 1.0F},
                                    tr.y + tr.h * 0.5F + c.ft.px * 0.30F, s,
                                    zk({Part::header_group, -1,
                                        c.g.col_index[static_cast<std::size_t>(i)]}));
                    }
                    i = j + 1;
                }
            }
}

// ── pass_header：列头：列底 + 标题 + 排序/筛选装饰（装饰在 feature.paint）

inline void pass_header(DrawContext &c)
{
    auto &dlist = c.out.draw;
            // 列头
            for (int i = 0; i < c.g.col_count(); ++i)
            {
                const int dc = c.g.col_index[static_cast<std::size_t>(i)];
                const Rect hr =
                    clip_x({c.g.col_x_at(i), c.g.col_header.y,
                            c.g.col_w[static_cast<std::size_t>(i)], c.g.col_header.h},
                           c.g.clip_x0(i), c.g.clip_x1(i));
                if (hr.empty())
                    continue;
                glm::vec4 bg = c.p.header_bg;
                if (c.ix.band_col == dc)
                    bg = glm::vec4(glm::vec3(bg) + glm::vec3(0.06F), bg.a);
                dlist.add_shape(Stream::chrome, hr, bg, c.p.cell_radius, {}, 0,
                             zone_key({Part::header, -1, dc}));
                const auto &def = c.d.cols[static_cast<std::size_t>(dc)];
                const float inner = std::max(
                    hr.w - 2 * c.p.cell_pad - (def.filterable ? 18.0F : 0.0F), 1.0F);
                const float tw = text_width(c.ft, def.title);
                float dx = 0;
                if (def.a == Align::center)
                    dx = std::max((inner - tw) * 0.5F, 0.0F);
                else if (def.a == Align::right)
                    dx = std::max(inner - tw, 0.0F);
                dlist.add_text(Stream::chrome,
                            {hr.x + c.p.cell_pad + dx, hr.y, inner - dx, hr.h},
                            c.p.header_text, hr.y + hr.h * 0.5F + c.ft.px * 0.30F, def.title,
                            zone_key({Part::header, -1, dc}));
            }
}

// ── pass_rows：行带（斑马纹/悬停）+ 空表提示

inline void pass_rows(DrawContext &c)
{
    auto &dlist = c.out.draw;
            // 行带 + 空表
            for (int ri = 0; ri < c.g.row_count(); ++ri)
            {
                const int dr = c.g.row_index[static_cast<std::size_t>(ri)];
                const Rect rr =
                    intersect({c.g.body.x, c.g.row_y[static_cast<std::size_t>(ri)], c.g.body.w,
                               c.g.row_h[static_cast<std::size_t>(ri)]},
                              c.g.body);
                if (rr.empty())
                    continue;
                glm::vec4 bg = c.p.row_bg[static_cast<std::size_t>(dr) & 1U];
                if (c.ix.band_row == dr)
                    bg = c.p.row_hover;
                dlist.add_shape(Stream::chrome, rr, bg, 0, {}, 0, zk({Part::cell, dr, -1}));
            }
            if (c.g.view_rows.empty())
                dlist.add_text(Stream::chrome, {c.g.body.x, c.g.body.y, c.g.body.w, c.g.body.h},
                            {0.55F, 0.58F, 0.65F, 1.0F},
                            c.g.body.y + c.g.body.h * 0.5F + c.ft.px * 0.30F, "No data",
                            zk({Part::frame}));
}

// ── pass_cells：单元格：tag / dot / bar / wrap / text 五种绘制

inline void pass_cells(DrawContext &c)
{
    auto &dlist = c.out.draw;
            // 单元格
            for (int ri = 0; ri < c.g.row_count(); ++ri)
            {
                const int dr = c.g.row_index[static_cast<std::size_t>(ri)];
                for (int ci = 0; ci < c.g.col_count(); ++ci)
                {
                    const int dc = c.g.col_index[static_cast<std::size_t>(ci)];
                    Rect cr =
                        clip_x({c.g.col_x_at(ci), c.g.row_y[static_cast<std::size_t>(ri)],
                                c.g.col_w[static_cast<std::size_t>(ci)],
                                c.g.row_h[static_cast<std::size_t>(ri)]},
                               c.g.clip_x0(ci), c.g.clip_x1(ci));
                    cr = intersect(cr, c.g.body);
                    if (cr.empty())
                        continue;
                    const auto z = zk({Part::cell, dr, dc});
                    if (c.ix.band_col == dc)
                        dlist.add_shape(Stream::content, cr, c.p.col_hover, c.p.cell_radius, {},
                                     0, z);
                    const auto &def = c.d.cols[static_cast<std::size_t>(dc)];
                    const auto text = c.d.text(dr, dc);
                    if (text.empty())
                        continue;
                    const float inner = std::max(cr.w - 2 * c.p.cell_pad, 1.0F);
                    const float baseline = cr.y + cr.h * 0.5F + c.ft.px * 0.30F;
                    if (def.render == RenderKind::tag)
                    {
                        const auto s = fit_text(c.ft, text, std::max(inner - 16, 1.0F),
                                                dlist.ar, c.scratch);
                        const float tw = text_width(c.ft, s);
                        const float pw = std::min(tw + 16, inner);
                        const auto tc = def.tag_color(s, {0.42F, 0.45F, 0.50F, 1.0F});
                        const Rect pill{cr.x + c.p.cell_pad, cr.y + (cr.h - 20) * 0.5F, pw,
                                        20};
                        dlist.add_shape(Stream::content, pill, {tc.r, tc.g, tc.b, 0.30F}, 10,
                                     {}, 0, z);
                        dlist.add_text(Stream::content,
                                    {pill.x + 8, pill.y, pw - 16, pill.h},
                                    {tc.r * 0.5F + 0.5F, tc.g * 0.5F + 0.5F,
                                     tc.b * 0.5F + 0.5F, 1},
                                    pill.y + pill.h * 0.5F + c.ft.px * 0.30F, s, z);
                    }
                    else if (def.render == RenderKind::dot)
                    {
                        const auto dc2 = def.tag_color(text, {0.42F, 0.45F, 0.50F, 1.0F});
                        dlist.add_shape(Stream::content,
                                     {cr.x + c.p.cell_pad, cr.y + (cr.h - 8) * 0.5F, 8, 8},
                                     dc2, 4, {}, 0, z);
                        const auto s = fit_text(c.ft, text, std::max(inner - 16, 1.0F),
                                                dlist.ar, c.scratch);
                        dlist.add_text(Stream::content,
                                    {cr.x + c.p.cell_pad + 16, cr.y, inner - 16, cr.h},
                                    c.p.text, baseline, s, z);
                    }
                    else if (def.render == RenderKind::bar)
                    {
                        float frac = 0;
                        std::size_t q = 0;
                        double val = 0;
                        bool any = false, pct = false;
                        while (q < text.size() && (text[q] == ' ' || text[q] == '\t'))
                            ++q;
                        while (q < text.size() && text[q] >= '0' && text[q] <= '9')
                        {
                            val = val * 10 + (text[q] - '0');
                            any = true;
                            ++q;
                        }
                        while (q < text.size() && (text[q] == ' ' || text[q] == '%'))
                        {
                            pct = pct || text[q] == '%';
                            ++q;
                        }
                        if (any)
                            frac = static_cast<float>(pct || val > 1 ? val / 100.0 : val);
                        frac = std::clamp(frac, 0.0F, 1.0F);
                        const float by = cr.y + (cr.h - 6) * 0.5F + 3;
                        dlist.add_shape(Stream::content, {cr.x + c.p.cell_pad, by, inner, 6},
                                     {1, 1, 1, 0.10F}, 3, {}, 0, z);
                        dlist.add_shape(
                            Stream::content,
                            {cr.x + c.p.cell_pad, by, std::max(inner * frac, 2.0F), 6},
                            c.p.accent, 3, {}, 0, z);
                    }
                    else
                    {
                        if (def.wrap || c.p.wrap)
                        {
                            // 折行：行高在 sync 里按 wrap_lines 算过，这里逐行画
                            std::vector<std::string_view> lines;
                            wrap_lines(c.ft, text, inner, lines);
                            const float lh = c.ft.px * c.ft.line_h;
                            float ty = cr.y + 4.0F;
                            for (const auto ln : lines)
                            {
                                if (ty >= cr.y1())
                                    break;
                                const float tw = text_width(c.ft, ln);
                                float dx = 0;
                                if (def.a == Align::center)
                                    dx = std::max((inner - tw) * 0.5F, 0.0F);
                                else if (def.a == Align::right)
                                    dx = std::max(inner - tw, 0.0F);
                                dlist.add_text(Stream::content,
                                            {cr.x + c.p.cell_pad + dx, ty,
                                             std::max(inner - dx, 1.0F),
                                             std::min(lh, cr.y1() - ty)},
                                            c.p.text, ty + c.ft.px * 0.85F, ln, z);
                                ty += lh;
                            }
                        }
                        else
                        {
                            const auto s = fit_text(c.ft, text, inner, dlist.ar, c.scratch);
                            const float tw = text_width(c.ft, s);
                            float dx = 0;
                            if (def.a == Align::center)
                                dx = std::max((inner - tw) * 0.5F, 0.0F);
                            else if (def.a == Align::right)
                                dx = std::max(inner - tw, 0.0F);
                            dlist.add_text(Stream::content,
                                        {cr.x + c.p.cell_pad + dx, cr.y, inner - dx, cr.h},
                                        c.p.text, baseline, s, z);
                        }
                    }
                }
            }
}

// ── pass_separators：列分隔线（表头 + 表体各一条）

inline void pass_separators(DrawContext &c)
{
    auto &dlist = c.out.draw;
            // 列分隔
            for (int i = 1; i < c.g.col_count(); ++i)
            {
                const int dc = c.g.col_index[static_cast<std::size_t>(i - 1)];
                const float xx = c.g.col_x_at(i);
                const auto z = zk({Part::col_sep, -1, dc});
                dlist.add_shape(Stream::chrome,
                             clip_x({xx - 0.5F, c.g.col_header.y, 1, c.g.col_header.h},
                                    c.g.clip_x0(i), c.g.clip_x1(i)),
                             c.p.sep, 0, {}, 0, z);
                dlist.add_shape(Stream::chrome,
                             clip_x({xx - 0.5F, c.g.body.y, 1, c.g.body.h}, c.g.clip_x0(i),
                                    c.g.clip_x1(i)),
                             c.p.sep, 0, {}, 0, z);
            }
}

// ── pass_zones：拾取代理：由 hit 的同一套几何生成 zones（最后画 = 拾取赢）

inline void pass_zones(DrawContext &c)
{
            // 代理（最后画 = 拾取赢）：直接由 hit_test 的同一套几何生成
            const auto add_zone = [&](const Rect &r, const Zone &z) {
                if (!r.empty())
                    c.out.zones.push_back({r, z, 0});
            };
            add_zone(c.g.box, {Part::frame});
            if (c.g.group_h > 0)
                for (const auto &sp : c.g.groups)
                {
                    const float x0 =
                        std::max(c.g.col_x_at(sp.first_pos), c.g.clip_x0(sp.first_pos));
                    const float x1 =
                        std::min(c.g.col_x_at(sp.last_pos) +
                                     c.g.col_w[static_cast<std::size_t>(sp.last_pos)],
                                 c.g.clip_x1(sp.last_pos));
                    add_zone({x0, c.g.group.y, x1 - x0, c.g.group.h},
                             {Part::header_group, -1, sp.first_col});
                }
            for (int i = 0; i < c.g.col_count(); ++i)
            {
                const Rect hz =
                    clip_x({c.g.col_x_at(i), c.g.col_header.y,
                            c.g.col_w[static_cast<std::size_t>(i)], c.g.col_header.h},
                           c.g.clip_x0(i), c.g.clip_x1(i));
                const int dc = c.g.col_index[static_cast<std::size_t>(i)];
                add_zone(hz, {Part::header, -1, dc});
                if (dc >= 0 && dc < c.d.cols_count() &&
                    c.d.cols[static_cast<std::size_t>(dc)].filterable && hz.w > 24)
                    add_zone({hz.x1() - 24, hz.y, 19, hz.h}, {Part::filter_btn, -1, dc});
            }
            add_zone(c.g.gutter_head, {Part::check_head});
            for (int ri = 0; ri < c.g.row_count(); ++ri)
            {
                const int dr = c.g.row_index[static_cast<std::size_t>(ri)];
                for (int ci = 0; ci < c.g.col_count(); ++ci)
                    add_zone(intersect(clip_x({c.g.col_x_at(ci),
                                               c.g.row_y[static_cast<std::size_t>(ri)],
                                               c.g.col_w[static_cast<std::size_t>(ci)],
                                               c.g.row_h[static_cast<std::size_t>(ri)]},
                                              c.g.clip_x0(ci), c.g.clip_x1(ci)),
                                       c.g.body),
                             {Part::cell, dr, c.g.col_index[static_cast<std::size_t>(ci)]});
                if (c.g.gutter_w > 0)
                    add_zone({c.g.gutter_body.x, c.g.row_y[static_cast<std::size_t>(ri)],
                              c.g.gutter_w, c.g.row_h[static_cast<std::size_t>(ri)]},
                             {Part::check, dr, -1});
            }
            for (int i = 1; i < c.g.col_count(); ++i)
            {
                const int dc = c.g.col_index[static_cast<std::size_t>(i - 1)];
                add_zone(clip_x({c.g.col_x_at(i) - 3, c.g.col_header.y, 6, c.g.col_header.h},
                                c.g.clip_x0(i), c.g.clip_x1(i)),
                         {Part::col_sep, -1, dc});
            }
            if (c.g.vbar)
            {
                add_zone(c.g.vtrack, {Part::vbar_track});
                add_zone(c.g.vthumb, {Part::vbar_thumb});
            }
            if (c.g.hbar)
            {
                add_zone(c.g.htrack, {Part::hbar_track});
                add_zone(c.g.hthumb, {Part::hbar_thumb});
            }
            add_zone(c.g.edge_n, {Part::edge_n});
            add_zone(c.g.edge_w, {Part::edge_w});
            add_zone(c.g.edge_ne, {Part::edge_ne});
            add_zone(c.g.edge_nw, {Part::edge_nw});
            add_zone(c.g.edge_se, {Part::edge_se});
            add_zone(c.g.edge_sw, {Part::edge_sw});
}

// ── pass_features：feature 的绘制贡献（菜单/tooltip/编辑框/find/筛选框…）

inline void pass_features(DrawContext &c)
{
            // feature 的绘制贡献；放在基础 Zone 之后，菜单等 Zone 才能赢拾取
            {
                PaintContext fc{c.d, c.v, c.ix, c.p, c.g, c.ft};
                c.sy.paint(fc, c.out);
            }
}

// ── pass_owners：所有权收尾：默认 key、table 位、zone→table、proxy op、Owner 分派

inline void pass_owners(DrawContext &c)
{
    auto &dlist = c.out.draw;
            // 装饰性 op（背景/外框/tooltip 底）也必须有主：默认归 table frame。
            // 更具体的 Zone 在 emit 时已经给了，不会被覆盖。
            const std::uint32_t frame_zone = zk({Part::frame});
            for (auto &ss : c.out.draw.shapes)
                for (auto &o : ss)
                    if (o.key == 0)
                        o.key = frame_zone;
            for (auto &tt : c.out.draw.texts)
                for (auto &o : tt)
                    if (o.key == 0)
                        o.key = frame_zone;
            const std::uint32_t tbits = (c.tid.value & bits::kTableMask)
                                        << bits::kTableShift;
            if (tbits != 0)
            {
                for (auto &ss : c.out.draw.shapes)
                    for (auto &o : ss)
                        if (o.key != 0)
                            o.key |= tbits;
                for (auto &tt : c.out.draw.texts)
                    for (auto &o : tt)
                        if (o.key != 0)
                            o.key |= tbits;
            }
            for (auto &zr : c.out.zones)
                zr.z.table = c.tid;
            for (const auto &z : c.out.zones)
                dlist.add_shape(Stream::proxy, z.r, {0, 0, 0, 0}, 0, {}, 0, zk(z.z));
            // every op（含 proxy）都必须带 Owner：新增 Part 时 to_owner 会先返回
            // none_owner，selftest 的 “no Owner” 断言会当场失败。
            if (c.tid.value != 0)
            {
                const auto assign_who = [&](auto &ops) {
                    for (auto &o : ops)
                        if (o.key != 0)
                            o.who = to_owner(model::target_of(c.d, c.tid, zone_from(o.key)));
                };
                for (auto &ss : c.out.draw.shapes)
                    assign_who(ss);
                for (auto &tt : c.out.draw.texts)
                    assign_who(tt);
            }
}

constexpr std::array<void (*)(DrawContext &), 10> kPaintPasses = {
    pass_visual, pass_frame, pass_group_bands, pass_header, pass_rows,
    pass_cells, pass_separators, pass_zones, pass_features, pass_owners,
};

// ── 唯一入口：paint passes 的有序组合 ─────────────────────────────────
inline void compose_draw(const Table &d, const State &v, const Interaction &ix,
                         const Theme &p, const Layout &g, const Font &ft,
                         const AppRouter &sy, DrawList &out, TableId tid = {})
{
    DrawContext c{d, v, ix, p, g, ft, sy, out, tid, {}};
    out.clear();
    out.version = v.revision;
    if (g.box.empty())
        return;
    for (const auto pass : kPaintPasses)
        pass(c);
}
} // namespace table::detail
