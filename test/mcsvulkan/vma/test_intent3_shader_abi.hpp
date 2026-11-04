#pragma once

// ============================================================================
// test_intent3_shader_abi.hpp
//
// test_intent3 专用。只服务 shaders/test_dod24.vert + test_dod24.frag。
// 不是库代码：这些结构体就是那两个 shader 的 buffer_reference 布局本身。
//
// 依赖（由 test_intent3.cpp 在本头之前提供）：
//   - ::UvTransform（glm::vec2 scale + offset，带 identity() 等静态函数）
//   - <glm/glm.hpp>
//
// 对应 GLSL：
//   layout(buffer_reference, scalar) readonly buffer GlyphBuffer     { Glyph glyphs[]; }
//   layout(buffer_reference, scalar) readonly buffer RectBuffer      { UiRect rects[]; }
//   layout(buffer_reference, scalar) readonly buffer RectangleBuffer { Rectangle rects[]; }
//   layout(buffer_reference, scalar) readonly buffer AttrBuffer      { VertexAttr attrs[]; }
//   layout(buffer_reference, scalar) readonly buffer CommandConstBuffer { CommandConstant consts[]; }
//   layout(push_constant) uniform PushConsts { ... }
//
// test_dod24.vert 的 switch 只有 4 个 case：
//   2 = Glyph   3 = UiRect   4 = Mesh(VertexAttr)   5 = RoundRect(Rectangle)
// 没有第五种。
// ============================================================================

#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

// ── push constants：全局命名空间（test_intent3.cpp 里一直这么用）──
struct PushData
{
    std::uint64_t vertexAddress{0};
    std::uint64_t instanceAddress{0}; // GLSL: dataAddress
    std::uint64_t commandConstantsAddress{0};
    std::uint32_t cameraIndex{0};   // 0: 3D, 1: UI
    std::uint32_t renderVersion{0}; // 与 gl_PrimitiveID 打包

    bool operator==(const PushData &) const = default;
    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return vertexAddress != 0 && instanceAddress != 0 && commandConstantsAddress != 0;
    }
};
static_assert(sizeof(PushData) == 32);
static_assert(offsetof(PushData, vertexAddress) == 0);
static_assert(offsetof(PushData, instanceAddress) == 8);
static_assert(offsetof(PushData, commandConstantsAddress) == 16);
static_assert(offsetof(PushData, cameraIndex) == 24);
static_assert(offsetof(PushData, renderVersion) == 28);

namespace shader_data
{

    // ── GLSL 侧同名常量 ─────────────────────────────────────────────
    inline constexpr std::uint32_t GLYPH_SIZE = 136;
    inline constexpr std::uint32_t UI_RECT_SIZE = 56;
    inline constexpr std::uint32_t RECTANGLE_SIZE = 272;
    inline constexpr std::uint32_t VERTEX_ATTR_SIZE = 16;
    inline constexpr std::uint32_t COMMAND_CONSTANT_SIZE = 12;

    // ── shader 的分支类型（CommandConstant::type_id）───────────────
    enum class InstanceKind : std::uint32_t
    {
        Glyph = 2,
        UiRect = 3,
        Mesh = 4,
        RoundRect = 5,
    };

    [[nodiscard]] constexpr std::uint32_t sizeOf(InstanceKind kind) noexcept
    {
        switch (kind)
        {
        case InstanceKind::Glyph:
            return GLYPH_SIZE;
        case InstanceKind::UiRect:
            return UI_RECT_SIZE;
        case InstanceKind::Mesh:
            return VERTEX_ATTR_SIZE;
        case InstanceKind::RoundRect:
            return RECTANGLE_SIZE;
        }
        return 0;
    }

    // ── 公共子结构 ─────────────────────────────────────────────────
    struct VertexTransform
    {
        glm::mat4 matrix;
    };
    static_assert(sizeof(VertexTransform) == 64);

    // ── 2：Glyph（MSDF / SDF / 位图）─────────────────────────────
    struct Glyph
    {
        static constexpr std::uint32_t type_id = 2;
        static constexpr InstanceKind kind = InstanceKind::Glyph;
        static constexpr std::uint32_t stride = GLYPH_SIZE;

        std::uint64_t data{0};         // 关联的 CPU 指针（只做身份）
        std::uint32_t entity_index{0}; // outPicking.y
        std::uint32_t textureIndex{0}; // bindless：textures[]
        std::uint32_t samplerIndex{0}; // bindless：samplers[]
        std::uint32_t fontType{0};     // 0..7，GLSL FONT_*
        float pxRange{0.0F};           // MSDF 距离场范围
        std::uint32_t modulateFlag{0}; // 1 = 顶点色调制
        glm::vec4 color{1.0F};
        glm::mat4 model{1.0F};       // [-0.5,0.5] quad -> NDC
        UvTransform uvTransform{};   // 图集 UV 变换
        std::uint32_t hover_fn{~0U}; // outPicking.w

        bool operator==(const Glyph &) const = default;
    };

    // ── 3：UiRect（布局线框）─────────────────────────────────────
    struct UiRect
    {
        static constexpr std::uint32_t type_id = 3;
        static constexpr InstanceKind kind = InstanceKind::UiRect;
        static constexpr std::uint32_t stride = UI_RECT_SIZE;

