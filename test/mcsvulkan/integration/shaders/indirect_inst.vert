#version 450
#extension GL_EXT_buffer_reference:require
#extension GL_EXT_scalar_block_layout:require
#extension GL_EXT_shader_explicit_arithmetic_types_int64:require

layout(location=0)in vec2 inPos;// 来自绑定的全局顶点缓冲区

struct Instance{
    float offX,offY;
    float r,g,b;
};
layout(buffer_reference,scalar)readonly buffer InstanceBuffer{
    Instance instances[];
};

layout(push_constant)uniform PushConsts{
    uint64_t instanceAddress;
}pc;

layout(location=0)out vec3 fragColor;

void main(){
    InstanceBuffer ibuf=InstanceBuffer(pc.instanceAddress);
    Instance i=ibuf.instances[gl_InstanceIndex];
    gl_Position=vec4(inPos+vec2(i.offX,i.offY),0.,1.);
    fragColor=vec3(i.r,i.g,i.b);
}