#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

#include "test_ui_ability7_abi.glsl"

layout(set = 0, binding = 1) uniform texture2D textures[];
layout(set = 0, binding = 2) uniform sampler samplers[];

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint program;
layout(location = 3) flat in uint64_t instancePtr;
layout(location = 4) in vec2 localPos;
layout(location = 5) flat in uint instanceId;

layout(location = 0) out vec4 outColor;
layout(location = 1) out uvec4 outPicking;

// ───────────────────────────────────────────────────────────────────────────
// 公共工具
// ───────────────────────────────────────────────────────────────────────────
float sdRoundBoxPx(vec2 p, vec2 halfSize, float radius)
{
    float r = clamp(radius, 0.0, min(halfSize.x, halfSize.y));
    vec2 q = abs(p) - halfSize + r;
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

// 圆角矩形遮罩：shape = (圆角px, 边缘柔化px)
float maskCoverage(vec2 pPx, vec2 pxSize, vec2 shape)
{
    vec2 halfSize = max(pxSize, vec2(1.0)) * 0.5;
    float radius = clamp(shape.x, 0.0, min(halfSize.x, halfSize.y));
    float sd = sdRoundBoxPx(pPx - halfSize, halfSize, radius);
    float aa = max(shape.y, 0.75);
    return 1.0 - smoothstep(-aa, aa, sd);
}

// 九宫格 UV：边框保持源像素宽度，中间区域拉伸。
//   b = (left, top, right, bottom)，单位都是源像素。
vec2 nineSliceUv(vec2 p, vec2 dst, vec2 src, vec4 b)
{
    vec2 s = max(src, vec2(1.0));
    vec2 uv;
    if (p.x < b.x)
        uv.x = p.x / s.x;
    else if (p.x > dst.x - b.z)
        uv.x = (s.x - (dst.x - p.x)) / s.x;
    else
    {
        float innerDst = max(dst.x - b.x - b.z, 1e-3);
        float innerSrc = max(s.x - b.x - b.z, 1e-3);
        uv.x = (b.x + (p.x - b.x) * innerSrc / innerDst) / s.x;
    }

    if (p.y < b.y)
        uv.y = p.y / s.y;
    else if (p.y > dst.y - b.w)
        uv.y = (s.y - (dst.y - p.y)) / s.y;
    else
    {
        float innerDst = max(dst.y - b.y - b.w, 1e-3);
        float innerSrc = max(s.y - b.y - b.w, 1e-3);
        uv.y = (b.y + (p.y - b.y) * innerSrc / innerDst) / s.y;
    }
    return clamp(uv, vec2(0.0), vec2(1.0));
}

// 三次贝塞尔展平：每段 4 个控制点（p0,c1,c2,p3），最多 4 段。
const int CURVE_STEPS = 8;
const int CURVE_MAX_PTS = 4 * CURVE_STEPS + 1;
int flattenCurve(vec2 ctrl[16], uint seg_count, vec2 scale, out vec2 pts[CURVE_MAX_PTS])
{
    int n = 0;
    int segs = clamp(int(seg_count), 0, 4);
    for (int s = 0; s < segs; ++s)
    {
        vec2 p0 = ctrl[4 * s + 0] * scale;
        vec2 c1 = ctrl[4 * s + 1] * scale;
        vec2 c2 = ctrl[4 * s + 2] * scale;
        vec2 p3 = ctrl[4 * s + 3] * scale;
        if (s == 0)
            pts[n++] = p0;
        for (int k = 1; k <= CURVE_STEPS; ++k)
        {
            float t = float(k) / float(CURVE_STEPS);
            float u = 1.0 - t;
            pts[n++] = u * u * u * p0 + 3.0 * u * u * t * c1 +
                       3.0 * u * t * t * c2 + t * t * t * p3;
        }
    }
    return n;
}

float sdSegmentPx(vec2 p, vec2 a, vec2 b)
{
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h);
}

