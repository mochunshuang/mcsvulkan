#version 450
// 启用所需扩展：缓冲引用、标量块布局、显式64位类型
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_ARB_shader_draw_parameters : enable //diff: 暴露 gl_BaseVertexARB

// NOTE: 公共顶点池：只存固定几何（pos/texCoord），顶点就是坐标本身
//       （quad 为 [-0.5,0.5] 单位矩形，三角形为 NDC 坐标），无需归一化。
//       颜色不是网格数据，而是实例数据（std::array<color,N> 由实例上传）。
struct VertexData
{
    vec3 pos;
    vec2 texCoord; // 纹理坐标
};
layout(buffer_reference, scalar) readonly buffer VertexBuffer
{
    VertexData vertices[];
};
// NOTE: 实例化相关的 顶点 和 UV 通过计算变化 才能得到真正的实例化的 顶点和UV
const int VERTEX_TRANSFORM_SIZE = 64;
struct VertexTransform
{
    mat4 matrix;
};
const int UV_TRANSFORM_SIZE = int(2 * 8 + 2 * 8);
struct UvTransform
{
    vec2 scale;
    vec2 offset;
};

// 全局统一缓冲：视图矩阵和投影矩阵
struct CameraInfo
{
    mat4 view;
    mat4 proj;
};
layout(set = 0, binding = 0) uniform UniformBufferObject
{
    CameraInfo cameraInfo[2];
}
ubo;

layout(buffer_reference, scalar) readonly buffer InstanceBuffer
{
    uint64_t instances[];
};

// 命令常量：一条间接绘制命令对应一条（type_id + 实例偏移）
struct CommandConstant
{
    uint type_id;
    uint adddress_offset; // 实例数据区偏移（heap）
    uint perInstanceAttributeCount; // type 7：每实例属性数
    uint attributeOffset;           // type 7：属性池偏移（上传命令常量时解析）
};
layout(buffer_reference, scalar) readonly buffer CommandConstBuffer
{
    CommandConstant consts[];
};

layout(push_constant) uniform PushConsts
{
    uint64_t vertexAddress;
    uint64_t dataAddress;
    uint64_t commandConstantsAddress;
    uint64_t attributeAddress; // 逐顶点属性池（type 7 用）
    uint cameraIndex;
}
pc;

// ============================================================
// type_id == 0：矩形实例（与 test_sdf.frag / test_sdf.cpp 对齐）。
// 颜色 = 实例数据：colors[4] 四顶点颜色（单色矩形 = 四顶点同色），
// 圆角/阴影特效对彩色矩形同样生效；缩放 = 实例 size，无任何魔术常数。
// struct Rectangle 共 276 字节（scalar 布局，C++/GLSL 精确一致）。
// ============================================================
#define RECTANGLE_SIZE 264

// 特效标志（与 cpp / frag 保持一致；追加式，值为默认推断）
const uint FX_ROUNDED = 1u; // 圆角
const uint FX_SHADOW = 2u;  // 阴影
const uint FX_FILL = 4u;    // 填充

struct Rectangle
{
    uint entity_index;         // 拾取实体索引（outPicking.y）
    uint effects;              // 特效标志（与 FX_* 按位或）
    vec4 colors[4];            // 四顶点颜色（quad 网格固定 N=4）
    mat4 model;                // 平移 + 旋转
    VertexTransform vertexTransform; // 顶点缩放（cpp 按 size 与公共顶点算好）
    UvTransform uvTransform;   // UV 变换
    vec2 size;                 // 卡片完整宽/高（NDC，SDF 用）
    vec2 shadowOffset;         // 阴影偏移（相对卡片宽/高；x 右为正，y 下为正）
    vec4 radiusSoftness;       // x=圆角比例 y=边缘柔化 z=阴影模糊 w=阴影扩散
    vec4 shadowColor;          // 阴影色 RGBA（a=0 → 无阴影）
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
    mat4 model;       // 平移 + 旋转 + 缩放
    vec4 colors[3];   // 三个顶点颜色（按本地顶点索引取）
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
    uint entity_index;         // 拾取实体索引（outPicking.y）
    uint textureIndex;         // 纹理数组下标（bindless）
    uint samplerIndex;         // 采样器数组下标（bindless）
    uint fontType;             // 字体类型（与 FontType 枚举一致）
    float pxRange;             // MSDF 距离场范围
    uint modulateFlag;         // 1 = 用顶点色调制
    vec4 color;                // 顶点色（默认白）
    mat4 model;                // 平移 + 缩放：[-0.5,0.5] quad -> NDC 字形矩形
    UvTransform uvTransform;   // 图集 UV 变换
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
    uint style;       // 0=圆盘 1=圆环 2=方块 3=菱形 4=十字 5=星形
    vec4 color;
    vec2 center;      // NDC
    vec2 size;        // NDC 直径
    float softness;
    float param;      // 圆环内径比例 / 星形瓣数
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
    uint style;       // 0=实线胶囊 1=虚线 2=箭头 3=渐变（颜色端点插值）
    vec4 colors[2];   // 端点颜色
    vec2 posA;        // NDC
    vec2 posB;        // NDC
    float width;
    float softness;
    float param;      // 虚线周期 / 箭头长度
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
    float radius;     // NDC 外接圆半径
    float rotation;   // 弧度
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
    mat4 model;       // 平移 + 旋转 + 缩放
    vec4 color;       // 单色填充
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
    mat4 model;       // 平移 + 旋转 + 缩放
};
layout(buffer_reference, scalar) readonly buffer UiMeshVcBuffer
{
    UiMeshVc insts[];
};