        std::uint64_t data{0};
        std::uint32_t entity_index{0};
        std::uint32_t hover_fn{~0U};
        glm::vec4 center_size{0.0F}; // center.xy + size.xy（NDC）
        glm::vec4 color{1.0F};
        float border{0.0F}; // 边框半宽（NDC）

        bool operator==(const UiRect &) const = default;
    };

    // ── 5：Rectangle（圆角 + 阴影）───────────────────────────────
    struct Rectangle
    {
        static constexpr std::uint32_t type_id = 5;
        static constexpr InstanceKind kind = InstanceKind::RoundRect;
        static constexpr std::uint32_t stride = RECTANGLE_SIZE;

        std::uint64_t data{0};
        std::uint32_t entity_index{0};
        std::uint32_t effects{0}; // FX_ROUNDED | FX_SHADOW | FX_FILL
        glm::vec4 colors[4]{};
        glm::mat4 model{1.0F};
        VertexTransform vertexTransform{};
        UvTransform uvTransform{};
        glm::vec2 size{0.0F};
        glm::vec2 shadowOffset{0.0F};
        glm::vec4 radiusSoftness{0.0F}; // x=圆角 y=边缘 z=模糊 w=扩散
        glm::vec4 shadowColor{0.0F};

        bool operator==(const Rectangle &) const = default;
    };

    inline constexpr std::uint32_t FX_ROUNDED = 1U;
    inline constexpr std::uint32_t FX_SHADOW = 2U;
    inline constexpr std::uint32_t FX_FILL = 4U;

    // ── 4：VertexAttr（普通网格，每顶点一份）─────────────────────
    struct VertexAttr
    {
        static constexpr std::uint32_t type_id = 4;
        static constexpr InstanceKind kind = InstanceKind::Mesh;
        static constexpr std::uint32_t stride = VERTEX_ATTR_SIZE;

        glm::vec4 color{1.0F};

        bool operator==(const VertexAttr &) const = default;
    };

    // ── 每条 indirect draw 一条（12 B）───────────────────────────
    struct CommandConstant
    {
        std::uint32_t type_id{0};        // InstanceKind
        std::uint32_t address_offset{0}; // 相对 heap 基址
        std::uint32_t slot_count{0};     // Mesh 专用
    };
    static_assert(sizeof(CommandConstant) == COMMAND_CONSTANT_SIZE);
    static_assert(offsetof(CommandConstant, type_id) == 0);
    static_assert(offsetof(CommandConstant, address_offset) == 4);
    static_assert(offsetof(CommandConstant, slot_count) == 8);

    // ============================================================================
    // ABI 锁：尺寸 + 偏移。任何一条炸了，就是 shader 和 C++ 脱钩了。
    // GLSL scalar 布局；uint64_t 让结构体对齐到 8。
    // ============================================================================

    static_assert(sizeof(Glyph) == GLYPH_SIZE);
    static_assert(alignof(Glyph) == 8);
    static_assert(offsetof(Glyph, data) == 0);
    static_assert(offsetof(Glyph, entity_index) == 8);
    static_assert(offsetof(Glyph, textureIndex) == 12);
    static_assert(offsetof(Glyph, samplerIndex) == 16);
    static_assert(offsetof(Glyph, fontType) == 20);
    static_assert(offsetof(Glyph, pxRange) == 24);
    static_assert(offsetof(Glyph, modulateFlag) == 28);
    static_assert(offsetof(Glyph, color) == 32);
    static_assert(offsetof(Glyph, model) == 48);
    static_assert(offsetof(Glyph, uvTransform) == 112);
    static_assert(offsetof(Glyph, hover_fn) == 128);

    static_assert(sizeof(UiRect) == UI_RECT_SIZE);
    static_assert(alignof(UiRect) == 8);
    static_assert(offsetof(UiRect, data) == 0);
    static_assert(offsetof(UiRect, entity_index) == 8);
    static_assert(offsetof(UiRect, hover_fn) == 12);
    static_assert(offsetof(UiRect, center_size) == 16);
    static_assert(offsetof(UiRect, color) == 32);
    static_assert(offsetof(UiRect, border) == 48);

    static_assert(sizeof(Rectangle) == RECTANGLE_SIZE);
    static_assert(alignof(Rectangle) == 8);
    static_assert(offsetof(Rectangle, data) == 0);
    static_assert(offsetof(Rectangle, entity_index) == 8);
    static_assert(offsetof(Rectangle, effects) == 12);
    static_assert(offsetof(Rectangle, colors) == 16);
    static_assert(offsetof(Rectangle, model) == 80);
    static_assert(offsetof(Rectangle, vertexTransform) == 144);
    static_assert(offsetof(Rectangle, uvTransform) == 208);
    static_assert(offsetof(Rectangle, size) == 224);
    static_assert(offsetof(Rectangle, shadowOffset) == 232);
    static_assert(offsetof(Rectangle, radiusSoftness) == 240);
    static_assert(offsetof(Rectangle, shadowColor) == 256);

    static_assert(sizeof(VertexAttr) == VERTEX_ATTR_SIZE);

    // 四种实例类型必须覆盖 shader 的全部分支
    static_assert(static_cast<std::uint32_t>(InstanceKind::Glyph) == 2);
    static_assert(static_cast<std::uint32_t>(InstanceKind::UiRect) == 3);
    static_assert(static_cast<std::uint32_t>(InstanceKind::Mesh) == 4);
    static_assert(static_cast<std::uint32_t>(InstanceKind::RoundRect) == 5);

} // namespace shader_data
