#version 450
#extension GL_EXT_scalar_block_layout:require

layout(location=0)in vec2 inPos;// binding=0 顶点位置 (NDC)

// 实例数据来自 SSBO (set=0, binding=0)
struct InstanceData{
    vec2 offset;
    vec3 color;
};
layout(scalar,set=0,binding=0)readonly buffer InstanceBuffer{
    InstanceData instances[];
}instanceSSBO;

layout(location=0)out vec3 fragColor;

void main(){
    // 通过 gl_InstanceIndex 读取当前实例的数据
    InstanceData data=instanceSSBO.instances[gl_InstanceIndex];
    //NOTE: 基于 data 表现
    gl_Position=vec4(inPos+data.offset,0.,1.);
    fragColor=data.color;
}