// type 7 逐顶点属性（独立属性池，只在用到时读取，同 test_dod18）
struct VertexAttribute
{
    vec3 color;
};
layout(buffer_reference, scalar) readonly buffer AttributePool
{
    VertexAttribute attributes[];
};


layout(location = 0) out vec4 fragColor;     // 插值后的颜色
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint type_id;
layout(location = 3) flat out uint64_t instancePtr;

layout(location = 4) out vec2 localPos; // 归一化局部坐标 [-0.5,0.5] × 阴影外扩系数

const uint INVALID_INDEX = 0xFFFFFFFFu;

void main()
{
    fragColor = vec4(1., 1., 1., 1.); //NOTE: 默认白色
    fragTexCoord = vec2(0.0);
    mat4 model = mat4(1.);            //NOTE: 默认无副作用
    vec3 pos = vec3(0.0);
    localPos = vec2(0.0);

    gl_PointSize = 8.; // 管线切换演示：POINT_LIST 时用大点，三角形/线框绘制时被忽略

    // NOTE: 多实例间接绘制（每条 draw 命令由 gl_DrawIDARB 对应一条命令常量）
    CommandConstBuffer cmdConsts = CommandConstBuffer(pc.commandConstantsAddress);
    CommandConstant cc = cmdConsts.consts[gl_DrawIDARB];
    type_id = cc.type_id;
    uint64_t heapBaseStart = pc.dataAddress + cc.adddress_offset;
    CameraInfo cam = ubo.cameraInfo[pc.cameraIndex];

    VertexBuffer vertBuf = VertexBuffer(pc.vertexAddress);
    VertexData v = vertBuf.vertices[gl_VertexIndex]; // 公共顶点池，按 mesh 偏移取用
    // 本地顶点索引（0 ~ mesh.vertexCount-1），颜色数组按它取
    uint localVertexIndex = gl_VertexIndex - gl_BaseVertexARB;

    switch (type_id)
    {
    case 0: {
        instancePtr = heapBaseStart + gl_InstanceIndex * RECTANGLE_SIZE;
        Rectangle inst = RectangleBuffer(instancePtr).rects[0]; // 直接读取实例
        model = inst.model * inst.vertexTransform.matrix; // 平移 + 旋转 + 尺寸缩放
        fragColor = inst.colors[localVertexIndex]; // 顶点颜色来自实例（最细粒度）
        fragTexCoord = inst.uvTransform.offset + v.texCoord * inst.uvTransform.scale;

        // ---- 阴影几何外扩：后端自动按阴影参数扩大 quad 范围 ----
        // 保证“卡片 + 阴影”始终落在顶点几何内，前端无需预留空间。
        bool hasShadow = (inst.effects & FX_SHADOW) != 0u || inst.shadowColor.a > 0.0;
        vec2 expand = vec2(1.0);
        if (hasShadow) {
            vec2 cardHalf = inst.size * 0.5;
            float edgeSoft = max(inst.radiusSoftness.y, 0.0005) * inst.size.x;
            float blur = max(inst.radiusSoftness.z, 0.0005) * inst.size.x;
            vec2 shadowHalf = cardHalf * max(inst.radiusSoftness.w, 0.0);
            vec2 offset = inst.shadowOffset * inst.size;
            vec2 margin = max(shadowHalf + blur + abs(offset) - cardHalf, vec2(0.0));
            margin += vec2(edgeSoft * 0.5); // 抗锯齿余量
            expand = (cardHalf + margin) / max(cardHalf, vec2(1e-6));
        }

        localPos = v.pos.xy * expand; // quad 顶点本身即 [-0.5,0.5]
        pos.xy = v.pos.xy * expand;   // 外扩发生在本地空间，先于 model 的旋转/缩放
    }
    break;
    case 1: {
        instancePtr = heapBaseStart + gl_InstanceIndex * COLORED_TRI_SIZE;
        ColoredTri inst = ColoredTriBuffer(instancePtr).insts[0];
        model = inst.model;
        fragColor = inst.colors[localVertexIndex]; // 顶点颜色来自实例（最细粒度）
        pos = v.pos;
    }
    break;
    case 2: {
        instancePtr = heapBaseStart + gl_InstanceIndex * GLYPH_SIZE;
        Glyph inst = GlyphBuffer(instancePtr).glyphs[0]; // 直接读取实例
        model = inst.model;                    // 平移 + 缩放（字形矩形）
        fragColor = inst.color;                // 顶点色，供 modulate 使用
        fragTexCoord = inst.uvTransform.offset + v.texCoord * inst.uvTransform.scale;
        localPos = v.pos.xy;
        pos = v.pos;
    }
    break;
    case 3: {
        instancePtr = heapBaseStart + gl_InstanceIndex * UI_POINT_SIZE;
        UiPoint inst = UiPointBuffer(instancePtr).insts[0];
        // 平移 + 缩放：quad [-0.5,0.5] -> NDC [center-size/2, center+size/2]
        model = mat4(vec4(inst.size.x, 0.0, 0.0, 0.0),
                     vec4(0.0, inst.size.y, 0.0, 0.0),
                     vec4(0.0, 0.0, 1.0, 0.0),
                     vec4(inst.center, 0.0, 1.0));
        fragColor = inst.color;
        localPos = v.pos.xy;
        pos = v.pos;
    }
    break;
    case 4: {
        instancePtr = heapBaseStart + gl_InstanceIndex * UI_LINE_SIZE;
        UiLine inst = UiLineBuffer(instancePtr).insts[0];
        vec2 a = inst.posA;
        vec2 b = inst.posB;
        vec2 dir = normalize(b - a);
        float len = length(b - a);
        vec2 nrm = vec2(-dir.y, dir.x);
        float w = max(inst.width, 1e-4);
        vec2 halfSize = vec2(len * 0.5 + w * 0.5, w * 0.5); // 圆头外扩
        vec2 p = v.pos.xy * 2.0 * halfSize;                 // 线段局部坐标（NDC）
        vec2 center = (a + b) * 0.5;
        pos.xy = center + dir * p.x + nrm * p.y;
        localPos = p;
        fragColor = inst.colors[uint(v.pos.x > 0.0)];       // 端点渐变
    }
    break;
    case 5: {
        instancePtr = heapBaseStart + gl_InstanceIndex * UI_POLYGON_SIZE;
        UiPolygon inst = UiPolygonBuffer(instancePtr).insts[0];
        // quad 覆盖外接圆 bbox：直径 = 2*radius
        model = mat4(vec4(2.0 * inst.radius, 0.0, 0.0, 0.0),
                     vec4(0.0, 2.0 * inst.radius, 0.0, 0.0),
                     vec4(0.0, 0.0, 1.0, 0.0),
                     vec4(inst.center, 0.0, 1.0));
        fragColor = inst.color;
        localPos = v.pos.xy;
        pos = v.pos;
    }
    break;
    case 6: {
        instancePtr = heapBaseStart + gl_InstanceIndex * UI_MESH_SIZE;
        UiMesh inst = UiMeshBuffer(instancePtr).insts[0];
        model = inst.model;
        fragColor = inst.color; // 实色填充
        pos = v.pos; // 几何由 CPU 三角化上传，这里直接使用
    }
    break;
    case 7: {
        instancePtr = heapBaseStart + gl_InstanceIndex * UI_MESH_VC_SIZE;
        UiMeshVc inst = UiMeshVcBuffer(instancePtr).insts[0];
        // 属性池基址来自推常量，属性偏移在命令常量上传时解析
        AttributePool attrPool = AttributePool(pc.attributeAddress);
        uint attrIdx = gl_InstanceIndex * cc.perInstanceAttributeCount + localVertexIndex +
                       cc.attributeOffset;
        VertexAttribute attr = attrPool.attributes[attrIdx];
        fragColor = vec4(attr.color, 1.0); // 颜色只来自属性池
        model = inst.model;
        pos = v.pos;
    }
    break;
    default:
        break;
    }

    mat4 mvp = cam.proj * cam.view * model;
    gl_Position = mvp * vec4(pos, 1.0);
}
