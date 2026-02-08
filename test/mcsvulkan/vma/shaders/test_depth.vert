#version 450
#extension GL_EXT_buffer_reference:require
#extension GL_EXT_scalar_block_layout:require
#extension GL_EXT_shader_explicit_arithmetic_types_int64:require

// 定义顶点数据结构（与C++端匹配）
struct VertexData{
    vec3 pos;//深度测试 启动 Z 坐标
    vec3 color;
};

// 缓冲引用：指向VertexData数组
layout(buffer_reference,scalar)readonly buffer VertexBuffer{
    VertexData vertices[];
};

// 推送常量：包含顶点缓冲地址
layout(push_constant)uniform PushConstants{
    uint64_t vertexBufferAddress;
}pc;

// Uniform Buffer定义
layout(set=0,binding=0)uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
}ubo;

layout(location=0)out vec3 fragColor;

void main(){
    VertexBuffer vertexBuffer=VertexBuffer(pc.vertexBufferAddress);
    uint vertexIndex=gl_VertexIndex;
    // 从缓冲读取顶点位置和颜色
    vec3 position=vertexBuffer.vertices[vertexIndex].pos;
    vec3 color=vertexBuffer.vertices[vertexIndex].color;
    
    // 使用MVP矩阵变换顶点位置
    mat4 mvp=ubo.proj*ubo.view*ubo.model;
    gl_Position=mvp*vec4(position,1.);
    fragColor=color;
}