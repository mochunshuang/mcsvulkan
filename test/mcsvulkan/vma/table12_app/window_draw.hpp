#pragma once
// ============================================================================
// table12_app/window_draw.hpp — window 层的 draw 链（只放定义）
//
//   draw_sync   : compose（layout+paint）并记录几何
//   draw_build  : 空（compose 已产出 DrawList）；保留为显式步骤名
//   draw_encode : platform::encode 的宿主包装
//   draw        : sync -> build -> encode + 统计
//   pointer_target / on_pick : CPU 命中与 picking 回读
//
//   依赖：host.hpp（Binding/Host）；不 include demo 场景代码。
// ============================================================================

#include "host.hpp"

namespace table::window
{
    inline void draw_sync(Binding &s, const table::Rect &box)
    {
        const float m = 48.0F;
        const table::Rect vp{box.x + m, box.y + m, std::max(box.w - 2.0F * m, 1.0F),
                                std::max(box.h - 2.0F * m, 1.0F)};
        s.vw = vp.w;
        s.vh = vp.h;
        // 门：compose = layout passes + paint passes（唯一派生入口）
        table::session::compose(s.session, vp);
        if (std::getenv("TABLE_GEOMLOG") != nullptr)
        {
            static std::uint32_t last_ver = ~0U;
            static float last_w = -1, last_h = -1;
            if (last_ver != s.session.layout.version || last_w != s.session.layout.box.w ||
                last_h != s.session.layout.box.h)
            {
                last_ver = s.session.layout.version;
                last_w = s.session.layout.box.w;
                last_h = s.session.layout.box.h;
                const auto &g = s.session.layout;
                std::print("[geom] box=({:.1f},{:.1f},{:.1f},{:.1f}) header_y={:.1f} "
                           "body=({:.1f},{:.1f},{:.1f},{:.1f}) scroll=({:.1f},{:.1f}) "
                           "cols=",
                           g.box.x, g.box.y, g.box.w, g.box.h, g.col_header.y, g.body.x,
                           g.body.y, g.body.w, g.body.h, g.sx, g.sy);
                for (int i = 0; i < g.col_count(); ++i)
                    std::print("{}:{:.1f}/{:.1f} ",
                               g.col_index[static_cast<std::size_t>(i)], g.col_x_at(i),
                               g.col_w[static_cast<std::size_t>(i)]);
                std::println("");
            }
        }
    }

    inline void draw_build(Binding &s)
    {
        // compose 已经产出 DrawList（s.session.draw 是 Session 的派生缓存）
        s.session.draw.table = s.session.id;
    }

    template <class Sink>
inline void draw_encode(const Binding &s, const ui_dao::view &view, Sink &&sink)
    {
        table::platform::encode(s.session.draw, s.session.font, view, std::forward<Sink>(sink),
                            s.host != nullptr ? s.host->hover_fn : ~0U);
    }

    inline const Binding::draw_info &draw(Binding &s, const table::Rect &box,
                                              const ui_dao::view &view,
                                              ::command::record_builder &rb)
    {
        draw_sync(s, box);
        draw_build(s);

        auto &info = s.info;
        info = {};
        info.version = s.session.state.revision;
        for (const auto &ops : s.session.draw.draw.shapes)
            info.shapes += static_cast<std::uint32_t>(ops.size());
        for (const auto &ops : s.session.draw.draw.texts)
            info.texts += static_cast<std::uint32_t>(ops.size());
        for (const auto &ops : s.session.draw.draw.shapes)
            for (const auto &o : ops)
                info.zoned += o.key != 0 ? 1U : 0U;

        draw_encode(s, view, [&](auto &&inst) {
            ++info.instances;
            rb.addInsteance(std::forward<decltype(inst)>(inst));
        });
        return info;
    }

    inline table::Target pointer_target(Binding &s, float x, float y)
    {
        s.pointer = table::session::hit(s.session, x, y);
        return s.pointer;
    }

    inline void on_pick(Binding &s, const picking_result &r)
    {
        s.pick = r;
        s.pick_version = s.session.state.revision;
        if (std::getenv("TABLE_PICKCHECK") == nullptr || r.key.object_type == 0xFFFFFFFF)
            return;
        const auto from_gpu = table::platform::resolve(s.session.model, r);
        const auto from_hit = table::session::hit(s.session, s.mx, s.my);
        if (from_gpu.to_zone() != from_hit.to_zone())
            std::println("[pick] MISMATCH gpu=({}, {}, {}) hit=({}, {}, {})",
                         table::part_name(from_gpu.kind), from_gpu.row_index,
                         from_gpu.col_index, table::part_name(from_hit.kind),
                         from_hit.row_index, from_hit.col_index);
        else
            std::println("[pick] ok {} row={} col={} entity={:#x}",
                         table::part_name(from_gpu.kind), from_gpu.row_index,
                         from_gpu.col_index, r.key.entity_index);
    }
} // namespace table::window
