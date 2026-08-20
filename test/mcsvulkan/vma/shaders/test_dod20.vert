#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_ARB_shader_draw_parameters : enable

#include "test_dod20.glsl"

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

// 命令常量：一条间接绘制命令对应一条（type_id + 实例数据区偏移 + 每实例顶点槽位数）
// 与 C++ shader_data::CommandConstant 布局完全一致（12B）
struct CommandConstant
{
    uint type_id;         // 实例类型（Glyph = 2）
    uint adddress_offset; // 实例数据区偏移（heap 基址偏移）
    uint slot_count;      // 每实例顶点槽位数（通用网格类型用，任意 N；固定类型忽略）
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
layout(location = 5) flat out uint
    instanceId; // 实例 id（frag 拾取用；gl_InstanceIndex 只在顶点阶段有效）

// 实例类型（与 C++ shader_data 约定一致；追加式）
const uint TYPE_GLYPH = 2u;      // 字形
const uint TYPE_RECT = 3u;       // 布局矩形线框
const uint TYPE_MESH = 4u;       // 普通网格绘制（无 SDF）：顶点 + 顶点属性
const uint TYPE_ROUND_RECT = 5u; // 圆角阴影矩形（填充 + 阴影）

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
    instanceId = gl_InstanceIndex; // 0..N-1，实例 id（顶点阶段内建量，flat 传给 frag）
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
    case TYPE_RECT: { // 布局矩形线框：自包含实例，localPos 来自几何
        instancePtr = heapBaseStart + gl_InstanceIndex * UI_RECT_SIZE;
        UiRect inst = RectBuffer(instancePtr).rects[0];
        model = mat4(vec4(inst.center_size.z, 0.0, 0.0, 0.0),
                     vec4(0.0, inst.center_size.w, 0.0, 0.0), vec4(0.0, 0.0, 1.0, 0.0),
                     vec4(inst.center_size.xy, 0.0, 1.0));
        fragColor = inst.color;
        localPos = v.pos.xy; // frag 掏空用
        pos = v.pos;
    }
    break;
    case TYPE_ROUND_RECT: { // 圆角阴影矩形（照抄 test_sdf case 0）
        instancePtr = heapBaseStart + gl_InstanceIndex * RECTANGLE_SIZE;
        Rectangle inst = RectangleBuffer(instancePtr).rects[0];
        model = inst.model * inst.vertexTransform.matrix; // 平移 + 尺寸缩放
        uint localVertexIndex = gl_VertexIndex - gl_BaseVertexARB;
        fragColor = inst.colors[localVertexIndex]; // 顶点颜色来自实例
        fragTexCoord = inst.uvTransform.offset + v.texCoord * inst.uvTransform.scale;
        // 阴影几何外扩：保证卡片+阴影落在顶点几何内
        bool hasShadow = (inst.effects & FX_SHADOW) != 0u || inst.shadowColor.a > 0.0;
        vec2 expand = vec2(1.0);
        if (hasShadow)
        {
            vec2 cardHalf = inst.size * 0.5;
            float edgeSoft = max(inst.radiusSoftness.y, 0.0005) * inst.size.x;
            float blur = max(inst.radiusSoftness.z, 0.0005) * inst.size.x;
            vec2 shadowHalf = cardHalf * max(inst.radiusSoftness.w, 0.0);
            vec2 offset = inst.shadowOffset * inst.size;
            vec2 margin = max(shadowHalf + blur + abs(offset) - cardHalf, vec2(0.0));
            margin += vec2(edgeSoft * 0.5);
            expand = (cardHalf + margin) / max(cardHalf, vec2(1e-6));
        }
        localPos = v.pos.xy * expand;
        pos.xy = v.pos.xy * expand;
    }
    break;
    case TYPE_MESH: { // 普通绘制（无 SDF）：实例数据 = VertexAttr 池，无实例结构体
        uint slot = gl_VertexIndex - gl_BaseVertexARB;
        uint attrIdx = gl_InstanceIndex * cc.slot_count + slot; // 实例 id × N + 槽位
        VertexAttr attr = AttrBuffer(pc.dataAddress + cc.adddress_offset).attrs[attrIdx];
        fragColor = attr.color; // 每顶点颜色（普通绘制，无 SDF）
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
