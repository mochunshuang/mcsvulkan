#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(set = 0, binding = 1) uniform texture2D textures[];
layout(set = 0, binding = 2) uniform sampler samplers[];

// 输入
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint type_id;
layout(location = 3) flat in uint64_t instancePtr;

// 从顶点着色器接收
layout(location = 4) in vec2 localPos; // 归一化局部坐标 [-0.5,0.5] × 外扩系数

// 输出：到颜色附件0（替换了gl_FragColor）
layout(location = 0) out vec4 outColor;
layout(location = 1) out uvec4 outPicking; // (object_type, entity_index, primitive_id, 0)
                                           // 与附件格式 R32G32B32A32_UINT 严格对齐

// 定义字体类型常量（与 C++ 枚举值保持一致）
const int FONT_HARD_MASK = 0;
const int FONT_SOFT_MASK = 1;
const int FONT_SDF = 2;
const int FONT_PSDF = 3;
const int FONT_MSDF = 4;
const int FONT_MTSDF = 5;
const int FONT_BITMAP = 6;
const int FONT_NONE = 7;

// 特效标志（与 cpp 上传的 effects 字段一致；追加式，值为默认推断）
const uint FX_ROUNDED = 1u; // 圆角
const uint FX_SHADOW = 2u;  // 阴影
const uint FX_FILL = 4u;    // 填充

struct VertexTransform
{
    mat4 matrix;
};
struct UvTransform
{
    vec2 scale;
    vec2 offset;
};
struct Rectangle
{
    uint entity_index;         // 拾取实体索引（outPicking.y）
    uint effects;              // 特效标志（与 FX_* 按位或）
    vec4 colors[4];            // 四顶点颜色（quad 网格固定 N=4）
    mat4 model;                // 平移 + 旋转
    VertexTransform vertexTransform; // 顶点缩放（cpp 按 size 与公共顶点算好）
    UvTransform uvTransform;   // UV 变换
    vec2 size;                 // 卡片完整宽/高（NDC，SDF 用）
    vec2 shadowOffset;         // 阴影偏移（相对卡片宽/高；x 右为正，y 下为正）
    vec4 radiusSoftness;       // x=圆角比例 y=边缘柔化 z=阴影模糊 w=阴影扩散
    vec4 shadowColor;          // 阴影色 RGBA（a=0 → 无阴影）
};
layout(buffer_reference, scalar) readonly buffer RectangleBuffer
{
    Rectangle rects[];
};

struct ColoredTri
{
    uint entity_index; // 拾取实体索引（outPicking.y）
    mat4 model;        // 平移 + 旋转 + 缩放
    vec4 colors[3];    // 三个顶点颜色（按本地顶点索引取）
};
layout(buffer_reference, scalar) readonly buffer ColoredTriBuffer
{
    ColoredTri insts[];
};

// ===== 从 ShaderToy 复制过来的 SDF 函数 =====
float roundedBoxSDF(vec2 CenterPosition, vec2 HalfSize, float Radius) {
    return length(max(abs(CenterPosition) - HalfSize + Radius, 0.0)) - Radius;
}

// ============================================================
// 矩形绘制后端：前端(cpp)上传 Rectangle 对象参数，这里统一执行。
// 顶点几何已由顶点着色器按阴影参数自动外扩，保证：
//     卡片 + 阴影 <= 顶点几何范围
// 所有数据都从实例对象（instancePtr）直接读取，零冗余传参。
//
// 合成采用 premultiplied alpha over（与管线的
// srcColor=ONE / dstColor=ONE_MINUS_SRC_ALPHA 匹配）：
//     阴影在下层，卡片在上层，半透明卡片能透出阴影（HTML 风格）。
// ============================================================
vec4 drawRect(vec2 uv, vec2 cardHalf, vec4 fillColor, Rectangle inst) {
    // ---------- 特效开关（标志位优先，值为默认推断）----------
    bool fxRounded = (inst.effects & FX_ROUNDED) != 0u || inst.radiusSoftness.x > 0.0;
    bool fxShadow  = (inst.effects & FX_SHADOW) != 0u || inst.shadowColor.a > 0.0;
    bool fxFill    = (inst.effects & FX_FILL) != 0u || fillColor.a > 0.0;

    vec2 cardSize = cardHalf * 2.0;

    // ---------- 参数解析（全部来自实例对象）----------
    float radius   = fxRounded ? max(inst.radiusSoftness.x, 0.0) * min(cardSize.x, cardSize.y) : 0.0;
    float edgeSoft = max(inst.radiusSoftness.y, 0.0005) * cardSize.x; // 边缘抗锯齿
    float blur     = max(inst.radiusSoftness.z, 0.0005) * cardSize.x; // 阴影模糊
    float spread   = max(inst.radiusSoftness.w, 0.0);                 // 阴影扩散（1=同卡片）
    vec2  shadowHalf   = cardHalf * spread;
    vec2  shadowOffset = inst.shadowOffset * cardSize;                // 相对卡片尺寸
    vec2  center = vec2(0.0);

    // ---------- 卡片（圆角矩形）----------
    float distance = roundedBoxSDF(uv - center, cardHalf, radius);
    float rectAlpha = 1.0 - smoothstep(0.0, edgeSoft, distance);

    // ---------- 阴影（HTML box-shadow 风格）----------
    float shadowDistance = roundedBoxSDF(uv - center - shadowOffset, shadowHalf, radius);
    float shadowAlpha = 1.0 - smoothstep(-blur, blur, shadowDistance);
    float shadowMix = fxShadow ? shadowAlpha : 0.0;

    // ---------- premultiplied over 合成 ----------
    vec4 shadowLayer;
    shadowLayer.rgb = inst.shadowColor.rgb * inst.shadowColor.a * shadowMix;
    shadowLayer.a   = inst.shadowColor.a * shadowMix;

    vec4 fillLayer;
    float fillCoverage = fxFill ? rectAlpha : 0.0; // 卡片覆盖系数（含边缘抗锯齿）
    fillLayer.rgb = fillColor.rgb * fillColor.a * fillCoverage;
    fillLayer.a   = fillColor.a * fillCoverage;

    vec4 result;
    result.rgb = fillLayer.rgb + shadowLayer.rgb * (1.0 - fillLayer.a);
    result.a   = fillLayer.a + shadowLayer.a * (1.0 - fillLayer.a);
    return result;
}

void main()
{
    outColor = fragColor;              // 其他类型使用顶点颜色
    outPicking = uvec4(0xFFFFFFFF, 0, 0, 0); // 无有效拾取（object_type = 0xFFFFFFFF）

    switch (type_id)
    {
    case 0: {
        Rectangle inst = RectangleBuffer(instancePtr).rects[0]; // 从实例对象取数据
        vec2 cardHalf = inst.size * 0.5;
        vec2 uv = localPos * inst.size; // 覆盖整个（已外扩的）quad 范围
        outColor = drawRect(uv, cardHalf, fragColor, inst); // fragColor = 插值顶点色
        outPicking = uvec4(type_id, inst.entity_index, gl_PrimitiveID, 0);
    }
    break;
    case 1:
    {
        // 三角形：顶点颜色来自实例（std::array<color,3>），premultiplied 直出
        outColor = vec4(fragColor.rgb * fragColor.a, fragColor.a);
        ColoredTri inst = ColoredTriBuffer(instancePtr).insts[0];
        outPicking = uvec4(type_id, inst.entity_index, gl_PrimitiveID, 0);
    }
    break;
    default:
        break;
    }
}
