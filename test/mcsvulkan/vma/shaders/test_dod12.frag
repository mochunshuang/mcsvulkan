#version 450
// diff: 添加非均匀索引扩展
#extension GL_EXT_nonuniform_qualifier:require

// 输入：来自顶点着色器的颜色（location 0）
layout(location=0)in vec3 fragColor;
layout(location=1)in vec2 fragTexCoord;// diff: 添加纹理坐标输入
layout(location=2)in flat uint fragTextureIndex;// diff: 添加纹理输入
layout(location=3)in flat uint fragSamplerIndex;// diff: 添加采样器输入

layout(location=4)in flat uint fragObjectId;// diff:[test_indirectdraw_no_pick] 接收物体 ID

// diff: [test_dod12] start
layout(location=5)in flat uint fontType;// 备用，目前未用
layout(location=6)in flat float pxRange;//diff: msdf算法需要
layout(location=7)in flat uint modulateFlag;//diff: [test_emoji]
// diff: [test_dod12] end

// 输出：到颜色附件0（替换了gl_FragColor）
layout(location=0)out vec4 outColor;

// diff:[test_indirectdraw_no_pick] 新增拾取输出（R32G32_UINT）
layout(location=1)out uvec2 outPicking;

// diff:  分离纹理和采样器数组. 从 1 开始。因为vert占用了0. 和布局有关
layout(set=0,binding=1)uniform texture2D textures[];
layout(set=0,binding=2)uniform sampler samplers[];

// diff: [test_dod12] start
float median(float r,float g,float b){
    return max(min(r,g),min(max(r,g),b));
}

float screenPxRange(){
    ivec2 texSize=textureSize(sampler2D(textures[nonuniformEXT(fragTextureIndex)],samplers[nonuniformEXT(fragSamplerIndex)]),0);
    vec2 unitRange=vec2(pxRange)/vec2(texSize);// 每个纹素对应的距离场范围（像素）
    vec2 screenTexSize=1./fwidth(fragTexCoord);// 屏幕空间纹素缩放因子
    return max(.5*dot(unitRange,screenTexSize),1.);// 屏幕像素范围，最小为1
}

// 定义字体类型常量（与 C++ 枚举值保持一致）
const int FONT_HARD_MASK=0;
const int FONT_SOFT_MASK=1;
const int FONT_SDF=2;
const int FONT_PSDF=3;
const int FONT_MSDF=4;
const int FONT_MTSDF=5;
const int FONT_BITMAP=6;
const int FONT_NONE=7;
// diff: [test_dod12] end

void main(){
    
    // 根据字体类型选择不同的渲染路径
    if(fontType==FONT_NONE){
        // 默认路径：标准纹理混合（用于 UI 矩形、普通模型等）
        vec4 texColor=texture(
            sampler2D(textures[nonuniformEXT(fragTextureIndex)],
            samplers[nonuniformEXT(fragSamplerIndex)]),
            fragTexCoord
        );
        outColor=vec4(fragColor*texColor.rgb,texColor.a);
    }else if(fontType==FONT_BITMAP){
        // 彩色位图（如表情符号）：直接采样纹理颜色
        vec4 texColor=texture(
            sampler2D(textures[nonuniformEXT(fragTextureIndex)],
            samplers[nonuniformEXT(fragSamplerIndex)]),
            fragTexCoord
        );
        // modulateFlag == 1 时用顶点颜色调制，否则直接输出纹理颜色
        if(modulateFlag==1u){
            outColor=texColor*vec4(fragColor,1.);
        }else{
            outColor=texColor;
        }
    }else{
        // MSDF / SDF 等距离场字体渲染
        vec3 msd=texture(
            sampler2D(textures[nonuniformEXT(fragTextureIndex)],
            samplers[nonuniformEXT(fragSamplerIndex)]),
            fragTexCoord
        ).rgb;
        
        float sd=median(msd.r,msd.g,msd.b);
        float screenPxDistance=screenPxRange()*(sd-.5);
        float opacity=clamp(screenPxDistance+.5,0.,1.);
        
        // modulateFlag 决定是否使用顶点颜色作为文字颜色
        if(modulateFlag==1u){
            outColor=vec4(fragColor*opacity,opacity);
        }else{
            outColor=vec4(vec3(opacity),opacity);// 默认白色文字
        }
    }
    
    outPicking=uvec2(fragObjectId,gl_PrimitiveID);// diff:[test_indirectdraw_no_pick] 直接使用内置变量
}