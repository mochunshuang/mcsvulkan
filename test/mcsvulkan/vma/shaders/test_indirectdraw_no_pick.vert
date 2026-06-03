#version 450
// 启用所需扩展：缓冲引用、标量块布局、显式64位类型
#extension GL_EXT_buffer_reference:require
#extension GL_EXT_scalar_block_layout:require
#extension GL_EXT_shader_explicit_arithmetic_types_int64:require

// 顶点数据：与C++端的 Vertex 结构对应
struct VertexData{
    vec3 pos;
    vec3 color;
    vec2 texCoord;// 纹理坐标
};

// 缓冲引用：指向顶点缓冲
layout(buffer_reference,scalar)readonly buffer VertexBuffer{
    VertexData vertices[];
};

// 缓冲引用：指向模型矩阵
layout(buffer_reference,scalar)readonly buffer MatrixReference{
    mat4 matrix;
};

// 每个物体的绘制参数，与C++端的 ObjectInfo 对齐
struct ObjectInfo{
    uint64_t vertexBufferAddress;// 顶点缓冲的设备地址
    uint64_t objectDataAddress;// 模型矩阵缓冲的设备地址
    uint textureIndex;// 纹理数组索引
    uint samplerIndex;// 采样器数组索引
    
    uint objectId;// diff:[test_indirectdraw_no_pick] 新增pick的id
};

// 存放所有物体信息的SSBO，binding=3
layout(set=0,binding=3)readonly buffer ObjectData{
    ObjectInfo objects[];
}objectData;

// 全局统一缓冲：视图矩阵和投影矩阵
layout(set=0,binding=0)uniform UniformBufferObject{
    mat4 view;
    mat4 proj;
}ubo;

// 输出到片段着色器
layout(location=0)out vec3 fragColor;
layout(location=1)out vec2 fragTexCoord;
layout(location=2)out flat uint fragTextureIndex;// 纹理索引（不需要插值）
layout(location=3)out flat uint fragSamplerIndex;// 采样器索引（不需要插值）

// diff:[test_indirectdraw_no_pick] 新增：传递物体 ID 给片段着色器
layout(location=4)out flat uint fragObjectId;

void main(){
    // 使用 gl_InstanceIndex 标识当前物体（间接绘制命令中的 firstInstance = 物体编号）
    ObjectInfo obj=objectData.objects[gl_InstanceIndex];
    
    // 通过设备地址访问顶点数据
    VertexBuffer vertexBuffer=VertexBuffer(obj.vertexBufferAddress);
    MatrixReference objectDataRef=MatrixReference(obj.objectDataAddress);
    
    uint vertexIndex=gl_VertexIndex;
    
    vec3 position=vertexBuffer.vertices[vertexIndex].pos;
    vec3 color=vertexBuffer.vertices[vertexIndex].color;
    vec2 texCoord=vertexBuffer.vertices[vertexIndex].texCoord;
    
    // 模型-视图-投影变换
    mat4 mvp=ubo.proj*ubo.view*objectDataRef.matrix;
    gl_Position=mvp*vec4(position,1.);
    
    // 将颜色、纹理坐标、纹理/采样器索引传递给片段着色器
    fragColor=color;
    fragTexCoord=texCoord;
    fragTextureIndex=obj.textureIndex;
    fragSamplerIndex=obj.samplerIndex;
    
    fragObjectId=obj.objectId;// diff:[test_indirectdraw_no_pick] 从 SSBO 直接获取
}