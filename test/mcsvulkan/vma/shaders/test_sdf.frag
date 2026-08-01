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
layout(location = 4) in vec2 localPos;   // [-0.5, 0.5]
layout(location = 5) in vec2 rectSize;   // 实际宽高

// 输出：到颜色附件0（替换了gl_FragColor）
layout(location = 0) out vec4 outColor;
layout(location = 1) out uvec2 outPicking;

// 定义字体类型常量（与 C++ 枚举值保持一致）
const int FONT_HARD_MASK = 0;
const int FONT_SOFT_MASK = 1;
const int FONT_SDF = 2;
const int FONT_PSDF = 3;
const int FONT_MSDF = 4;
const int FONT_MTSDF = 5;
const int FONT_BITMAP = 6;
const int FONT_NONE = 7;

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
    vec4 color;
    mat4 model;
    VertexTransform vertexTransform;
    UvTransform uvTransform;
};
layout(buffer_reference, scalar) readonly buffer RectangleBuffer
{
    Rectangle rects[];
};

// ===== 从 ShaderToy 复制过来的 SDF 函数 =====
float roundedBoxSDF(vec2 CenterPosition, vec2 Size, float Radius) {
    return length(max(abs(CenterPosition) - Size + Radius, 0.0)) - Radius;
}

// NOTE: 先顶点着色器生成颜色，你才能丢弃或改变颜色。不能凭空生成颜色的
vec4 drawShadowedRect(vec2 uv, vec2 size, vec4 baseColor) {
    float radius = min(size.x, size.y) * 0.1;
    float edgeSoftness = 1.0;

    // ---------- 调整这些参数 ----------
    float shadowSoftness = 2.0;                 // 更小的模糊，阴影更实
    vec2 shadowOffset = vec2(0.0, -0.15);       // 向下偏移明显一些（相对于尺寸0.3~0.4）
    vec4 shadowColor = vec4(1.0, 1.0, 1.0, 0.9); // 高不透明度白色
    // ---------------------------------

    vec2 center = vec2(0.0);
    float distance = roundedBoxSDF(uv - center, size / 2.0, radius);
    float rectAlpha = 1.0 - smoothstep(0.0, edgeSoftness, distance);
    if (distance > 0.0) discard;

    float shadowDistance = roundedBoxSDF(uv - center + shadowOffset, size / 2.0, radius);
    float shadowAlpha = 1.0 - smoothstep(-shadowSoftness, shadowSoftness, shadowDistance);
    shadowAlpha = max(0.0, shadowAlpha - rectAlpha);

    vec4 finalColor = shadowColor * shadowAlpha;
    finalColor = mix(finalColor, baseColor, rectAlpha);
    return finalColor;
}

void main()
{
    outColor = fragColor;              // 其他类型使用顶点颜色
    outPicking = uvec2(0xFFFFFFFF, 0); // 无有效拾取

    switch (type_id)
    {
    case 0: {
        Rectangle inst = RectangleBuffer(instancePtr).rects[0];
    
        // 用局部坐标和尺寸，调用阴影绘制函数
        vec4 finalColor = drawShadowedRect(localPos * rectSize, rectSize, fragColor);
        outColor = finalColor;
    }
    break;
    default:
        break;
    }
}

