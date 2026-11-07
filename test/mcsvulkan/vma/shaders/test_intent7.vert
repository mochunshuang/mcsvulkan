#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_ARB_shader_draw_parameters : enable

#include "test_intent7_abi.glsl"

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

struct VertexData
{
    vec3 pos;
    vec2 texCoord;
};

layout(buffer_reference, scalar) readonly buffer VertexBuffer
{
    VertexData vertices[];
};

layout(buffer_reference, scalar) readonly buffer CommandConstBuffer
{
    CommandConstant consts[];
};

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint program;
layout(location = 3) flat out uint64_t instancePtr;
layout(location = 4) out vec2 localPos;
layout(location = 5) flat out uint instanceId;

void main()
{
    fragColor = vec4(1.0);
    fragTexCoord = vec2(0.0);
    mat4 model = mat4(1.0);
    vec3 pos = vec3(0.0);
    localPos = vec2(0.0);

    CommandConstBuffer cmdConsts = CommandConstBuffer(pc.commandConstantsAddress);
    CommandConstant cc = cmdConsts.consts[gl_DrawIDARB];
    program = cc.program;
    instanceId = gl_InstanceIndex;
    uint64_t heapBaseStart = pc.dataAddress + cc.address_offset;
    CameraInfo cam = ubo.cameraInfo[pc.cameraIndex];

    VertexBuffer vertBuf = VertexBuffer(pc.vertexAddress);
    VertexData v = vertBuf.vertices[gl_VertexIndex];

    switch (program)
    {
    case PROGRAM_SHAPE: {
        instancePtr = heapBaseStart + gl_InstanceIndex * SHAPE_SIZE;
        Shape inst = ShapeBuffer(instancePtr).shapes[0];
        model = inst.model;
        uint localVertexIndex = gl_VertexIndex - gl_BaseVertexARB;
        fragColor = inst.colors[localVertexIndex];

        bool hasShadow = (inst.effects & FX_SHADOW) != 0u;
        vec2 expand = vec2(1.0);
        if (hasShadow)
        {
            vec2 cardHalf = max(inst.px_size * 0.5, vec2(1.0));
            float blur = max(inst.blur_px, 0.5);
            vec2 shadowHalf = cardHalf * max(inst.spread, 0.0);
            vec2 offset = inst.shadow_offset_px;
            vec2 margin = max(shadowHalf + blur + abs(offset) - cardHalf, vec2(0.0));
            margin += vec2(0.5); // AA
            expand = (cardHalf + margin) / cardHalf;
        }
        localPos = v.pos.xy * expand;
        pos.xy = v.pos.xy * expand;
    }
    break;
    case PROGRAM_PATH_FILL: {
        instancePtr = heapBaseStart + gl_InstanceIndex * PATH_FILL_SIZE;
        PathFill inst = PathFillBuffer(instancePtr).fills[0];
        model = inst.model;
        fragColor = inst.color;
        localPos = v.pos.xy;
        pos = v.pos;
    }
    break;
    case PROGRAM_PATH_STROKE: {
        instancePtr = heapBaseStart + gl_InstanceIndex * PATH_STROKE_SIZE;
        PathStroke inst = PathStrokeBuffer(instancePtr).strokes[0];
        model = inst.model;
        fragColor = inst.color;
        localPos = v.pos.xy;
        pos = v.pos;
    }
    break;
    case PROGRAM_TEXTURE: {
        instancePtr = heapBaseStart + gl_InstanceIndex * TEX_QUAD_SIZE;
        TexQuad inst = TexQuadBuffer(instancePtr).quads[0];
        model = inst.model;
        fragColor = inst.color;
        fragTexCoord = inst.uvTransform.offset + v.texCoord * inst.uvTransform.scale;
        localPos = v.pos.xy;
        pos = v.pos;
    }
    break;
    case PROGRAM_BLUR: {
        instancePtr = heapBaseStart + gl_InstanceIndex * BLUR_QUAD_SIZE;
        BlurQuad inst = BlurQuadBuffer(instancePtr).quads[0];
        model = inst.model;
        fragColor = inst.color;
        fragTexCoord = inst.uvTransform.offset + v.texCoord * inst.uvTransform.scale;
        localPos = v.pos.xy;
        pos = v.pos;
    }
    break;
    case PROGRAM_COLOR_MATRIX: {
        instancePtr = heapBaseStart + gl_InstanceIndex * COLOR_MATRIX_SIZE;
        ColorMatrix inst = ColorMatrixBuffer(instancePtr).quads[0];
        model = inst.model;
        fragColor = inst.color;
        fragTexCoord = inst.uvTransform.offset + v.texCoord * inst.uvTransform.scale;
        localPos = v.pos.xy;
        pos = v.pos;
    }
    break;
    case PROGRAM_NOISE: {
        instancePtr = heapBaseStart + gl_InstanceIndex * NOISE_QUAD_SIZE;
        NoiseQuad inst = NoiseQuadBuffer(instancePtr).quads[0];
        model = inst.model;
        fragColor = inst.color_a;
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
