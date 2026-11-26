#pragma once
// ============================================================================
// table12/detail/features/layout_ops.hpp — SortFeature / ResizeFeature / ReorderFeature / FreezeFeature / TableResizeFeature
//
//   纯移动自 detail/features.hpp（G3）；不改行为、不改顺序。
//   依赖：../common.hpp。
// ============================================================================

#include "../common.hpp"

namespace table::detail
{
        struct SortFeature
        {
            bool toggle(Context &c, int col)
            {
                c.state.sort.toggle(col);
                c.state.touch();
                return true;
            }
            template <class E>
            Outcome handle(Context &, const E &)
            {
                return command::handled({});
            }
            void rows(Context &c, std::vector<row_idx> &r)
            {
                if (c.state.sort.dir == 0 || c.state.sort.col < 0)
                    return;
                const int cc = c.state.sort.col;
                const bool desc = c.state.sort.dir == 2;
                std::stable_sort(r.begin(), r.end(), [&](int a, int b) {
                    const auto x = c.model.text(a, cc);
                    const auto y = c.model.text(b, cc);
                    return desc ? x > y : x < y;
                });
            }

            void paint(const PaintContext &c, DrawList &fr) const
            {
                if (c.state.sort.dir == 0 || c.state.sort.col < 0)
                    return;
                const int pos = c.layout.pos_of_col(c.state.sort.col);
                if (pos < 0)
                    return;
                const Rect hr =
                    clip_x({c.layout.col_x_at(pos), c.layout.col_header.y,
                            c.layout.col_w[static_cast<std::size_t>(pos)], c.layout.col_header.h},
                           c.layout.clip_x0(pos), c.layout.clip_x1(pos));
                if (hr.empty())
                    return;
                fr.draw.add_shape(Stream::chrome,
                                  {hr.x1() - 12, hr.y + hr.h * 0.5F - 3, 8, 6},
                                  c.theme.header_text, 1, {}, 0,
                                  zone_key({Part::header, -1, c.state.sort.col}));
            }
        };

