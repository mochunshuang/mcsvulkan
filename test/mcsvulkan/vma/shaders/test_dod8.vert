#version 450
// 启用所需扩展：缓冲引用、标量块布局、显式64位类型
#extension GL_EXT_buffer_reference:require
#extension GL_EXT_scalar_block_layout:require
#extension GL_EXT_shader_explicit_arithmetic_types_int64:require
#extension GL_ARB_shader_draw_parameters:enable//diff: 暴露 gl_BaseVertexARB

// 全局统一缓冲：视图矩阵和投影矩阵
layout(set=0,binding=0)uniform UniformBufferObject{
    mat4 view;
    mat4 proj;
}ubo;

// 顶点数据：与C++端的 Vertex 结构对应
struct VertexData{
    vec3 pos;
    vec2 texCoord;// 纹理坐标
};
struct VertexAttribute{
    vec3 color;// 仅颜色
};

struct InstanceData{
    uint textureIndex;// 纹理数组索引
    uint samplerIndex;// 采样器数组索引
    
    uint objectId;
    uint vertexAttributeOffset;
    mat4 matrix;
};

layout(buffer_reference,scalar)readonly buffer VertexBuffer{
    VertexData vertices[];
};
layout(buffer_reference,scalar)readonly buffer AttributePool{
    VertexAttribute attributes[];
};
layout(buffer_reference,scalar)readonly buffer InstanceBuffer{
    InstanceData instances[];
};

layout(push_constant)uniform PushConsts{
    uint64_t vertexAddress;
    uint64_t attributePoolAddress;
    uint64_t instanceAddress;
}pc;

// 输出到片段着色器
layout(location=0)out vec3 fragColor;
layout(location=1)out vec2 fragTexCoord;
layout(location=2)out flat uint fragTextureIndex;// 纹理索引（不需要插值）
layout(location=3)out flat uint fragSamplerIndex;// 采样器索引（不需要插值）

// diff:[test_indirectdraw_no_pick] 新增：传递物体 ID 给片段着色器
layout(location=4)out flat uint fragObjectId;

void main(){
    VertexBuffer vertBuf=VertexBuffer(pc.vertexAddress);
    AttributePool attrPool=AttributePool(pc.attributePoolAddress);
    InstanceBuffer instBuf=InstanceBuffer(pc.instanceAddress);
    
    VertexData v=vertBuf.vertices[gl_VertexIndex];// 全局位置索引，正确
    InstanceData inst=instBuf.instances[gl_InstanceIndex];
    
    // 计算本地顶点索引（0 ~ mesh.vertexCount-1）
    uint localVertexIndex=gl_VertexIndex-gl_BaseVertexARB;
    uint attrIdx=inst.vertexAttributeOffset+localVertexIndex;// 修正点
    VertexAttribute attr=attrPool.attributes[attrIdx];
    
    // 模型-视图-投影变换
    mat4 mvp=ubo.proj*ubo.view*inst.matrix;
    gl_Position=mvp*vec4(v.pos,1.);
    
    // 将颜色、纹理坐标、纹理/采样器索引传递给片段着色器
    fragColor=attr.color;
    fragTexCoord=v.texCoord;
    fragTextureIndex=inst.textureIndex;
    fragSamplerIndex=inst.samplerIndex;
    fragObjectId=inst.objectId;
}