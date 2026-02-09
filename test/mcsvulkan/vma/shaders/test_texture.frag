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

void main(){
    
    // diff: [修改] 使用分离的纹理和采样器
    vec4 texColor=texture(
        sampler2D(textures[nonuniformEXT(fragTextureIndex)],
        samplers[nonuniformEXT(fragSamplerIndex)]),
        fragTexCoord
    );
    // 混合纹理和顶点颜色
    outColor=vec4(fragColor*texColor.rgb,texColor.a);
}