float aaWidth(vec2 pPx)
{
    return max(max(fwidth(pPx.x), fwidth(pPx.y)), 1e-4);
}

vec4 sampleBindless(uint texIndex, uint sampIndex, vec2 uv)
{
    return texture(sampler2D(textures[nonuniformEXT(texIndex)],
                             samplers[nonuniformEXT(sampIndex)]),
                   uv);
}

ivec2 textureSizeBindless(uint texIndex, uint sampIndex)
{
    return textureSize(sampler2D(textures[nonuniformEXT(texIndex)],
                                 samplers[nonuniformEXT(sampIndex)]),
                       0);
}

float median(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}

// Color management for color-emoji bitmaps: the emoji atlas is an sRGB PNG
// stored in an UNORM texture, so its texels are sRGB-encoded.  The swapchain
// is B8G8R8A8_SRGB and expects linear shader output; convert once here instead
// of letting the hardware encode the already-encoded texel again.
vec3 srgbToLinear(vec3 c)
{
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)),
               step(vec3(0.04045), c));
}

float screenPxRangeTex(TexQuad inst, vec2 uv)
{
    ivec2 texSize = textureSizeBindless(inst.textureIndex, inst.samplerIndex);
    vec2 unitRange = vec2(inst.px_range) / vec2(texSize);
    vec2 screenTexSize = 1.0 / fwidth(uv);
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

vec4 shadeGlyphTex(TexQuad inst, vec4 texel, vec2 uv)
{
    // 所有分支都输出预乘色：bitmap 图集是直通 alpha，先乘 tint 再预乘。
    if (inst.glyph_mode == uint(FONT_NONE))
    {
        float a = texel.a * inst.color.a * inst.opacity;
        return vec4(inst.color.rgb * texel.rgb * a, a);
    }

    if (inst.glyph_mode == uint(FONT_BITMAP))
    {
        const vec3 tint = (inst.glyph_flags & 1u) != 0u ? inst.color.rgb : vec3(1.0);
        float a = texel.a * inst.color.a * inst.opacity;
        // texel.rgb is sRGB-encoded (UNORM atlas); output is linear.
        vec3 rgb = srgbToLinear(texel.rgb);
        return vec4(rgb * tint * a, a);
    }

    vec3 msd = texel.rgb;
    float sd = median(msd.r, msd.g, msd.b);
    float screenPxDistance = screenPxRangeTex(inst, uv) * (sd - 0.5);
    float opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);
    float a = opacity * inst.color.a * inst.opacity;
    if ((inst.glyph_flags & 1u) != 0u)
        return vec4(inst.color.rgb * a, a);
    return vec4(vec3(a), a);
}

