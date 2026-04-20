#version 450
#extension GL_EXT_buffer_reference:require
#extension GL_EXT_scalar_block_layout:require
#extension GL_EXT_shader_explicit_arithmetic_types_int64:require

// 定义顶点数据结构（与C++端匹配）
struct VertexData{
    vec2 pos;
    vec3 color;
};

// 缓冲引用：指向VertexData数组
layout(buffer_reference,scalar)readonly buffer VertexBuffer{
    VertexData vertices[];
};

// 推送常量：包含顶点缓冲地址
layout(push_constant)uniform PushConstants{
    uint64_t vertexBufferAddress;
    float windowWidth;
    float windowHeight;
}pc;
layout(location=0)out vec3 fragColor;

void main(){
    VertexBuffer vertexBuffer=VertexBuffer(pc.vertexBufferAddress);
    uint vertexIndex=gl_VertexIndex;
    
    vec2 pixelPos=vertexBuffer.vertices[vertexIndex].pos;
    vec3 color=vertexBuffer.vertices[vertexIndex].color;
    
    // 像素坐标 → NDC
    // NDC: x∈[-1,1], y∈[-1,1] (y向上)
    float ndcX=(pixelPos.x/pc.windowWidth)*2.-1.;
    float ndcY=(pixelPos.y/pc.windowHeight)*2.-1.;
    
    gl_Position=vec4(ndcX,ndcY,0.,1.);
    fragColor=color;
}