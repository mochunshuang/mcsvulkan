#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

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
    uint entity_index;               // 拾取实体索引（outPicking.y）
    uint effects;                    // 特效标志（与 FX_* 按位或）
    vec4 colors[4];                  // 四顶点颜色（quad 网格固定 N=4）
    mat4 model;                      // 平移 + 旋转
    VertexTransform vertexTransform; // 顶点缩放（cpp 按 size 与公共顶点算好）
    UvTransform uvTransform;         // UV 变换
    vec2 size;                       // 卡片完整宽/高（NDC，SDF 用）
    vec2 shadowOffset;               // 阴影偏移（相对卡片宽/高；x 右为正，y 下为正）
    vec4 radiusSoftness;             // x=圆角比例 y=边缘柔化 z=阴影模糊 w=阴影扩散
    vec4 shadowColor;                // 阴影色 RGBA（a=0 → 无阴影）
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

struct Glyph
{
    uint entity_index;       // 拾取实体索引（outPicking.y）
    uint textureIndex;       // 纹理数组下标（bindless）
    uint samplerIndex;       // 采样器数组下标（bindless）
    uint fontType;           // 字体类型（与 FontType 枚举一致）
    float pxRange;           // MSDF 距离场范围
    uint modulateFlag;       // 1 = 用顶点色调制
    vec4 color;              // 顶点色（默认白）
    mat4 model;              // 平移 + 缩放（字形矩形）
    UvTransform uvTransform; // 图集 UV 变换
};
layout(buffer_reference, scalar) readonly buffer GlyphBuffer
{
    Glyph glyphs[];
};

// ============================================================
// type_id == 3/4/5：补充 SDF 的实例结构（与 test_sdf.vert / test_sdf.cpp 对齐）
// ============================================================
struct UiPoint
{
    uint entity_index;
    uint style; // 0=圆盘 1=圆环 2=方块 3=菱形 4=十字 5=星形
    vec4 color;
    vec2 center; // NDC
    vec2 size;   // NDC 直径
    float softness;
    float param; // 圆环内径比例 / 星形瓣数
};
layout(buffer_reference, scalar) readonly buffer UiPointBuffer
{
    UiPoint insts[];
};

struct UiLine
{
    uint entity_index;
    uint style;     // 0=实线胶囊 1=虚线 2=箭头 3=渐变（颜色端点插值）
    vec4 colors[2]; // 端点颜色
    vec2 posA;      // NDC
    vec2 posB;      // NDC
    float width;
    float softness;
    float param; // 虚线周期 / 箭头长度
};
layout(buffer_reference, scalar) readonly buffer UiLineBuffer
{
    UiLine insts[];
};

struct UiPolygon
{
    uint entity_index;
    uint sides;
    vec4 color;
    vec2 center;
    float radius;   // NDC 外接圆半径
    float rotation; // 弧度
    float softness;
};
layout(buffer_reference, scalar) readonly buffer UiPolygonBuffer
{
    UiPolygon insts[];
};

#define UI_MESH_SIZE 84
struct UiMesh
{
    uint entity_index;
    mat4 model;       // 平移 + 旋转 + 缩放
    vec4 color;       // 单色填充
};
layout(buffer_reference, scalar) readonly buffer UiMeshBuffer
{
    UiMesh insts[];
};

#define UI_MESH_VC_SIZE 68
struct UiMeshVc
{
    uint entity_index;
    mat4 model;
};
layout(buffer_reference, scalar) readonly buffer UiMeshVcBuffer
{
    UiMeshVc insts[];
};

// ===== 从 ShaderToy 复制过来的 SDF 函数 =====
float roundedBoxSDF(vec2 CenterPosition, vec2 HalfSize, float Radius)
{
    return length(max(abs(CenterPosition) - HalfSize + Radius, 0.0)) - Radius;
}

// ===== 文字距离场辅助函数（与 test_dod12.frag 一致）=====
float median(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}

