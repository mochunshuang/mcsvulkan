#version 450

// 推送常量：窗口尺寸
layout(push_constant)uniform PushConsts{
    vec2 windowSize;
}pc;

// 每个顶点的数据（共享几何体）
layout(location=0)in vec2 inPosition;// 顶点本地坐标

// 每个实例的数据（通过 binding 1, per-instance 输入）
layout(location=1)in vec2 inOffset;// 实例位置偏移
layout(location=2)in vec3 inColor;// 实例颜色

layout(location=0)out vec3 fragColor;

void main(){
    // 世界空间位置 = 本地坐标 + 实例偏移
    vec2 worldPos=inPosition+inOffset;
    
    // 映射到 NDC：保持宽高比，1 世界单位 = 1 像素
    vec2 scale=vec2(2.)/pc.windowSize;
    gl_Position=vec4(worldPos*scale,0.,1.);
    
    fragColor=inColor;
}