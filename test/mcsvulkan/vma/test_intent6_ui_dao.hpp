#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// test_intent6_ui_dao.hpp —— DAO 层：把“像素参数”编码成 shader 的实例行
//
//   只做数据编码，不依赖 Vulkan、不依赖 CommandBuilder。
//   依赖：shader_data 的 ABI 结构（Glyph/UiRect/Rectangle/VertexAttr）。
//   约定：像素坐标 y 向下；NDC y 向上；颜色传“直色”（shader 内部预乘）。
//
//   包含顺序：本文件要求 UvTransform / VertexTransform 已在前面定义
//   （test_intent6.cpp 第 300 行附近），或由使用者在 include 前定义。
// ═══════════════════════════════════════════════════════════════════════════

#include "test_intent6_shader_abi.hpp"

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace ui_dao
{

struct rect_px
{
    float x = 0, y = 0, w = 0, h = 0;
};

struct view
{
    float w = 1, h = 1;
};

inline glm::vec2 ndc_center(const rect_px &r, const view &v) noexcept
{
    return {r.x / v.w * 2.0F - 1.0F + r.w / v.w,
            1.0F - (r.y / v.h * 2.0F + r.h / v.h)};
}

inline glm::vec2 ndc_size(const rect_px &r, const view &v) noexcept
{
    return {r.w / v.w * 2.0F, r.h / v.h * 2.0F};
}

inline glm::mat4 model_for(const rect_px &r, const view &v) noexcept
{
    const glm::vec2 c = ndc_center(r, v);
    const glm::vec2 s = ndc_size(r, v);
    return glm::translate(glm::mat4(1.0F), glm::vec3(c, 0.0F)) *
           glm::scale(glm::mat4(1.0F), glm::vec3(s, 1.0F));
}

// Glyph：MSDF / 位图
inline shader_data::Glyph glyph(const rect_px &r, const view &v,
                                std::uint32_t texture, std::uint32_t sampler,
                                std::uint32_t fontType, float pxRange,
                                const glm::vec4 &tint,
                                const UvTransform &uv = {}, std::uint32_t entity = 0)
{
    shader_data::Glyph g{};
    g.model = model_for(r, v);
    g.uvTransform = uv;
    g.textureIndex = texture;
    g.samplerIndex = sampler;
    g.fontType = fontType;
    g.pxRange = pxRange;
    g.modulateFlag = 1U;
    g.color = tint;
    g.entity_index = entity;
    return g;
}

// Rectangle：圆角 + 阴影 + 填充
inline shader_data::Rectangle round_rect(const rect_px &r, const view &v,
                                         float radius_px, const glm::vec4 &fill,
                                         const glm::vec4 &shadow = {},
                                         float shadow_ox = 0.0F,
                                         float shadow_oy = 0.0F,
                                         float blur_px = 0.0F,
                                         float spread = 1.0F,
                                         std::uint32_t entity = 0)
{
    shader_data::Rectangle out{};
    out.model = model_for(r, v);
    out.size = ndc_size(r, v);
    out.effects = shader_data::FX_ROUNDED | shader_data::FX_FILL;
    for (auto &c : out.colors)
        c = fill;
    out.shadowColor = shadow;
    if (shadow.a > 0.0F)
        out.effects |= shader_data::FX_SHADOW;
    // radiusSoftness：x=圆角/最短边，y=1px/w，z=模糊/w，w=扩散
    const float w_ndc = out.size.x;
    const float min_ndc = std::max(std::min(out.size.x, out.size.y), 1e-6F);
    out.radiusSoftness = {radius_px * 2.0F / v.w / min_ndc,
                          (1.0F / v.w) / std::max(w_ndc, 1e-6F),
                          (blur_px * 2.0F / v.w) / std::max(w_ndc, 1e-6F),
                          spread};
    out.shadowOffset = {shadow_ox * 2.0F / v.w / std::max(w_ndc, 1e-6F),
                        shadow_oy * 2.0F / v.h / std::max(out.size.y, 1e-6F)};
    out.entity_index = entity;
    return out;
}

// UiRect：布局线框/边框
inline shader_data::UiRect ui_rect(const rect_px &r, const view &v,
                                   const glm::vec4 &color, float border_px,
                                   std::uint32_t entity = 0)
{
    shader_data::UiRect out{};
    const glm::vec2 c = ndc_center(r, v);
    const glm::vec2 s = ndc_size(r, v);
    out.center_size = {c.x, c.y, s.x, s.y};
    out.color = color;
    // border 是 local [-0.5,0.5] 的半宽：px / 最短边(px)
    const float min_px = std::min(r.w, r.h);
    out.border = (min_px > 0.0F) ? border_px / min_px : 0.0F;
    out.entity_index = entity;
    return out;
}

inline shader_data::VertexAttr vertex_attr(const glm::vec4 &color)
{
    shader_data::VertexAttr out{};
    out.color = color;
    return out;
}

} // namespace ui_dao
