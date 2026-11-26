#pragma once
// ============================================================================
// table12/frame.hpp — 一帧的派生结果：Layout（几何）+ DrawList（绘制指令）
//
//   Layout   一次布局的派生几何：列盒、可见行、滚动条、边缘把手。每帧可重建；
//            不持有 model/state（只读，由 composer 传入）。
//   DrawList 一帧的绘制指令：shapes/texts 按 Stream 分桶 + zones + visual。
//            只被 platform::encode 消费；不持有 GPU。
//   Frame    { Layout, DrawList }：session::compose 的返回值。
//   PaintContext  paint pass / feature.paint 看到的上下文（只读 + 独立 DL 出口）。
//
//   纯函数：layout（detail/layout_passes）、paint（detail/paint_passes）、
//   hit（本文件，唯一几何命中；语义覆盖在 detail/gestures）。
//
//   依赖：contract/model/intent。不 include command/session/window/platform。
// ============================================================================

#include "contract.hpp"
#include "intent.hpp"
#include "model.hpp"

namespace table::frame
{
using intent::Interaction;
using intent::State;
using intent::Theme;
using model::Table;

    // ── 行变换（filter / sort）───────────────────────────────────────────
    struct RowView
    {
        std::span<const row_idx> rows;
        std::uint32_t version{0};
    };

    struct RowPipeline
    {
        std::vector<row_idx> buf;
        RowView view_{};
        RowView base(const Table &d)
        {
            buf.resize(static_cast<std::size_t>(d.rows()));
            std::iota(buf.begin(), buf.end(), 0);
            view_.rows = buf;
            view_.version = static_cast<std::uint32_t>(buf.size());
            return view_;
        }
    };
    // ── Layout：派生 ────────────────────────────────────────────────────
    struct ZoneRect
    {
        Rect r{};
        Zone z{};
        std::uint16_t order{0};
    };

    // 表头分组带：一段连续的可见列（first/last 是几何位置，first_col/last_col 是数据列）
    struct GroupSpan
    {
        int first_pos{0}, last_pos{0};
        int first_col{0}, last_col{0};
    };

    struct Layout
    {
        Rect box, header, group, col_header, body, frozen_view, scrollv;
        Rect gutter_head, gutter_body;
        float group_h{0}, gutter_w{0}, frozen_w{0}, content_w{0}, content_h{0};
        float sx{0}, sy{0}, step{0};
        int frozen{0};

        std::vector<int> col_index;
        std::vector<float> col_x, col_w;
        std::vector<int> view_rows;
        std::vector<int> row_index;
        std::vector<float> row_y, row_h, row_off;
        std::vector<float> row_h_data;
        std::vector<GroupSpan> groups;
        float row_h_key{-1};
        std::uint32_t src_ver{0};

        bool vbar{false}, hbar{false};
        Rect vtrack, vthumb, htrack, hthumb, corner;
        Rect edge_n, edge_w, edge_ne, edge_nw, edge_se, edge_sw;
        std::uint32_t version{0};

        [[nodiscard]] int col_count() const noexcept
        {
            return static_cast<int>(col_index.size());
        }
        [[nodiscard]] int row_count() const noexcept
        {
            return static_cast<int>(row_index.size());
        }
        [[nodiscard]] float col_x_at(int p) const noexcept
        {
            return body.x + gutter_w + (p < frozen ? 0.0F : -sx) +
                   col_x[static_cast<std::size_t>(p)];
        }
        [[nodiscard]] float clip_x0(int p) const noexcept
        {
            return p < frozen ? frozen_view.x : scrollv.x;
        }
        [[nodiscard]] float clip_x1(int p) const noexcept
        {
            return p < frozen ? frozen_view.x1() : scrollv.x1();
        }
        [[nodiscard]] int row_at(float y) const noexcept
        {
            for (int i = 0; i < row_count(); ++i)
            {
                const float y0 = row_y[static_cast<std::size_t>(i)];
                if (y >= y0 && y < y0 + row_h[static_cast<std::size_t>(i)])
                    return row_index[static_cast<std::size_t>(i)];
            }
            return -1;
        }
        [[nodiscard]] int col_at(float x) const noexcept
        {
            if (x < body.x + gutter_w)
                return -1;
            for (int i = 0; i < col_count(); ++i)
            {
                const float x0 = std::max(col_x_at(i), clip_x0(i));
                const float x1 = std::min(
                    col_x_at(i) + col_w[static_cast<std::size_t>(i)], clip_x1(i));
                if (x >= x0 && x < x1)
                    return col_index[static_cast<std::size_t>(i)];
            }
            return -1;
        }
        [[nodiscard]] int pos_of_col(int c) const noexcept
        {
            for (int i = 0; i < col_count(); ++i)
                if (col_index[static_cast<std::size_t>(i)] == c)
                    return i;
            return -1;
        }
        [[nodiscard]] int pos_at(float x) const noexcept
        {
            for (int i = 0; i < col_count(); ++i)
            {
                const float x0 = std::max(col_x_at(i), clip_x0(i));
                const float x1 = std::min(
                    col_x_at(i) + col_w[static_cast<std::size_t>(i)], clip_x1(i));
                if (x >= x0 && x < x1)
                    return i;
            }
            return -1;
        }
        // ── 全量行序（view_rows/row_off）：键盘可见性用 ──
        [[nodiscard]] int order_pos(int data_row) const noexcept
        {
            for (int i = 0; i < static_cast<int>(view_rows.size()); ++i)
                if (view_rows[static_cast<std::size_t>(i)] == data_row)
                    return i;
            return -1;
        }
        [[nodiscard]] float order_y(int data_row) const noexcept
        {
            const int p = order_pos(data_row);
            return p < 0 ? -1.0F : row_off[static_cast<std::size_t>(p)];
        }
        [[nodiscard]] float order_h(int data_row) const noexcept
        {
            return static_cast<std::size_t>(data_row) < row_h_data.size()
                       ? row_h_data[static_cast<std::size_t>(data_row)]
                       : 0.0F;
        }
    };
    // ── DrawList：派生 ──────────────────────────────────────────────────
    enum class Stream : std::uint8_t
    {
        chrome = 0,
        content = 1,
        overlay = 2,
        proxy = 3,
        Count
    };

