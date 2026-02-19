#version 450

// 推送常量（与顶点着色器一致）
layout(push_constant)uniform PushConstants{
    mat4 model;
    uint objectId;
}push;

// 输出：R通道 = 物体ID，G通道 = 图元ID（由GPU自动生成）
layout(location=0)out uvec2 outID;

void main(){
    outID=uvec2(push.objectId,gl_PrimitiveID);
}