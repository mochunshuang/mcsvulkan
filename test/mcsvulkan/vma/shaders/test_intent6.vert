#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_ARB_shader_draw_parameters : enable

#include "test_intent6_abi.glsl"

struct VertexData
{
    vec3 pos;
    vec2 texCoord;
};
struct CameraInfo
{
    mat4 view;
    mat4 proj;
};
struct CommandConstant
{
    uint type_id;
    uint adddress_offset;
    uint slot_count;
};

layout(buffer_reference, scalar) readonly buffer VertexBuffer
{
    VertexData vertices[];
};

layout(set = 0, binding = 0) uniform UniformBufferObject
{
    CameraInfo cameraInfo[2];
}
ubo;
layout(buffer_reference, scalar) readonly buffer CommandConstBuffer
{
    CommandConstant consts[];
};

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

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint type_id;
layout(location = 3) flat out uint64_t instancePtr;
layout(location = 4) out vec2 localPos;
layout(location = 5) flat out uint instanceId;

const uint TYPE_GLYPH = 2u;
const uint TYPE_RECT = 3u;
const uint TYPE_MESH = 4u;
const uint TYPE_ROUND_RECT = 5u;

void main()
{
    fragColor = vec4(1., 1., 1., 1.);
    fragTexCoord = vec2(0.0);
    mat4 model = mat4(1.);
    vec3 pos = vec3(0.0);
    localPos = vec2(0.0);

    CommandConstBuffer cmdConsts = CommandConstBuffer(pc.commandConstantsAddress);
    CommandConstant cc = cmdConsts.consts[gl_DrawIDARB];
    type_id = cc.type_id;
    instanceId = gl_InstanceIndex;
    uint64_t heapBaseStart = pc.dataAddress + cc.adddress_offset;
    CameraInfo cam = ubo.cameraInfo[pc.cameraIndex];

    VertexBuffer vertBuf = VertexBuffer(pc.vertexAddress);
    VertexData v = vertBuf.vertices[gl_VertexIndex];

    switch (type_id)
    {
    case TYPE_GLYPH: {
        instancePtr = heapBaseStart + gl_InstanceIndex * GLYPH_SIZE;
        Glyph inst = GlyphBuffer(instancePtr).glyphs[0];
        model = inst.model;
        fragColor = inst.color;
        fragTexCoord = inst.uvTransform.offset + v.texCoord * inst.uvTransform.scale;
        localPos = v.pos.xy;
        pos = v.pos;
    }
    break;
    case TYPE_RECT: {
        instancePtr = heapBaseStart + gl_InstanceIndex * UI_RECT_SIZE;
        UiRect inst = RectBuffer(instancePtr).rects[0];
        model = mat4(vec4(inst.center_size.z, 0.0, 0.0, 0.0),
                     vec4(0.0, inst.center_size.w, 0.0, 0.0), vec4(0.0, 0.0, 1.0, 0.0),
                     vec4(inst.center_size.xy, 0.0, 1.0));
        fragColor = inst.color;
        localPos = v.pos.xy;
        pos = v.pos;
    }
    break;
    case TYPE_ROUND_RECT: {
        instancePtr = heapBaseStart + gl_InstanceIndex * RECTANGLE_SIZE;
        Rectangle inst = RectangleBuffer(instancePtr).rects[0];
        model = inst.model * inst.vertexTransform.matrix;
        uint localVertexIndex = gl_VertexIndex - gl_BaseVertexARB;
        fragColor = inst.colors[localVertexIndex];
        fragTexCoord = inst.uvTransform.offset + v.texCoord * inst.uvTransform.scale;
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
    case TYPE_MESH: {
        uint slot = gl_VertexIndex - gl_BaseVertexARB;
        uint attrIdx = gl_InstanceIndex * cc.slot_count + slot;
        VertexAttr attr = AttrBuffer(pc.dataAddress + cc.adddress_offset).attrs[attrIdx];
        fragColor = attr.color;
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
