
#define GLYPH_SIZE 120
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
};
layout(buffer_reference, scalar) readonly buffer GlyphBuffer
{
    Glyph glyphs[];
};
