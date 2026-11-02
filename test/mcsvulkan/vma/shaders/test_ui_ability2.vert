#version 450
// ═══════════════════════════════════════════════════════════════════════════
// Recorder 数据协议（顶点阶段）
//
// 一条间接绘制命令 = 一个 CommandConstant{type_id, address_offset} + 一段 heap。
// 顶点阶段只负责“按 type_id 解释 heap 里的字节”，不参与任何 UI 语义：
//
//   type 1 = 实例四边形   heap[offset + i*96] 是第 i 个 DrawInstance，
//                         每个实例用自带的 rect 展开成 2 个三角形（6 顶点）
//   type 2 = 路径描边批   heap[offset + 96] 起是 PathSegment（每段 5 个 vec4），
//                         每段 6 顶点，索引 0,1,2,2,1,3 组成两个三角形
//   type 3 = 路径扇形批   heap[offset + 96] 起是 vec2 顶点表（NDC）
//
// 路径 payload 前面都放一个 DrawInstance 头（颜色 / 参数），片元阶段靠它统一取值。
// ═══════════════════════════════════════════════════════════════════════════
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

// ── GPU 端结构体：C++ shader_data 必须逐字节一致（scalar layout）─────────
struct DrawInstance
{
    vec4 rect;   // xy = 左上角像素，zw = 宽高像素
    vec4 color;  // 主色
    vec4 color2; // 次色
    vec4 params; // x = mode，y/z/w = 模式参数
    vec4 aux;    // 追加参数（方向 / 端点 / UV / 偏移）
    uvec4 pick;  // x=object_type y=entity z=(version<<16)|primitive w=hover_fn
    vec4 xf0;    // 作用域变换 2x2: (m00, m01, m10, m11)
    vec4 xf1;    // (tx, ty, i00, i01)
    vec4 xf2;    // (i10, i11, 0, 0)
}; // 144 B

struct CommandConstant
{
    uint type_id;        // 1=实例 2=描边 3=扇形
    uint address_offset; // 相对 heap 基址的字节偏移
    uint slot_count;     // 预留（通用网格槽位数）
}; // 12 B

layout(buffer_reference, scalar) readonly buffer InstanceBuffer
{
    DrawInstance instances[];
};
layout(buffer_reference, scalar) readonly buffer ConstantBuffer
{
    CommandConstant constants[];
};
layout(buffer_reference, scalar) readonly buffer PathSegmentBuffer
{
    vec4 segs[]; // 每段 5 个 vec4 = 80 B
};
layout(buffer_reference, scalar) readonly buffer Vec2Buffer
{
    vec2 pts[];
};

// ── 推送常量：只放“这次绘制去哪里找数据”─────────────────────────────────
layout(push_constant) uniform PC
{
    uint64_t instancesAddress;        // heap 的设备地址
    uint64_t commandConstantsAddress; // 本批 CommandConstant 的地址
    vec2 viewportSize;                // framebuffer 像素尺寸
    uint flags;                       // 预留
    uint pad;
} pc;

layout(location = 0) out vec2 vUV;
layout(location = 1) flat out uint vSegIndex;
layout(location = 2) flat out uint64_t vPayload; // 本命令 payload 的起始地址

const uint TYPE_INSTANCE = 1u;
const uint TYPE_STROKE = 2u;
const uint TYPE_FAN = 3u;
const uint INSTANCE_SIZE = 144u;

void main()
{
    const CommandConstant cc = ConstantBuffer(pc.commandConstantsAddress).constants[0];
    const uint64_t base = pc.instancesAddress + uint64_t(cc.address_offset);

    if (cc.type_id == TYPE_STROKE)
    {
        // 每段 6 顶点：0,1,2,2,1,3 → 两个三角形覆盖胶囊
        const uint seg = uint(gl_VertexIndex) / 6u;
        const uint corner = uint(gl_VertexIndex) % 6u;
        const uint idx = (corner < 3u) ? corner : (5u - corner);
        const uint64_t payload = base + uint64_t(INSTANCE_SIZE);
        const vec4 q01 = PathSegmentBuffer(payload).segs[seg * 5u + 0u];
        const vec4 q23 = PathSegmentBuffer(payload).segs[seg * 5u + 1u];
        const vec2 corners[4] = vec2[4](q01.xy, q01.zw, q23.xy, q23.zw);
        gl_Position = vec4(corners[idx], 0.0, 1.0);
        vUV = corners[idx] * 0.5 + 0.5;
        vSegIndex = seg;
        vPayload = base;
        return;
    }
    if (cc.type_id == TYPE_FAN)
    {
        const vec2 pos = Vec2Buffer(base + uint64_t(INSTANCE_SIZE)).pts[uint(gl_VertexIndex)];
        gl_Position = vec4(pos, 0.0, 1.0);
        vUV = pos * 0.5 + 0.5;
        vSegIndex = 0u;
        vPayload = base;
        return;
    }

    // type 1：每个实例自己带 rect，顶点在 NDC 里就是这块矩形
    const uint64_t addr = base + uint64_t(gl_InstanceIndex) * uint64_t(INSTANCE_SIZE);
    const DrawInstance inst = InstanceBuffer(addr).instances[0];
    const vec2 corners[6] = vec2[6](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
                                    vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0));
    // 作用域变换：local（图元自己的坐标）→ 父空间像素
    const vec2 local = inst.rect.xy + corners[uint(gl_VertexIndex)] * inst.rect.zw;
    const vec2 px = vec2(inst.xf0.x * local.x + inst.xf0.y * local.y + inst.xf1.x,
                         inst.xf0.z * local.x + inst.xf0.w * local.y + inst.xf1.y);
    const vec2 ndc = px / max(pc.viewportSize, vec2(1.0)) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = px / max(inst.rect.zw, vec2(1.0));
    vSegIndex = 0u;
    vPayload = addr;
}
