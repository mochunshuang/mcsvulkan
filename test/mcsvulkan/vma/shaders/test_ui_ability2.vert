#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
layout(location = 0) out vec2 vUV;
layout(location = 1) flat out uint vSegIndex;
// 路径段数据（描边批处理）：每段 3 个 vec4 = 4 个 quad 角 + 2 个端点
layout(set = 0, binding = 3) readonly buffer SegmentBuffer
{
    vec4 segs[];
} segBuf;
layout(push_constant) uniform PC_geo
{
    uint64_t vertexAddress;
    uint64_t dataAddress;
    uint64_t commandConstantsAddress;
    uint geomEntry; // 几何入口 id：0=普通全屏三角形，1=描边批处理，2=填充扇形批处理
    uint renderVersion;
    vec4 rect;   // xy / zw = 多边形点 0 / 1
    vec4 color;  // xy / zw = 多边形点 2 / 3
    vec4 color2; // xy = 多边形点 4
    vec4 params;
    vec4 aux;
    uvec4 pick;
} pcg;

const uint kFanBase = 2048u; // 填充扇形的顶点起点（描边段数据占低位）

void main()
{
    // ── 几何入口（显式标记，不再用 996/997/998/999 这种散落魔数）──
    //   1 = 描边批处理（段数据在 storage buffer）
    //   2 = 填充扇形批处理（顶点在 storage buffer 的高位区域）
    if (pcg.geomEntry == 2u)
    {
        // 数据按“全局顶点号”排在 buffer 高位区，所以直接用 gl_VertexIndex 索引
        uint v = uint(gl_VertexIndex);
        vec4 pack = segBuf.segs[v / 2u];
        vec2 pos = ((v & 1u) == 0u) ? pack.xy : pack.zw;
        gl_Position = vec4(pos, 0.0, 1.0);
        vUV = pos * 0.5 + 0.5;
        vSegIndex = 0u;
        return;
    }
    if (pcg.geomEntry == 1u)
    {
        uint seg = uint(gl_VertexIndex) / 6u;
        uint corner = uint(gl_VertexIndex) % 6u;
        uint idx = (corner < 3u) ? corner : (5u - corner); // 0,1,2,2,1,3
        vec4 q01 = segBuf.segs[seg * 5u + 0u]; // q0, q1
        vec4 q23 = segBuf.segs[seg * 5u + 1u]; // q2, q3
        vec2 corners[4] = vec2[4](q01.xy, q01.zw, q23.xy, q23.zw);
        gl_Position = vec4(corners[idx], 0.0, 1.0);
        vUV = corners[idx] * 0.5 + 0.5;
        vSegIndex = seg;
        return;
    }
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}

