// ═══════════════════════════════════════════════════════════════════════════
//
// 这里只放「两个阶段必须一致」的常量与定义：
//   §0 program id      —— CommandConstant::program，两边 switch 共用
//   §1 命令流契约       —— CommandConstant{program, address_offset}（8 B）
//   §2 公共子结构       —— UvTransform
//   §3 实例布局         —— 7 个机制程序：SHAPE/…/NOISE_SIZE + struct + buffer
//   §4 每帧输入         —— push constant
//   §5 拾取打包
//
// 布局约定：scalar block layout；实例行只保留 shader 真正读的字段。
// ═══════════════════════════════════════════════════════════════════════════

#ifndef TEST_INTENT6_ABI_GLSL
#define TEST_INTENT6_ABI_GLSL

// ───────────────────────────────────────────────────────────────────────────
// §0 program id：7 个机制程序，密集 0..6
//    shape / path_fill / path_stroke / texture / blur / color_matrix / noise
//    glyph 是 texture 的 metadata；border 是 shape 的参数；没有 UiRect/Mesh。
// ───────────────────────────────────────────────────────────────────────────
const uint PROGRAM_SHAPE = 0u;
const uint PROGRAM_PATH_FILL = 1u;
const uint PROGRAM_PATH_STROKE = 2u;
const uint PROGRAM_TEXTURE = 3u;
const uint PROGRAM_BLUR = 4u;
const uint PROGRAM_COLOR_MATRIX = 5u;
const uint PROGRAM_NOISE = 6u;

// ───────────────────────────────────────────────────────────────────────────
// §1 命令流契约：indirect draw 一条对应一条命令
// ───────────────────────────────────────────────────────────────────────────
struct CommandConstant
{
    uint program;        // §0
    uint address_offset; // 相对实例堆基址
};

// ───────────────────────────────────────────────────────────────────────────
// §2 公共子结构
// ───────────────────────────────────────────────────────────────────────────
struct UvTransform
{
    vec2 scale;
    vec2 offset;
};

// ───────────────────────────────────────────────────────────────────────────
// §3 实例布局（按 program id 升序）
// ───────────────────────────────────────────────────────────────────────────

// ── 3.0 机制 0：Shape（SDF；fill / border / shadow 是参数）─────────────────
// FX_* 位
const uint FX_FILL = 1u;
const uint FX_BORDER = 2u;
const uint FX_SHADOW = 4u;

#define SHAPE_SIZE 248
struct Shape
{
    uint entity_index;
    uint hover_fn;
    uint effects;           // FX_FILL | FX_BORDER | FX_SHADOW
    vec4 colors[4];         // fill（四顶点）
    vec4 border_color;
    mat4 model;
    vec2 px_size;           // quad 像素尺寸
    vec4 radii_px;          // TL/TR/BR/BL
    float border_px;
    float border_soft_px;
    vec4 gradient;          // x=mode(0 per-vertex,1 radial,2 conic), yz=center px, w=radius/repeats
    vec2 shadow_offset_px;
    float blur_px;
    float spread;
    vec4 shadow_color;
    vec2 maskParams;  // checker 频率
    uint maskFlags;   // bit0: 程序化 checker alpha mask
};
layout(buffer_reference, scalar) readonly buffer ShapeBuffer
{
    Shape shapes[];
};

// ── 3.1 机制 1：PathFill（type 1）──────────────────────────────────────────
#define PATH_MAX_POINTS 16
#define PATH_FILL_SIZE 252
struct PathFill
{
    uint entity_index;
    uint hover_fn;
    mat4 model;
    vec4 color;
    vec4 color_b;
    vec2 px_size;
    uint point_count;
    uint flags; // bit0: color→color_b
    float aa_width; // 0 = 硬边
    vec2 points[PATH_MAX_POINTS];
};
layout(buffer_reference, scalar) readonly buffer PathFillBuffer
{
    PathFill fills[];
};