// per-corner SDF：radii = TL,TR,BR,BL（p.y < 0 = 上边）
float sdRound4Px(vec2 p, vec2 halfSize, vec4 radii)
{
    float r = p.x < 0.0 ? (p.y < 0.0 ? radii.x : radii.w)
                        : (p.y < 0.0 ? radii.y : radii.z);
    r = clamp(r, 0.0, min(halfSize.x, halfSize.y));
    vec2 q = abs(p) - halfSize + r;
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

// 机制 0：Shape。fill / border / shadow / gradient / 四角半径都是参数。
vec4 drawShape(Shape inst, vec2 pPx, vec4 fillColor)
{
    vec2 halfSize = max(inst.px_size, vec2(1.0)) * 0.5;
    float sd = sdRound4Px(pPx - halfSize, halfSize, inst.radii_px);
    const float aa = 0.75;

    // shading：linear 走顶点色；radial/conic 是位置函数，仍然是 Shape 的参数
    if (inst.gradient.x == 1.0)
    {
        float t = clamp(length(pPx - inst.gradient.yz) / max(inst.gradient.w, 1e-3),
                        0.0, 1.0);
        fillColor = mix(inst.colors[0], inst.colors[1], t);
    }
    else if (inst.gradient.x == 2.0)
    {
        vec2 d = pPx - inst.gradient.yz;
        float ang = atan(d.y, d.x);
        float t = fract((ang + 3.14159265359) / 6.28318530718 *
                        max(inst.gradient.w, 1.0));
        fillColor = mix(inst.colors[0], inst.colors[1], t);
    }

    bool fillOn = (inst.effects & FX_FILL) != 0u;
    bool borderOn = (inst.effects & FX_BORDER) != 0u && inst.border_px > 0.0;
    bool shadowOn = (inst.effects & FX_SHADOW) != 0u && inst.shadow_color.a > 0.0;

    vec4 result = vec4(0.0);

    if (fillOn)
    {
        float cov = 1.0 - smoothstep(-aa, aa, sd);
        result = vec4(fillColor.rgb * fillColor.a * cov, fillColor.a * cov);
    }
    if (borderOn)
    {
        float soft = max(inst.border_soft_px, 0.5);
        float cov = smoothstep(-inst.border_px - soft, -inst.border_px + soft, sd) *
                    (1.0 - smoothstep(-soft, soft, sd));
        vec4 border = vec4(inst.border_color.rgb * inst.border_color.a * cov,
                           inst.border_color.a * cov);
        result = border + result * (1.0 - border.a);
    }
    if (shadowOn)
    {
        vec2 shadowHalf = halfSize * max(inst.spread, 0.0);
        float blur = max(inst.blur_px, 0.5);
        float sdShadow =
            sdRound4Px(pPx - halfSize - inst.shadow_offset_px, shadowHalf, inst.radii_px);
        float cov = 1.0 - smoothstep(-blur, blur, sdShadow);
        vec4 shadow = vec4(inst.shadow_color.rgb * inst.shadow_color.a * cov,
                           inst.shadow_color.a * cov);
        result = result + shadow * (1.0 - result.a);
    }
    // 程序化 alpha mask：maskFlags bit1 = 点阵（ability2 MODE_ALPHA_MASK），否则 checker
    if ((inst.maskFlags & 1u) != 0u)
    {
        const float cell = max(inst.maskParams.x, 4.0);
        vec2 q = pPx / cell;
        float m;
        if ((inst.maskFlags & 2u) != 0u)
        {
            vec2 g = fract(q) - 0.5;
            m = 1.0 - smoothstep(0.26, 0.42, length(g));
        }
        else
        {
            m = step(0.5, fract(q.x) + fract(q.y));
        }
        result *= m;
    }
    // 圆弧裁剪：以形状中心为极点，保留 [arc.x, arc.y] 角度区间
    float arc_span = inst.arc.y - inst.arc.x;
    if (arc_span > 0.0 && arc_span < 6.28318530718)
    {
        vec2 d = pPx - halfSize;
        float ang = atan(d.y, d.x);
        float rel = mod(ang - inst.arc.x, 6.28318530718);
        const float aaA = 0.01;
        float arcCov = smoothstep(0.0, aaA, rel) *
                       (1.0 - smoothstep(arc_span - aaA, arc_span + aaA, rel));
        result *= arcCov;
    }
    return result;
}

// 确定性 hash / value noise / fbm
float hash21(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main()
{
    outColor = fragColor;
    outPicking = uvec4(0xFFFFFFFF, 0, 0, 0);
    uint packed_z = (pc.render_version << RENDER_VERSION_BIT_WIDTH) |
                    (uint(gl_PrimitiveID) & PRIMITIVE_MASK);

    switch (program)
    {
    // ── 机制 0：Shape（SDF；fill / border / shadow 参数）────────────────
    case PROGRAM_SHAPE: {
        Shape inst = ShapeBuffer(instancePtr).shapes[0];
        vec2 pPx = (localPos + 0.5) * inst.px_size;
        outColor = drawShape(inst, pPx, fragColor);
        outPicking = uvec4(program, inst.entity_index, packed_z, inst.hover_fn);
    }
    break;
    // ── 机制 1：PathFill ─────────────────────────────────────────────────
    case PROGRAM_PATH_FILL: {
        PathFill inst = PathFillBuffer(instancePtr).fills[0];
        vec2 p = localPos + 0.5;
        vec2 pPx = p * inst.px_size;

        vec2 pts[CURVE_MAX_PTS];
        int n;
        if (inst.curve_count > 0u)
            n = flattenCurve(inst.curve, inst.curve_count, inst.px_size, pts);
        else
        {
            for (int i = 0; i < PATH_MAX_POINTS; ++i)
                pts[i] = inst.points[i] * inst.px_size;
            n = clamp(int(inst.point_count), 3, PATH_MAX_POINTS);
        }
        float d = 1e9;
        for (int i = 0; i < n; ++i)
            d = min(d, sdSegmentPx(pPx, pts[i], pts[(i + 1) % n]));

        bool inside = false;
        for (int i = 0, j = n - 1; i < n; j = i++)
        {
            vec2 a = pts[i];
            vec2 b = pts[j];
            if (((a.y > pPx.y) != (b.y > pPx.y)) &&
                (pPx.x < (b.x - a.x) * (pPx.y - a.y) / (b.y - a.y) + a.x))
                inside = !inside;
        }

        float aa = inst.aa_width > 0.0 ? max(inst.aa_width, aaWidth(pPx)) : 0.0;
        float cov = inst.aa_width > 0.0 ? smoothstep(-aa, aa, inside ? d : -d)
                                        : step(0.0, inside ? d : -d);
        vec4 c = inst.color;
        if ((inst.flags & 1u) != 0u)
            c = mix(inst.color, inst.color_b, clamp(p.y, 0.0, 1.0));
        float a = c.a * cov;
        outColor = vec4(c.rgb * a, a);
        outPicking = uvec4(program, inst.entity_index, packed_z, inst.hover_fn);
    }
    break;
    // ── 机制 2：PathStroke ───────────────────────────────────────────────
    case PROGRAM_PATH_STROKE: {
        PathStroke inst = PathStrokeBuffer(instancePtr).strokes[0];
        vec2 p = localPos + 0.5;
        vec2 pPx = p * inst.px_size;

        vec2 pts[CURVE_MAX_PTS];
        int n;
        if (inst.curve_count > 0u)
            n = flattenCurve(inst.curve, inst.curve_count, inst.px_size, pts);
        else
        {
            for (int i = 0; i < PATH_MAX_POINTS; ++i)
                pts[i] = inst.points[i] * inst.px_size;
            n = clamp(int(inst.point_count), 2, PATH_MAX_POINTS);
        }
        float d = 1e9;
        float best_s = 0.0; // 最近点在线上的弧长位置
        float acc = 0.0;
        for (int i = 0; i < n - 1; ++i)
        {
            vec2 a = pts[i];
            vec2 b = pts[i + 1];
            vec2 ab = b - a;
            float seg_len = length(ab);
            float dd = sdSegmentPx(pPx, a, b);
            if (dd < d)
            {
                d = dd;
                float t = clamp(dot(pPx - a, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
                best_s = acc + t * seg_len;
            }
            acc += seg_len;
        }

        const bool dashed = inst.dash.x > 0.0 && inst.dash.y > 0.0;
        if (!dashed && inst.cap != 0u)
        {
            d = min(d, length(pPx - pts[0]));
            d = min(d, length(pPx - pts[n - 1]));
        }

        float halfW = max(inst.width, 0.5) * 0.5;
        float aa = inst.aa_width > 0.0 ? max(inst.aa_width, aaWidth(pPx)) : 0.0;
        float cov = inst.aa_width > 0.0 ? 1.0 - smoothstep(halfW - aa, halfW + aa, d)
                                        : step(d, halfW);
        if (dashed)
        {
            float period = inst.dash.x + inst.dash.y;
            float m = mod(best_s, period);
            cov *= step(m, inst.dash.x);
        }
        vec4 c = mix(inst.color, inst.color_b, clamp(p.y, 0.0, 1.0));
        float a = c.a * cov;
        outColor = vec4(c.rgb * a, a);
        outPicking = uvec4(program, inst.entity_index, packed_z, inst.hover_fn);
    }
    break;
    // ── 机制 3：Texture（含 glyph metadata）──────────────────────────────
    case PROGRAM_TEXTURE: {
        TexQuad inst = TexQuadBuffer(instancePtr).quads[0];
        vec2 pPx = (localPos + 0.5) * inst.px_size;
        vec2 uv = fragTexCoord;
        if (inst.border.x + inst.border.y + inst.border.z + inst.border.w > 0.0)
            uv = nineSliceUv(pPx, inst.px_size, inst.src_size, inst.border);

        vec4 tex;
        if (inst.lodMode != 0u)
            tex = textureLod(sampler2D(textures[nonuniformEXT(inst.textureIndex)],
                                       samplers[nonuniformEXT(inst.samplerIndex)]),
                             uv, inst.lod);
        else
            tex = sampleBindless(inst.textureIndex, inst.samplerIndex, uv);
        vec4 result;

        if ((inst.glyph_flags & 2u) != 0u)
        {
            result = shadeGlyphTex(inst, tex, fragTexCoord);
        }
        else
        {
            float cov = maskCoverage(pPx, inst.px_size, inst.shape);
            float a = tex.a * inst.color.a * inst.opacity * cov;
            if ((inst.maskFlags & 1u) != 0u)
            {
                // 纹理 alpha mask（ability2 MODE_ALPHA_MASK_TEX）
                vec2 muv = inst.maskUv.offset + fragTexCoord * inst.maskUv.scale;
                a *= sampleBindless(inst.maskTextureIndex, inst.maskSamplerIndex, muv).a;
            }
            result = vec4(tex.rgb * inst.color.rgb * a, a);
        }
        outColor = result;
        outPicking = uvec4(program, inst.entity_index, packed_z, inst.hover_fn);
    }
    break;
    // ── 机制 4：Blur（邻域卷积）────────────────────────────────────────
    case PROGRAM_BLUR: {
        BlurQuad inst = BlurQuadBuffer(instancePtr).quads[0];
        vec2 pPx = (localPos + 0.5) * inst.px_size;
        float cov = maskCoverage(pPx, inst.px_size, inst.shape);

        ivec2 texSize = textureSizeBindless(inst.textureIndex, inst.samplerIndex);
        vec2 texel = 1.0 / vec2(max(texSize, ivec2(1)));
        int taps = clamp(int(inst.taps), 1, 6);
        float stepSize = max(inst.radius, 0.0) / float(taps);
        vec2 stepUV = texel * stepSize;
        float sigma2 = max(float(taps * taps) * 0.5, 0.25);

        vec4 sum = vec4(0.0);
        float wsum = 0.0;
        for (int y = -taps; y <= taps; ++y)
        {
            for (int x = -taps; x <= taps; ++x)
            {
                float w = exp(-0.5 * float(x * x + y * y) / sigma2);
                sum += sampleBindless(inst.textureIndex, inst.samplerIndex,
                                      fragTexCoord + vec2(x, y) * stepUV) * w;
                wsum += w;
            }
        }
        vec4 tex = sum / max(wsum, 1e-6);
        float a = tex.a * inst.color.a * cov;
        outColor = vec4(tex.rgb * inst.color.rgb * a, a);
        outPicking = uvec4(program, inst.entity_index, packed_z, inst.hover_fn);
    }
    break;
    // ── 机制 5：ColorMatrix（逐点矩阵）──────────────────────────────────
    case PROGRAM_COLOR_MATRIX: {
        ColorMatrix inst = ColorMatrixBuffer(instancePtr).quads[0];
        vec2 pPx = (localPos + 0.5) * inst.px_size;
        float cov = maskCoverage(pPx, inst.px_size, inst.shape);

        vec4 tex = sampleBindless(inst.textureIndex, inst.samplerIndex, fragTexCoord);
        vec4 m = inst.matrix * vec4(tex.rgb, tex.a) + inst.offset;
        vec3 rgb = clamp(m.rgb, 0.0, 1.0);
        float a = tex.a * inst.color.a * inst.opacity * cov;
        outColor = vec4(rgb * inst.color.rgb * a, a);
        outPicking = uvec4(program, inst.entity_index, packed_z, inst.hover_fn);
    }
    break;
    // ── 机制 6：Noise（程序化）──────────────────────────────────────────
    case PROGRAM_NOISE: {
        NoiseQuad inst = NoiseQuadBuffer(instancePtr).quads[0];
        vec2 pPx = (localPos + 0.5) * inst.px_size;
        float cov = maskCoverage(pPx, inst.px_size, inst.shape);

        vec2 np = pPx / max(min(inst.px_size.x, inst.px_size.y), 1.0);
        np = np * inst.frequency + vec2(inst.seed * 17.13, inst.seed * 9.71);

        float n = 0.0;
        if ((inst.flags & 1u) != 0u)
        {
            int oct = clamp(int(inst.octaves), 1, 6);
            float amp = 0.5;
            float freq = 1.0;
            float sum = 0.0;
            float norm = 0.0;
            for (int i = 0; i < oct; ++i)
            {
                sum += amp * valueNoise(np * freq);
                norm += amp;
                freq *= 2.02;
                amp *= 0.5;
            }
            n = sum / max(norm, 1e-6);
        }
        else
        {
            n = valueNoise(np);
        }
        n = smoothstep(0.15, 0.85, n);

        vec4 c = mix(inst.color_a, inst.color_b, n);
        float a = c.a * cov;
        outColor = vec4(c.rgb * a, a);
        outPicking = uvec4(program, inst.entity_index, packed_z, inst.hover_fn);
    }
    break;
    // ── 机制 7：Gradient（多 stop；linear / radial / conic）──────────────
    case PROGRAM_GRADIENT: {
        GradientQuad inst = GradientQuadBuffer(instancePtr).quads[0];
        vec2 pPx = (localPos + 0.5) * inst.px_size;
        float cov = maskCoverage(pPx, inst.px_size, inst.shape);

        float t;
        if (inst.mode == 1u)
        {
            t = length(pPx - inst.p0) / max(inst.p1.x, 1e-3);
        }
        else if (inst.mode == 2u)
        {
            vec2 d = pPx - inst.p0;
            t = fract((atan(d.y, d.x) + 3.14159265359) / 6.28318530718 *
                      max(inst.p1.x, 1.0));
        }
        else
        {
            vec2 ab = inst.p1 - inst.p0;
            t = dot(pPx - inst.p0, ab) / max(dot(ab, ab), 1e-6);
        }
        t = clamp(t, 0.0, 1.0);

        int n = clamp(int(inst.stop_count), 2, 8);
        vec4 c = inst.stop_color[0];
        for (int i = 0; i < 7; ++i)
        {
            if (i + 1 >= n)
                break;
            float a = inst.stop_pos[i];
            float b = inst.stop_pos[i + 1];
            if (t >= a)
                c = mix(inst.stop_color[i], inst.stop_color[i + 1],
                        clamp((t - a) / max(b - a, 1e-6), 0.0, 1.0));
        }

        float alpha = c.a * inst.color.a * inst.opacity * cov;
        outColor = vec4(c.rgb * inst.color.rgb * alpha, alpha);
        outPicking = uvec4(program, inst.entity_index, packed_z, inst.hover_fn);
    }
    break;
    default:
        break;
    }
}
