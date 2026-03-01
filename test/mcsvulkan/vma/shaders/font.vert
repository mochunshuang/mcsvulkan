#version 450

layout(location=0)in vec2 inPos;
layout(location=1)in vec2 inUV;

layout(location=0)out vec2 fragUV;

void main(){
    gl_Position=vec4(inPos,0.,1.);
    // 将屏幕坐标 [0,width]x[0,height] 转换到 NDC [-1,1]x[-1,1]（Y 轴向下）
    // 这里我们直接用正交投影，但为了简化，直接在顶点着色器映射：
    // 假设传入的坐标已经是 [0,width] x [0,height]（原点左上角）
    // 需要转换到 NDC： x_ndc = (x / width) * 2 - 1, y_ndc = 1 - (y / height) * 2
    // 为简化，我们在 CPU 端使用 glm::ortho 生成投影矩阵并通过 push constant 传入，
    // 但本 demo 为了精简，直接在顶点着色器硬编码屏幕变换。
    // 下面的代码假设传入的 inPos 已经是 NDC 坐标（由 CPU 预先变换好）。
    // 因此直接传递 gl_Position = vec4(inPos, 0,1);
    // 注意：在生成顶点时，我们直接使用了屏幕坐标，没有应用投影矩阵。
    // 为了正确显示，我们需要在顶点着色器中将屏幕坐标转换到 NDC。
    // 下面添加这个转换：
    // 假设 inPos 范围 [0, WIDTH] x [0, HEIGHT]
    float w=800.;// 需要从外部传入，但为简化，写死
    float h=600.;
    float x_ndc=(inPos.x/w)*2.-1.;
    float y_ndc=1.-(inPos.y/h)*2.;// 翻转 Y
    gl_Position=vec4(x_ndc,y_ndc,0.,1.);
    
    fragUV=inUV;
}