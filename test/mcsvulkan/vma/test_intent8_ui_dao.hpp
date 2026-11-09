#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// test_intent8_ui_dao.hpp —— DAO 层：把“像素参数”编码成 shader 的实例行
//
//   只做数据编码，不依赖 Vulkan、不依赖 CommandBuilder。
//   依赖：shader_data 的 7 个机制行 Shape / PathFill / PathStroke / TexQuad /
//   BlurQuad / ColorMatrix / NoiseQuad。
//   注意：glyph 不是独立程序，DAO 直接编码成 TexQuad（texture + metadata）。
//   约定：像素坐标 y 向下；NDC y 向下（-1 = 屏幕顶部，+1 = 底部）；
//        颜色传“直色”（shader 内部预乘）。
//
//   包含顺序：本文件要求 UvTransform / VertexTransform 已在前面定义
//   （test_intent6.cpp 第 300 行附近），或由使用者在 include 前定义。
// ═══════════════════════════════════════════════════════════════════════════

#include "test_intent8_shader_abi.hpp"

#include <algorithm>
#include <cstddef>
#include <span>

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
    // Vulkan UI 相机：NDC -1 = 屏幕顶部，+1 = 底部；屏幕像素 y 向下。
    // 所以像素中心 y 直接线性映射到 NDC y（不再取反）。
    return {r.x / v.w * 2.0F - 1.0F + r.w / v.w,
            r.y / v.h * 2.0F - 1.0F + r.h / v.h};
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

// Glyph = texture + metadata：DAO 直接产出 TexQuad，不经过 Glyph 源行。
inline shader_data::TexQuad glyph_quad(const rect_px &r, const view &v,
                                       std::uint32_t texture, std::uint32_t sampler,
                                       std::uint32_t fontType, float pxRange,
                                       const glm::vec4 &tint,
                                       const UvTransform &uv = {},
                                       std::uint32_t entity = 0)
{
    shader_data::TexQuad q{};
    q.model = model_for(r, v);
    q.uvTransform = uv;
    q.textureIndex = texture;
    q.samplerIndex = sampler;
    q.color = tint;
    q.px_size = {1.0F, 1.0F};
    q.shape = {0.0F, 0.0F};
    q.opacity = 1.0F;
    q.glyph_mode = fontType;
    q.px_range = pxRange;
    q.glyph_flags = 3U; // bit0=modulate, bit1=is-glyph
    q.entity_index = entity;
    return q;
}

// ── 机制 0：sdf_shape / shape_border（同一个 Shape 程序）────────────
inline shader_data::Shape sdf_shape(const rect_px &r, const view &v,
                                    float radius_px, const glm::vec4 &fill,
                                    const glm::vec4 &shadow = {},
                                    float shadow_oy = 0.0F,
                                    float blur_px = 0.0F,
                                    float spread = 1.0F,
                                    std::uint32_t entity = 0)
{
    shader_data::Shape out{};
    out.model = model_for(r, v);
    out.px_size = {std::max(r.w, 1.0F), std::max(r.h, 1.0F)};
    out.radii_px = glm::vec4(std::max(radius_px, 0.0F));
    out.effects = shader_data::FX_FILL;
    for (auto &c : out.colors)
        c = fill;
    out.border_color = fill;
    out.shadow_color = shadow;
    out.shadow_offset_px = {0.0F, shadow_oy};
    out.blur_px = std::max(blur_px, 0.0F);
    out.spread = std::max(spread, 0.0F);
    if (shadow.a > 0.0F)
        out.effects |= shader_data::FX_SHADOW;
    out.entity_index = entity;
    return out;
}

inline shader_data::Shape shape_border(const rect_px &r, const view &v,
                                       float radius_px, const glm::vec4 &color,
                                       float border_px, std::uint32_t entity = 0)
{
    shader_data::Shape out{};
    out.model = model_for(r, v);
    out.px_size = {std::max(r.w, 1.0F), std::max(r.h, 1.0F)};
    out.radii_px = glm::vec4(std::max(radius_px, 0.0F));
    out.border_px = std::max(border_px, 0.5F);
    out.border_color = color;
    out.effects = shader_data::FX_BORDER;
    for (auto &c : out.colors)
        c = color; // fill 关闭；填色只为保持行数据完整
    out.entity_index = entity;
    return out;
}

