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
void main(){
    gl_Position=vec4(positions[gl_VertexIndex],0.,1.);
    fragColor=colors[gl_VertexIndex];
}
