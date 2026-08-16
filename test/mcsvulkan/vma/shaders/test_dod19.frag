#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

#include "test_dod19.glsl"

layout(set = 0, binding = 1) uniform texture2D textures[];
layout(set = 0, binding = 2) uniform sampler samplers[];

// 输入
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint type_id;
layout(location = 3) flat in uint64_t instancePtr;

// 从顶点着色器接收
layout(location = 4) in vec2 localPos; // 归一化局部坐标 [-0.5,0.5]
layout(location = 5) flat in uint instanceId; // 实例 id（vert 传，普通绘制拾取用）

// 输出：到颜色附件0（替换了gl_FragColor）
layout(location = 0) out vec4 outColor;
layout(location = 1) out uvec4 outPicking; // (object_type, entity_index, primitive_id, hover_fn)
                                           // 与附件格式 R32G32B32A32_UINT 严格对齐
                                           // w = 绑定的 hover 函数池实体下标（0xFFFFFFFF = 未绑定）

// 定义// 字体类型枚举（与 C++ FontType 枚举一致：e 前缀 + 固定编译期类型值 0..7）
const int FONT_HARD_MASK = 0;
const int FONT_SOFT_MASK = 1;
const int FONT_SDF = 2;
const int FONT_PSDF = 3;
const int FONT_MSDF = 4;
const int FONT_MTSDF = 5;
const int FONT_BITMAP = 6;
const int FONT_NONE = 7;

// ===== 文字距离场辅助函数（与 test_sdf.frag 一致）=====
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

// ===== 圆角阴影矩形（照抄 test_sdf drawRect；premultiplied over，与管线 ONE 混合匹配）=====
float roundedBoxSDF(vec2 CenterPosition, vec2 HalfSize, float Radius)
{
    return length(max(abs(CenterPosition) - HalfSize + Radius, 0.0)) - Radius;
}

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

    // ---------- premultiplied over 合成（阴影在下，卡片在上）----------
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

void main()
{
    outColor = fragColor;                    // 默认使用顶点颜色
    outPicking = uvec4(0xFFFFFFFF, 0, 0, 0); // 无有效拾取（object_type = 0xFFFFFFFF）

    switch (type_id)
    {
    case 2: { // Glyph：与 test_sdf.frag 的 case 2 完全一致
        Glyph inst = GlyphBuffer(instancePtr).glyphs[0];
        vec4 texColor = texture(sampler2D(textures[nonuniformEXT(inst.textureIndex)],
                                          samplers[nonuniformEXT(inst.samplerIndex)]),
                                fragTexCoord);

        // 根据 fontType 选择 普通纹理 / 彩色位图 / 距离场 三种路径
        if (inst.fontType == uint(FONT_NONE))
        {
            // 默认路径：标准纹理混合（UI 矩形、普通模型等）
            outColor = vec4(fragColor.rgb * texColor.rgb, texColor.a);
        }
        else if (inst.fontType == uint(FONT_BITMAP))
        {
            // 彩色位图（如 emoji）：直接采样纹理颜色
            if (inst.modulateFlag == 1U)
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
        outPicking = uvec4(type_id, inst.entity_index, gl_PrimitiveID, inst.hover_fn);
    }
    break;
    case 3: { // TYPE_RECT：空心线框（不 discard，内部 alpha=0，拾取整块）
        UiRect inst = RectBuffer(instancePtr).rects[0];
        float strip = max(abs(localPos.x), abs(localPos.y));      // [0, 0.5]
        float alpha = smoothstep(0.5 - inst.border, 0.5, strip);  // 内部→0，边→1
        // premultiplied：rgb 必须乘 alpha，否则内部 alpha=0 时 rgb 仍被 src=ONE 加进背景
        outColor = vec4(inst.color.rgb * alpha, inst.color.a * alpha);
        outPicking = uvec4(type_id, inst.entity_index, gl_PrimitiveID, inst.hover_fn);
    }
    break;
    case 5: { // TYPE_ROUND_RECT：圆角阴影矩形（照抄 test_sdf case 0；不 discard；整块可拾取）
        Rectangle inst = RectangleBuffer(instancePtr).rects[0];
        vec2 cardHalf = inst.size * 0.5;
        vec2 uv = localPos * inst.size; // 覆盖外扩后的 quad
        outColor = drawRect(uv, cardHalf, fragColor, inst);
        outPicking = uvec4(type_id, inst.entity_index, gl_PrimitiveID, ~0u); // hover 未绑定
    }
    break;
    case 4: { // TYPE_MESH：普通绘制（无 SDF），实心；实体 = 实例 id
        outColor = fragColor; // 每顶点颜色插值（纯色 / 渐变填充）
        outPicking = uvec4(type_id, instanceId, gl_PrimitiveID, ~0u); // 未绑定
    }
    break;
    default:
        break;
    }
}