        // 筛选：语义命令 + 行 stage + 列头漏斗（状态在 State.filter）
        struct ResizeFeature
        {
            bool begin(Context &c, const Target &t, float x)
            {
                const int col = t.col_index;
                const float base =
                    col >= 0 && static_cast<std::size_t>(col) < c.state.cols.w.size()
                        ? c.state.cols.w[static_cast<std::size_t>(col)]
                        : 0.0F;
                return begin(c, col, x, base);
            }
            bool begin(Context &c, int col, float x, float base)
            {
                c.state.cols.resize_col = col;
                c.state.cols.resize_x0 = x;
                c.state.cols.resize_base = base;
                c.state.touch();
                return true;
            }
            bool move(Context &c, float x)
            {
                auto &v = c.state;
                if (v.cols.resize_col >= 0 &&
                    static_cast<std::size_t>(v.cols.resize_col) < v.cols.w.size())
                {
                    const auto &def =
                        c.model.cols[static_cast<std::size_t>(v.cols.resize_col)];
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
                c.state.cols.resize_col = -1;
                c.state.touch();
                return true;
            }
            template <class E>
            Outcome handle(Context &, const E &)
            {
                return command::handled({});
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
                auto &v = c.state;
                auto &ix = c.interaction;
                v.cols.reorder_from = c.layout.pos_of_col(col);
                v.cols.reorder_to = v.cols.reorder_from;
                v.cols.reorder_col = col;
                v.cols.reorder_x0 = x;
                ix.header_moved = false;
                v.touch();
                return true;
            }
            bool move(Context &c, float x)
            {
                auto &v = c.state;
                auto &ix = c.interaction;
                if (v.cols.reorder_from >= 0)
                {
                    if (std::abs(x - v.cols.reorder_x0) > 4.0F)
                        ix.header_moved = true;
                    const int to = c.layout.pos_at(x);
                    if (to >= 0)
                        v.cols.reorder_to = to;
                    v.touch();
                }
                return true;
            }
            int end(Context &c)
            {
                auto &v = c.state;
                auto &ix = c.interaction;
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
            Outcome handle(Context &, const E &)
            {
                return command::handled({});
            }
            void rows(Context &, std::vector<row_idx> &) {}

            void paint(const PaintContext &c, DrawList &fr) const
            {
                if (c.interaction.drag != DragMode::header || c.state.cols.reorder_from < 0)
                    return;
                const int from = c.state.cols.reorder_from;
                if (from >= static_cast<int>(c.state.cols.order.size()))
                    return;
                const int col = c.state.cols.order[static_cast<std::size_t>(from)];
                const int pos = c.layout.pos_of_col(col);
                if (pos < 0)
                    return;
                const float gw = c.layout.col_w[static_cast<std::size_t>(pos)];
                const float gx = c.interaction.pointer.x - gw * 0.5F;
                const Rect ghost{gx, c.layout.col_header.y, gw, c.layout.col_header.h};
                fr.draw.add_shape(
                    Stream::overlay, ghost,
                    {c.theme.header_bg.r, c.theme.header_bg.g, c.theme.header_bg.b, 0.85F},
                    c.theme.cell_radius, c.theme.accent, 1.5F, 0);
                if (col < c.model.cols_count())
                    fr.draw.add_text(
                        Stream::overlay,
                        {ghost.x + c.theme.cell_pad, ghost.y, gw - 2 * c.theme.cell_pad, ghost.h},
                        c.theme.header_text, ghost.y + ghost.h * 0.5F + c.font.px * 0.30F,
                        c.model.cols[static_cast<std::size_t>(col)].title, 0);
                if (c.state.cols.reorder_to >= 0)
                {
                    const int to = c.state.cols.reorder_to;
                    const float xt =
                        to < c.layout.col_count()
                            ? c.layout.col_x_at(to)
                            : c.layout.col_x_at(c.layout.col_count() - 1) + c.layout.col_w.back();
                    fr.draw.add_shape(
                        Stream::overlay,
                        {xt - 1, c.layout.col_header.y, 2, c.layout.body.y1() - c.layout.col_header.y},
                        c.theme.accent, 0, {}, 0, 0);
                }
            }
        };

        // 冻结列：只写 State.frozen；几何在 sync 里消费
        struct FreezeFeature
        {
            bool set(Context &c, int n)
            {
                c.state.cols.frozen = n;
                c.state.touch();
                return true;
            }
            template <class E>
            Outcome handle(Context &, const E &)
            {
                return command::handled({});
            }
            void rows(Context &, std::vector<row_idx> &) {}
            void paint(const PaintContext &, DrawList &) const {}
        };

        // 行勾选：命令 + 沟槽绘制
        struct TableResizeFeature
        {
            bool begin(Context &c, float x, float y, float w, float h)
            {
                c.interaction.trs_x0 = x;
                c.interaction.trs_y0 = y;
                c.interaction.trs_w = w;
                c.interaction.trs_h = h;
                c.state.touch();
                return true;
            }
            bool move(Context &c, float x, float y)
            {
                auto &v = c.state;
                auto &ix = c.interaction;
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
                v.cols.user_w = std::max(ix.trs_w + (use_x ? dx : 0.0F), c.theme.min_w);
                v.cols.user_h = std::max(ix.trs_h + (use_y ? dy : 0.0F), c.theme.min_h);
                v.touch();
                return true;
            }
            bool end(Context &c)
            {
                c.state.touch();
                return true;
            }
            template <class E>
            Outcome handle(Context &, const E &)
            {
                return command::handled({});
            }
            void rows(Context &, std::vector<row_idx> &) {}
            void paint(const PaintContext &, DrawList &) const {}
        };

        // 剪贴板 / 删除：读 selection 切片，写 Effect + undo
} // namespace table::detail
