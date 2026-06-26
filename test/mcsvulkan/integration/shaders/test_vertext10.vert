#version 450
#extension GL_EXT_buffer_reference:require
#extension GL_EXT_scalar_block_layout:require
#extension GL_EXT_shader_explicit_arithmetic_types_int64:require
#extension GL_ARB_shader_draw_parameters:enable//diff: [test_vertext10], gl_BaseVertexARB

struct Vertex{
    vec2 pos;
};

layout(buffer_reference,scalar)readonly buffer VertexBuffer{
    Vertex vertices[];
};

struct VertexAttribute{
    vec3 color;// 仅颜色
};

layout(buffer_reference,scalar)readonly buffer AttributePool{
    VertexAttribute attributes[];
};

struct InstanceData{
    vec2 offset;
    uint vertexAttributeOffset;
};

layout(buffer_reference,scalar)readonly buffer InstanceBuffer{
    InstanceData instances[];
};

layout(push_constant)uniform PushConsts{
    uint64_t vertexAddress;
    uint64_t attributePoolAddress;
    uint64_t instanceAddress;
}pc;

layout(location=0)out vec3 fragColor;

void main(){
    VertexBuffer vertBuf=VertexBuffer(pc.vertexAddress);
    AttributePool attrPool=AttributePool(pc.attributePoolAddress);
    InstanceBuffer instBuf=InstanceBuffer(pc.instanceAddress);
    
    Vertex v=vertBuf.vertices[gl_VertexIndex];// 全局位置索引，正确
    InstanceData inst=instBuf.instances[gl_InstanceIndex];
    
    // 计算本地顶点索引（0 ~ mesh.vertexCount-1）
    uint localVertexIndex=gl_VertexIndex-gl_BaseVertexARB;
    uint attrIdx=inst.vertexAttributeOffset+localVertexIndex;// 修正点
    VertexAttribute attr=attrPool.attributes[attrIdx];
    
    vec2 targetPos=v.pos+inst.offset;
    gl_Position=vec4(targetPos,0.,1.);
    fragColor=attr.color;
}