#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

#include "test_intent6_abi.glsl"

layout(set = 0, binding = 1) uniform texture2D textures[];
layout(set = 0, binding = 2) uniform sampler samplers[];

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint type_id;
layout(location = 3) flat in uint64_t instancePtr;
layout(location = 4) in vec2 localPos;
layout(location = 5) flat in uint instanceId;

layout(location = 0) out vec4 outColor;
layout(location = 1) out uvec4 outPicking;

const int FONT_HARD_MASK = 0;
const int FONT_SOFT_MASK = 1;
const int FONT_SDF = 2;
const int FONT_PSDF = 3;
const int FONT_MSDF = 4;
const int FONT_MTSDF = 5;
const int FONT_BITMAP = 6;
const int FONT_NONE = 7;

layout(push_constant) uniform PushConsts
{
    uint64_t vertexAddress;
    uint64_t dataAddress;
    uint64_t commandConstantsAddress;
    uint cameraIndex;
    uint render_version;
}
pc;
const int RENDER_VERSION_BIT_WIDTH = 16;
#define PRIMITIVE_MASK ((1u << (32 - RENDER_VERSION_BIT_WIDTH)) - 1u)

float median(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}

float screenPxRange(Glyph inst)
{
    ivec2 texSize = textureSize(sampler2D(textures[nonuniformEXT(inst.textureIndex)],
                                          samplers[nonuniformEXT(inst.samplerIndex)]),
                                0);
    vec2 unitRange = vec2(inst.pxRange) / vec2(texSize);
    vec2 screenTexSize = 1.0 / fwidth(fragTexCoord);
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

float roundedBoxSDF(vec2 CenterPosition, vec2 HalfSize, float Radius)
{
    return length(max(abs(CenterPosition) - HalfSize + Radius, 0.0)) - Radius;
}

vec4 drawRect(vec2 uv, vec2 cardHalf, vec4 fillColor, Rectangle inst)
{
    bool fxRounded = (inst.effects & FX_ROUNDED) != 0u || inst.radiusSoftness.x > 0.0;
    bool fxShadow = (inst.effects & FX_SHADOW) != 0u || inst.shadowColor.a > 0.0;
    bool fxFill = (inst.effects & FX_FILL) != 0u || fillColor.a > 0.0;

    vec2 cardSize = cardHalf * 2.0;
    float radius =
        fxRounded ? max(inst.radiusSoftness.x, 0.0) * min(cardSize.x, cardSize.y) : 0.0;
    float edgeSoft = max(inst.radiusSoftness.y, 0.0005) * cardSize.x;
    float blur = max(inst.radiusSoftness.z, 0.0005) * cardSize.x;
    float spread = max(inst.radiusSoftness.w, 0.0);
    vec2 shadowHalf = cardHalf * spread;
    vec2 shadowOffset = inst.shadowOffset * cardSize;
    vec2 center = vec2(0.0);

    float distance = roundedBoxSDF(uv - center, cardHalf, radius);
    float rectAlpha = 1.0 - smoothstep(0.0, edgeSoft, distance);

    float shadowDistance = roundedBoxSDF(uv - center - shadowOffset, shadowHalf, radius);
    float shadowAlpha = 1.0 - smoothstep(-blur, blur, shadowDistance);
    float shadowMix = fxShadow ? shadowAlpha : 0.0;

    vec4 shadowLayer;
    shadowLayer.rgb = inst.shadowColor.rgb * inst.shadowColor.a * shadowMix;
    shadowLayer.a = inst.shadowColor.a * shadowMix;

    vec4 fillLayer;
    float fillCoverage = fxFill ? rectAlpha : 0.0;
    fillLayer.rgb = fillColor.rgb * fillColor.a * fillCoverage;
    fillLayer.a = fillColor.a * fillCoverage;

    vec4 result;
    result.rgb = fillLayer.rgb + shadowLayer.rgb * (1.0 - fillLayer.a);
    result.a = fillLayer.a + shadowLayer.a * (1.0 - fillLayer.a);
    return result;
}

// 同一套绘制算法，数据来自 UI 图元（rect/params/color/color2）
void main()
{
    outColor = fragColor;
    outPicking = uvec4(0xFFFFFFFF, 0, 0, 0);
    uint packed_z = (pc.render_version << RENDER_VERSION_BIT_WIDTH) |
                    (uint(gl_PrimitiveID) & PRIMITIVE_MASK);

    switch (type_id)
    {
    case 2: {
        Glyph inst = GlyphBuffer(instancePtr).glyphs[0];
        vec4 texColor = texture(sampler2D(textures[nonuniformEXT(inst.textureIndex)],
                                          samplers[nonuniformEXT(inst.samplerIndex)]),
                                fragTexCoord);

        if (inst.fontType == uint(FONT_NONE))
        {
            outColor = vec4(fragColor.rgb * texColor.rgb, texColor.a);
        }
        else if (inst.fontType == uint(FONT_BITMAP))
        {
            if (inst.modulateFlag == 1U)
                outColor = texColor * vec4(fragColor.rgb, 1.0);
            else
                outColor = texColor;
        }
        else
        {
            vec3 msd = texColor.rgb;
            float sd = median(msd.r, msd.g, msd.b);
            float screenPxDistance = screenPxRange(inst) * (sd - 0.5);
            float opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);
            if (inst.modulateFlag == 1u)
                outColor = vec4(fragColor.rgb * opacity, opacity);
            else
                outColor = vec4(vec3(opacity), opacity);
        }
        outPicking = uvec4(type_id, inst.entity_index, packed_z, inst.hover_fn);
    }
    break;
    case 3: {
        UiRect inst = RectBuffer(instancePtr).rects[0];
        float strip = max(abs(localPos.x), abs(localPos.y));
        float alpha = smoothstep(0.5 - inst.border, 0.5, strip);
        outColor = vec4(inst.color.rgb * alpha, inst.color.a * alpha);
        outPicking = uvec4(type_id, inst.entity_index, packed_z, inst.hover_fn);
    }
    break;
    case 5: {
        Rectangle inst = RectangleBuffer(instancePtr).rects[0];
        vec2 cardHalf = inst.size * 0.5;
        vec2 uv = localPos * inst.size;
        outColor = drawRect(uv, cardHalf, fragColor, inst);
        outPicking = uvec4(type_id, inst.entity_index, packed_z, ~0u);
    }
    break;
    case 4: {
        outColor = fragColor;
        outPicking = uvec4(type_id, instanceId, packed_z, ~0u);
    }
    break;

    default:
        break;
    }
}
