
#define GLYPH_SIZE 124
struct UvTransform
{
    vec2 scale;
    vec2 offset;
};
struct Glyph
{
    uint entity_index;       // 拾取实体索引（outPicking.y）
    uint textureIndex;       // 纹理数组下标（bindless）
    uint samplerIndex;       // 采样器数组下标（bindless）
    uint fontType;           // 字体类型（与 FontType 枚举一致）
    float pxRange;           // MSDF 距离场范围
    uint modulateFlag;       // 1 = 用顶点色调制
    vec4 color;              // 顶点色（默认白）
    mat4 model;              // 平移 + 缩放：[-0.5,0.5] quad -> NDC 字形矩形
    UvTransform uvTransform; // 图集 UV 变换
    uint hover_fn;           // 绑定的 hover 函数池实体下标（0xFFFFFFFF = 未绑定），随实例上传，写回 outPicking.w
};
layout(buffer_reference, scalar) readonly buffer GlyphBuffer
{
    Glyph glyphs[];
};

// ===== 矩形线框（布局 debug 边框；独立于圆角阴影矩形）=====
#define UI_RECT_SIZE 44
struct UiRect
{
    uint entity_index;  // 拾取外键
    uint hover_fn;      // hover 池下标（0xFFFFFFFF = 未绑定）
    vec4 center_size;   // center.xy + size.xy（NDC）
    vec4 color;         // 边框色
    float border;       // 边框半宽（NDC）
};
layout(buffer_reference, scalar) readonly buffer RectBuffer
{
    UiRect rects[];
};

// ===== 圆角阴影矩形（照抄 test_sdf Rectangle + drawRect；TYPE_ROUND_RECT=5）=====
// HTML box-shadow 风格：圆角卡片 + 投影，全部参数化
const uint FX_ROUNDED = 1u; // 圆角
const uint FX_SHADOW = 2u;  // 阴影
const uint FX_FILL = 4u;    // 填充

struct VertexTransform
{
    mat4 matrix;
};

#define RECTANGLE_SIZE 264
struct Rectangle
{
    uint entity_index;               // 拾取实体索引（outPicking.y）
    uint effects;                    // 特效标志（与 FX_* 按位或）
    vec4 colors[4];                  // 四顶点颜色（单色 = 四顶点同色）
    mat4 model;                      // 平移 + 旋转
    VertexTransform vertexTransform; // 顶点缩放（quad [-0.5,0.5] × size）
    UvTransform uvTransform;         // UV 变换
    vec2 size;                       // 卡片完整宽/高（NDC，SDF 用）
    vec2 shadowOffset;               // 阴影偏移（相对卡片尺寸；x 右为正，y 下为正）
    vec4 radiusSoftness;             // x=圆角比例 y=边缘柔化 z=阴影模糊 w=阴影扩散
    vec4 shadowColor;                // 阴影色 RGBA（a=0 → 无阴影）
};
layout(buffer_reference, scalar) readonly buffer RectangleBuffer
{
    Rectangle rects[];
};

// ===== 顶点属性（普通绘制用）：每顶点一份 =====
// 顶点本身 + 每顶点属性（无 SDF）；顶点位置直接来自属性池
struct VertexAttr
{
    vec3 pos;   // 顶点位置（NDC/局部坐标，直接作为顶点）
    vec4 color; // 每顶点颜色（插值 → 渐变）
};
#define VERTEX_ATTR_SIZE 28
layout(buffer_reference, scalar) readonly buffer AttrBuffer
{
    VertexAttr attrs[];
};
