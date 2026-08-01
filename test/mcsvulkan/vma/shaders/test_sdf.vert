#version 450
// 启用所需扩展：缓冲引用、标量块布局、显式64位类型
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_ARB_shader_draw_parameters : enable //diff: 暴露 gl_BaseVertexARB

// NOTE: 全局一份的顶点
struct VertexData
{
    vec3 pos;
    vec2 texCoord; // 纹理坐标
};
layout(buffer_reference, scalar) readonly buffer VertexBuffer
{
    VertexData vertices[];
};
// NOTE: 实例化相关的 顶点 和 UV 通过计算变化 才能得到真正的实例化的 顶点和UV
const int VERTEX_TRANSFORM_SIZE = 64;
struct VertexTransform
{
    mat4 matrix;
};
const int UV_TRANSFORM_SIZE = int(2 * 8 + 2 * 8);
struct UvTransform
{
    vec2 scale;
    vec2 offset;
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

layout(buffer_reference, scalar) readonly buffer InstanceBuffer
{
    uint64_t instances[];
};

struct CommandConstant
{
    uint type_id;
    uint adddress_offset;
};
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
}
pc;

// type_id == 0
#define RECTANGLE_SIZE 160
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


layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint type_id;
layout(location = 3) flat out uint64_t instancePtr;

layout(location = 4) out vec2 localPos;
layout(location = 5) out vec2 rectSize;

const uint INVALID_INDEX = 0xFFFFFFFFu;

void main()
{
    fragColor = vec4(1., 1., 1., 1.); //NOTE: 默认白色
    mat4 model = mat4(1.);            //NOTE:默认无副作用

    gl_PointSize = 1.;

    // NOTE: 多实例间接绘制. 从
    CommandConstBuffer cmdConsts = CommandConstBuffer(pc.commandConstantsAddress);
    CommandConstant cc = cmdConsts.consts[gl_DrawIDARB];
    type_id = cc.type_id;
    uint adddress_offset = cc.adddress_offset;
    uint64_t heapBaseStart = pc.dataAddress + adddress_offset;

    VertexBuffer vertBuf = VertexBuffer(pc.vertexAddress);
    VertexData v = vertBuf.vertices[gl_VertexIndex]; // 全局位置索引，正确
    // 计算本地顶点索引（0 ~ mesh.vertexCount-1）
    uint localVertexIndex =
        gl_VertexIndex - gl_BaseVertexARB; //NOTE: 如果每个顶点的颜色都设置,这个非常重要
    CameraInfo cam = ubo.cameraInfo[pc.cameraIndex];

    switch (type_id)
    {
    case 0: {
        instancePtr = heapBaseStart + gl_InstanceIndex * RECTANGLE_SIZE;
        Rectangle inst = RectangleBuffer(instancePtr).rects[0]; // 直接读取第一个
        model = inst.model * inst.vertexTransform.matrix;
        fragColor = inst.color;
        fragTexCoord = inst.uvTransform.offset + v.texCoord * inst.uvTransform.scale;

        mat4 vt = inst.vertexTransform.matrix;
        rectSize = vec2(vt[0][0], vt[1][1]);
        localPos = v.pos.xy;   // v.pos.xy 是 [-0.5, 0.5]
    }
    default:
        break;
    }
    // NOTE: 阴影无法生成，圆角能生成是因为，确实是属于画布内。什么都不操作就是画布了。先顶点着色器生成颜色，你才能丢弃或改变颜色
    mat4 mvp = cam.proj * cam.view * model;
    gl_Position = mvp * vec4(v.pos, 1.);
}