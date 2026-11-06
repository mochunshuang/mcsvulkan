// ═══════════════════════════════════════════════════════════════════════════
//
// 这里只放「两个阶段必须一致」的常量与定义：
//   §0 类型 id          —— CommandConstant::type_id，两边 switch 共用一套名字
//   §1 命令流契约        —— CommandConstant 的布局（type_id 就在它里面）
//   §2 公共子结构        —— 被下面实例布局引用的两个变换结构
//   §3 实例布局          —— 每种实例：STRIDE + struct + buffer_reference（三者相邻）
//   §4 每帧输入          —— push constant（顶点/片元逐字段一致）
//   §5 拾取打包          —— render_version 与 primitive_id 的位宽
//
// 不放进来：
//   · layout(location = N) 的 in/out、main、屏幕空间算法 —— 属于各阶段自身；
//   · 只被一个阶段使用的资源布局（UBO / VertexBuffer / 纹理与采样器）——
//     留在各自的 .vert / .frag 里，避免另一个阶段凭空多出未使用的 descriptor。
//
// 布局约定：scalar block layout；uint64_t 让结构体按 8 字节对齐。
// C++ 侧用 sizeof / offsetof 锁同样的布局，见 test_intent6_shader_abi.hpp。
// ═══════════════════════════════════════════════════════════════════════════

#ifndef TEST_INTENT6_ABI_GLSL
#define TEST_INTENT6_ABI_GLSL

// ───────────────────────────────────────────────────────────────────────────
// §0 类型 id
//    CommandConstant::type_id 的取值，与 C++ InstanceKind 一一对应。
//    顶点阶段按它选实例布局，片元阶段按它选着色分支——必须是同一组名字。
// ───────────────────────────────────────────────────────────────────────────
const uint TYPE_GLYPH = 2u;      // Glyph       MSDF / SDF / 位图字形
const uint TYPE_RECT = 3u;       // UiRect      布局线框
const uint TYPE_MESH = 4u;       // VertexAttr  普通网格（每顶点一份属性）
const uint TYPE_ROUND_RECT = 5u; // Rectangle   圆角 + 阴影卡片
// 6：Cell —— C++ 侧已保留（CELL_SIZE = 80 / InstanceKind::Cell），
//     shader 侧的结构体与分支还没补，所以这里先不给名字，避免出现无人实现的分支。

// ───────────────────────────────────────────────────────────────────────────
// §1 命令流契约
//    indirect draw 一条对应一条命令；地址 + 类型 + 槽位数。
//    与 C++ CommandConstant 一致（12 B，scalar 布局，字段顺序不可动）。
// ───────────────────────────────────────────────────────────────────────────
struct CommandConstant
{
    uint type_id;        // §0 的 TYPE_*
    uint address_offset; // 相对实例堆基址
    uint slot_count;     // 仅 Mesh 使用：每条实例占多少顶点
};

// ───────────────────────────────────────────────────────────────────────────
// §2 公共子结构
//    只被 §3 的实例布局引用：Glyph 用 UvTransform，Rectangle 两个都用。
// ───────────────────────────────────────────────────────────────────────────
struct UvTransform
{
    vec2 scale;
    vec2 offset;
};

struct VertexTransform
{
    mat4 matrix;
};

// ───────────────────────────────────────────────────────────────────────────
// §3 实例布局
//    每种实例固定三段，按 type_id 升序排：
//        #define XXX_SIZE   —— = C++ 的 stride，两边必须一致
//        struct XXX
//        layout(buffer_reference, scalar) buffer XXXBuffer
//    缓冲声明紧跟结构体：改字段时不用翻文件找配套的 buffer_reference。
// ───────────────────────────────────────────────────────────────────────────

// ── 3.1 Glyph（type 2）──────────────────────────────────────────────────────
// fontType 的取值，与 C++ FontType 枚举一致
const int FONT_HARD_MASK = 0;
const int FONT_SOFT_MASK = 1;
const int FONT_SDF = 2;
const int FONT_PSDF = 3;
const int FONT_MSDF = 4;
const int FONT_MTSDF = 5;
const int FONT_BITMAP = 6;
const int FONT_NONE = 7;

