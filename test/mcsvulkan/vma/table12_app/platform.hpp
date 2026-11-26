#pragma once
// ============================================================================
// table12_app/platform.hpp — GPU/事件 ABI 端口（唯一 GPU 出口）
//
//   encode：DrawList -> shader instances（entity_index = TargetKey）
//     纯数据变换：不读窗口、不改 Model/State、不做 I/O。
//   resolve：TargetKey -> Target 在 table::model::resolve（纯函数，picking 回读用）。
//
//   hover_fn 由 window::Host 提供（ability11 的 hover_fn_ref 全局已删除）。
//
//   ⚠ 本头是 app 层：它认识 ui_dao 的 shader 实例类型，必须在 ui_dao 定义之后
//     被 include；核心（table12/）永远不 include 它。
// ============================================================================

#include "../table12/frame.hpp"

namespace table::platform
{
// ── 字形裁剪（encode 的私有工具，纯几何） ───────────────────────────
    inline void clip_glyph(ui_dao::rect_px &r, UvTransform &uv,
                           const ui_dao::rect_px &clip) noexcept
    {
        if (r.w <= 0.0F || r.h <= 0.0F || clip.w <= 0.0F || clip.h <= 0.0F)
            return;
        const float l = std::max(r.x, clip.x), t = std::max(r.y, clip.y);
        const float q = std::min(r.x + r.w, clip.x + clip.w);
        const float b = std::min(r.y + r.h, clip.y + clip.h);
        if (q <= l || b <= t)
        {
            r = {};
            return;
        }
        const float sx = (l - r.x) / r.w, sy = (t - r.y) / r.h;
        const float sw = (q - l) / r.w, sh = (b - t) / r.h;
        uv.offset += glm::vec2(sx * uv.scale.x, sy * uv.scale.y);
        uv.scale *= glm::vec2(sw, sh);
        r = {l, t, q - l, b - t};
    }


// ── 唯一 GPU 出口 ────────────────────────────────────────────────────
//   target_key 就是 entity_index：draw 与 picking 只在这一处合并。
template <class View, class Sink>
inline void encode(const frame::DrawList &in, const Font &f, const View &v, Sink &&sink,
                   std::uint32_t hover_fn)
{
    const auto emit_shape = [&](const frame::ShapeOp &o) {
        auto s = ui_dao::sdf_shape({o.r.x, o.r.y, o.r.w, o.r.h}, v, o.radius, o.color, {},
                                   0.0F, 0.0F, 1.0F, o.key);
        s.hover_fn = hover_fn;
        s.effects = shader_data::FX_FILL;
        const bool has_border = o.border_w > 0.0F && o.border.a > 0.0F;
        if (has_border)
        {
            s.border_px = o.border_w;
            s.border_color = o.border;
            s.effects |= shader_data::FX_BORDER;
        }
        if (o.color.a <= 0.0F)
            s.effects = has_border ? shader_data::FX_BORDER : 0U;
        if (s.effects != 0U || o.key != 0U)
            sink(s);
    };
    const auto emit_text = [&](const frame::TextOp &o) {
        const float px = f.px;
        const std::string_view str{o.p, o.len};
        float x = o.r.x;
        std::size_t i = 0;
        while (i < str.size())
        {
            char32_t cp = 0;
            const std::size_t n = utf8_decode(str.substr(i), cp);
            if (const auto *g = f.find(cp))
            {
                const float ydir = g->y_down ? 1.0F : -1.0F;
                const float left = x + g->plane.x * px;
                const float right = x + g->plane.z * px;
                const float top = o.baseline + ydir * g->plane.y * px;
                const float bottom = o.baseline + ydir * g->plane.w * px;
                ui_dao::rect_px r{left, top, right - left, bottom - top};
                UvTransform uv{};
                uv.scale = {g->uv.z - g->uv.x, g->uv.w - g->uv.y};
                uv.offset = {g->uv.x, g->uv.y};
                clip_glyph(r, uv, {o.r.x, o.r.y, o.r.w, o.r.h});
                if (r.w > 0.0F && r.h > 0.0F)
                    sink(ui_dao::glyph_quad(r, v, g->tex, g->samp, g->font_id,
                                            g->px_range, o.color, uv, o.key));
                x += g->advance * px;
            }
            i += n;
        }
    };
    for (const auto &s : in.draw.shapes[0])
        emit_shape(s);
    for (const auto &t : in.draw.texts[0])
        emit_text(t);
    for (const auto &s : in.draw.shapes[1])
        emit_shape(s);
    for (const auto &t : in.draw.texts[1])
        emit_text(t);
    for (const auto &s : in.draw.shapes[2])
        emit_shape(s);
    for (const auto &t : in.draw.texts[2])
        emit_text(t);
    for (const auto &s : in.draw.shapes[3])
        emit_shape(s);
}
} // namespace table::platform
