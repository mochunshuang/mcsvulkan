#version 450
// diff: 添加非均匀索引扩展
#extension GL_EXT_nonuniform_qualifier:require

// 输入：来自顶点着色器的颜色（location 0）
layout(location=0)in vec3 fragColor;
layout(location=1)in vec2 fragTexCoord;// diff: 添加纹理坐标输入
layout(location=2)in flat uint fragTextureIndex;// diff: 添加纹理输入
layout(location=3)in flat uint fragSamplerIndex;// diff: 添加采样器输入

// 输出：到颜色附件0（替换了gl_FragColor）
layout(location=0)out vec4 outColor;

// diff:  分离纹理和采样器数组. 从 1 开始。因为vert占用了0. 和布局有关
layout(set=0,binding=1)uniform texture2D textures[];
layout(set=0,binding=2)uniform sampler samplers[];

//diff: [test_texture2.frag] start
// 硬编码：根据生成参数计算得到 pxRange = 2 * 0.125 * 32 = 8.0
const float PX_RANGE=8.;

float median(float r,float g,float b){
    return max(min(r,g),min(max(r,g),b));
}

float screenPxRange(){
    ivec2 texSize=textureSize(sampler2D(textures[nonuniformEXT(fragTextureIndex)],samplers[nonuniformEXT(fragSamplerIndex)]),0);
    vec2 unitRange=vec2(PX_RANGE)/vec2(texSize);// 每个纹素对应的距离场范围（像素）
    vec2 screenTexSize=1./fwidth(fragTexCoord);// 屏幕空间纹素缩放因子
    return max(.5*dot(unitRange,screenTexSize),1.);// 屏幕像素范围，最小为1
}
//diff: [test_texture2.frag] end

void main(){
    
    // 采样 MSDF 纹理（RGB 通道，必须为线性空间）
    vec3 msd=texture(sampler2D(textures[nonuniformEXT(fragTextureIndex)],samplers[nonuniformEXT(fragSamplerIndex)]),fragTexCoord).rgb;
    
    float sd=median(msd.r,msd.g,msd.b);// 中位数得到带符号距离
    float screenPxDistance=screenPxRange()*(sd-.5);// 转换到屏幕空间
    float opacity=clamp(screenPxDistance+.5,0.,1.);// 抗锯齿透明度
    
    // 输出：使用顶点颜色作为文字颜色，透明度调制（假设 fragColor 为 RGB，无 Alpha）
    outColor=vec4(fragColor*opacity,opacity);
}