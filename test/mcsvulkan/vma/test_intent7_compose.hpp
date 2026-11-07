#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// 组合层：把 7 个基础 program 组合成更高层图元。
//
// 这里没有任何 shader program，也没有新的 Instance 类型；
// 每个函数只是向 CommandBuilder 追加若干现有实例：
//
//   linear/radial/conic   = Shape 的 gradient 参数
//   per_corner            = Shape 的四角半径参数
//   ring                  = Shape border（圆角=半径一半）
//   arc / dash            = PathStroke 的多段组合
//   triangle / star       = PathFill 的点集
//   nine_slice            = 9 个 Texture
//   masked                = Texture + mask 元数据
//   inner_shadow          = Shape 的软边框（+ 模糊剪影）
//   rotated / skewed      = Shape 的 model（仿射）
//   composed_ui           = Shape + Path + Glyph 的实例组合
//
// 依赖：调用处已经可见 ui_dao / shader_data::CommandBuilder / glm。
// ═══════════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ui_compose
{

using ui_dao::rect_px;
using ui_dao::view;

// 屏幕像素空间的仿射：local[-0.5,0.5]² → px → 旋转/斜切 → NDC
inline glm::mat4 transformed_model(const rect_px &r, const view &v, float angle_rad,
                                   float shear)
{
    const glm::mat4 to_ndc =
        glm::translate(glm::mat4(1.0F), glm::vec3(-1.0F, -1.0F, 0.0F)) *
        glm::scale(glm::mat4(1.0F), glm::vec3(2.0F / v.w, 2.0F / v.h, 1.0F));
    const glm::mat4 px = glm::translate(
        glm::mat4(1.0F), glm::vec3(r.x + r.w * 0.5F, r.y + r.h * 0.5F, 0.0F));
    const glm::mat4 rot =
        glm::rotate(glm::mat4(1.0F), angle_rad, glm::vec3(0.0F, 0.0F, 1.0F));
    glm::mat4 sh(1.0F);
    sh[1][0] = shear;
    const glm::mat4 sc = glm::scale(glm::mat4(1.0F), glm::vec3(r.w, r.h, 1.0F));
    return to_ndc * px * rot * sh * sc;
}

// ── Shape 的 gradient / 四角半径参数 ─────────────────────────────────────
inline void linear(shader_data::CommandBuilder &b, const rect_px &r, const view &v,
                   const glm::vec4 &c0, const glm::vec4 &c1, bool horizontal = true,
                   float radius = 8.0F)
{
    b.addInstance(ui_dao::sdf_linear(r, v, radius, c0, c1, horizontal));
}

inline void radial(shader_data::CommandBuilder &b, const rect_px &r, const view &v,
                   const glm::vec4 &inner, const glm::vec4 &outer, float radius = 8.0F)
{
    b.addInstance(ui_dao::sdf_radial(r, v, radius, inner, outer));
}

inline void conic(shader_data::CommandBuilder &b, const rect_px &r, const view &v,
                  const glm::vec4 &c0, const glm::vec4 &c1, float repeats = 1.0F,
                  float radius = 8.0F)
{
    b.addInstance(ui_dao::sdf_conic(r, v, radius, c0, c1, repeats));
}

inline void per_corner(shader_data::CommandBuilder &b, const rect_px &r, const view &v,
                       const glm::vec4 &radii, const glm::vec4 &fill)
{
    b.addInstance(ui_dao::shape_per_corner(r, v, radii, fill));
}

// ── ring：Shape border，圆角 = 半径一半，就是一个圆环 ───────────────────
inline void ring(shader_data::CommandBuilder &b, const rect_px &r, const view &v,
                 const glm::vec4 &color, float thickness, float scale = 0.72F)
{
    const float m = std::min(r.w, r.h) * scale;
    const rect_px sq{r.x + (r.w - m) * 0.5F, r.y + (r.h - m) * 0.5F, m, m};
    b.addInstance(ui_dao::shape_border(sq, v, m * 0.5F, color, thickness));
}

// ── arc：PathStroke 采样（16 段折线 = 一个弧）────────────────────────────
inline void arc(shader_data::CommandBuilder &b, const rect_px &r, const view &v,
                const glm::vec4 &color, float width = 6.0F, float sweep = 4.4F)
{
    std::array<glm::vec2, 16> pts{};
    const float cx = r.x + r.w * 0.5F;
    const float cy = r.y + r.h * 0.5F;
    const float rad = std::min(r.w, r.h) * 0.36F;
    for (int i = 0; i < 16; ++i)
    {
        const float a = -sweep * 0.5F + float(i) * (sweep / 15.0F);
        pts[static_cast<std::size_t>(i)] = {(cx + std::cos(a) * rad - r.x) / r.w,
                                            (cy + std::sin(a) * rad - r.y) / r.h};
    }
    b.addInstance(ui_dao::path_stroke(r, v, pts, width, color));
}

// ── dash：5 个 PathStroke 小段 = 一条虚线 ────────────────────────────────
inline void dash(shader_data::CommandBuilder &b, const rect_px &r, const view &v,
                 const glm::vec4 &color, float width = 5.0F)
{
    const float y = r.y + r.h * 0.5F;
    for (int i = 0; i < 5; ++i)
    {
        const float x0 = r.x + r.w * (0.08F + float(i) * 0.18F);
        const float x1 = x0 + r.w * 0.10F;
        std::array<glm::vec2, 2> pts{{{(x0 - r.x) / r.w, (y - r.y) / r.h},
                                      {(x1 - r.x) / r.w, (y - r.y) / r.h}}};
        b.addInstance(ui_dao::path_stroke(r, v, pts, width, color));
    }
}

// ── triangle / star：PathFill 的点集 ─────────────────────────────────────
inline void triangle(shader_data::CommandBuilder &b, const rect_px &r, const view &v,
                     const glm::vec4 &c0, const glm::vec4 &c1, float aa_width = 1.0F)
{
    std::array<glm::vec2, 3> pts{{{0.5F, 0.10F}, {0.12F, 0.90F}, {0.88F, 0.90F}}};
    b.addInstance(ui_dao::path_fill(r, v, pts, c0, c1, 0, aa_width));
}

inline void star(shader_data::CommandBuilder &b, const rect_px &r, const view &v,
                 const glm::vec4 &c0, const glm::vec4 &c1, float aa_width = 1.0F)
{
    std::array<glm::vec2, 10> pts{};
    for (int k = 0; k < 10; ++k)
    {
        const float a = -1.5707963F + float(k) * 0.62831853F;
        const float rr = (k % 2 == 0) ? 0.40F : 0.17F;
        pts[static_cast<std::size_t>(k)] = {0.5F + std::cos(a) * rr,
                                            0.52F + std::sin(a) * rr};
    }
    b.addInstance(ui_dao::path_fill(r, v, pts, c0, c1, 0, aa_width));
}

// ── rotated / skewed：Shape 的 model（仿射）──────────────────────────────
inline void rotated(shader_data::CommandBuilder &b, const rect_px &r, const view &v,
                    const glm::vec4 &fill, float angle, float radius = 8.0F)
{
    auto s = ui_dao::sdf_shape(r, v, radius, fill);
    s.model = transformed_model(r, v, angle, 0.0F);
    b.addInstance(s);
}

inline void skewed(shader_data::CommandBuilder &b, const rect_px &r, const view &v,
                   const glm::vec4 &fill, float shear, float radius = 8.0F)
{
    auto s = ui_dao::sdf_shape(r, v, radius, fill);
    s.model = transformed_model(r, v, 0.0F, shear);
    b.addInstance(s);
}

// ── nine_slice_color：程序化 3×3 颜色九宫格（ability2 MODE_NINE_SLICE）──
inline void nine_slice_color(shader_data::CommandBuilder &b, const rect_px &r,
                             const view &v, float border_px = 2.0F, float radius = 8.0F)
{
    static const glm::vec4 pal[9] = {
        {0.90F, 0.20F, 0.20F, 1.0F}, {0.20F, 0.80F, 0.30F, 1.0F},
        {0.20F, 0.40F, 0.90F, 1.0F}, {0.90F, 0.80F, 0.20F, 1.0F},
        {0.95F, 0.95F, 0.95F, 1.0F}, {0.20F, 0.85F, 0.85F, 1.0F},
        {0.80F, 0.30F, 0.85F, 1.0F}, {0.95F, 0.55F, 0.20F, 1.0F},
        {0.55F, 0.55F, 0.55F, 1.0F}};
    const float cw = r.w / 3.0F;
    const float ch = r.h / 3.0F;
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i)
            b.addInstance(ui_dao::sdf_shape(
                {r.x + float(i) * cw, r.y + float(j) * ch, cw, ch}, v, 0.0F,
                pal[j * 3 + i]));
    b.addInstance(ui_dao::shape_border(r, v, radius,
                                       glm::vec4{0.08F, 0.08F, 0.10F, 1.0F}, border_px));
}