// 线性渐变：四顶点色，gradient=0（Shape 的 linear shading）
inline shader_data::Shape sdf_linear(const rect_px &r, const view &v, float radius_px,
                                     const glm::vec4 &c0, const glm::vec4 &c1,
                                     bool horizontal = true)
{
    auto out = sdf_shape(r, v, radius_px, c0);
    if (horizontal)
        out.colors[0] = out.colors[3] = c0, out.colors[1] = out.colors[2] = c1;
    else
        out.colors[0] = out.colors[1] = c0, out.colors[2] = out.colors[3] = c1;
    return out;
}

// 径向渐变：gradient = (1, center_px, radius_px)
inline shader_data::Shape sdf_radial(const rect_px &r, const view &v, float radius_px,
                                     const glm::vec4 &inner, const glm::vec4 &outer)
{
    auto out = sdf_shape(r, v, radius_px, outer);
    out.colors[0] = inner;
    out.colors[1] = outer;
    out.gradient = {1.0F, r.w * 0.5F, r.h * 0.5F, std::min(r.w, r.h) * 0.5F};
    return out;
}

// 圆锥渐变：gradient = (2, center_px, repeats)
inline shader_data::Shape sdf_conic(const rect_px &r, const view &v, float radius_px,
                                    const glm::vec4 &c0, const glm::vec4 &c1,
                                    float repeats = 1.0F)
{
    auto out = sdf_shape(r, v, radius_px, c0);
    out.colors[0] = c0;
    out.colors[1] = c1;
    out.gradient = {2.0F, r.w * 0.5F, r.h * 0.5F, repeats};
    return out;
}

// 四角半径：TL/TR/BR/BL
inline shader_data::Shape shape_per_corner(const rect_px &r, const view &v,
                                           const glm::vec4 &radii,
                                           const glm::vec4 &fill)
{
    auto out = sdf_shape(r, v, 0.0F, fill);
    out.radii_px = glm::max(radii, glm::vec4(0.0F));
    return out;
}

// 内阴影：软边框（border_soft_px）是 Shape 的参数
inline shader_data::Shape shape_inner_shadow(const rect_px &r, const view &v,
                                             float radius_px, const glm::vec4 &color,
                                             float border_px, float soft_px)
{
    auto out = shape_border(r, v, radius_px, color, border_px);
    out.border_soft_px = std::max(soft_px, 0.5F);
    return out;
}

// 椭圆：方形 SDF + 各向异性 model（SDF 仍是圆，被 model 拉成椭圆）
inline shader_data::Shape shape_ellipse(const rect_px &r, const view &v,
                                        const glm::vec4 &fill)
{
    const float m = std::min(std::max(r.w, 1.0F), std::max(r.h, 1.0F));
    auto out = sdf_shape(r, v, m * 0.5F, fill);
    out.px_size = {m, m};
    out.radii_px = glm::vec4(m * 0.5F);
    return out;
}

// 模糊形状：shadow 参数 + 0 偏移 + 0 填充 = 模糊剪影
inline shader_data::Shape shape_blurred(const rect_px &r, const view &v, float radius_px,
                                        const glm::vec4 &color, float blur_px)
{
    auto out = sdf_shape(r, v, radius_px, glm::vec4(0.0F), color, 0.0F, blur_px);
    out.effects = shader_data::FX_SHADOW;
    out.colors[0] = out.colors[1] = out.colors[2] = out.colors[3] = glm::vec4(0.0F);
    out.shadow_color = color;
    out.shadow_offset_px = {0.0F, 0.0F};
    return out;
}

// 程序化 alpha mask（Shape 参数）：dots=true 点阵，false checker
inline shader_data::Shape shape_alpha_mask(const rect_px &r, const view &v, float radius_px,
                                           const glm::vec4 &fill, float cell_px = 12.0F,
                                           bool dots = true)
{
    auto out = sdf_shape(r, v, radius_px, fill);
    const float cell = std::max(cell_px, 4.0F);
    out.maskFlags = 1U | (dots ? 2U : 0U);
    out.maskParams = {cell, cell};
    return out;
}

