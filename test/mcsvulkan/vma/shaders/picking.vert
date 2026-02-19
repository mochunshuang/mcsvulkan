#version 450

// 与主渲染共用的 Uniform Buffer（set=0, binding=0）
layout(set=0,binding=0)uniform UniformBufferObject{
    mat4 view;
    mat4 proj;
}ubo;

// 推送常量：每个物体的模型矩阵和物体ID
layout(push_constant)uniform PushConstants{
    mat4 model;// 模型矩阵，64字节
    uint objectId;// 物体ID，4字节（总大小68字节，对齐到4）
}push;

// 顶点输入：位置属性（location = 0）
layout(location=0)in vec3 inPosition;

void main(){
    // 计算裁剪空间位置：proj * view * model * position
    gl_Position=ubo.proj*ubo.view*push.model*vec4(inPosition,1.);
}