// ── nine_slice：9 个 Texture 子矩形（3×3）────────────────────────────────
inline void nine_slice(shader_data::CommandBuilder &b, const rect_px &r, const view &v,
                       std::uint32_t tex, std::uint32_t sampler, float src = 0.2F,
                       float dst = 14.0F)
{
    const float u[4] = {0.0F, src, 1.0F - src, 1.0F};
    const float x[4] = {r.x, r.x + dst, r.x + r.w - dst, r.x + r.w};
    const float y[4] = {r.y, r.y + dst, r.y + r.h - dst, r.y + r.h};
    for (int j = 0; j < 3; ++j)
    {
        for (int i = 0; i < 3; ++i)
        {
            const rect_px cell{x[i], y[j], x[i + 1] - x[i], y[j + 1] - y[j]};
            UvTransform uv;
            uv.scale = {u[i + 1] - u[i], u[j + 1] - u[j]};
            uv.offset = {u[i], u[j]};
            b.addInstance(ui_dao::texture(cell, v, tex, sampler, glm::vec4(1.0F), uv));
        }
    }
}

// ── alpha_mask：Shape 的程序化点阵/棋盘 alpha（ability2 MODE_ALPHA_MASK）──
inline void alpha_mask(shader_data::CommandBuilder &b, const rect_px &r, const view &v,
                       const glm::vec4 &fill, float radius = 8.0F, float cell = 12.0F,
                       bool dots = true)
{
    b.addInstance(ui_dao::shape_alpha_mask(r, v, radius, fill, cell, dots));
}