// ── 3.2 机制 2：PathStroke（type 2）────────────────────────────────────────
#define PATH_STROKE_SIZE 256
struct PathStroke
{
    uint entity_index;
    uint hover_fn;
    mat4 model;
    vec4 color;
    vec4 color_b;
    vec2 px_size;
    float width;
    uint point_count;
    uint cap; // 0=butt, 1=round（join 恒为圆角）
    float aa_width; // 0 = 硬边
    vec2 points[PATH_MAX_POINTS];
};
layout(buffer_reference, scalar) readonly buffer PathStrokeBuffer
{
    PathStroke strokes[];
};

// ── 3.3 机制 3：TexQuad（type 3；glyph metadata 也走这里）──────────────────
const int FONT_HARD_MASK = 0;
const int FONT_SOFT_MASK = 1;
const int FONT_SDF = 2;
const int FONT_PSDF = 3;
const int FONT_MSDF = 4;
const int FONT_MTSDF = 5;
const int FONT_BITMAP = 6;
const int FONT_NONE = 7;

#define TEX_QUAD_SIZE 180
struct TexQuad
{
    uint entity_index;
    uint hover_fn;
    mat4 model;
    UvTransform uvTransform;
    vec4 color;
    vec2 px_size;
    vec2 shape; // (圆角px, 边缘px)
    uint textureIndex;
    uint samplerIndex;
    float opacity;
    uint glyph_mode;  // FONT_*；仅 glyph_flags bit1 有效
    float px_range;
    uint glyph_flags; // bit0: modulate；bit1: is-glyph
    UvTransform maskUv;
    uint maskTextureIndex;
    uint maskSamplerIndex;
    uint maskFlags;   // bit0: alpha mask
    float lod;        // explicit mipmap lod
    uint lodMode;     // 0 = implicit, 1 = textureLod
};
layout(buffer_reference, scalar) readonly buffer TexQuadBuffer
{
    TexQuad quads[];
};

// ── 3.4 机制 4：BlurQuad（type 4）──────────────────────────────────────────
#define BLUR_QUAD_SIZE 136
struct BlurQuad
{
    uint entity_index;
    uint hover_fn;
    mat4 model;
    UvTransform uvTransform;
    vec4 color;
    vec2 px_size;
    vec2 shape;
    uint textureIndex;
    uint samplerIndex;
    float radius;
    uint taps;
};
layout(buffer_reference, scalar) readonly buffer BlurQuadBuffer
{
    BlurQuad quads[];
};

// ── 3.5 机制 5：ColorMatrix（type 5）───────────────────────────────────────
#define COLOR_MATRIX_SIZE 212
struct ColorMatrix
{
    uint entity_index;
    uint hover_fn;
    mat4 model;
    UvTransform uvTransform;
    vec4 color;
    mat4 matrix;
    vec4 offset;
    vec2 px_size;
    uint textureIndex;
    uint samplerIndex;
    vec2 shape;
    float opacity;
};
layout(buffer_reference, scalar) readonly buffer ColorMatrixBuffer
{
    ColorMatrix quads[];
};

// ── 3.6 机制 6：NoiseQuad（type 6）─────────────────────────────────────────
#define NOISE_QUAD_SIZE 136
struct NoiseQuad
{
    uint entity_index;
    uint hover_fn;
    mat4 model;
    vec4 color_a;
    vec4 color_b;
    vec2 px_size;
    vec2 shape;
    float frequency;
    float seed;
    uint octaves;
    uint flags; // bit0: fbm
};
layout(buffer_reference, scalar) readonly buffer NoiseQuadBuffer
{
    NoiseQuad quads[];
};

// ───────────────────────────────────────────────────────────────────────────
// §4 每帧输入：push constant
// ───────────────────────────────────────────────────────────────────────────
layout(push_constant) uniform PushConsts
{
    uint64_t vertexAddress;
    uint64_t dataAddress; // 实例堆基址
    uint64_t commandConstantsAddress;
    uint cameraIndex; // 0: 3D, 1: UI
    uint render_version;
}
pc;

// ───────────────────────────────────────────────────────────────────────────
// §5 拾取打包
// ───────────────────────────────────────────────────────────────────────────
const int RENDER_VERSION_BIT_WIDTH = 16;
#define PRIMITIVE_MASK ((1u << (32 - RENDER_VERSION_BIT_WIDTH)) - 1u)

#endif // TEST_INTENT6_ABI_GLSL
