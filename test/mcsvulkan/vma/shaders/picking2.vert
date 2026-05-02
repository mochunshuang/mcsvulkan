#version 450

#extension GL_EXT_scalar_block_layout:require
#extension GL_EXT_buffer_reference:require
#extension GL_EXT_scalar_block_layout:require
#extension GL_EXT_shader_explicit_arithmetic_types_int64:require

// 顶点数据结构（与主渲染中的 Vertex 对齐）
struct VertexData{
    vec3 pos;// 我们只需要位置，其余字段作为占位
    vec3 color;
    vec2 texCoord;
};

layout(buffer_reference,scalar)readonly buffer VertexBuffer{
    VertexData vertices[];
};

// NOTE: 注意 scalar 修改 映射
struct PickObjectInfo{
    uint64_t vertexBufferAddress;// 顶点缓冲地址
    mat4 model;
    uint objectId;
};

layout(set=0,binding=0)uniform UniformBufferObject{
    mat4 view;
    mat4 proj;
}ubo;

layout(set=0,binding=1,scalar)readonly buffer PickObjectData{
    PickObjectInfo objects[];
}pickData;

layout(location=0)out flat uint fragObjectId;

void main(){
    PickObjectInfo obj=pickData.objects[gl_InstanceIndex];
    VertexBuffer vb=VertexBuffer(obj.vertexBufferAddress);
    
    // 直接从缓冲中取顶点的 pos（结构体开头 3 个 float）
    vec3 position=vb.vertices[gl_VertexIndex].pos;
    
    gl_Position=ubo.proj*ubo.view*obj.model*vec4(position,1.);
    fragObjectId=obj.objectId;
}