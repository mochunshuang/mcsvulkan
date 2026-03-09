#version 450
#extension GL_EXT_buffer_reference:require
#extension GL_EXT_scalar_block_layout:require
#extension GL_EXT_shader_explicit_arithmetic_types_int64:require

// 定义顶点数据结构（与C++端匹配）
struct VertexData{
    vec3 pos;//深度测试 启动 Z 坐标
    vec3 color;
    vec2 texCoord;//diff: 纹理坐标
    
    //diff: test_msdf_atlas_gen start
    uint textureIndex;
    uint samplerIndex;
    uint fontType;
    float pxRange;
    //diff: test_msdf_atlas_gen end
};

// 缓冲引用：指向VertexData数组
layout(buffer_reference,scalar)readonly buffer VertexBuffer{
    VertexData vertices[];
};

// 推送常量：包含顶点缓冲地址
layout(push_constant)uniform PushConstants{
    uint64_t vertexBufferAddress;
    //diff: test_msdf_atlas_gen start
    // uint textureIndex;// diff: 添加纹理索引，传递给片段着色器
    // uint samplerIndex;// diff: 添加采样器索引
    //diff: test_msdf_atlas_gen end
}pc;

// Uniform Buffer定义
layout(set=0,binding=0)uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
}ubo;

layout(location=0)out vec3 fragColor;
layout(location=1)out vec2 fragTexCoord;// diff: 纹理坐标
layout(location=2)out flat uint fragTextureIndex;// diff: 添加纹理索引输出
layout(location=3)out flat uint fragSamplerIndex;// diff: 添加采样器索引输出
layout(location=4)out flat uint fragFontType;//diff: test_msdf_atlas_gen 备用绑定解析纹理的算法
layout(location=5)out flat float pxRange;//diff: msdf算法需要
// NOTE: flat意味着禁用插值，每个片段获得相同的值（来自引发顶点）

void main(){
    VertexBuffer vertexBuffer=VertexBuffer(pc.vertexBufferAddress);
    uint vertexIndex=gl_VertexIndex;
    // 从缓冲读取顶点位置和颜色
    VertexData v=vertexBuffer.vertices[vertexIndex];
    vec3 position=v.pos;
    vec3 color=v.color;
    
    // 使用MVP矩阵变换顶点位置
    mat4 mvp=ubo.proj*ubo.view*ubo.model;
    gl_Position=mvp*vec4(position,1.);
    
    //out
    fragColor=v.color;
    fragTexCoord=v.texCoord;
    fragTextureIndex=v.textureIndex;
    fragSamplerIndex=v.samplerIndex;
    fragFontType=v.fontType;
    pxRange=v.pxRange;
}