// ── 机制 2：path_fill ──────────────────────────────────────────────────────
inline shader_data::PathFill path_fill(const rect_px &r, const view &v,
                                       std::span<const glm::vec2> points,
                                       const glm::vec4 &fill,
                                       const glm::vec4 &fill_b = {},
                                       std::uint32_t entity = 0, float aa_width = 1.0F)
{
    shader_data::PathFill out{};
    out.model = model_for(r, v);
    out.px_size = {std::max(r.w, 1.0F), std::max(r.h, 1.0F)};
    out.color = fill;
    out.color_b = (fill_b.a > 0.0F) ? fill_b : fill;
    out.point_count = static_cast<std::uint32_t>(
        std::min(points.size(), static_cast<std::size_t>(shader_data::PATH_MAX_POINTS)));
    out.flags = 1; // color → color_b 纵向渐变
    out.aa_width = std::max(aa_width, 0.0F);
    for (std::uint32_t i = 0; i < out.point_count; ++i)
        out.points[i] = points[i];
    out.entity_index = entity;
    return out;
}

// ── 机制 3：path_stroke ───────────────────────────────────────────────────
inline shader_data::PathStroke path_stroke(const rect_px &r, const view &v,
                                           std::span<const glm::vec2> points,
                                           float width_px, const glm::vec4 &fill,
                                           const glm::vec4 &fill_b = {},
                                           std::uint32_t cap = 1,
                                           std::uint32_t entity = 0, float aa_width = 1.0F)
{
    shader_data::PathStroke out{};
    out.model = model_for(r, v);
    out.px_size = {std::max(r.w, 1.0F), std::max(r.h, 1.0F)};
    out.color = fill;
    out.color_b = (fill_b.a > 0.0F) ? fill_b : fill;
    out.width = std::max(width_px, 0.5F);
    out.point_count = static_cast<std::uint32_t>(
        std::min(points.size(), static_cast<std::size_t>(shader_data::PATH_MAX_POINTS)));
    out.cap = cap;
    out.aa_width = std::max(aa_width, 0.0F);
    for (std::uint32_t i = 0; i < out.point_count; ++i)
        out.points[i] = points[i];
    out.entity_index = entity;
    return out;
}

// ── 机制 4：texture ───────────────────────────────────────────────────────
inline shader_data::TexQuad texture(const rect_px &r, const view &v,
                                    std::uint32_t tex, std::uint32_t sampler,
                                    const glm::vec4 &tint = glm::vec4(1.0F),
                                    const UvTransform &uv = {},
                                    float radius_px = 0.0F,
                                    std::uint32_t entity = 0)
{
    shader_data::TexQuad out{};
    out.model = model_for(r, v);
    out.uvTransform = uv;
    out.color = tint;
    out.px_size = {std::max(r.w, 1.0F), std::max(r.h, 1.0F)};
    out.shape = {radius_px, 1.0F};
    out.textureIndex = tex;
    out.samplerIndex = sampler;
    out.opacity = 1.0F;
    out.entity_index = entity;
    return out;
}

// explicit mipmap lod（ability2 MODE_MIPMAP_TEX）
inline shader_data::TexQuad texture_lod(const rect_px &r, const view &v,
                                        std::uint32_t tex, std::uint32_t sampler, float lod,
                                        const glm::vec4 &tint = glm::vec4(1.0F),
                                        const UvTransform &uv = {},
                                        float radius_px = 0.0F, std::uint32_t entity = 0)
{
    auto out = texture(r, v, tex, sampler, tint, uv, radius_px, entity);
    out.lod = lod;
    out.lodMode = 1;
    return out;
}

// 纹理 + alpha 遮罩：maskFlags bit0；maskUv 把内容 UV 映射到遮罩 UV
inline shader_data::TexQuad texture_masked(const rect_px &r, const view &v,
                                           std::uint32_t tex, std::uint32_t sampler,
                                           std::uint32_t maskTex, std::uint32_t maskSampler,
                                           const UvTransform &maskUv,
                                           const glm::vec4 &tint = glm::vec4(1.0F),
                                           const UvTransform &uv = {},
                                           float radius_px = 0.0F,
                                           std::uint32_t entity = 0)
{
    auto out = texture(r, v, tex, sampler, tint, uv, radius_px, entity);
    out.maskTextureIndex = maskTex;
    out.maskSamplerIndex = maskSampler;
    out.maskUv = maskUv;
    out.maskFlags = 1U;
    return out;
}

