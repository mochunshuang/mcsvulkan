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

// 输出：到颜色附件0（替换了gl_FragColor）
layout(location = 0) out vec4 outColor;
layout(location = 1) out uvec4 outPicking; // (object_type, entity_index, primitive_id, 0)
                                           // 与附件格式 R32G32_UINT 严格对齐（只用 xy）

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
        outPicking = uvec4(type_id, inst.entity_index, gl_PrimitiveID, 0);
    }
    break;
    default:
        break;
    }
}