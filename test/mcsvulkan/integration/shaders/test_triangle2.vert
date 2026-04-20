#version 450

layout(location=0)out vec3 fragColor;

vec2 positions[3]=vec2[](
    vec2(0.,-.5),
    vec2(.5,.5),
    vec2(-.5,.5)
);

vec3 colors[3]=vec3[](
    vec3(1.,0.,0.),
    vec3(0.,1.,0.),
    vec3(0.,0.,1.)
);

/*
// 在Vulkan C++代码中
pipelineInfo.inputAssemblyState.primitiveRestartEnable = VK_FALSE;
pipelineInfo.inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

gl_VertexIndex 是顶点的索引号.在绘制三角形时，它会依次为：0, 1, 2, 0, 1, 2...
*/

// 基准窗口尺寸（您希望三角形在这个尺寸下显示正常）
const vec2 BASE_SIZE=vec2(800.,600.);
// 推送常量：每帧传入当前窗口尺寸
layout(push_constant)uniform PushConsts{
    vec2 windowSize;// (width, height)
}pc;

void main(){
    // 计算补偿缩放因子：基准尺寸 / 当前尺寸
    vec2 scale=BASE_SIZE/pc.windowSize;
    // 对固定 NDC 坐标进行缩放，Z/W 保持默认
    gl_Position=vec4(positions[gl_VertexIndex]*scale,0.,1.);
    fragColor=colors[gl_VertexIndex];
}
