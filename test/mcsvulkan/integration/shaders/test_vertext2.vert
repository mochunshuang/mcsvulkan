#version 450
#extension GL_EXT_buffer_reference:require
#extension GL_EXT_scalar_block_layout:require
#extension GL_EXT_shader_explicit_arithmetic_types_int64:require

layout(location=0)in vec2 inPosition;

layout(location=0)out vec3 fragColor;

struct InstanceData{
    vec2 offset;
    vec3 color;
};
layout(buffer_reference,scalar)readonly buffer InstanceBuffer{
    InstanceData instances[];
};
layout(push_constant)uniform PushConsts{
    uint64_t instanceAddress;
}pc;

void main(){
    InstanceBuffer instanceSSBO=InstanceBuffer(pc.instanceAddress);
    InstanceData data=instanceSSBO.instances[gl_InstanceIndex];
    vec2 target_pos=inPosition+data.offset;
    
    gl_Position=vec4(target_pos,0.,1.);
    fragColor=data.color;
}