    struct ShapeOp
    {
        Rect r{};
        glm::vec4 color{0};
        glm::vec4 border{0};
        float radius{0}, border_w{0};
        std::uint32_t key{0};
        Owner who{};
    };
    struct TextOp
    {
        Rect r{};
        glm::vec4 color{1};
        float baseline{0};
        const char *p{nullptr};
        std::uint32_t len{0};
        std::uint32_t key{0};
        Owner who{};
    };

    struct Draw
    {
        std::array<std::vector<ShapeOp>, 4> shapes;
        std::array<std::vector<TextOp>, 4> texts;
        Arena ar;
        void clear()
        {
            for (auto &s : shapes)
                s.clear();
            for (auto &t : texts)
                t.clear();
            ar.reset();
        }
        void add_shape(Stream st, const Rect &r, const glm::vec4 &c, float radius = 0,
                       const glm::vec4 &border = {}, float bw = 0, std::uint32_t zk = 0)
        {
            if (r.empty())
                return;
            shapes[static_cast<std::size_t>(st)].push_back(
                {r, c, border, radius, bw, zk});
        }
        void add_text(Stream st, const Rect &r, const glm::vec4 &c, float baseline,
                      std::string_view s, std::uint32_t zk = 0)
        {
            if (s.empty() || r.empty())
                return;
            const auto k = ar.put(s);
            texts[static_cast<std::size_t>(st)].push_back(
                {r, c, baseline, k.data(), static_cast<std::uint32_t>(k.size()), zk});
        }
        [[nodiscard]] std::size_t op_count() const noexcept
        {
            std::size_t n = 0;
            for (const auto &s : shapes)
                n += s.size();
            for (const auto &t : texts)
                n += t.size();
            return n;
        }
    };

    struct DrawList
    {
        Draw draw;
        std::vector<ZoneRect> zones;
        std::vector<std::uint8_t> visual;
        std::uint32_t version{0};
        TableId table{};
        void clear()
        {
            draw.clear();
            zones.clear();
            visual.clear();
        }
    };
    enum : std::uint8_t
    {
        vis_in_range = 1u << 0,
        vis_active = 1u << 1,
        vis_editing = 1u << 2,
    };
        inline Zone hit(const Layout &g, float x, float y) noexcept
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

// ── PaintContext：paint pass 与 feature.paint 的唯一入口参数 ───────────
//   d=model, v=state, ix=interaction, p=theme, g=layout, ft=font；
//   绘制输出由调用者单独传入（DrawList &），不放进上下文。
struct PaintContext
{
    const model::Table &d;
    const intent::State &v;
    const intent::Interaction &ix;
    const intent::Theme &p;
    const Layout &g;
    const Font &ft;
};

// ── 一帧的完整结果：对 Session 缓存几何/DL 的借用视图（不拷贝） ──────
//   compose 写 Session.geom / Session.frame，然后把这一对借给调用者。
//   想长期持有就自己拷贝 Layout/DrawList；Frame 只是这一帧的把手。
struct Frame
{
    const Layout &layout;
    const DrawList &draw;
    std::uint32_t revision{0};
    TableId table{};
};
} // namespace table::frame
