// ============================================================
// test_sdf.vert / test_sdf.frag 共享元数据（glslc #include）
// 注意：头文件里不能有 #version / #extension，必须留在主文件最前面
// 每个 type 的 大小 + 结构体 + buffer 声明 定义在一起
// ============================================================
#ifndef TEST_SDF_SHARED_GLSL
#define TEST_SDF_SHARED_GLSL

// 特效标志（与 cpp 上传的 effects 字段一致；追加式，值为默认推断）
const uint FX_ROUNDED = 1u; // 圆角
const uint FX_SHADOW = 2u;  // 阴影
const uint FX_FILL = 4u;    // 填充

// 基础辅助结构（被 Rectangle / Glyph 引用）
struct VertexTransform
{
    mat4 matrix;
};
struct UvTransform
{
    vec2 scale;
    vec2 offset;
};

// ============================================================
// type_id == 0：矩形实例（与 test_sdf.frag / test_sdf.cpp 对齐）。
// 颜色 = 实例数据：colors[4] 四顶点颜色（单色矩形 = 四顶点同色），
// 圆角/阴影特效对彩色矩形同样生效；缩放 = 实例 size，无任何魔术常数。
// ============================================================
#define RECTANGLE_SIZE 264
struct Rectangle
{
    uint entity_index;               // 拾取实体索引（outPicking.y）
    uint effects;                    // 特效标志（与 FX_* 按位或）
    vec4 colors[4];                  // 四顶点颜色（quad 网格固定 N=4）
    mat4 model;                      // 平移 + 旋转
    VertexTransform vertexTransform; // 顶点缩放（cpp 按 size 与公共顶点算好）
    UvTransform uvTransform;         // UV 变换
    vec2 size;                       // 卡片完整宽/高（NDC，SDF 用）
    vec2 shadowOffset;               // 阴影偏移（相对卡片宽/高；x 右为正，y 下为正）
    vec4 radiusSoftness;             // x=圆角比例 y=边缘柔化 z=阴影模糊 w=阴影扩散
    vec4 shadowColor;                // 阴影色 RGBA（a=0 → 无阴影）
};
layout(buffer_reference, scalar) readonly buffer RectangleBuffer
{
    Rectangle rects[];
};

// ============================================================
// type_id == 1：三角形实例（mesh 固定 N=3，颜色 = 实例数据）。
// 几何 = 固定顶点 × model；颜色 = colors[本地顶点索引]。
// ============================================================
#define COLORED_TRI_SIZE 116
struct ColoredTri
{
    uint entity_index; // 拾取实体索引（outPicking.y）
    mat4 model;        // 平移 + 旋转 + 缩放
    vec4 colors[3];    // 三个顶点颜色（按本地顶点索引取）
};
layout(buffer_reference, scalar) readonly buffer ColoredTriBuffer
{
    ColoredTri insts[];
};

// ============================================================
// type_id == 2：字形实例（纹理采样，MSDF/SDF/Bitmap，与 cpp / frag 对齐）。
// 字形几何 = 公共 quad × model（平移+缩放）；UV 由 uvTransform 从图集映射；
// 颜色/纹理/采样器/字体类型全部来自实例对象。
// struct Glyph 共 120 字节（scalar 布局，C++/GLSL 精确一致）。
// ============================================================
#define GLYPH_SIZE 120
struct Glyph
{
    uint entity_index;       // 拾取实体索引（outPicking.y）
    uint textureIndex;       // 纹理数组下标（bindless）
    uint samplerIndex;       // 采样器数组下标（bindless）
    uint fontType;           // 字体类型（与 FontType 枚举一致）
    float pxRange;           // MSDF 距离场范围
    uint modulateFlag;       // 1 = 用顶点色调制
    vec4 color;              // 顶点色（默认白）
    mat4 model;              // 平移 + 缩放：[-0.5,0.5] quad -> NDC 字形矩形
    UvTransform uvTransform; // 图集 UV 变换
};
layout(buffer_reference, scalar) readonly buffer GlyphBuffer
{
    Glyph glyphs[];
};

// ============================================================
// type_id == 3：通用点（quad + disc/ring/square/diamond/cross/star SDF）
// 与 test_sdf.frag / test_sdf.cpp 对齐
// ============================================================
#define UI_POINT_SIZE 48
struct UiPoint
{
    uint entity_index;
    uint style; // 0=圆盘 1=圆环 2=方块 3=菱形 4=十字 5=星形
    vec4 color;
    vec2 center; // NDC
    vec2 size;   // NDC 直径
    float softness;
    float param; // 圆环内径比例 / 星形瓣数
};
layout(buffer_reference, scalar) readonly buffer UiPointBuffer
{
    UiPoint insts[];
};

// ============================================================
// type_id == 4：通用粗线段（quad + capsule/dash/arrow SDF，圆头、端点渐变）
// ============================================================
#define UI_LINE_SIZE 68
struct UiLine
{
    uint entity_index;
    uint style;     // 0=实线胶囊 1=虚线 2=箭头 3=渐变（颜色端点插值）
    vec4 colors[2]; // 端点颜色
    vec2 posA;      // NDC
    vec2 posB;      // NDC
    float width;
    float softness;
    float param; // 虚线周期 / 箭头长度
};
layout(buffer_reference, scalar) readonly buffer UiLineBuffer
{
    UiLine insts[];
};

// ============================================================
// type_id == 5：正多边形（quad + 极坐标 SDF，sides=3..64）
// ============================================================
#define UI_POLYGON_SIZE 44
struct UiPolygon
{
    uint entity_index;
    uint sides;
    vec4 color;
    vec2 center;
    float radius;   // NDC 外接圆半径
    float rotation; // 弧度
    float softness;
};
layout(buffer_reference, scalar) readonly buffer UiPolygonBuffer
{
    UiPolygon insts[];
};

// ============================================================
// type_id == 6：任意路径网格（CPU 三角化，顶点/索引由前端上传到全局池）
// ============================================================
#define UI_MESH_SIZE 84
struct UiMesh
{
    uint entity_index;
    mat4 model; // 平移 + 旋转 + 缩放
    vec4 color; // 单色填充
};
layout(buffer_reference, scalar) readonly buffer UiMeshBuffer
{
    UiMesh insts[];
};

// ============================================================
// type_id == 7：逐顶点颜色网格（独立结构，实例不带颜色）
// 颜色只来自属性池（VertexAttribute），实例只有 entity + model
// ============================================================
#define UI_MESH_VC_SIZE 68
struct UiMeshVc
{
    uint entity_index;
    mat4 model; // 平移 + 旋转 + 缩放
};
layout(buffer_reference, scalar) readonly buffer UiMeshVcBuffer
{
    UiMeshVc insts[];
};

#endif // TEST_SDF_SHARED_GLSL
