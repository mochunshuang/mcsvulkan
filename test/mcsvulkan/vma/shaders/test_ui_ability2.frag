#version 450
// ═══════════════════════════════════════════════════════════════════════════
// UI ability uber shader
//
// 一条 UI 命令 = 一个全屏三角形 + 一组推送常量（没有顶点缓冲、没有实例缓冲）。
// 所有能力都在这里用 mode 分支实现，C++ 侧只负责按顺序 pushConstants + draw。
//
// 约定（C++ 侧 PushData 必须完全一致）：
//   rect   : xy = 命令矩形左上角（framebuffer 像素，y 向下），zw = 宽高（像素）
//   color  : 主色            color2 : 次色（渐变终点/阴影色/底色）
//   params : x = mode，y/z/w = 模式参数
//   aux    : 追加参数（线端点 / 偏移 / 角度 / 图集 UV / 拾取 id）
//
// 输出：预乘 alpha（管线混合为 ONE / ONE_MINUS_SRC_ALPHA）。
// ═══════════════════════════════════════════════════════════════════════════
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 vUV;
layout(location = 1) flat in uint vSegIndex;
layout(set = 0, binding = 3) readonly buffer SegmentBuffer
{
    vec4 segs[];
} segBuf;
layout(location = 0) out vec4 outColor;
layout(location = 1) out uvec4 outPicking;

layout(set = 0, binding = 1) uniform texture2D uTextures[];
layout(set = 0, binding = 2) uniform sampler uSamplers[];

layout(push_constant) uniform PC
{
    uint64_t vertexAddress;
    uint64_t dataAddress;
    uint64_t commandConstantsAddress;
    uint geomEntry;
    uint renderVersion;

    vec4 rect;
    vec4 color;
    vec4 color2;
    vec4 params;
    vec4 aux;
    uvec4 pick; // 拾取：x=命令 id y=实体 z=(version<<16)|primitive w=hover_fn
} pc;

#define MODE_FILL 0          // 圆角矩形填充         y=圆角
#define MODE_LINEAR 1        // 线性渐变             y=圆角 aux.xy=方向
#define MODE_RADIAL 2        // 径向渐变             y=半径 z=圆角
#define MODE_CONIC 3         // 圆锥渐变             y=相位 z=圆角
#define MODE_ARC 4           // 圆弧/环              y=半径 z=线宽 aux.xy=起止角
#define MODE_OUTER_SHADOW 5  // 外阴影               y=圆角 z=模糊 aux.xy=偏移
#define MODE_INNER_SHADOW 6  // 内阴影               y=圆角 z=模糊
#define MODE_PERCORNER 7     // 每角半径             y/z/w/aux.x = TL/TR/BR/BL
#define MODE_ALPHA_MASK 8    // alpha 遮罩           y=圆角 z=遮罩格
#define MODE_PATH_AA 9       // 路径解析抗锯齿       y=边宽（0=关闭）
#define MODE_DASH 10         // 虚线描边             y=线宽 z=实长 w=虚长 aux=端点
#define MODE_ROTATE 11       // 旋转                 y=角度 z=圆角
#define MODE_SKEW 12         // 斜切                 y=圆角 aux.xy=斜切系数
#define MODE_NINE_SLICE 13   // 九宫格               y=边宽 z=圆角
#define MODE_EVENODD 14      // 奇偶填充（星形）     y=内半径比例
#define MODE_GLYPH 15        // MSDF 字形            y=distanceRange z=纹理 w=采样器 aux=UV
#define MODE_GLYPH_BITMAP 33 // 位图字形（emoji）：图集是 RGBA，不是距离场
#define MODE_SRGB 16         // sRGB / 线性渐变      y=圆角
#define MODE_BLUR 17         // 模糊                 y=模糊半径
#define MODE_BACKDROP 18     // 背景模糊             y=模糊半径 z=面板圆角
#define MODE_GROUP 19        // 组不透明度           z=组 alpha
#define MODE_MIPMAP 20       // mipmap 采样          y=棋盘频率
#define MODE_PICKING 21      // 拾取                 y=圆角 z=id aux.xy=实体
#define MODE_BLEND 22        // 混合模式             over / add / multiply
#define MODE_LAYER_BLUR 23   // 离屏可分离模糊       y=方向(0=横/1=纵) z=半径像素 w=源纹理 aux.x=源采样器
#define MODE_LAYER_COMPOSITE 24 // 离屏合成          y=不透明度 z=源纹理 w=源采样器 aux.x=圆角
#define MODE_CHECKER 25      // 棋盘内容             y=格边长像素
#define MODE_NINE_SLICE_TEX 26 // 纹理九宫格          y=源边框像素 z=源纹理 w=源采样器 aux.xy=源尺寸
#define MODE_ALPHA_MASK_TEX 27 // 纹理 alpha 遮罩     z=纹理 w=源采样器
#define MODE_MIPMAP_TEX 28     // 真 mip 采样         y=最大 lod z=纹理 w=源采样器
#define MODE_CLIP_MASK 29      // 模板裁剪遮罩        y=圆角（形状外 discard，只留模板写入）
#define MODE_SOLID 30          // 纯色填充（覆盖 pass 用，不算形状）
#define MODE_SOLID_GEOM 31     // 已废弃：改用 MODE_SOLID（颜色现在由推送常量带）
#define MODE_PATH_STROKE 32    // 路径描边：解析覆盖率 AA（y=半宽 z=1 开 AA）

