#version 450

layout(location=0)in vec2 fragUV;
layout(location=0)out vec4 outColor;

layout(binding=0)uniform sampler2D fontSampler;

// MSDF 中值函数
float median(float r,float g,float b){
    return max(min(r,g),min(max(r,g),b));
}

void main(){
    vec3 msdf=texture(fontSampler,fragUV).rgb;
    float sigDist=median(msdf.r,msdf.g,msdf.b)-.5;
    float opacity=clamp(sigDist+.5,0.,1.);
    // 为了调试，先显示纹理颜色
    // outColor = vec4(msdf, 1.0);
    // 正确的 MSDF 渲染
    outColor=vec4(1.,1.,1.,opacity);
}