#pragma once
#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

namespace shader_data
{
    struct PushData
    {
        std::uint64_t vertexAddress{0};
        std::uint64_t instanceAddress{0}; // GLSL: dataAddress
        std::uint64_t commandConstantsAddress{0};
        std::uint32_t cameraIndex{0};   // 0: 3D, 1: UI
        std::uint32_t renderVersion{0}; // 与 gl_PrimitiveID 打包

        bool operator==(const PushData &) const = default;
        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return vertexAddress != 0 && instanceAddress != 0 &&
                   commandConstantsAddress != 0;
        }
    };
    static_assert(sizeof(PushData) == 32);

    // ── 协议：7 个机制程序，密集 0..6 ────────────────────────────
    //   shape        SDF coverage：fill / border / shadow 都是它的参数
    //   path_fill    路径谓词 coverage
    //   path_stroke  路径距离 coverage
    //   texture      纹理查表（glyph 是它的 metadata）
    //   blur         邻域卷积
    //   color_matrix 逐点矩阵
    //   noise        程序化
    // 没有 UiRect / Mesh 支撑类型：边框是 shape 的参数，几何始终是 quad。
    enum class Program : std::uint32_t
    {
        Shape = 0,
        PathFill = 1,
        PathStroke = 2,
        Texture = 3,
        Blur = 4,
        ColorMatrix = 5,
        Noise = 6,
    };

    inline constexpr std::uint32_t SHAPE_SIZE = 248;
    inline constexpr std::uint32_t PATH_MAX_POINTS = 16;
    inline constexpr std::uint32_t PATH_FILL_SIZE = 252;
    inline constexpr std::uint32_t PATH_STROKE_SIZE = 256;
    inline constexpr std::uint32_t TEX_QUAD_SIZE = 180;
    inline constexpr std::uint32_t BLUR_QUAD_SIZE = 136;
    inline constexpr std::uint32_t COLOR_MATRIX_SIZE = 212;
    inline constexpr std::uint32_t NOISE_QUAD_SIZE = 136;
    inline constexpr std::uint32_t COMMAND_CONSTANT_SIZE = 8;

    [[nodiscard]] constexpr std::uint32_t sizeOf(Program program) noexcept
    {
        switch (program)
        {
        case Program::Shape:
            return SHAPE_SIZE;
        case Program::PathFill:
            return PATH_FILL_SIZE;
        case Program::PathStroke:
            return PATH_STROKE_SIZE;
        case Program::Texture:
            return TEX_QUAD_SIZE;
        case Program::Blur:
            return BLUR_QUAD_SIZE;
        case Program::ColorMatrix:
            return COLOR_MATRIX_SIZE;
        case Program::Noise:
            return NOISE_QUAD_SIZE;
        }
        return 0;
    }

    // ── 机制 0：Shape（SDF；fill/border/shadow/gradient/四角半径都是参数）
    //     SDF 在像素空间算，border/radius/blur/gradient 都是像素，各向同性。
    struct Shape
    {
        std::uint32_t entity_index{0};
        std::uint32_t hover_fn{~0U};
        std::uint32_t effects{0}; // FX_FILL | FX_BORDER | FX_SHADOW
        glm::vec4 colors[4]{};    // fill（四顶点；linear 渐变用）
        glm::vec4 border_color{1.0F};
        glm::mat4 model{1.0F};
        glm::vec2 px_size{1.0F};
        glm::vec4 radii_px{0.0F}; // TL/TR/BR/BL
        float border_px{0.0F};
        float border_soft_px{0.5F};
        glm::vec4 gradient{
            0.0F}; // x=mode(0 per-vertex,1 radial,2 conic), yz=center px, w=radius/repeats
        glm::vec2 shadow_offset_px{0.0F};
        float blur_px{0.0F};
        float spread{1.0F};
        glm::vec4 shadow_color{0.0F};
        glm::vec2 maskParams{8.0F, 8.0F}; // checker 频率
        std::uint32_t maskFlags{0};       // bit0: 程序化 checker alpha mask
    };

    inline constexpr std::uint32_t FX_FILL = 1U;
    inline constexpr std::uint32_t FX_BORDER = 2U;
    inline constexpr std::uint32_t FX_SHADOW = 4U;

    // ── 机制 1：PathFill（type 1；多边形填充，点坐标 [0,1]²）
    struct PathFill
    {
        std::uint32_t entity_index{0};
        std::uint32_t hover_fn{~0U};
        glm::mat4 model{1.0F};
        glm::vec4 color{1.0F};
        glm::vec4 color_b{1.0F};
        glm::vec2 px_size{1.0F};
        std::uint32_t point_count{0};
        std::uint32_t flags{0}; // bit0: color→color_b 渐变
        float aa_width{1.0F};   // 0 = 硬边（ability2 MODE_PATH_AA 的 off）
        glm::vec2 points[PATH_MAX_POINTS]{};
    };

    // ── 机制 2：PathStroke（type 2；折线描边，距离场）
    struct PathStroke
    {
        std::uint32_t entity_index{0};
        std::uint32_t hover_fn{~0U};
        glm::mat4 model{1.0F};
        glm::vec4 color{1.0F};
        glm::vec4 color_b{1.0F};
        glm::vec2 px_size{1.0F};
        float width{1.0F};
        std::uint32_t point_count{0};
        std::uint32_t cap{1}; // 0=butt, 1=round（join 恒为圆角）
        float aa_width{1.0F}; // 0 = 硬边
        glm::vec2 points[PATH_MAX_POINTS]{};
    };

    // ── 机制 3：TexQuad（type 3；纹理查表，glyph 是 metadata）
    struct TexQuad
    {
        std::uint32_t entity_index{0};
        std::uint32_t hover_fn{~0U};
        glm::mat4 model{1.0F};
        UvTransform uvTransform{};
        glm::vec4 color{1.0F};
        glm::vec2 px_size{1.0F};
        glm::vec2 shape{0.0F}; // (圆角px, 边缘px)
        std::uint32_t textureIndex{0};
        std::uint32_t samplerIndex{0};
        float opacity{1.0F};
        std::uint32_t glyph_mode{0};  // FONT_*；仅 glyph_flags bit1 有效
        float px_range{0.0F};         // MSDF 距离场范围
        std::uint32_t glyph_flags{0}; // bit0: modulate；bit1: is-glyph
        UvTransform maskUv{};         // 遮罩纹理 UV 变换（maskFlags bit0）
        std::uint32_t maskTextureIndex{0};
        std::uint32_t maskSamplerIndex{0};
        std::uint32_t maskFlags{0}; // bit0: alpha mask
        float lod{0.0F};            // explicit mipmap lod
        std::uint32_t lodMode{0};   // 0 = implicit, 1 = textureLod
    };

    // ── 机制 4：Blur（type 4；邻域卷积）
    struct BlurQuad
    {
        std::uint32_t entity_index{0};
        std::uint32_t hover_fn{~0U};
        glm::mat4 model{1.0F};
        UvTransform uvTransform{};
        glm::vec4 color{1.0F};
        glm::vec2 px_size{1.0F};
        glm::vec2 shape{0.0F};
        std::uint32_t textureIndex{0};
        std::uint32_t samplerIndex{0};
        float radius{8.0F};
        std::uint32_t taps{3};
    };

    // ── 机制 5：ColorMatrix（type 5；逐点矩阵）
    struct ColorMatrix
    {
        std::uint32_t entity_index{0};
        std::uint32_t hover_fn{~0U};
        glm::mat4 model{1.0F};
        UvTransform uvTransform{};
        glm::vec4 color{1.0F};
        glm::mat4 matrix{1.0F};
        glm::vec4 offset{0.0F};
        glm::vec2 px_size{1.0F};
        std::uint32_t textureIndex{0};
        std::uint32_t samplerIndex{0};
        glm::vec2 shape{0.0F};
        float opacity{1.0F};
    };

    // ── 机制 6：Noise（type 6；程序化）
    struct NoiseQuad
    {
        std::uint32_t entity_index{0};
        std::uint32_t hover_fn{~0U};
        glm::mat4 model{1.0F};
        glm::vec4 color_a{1.0F};
        glm::vec4 color_b{0.0F, 0.0F, 0.0F, 1.0F};
        glm::vec2 px_size{1.0F};
        glm::vec2 shape{0.0F};
        float frequency{6.0F};
        float seed{0.0F};
        std::uint32_t octaves{4};
        std::uint32_t flags{1}; // bit0: fbm
    };

    // ── 每条 indirect draw 一条：program + 堆偏移（8 B）
    struct CommandConstant
    {
        std::uint32_t program{0};
        std::uint32_t address_offset{0};
    };
    static_assert(sizeof(CommandConstant) == COMMAND_CONSTANT_SIZE);

    static_assert(sizeof(Shape) == SHAPE_SIZE);
    static_assert(sizeof(PathFill) == PATH_FILL_SIZE);
    static_assert(sizeof(PathStroke) == PATH_STROKE_SIZE);
    static_assert(sizeof(TexQuad) == TEX_QUAD_SIZE);
    static_assert(sizeof(BlurQuad) == BLUR_QUAD_SIZE);
    static_assert(sizeof(ColorMatrix) == COLOR_MATRIX_SIZE);
    static_assert(sizeof(NoiseQuad) == NOISE_QUAD_SIZE);

    static_assert(offsetof(Shape, effects) == 8);
    static_assert(offsetof(Shape, colors) == 12);
    static_assert(offsetof(Shape, border_color) == 76);
    static_assert(offsetof(Shape, model) == 92);
    static_assert(offsetof(Shape, px_size) == 156);
    static_assert(offsetof(Shape, radii_px) == 164);
    static_assert(offsetof(Shape, border_px) == 180);
    static_assert(offsetof(Shape, border_soft_px) == 184);
    static_assert(offsetof(Shape, gradient) == 188);
    static_assert(offsetof(Shape, shadow_offset_px) == 204);
    static_assert(offsetof(Shape, blur_px) == 212);
    static_assert(offsetof(Shape, spread) == 216);
    static_assert(offsetof(Shape, shadow_color) == 220);
    static_assert(offsetof(Shape, maskParams) == 236);
    static_assert(offsetof(Shape, maskFlags) == 244);

    static_assert(offsetof(PathFill, model) == 8);
    static_assert(offsetof(PathFill, color) == 72);
    static_assert(offsetof(PathFill, color_b) == 88);
    static_assert(offsetof(PathFill, px_size) == 104);
    static_assert(offsetof(PathFill, point_count) == 112);
    static_assert(offsetof(PathFill, flags) == 116);
    static_assert(offsetof(PathFill, aa_width) == 120);
    static_assert(offsetof(PathFill, points) == 124);

    static_assert(offsetof(PathStroke, model) == 8);
    static_assert(offsetof(PathStroke, color) == 72);
    static_assert(offsetof(PathStroke, color_b) == 88);
    static_assert(offsetof(PathStroke, px_size) == 104);
    static_assert(offsetof(PathStroke, width) == 112);
    static_assert(offsetof(PathStroke, point_count) == 116);
    static_assert(offsetof(PathStroke, cap) == 120);
    static_assert(offsetof(PathStroke, aa_width) == 124);
    static_assert(offsetof(PathStroke, points) == 128);

    static_assert(offsetof(TexQuad, model) == 8);
    static_assert(offsetof(TexQuad, uvTransform) == 72);
    static_assert(offsetof(TexQuad, color) == 88);
    static_assert(offsetof(TexQuad, px_size) == 104);
    static_assert(offsetof(TexQuad, shape) == 112);
    static_assert(offsetof(TexQuad, textureIndex) == 120);
    static_assert(offsetof(TexQuad, samplerIndex) == 124);
    static_assert(offsetof(TexQuad, opacity) == 128);
    static_assert(offsetof(TexQuad, glyph_mode) == 132);
    static_assert(offsetof(TexQuad, px_range) == 136);
    static_assert(offsetof(TexQuad, glyph_flags) == 140);
    static_assert(offsetof(TexQuad, maskUv) == 144);
    static_assert(offsetof(TexQuad, maskTextureIndex) == 160);
    static_assert(offsetof(TexQuad, maskSamplerIndex) == 164);
    static_assert(offsetof(TexQuad, maskFlags) == 168);
    static_assert(offsetof(TexQuad, lod) == 172);
    static_assert(offsetof(TexQuad, lodMode) == 176);

    static_assert(offsetof(BlurQuad, model) == 8);
    static_assert(offsetof(BlurQuad, uvTransform) == 72);
    static_assert(offsetof(BlurQuad, color) == 88);
    static_assert(offsetof(BlurQuad, px_size) == 104);
    static_assert(offsetof(BlurQuad, shape) == 112);
    static_assert(offsetof(BlurQuad, textureIndex) == 120);
    static_assert(offsetof(BlurQuad, samplerIndex) == 124);
    static_assert(offsetof(BlurQuad, radius) == 128);
    static_assert(offsetof(BlurQuad, taps) == 132);

    static_assert(offsetof(ColorMatrix, model) == 8);
    static_assert(offsetof(ColorMatrix, uvTransform) == 72);
    static_assert(offsetof(ColorMatrix, color) == 88);
    static_assert(offsetof(ColorMatrix, matrix) == 104);
    static_assert(offsetof(ColorMatrix, offset) == 168);
    static_assert(offsetof(ColorMatrix, px_size) == 184);
    static_assert(offsetof(ColorMatrix, textureIndex) == 192);
    static_assert(offsetof(ColorMatrix, samplerIndex) == 196);
    static_assert(offsetof(ColorMatrix, shape) == 200);
    static_assert(offsetof(ColorMatrix, opacity) == 208);

    static_assert(offsetof(NoiseQuad, model) == 8);
    static_assert(offsetof(NoiseQuad, color_a) == 72);
    static_assert(offsetof(NoiseQuad, color_b) == 88);
    static_assert(offsetof(NoiseQuad, px_size) == 104);
    static_assert(offsetof(NoiseQuad, shape) == 112);
    static_assert(offsetof(NoiseQuad, frequency) == 120);
    static_assert(offsetof(NoiseQuad, seed) == 124);
    static_assert(offsetof(NoiseQuad, octaves) == 128);
    static_assert(offsetof(NoiseQuad, flags) == 132);

    static_assert(static_cast<std::uint32_t>(Program::Shape) == 0);
    static_assert(static_cast<std::uint32_t>(Program::PathFill) == 1);
    static_assert(static_cast<std::uint32_t>(Program::PathStroke) == 2);
    static_assert(static_cast<std::uint32_t>(Program::Texture) == 3);
    static_assert(static_cast<std::uint32_t>(Program::Blur) == 4);
    static_assert(static_cast<std::uint32_t>(Program::ColorMatrix) == 5);
    static_assert(static_cast<std::uint32_t>(Program::Noise) == 6);

} // namespace shader_data
