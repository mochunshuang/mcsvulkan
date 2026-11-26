#pragma once
// ============================================================================
// table12/detail/features/overlay.hpp — 列 helper + 菜单常量/几何 helper + MenuFeature / TooltipFeature
//
//   纯移动自 detail/features.hpp（G3）；不改行为、不改顺序。
//   依赖：../common.hpp。
// ============================================================================

#include "../common.hpp"

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

        struct TooltipFeature
        {
            template <class E>
            Outcome handle(Context &c, const E &)
            {
                return command::handled({});
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
            Outcome handle(Context &c, const E &x)
            {
                auto &v = c.v;
                if constexpr (std::is_same_v<E, Press>)
                {
                    if (!v.menu.open)
                return {};
                    if (x.z.kind != Part::menu_item)
                    {
                        v.menu.open = false;
                        v.touch();
                    }
                return {};
                }
                else if constexpr (std::is_same_v<E, KeyPress>)
                {
                    if (!v.menu.open)
                return {};
                    if (x.k == Key::esc)
                    {
                        v.menu.open = false;
                        v.touch();
                    }
                return Outcome::handled_now();
                }
                return command::handled({});
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
} // namespace table::detail