const float PI = 3.14159265359;
const float TAU = 6.28318530718;

// ── SDF ────────────────────────────────────────────────────────────────────
float sdRound(vec2 p, vec2 b, float r)
{
    r = clamp(r, 0.0, min(b.x, b.y));
    vec2 q = abs(p) - b + vec2(r);
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

float sdRound4(vec2 p, vec2 b, vec4 r)
{
    // p.y < 0 = 上边（framebuffer y 向下）
    float radius = p.x < 0.0 ? (p.y < 0.0 ? r.x : r.w) : (p.y < 0.0 ? r.y : r.z);
    radius = clamp(radius, 0.0, min(b.x, b.y));
    vec2 q = abs(p) - b + vec2(radius);
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - radius;
}

float sdTriangle(vec2 p, vec2 p0, vec2 p1, vec2 p2)
{
    vec2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;
    vec2 v0 = p - p0, v1 = p - p1, v2 = p - p2;
    vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / max(dot(e0, e0), 1e-6), 0.0, 1.0);
    vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / max(dot(e1, e1), 1e-6), 0.0, 1.0);
    vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / max(dot(e2, e2), 1e-6), 0.0, 1.0);
    float s = sign(e0.x * e2.y - e0.y * e2.x);
    vec2 d = min(min(vec2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
                     vec2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
                 vec2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(d.x) * sign(d.y);
}

// ── 小工具 ─────────────────────────────────────────────────────────────────
float cov(float d) { return clamp(0.5 - d, 0.0, 1.0); } // 1px 解析抗锯齿
vec4 premul(vec4 c, float a) { return vec4(c.rgb * c.a * a, c.a * a); }
float median3(vec3 v) { return max(min(v.r, v.g), min(max(v.r, v.g), v.b)); }

float checker(vec2 q)
{
    return step(0.5, fract(q.x) + fract(q.y));
}

// 九宫格的单轴映射：两端固定(source border)，中间拉满
float nine_slice_axis(float d, float t, float s, float sb, float db)
{
    float b = min(db, t * 0.5);
    float k = sb / max(b, 1e-3);
    if (d < b)
        return d * k; // 起始固定段
    if (d > t - b)
        return s - (t - d) * k; // 结尾固定段
    return sb + (d - b) * (s - 2.0 * sb) / max(t - 2.0 * b, 1e-3); // 中间拉伸段
}

void main()
{
    // ── 命令矩形 → 图块局部坐标（像素，左上角原点，y 向下）────────────────
    vec2 tile = max(pc.rect.zw, vec2(1.0));
    vec2 p = gl_FragCoord.xy - pc.rect.xy;
    vec2 uv = p / tile;
    vec2 hs = tile * 0.5;
    vec2 c = p - hs;

    int mode = int(pc.params.x + 0.5);
    float r = max(pc.params.y, 0.0);

    vec4 col = vec4(0.0);

    if (mode == MODE_FILL)
    {
        col = premul(pc.color, cov(sdRound(c, hs, r)));
    }
    else if (mode == MODE_LINEAR)
    {
        vec2 dir = pc.aux.xy;
        if (dot(dir, dir) < 1e-6)
            dir = vec2(1.0, 0.0);
        dir = normalize(dir);
        float t = clamp(dot(uv - 0.5, dir) + 0.5, 0.0, 1.0);
        col = premul(mix(pc.color, pc.color2, t), cov(sdRound(c, hs, r)));
    }
    else if (mode == MODE_RADIAL)
    {
        float t = clamp(length(c) / max(r, 1.0), 0.0, 1.0);
        col = premul(mix(pc.color, pc.color2, t), cov(sdRound(c, hs, pc.params.z)));
    }
    else if (mode == MODE_CONIC)
    {
        float a = fract(atan(c.y, c.x) / TAU + 0.5 + r);
        col = premul(mix(pc.color, pc.color2, a), cov(sdRound(c, hs, pc.params.z)));
    }
    else if (mode == MODE_ARC)
    {
        float band = abs(length(c) - r) - max(pc.params.z, 0.5) * 0.5;
        float ang = atan(c.y, c.x);
        float a0 = pc.aux.x, a1 = pc.aux.y;
        float m = (a0 <= a1) ? step(a0, ang) * step(ang, a1)
                             : max(step(a0, ang), step(ang, a1));
        col = premul(pc.color, cov(band) * m);
    }
    else if (mode == MODE_OUTER_SHADOW)
    {
        vec2 off = pc.aux.xy;
        float blur = max(pc.params.z, 0.5);
        float sh = (1.0 - smoothstep(0.0, blur, sdRound(c - off, hs, r))) * pc.color2.a;
        float f = cov(sdRound(c, hs, r));
        vec4 fore = premul(pc.color, f);
        vec4 back = vec4(pc.color2.rgb * sh, sh);
        col = fore + back * (1.0 - fore.a);
    }
    else if (mode == MODE_INNER_SHADOW)
    {
        float blur = max(pc.params.z, 1.0);
        float d = sdRound(c, hs, r);
        float f = clamp(1.0 + d / blur, 0.0, 1.0) * pc.color2.a;
        col = premul(mix(pc.color, pc.color2, f), cov(d));
    }
    else if (mode == MODE_PERCORNER)
    {
        col = premul(pc.color,
                     cov(sdRound4(c, hs, vec4(pc.params.y, pc.params.z, pc.params.w,
                                              pc.aux.x))));
    }
    else if (mode == MODE_ALPHA_MASK)
    {
        vec2 g = fract(p / max(pc.params.z, 4.0)) - 0.5;
        float m = 1.0 - smoothstep(0.26, 0.42, length(g));
        col = premul(pc.color, cov(sdRound(c, hs, r)) * m);
    }
    else if (mode == MODE_PATH_AA)
    {
        // 一条三角形路径：覆盖率来自解析 SDF（对比 1px smoothstep 的观感）
        vec2 p0 = vec2(hs.x, tile.y * 0.14);
        vec2 p1 = vec2(tile.x * 0.12, tile.y * 0.88);
        vec2 p2 = vec2(tile.x * 0.88, tile.y * 0.88);
        col = premul(pc.color, cov(sdTriangle(p, p0, p1, p2)));
    }
    else if (mode == MODE_DASH)
    {
        vec2 a = pc.aux.xy;
        vec2 b = pc.aux.zw;
        vec2 ab = b - a;
        float lab = max(length(ab), 1e-3);
        vec2 dir = ab / lab;
        vec2 pa = p - a;
        float along = dot(pa, dir);
        float d = length(pa - dir * clamp(along, 0.0, lab)) - pc.params.y * 0.5;
        float period = max(pc.params.z + pc.params.w, 1e-3);
        float on = step(mod(max(along, 0.0), period), pc.params.z);
        col = premul(pc.color, cov(d) * on);
    }
    else if (mode == MODE_ROTATE)
    {
        float ang = pc.params.y;
        mat2 rot = mat2(cos(ang), -sin(ang), sin(ang), cos(ang));
        col = premul(pc.color, cov(sdRound(rot * c, hs * 0.72, pc.params.z)));
    }
    else if (mode == MODE_SKEW)
    {
        mat2 skew = mat2(1.0, pc.aux.y, pc.aux.x, 1.0);
        col = premul(pc.color, cov(sdRound(skew * c, hs * 0.8, r)));
    }
    else if (mode == MODE_NINE_SLICE)
    {
        // 3x3 源图（每格固定颜色）+ 边框：中间拉伸、四角不变
        const vec3 pal[9] = vec3[9](
            vec3(0.90, 0.20, 0.20), vec3(0.20, 0.80, 0.30), vec3(0.20, 0.40, 0.90),
            vec3(0.90, 0.80, 0.20), vec3(0.95, 0.95, 0.95), vec3(0.20, 0.85, 0.85),
            vec3(0.80, 0.30, 0.85), vec3(0.95, 0.55, 0.20), vec3(0.55, 0.55, 0.55));
        ivec2 cell = ivec2(clamp(uv * 3.0, vec2(0.0), vec2(2.999)));
        vec3 rgb = pal[cell.y * 3 + cell.x];
        float outer = cov(sdRound(c, hs, pc.params.z));
        float inner = cov(sdRound(c, hs - vec2(pc.params.y), max(pc.params.z - pc.params.y, 0.0)));
        vec3 border = mix(rgb, pc.color2.rgb, outer * (1.0 - inner));
        col = premul(vec4(border, 1.0), outer);
    }
    else if (mode == MODE_EVENODD)
    {
        // 奇偶（even-odd）填充：五角星，10 条边做跳数
        float outerR = min(hs.x, hs.y) * 0.95;
        float innerR = outerR * clamp(r > 0.0 ? r : 0.45, 0.05, 1.0);
        vec2 pts[10];
        for (int i = 0; i < 10; ++i)
        {
            float ang = -PI * 0.5 + float(i) * PI / 5.0;
            float rad = (i % 2 == 0) ? outerR : innerR;
            pts[i] = hs + vec2(cos(ang), sin(ang)) * rad;
        }
        bool inside = false;
        for (int i = 0; i < 10; ++i)
        {
            vec2 a = pts[i];
            vec2 b = pts[(i + 1) % 10];
            if (((a.y > p.y) != (b.y > p.y)) &&
                (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x))
                inside = !inside;
        }
        col = premul(pc.color, inside ? 1.0 : 0.0);
    }
    else if (mode == MODE_GLYPH)
    {
        // MSDF 字形：图块内 uv 映射到图集 uv，再按屏幕导数算距离场缩放
        uint ti = uint(pc.params.z + 0.5);
        uint si = uint(pc.params.w + 0.5);
        vec2 guv = mix(pc.aux.xy, pc.aux.zw, clamp(uv, vec2(0.0), vec2(1.0)));
        vec3 msd = texture(sampler2D(uTextures[nonuniformEXT(ti)],
                                     uSamplers[nonuniformEXT(si)]),
                           guv)
                       .rgb;
        float sd = median3(msd);
        ivec2 texSize = textureSize(sampler2D(uTextures[nonuniformEXT(ti)],
                                              uSamplers[nonuniformEXT(si)]),
                                    0);
        vec2 unitRange = vec2(max(pc.params.y, 0.001)) / vec2(texSize);
        vec2 screenTexSize = vec2(1.0) / max(fwidth(guv), vec2(1e-6));
        float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);
        float a = clamp(screenPxRange * (sd - 0.5) + 0.5, 0.0, 1.0);
        col = premul(pc.color, a * cov(sdRound(c, hs, 0.0)));
    }
    else if (mode == MODE_GLYPH_BITMAP)
    {
        // 位图字形（emoji 等）：图集里就是 RGBA 颜色，不是距离场
        //   不能走 median3 / distanceRange，直接采样后按 alpha 合成预乘色
        uint ti = uint(pc.params.z + 0.5);
        uint si = uint(pc.params.w + 0.5);
        vec2 guv = mix(pc.aux.xy, pc.aux.zw, clamp(uv, vec2(0.0), vec2(1.0)));
        vec4 tex = texture(sampler2D(uTextures[nonuniformEXT(ti)],
                                     uSamplers[nonuniformEXT(si)]),
                           guv);
        float a = tex.a * cov(sdRound(c, hs, 0.0));
        // 图集是直通 alpha：先乘 UI 颜色再预乘
        col = vec4(tex.rgb * pc.color.rgb * pc.color.a * a, pc.color.a * a);
    }
    else if (mode == MODE_SRGB)
    {
        // 上半：gamma 空间插值；下半：线性空间插值（同一个渐变，观感不同）
        float t = uv.x;
        vec3 lin = mix(pc.color.rgb, pc.color2.rgb, t);
        vec3 gam = mix(pow(pc.color.rgb, vec3(1.0 / 2.2)),
                       pow(pc.color2.rgb, vec3(1.0 / 2.2)), t);
        vec3 rgb = (uv.y < 0.5) ? gam : lin;
        col = premul(vec4(rgb, 1.0), cov(sdRound(c, hs, r)));
    }
    else if (mode == MODE_MIPMAP)
    {
        // mipmap：四条横带，同一张棋盘逐级放大格子（像一条 mip 链）
        float band = clamp(floor(uv.y * 4.0), 0.0, 3.0);
        float cell = 4.0 * exp2(band);
        vec2 g = floor(p / cell);
        col = premul(mix(pc.color, pc.color2, mod(g.x + g.y, 2.0)), 1.0);
    }
    else if (mode == MODE_PICKING)
    {
        // 拾取 id 由推送常量的 pc.pick 提供，这里只负责画形状
        col = premul(pc.color, cov(sdRound(c, hs, r)));
    }
    else if (mode == MODE_BLEND)
    {
        // 三种混合：over / add / multiply（同一张源图，三段不同合成公式）
        vec3 base = pc.color2.rgb;
        vec3 src = pc.color.rgb;
        float a1 = cov(length(p - vec2(tile.x * 0.18, hs.y)) - hs.y * 0.72);
        float a2 = cov(length(p - vec2(tile.x * 0.50, hs.y)) - hs.y * 0.72);
        float a3 = cov(length(p - vec2(tile.x * 0.82, hs.y)) - hs.y * 0.72);
        vec3 res = base;
        res = mix(res, src, a1);
        res = mix(res, min(vec3(1.0), res + src * 0.85), a2);
        res = mix(res, res * src, a3);
        col = premul(vec4(res, 1.0), 1.0);
    }
    else if (mode == MODE_LAYER_BLUR)
    {
        // 可分离高斯：一次只沿一个方向采样，横一趟、纵一趟
        vec2 uv = p / tile;
        vec2 dir = (pc.params.y < 0.5) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
        float radius = max(pc.params.z, 0.0);
        uint ti = uint(pc.params.w + 0.5);
        uint si = uint(pc.aux.x + 0.5);
        vec4 sum = vec4(0.0);
        float wsum = 0.0;
        for (int i = -8; i <= 8; ++i)
        {
            float fi = float(i);
            float w = exp(-0.5 * (fi / 3.0) * (fi / 3.0));
            vec2 off = dir * (fi * radius / tile);
            sum += texture(sampler2D(uTextures[nonuniformEXT(ti)],
                                     uSamplers[nonuniformEXT(si)]),
                           uv + off) *
                   w;
            wsum += w;
        }
        col = sum / max(wsum, 1e-5); // 层里存的是预乘色，直接加权平均即可
    }
    else if (mode == MODE_LAYER_COMPOSITE)
    {
        // 把离屏层按 uv 铺回目标矩形，整体乘一个不透明度（组不透明度就是这么来的）
        uint ti = uint(pc.params.z + 0.5);
        uint si = uint(pc.params.w + 0.5);
        // aux.yz > 0 时表示：层里存的是整屏内容，需要把图块在屏幕上的位置映射成 uv
        vec2 uv = (pc.aux.y * pc.aux.z > 0.0)
                      ? clamp((pc.rect.xy + p) / pc.aux.yz, vec2(0.0), vec2(1.0))
                      : clamp(p / tile, vec2(0.0), vec2(1.0));
        vec4 layerColor = texture(sampler2D(uTextures[nonuniformEXT(ti)],
                                            uSamplers[nonuniformEXT(si)]),
                                  uv);
        float a = clamp(pc.params.y, 0.0, 1.0) * cov(sdRound(c, hs, pc.aux.x));
        col = vec4(layerColor.rgb * a, layerColor.a * a);
    }
    else if (mode == MODE_CHECKER)
    {
        // 棋盘内容：背景模糊里的“背景”
        float cell = max(pc.params.y, 2.0);
        vec2 g = floor(p / cell);
        col = premul(mix(pc.color, pc.color2, mod(g.x + g.y, 2.0)), 1.0);
    }
    else if (mode == MODE_NINE_SLICE_TEX)
    {
        // 真·九宫格：3x3 分区 UV 重映射，边角保持像素宽，中间行/列拉伸
        uint ti = uint(pc.params.z + 0.5);
        uint si = uint(pc.params.w + 0.5);
        vec2 srcSize = max(pc.aux.xy, vec2(1.0));
        float sb = max(pc.params.y, 0.0);      // 源图边框宽（像素）
        vec2 db = vec2(sb);                    // 目标边框宽（像素，必要时收窄）
        vec2 src;
        src.x = nine_slice_axis(p.x, tile.x, srcSize.x, sb, min(db.x, sb));
        src.y = nine_slice_axis(p.y, tile.y, srcSize.y, sb, min(db.y, sb));
        vec2 uv = clamp(src / srcSize, vec2(0.0), vec2(1.0));
        vec4 texel = texture(sampler2D(uTextures[nonuniformEXT(ti)],
                                       uSamplers[nonuniformEXT(si)]),
                             uv);
        col = premul(texel, 1.0);
    }
    else if (mode == MODE_ALPHA_MASK_TEX)
    {
        // 真·纹理 alpha 遮罩：alpha 来自纹理，不是程序化点阵
        uint ti = uint(pc.params.z + 0.5);
        uint si = uint(pc.params.w + 0.5);
        vec2 uv = clamp(p / tile, vec2(0.0), vec2(1.0));
        float mask = texture(sampler2D(uTextures[nonuniformEXT(ti)],
                                       uSamplers[nonuniformEXT(si)]),
                             uv)
                         .a;
        col = premul(pc.color, mask * cov(sdRound(c, hs, r)));
    }
    else if (mode == MODE_MIPMAP_TEX)
    {
        // 真·mip 采样：同一张棋盘按 lod=0..3 采样，四条横带
        uint ti = uint(pc.params.z + 0.5);
        uint si = uint(pc.params.w + 0.5);
        vec2 uv = clamp(p / tile, vec2(0.0), vec2(1.0));
        float band = clamp(floor(uv.y * 4.0), 0.0, 3.0);
        float lod = min(band, max(pc.params.y, 0.0));
        vec4 texel = textureLod(sampler2D(uTextures[nonuniformEXT(ti)],
                                          uSamplers[nonuniformEXT(si)]),
                                uv, lod);
        col = premul(texel, 1.0);
    }
    else if (mode == MODE_SOLID)
    {
        // 覆盖 pass：形状已经由几何/模板决定，这里只填色
        col = premul(pc.color, 1.0);
    }
    else if (mode == MODE_SOLID_GEOM) // 兼容旧命令：等价于 MODE_SOLID
    {
        col = premul(pc.color, 1.0);
    }
    else if (mode == MODE_PATH_STROKE)
    {
        // 解析覆盖率（胶囊）：片元到该段的最短距离，1px 过渡带
        //   NOTE: miter 两次尝试都出问题（外角戳刺 / 径向断条），暂回圆头，
        //         数据侧已备好斜接点与角平分平面，接回需单独一课
        vec4 ab = segBuf.segs[vSegIndex * 5u + 2u];
        vec2 a = ab.xy;
        vec2 b = ab.zw;
        vec2 pa = gl_FragCoord.xy - a;
        vec2 ba = b - a;
        float t = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-4), 0.0, 1.0);
        float d = length(pa - ba * t) - pc.params.y; // 负=在笔画内
        float coverage =
            (pc.params.z > 0.5) ? clamp(0.5 - d, 0.0, 1.0) : (d <= 0.0 ? 1.0 : 0.0);
        col = premul(pc.aux, coverage);
    }
    else if (mode == MODE_CLIP_MASK)
    {
        // 只做模板：形状外丢弃，形状内写 0 颜色（颜色的写掩码在管线里关掉）
        float d = sdRound(c, hs, r);
        if (d > 0.5)
            discard;
        col = vec4(0.0);
    }
    else
    {
        col = premul(pc.color, cov(sdRound(c, hs, r)));
    }

    // 完全透明的图元：颜色写 0（预乘 alpha 混合下等于不画），
    // 拾取写 0xFFFFFFFF（= 没命中）。不用 discard：SPIR-V 1.6 会把 discard
    // 降级成 DemoteToHelperInvocation，那需要额外开启设备特性。
    const bool visible = col.a > 0.0;
    const bool has_pick = pc.pick.x != 0xFFFFFFFFu;
    outColor = visible ? col : vec4(0.0);
    outPicking = (visible && has_pick) ? pc.pick : uvec4(0xFFFFFFFFu, 0xFFFFFFFFu, 0u, 0u);
}