// ── glyph metadata（texture 的变体，不是独立机制）──────────────────────
// 前端只做整形；几何/UV/纹理槽位由 DAO 直接编码成 TexQuad。
template <class ShapedGlyph>
inline shader_data::TexQuad glyph_from_shaped(const ShapedGlyph &g, const view &v,
                                              float fontSize_px, float cursor_x,
                                              float baseline_y, const glm::vec4 &tint,
                                              std::uint32_t entity = 0)
{
    const bool y_down = (g.font_ctx->font.atlas.yOrigin == "top");
    const float ydir = y_down ? 1.0F : -1.0F;
    const float ox = static_cast<float>(g.offset_x) * fontSize_px;
    const float oy = static_cast<float>(g.offset_y) * fontSize_px;
    const float left =
        cursor_x + ox + static_cast<float>(g.plane_bounds.left) * fontSize_px;
    const float right =
        cursor_x + ox + static_cast<float>(g.plane_bounds.right) * fontSize_px;
    const float top =
        baseline_y - oy + ydir * static_cast<float>(g.plane_bounds.top) * fontSize_px;
    const float bottom =
        baseline_y - oy + ydir * static_cast<float>(g.plane_bounds.bottom) * fontSize_px;

    UvTransform uv{};
    uv.scale = {static_cast<float>(g.uv_bounds.right - g.uv_bounds.left),
                static_cast<float>(g.uv_bounds.bottom - g.uv_bounds.top)};
    uv.offset = {static_cast<float>(g.uv_bounds.left),
                 static_cast<float>(g.uv_bounds.top)};

    return glyph_quad(rect_px{left, top, right - left, bottom - top}, v,
                      g.font_ctx->bind.texture_index, g.font_ctx->bind.sampler_index,
                      static_cast<std::uint32_t>(g.font_ctx->type),
                      static_cast<float>(g.font_ctx->font.atlas.distanceRange.value_or(0.0)),
                      tint, uv, entity);
}

// ── 机制 5：blur ──────────────────────────────────────────────────────────
inline shader_data::BlurQuad blur(const rect_px &r, const view &v,
                                  std::uint32_t tex, std::uint32_t sampler,
                                  float radius_texels, std::uint32_t taps,
                                  const glm::vec4 &tint = glm::vec4(1.0F),
                                  const UvTransform &uv = {},
                                  float radius_px = 0.0F,
                                  std::uint32_t entity = 0)
{
    shader_data::BlurQuad out{};
    out.model = model_for(r, v);
    out.uvTransform = uv;
    out.color = tint;
    out.px_size = {std::max(r.w, 1.0F), std::max(r.h, 1.0F)};
    out.shape = {radius_px, 1.0F};
    out.textureIndex = tex;
    out.samplerIndex = sampler;
    out.radius = std::max(radius_texels, 0.0F);
    out.taps = std::clamp(taps, 1U, 6U);
    out.entity_index = entity;
    return out;
}

// ── 机制 6：color_matrix ──────────────────────────────────────────────────
inline shader_data::ColorMatrix color_matrix(const rect_px &r, const view &v,
                                             std::uint32_t tex, std::uint32_t sampler,
                                             const glm::mat4 &matrix,
                                             const glm::vec4 &bias = {},
                                             const glm::vec4 &tint = glm::vec4(1.0F),
                                             const UvTransform &uv = {},
                                             float radius_px = 0.0F,
                                             std::uint32_t entity = 0)
{
    shader_data::ColorMatrix out{};
    out.model = model_for(r, v);
    out.uvTransform = uv;
    out.color = tint;
    out.matrix = matrix;
    out.offset = bias;
    out.px_size = {std::max(r.w, 1.0F), std::max(r.h, 1.0F)};
    out.textureIndex = tex;
    out.samplerIndex = sampler;
    out.shape = {radius_px, 1.0F};
    out.opacity = 1.0F;
    out.entity_index = entity;
    return out;
}

// ── 机制 7：noise ─────────────────────────────────────────────────────────
inline shader_data::NoiseQuad noise(const rect_px &r, const view &v,
                                    const glm::vec4 &color_a, const glm::vec4 &color_b,
                                    float frequency, float seed,
                                    std::uint32_t octaves = 4,
                                    float radius_px = 0.0F,
                                    std::uint32_t entity = 0)
{
    shader_data::NoiseQuad out{};
    out.model = model_for(r, v);
    out.color_a = color_a;
    out.color_b = color_b;
    out.px_size = {std::max(r.w, 1.0F), std::max(r.h, 1.0F)};
    out.shape = {radius_px, 1.0F};
    out.frequency = frequency;
    out.seed = seed;
    out.octaves = std::clamp(octaves, 1U, 6U);
    out.flags = 1; // fbm
    out.entity_index = entity;
    return out;
}

} // namespace ui_dao
