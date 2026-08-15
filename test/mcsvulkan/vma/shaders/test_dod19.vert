#version 450
// 启用所需扩展：缓冲引用、标量块布局、显式64位类型
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_ARB_shader_draw_parameters : enable //diff: 暴露 gl_BaseVertexARB / gl_DrawIDARB

// 共享结构体（Glyph 与 frag 一致，移植自 test_sdf_shared.glsl 的 Glyph 部分）
#include "test_dod19.glsl"

// 顶点数据：与C++端的 Vertex 结构对应（[-0.5,0.5] 单位 quad）
struct VertexData
{
    vec3 pos;
    vec2 texCoord; // 纹理坐标
};
layout(buffer_reference, scalar) readonly buffer VertexBuffer
{
    VertexData vertices[];
};

// 全局统一缓冲：视图矩阵和投影矩阵
struct CameraInfo
{
    mat4 view;
    mat4 proj;
};
layout(set = 0, binding = 0) uniform UniformBufferObject
{
    CameraInfo cameraInfo[2];
}
ubo;

// 命令常量：一条间接绘制命令对应一条（type_id + 实例数据区偏移）
// 与 test_sdf.vert 的 CommandConstant 布局完全一致（16B）
struct CommandConstant
{
    uint type_id;         // 实例类型（Glyph = 2）
    uint adddress_offset; // 实例数据区偏移（heap 基址偏移）
};
layout(buffer_reference, scalar) readonly buffer CommandConstBuffer
{
    CommandConstant consts[];
};

layout(push_constant) uniform PushConsts
{
    uint64_t vertexAddress;
    uint64_t dataAddress; // 实例堆基址（heap）
    uint64_t commandConstantsAddress;
    uint cameraIndex;
}
pc;

layout(location = 0) out vec4 fragColor; // 插值后的颜色
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint type_id;
layout(location = 3) flat out uint64_t instancePtr;

layout(location = 4) out vec2 localPos; // 归一化局部坐标 [-0.5,0.5]

// 实例类型（与 C++ shader_data 约定一致；追加式）
const uint TYPE_GLYPH = 2u; // 字形

void main()
{
    fragColor = vec4(1., 1., 1., 1.); //NOTE: 默认白色
    fragTexCoord = vec2(0.0);
    mat4 model = mat4(1.); //NOTE: 默认无副作用
    vec3 pos = vec3(0.0);
    localPos = vec2(0.0);

    // 多实例间接绘制（每条 draw 命令由 gl_DrawIDARB 对应一条命令常量）
    CommandConstBuffer cmdConsts = CommandConstBuffer(pc.commandConstantsAddress);
    CommandConstant cc = cmdConsts.consts[gl_DrawIDARB];
    type_id = cc.type_id;
    uint64_t heapBaseStart = pc.dataAddress + cc.adddress_offset;
    CameraInfo cam = ubo.cameraInfo[pc.cameraIndex];

    VertexBuffer vertBuf = VertexBuffer(pc.vertexAddress);
    VertexData v = vertBuf.vertices[gl_VertexIndex]; // 公共顶点池，按 mesh 偏移取用

    switch (type_id)
    {
    case TYPE_GLYPH: { // Glyph：与 test_sdf.vert 的 case 2 完全一致
        instancePtr = heapBaseStart + gl_InstanceIndex * GLYPH_SIZE;
        Glyph inst = GlyphBuffer(instancePtr).glyphs[0]; // 直接读取实例
        model = inst.model;                              // 平移 + 缩放（字形矩形）
        fragColor = inst.color;                          // 顶点色，供 modulate 使用
        fragTexCoord = inst.uvTransform.offset + v.texCoord * inst.uvTransform.scale;
        localPos = v.pos.xy;
        pos = v.pos;
    }
    break;
    default:
        break;
    }

    mat4 mvp = cam.proj * cam.view * model;
    gl_Position = mvp * vec4(pos, 1.0);
}