// 把 pxRange（图集单位距离场范围）换算成屏幕像素范围，用于抗锯齿
float screenPxRange(Glyph inst)
{
    ivec2 texSize = textureSize(sampler2D(textures[nonuniformEXT(inst.textureIndex)],
                                          samplers[nonuniformEXT(inst.samplerIndex)]),
                                0);
    vec2 unitRange = vec2(inst.pxRange) / vec2(texSize); // 每个纹理像素对应的距离场范围
    vec2 screenTexSize = 1.0 / fwidth(fragTexCoord);     // 屏幕空间纹理缩放因子
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
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
vec4 drawRect(vec2 uv, vec2 cardHalf, vec4 fillColor, Rectangle inst)
{
    // ---------- 特效开关（标志位优先，值为默认推断）----------
    bool fxRounded = (inst.effects & FX_ROUNDED) != 0u || inst.radiusSoftness.x > 0.0;
    bool fxShadow = (inst.effects & FX_SHADOW) != 0u || inst.shadowColor.a > 0.0;
    bool fxFill = (inst.effects & FX_FILL) != 0u || fillColor.a > 0.0;

    vec2 cardSize = cardHalf * 2.0;

    // ---------- 参数解析（全部来自实例对象）----------
    float radius =
        fxRounded ? max(inst.radiusSoftness.x, 0.0) * min(cardSize.x, cardSize.y) : 0.0;
    float edgeSoft = max(inst.radiusSoftness.y, 0.0005) * cardSize.x; // 边缘抗锯齿
    float blur = max(inst.radiusSoftness.z, 0.0005) * cardSize.x;     // 阴影模糊
    float spread = max(inst.radiusSoftness.w, 0.0); // 阴影扩散（1=同卡片）
    vec2 shadowHalf = cardHalf * spread;
    vec2 shadowOffset = inst.shadowOffset * cardSize; // 相对卡片尺寸
    vec2 center = vec2(0.0);

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
    shadowLayer.a = inst.shadowColor.a * shadowMix;

    vec4 fillLayer;
    float fillCoverage = fxFill ? rectAlpha : 0.0; // 卡片覆盖系数（含边缘抗锯齿）
    fillLayer.rgb = fillColor.rgb * fillColor.a * fillCoverage;
    fillLayer.a = fillColor.a * fillCoverage;

    vec4 result;
    result.rgb = fillLayer.rgb + shadowLayer.rgb * (1.0 - fillLayer.a);
    result.a = fillLayer.a + shadowLayer.a * (1.0 - fillLayer.a);
    return result;
}

// ===== 补充 SDF：通用点 / 通用线 / 正多边形 =====
// 点样式 SDF：p 为局部坐标（NDC），halfSize 为半直径
float uiPointSDF(vec2 p, float halfSize, uint style, float param)
{
    float d = 1e9;
    if (style == 0u)
    { // 圆盘
        d = length(p) - halfSize;
    }
    else if (style == 1u)
    { // 圆环
        float inner = halfSize * clamp(param, 0.05, 0.95);
        d = max(inner - length(p), length(p) - halfSize);
    }
    else if (style == 2u)
    { // 圆角方块
        d = roundedBoxSDF(p, vec2(halfSize), halfSize * 0.1);
    }
    else if (style == 3u)
    { // 菱形
        d = abs(p.x) + abs(p.y) - halfSize;
    }
    else if (style == 4u)
    { // 十字
        d = min(abs(p.x), abs(p.y)) - halfSize * 0.18;
    }
    else
    { // 星形（param=瓣数，默认 5）
        float spikes = max(2.0, param);
        float ang = 6.2831853 / spikes;
        float rr = abs(mod(atan(p.y, p.x), ang) - ang * 0.5);
        float edgeR = halfSize * (1.0 - 0.55 * rr / (ang * 0.5)); // 内径比例 0.45
        d = length(p) - edgeR;
    }
    return d;
}

// 线段 SDF：p 为线段局部坐标（x 沿线段、y 垂直），半长 halfLen、半宽 halfW
float uiLineSDF(vec2 p, float halfLen, float halfW, uint style, float param)
{
    if (style == 1u)
    { // 虚线（param=周期，NDC）
        float period = max(param, 0.02);
        float s = p.x + halfLen; // 沿线段 0..2*halfLen
        if (step(fract(s / period), 0.5) < 0.5)
            return 1e9;
    }
    else if (style == 2u)
    { // 箭头（头部在 +x 端，param=头长）
        float headLen = max(param, halfW * 2.0);
        float bodyEnd = halfLen - headLen;
        if (p.x > bodyEnd)
        {
            float t = clamp((p.x - bodyEnd) / headLen, 0.0, 1.0);
            float halfHead = halfW * (1.0 - t);
            return max(abs(p.y) - halfHead, p.x - halfLen);
        }
    }
    return roundedBoxSDF(p, vec2(halfLen, halfW), halfW); // 胶囊（圆头实线/渐变）
}

void main()
{
    outColor = fragColor;                    // 其他类型使用顶点颜色
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
    case 1: {
        // 三角形：顶点颜色来自实例（std::array<color,3>），premultiplied 直出
        outColor = vec4(fragColor.rgb * fragColor.a, fragColor.a);
        ColoredTri inst = ColoredTriBuffer(instancePtr).insts[0];
        outPicking = uvec4(type_id, inst.entity_index, gl_PrimitiveID, 0);
    }
    break;
    case 2: {
        // 字形：根据 fontType 选择 普通纹理 / 彩色位图 / 距离场 三种路径
        Glyph inst = GlyphBuffer(instancePtr).glyphs[0];
        vec4 texColor = texture(sampler2D(textures[nonuniformEXT(inst.textureIndex)],
                                          samplers[nonuniformEXT(inst.samplerIndex)]),
                                fragTexCoord);

        if (inst.fontType == FONT_NONE)
        {
            // 默认路径：标准纹理混合（UI 矩形、普通模型等）
            outColor = vec4(fragColor.rgb * texColor.rgb, texColor.a);
        }
        else if (inst.fontType == FONT_BITMAP)
        {
            // 彩色位图（如 emoji）：直接采样纹理颜色
            if (inst.modulateFlag == 1u)
            {
                outColor = texColor * vec4(fragColor.rgb, 1.0);
            }
            else
            {
                outColor = texColor;
            }
        }
        else
        {
            // MSDF / SDF 等距离场字体渲染
            vec3 msd = texColor.rgb;
            float sd = median(msd.r, msd.g, msd.b);
            float screenPxDistance = screenPxRange(inst) * (sd - 0.5);
            float opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);
            if (inst.modulateFlag == 1u)
            {
                outColor = vec4(fragColor.rgb * opacity, opacity);
            }
            else
            {
                outColor = vec4(vec3(opacity), opacity); // 默认白色文字
            }
        }
        outPicking = uvec4(type_id, inst.entity_index, gl_PrimitiveID, 0);
    }
    break;
    case 3: {
        UiPoint inst = UiPointBuffer(instancePtr).insts[0];
        vec2 uv = localPos * inst.size; // 覆盖 quad 范围
        float halfSize = min(inst.size.x, inst.size.y) * 0.5;
        float d = uiPointSDF(uv, halfSize, inst.style, inst.param);
        float alpha = 1.0 - smoothstep(0.0, max(inst.softness, 0.0005), d);
        outColor = vec4(fragColor.rgb * fragColor.a * alpha, fragColor.a * alpha);
        outPicking = uvec4(type_id, inst.entity_index, gl_PrimitiveID, 0);
    }
    break;
    case 4: {
        UiLine inst = UiLineBuffer(instancePtr).insts[0];
        float len = length(inst.posB - inst.posA);
        float w = max(inst.width, 1e-4);
        float d = uiLineSDF(localPos, len * 0.5, w * 0.5, inst.style, inst.param);
        float alpha = 1.0 - smoothstep(0.0, max(inst.softness, 0.0005), d);
        outColor = vec4(fragColor.rgb * fragColor.a * alpha, fragColor.a * alpha);
        outPicking = uvec4(type_id, inst.entity_index, gl_PrimitiveID, 0);
    }
    break;
    case 5: {
        UiPolygon inst = UiPolygonBuffer(instancePtr).insts[0];
        vec2 uv = localPos * 2.0 * inst.radius;
        float n = max(3.0, float(inst.sides));
        float sector = 6.2831853 / n;
        float phi = abs(mod(atan(uv.y, uv.x) - inst.rotation, sector) - sector * 0.5);
        float edgeR = inst.radius * cos(sector * 0.5) / cos(phi);
        float d = length(uv) - edgeR;
        float alpha = 1.0 - smoothstep(0.0, max(inst.softness, 0.0005), d);
        outColor = vec4(fragColor.rgb * fragColor.a * alpha, fragColor.a * alpha);
        outPicking = uvec4(type_id, inst.entity_index, gl_PrimitiveID, 0);
    }
    break;
    case 6: {
        UiMesh inst = UiMeshBuffer(instancePtr).insts[0];
        outColor = vec4(fragColor.rgb * fragColor.a, fragColor.a); // CPU 三角化，直出
        outPicking = uvec4(type_id, inst.entity_index, gl_PrimitiveID, 0);
    }
    break;
    case 7: {
        UiMeshVc inst = UiMeshVcBuffer(instancePtr).insts[0];
        outColor = vec4(fragColor.rgb * fragColor.a, fragColor.a); // 逐顶点色渐变，直出
        outPicking = uvec4(type_id, inst.entity_index, gl_PrimitiveID, 0);
    }
    break;
    default:
        break;
    }
}
