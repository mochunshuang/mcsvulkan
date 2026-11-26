#pragma once
// ============================================================================
// table12/detail/common.hpp — detail 层的名字扁平化
//
//   detail 是“实现层”：feature / pass 天天要写 model::Table、intent::State、
//   frame::Layout、command::Press 这些跨模块名字。这里统一 using 一次，
//   让实现代码保持短名，而关系仍能从 include 图看出来（detail → 全部 schema）。
//
//   注意：detail 只被 session 使用；公共头不 include 本文件。
// ============================================================================

#include "../command.hpp"

namespace table::detail
{
// command
using command::ClearFilter;
using command::Command;
using command::Context;
using command::Drag;
using command::Effect;
using command::Hover;
using command::Key;
using command::KeyPress;
using command::Outcome;
using command::Press;
using command::Release;
using command::ScrollBegin;
using command::SetCell;
using command::Wheel;
using command::WriteClipboard;
// frame
using frame::DrawList;
using frame::Frame;
using frame::Layout;
using frame::PaintContext;
using frame::RowPipeline;
using frame::RowView;
using frame::Stream;
using frame::vis_active;
using frame::vis_editing;
using frame::vis_in_range;
using frame::ZoneRect;
// intent
using intent::DragColW;
using intent::DragHThumb;
using intent::DragHeader;
using intent::DragMode;
using intent::DragNone;
using intent::DragRef;
using intent::DragSelect;
using intent::DragTableSize;
using intent::DragVThumb;
using intent::Interaction;
using intent::State;
using intent::Theme;
using intent::to_drag_ref;
using intent::visit_drag;
// model
using model::Table;

// ── 键盘/查找把活动格滚进视口：只写 State.scroll，下一次 compose 生效 ──
//   （纯函数做不到，因为它要“把选择拉进视野”；但它不碰 Model）
inline void ensure_visible(State &v, const Layout &g, const Theme &, int row,
                           int col) noexcept
{
    if (row >= 0 && g.body.h > 0)
    {
        const int pos = g.order_pos(row);
        if (pos >= 0)
        {
            const float y0 = g.row_off[static_cast<std::size_t>(pos)];
            const float y1 = y0 + g.order_h(row);
            const float max_sy = std::max(0.0F, g.content_h - g.body.h);
            float ny = v.scroll.y;
            if (y0 < ny + 0.5F)
                ny = y0;
            else if (y1 > ny + g.body.h - 0.5F)
                ny = y1 - g.body.h;
            ny = std::clamp(ny, 0.0F, max_sy);
            if (ny != v.scroll.y)
            {
                v.scroll.y = ny;
                v.touch();
            }
        }
    }
    if (col >= 0)
    {
        const int cp = g.pos_of_col(col);
        if (cp >= 0 && cp >= g.frozen && g.scrollv.w > 0)
        {
            const float x0 = g.col_x[static_cast<std::size_t>(cp)];
            const float x1 = x0 + g.col_w[static_cast<std::size_t>(cp)];
            const float span = g.scrollv.w;
            const float max_sx = std::max(0.0F, g.content_w - g.frozen_w - span);
            float nx = v.scroll.x;
            if (x0 < nx)
                nx = x0;
            else if (x1 > nx + span)
                nx = x1 - span;
            nx = std::clamp(nx, 0.0F, max_sx);
            if (nx != v.scroll.x)
            {
                v.scroll.x = nx;
                v.touch();
            }
        }
    }
}
} // namespace table::detail