#define GLYPH_SIZE 136
struct Glyph
{
    uint64_t data;           // 关联的 CPU 指针（只做身份）
    uint entity_index;       // 拾取实体索引（outPicking.y）
    uint textureIndex;       // 纹理数组下标（bindless）
    uint samplerIndex;       // 采样器数组下标（bindless）
    uint fontType;           // FONT_*（与 C++ FontType 一致）
    float pxRange;           // MSDF 距离场范围
    uint modulateFlag;       // 1 = 用顶点色调制
    vec4 color;              // 顶点色（默认白）
    mat4 model;              // 平移 + 缩放：[-0.5,0.5] quad → NDC 字形矩形
    UvTransform uvTransform; // 图集 UV 变换
    uint hover_fn;           // hover 函数池下标（0xFFFFFFFF = 未绑定），写回 outPicking.w
};
layout(buffer_reference, scalar) readonly buffer GlyphBuffer
{
    Glyph glyphs[];
};

// ── 3.2 UiRect（type 3）─────────────────────────────────────────────────────
#define UI_RECT_SIZE 56
struct UiRect
{
    uint64_t data;     // 关联的 CPU 指针
    uint entity_index; // 拾取外键
    uint hover_fn;     // hover 池下标（0xFFFFFFFF = 未绑定）
    vec4 center_size;  // center.xy + size.xy（NDC）
    vec4 color;        // 边框色
    float border;      // 边框半宽（NDC）
};
layout(buffer_reference, scalar) readonly buffer RectBuffer
{
    UiRect rects[];
};

// ── 3.3 VertexAttr（type 4，Mesh）───────────────────────────────────────────
// 每顶点一份属性；顶点位置来自 .vert 的 VertexData，不走这里。
#define VERTEX_ATTR_SIZE 16
struct VertexAttr
{
    vec4 color; // 每顶点颜色（插值 → 渐变）
};
layout(buffer_reference, scalar) readonly buffer AttrBuffer
{
    VertexAttr attrs[];
};

// ── 3.4 Rectangle（type 5）──────────────────────────────────────────────────
// effects 位，与 C++ FX_* 一致；三个位可以任意组合。
const uint FX_ROUNDED = 1u; // 圆角
const uint FX_SHADOW = 2u;  // 投影
const uint FX_FILL = 4u;    // 填充

#define RECTANGLE_SIZE 272
struct Rectangle
{
    uint64_t data;                   // 关联的 CPU 指针
    uint entity_index;               // 拾取实体索引（outPicking.y）
    uint effects;                    // §3.4 顶部 FX_* 按位或
    vec4 colors[4];                  // 四顶点颜色（单色 = 四顶点同色）
    mat4 model;                      // 平移 + 旋转
    VertexTransform vertexTransform; // 顶点缩放（quad [-0.5,0.5] × size）
    UvTransform uvTransform;         // UV 变换
    vec2 size;                       // 卡片完整宽/高（NDC，SDF 用）
    vec2 shadowOffset;               // 阴影偏移（相对卡片尺寸；x 右为正，y 下为正）
    vec4 radiusSoftness;             // x=圆角比例 y=边缘柔化 z=阴影模糊 w=阴影扩散
    vec4 shadowColor;                // 阴影色 RGBA（a=0 → 无阴影）
};
layout(buffer_reference, scalar) readonly buffer RectangleBuffer
{
    Rectangle rects[];
};

// ───────────────────────────────────────────────────────────────────────────
// §4 每帧输入：push constant
//    顶点与片元必须逐字段一致（片元只用到 render_version）。
//    与 C++ PushData 的命名对应：dataAddress ↔ instanceAddress。
// ───────────────────────────────────────────────────────────────────────────
layout(push_constant) uniform PushConsts
{
    uint64_t vertexAddress;
    uint64_t dataAddress; // 实例堆基址（C++：instanceAddress）
    uint64_t commandConstantsAddress;
    uint cameraIndex; // 0: 3D, 1: UI
    uint render_version;
}
pc;

// ───────────────────────────────────────────────────────────────────────────
// §5 拾取打包
//    outPicking.z = (render_version << 16) | (gl_PrimitiveID & PRIMITIVE_MASK)
//    与 C++ render_version<16> / picking_result 的位分配对齐。
// ───────────────────────────────────────────────────────────────────────────
const int RENDER_VERSION_BIT_WIDTH = 16;
#define PRIMITIVE_MASK ((1u << (32 - RENDER_VERSION_BIT_WIDTH)) - 1u)

#endif // TEST_INTENT6_ABI_GLSL