// ── masked：Texture + 第二个 texture 的 alpha 作为 mask ─────────────────
inline void masked(shader_data::CommandBuilder &b, const rect_px &r, const view &v,
                   std::uint32_t tex, std::uint32_t sampler, std::uint32_t maskTex,
                   std::uint32_t maskSampler, const UvTransform &maskUv,
                   float radius = 6.0F)
{
    b.addInstance(ui_dao::texture_masked(r, v, tex, sampler, maskTex, maskSampler, maskUv,
                                         glm::vec4(1.0F), UvTransform{}, radius));
}

// ── inner_shadow：模糊剪影 + Shape 软边框（border_soft_px）──────────────
inline void inner_shadow(shader_data::CommandBuilder &b, const rect_px &r, const view &v,
                         const glm::vec4 &fill, const glm::vec4 &shadow,
                         const glm::vec4 &inner, float blur = 9.0F,
                         float border = 16.0F, float soft = 7.0F, float s = 1.0F)
{
    b.addInstance(ui_dao::shape_blurred(r, v, 10.0F * s, shadow, blur));
    const rect_px card{r.x + 18.0F * s, r.y + 12.0F * s, r.w - 36.0F * s, r.h - 24.0F * s};
    b.addInstance(ui_dao::sdf_shape(card, v, 8.0F * s, fill));
    b.addInstance(ui_dao::shape_inner_shadow(card, v, 8.0F * s, inner, border, soft));
}

// ── composed_ui：Shape + Path + Glyph 的实例组合（一个 mini table）──────
inline void composed_ui(shader_data::CommandBuilder &b, const rect_px &wide,
                        const view &v, const glm::vec4 &accent,
                        const glm::vec4 &rowBg, const glm::vec4 &green,
                        const glm::vec4 &orange, const glm::vec4 &dim,
                        std::uint32_t checkerTex, std::uint32_t checkerSampler, float s = 1.0F)
{
    const rect_px head{wide.x + 6.0F * s, wide.y + 20.0F * s, wide.w - 12.0F * s, 22.0F * s};
    b.addInstance(ui_dao::shape_per_corner(head, v, glm::vec4{10.0F * s, 10.0F * s, 0.0F, 0.0F},
                                           accent));
    // SELECT TEXT 草稿：selection 是 Shape，文本是 glyph；范围选择属于 CPU 布局
    b.addInstance(ui_dao::sdf_shape({wide.x + 176.0F * s, wide.y + 60.0F * s,
                                     wide.w - 188.0F * s, 12.0F * s},
                                    v, 3.0F * s, glm::vec4{0.33F, 0.68F, 1.0F, 0.22F}));
    // MODE_CHECKER：棋盘内容 = Texture 采样棋盘纹理（数据，不是新 program）
    b.addInstance(ui_dao::texture({wide.x + wide.w - 26.0F * s, wide.y + 24.0F * s, 16.0F * s,
                                   16.0F * s},
                                  v, checkerTex, checkerSampler, glm::vec4(1.0F), UvTransform{},
                                  3.0F * s));
    for (int i = 0; i < 3; ++i)
    {
        const float y = wide.y + 48.0F * s + float(i) * 22.0F * s;
        const rect_px row{wide.x + 10.0F * s, y, wide.w - 190.0F * s, 18.0F * s};
        const glm::vec4 rowFill =
            (i == 1) ? glm::vec4{0.19F, 0.31F, 0.44F, 0.95F} : rowBg; // 选中行
        b.addInstance(ui_dao::sdf_shape(row, v, 4.0F * s, rowFill));
        b.addInstance(ui_dao::shape_ellipse(
            {wide.x + 16.0F * s, y + 4.0F * s, 10.0F * s, 10.0F * s}, v,
            (i == 1) ? orange : green));
        b.addInstance(ui_dao::sdf_shape(
            {wide.x + 32.0F * s, y + 8.5F * s, wide.w - 250.0F * s, 1.0F * s}, v, 0.0F,
            dim));
    }
}

} // namespace ui_compose
