#version 450

layout(location=0)out vec3 fragColor;

/*
-1 (左)          0 (中心)          1 (右)
-1 ┌─────────────────┬─────────────────┐  ← 屏幕顶部 (y = -1)
│                 │                 │
│    第二象限      │    第一象限      │
0  │        * (0,0)  │                 │
│                 │                 │
│    第三象限      │    第四象限      │
1  └─────────────────┴─────────────────┘  ← 屏幕底部 (y = 1)



公式：
// https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#vertexpostproc-viewport

xf=(px/2)xd+ox
yf=(py/2)yd+oy
zf=pz×zd+oz

NDC坐标yd       帧缓冲坐标yf                屏幕位置
-1              (py/2)*(-1)+py/2= 0          顶部
0               (py/2)*0+py/2= py/2          中心
+1              (py/2)*1+py/2= py            底部

结论：
x从左到右：[-1,y]->[1,y]
y从上到下:[x,-1]->[x,1]


⚖️对象空间vs NDC空间
这是你问的"对比"。假设一个边长为2、中心在原点的矩形：

对比维度            对象空间(Object Space)              NDC(Normalized Device Coordinates)
坐标系原点          模型中心                            屏幕中心
Y轴方向             向上为正(业界共识)                  向下为正(Vulkan规范)
坐标范围            任意值                              X,Y∈[-1.,1.],Z∈[0.,1.]
举例：左上角        (-1,1)                              (-.5,-.5)
举例：右上角        (1,1)                               (.5,-.5)
举例：右下角        (1,-1)                              (.5,.5)
举例：左下角        (-1,-1)                             (-.5,.5)

NOTE: 当前的vulkan配置
.cullMode=VK_CULL_MODE_BACK_BIT,
.frontFace=VK_FRONT_FACE_CLOCKWISE,
它定义了顶点顺序与正面朝向的对应关系。
VK_FRONT_FACE_CLOCKWISE：当你按0→1→2的顺序看这个三角形时，如果顶点是顺时针排列的，那这一面就是正面。
VK_FRONT_FACE_COUNTER_CLOCKWISE：反之，逆时针排列的那一面才是正面。


// https://vulkan-tutorial.com/Uniform_buffers/Descriptor_set_layout_and_buffer
// ubo.proj[1][1]*=-1; //NOTE: BUG  的来源。 正交投影才需要
*/
// 正三角形. 顶点正上中心
// vec2 positions[3]=vec2[](
    //     vec2(0.,-.5),
    //     vec2(.5,.5),
    //     vec2(-.5,.5)
// );


// 左下角三角形
vec2 positions[3]=vec2[](
    vec2(-.5,-.5),
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
