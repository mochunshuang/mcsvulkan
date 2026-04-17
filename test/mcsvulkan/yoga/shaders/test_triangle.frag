#version 450
// 输入：来自顶点着色器的颜色（location 0）
layout(location=0)in vec3 fragColor;

// 输出：到颜色附件0（替换了gl_FragColor）
layout(location=0)out vec4 outColor;

void main(){
    outColor=vec4(fragColor,1.);// 加上Alpha通道
}