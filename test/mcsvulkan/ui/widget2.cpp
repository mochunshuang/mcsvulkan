#include "head.hpp"
#include <any>
#include <cassert>
#include <cstddef>
#include <type_traits>
#include <typeindex>

using mcs::vulkan::ecs::gen_soa_struct;
using mcs::vulkan::meta::make_aggregate;
using mcs::vulkan::meta::static_string;

#include <iostream>
#include <exception>

// 原理: https://docs.flutter.dev/ui/layout/constraints
//      https://docs.flutter.dev/ui/layout#overview
// 概念：约束向下传递，尺寸向上传递，父节点决定子节点位置。
// 本实现将 Flutter 布局原则映射为 C++ 代码，所有行为均与官方一致。

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/// 表示一个矩形的尺寸（宽、高）
struct Size
{
    float width, height;
};

/// 二维偏移量（x, y）
struct Offset
{
    float x, y;
};

/// 边缘空白（左、上、右、下），对应 Flutter 的 EdgeInsets
struct EdgeInsets
{
    float left = 0, top = 0, right = 0, bottom = 0;
    constexpr float horizontal() const
    {
        return left + right;
    }
    constexpr float vertical() const
    {
        return top + bottom;
    }
};

/// 对齐方式，用归一化坐标（-1..1）表示，对应 Flutter 的 Alignment
struct Alignment
{
    float x, y;
};
namespace Align
{
    constexpr Alignment topLeft{-1, -1}, topCenter{0, -1}, topRight{1, -1};
    constexpr Alignment centerLeft{-1, 0}, center{0, 0}, centerRight{1, 0};
    constexpr Alignment bottomLeft{-1, 1}, bottomCenter{0, 1}, bottomRight{1, 1};
} // namespace Align

// ============================================================================
// 约束（Constraints）：Flutter 布局的核心
// ============================================================================

/// 约束由最小/最大宽度和最小/最大高度组成。
/// 父级将约束传递给子级，子级在约束范围内选择自己的尺寸。
struct Constraints
{
    static constexpr float inf = std::numeric_limits<float>::infinity();
    float minW = 0, maxW = inf, minH = 0, maxH = inf;

    constexpr bool hasBoundedWidth() const noexcept
    {
        return maxW < inf;
    }
    constexpr bool hasBoundedHeight() const noexcept
    {
        return maxH < inf;
    }
    constexpr bool hasUnboundedWidth() const noexcept
    {
        return maxW >= inf;
    }
    constexpr bool hasUnboundedHeight() const noexcept
    {
        return maxH >= inf;
    }

    /// 减去 EdgeInsets 后返回新的约束（用于 padding 或 margin）
    constexpr Constraints deflate(const EdgeInsets &e) const noexcept
    {
        float h = e.horizontal(), v = e.vertical();
        return {std::max(0.0f, minW - h), std::max(0.0f, maxW - h),
                std::max(0.0f, minH - v), std::max(0.0f, maxH - v)};
    }

    /// 与本约束取交集（收紧约束），对应 Flutter 的 BoxConstraints.intersect
    /// 注意：当 min > max 时，本实现会尝试修正为合理值，而非直接返回无效约束。
    /// 这与 Flutter 官方行为略有不同（官方会保留 min > max 让上层处理），
    /// 但能避免大多数意外崩溃，对布局结果无实质影响。
    constexpr Constraints intersect(const Constraints &parent) const noexcept
    {
        float newMinW = std::max(parent.minW, minW);
        float newMaxW = std::min(parent.maxW, maxW);
        float newMinH = std::max(parent.minH, minH);
        float newMaxH = std::min(parent.maxH, maxH);
        if (newMinW > newMaxW)
            newMinW = newMaxW = std::clamp(minW, parent.minW, parent.maxW);
        if (newMinH > newMaxH)
            newMinH = newMaxH = std::clamp(minH, parent.minH, parent.maxH);
        return {newMinW, newMaxW, newMinH, newMaxH};
    }

    /// 应用固定宽度（收紧约束）
    constexpr Constraints applyFixedWidth(float w) const noexcept
    {
        return Constraints{w, w, minH, maxH}.intersect(*this);
    }
    /// 应用固定高度（收紧约束）
    constexpr Constraints applyFixedHeight(float h) const noexcept
    {
        return Constraints{minW, maxW, h, h}.intersect(*this);
    }

    /// 将 Size 限定在约束范围内
    constexpr Size clamp(const Size &s) const noexcept
    {
        return {std::clamp(s.width, minW, maxW), std::clamp(s.height, minH, maxH)};
    }
};

// ============================================================================
// 文本方向与垂直方向（支持 RTL/Up 布局）
// ============================================================================
/// 文本方向，影响 Row 的主轴起始方向
enum class TextDirection
{
    ltr,
    rtl
};

/// 垂直方向，影响 Column 的主轴起始方向
enum class VerticalDirection
{
    down,
    up
};

// ============================================================================
// 枚举与样式（对应 Flutter 的各类配置）
// ============================================================================
// NOTE: 用途：对齐 widget。https://docs.flutter.cn/ui/layout#aligning-widgets
// NOTE: 对于一行来说，主轴水平延伸，交叉轴垂直延伸。对于一列来说，主轴垂直延伸，交叉轴水平延伸。
enum class MainAxisAlignment
{
    start,        //子项从主轴的起始边开始排列
    end,          //从主轴的末尾边开始排列。
    center,       //子项在主轴上居中排列。
    spaceBetween, //第一个子项靠起始边，最后一个靠末尾边，其余子项之间的间距均匀相等
    spaceAround, //每个子项两侧的间距相等，但首尾两侧的间距是子项之间间距的一半（因为两端各只有一半）
    spaceEvenly // 因此设置主轴对齐方式为 spaceEvenly 会将空余空间在每个图像之间、之前和之后均匀地划分
};
enum class CrossAxisAlignment
{
    start,   //对齐到交叉轴的起始边（Row 的顶部，Column 的左侧，不受 TextDirection 影响）
    end,     //对齐到交叉轴的末尾边（Row 的底部，Column 的右侧）。
    center,  //在交叉轴上居中。
    stretch, //子项在交叉轴方向上拉伸以填满整个容器（例如 Row 中的子项高度会被拉伸到与 Row 高度一致，除非子项本身有固定高度约束）
    baseline // 新增：基线对齐（仅对 Row 有效）
};
enum class MainAxisSize
{
    max,
    min
};
enum class WidgetKind : uint8_t
{
    Container,
    Row,
    Column,
    Expanded, //通过使用 Expanded widget，可以调整 widget 的大小【以适合行或列】。
    Text
};

/// Widget 引用：类型 + 索引，用于从全局池中获取样式
struct WidgetRef
{
    WidgetKind kind;
    uint32_t idx;
};
/// 几何信息（位置和尺寸），移除了冗余的 padding 字段
struct BoxGeometry
{
    float x = 0, y = 0, w = 0, h = 0;
};

/// 布局树节点，包含 Widget 引用、子节点、几何信息和调试名称
struct Node
{
    WidgetRef ref;
    std::vector<Node> children;
    BoxGeometry geometry;
    std::string name;
    float baseline = 0; // 基线偏移，相对于节点自身顶部（y 方向）
};
/// 测量结果：包含自然尺寸和基线偏移（相对于顶部）
struct MeasuredSize
{
    Size size;
    float baseline = 0; // 基线偏移量（从顶部算起），若未知可设为 size.height
};

// ----------------- 主轴/交叉轴工具（用于 Flex 布局） -----------------
// 下列函数不再只依赖 isRow，而是使用 AxisDirection 来精确计算方向
enum class AxisDirection
{
    right, // 左→右 (Row, ltr)
    left,  // 右→左 (Row, rtl)
    down,  // 上→下 (Column, down)
    up     // 下→上 (Column, up)
};

/// 根据布局参数推导主轴方向
static AxisDirection axisDirectionForFlex(bool isRow, TextDirection td,
                                          VerticalDirection vd)
{
    if (isRow)
        return (td == TextDirection::ltr) ? AxisDirection::right : AxisDirection::left;
    else
        return (vd == VerticalDirection::down) ? AxisDirection::down : AxisDirection::up;
}

/// 返回该轴方向是否在物理坐标上是正向（从左到右或从上到下）
static bool isAxisForward(AxisDirection dir)
{
    return dir == AxisDirection::right || dir == AxisDirection::down;
}

/// 从约束中提取主轴尺寸（Row 取宽度，Column 取高度）
static float mainAxisSizeFromConstraints(const Constraints &bc, bool isRow)
{
    return isRow ? bc.maxW : bc.maxH;
}

/// 从约束中提取交叉轴尺寸（Row 取高度，Column 取宽度）
static float crossAxisSizeFromConstraints(const Constraints &bc, bool isRow)
{
    return isRow ? bc.maxH : bc.maxW;
}

/// 根据轴方向从 EdgeInsets 中提取主轴起始边的 padding 值
/// 例如：右向轴（Row ltr）取 left，左向轴（Row rtl）取 right，下向轴取 top，上向轴取 bottom
static float mainAxisPaddingStart(const EdgeInsets &pad, AxisDirection dir)
{
    switch (dir)
    {
    case AxisDirection::right:
        return pad.left;
    case AxisDirection::left:
        return pad.right;
    case AxisDirection::down:
        return pad.top;
    case AxisDirection::up:
        return pad.bottom;
    }
    return 0;
}

/// 根据轴方向从 EdgeInsets 中提取主轴末尾边的 padding 值（与 start 相对）
static float mainAxisPaddingEnd(const EdgeInsets &pad, AxisDirection dir)
{
    switch (dir)
    {
    case AxisDirection::right:
        return pad.right;
    case AxisDirection::left:
        return pad.left;
    case AxisDirection::down:
        return pad.bottom;
    case AxisDirection::up:
        return pad.top;
    }
    return 0;
}

/// 根据轴方向从 EdgeInsets 中提取交叉轴起始边的 padding 值
/// 对于 Row（水平主轴），交叉轴是垂直方向，起始边为 top；对于 Column，交叉轴是水平方向，起始边为 left
static float crossAxisPaddingStart(const EdgeInsets &pad, AxisDirection dir)
{
    if (dir == AxisDirection::right || dir == AxisDirection::left)
        return pad.top; // Row 的交叉轴起始边是 top
    else
        return pad.left; // Column 的交叉轴起始边是 left
}

/// 根据轴方向从 EdgeInsets 中提取交叉轴末尾边的 padding 值（与 start 相对）
static float crossAxisPaddingEnd(const EdgeInsets &pad, AxisDirection dir)
{
    if (dir == AxisDirection::right || dir == AxisDirection::left)
        return pad.bottom;
    else
        return pad.right;
}

/// 获取子节点在主轴方向上的长度（Row 取宽度，Column 取高度）
static float childMainAxisLength(const Node &child, bool isRow)
{
    return isRow ? child.geometry.w : child.geometry.h;
}

/// 获取子节点在交叉轴方向上的长度（Row 取高度，Column 取宽度）
static float childCrossAxisLength(const Node &child, bool isRow)
{
    return isRow ? child.geometry.h : child.geometry.w;
}

/// 设置子节点在主轴上的物理坐标，考虑轴方向（正向或反向）
/// pos 是逻辑起点（从主轴的 start 边算起），函数会根据轴方向计算出最终物理坐标
static void setChildMainAxisPosition(Node &child, AxisDirection dir, float pos,
                                     float childMainLen, float parentMainSize,
                                     float padStart, float padEnd)
{
    // pos 是逻辑起点（从主轴的 start 边算起，不考虑方向）
    float physical = 0;
    if (isAxisForward(dir))
    {
        // 正向轴：物理坐标 = padding_start + 逻辑位置
        physical = padStart + pos;
    }
    else
    {
        // 反向轴：物理坐标 = 父容器尺寸 - padding_end - 逻辑位置 - 子元素长度
        physical = parentMainSize - padEnd - pos - childMainLen;
    }
    // 根据轴方向设置对应的坐标字段
    if (dir == AxisDirection::right || dir == AxisDirection::left)
        child.geometry.x = physical;
    else
        child.geometry.y = physical;
}

/// 设置子节点在交叉轴上的物理坐标（目前仅支持正向，即从左到右或从上到下）
static void setChildCrossAxisPosition(Node &child, AxisDirection dir, float pos,
                                      float childCrossLen, float parentCrossSize,
                                      float padCrossStart, float padCrossEnd)
{
    // 交叉轴目前总是正向，暂不考虑翻转
    (void)childCrossLen;
    (void)parentCrossSize;
    (void)padCrossEnd;
    if (dir == AxisDirection::right || dir == AxisDirection::left)
        child.geometry.y = padCrossStart + pos;
    else
        child.geometry.x = padCrossStart + pos;
}

/// 判断约束在主轴方向是否有界（不是无限）
static bool mainAxisIsBounded(const Constraints &bc, bool isRow)
{
    return isRow ? bc.hasBoundedWidth() : bc.hasBoundedHeight();
}

/// 判断约束在交叉轴方向是否有界（不是无限）
static bool crossAxisIsBounded(const Constraints &bc, bool isRow)
{
    return isRow ? bc.hasBoundedHeight() : bc.hasBoundedWidth();
}

/// 根据 Row/Column 生成主轴/交叉轴对应的约束（辅助函数）
static Constraints makeFlexAxisConstraints(bool isRow, float minMain, float maxMain,
                                           float minCross, float maxCross)
{
    if (isRow)
        return {minMain, maxMain, minCross, maxCross};
    else
        return {minCross, maxCross, minMain, maxMain};
}

class RefAny
{
  private:
    void *ptr_ = nullptr;
    std::type_index type_ = typeid(void); // 默认空

  public:
    // 构造函数：只接受左值引用，存入指针和类型
    constexpr RefAny() = default;
    template <typename T> //NOTE: 不保留引用信息
    constexpr RefAny(T &value) noexcept
        : ptr_(static_cast<void *>(std::addressof(value))), type_(typeid(T))
    {
    }

    // 判断是否有值
    constexpr bool has_value() const noexcept
    {
        return ptr_ != nullptr;
    }

    // 获取原始地址（裸指针）
    constexpr void *raw_address() const noexcept
    {
        return ptr_;
    }

    // NOTE: 如果不是或不存在 或 判断失败，异常，严格的
    // 类型安全提取引用（类似 std::any_cast）
    template <typename T>
    constexpr friend T &any_cast(RefAny &any)
    {

        if (any.type_ != typeid(T))
            throw std::bad_cast();
        return *static_cast<T *>(any.ptr_);
    }

    template <typename T>
    constexpr friend const T &any_cast(const RefAny &any)
    {
        if (any.type_ != typeid(T))
            throw std::bad_cast();
        return *static_cast<const T *>(any.ptr_);
    }

    template <typename T>
    constexpr auto &cast_to(this auto &&self)
    {
        return any_cast<T>(self);
    }

    // NOTE: 如果 cast 失败还有默认值，对于看你不存在的 成员 ？给默认成员，松散的。 如果 any 不存放这个类型,避免异常
    template <typename T>
    constexpr auto value_or(this auto &&self, T value) noexcept
        requires(requires() { *(any_cast<T>(self)); })
    {
        try
        {
            auto &ref = any_cast<T>(self);
            return ref ? *ref : value;
        }
        catch (...)
        {
        }
        return value;
    }

    // 如果 RefAny 为空（默认构造），返回给定的默认值；
    // 否则提取为 T 并返回（值语义）。
    template <typename T>
    constexpr T miss_return(this auto &&self, T defaultVal) noexcept
    {
        if (!self.has_value())
            return defaultVal;
        return any_cast<T>(self);
    }
};

// ============================================================================
// 布局辅助函数（对应 Flutter 布局过程中的常见操作）
// ============================================================================
template <static_string name>
static constexpr auto do_get_member(auto &soaCtx, const WidgetRef &ref)
{
    return soaCtx.get_member.template operator()<name>(soaCtx, ref);
};
/// 生成宽松约束：min=0, max=原 max，让子节点可以自由选择尺寸
static Constraints makeLooseConstraints(const Constraints &c)
{
    return {0.0f, c.maxW, 0.0f, c.maxH};
}

/// 计算子节点占用的总尺寸（包括 margin）
static Size childFullSize(auto &soaCtx, const Node &child)
{
    EdgeInsets m =
        do_get_member<"margin">(soaCtx, child.ref).template miss_return(EdgeInsets{});
    return {child.geometry.w + m.horizontal(), child.geometry.h + m.vertical()};
}

/// 根据对齐方式计算偏移量（用于 Container 的对齐），考虑 border、padding 和 margin
static Offset alignmentOffset(const Alignment &align, float extraW, float extraH,
                              const EdgeInsets &pad, const EdgeInsets &margin,
                              const EdgeInsets &border)
{
    return {border.left + pad.left + margin.left + extraW * (align.x + 1.0f) / 2.0f,
            border.top + pad.top + margin.top + extraH * (align.y + 1.0f) / 2.0f};
}

/// 将子节点按对齐方式放置在容器内容区（如果 align 为空，默认使用 topLeft）
static void positionChildByAlignment(auto &soaCtx, Node &child,
                                     const Constraints &containerConstraints,
                                     const EdgeInsets &padding,
                                     const std::optional<Alignment> &align,
                                     const EdgeInsets &border)
{
    EdgeInsets childMargin =
        do_get_member<"margin">(soaCtx, child.ref).miss_return(EdgeInsets{});
    Size childFull = childFullSize(soaCtx, child);
    float contentW =
        containerConstraints.maxW - border.horizontal() - padding.horizontal();
    float contentH = containerConstraints.maxH - border.vertical() - padding.vertical();
    float extraW = std::max(0.0f, contentW - childFull.width);
    float extraH = std::max(0.0f, contentH - childFull.height);
    Alignment al = align.value_or(Align::topLeft);
    Offset offset = alignmentOffset(al, extraW, extraH, padding, childMargin, border);
    child.geometry.x = offset.x;
    child.geometry.y = offset.y;
}

struct ContainerStyleTrait
{
    static void layout(auto &soaCtx, auto &selfSoa, auto &node, Constraints borderBC,
                       const EdgeInsets &pad)
    {
        auto &ref = node.ref;
        auto [border, ownConstraints, width, height, align] =
            selfSoa.template view_entity<"border", "constraints", "width", "height",
                                         "alignment">(0, ref.idx);

        // 应用 Container 自身的约束（如果存在）
        if (ownConstraints)
        {
            borderBC = borderBC.intersect(*ownConstraints);
        }

        if (node.children.empty())
        {
            // 叶子 Container：宽高取固定值或 0，由约束收紧
            float w = width.value_or(0.0f);
            float h = height.value_or(0.0f);
            Size size = borderBC.clamp({w, h});
            node.geometry = BoxGeometry{0, 0, size.width, size.height};
            node.baseline = size.height; // 叶子容器 baseline 默认为底部
            return;
        }

        if (node.children.size() != 1)
            throw std::logic_error("Container '" + node.name +
                                   "' must have exactly one child.");

        Node &child = node.children[0];
        // 内容区约束 = 扣除 border 和 padding 后的剩余空间
        Constraints innerBC = borderBC.deflate(border).deflate(pad);

        // 有 alignment => 给子节点宽松约束，让子节点自由选择尺寸
        // 无 alignment => 传递紧约束，让子节点填满
        Constraints childBC = align.has_value() ? makeLooseConstraints(innerBC) : innerBC;
        childBC = childBC.deflate(
            do_get_member<"margin">(soaCtx, child.ref).miss_return(EdgeInsets{}));

        soaCtx.layout.template operator()(soaCtx, child, childBC);

        // 先计算贴合子节点的基准尺寸（先于固定宽高）
        Size childFull = childFullSize(soaCtx, child);
        float baseW = childFull.width + pad.horizontal() + border.horizontal();
        float baseH = childFull.height + pad.vertical() + border.vertical();

        // 再用固定宽高覆盖（若有），否则使用贴合后的基准尺寸
        float containerW = width.value_or(baseW);
        float containerH = height.value_or(baseH);
        Size containerSize = borderBC.clamp({containerW, containerH});

        // 定位子节点（若有 alignment 则对齐，否则默认左上），考虑 border
        Constraints containerAsConstraints{0, containerSize.width, 0,
                                           containerSize.height};
        positionChildByAlignment(soaCtx, child, containerAsConstraints, pad, align,
                                 border);

        node.geometry = BoxGeometry{0, 0, containerSize.width, containerSize.height};
        // 有子节点时，基线为子节点基线加上子节点在容器内的 y 偏移
        node.baseline = child.baseline + child.geometry.y;
    }
};
using ContainerStyleObject =
    gen_soa_struct<ContainerStyleTrait, {"width", ^^std::optional<float>},
                   {"height", ^^std::optional<float>}, {"margin", ^^EdgeInsets},
                   {"padding", ^^EdgeInsets}, {"border", ^^EdgeInsets},
                   {"alignment", ^^std::optional<Alignment>},
                   {"constraints", ^^std::optional<Constraints>}>;

// ============================================================================
// Flex 布局（Row / Column）：实现主轴/交叉轴对齐、Expanded、间隙等
// ============================================================================

/// 布局时每个子节点的临时信息
struct FlexChildInfo
{
    Node *node;        // 实际子节点（展开 Expanded 后为真实子节点）
    float flex;        // flex 值（0 表示非弹性）
    float marginMain;  // 主轴方向总 margin
    float marginCross; // 交叉轴方向总 margin
};

/// 收集 Flex 子节点，展开 Expanded，计算总 flex
static std::vector<FlexChildInfo> collectFlexChildren(auto &soaCtx, Node &node,
                                                      bool isRow, float &outTotalFlex)
{
    std::vector<FlexChildInfo> infos;
    outTotalFlex = 0.0f;
    for (auto &child : node.children)
    {
        constexpr auto type_id = std::decay_t<decltype(soaCtx)>::find_name("expandeds");
        static_assert(type_id != ~0);
        if (size_t(child.ref.kind) == type_id)
        {
            if (child.children.empty())
                throw std::logic_error("Expanded widget has no child.");
            Node &real = child.children[0];
            auto [flexVal] =
                soaCtx.expandeds.template view_entity<"flex">(0, child.ref.idx);
            EdgeInsets m =
                do_get_member<"margin">(soaCtx, real.ref).miss_return(EdgeInsets{});
            float mM = isRow ? m.horizontal() : m.vertical();
            float mC = isRow ? m.vertical() : m.horizontal();
            infos.push_back({&real, flexVal, mM, mC});
            outTotalFlex += flexVal;
        }
        else
        {
            EdgeInsets m =
                do_get_member<"margin">(soaCtx, child.ref).miss_return(EdgeInsets{});
            float mM = isRow ? m.horizontal() : m.vertical();
            float mC = isRow ? m.vertical() : m.horizontal();
            infos.push_back({&child, 0.0f, mM, mC});
        }
    }
    return infos;
}

/// 布局所有非弹性子节点，获取它们在主轴上的自然长度
/// 关键修改：对于非弹性子节点，主轴方向不施加任何最大约束（maxMain = inf），
/// 以完全符合 Flutter Row/Column “不对子节点施加约束”的语义。
/// 子节点将完全根据自身内容或固定尺寸决定大小。
static std::vector<float> layoutChildrenNaturally(auto &soaCtx,
                                                  const std::vector<FlexChildInfo> &infos,
                                                  bool isRow, bool isBoundedMain,
                                                  float innerMain, float innerCross,
                                                  CrossAxisAlignment crossAlign)
{
    std::vector<float> naturalMain;
    naturalMain.reserve(infos.size());
    for (const auto &info : infos)
    {
        // 弹性子节点暂不布局，自然长度记为 0（由后续分配）
        // 非弹性子节点：主轴最大尺寸为无穷大，即不施加约束
        float childMainMax = (info.flex > 0) ? 0.0f : Constraints::inf;
        float childCrossMax = innerCross;
        float childCrossMin =
            (crossAlign == CrossAxisAlignment::stretch) ? childCrossMax : 0.0f;

        Constraints childBC = makeFlexAxisConstraints(isRow, 0.0f, childMainMax,
                                                      childCrossMin, childCrossMax);
        if (info.flex == 0)
            childBC = childBC.deflate(
                EdgeInsets{info.marginMain, info.marginCross, 0.0f, 0.0f});

        soaCtx.layout.template operator()(soaCtx, *info.node, childBC);
        naturalMain.push_back(childMainAxisLength(*info.node, isRow));
    }
    return naturalMain;
}

/// 将剩余主轴空间按 flex 比例分配给弹性子节点
static void distributeFlexSpace(auto &soaCtx, const std::vector<FlexChildInfo> &infos,
                                std::vector<float> &naturalMain, bool isRow,
                                float freeMain, float totalFlex, float innerCross,
                                CrossAxisAlignment crossAlign)
{
    if (totalFlex <= 0 || freeMain <= 0)
        return;
    float flexUnit = freeMain / totalFlex;
    for (size_t i = 0; i < infos.size(); ++i)
    {
        if (infos[i].flex > 0)
        {
            float allocated = infos[i].flex * flexUnit;
            float childCrossMax = innerCross;
            float childCrossMin =
                (crossAlign == CrossAxisAlignment::stretch) ? childCrossMax : 0.0f;
            Constraints tightBC = makeFlexAxisConstraints(isRow, allocated, allocated,
                                                          childCrossMin, childCrossMax);
            soaCtx.layout.template operator()(soaCtx, *infos[i].node, tightBC);
            naturalMain[i] = childMainAxisLength(*infos[i].node, isRow);
        }
    }
}

/// 计算最终主轴内容区尺寸（根据 MainAxisSize）
static float computeFinalMainSize(bool isBoundedMain, float totalNatural, float innerMain,
                                  MainAxisSize axisSize)
{
    if (!isBoundedMain)
        return totalNatural;
    if (axisSize == MainAxisSize::min)
        return std::min(totalNatural, innerMain);
    return innerMain;
}

/// 计算主轴对齐时的间隙和起始偏移（对应 MainAxisAlignment）
/// 此偏移是基于主轴起始边（start 边）的逻辑偏移
static void computeMainGapAndStart(size_t childCount, float extraMain,
                                   MainAxisAlignment mainAlign, float &outMainGap,
                                   float &outMainStartOff)
{
    outMainGap = 0.0f;
    outMainStartOff = 0.0f;
    if (childCount == 1)
    {
        if (mainAlign == MainAxisAlignment::spaceAround ||
            mainAlign == MainAxisAlignment::spaceEvenly)
            outMainStartOff = extraMain / 2.0f;
    }
    else if (childCount > 1)
    {
        switch (mainAlign)
        {
        case MainAxisAlignment::start:
            outMainStartOff = 0.0f;
            break;
        case MainAxisAlignment::end:
            outMainStartOff = extraMain;
            break;
        case MainAxisAlignment::center:
            outMainStartOff = extraMain / 2.0f;
            break;
        case MainAxisAlignment::spaceBetween:
            outMainGap = extraMain / (childCount - 1);
            break;
        case MainAxisAlignment::spaceAround:
            outMainGap = extraMain / childCount;
            outMainStartOff = outMainGap / 2.0f;
            break;
        case MainAxisAlignment::spaceEvenly:
            outMainGap = extraMain / (childCount + 1);
            outMainStartOff = outMainGap;
            break;
        }
    }
}

/// 根据主轴间隙和交叉轴对齐设置每个子节点的位置
static void positionChildrenInFlex(auto &soaCtx, const std::vector<FlexChildInfo> &infos,
                                   const std::vector<float> &naturalMain,
                                   AxisDirection dir, bool isRow, float innerMain,
                                   float innerCross, float padMainStart, float padMainEnd,
                                   float padCrossStart, float padCrossEnd,
                                   CrossAxisAlignment crossAlign, float mainGap,
                                   float mainStartOff, float parentMainSize,
                                   float parentCrossSize)
{
    float mainPos = mainStartOff; // 逻辑主轴位置（从 start 边算起）
    // 基线对齐预处理
    float maxBaseline = 0;
    if (crossAlign == CrossAxisAlignment::baseline && isRow)
    {
        for (size_t i = 0; i < infos.size(); ++i)
        {
            float b = infos[i].node->baseline;
            if (b > maxBaseline)
                maxBaseline = b;
        }
    }

    for (size_t i = 0; i < infos.size(); ++i)
    {
        Node &child = *infos[i].node;
        EdgeInsets childMargin =
            do_get_member<"margin">(soaCtx, child.ref).miss_return(EdgeInsets{});
        float childMainLen = naturalMain[i];
        float childCrossLen = childCrossAxisLength(child, isRow);

        // 设置主轴位置（考虑方向）
        // 主轴位置逻辑偏移 = mainPos + 子节点的 leading margin
        float leadingMargin = isRow ? childMargin.left : childMargin.top;
        setChildMainAxisPosition(child, dir, mainPos + leadingMargin, childMainLen,
                                 parentMainSize, padMainStart, padMainEnd);

        // 设置交叉轴位置
        float crossStart = 0;
        float crossExtra =
            std::max(0.0f, innerCross - (childCrossLen + infos[i].marginCross));
        switch (crossAlign)
        {
        case CrossAxisAlignment::start:
            crossStart = 0;
            break;
        case CrossAxisAlignment::end:
            crossStart = crossExtra;
            break;
        case CrossAxisAlignment::center:
            crossStart = crossExtra / 2.0f;
            break;
        case CrossAxisAlignment::stretch:
            crossStart = 0;
            break;
        case CrossAxisAlignment::baseline:
            if (isRow)
            {
                // Row 的交叉轴是垂直方向，baseline 是 y 偏移
                crossStart = maxBaseline - child.baseline;
            }
            else
            {
                // Column 不支持 baseline，退化为 start
                crossStart = 0;
            }
            break;
        }
        // 交叉轴的 leading margin
        float crossLeadingMargin = isRow ? childMargin.top : childMargin.left;
        setChildCrossAxisPosition(child, dir, crossStart + crossLeadingMargin,
                                  childCrossLen, parentCrossSize, padCrossStart,
                                  padCrossEnd);

        // 更新主轴逻辑位置
        mainPos += naturalMain[i] + infos[i].marginMain + mainGap;
    }
}

/// 布局 Flex（Row/Column）的主函数
/// 步骤概览：
/// 1. 准备轴方向、约束边界，检查合法性
/// 2. 收集子节点信息（展开 Expanded）
/// 3. 布局非弹性子节点，获得自然主轴尺寸
/// 4. 计算所有子节点占用的总主轴长度
/// 5. 根据 MainAxisSize 决定最终内容区主轴尺寸，计算剩余空间
/// 6. 将剩余空间按 flex 分配给弹性子节点
/// 7. 重新计算实际占用主轴长度
/// 8. 计算主轴对齐的间隙和起始偏移
/// 9. 定位所有子节点
/// 10. 计算 Flex 自身尺寸
// 辅助函数：实现 Flex 布局的主体逻辑
template <bool isRow>
void layoutFlexImpl(auto &soaCtx, Node &node, Constraints borderBC, const EdgeInsets &pad,
                    AxisDirection dir, CrossAxisAlignment crossAlign,
                    MainAxisSize axisSize, MainAxisAlignment mainAlign,
                    std::optional<float> w, std::optional<float> h)
{
    // 原本 layoutFlex 的剩余代码全部移入此处（从变量 dir 开始）
    // 注意：以下所有代码与原 layoutFlex 函数体一致，只是不再重复获取 td/vd
    bool isBoundedMain = mainAxisIsBounded(borderBC, isRow);
    bool isBoundedCross = crossAxisIsBounded(borderBC, isRow);
    if (!isBoundedCross)
        throw std::logic_error("Row/Column '" + node.name +
                               "' has unbounded cross axis.");

    if (!isBoundedMain)
    {
        for (const auto &child : node.children)
            if (child.ref.kind == WidgetKind::Expanded)
                throw std::logic_error("Row/Column '" + node.name +
                                       "' has unbounded main axis and Expanded child.");
    }

    float mainSize = mainAxisSizeFromConstraints(borderBC, isRow);
    float crossSize = crossAxisSizeFromConstraints(borderBC, isRow);

    float padMainStart = mainAxisPaddingStart(pad, dir);
    float padMainEnd = mainAxisPaddingEnd(pad, dir);
    float padCrossStart = crossAxisPaddingStart(pad, dir);
    float padCrossEnd = crossAxisPaddingEnd(pad, dir);

    float innerMain = mainSize - padMainStart - padMainEnd;
    if (!isBoundedMain)
        innerMain = Constraints::inf;
    float innerCross = crossSize - padCrossStart - padCrossEnd;

    float totalFlex = 0;
    auto infos = collectFlexChildren(soaCtx, node, isRow, totalFlex);

    auto naturalMain = layoutChildrenNaturally(soaCtx, infos, isRow, isBoundedMain,
                                               innerMain, innerCross, crossAlign);

    float totalNatural = 0.0f;
    for (size_t i = 0; i < infos.size(); ++i)
        totalNatural += naturalMain[i] + infos[i].marginMain;

    float finalInnerMain =
        computeFinalMainSize(isBoundedMain, totalNatural, innerMain, axisSize);
    float freeMain = std::max(0.0f, finalInnerMain - totalNatural);

    distributeFlexSpace(soaCtx, infos, naturalMain, isRow, freeMain, totalFlex,
                        innerCross, crossAlign);

    float totalMainUsed = 0.0f;
    for (size_t i = 0; i < infos.size(); ++i)
        totalMainUsed += naturalMain[i] + infos[i].marginMain;

    float extraMain = std::max(0.0f, finalInnerMain - totalMainUsed);
    float mainGap = 0.0f, mainStartOff = 0.0f;
    computeMainGapAndStart(infos.size(), extraMain, mainAlign, mainGap, mainStartOff);

    positionChildrenInFlex(soaCtx, infos, naturalMain, dir, isRow, innerMain, innerCross,
                           padMainStart, padMainEnd, padCrossStart, padCrossEnd,
                           crossAlign, mainGap, mainStartOff, mainSize, crossSize);

    Size size;
    if (isRow)
    {
        size.width = finalInnerMain + pad.horizontal();
        size.height = innerCross + pad.vertical();
    }
    else
    {
        size.width = innerCross + pad.horizontal();
        size.height = finalInnerMain + pad.vertical();
    }
    if (w)
        size.width = *w;
    if (h)
        size.height = *h;
    size = borderBC.clamp(size);

    node.geometry = BoxGeometry{0, 0, size.width, size.height};
    if (!infos.empty())
    {
        Node &first = *infos[0].node;
        if (isRow)
            node.baseline = first.baseline + first.geometry.y;
        else
            node.baseline = first.baseline + first.geometry.x;
    }
    else
    {
        node.baseline = isRow ? size.height : size.width;
    }
}

struct RowStyleTrait
{
    static void layout(auto &soaCtx, auto &selfSoa, Node &node, Constraints borderBC,
                       const EdgeInsets &pad)
    {
        auto &ref = node.ref;
        auto [td, crossAlign, axisSize, mainAlign, w, h] =
            selfSoa.template view_entity<"textDirection", "crossAlign", "mainAxisSize",
                                         "mainAlign", "width", "height">(0, ref.idx);
        AxisDirection dir = axisDirectionForFlex(true, td, VerticalDirection::down);
        layoutFlexImpl<true>(soaCtx, node, borderBC, pad, dir, crossAlign, axisSize,
                             mainAlign, w, h);
    }
};
using RowStyleObject =
    gen_soa_struct<RowStyleTrait, {"width", ^^std::optional<float>},
                   {"height", ^^std::optional<float>}, {"margin", ^^EdgeInsets},
                   {"padding", ^^EdgeInsets}, {"mainAlign", ^^MainAxisAlignment},
                   {"crossAlign", ^^CrossAxisAlignment}, {"mainAxisSize", ^^MainAxisSize},
                   {"textDirection", ^^TextDirection}>;

struct ColumnStyleTrait
{
    static void layout(auto &soaCtx, auto &selfSoa, Node &node, Constraints borderBC,
                       const EdgeInsets &pad)
    {
        auto &ref = node.ref;
        auto [vd, crossAlign, axisSize, mainAlign, w, h] =
            selfSoa.template view_entity<"verticalDirection", "crossAlign",
                                         "mainAxisSize", "mainAlign", "width", "height">(
                0, ref.idx);
        AxisDirection dir = axisDirectionForFlex(false, TextDirection::ltr, vd);
        layoutFlexImpl<false>(soaCtx, node, borderBC, pad, dir, crossAlign, axisSize,
                              mainAlign, w, h);
    }
};
using ColumnStyleObject =
    gen_soa_struct<ColumnStyleTrait, {"width", ^^std::optional<float>},
                   {"height", ^^std::optional<float>}, {"margin", ^^EdgeInsets},
                   {"padding", ^^EdgeInsets}, {"mainAlign", ^^MainAxisAlignment},
                   {"crossAlign", ^^CrossAxisAlignment}, {"mainAxisSize", ^^MainAxisSize},
                   {"verticalDirection", ^^VerticalDirection}>;

struct ExpandedStyleTrait
{
};
using ExpandedStyleObject = gen_soa_struct<ExpandedStyleTrait, {"flex", ^^int}>;

// 全局文本测量函数指针，测试前设置
using MeasureFunc = MeasuredSize (*)(const WidgetRef &, Constraints);
MeasureFunc g_textMeasure = nullptr;
struct TextStyleTrait
{
    static constexpr MeasuredSize measure(auto &soaCtx, auto &selfSoa, auto &node,
                                          Constraints bc)
    {
        if (g_textMeasure)
            return g_textMeasure(node.ref, bc);
        // 默认返回空尺寸（无测量函数时）
        return {{0, 0}, 0};
    }
    constexpr static void layout(auto &soaCtx, auto &selfSoa, auto &node,
                                 Constraints borderBC, const EdgeInsets &pad)
    {
        (void)pad; // 当前未使用 padding，但保留接口
        auto &ref = node.ref;
        if (!node.children.empty())
            throw std::logic_error("Text widget '" + node.name +
                                   "' cannot have children.");

        // NOTE: 让 g_measureFuncs 作为 对象成员属性? 目前是类属性
        auto [width, height] =
            selfSoa.template view_entity<"width", "height">(0, ref.idx);

        MeasuredSize measured = measure(soaCtx, selfSoa, node, borderBC);
        float w = width.value_or(measured.size.width);
        float h = height.value_or(measured.size.height);
        Size size = borderBC.clamp({w, h});
        node.geometry = BoxGeometry{0, 0, size.width, size.height};

        // 基线：若测量函数提供了有效值则使用，否则基于字体度量估算（默认 ~0.75 * 高度）
        float baselineFromMetrics =
            measured.baseline > 0 ? measured.baseline : size.height * 0.75f;
        node.baseline = std::min(baselineFromMetrics, size.height);
    }
};
using TextStyleObject =
    gen_soa_struct<TextStyleTrait, {"width", ^^std::optional<float>},
                   {"height", ^^std::optional<float>}, {"margin", ^^EdgeInsets},
                   {"padding", ^^EdgeInsets}>;

constexpr auto initSoaData()
{
    ContainerStyleObject containers{100};
    RowStyleObject rows{100};
    ColumnStyleObject columns{100};
    ExpandedStyleObject expandeds{100};
    TextStyleObject texts{100};

    constexpr auto get_member =
        []<static_string member_name>(auto &soaCtx, const WidgetRef &ref) -> RefAny {
        constexpr auto members = std::remove_cvref_t<decltype(soaCtx)>::members;
        template for (constexpr auto I : std::ranges::views::indices(members.size()))
        {
            using MemberType = std::remove_cvref_t<decltype(soaCtx.[:members[I]:])>;

            // 只有真正的 SOA 结构体才有 trait_type
            if constexpr (requires { typename MemberType::trait_type; })
            {
                if constexpr (MemberType::has_member(member_name))
                {
                    if (I == (size_t)(ref.kind))
                    {
                        auto [m] = soaCtx.[:members[I]:]
                            .template view_entity<member_name>(0, ref.idx);
                        using Ref = decltype(m);
                        static_assert(std::is_reference_v<Ref>, "should reference type");
                        return RefAny{m};
                    }
                }
            }
        }
        return RefAny{};
    };
    constexpr auto layout = [](auto &soaCtx, Node &node, Constraints constraints) {
        const auto &ref = node.ref;
        if (ref.kind == WidgetKind::Expanded)
            throw std::logic_error(
                "Expanded widget must be placed directly inside Row/Column/Flex.");

        Constraints borderBC = constraints;

        // 如果 Widget 自身有固定宽高，收紧约束
        if (auto w = do_get_member<"width">(soaCtx, ref).value_or(std::optional<float>{}))
            borderBC = borderBC.applyFixedWidth(*w);
        if (auto h =
                do_get_member<"height">(soaCtx, ref).value_or(std::optional<float>{}))
            borderBC = borderBC.applyFixedHeight(*h);

        // 对于 Container，还要合并自身的 constraints
        if (auto ownConstraints = do_get_member<"constraints">(soaCtx, ref)
                                      .value_or(std::optional<Constraints>{}))
        {
            borderBC = borderBC.intersect(*ownConstraints);
        }

        constexpr auto members = std::remove_cvref_t<decltype(soaCtx)>::members;
        // 叶子节点且存在测量函数：使用自然尺寸作为默认宽高
        if (node.children.empty() &&
            (!do_get_member<"width">(soaCtx, ref).value_or(std::optional<float>{}) ||
             !do_get_member<"height">(soaCtx, ref).value_or(std::optional<float>{})))
        {
            // bool dispatch{};
            template for (constexpr auto I : std::ranges::views::indices(members.size()))
            {
                using MemberType = std::remove_cvref_t<decltype(soaCtx.[:members[I]:])>;
                if constexpr (requires {
                                  typename MemberType::trait_type;
                                  {
                                      MemberType::trait_type::template measure(
                                          soaCtx, soaCtx.[:members[I]:], node, borderBC)
                                  } -> std::same_as<MeasuredSize>;
                              })
                {
                    if (size_t(ref.kind) == I)
                    {
                        MeasuredSize natural = MemberType::trait_type::template measure(
                            soaCtx, soaCtx.[:members[I]:], node, borderBC);
                        if (!do_get_member<"width">(soaCtx, ref)
                                 .value_or(std::optional<float>{}))
                            borderBC = borderBC.applyFixedWidth(natural.size.width);
                        if (!do_get_member<"height">(soaCtx, ref)
                                 .value_or(std::optional<float>{}))
                            borderBC = borderBC.applyFixedHeight(natural.size.height);
                        // dispatch = true;
                    }
                }
            }
            // NOTE: 有测量函数就利用它，没有就忽略
            // if (not dispatch)
            //     throw std::logic_error("Unhandled WidgetKind in measure");
        }

        EdgeInsets padding =
            do_get_member<"padding">(soaCtx, ref).template miss_return(EdgeInsets{});

        bool dispatch{};
        template for (constexpr auto I : std::ranges::views::indices(members.size()))
        {
            using MemberType = std::remove_cvref_t<decltype(soaCtx.[:members[I]:])>;
            if constexpr (requires {
                              typename MemberType::trait_type;
                              MemberType::trait_type::template layout(
                                  soaCtx, soaCtx.[:members[I]:], node, borderBC, padding);
                          })
            {
                if (size_t(ref.kind) == I)
                {
                    MemberType::trait_type::template layout(
                        soaCtx, soaCtx.[:members[I]:], node, borderBC, padding);
                    dispatch = true;
                }
            }
        }
        if (not dispatch)
            throw std::logic_error("Unhandled WidgetKind in layout");

        // 溢出检测（若最终尺寸超出父约束则警告）
        Size finalSize{node.geometry.w, node.geometry.h};
        if (finalSize.width > constraints.maxW + 1e-6f ||
            finalSize.height > constraints.maxH + 1e-6f)
        {
            std::cerr << "WARNING: Widget '" << node.name << "' overflows parent by ("
                      << finalSize.width - constraints.maxW << ", "
                      << finalSize.height - constraints.maxH << ")\n";
        }
    };

    auto soaCtx = make_aggregate<"soaData", "containers", "rows", "columns", "expandeds",
                                 "texts", "get_member", "layout">(
        std::move(containers), std::move(rows), std::move(columns), std::move(expandeds),
        std::move(texts), std::move(get_member), std::move(layout));

    assert(bool(soaCtx.containers.capacity() == 100));
    assert(bool(soaCtx.rows.capacity() == 100));
    assert(bool(soaCtx.columns.capacity() == 100));
    assert(bool(soaCtx.expandeds.capacity() == 100));
    assert(bool(soaCtx.texts.capacity() == 100));
    return soaCtx;
}
auto soaCtx = initSoaData();

// ============================================================================
// 访问器：从 WidgetRef 获取样式属性
// ============================================================================

// NOTE: 返回值统一是不可能的
// NOTE: 先访问类型，再访问成员。 成员类型很不同和解？
template <static_string member_name>
constexpr auto get_member_impl(auto &soaCtx, const WidgetRef &ref) -> RefAny
{
    constexpr auto members = std::remove_cvref_t<decltype(soaCtx)>::members;
    template for (constexpr auto I : std::ranges::views::indices(members.size()))
    {
        using MemberType = std::remove_cvref_t<decltype(soaCtx.[:members[I]:])>;

        // 只有真正的 SOA 结构体才有 trait_type
        if constexpr (requires { typename MemberType::trait_type; })
        {
            if constexpr (MemberType::has_member(member_name))
            {
                if (I == (size_t)(ref.kind))
                {
                    auto [m] = soaCtx.[:members[I]:].template view_entity<member_name>(
                                                        0, ref.idx);
                    using Ref = decltype(m);
                    static_assert(std::is_reference_v<Ref>, "should reference type");
                    return RefAny{m};
                }
            }
        }
    }
    return RefAny{};
}
template <static_string member_name>
constexpr auto get_member(const WidgetRef &ref)
{
    return get_member_impl<member_name>(soaCtx, ref);
}
static_assert([]() constexpr {
    auto soa = initSoaData();
    std::optional<float> width = std::nullopt;
    std::optional<float> height = std::nullopt;
    EdgeInsets margin = {};
    EdgeInsets padding = {};
    EdgeInsets border = {};
    std::optional<Alignment> alignment = std::nullopt;
    std::optional<Constraints> constraints = std::nullopt;

    auto index = soa.containers.new_entity(width, height, margin, padding, border,
                                           alignment, constraints);

    return true;
}());

/// 测量函数类型：用于 Text 等需要自然尺寸的叶子 Widget

void clearPools()
{
    g_textMeasure = nullptr;
    soaCtx = initSoaData();
    assert(bool(soaCtx.containers.capacity() == 100));
    assert(bool(soaCtx.rows.capacity() == 100));
    assert(bool(soaCtx.columns.capacity() == 100));
    assert(bool(soaCtx.expandeds.capacity() == 100));
    assert(bool(soaCtx.texts.capacity() == 100));
}

// ============================================================================
// 工厂函数：创建 WidgetRef，直接存入 soaCtx 对应的 SOA 结构中
// ============================================================================

WidgetRef makeContainer(std::optional<float> width = std::nullopt,
                        std::optional<float> height = std::nullopt,
                        EdgeInsets margin = {}, EdgeInsets padding = {},
                        EdgeInsets border = {},
                        std::optional<Alignment> alignment = std::nullopt,
                        std::optional<Constraints> constraints = std::nullopt)
{
    uint32_t idx = soaCtx.containers.new_entity(width, height, margin, padding, border,
                                                alignment, constraints);
    return {WidgetKind::Container, idx};
}

// 注意参数顺序已调整为与 RowStyleObject 的字段顺序一致：
// width, height, margin, padding, mainAlign, crossAlign, mainAxisSize, textDirection
WidgetRef makeRow(std::optional<float> width = std::nullopt,
                  std::optional<float> height = std::nullopt, EdgeInsets margin = {},
                  EdgeInsets padding = {},
                  MainAxisAlignment mainAlign = MainAxisAlignment::start,
                  CrossAxisAlignment crossAlign = CrossAxisAlignment::start,
                  MainAxisSize mainAxisSize = MainAxisSize::max,
                  TextDirection textDirection = TextDirection::ltr)
{
    uint32_t idx = soaCtx.rows.new_entity(width, height, margin, padding, mainAlign,
                                          crossAlign, mainAxisSize, textDirection);
    return {WidgetKind::Row, idx};
}

// Column 参数顺序：width, height, margin, padding, mainAlign, crossAlign, mainAxisSize, verticalDirection
WidgetRef makeColumn(std::optional<float> width = std::nullopt,
                     std::optional<float> height = std::nullopt, EdgeInsets margin = {},
                     EdgeInsets padding = {},
                     MainAxisAlignment mainAlign = MainAxisAlignment::start,
                     CrossAxisAlignment crossAlign = CrossAxisAlignment::start,
                     MainAxisSize mainAxisSize = MainAxisSize::max,
                     VerticalDirection verticalDirection = VerticalDirection::down)
{
    uint32_t idx = soaCtx.columns.new_entity(width, height, margin, padding, mainAlign,
                                             crossAlign, mainAxisSize, verticalDirection);
    return {WidgetKind::Column, idx};
}

WidgetRef makeExpanded(int flex = 1)
{
    uint32_t idx = soaCtx.expandeds.new_entity(flex);
    return {WidgetKind::Expanded, idx};
}

WidgetRef makeText(std::optional<float> w = std::nullopt,
                   std::optional<float> h = std::nullopt, EdgeInsets m = {},
                   EdgeInsets p = {})
{
    uint32_t idx = soaCtx.texts.new_entity(w, h, m, p);
    return {WidgetKind::Text, idx};
}

bool approx(float a, float b, float eps = 1e-3f)
{
    return std::abs(a - b) < eps;
}

bool verifyGeometry(const Node &node, float x, float y, float w, float h)
{
    return approx(node.geometry.x, x) && approx(node.geometry.y, y) &&
           approx(node.geometry.w, w) && approx(node.geometry.h, h);
}

bool checkGeometry(const Node &node, float x, float y, float w, float h,
                   const char *label)
{
    if (!verifyGeometry(node, x, y, w, h))
    {
        std::cerr << "FAIL: " << label << " expected (" << x << "," << y << "," << w
                  << "," << h << ") got (" << node.geometry.x << "," << node.geometry.y
                  << "," << node.geometry.w << "," << node.geometry.h << ")\n";
        return false;
    }
    return true;
}

bool tryLayout(Node &node, Constraints c, const std::string &expectedError = "")
{
    try
    {
        c = c.deflate(
            do_get_member<"margin">(soaCtx, node.ref)
                .template miss_return(EdgeInsets{})); // 根节点 margin 由父级模拟扣除

        soaCtx.layout.template operator()(soaCtx, node, c);

        if (!expectedError.empty())
        {
            std::cerr << "FAIL: expected exception but none thrown\n";
            return false;
        }
        return true;
    }
    catch (const std::logic_error &e)
    {
        if (expectedError.empty())
        {
            std::cerr << "FAIL: unexpected exception: " << e.what() << "\n";
            return false;
        }
        if (std::string(e.what()).find(expectedError) == std::string::npos)
        {
            std::cerr << "FAIL: expected '" << expectedError << "' but got '" << e.what()
                      << "'\n";
            return false;
        }
        return true;
    }
}

/// 屏幕几何信息（用于 UI 实例生成）
struct ScreenGeometry
{
    float x, y, w, h;
    float padL, padR, padT, padB;
    float borderL, borderR, borderT, borderB;
};

/// 深度优先遍历引擎布局树，提取每个节点的几何 + padding + border
static std::vector<ScreenGeometry> extractScreenGeometries(const Node &root)
{
    std::vector<ScreenGeometry> geos;
    auto dfs = [&](this auto &self, const Node &node, float parentX, float parentY) {
        float x = parentX + node.geometry.x;
        float y = parentY + node.geometry.y;
        float w = node.geometry.w;
        float h = node.geometry.h;
        if (node.ref.kind == WidgetKind::Container)
        {
            auto [pad, border] =
                soaCtx.containers.template view_entity<"padding", "border">(0,
                                                                            node.ref.idx);
            geos.push_back({x, y, w, h, pad.left, pad.right, pad.top, pad.bottom,
                            border.left, border.right, border.top, border.bottom});
        }
        if (node.children.empty())
            return;
        for (const auto &child : node.children)
            self(child, x, y);
    };
    dfs(root, 0.0f, 0.0f);
    return geos;
}
bool test_specific_layout_equivalence()
{
    clearPools();

    Node screen{makeContainer(800.0f, 600.0f, {}, {}), {}, {}, "screen"};

    Node root{makeContainer(200.0f, 200.0f, {}, {10, 10, 10, 10}, {}, Align::topLeft),
              {},
              {},
              "root"};
    Node child0{makeContainer(100.0f, 100.0f, {5, 5, 5, 5}, {}, {}, Align::topLeft),
                {},
                {},
                "child0"};
    Node child0_0{makeContainer(50.0f, 50.0f, {5, 5, 5, 5}, {}), {}, {}, "child0-0"};
    child0.children.push_back(std::move(child0_0));
    Node node1_in_root{makeContainer(100.0f, 20.0f, {5, 5, 5, 5}, {}), {}, {}, "1"};

    // 修改后的 makeColumn 调用（注意参数顺序）
    Node root_column{makeColumn(std::nullopt, std::nullopt, {}, {},
                                MainAxisAlignment::start, CrossAxisAlignment::start,
                                MainAxisSize::max),
                     {},
                     {},
                     "root_column"};
    root_column.children.push_back(std::move(child0));
    root_column.children.push_back(std::move(node1_in_root));
    root.children.push_back(std::move(root_column));

    Node root2{makeContainer(200.0f, 200.0f, {}, {10, 10, 10, 10}, {}, Align::topLeft),
               {},
               {},
               "root2"};
    Node node1_in_root2{makeContainer(100.0f, 20.0f, {5, 5, 5, 5}, {}), {}, {}, "1"};
    Node root2_column{makeColumn(std::nullopt, std::nullopt, {}, {},
                                 MainAxisAlignment::start, CrossAxisAlignment::start,
                                 MainAxisSize::max),
                      {},
                      {},
                      "root2_column"};
    root2_column.children.push_back(std::move(node1_in_root2));
    root2.children.push_back(std::move(root2_column));

    Node text_display{
        makeContainer(200.0f, 200.0f, {}, {10, 10, 10, 10}), {}, {}, "text_display"};

    Node screen_column{makeColumn(std::nullopt, std::nullopt, {}, {},
                                  MainAxisAlignment::start, CrossAxisAlignment::start,
                                  MainAxisSize::max),
                       {},
                       {},
                       "screen_column"};
    screen_column.children.push_back(std::move(root));
    screen_column.children.push_back(std::move(root2));
    screen_column.children.push_back(std::move(text_display));
    screen.children.push_back(std::move(screen_column));

    if (!tryLayout(screen, {0, 800, 0, 600}))
        return false;

    auto geos = extractScreenGeometries(screen);

    struct Expected
    {
        std::string name;
        float x, y, w, h;
        float padL, padR, padT, padB;
        float borderL, borderR, borderT, borderB;
    };
    std::vector<Expected> expected = {
        {"screen", 0, 0, 800, 600, 0, 0, 0, 0, 0, 0, 0, 0},
        {"root", 0, 0, 200, 200, 10, 10, 10, 10, 0, 0, 0, 0},
        {"child0", 15, 15, 100, 100, 0, 0, 0, 0, 0, 0, 0, 0},
        {"child0-0", 20, 20, 50, 50, 0, 0, 0, 0, 0, 0, 0, 0},
        {"1", 15, 125, 100, 20, 0, 0, 0, 0, 0, 0, 0, 0},
        {"root2", 0, 200, 200, 200, 10, 10, 10, 10, 0, 0, 0, 0},
        {"1", 15, 215, 100, 20, 0, 0, 0, 0, 0, 0, 0, 0},
        {"text_display", 0, 400, 200, 200, 10, 10, 10, 10, 0, 0, 0, 0}};

    if (geos.size() != expected.size())
    {
        std::cerr << "FAIL: geometry count mismatch: got " << geos.size() << ", expected "
                  << expected.size() << "\n";
        return false;
    }

    for (size_t i = 0; i < geos.size(); ++i)
    {
        const auto &g = geos[i];
        const auto &e = expected[i];
        if (!approx(g.x, e.x) || !approx(g.y, e.y) || !approx(g.w, e.w) ||
            !approx(g.h, e.h) || !approx(g.padL, e.padL) || !approx(g.padR, e.padR) ||
            !approx(g.padT, e.padT) || !approx(g.padB, e.padB) ||
            !approx(g.borderL, e.borderL) || !approx(g.borderR, e.borderR) ||
            !approx(g.borderT, e.borderT) || !approx(g.borderB, e.borderB))
        {
            std::cerr << "FAIL: geometry mismatch at index " << i
                      << " (expected name: " << e.name << ")\n";
            std::cerr << "  expected: (" << e.x << "," << e.y << "," << e.w << "," << e.h
                      << ") pad(" << e.padL << "," << e.padR << "," << e.padT << ","
                      << e.padB << ") border(" << e.borderL << "," << e.borderR << ","
                      << e.borderT << "," << e.borderB << ")\n";
            std::cerr << "  got:      (" << g.x << "," << g.y << "," << g.w << "," << g.h
                      << ") pad(" << g.padL << "," << g.padR << "," << g.padT << ","
                      << g.padB << ") border(" << g.borderL << "," << g.borderR << ","
                      << g.borderT << "," << g.borderB << ")\n";
            return false;
        }
    }
    return true;
}

// ============================================================================
// 移植的测试用例（共24个，按原顺序）
// ============================================================================

/// 测试1：父容器固定尺寸且显式 alignment，子容器保持固定尺寸并带 margin。
bool test_parent_constraints_win_over_fixed_child()
{
    clearPools();
    Node parent = {
        makeContainer(100.0f, 100.0f, {}, {}, {}, Align::topLeft), {}, {}, "parent"};
    Node child = {makeContainer(50.0f, 50.0f, {5, 5, 5, 5}), {}, {}, "child"};
    parent.children.push_back(std::move(child));
    if (!tryLayout(parent, {0, 800, 0, 600}))
        return false;
    return checkGeometry(parent, 0, 0, 100, 100, "parent") &&
           checkGeometry(parent.children[0], 5, 5, 50, 50, "child fixed");
}

/// 测试2：Row 中放置固定尺寸子节点，子节点保持自身大小。
bool test_fixed_size_child_inside_row()
{
    clearPools();
    Node row = {makeRow(300.0f, 100.0f), {}, {}, "row"};
    row.children.push_back({makeContainer(50.0f, 30.0f), {}, {}, "c1"});
    if (!tryLayout(row, {0, 500, 0, 200}))
        return false;
    return checkGeometry(row, 0, 0, 300, 100, "row") &&
           checkGeometry(row.children[0], 0, 0, 50, 30, "c1");
}

/// 测试3：Expanded 按 flex 比例分配剩余主轴空间。
bool test_expanded_distributes_space_by_flex()
{
    clearPools();
    Node row = {makeRow(300.0f, 100.0f), {}, {}, "row"};
    Node exp = {makeExpanded(2), {}, {}, "exp"};
    Node c2 = {makeContainer(std::nullopt, 20.0f), {}, {}, "c2"};
    exp.children.push_back(std::move(c2));
    row.children.push_back({makeContainer(50.0f, 30.0f), {}, {}, "c1"});
    row.children.push_back(std::move(exp));
    row.children.push_back({makeContainer(70.0f, 40.0f), {}, {}, "c3"});
    if (!tryLayout(row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(row, 0, 0, 300, 100, "row") &&
           checkGeometry(row.children[0], 0, 0, 50, 30, "c1") &&
           checkGeometry(row.children[1].children[0], 50, 0, 180, 20, "c2 expanded") &&
           checkGeometry(row.children[2], 230, 0, 70, 40, "c3");
}

/// 测试4：CrossAxisAlignment::stretch 覆盖子节点固定高度，拉伸至 Row 高度。
bool test_stretch_overrides_fixed_height()
{
    clearPools();
    Node row = {makeRow(300.0f, 100.0f, {}, {}, MainAxisAlignment::start,
                        CrossAxisAlignment::stretch),
                {},
                {},
                "row"};
    row.children.push_back({makeContainer(50.0f, std::nullopt), {}, {}, "c1"});
    row.children.push_back({makeContainer(70.0f, 40.0f), {}, {}, "c2"});
    if (!tryLayout(row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(row.children[0], 0, 0, 50, 100, "c1 stretched") &&
           checkGeometry(row.children[1], 50, 0, 70, 100, "c2 forced");
}

/// 测试5：MainAxisSize::min 使 Column 高度收缩到子节点总高度。
bool test_main_axis_min_shrinks_column()
{
    clearPools();
    Node col = {makeColumn(200.0f, std::nullopt, {}, {}, MainAxisAlignment::start,
                           CrossAxisAlignment::start, MainAxisSize::min),
                {},
                {},
                "col"};
    col.children.push_back({makeContainer(100.0f, 30.0f), {}, {}, "c1"});
    col.children.push_back({makeContainer(80.0f, 20.0f), {}, {}, "c2"});
    if (!tryLayout(col, {0, 200, 0, Constraints::inf}))
        return false;
    return approx(col.geometry.h, 50);
}

/// 测试6：Text 使用注册的测量函数获得自然尺寸，并正确应用 margin。
bool test_text_uses_measure_func_and_margin()
{
    clearPools();
    g_textMeasure = [](const WidgetRef &, Constraints) -> MeasuredSize {
        return {{120, 25}, 20}; // baseline = 20
    };
    Node col = {makeColumn(300.0f, 200.0f), {}, {}, "col"};
    col.children.push_back(
        {makeText(std::nullopt, std::nullopt, {5, 5, 5, 5}), {}, {}, "text"});
    if (!tryLayout(col, {0, 300, 0, 200}))
        return false;
    return checkGeometry(col.children[0], 5, 5, 120, 25, "text");
}

/// 测试7：Container 无固定尺寸且无 alignment 时，在紧约束下填满父容器。
bool test_container_shrink_to_child()
{
    clearPools();
    Node parent = {makeContainer(200.0f, 200.0f), {}, {}, "parent"};
    Node child = {
        makeContainer(std::nullopt, std::nullopt, {10, 10, 10, 10}), {}, {}, "child"};
    child.children.push_back({makeContainer(50.0f, 30.0f), {}, {}, "inner"});
    parent.children.push_back(std::move(child));
    if (!tryLayout(parent, {0, 200, 0, 200}))
        return false;
    return checkGeometry(parent.children[0], 10, 10, 180, 180, "child fills parent") &&
           checkGeometry(parent.children[0].children[0], 0, 0, 180, 180,
                         "inner fills child");
}

/// 测试8：叶子 Container 在无界宽度下收缩为 0 尺寸。
bool test_unbounded_container_shrinks_to_zero()
{
    clearPools();
    Node c = {makeContainer(), {}, {}, "c"};
    if (!tryLayout(c, {0, Constraints::inf, 0, 100}))
        return false;
    return checkGeometry(c, 0, 0, 0, 0, "c unbounded becomes zero");
}

/// 测试9：Expanded 放在非 Flex 父节点中抛出异常。
bool test_expanded_outside_flex_throws()
{
    clearPools();
    Node cont = {makeContainer(200.0f, 200.0f), {}, {}, "cont"};
    Node exp = {makeExpanded(), {}, {}, "exp"};
    exp.children.push_back({makeContainer(50.0f, 50.0f), {}, {}, "inner"});
    cont.children.push_back(std::move(exp));
    return tryLayout(cont, {0, 200, 0, 200}, "Expanded widget");
}

/// 测试10：Container 带 padding 且无 alignment 时，填满父容器，子节点受 padding 影响。
bool test_container_with_padding_shrinks()
{
    clearPools();
    Node parent = {makeContainer(200.0f, 200.0f), {}, {}, "parent"};
    Node child = {
        makeContainer(std::nullopt, std::nullopt, {}, {10, 10, 10, 10}), {}, {}, "child"};
    child.children.push_back({makeContainer(50.0f, 30.0f), {}, {}, "inner"});
    parent.children.push_back(std::move(child));
    if (!tryLayout(parent, {0, 200, 0, 200}))
        return false;
    return checkGeometry(parent.children[0], 0, 0, 200, 200, "child fills parent") &&
           checkGeometry(parent.children[0].children[0], 10, 10, 180, 180,
                         "inner fills child");
}

/// 测试11：嵌套 Container 无 alignment 且紧约束下均被拉伸。
bool test_nested_container_shrink()
{
    clearPools();
    Node outer = {makeContainer(200.0f, 200.0f), {}, {}, "outer"};
    Node mid = {
        makeContainer(std::nullopt, std::nullopt, {}, {5, 5, 5, 5}), {}, {}, "mid"};
    mid.children.push_back({makeContainer(30.0f, 20.0f), {}, {}, "inner"});
    outer.children.push_back(std::move(mid));
    if (!tryLayout(outer, {0, 200, 0, 200}))
        return false;
    return checkGeometry(outer.children[0], 0, 0, 200, 200, "mid fills outer") &&
           checkGeometry(outer.children[0].children[0], 5, 5, 190, 190,
                         "inner fills mid");
}

/// 测试12：Row 的 stretch 只影响交叉轴（高度），不影响主轴（宽度）。
bool test_row_stretch_only_affects_height()
{
    clearPools();
    Node row = {makeRow(300.0f, 100.0f, {}, {}, MainAxisAlignment::start,
                        CrossAxisAlignment::stretch),
                {},
                {},
                "row"};
    row.children.push_back({makeContainer(50.0f, std::nullopt), {}, {}, "c1"});
    row.children.push_back({makeContainer(70.0f, 40.0f), {}, {}, "c2"});
    if (!tryLayout(row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(row.children[0], 0, 0, 50, 100, "c1 width fixed") &&
           checkGeometry(row.children[1], 50, 0, 70, 100, "c2 width fixed");
}

/// 测试13：Column 的 spaceEvenly 对齐方式正确分布空间。
bool test_column_space_evenly_distribution()
{
    clearPools();
    Node col = {makeColumn(100.0f, 200.0f, {}, {}, MainAxisAlignment::spaceEvenly),
                {},
                {},
                "col"};
    col.children.push_back({makeContainer(50.0f, 30.0f), {}, {}, "c1"});
    col.children.push_back({makeContainer(50.0f, 30.0f), {}, {}, "c2"});
    col.children.push_back({makeContainer(50.0f, 30.0f), {}, {}, "c3"});
    if (!tryLayout(col, {0, 100, 0, 200}))
        return false;
    return checkGeometry(col.children[0], 0, 27.5, 50, 30, "c1 at 27.5") &&
           checkGeometry(col.children[1], 0, 85, 50, 30, "c2 at 85") &&
           checkGeometry(col.children[2], 0, 142.5, 50, 30, "c3 at 142.5");
}

/// 测试14：父容器约束比子节点固定尺寸更紧，子节点被截断。
bool test_parent_tighter_than_fixed_override()
{
    clearPools();
    Node parent = {makeContainer(50.0f, 50.0f), {}, {}, "parent"};
    parent.children.push_back({makeContainer(100.0f, 100.0f), {}, {}, "child"});
    if (!tryLayout(parent, {0, 200, 0, 200}))
        return false;
    return checkGeometry(parent.children[0], 0, 0, 50, 50, "child forced to 50x50");
}

/// 测试15：Text 在无界宽度下使用测量函数的自然宽度。
bool test_text_works_in_unbounded_width()
{
    clearPools();
    g_textMeasure = [](const WidgetRef &, Constraints) -> MeasuredSize {
        return {{120, 25}, 20};
    };
    Node text = {makeText(), {}, {}, "text"};
    if (!tryLayout(text, {0, Constraints::inf, 0, 100}))
        return false;
    return checkGeometry(text, 0, 0, 120, 25, "text in unbounded");
}

/// 测试16：Column 中的 Expanded 按 flex 分配垂直剩余空间。
bool test_expanded_in_column_distributes_vertically()
{
    clearPools();
    Node col = {makeColumn(100.0f, 200.0f), {}, {}, "col"};
    col.children.push_back({makeContainer(50.0f, 30.0f), {}, {}, "c1"});
    Node exp = {makeExpanded(2), {}, {}, "exp"};
    exp.children.push_back({makeContainer(50.0f, std::nullopt), {}, {}, "c2"});
    col.children.push_back(std::move(exp));
    if (!tryLayout(col, {0, 100, 0, 200}))
        return false;
    return checkGeometry(col.children[0], 0, 0, 50, 30, "c1") &&
           checkGeometry(col.children[1].children[0], 0, 30, 50, 170, "expanded c2");
}

/// 测试17：负 margin 导致子节点重叠。
bool test_negative_margin_overlap()
{
    clearPools();
    Node row = {makeRow(200.0f, 100.0f), {}, {}, "row"};
    row.children.push_back({makeContainer(50.0f, 50.0f), {}, {}, "c1"});
    row.children.push_back({makeContainer(50.0f, 50.0f, {-10, 0, 0, 0}), {}, {}, "c2"});
    if (!tryLayout(row, {0, 200, 0, 100}))
        return false;
    return checkGeometry(row.children[0], 0, 0, 50, 50, "c1") &&
           checkGeometry(row.children[1], 40, 0, 50, 50, "c2 overlapping");
}

/// 测试18：Expanded 子节点带有 margin。
bool test_expanded_with_margin()
{
    clearPools();
    Node row = {makeRow(300.0f, 100.0f), {}, {}, "row"};
    row.children.push_back({makeContainer(50.0f, 30.0f), {}, {}, "c1"});
    Node exp = {makeExpanded(1), {}, {}, "exp"};
    Node c2 = {makeContainer(std::nullopt, 30.0f, {10, 0, 0, 0}), {}, {}, "c2"};
    exp.children.push_back(std::move(c2));
    row.children.push_back(std::move(exp));
    if (!tryLayout(row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(row.children[0], 0, 0, 50, 30, "c1") &&
           checkGeometry(row.children[1].children[0], 60, 0, 240, 30, "c2 with margin");
}

/// 测试19：Container.constraints
bool test_container_own_constraints()
{
    clearPools();
    Node cont = {makeContainer(std::nullopt, std::nullopt, {}, {}, {}, std::nullopt,
                               Constraints{80, 120, 0, Constraints::inf}),
                 {},
                 {},
                 "cont"};
    cont.children.push_back({makeContainer(50.0f, 50.0f), {}, {}, "inner"});
    if (!tryLayout(cont, {0, 200, 0, 200}))
        return false;
    return checkGeometry(cont, 0, 0, 80, 50, "cont constrained to 80x50") &&
           checkGeometry(cont.children[0], 0, 0, 80, 50, "inner fills cont");
}

/// 测试20：textDirection (RTL Row)
bool test_row_rtl()
{
    clearPools();
    Node row = {makeRow(300.0f, 100.0f, {}, {}, MainAxisAlignment::start,
                        CrossAxisAlignment::start, MainAxisSize::max, TextDirection::rtl),
                {},
                {},
                "row"};
    row.children.push_back({makeContainer(50.0f, 30.0f), {}, {}, "c1"});
    row.children.push_back({makeContainer(70.0f, 40.0f), {}, {}, "c2"});
    if (!tryLayout(row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(row.children[0], 250, 0, 50, 30, "c1 right") &&
           checkGeometry(row.children[1], 180, 0, 70, 40, "c2 left");
}

/// 测试21：verticalDirection (Up Column)
bool test_column_up()
{
    clearPools();
    Node col = {makeColumn(100.0f, 200.0f, {}, {}, MainAxisAlignment::start,
                           CrossAxisAlignment::start, MainAxisSize::max,
                           VerticalDirection::up),
                {},
                {},
                "col"};
    col.children.push_back({makeContainer(50.0f, 30.0f), {}, {}, "c1"});
    col.children.push_back({makeContainer(50.0f, 40.0f), {}, {}, "c2"});
    if (!tryLayout(col, {0, 100, 0, 200}))
        return false;
    return checkGeometry(col.children[0], 0, 170, 50, 30, "c1 bottom") &&
           checkGeometry(col.children[1], 0, 130, 50, 40, "c2 above");
}

/// 测试22：CrossAxisAlignment.baseline (Row) – 使用真实的基线值
bool test_baseline_alignment()
{
    clearPools();
    // 模拟不同字高的文本，基线为高度的 0.8 倍
    g_textMeasure = [](const WidgetRef &ref, Constraints) -> MeasuredSize {
        float h =
            get_member<"height">(ref).value_or(std::optional<float>{}).value_or(20.0f);
        return {{80, h}, h * 0.8f}; // baseline = 0.8 * height
    };
    Node row = {makeRow(300.0f, 100.0f, {}, {}, MainAxisAlignment::start,
                        CrossAxisAlignment::baseline),
                {},
                {},
                "row"};
    row.children.push_back({makeText(std::nullopt, 40.0f), {}, {}, "t1"});
    row.children.push_back({makeText(std::nullopt, 20.0f), {}, {}, "t2"});
    row.children.push_back({makeText(std::nullopt, 60.0f), {}, {}, "t3"});
    if (!tryLayout(row, {0, 300, 0, 100}))
        return false;
    // baseline: t1=32, t2=16, t3=48, maxBaseline=48
    return checkGeometry(row.children[0], 0, 16, 80, 40, "t1 y=16") &&
           checkGeometry(row.children[1], 80, 32, 80, 20, "t2 y=32") &&
           checkGeometry(row.children[2], 160, 0, 80, 60, "t3 y=0");
}

/// 测试23：多层 Expanded 嵌套 (Row > Expanded > Column > Expanded)
bool test_nested_expanded()
{
    clearPools();
    Node row = {makeRow(300.0f, 200.0f), {}, {}, "row"};
    row.children.push_back({makeContainer(50.0f, 50.0f), {}, {}, "fixed"});
    Node expRow = {makeExpanded(1), {}, {}, "expRow"};
    Node col = {makeColumn(std::nullopt, std::nullopt, {}, {}, MainAxisAlignment::start,
                           CrossAxisAlignment::stretch, MainAxisSize::max),
                {},
                {},
                "col"};
    Node expCol = {makeExpanded(1), {}, {}, "expCol"};
    expCol.children.push_back(
        {makeContainer(std::nullopt, std::nullopt), {}, {}, "inner"});
    col.children.push_back({makeContainer(80.0f, 30.0f), {}, {}, "colFixed"});
    col.children.push_back(std::move(expCol));
    expRow.children.push_back(std::move(col));
    row.children.push_back(std::move(expRow));
    if (!tryLayout(row, {0, 300, 0, 200}))
        return false;
    return checkGeometry(row.children[0], 0, 0, 50, 50, "fixed") &&
           checkGeometry(row.children[1].children[0].children[0], 0, 0, 250, 30,
                         "colFixed") &&
           checkGeometry(row.children[1].children[0].children[1].children[0], 0, 30, 250,
                         170, "inner expanded");
}

/// 测试24：验证 border 对 Container 尺寸和子组件位置的影响
bool test_border()
{
    clearPools();
    Node parent{
        makeContainer(100.0f, 100.0f, {}, {10, 10, 10, 10}, {5, 5, 5, 5}, Align::topLeft),
        {},
        {},
        "parent"};
    Node child{makeContainer(30.0f, 20.0f), {}, {}, "child"};
    parent.children.push_back(std::move(child));
    if (!tryLayout(parent, {0, 200, 0, 200}))
        return false;
    return checkGeometry(parent, 0, 0, 100, 100, "parent with border") &&
           checkGeometry(parent.children[0], 15, 15, 30, 20, "child inside border");
}

// ======================== Vue3 风格 DSL Builder ========================
struct WidgetBuilder
{
    Node node;

    WidgetBuilder(std::string name, WidgetRef ref) : node{ref, {}, {}, std::move(name)} {}

    // 添加子节点（带构建 lambda）
    WidgetBuilder &child(std::string name, WidgetRef ref,
                         std::function<void(WidgetBuilder &)> build = nullptr)
    {
        node.children.emplace_back(ref, std::vector<Node>{}, BoxGeometry{},
                                   std::move(name));
        WidgetBuilder &childBuilder =
            *reinterpret_cast<WidgetBuilder *>(&node.children.back());
        if (build)
            build(childBuilder);
        return *this;
    }

    // 便捷接口：直接添加已构建好的 Node
    WidgetBuilder &child(Node n)
    {
        node.children.push_back(std::move(n));
        return *this;
    }

    Node build()
    {
        return std::move(node);
    }
};

// 工厂辅助：创建带默认名称的 WidgetBuilder
WidgetBuilder Container(std::string name, std::optional<float> w = {},
                        std::optional<float> h = {}, EdgeInsets margin = {},
                        EdgeInsets padding = {}, EdgeInsets border = {},
                        std::optional<Alignment> align = {},
                        std::optional<Constraints> constraints = {})
{
    return WidgetBuilder(std::move(name), makeContainer(w, h, margin, padding, border,
                                                        align, constraints));
}

WidgetBuilder Row(std::string name, std::optional<float> w = {},
                  std::optional<float> h = {}, EdgeInsets margin = {},
                  EdgeInsets padding = {},
                  MainAxisAlignment mainAlign = MainAxisAlignment::start,
                  CrossAxisAlignment crossAlign = CrossAxisAlignment::start,
                  MainAxisSize mainAxisSize = MainAxisSize::max,
                  TextDirection textDir = TextDirection::ltr)
{
    return WidgetBuilder(std::move(name), makeRow(w, h, margin, padding, mainAlign,
                                                  crossAlign, mainAxisSize, textDir));
}

WidgetBuilder Column(std::string name, std::optional<float> w = {},
                     std::optional<float> h = {}, EdgeInsets margin = {},
                     EdgeInsets padding = {},
                     MainAxisAlignment mainAlign = MainAxisAlignment::start,
                     CrossAxisAlignment crossAlign = CrossAxisAlignment::start,
                     MainAxisSize mainAxisSize = MainAxisSize::max,
                     VerticalDirection vertDir = VerticalDirection::down)
{
    return WidgetBuilder(std::move(name), makeColumn(w, h, margin, padding, mainAlign,
                                                     crossAlign, mainAxisSize, vertDir));
}

WidgetBuilder Text(std::string name, std::optional<float> w = {},
                   std::optional<float> h = {}, EdgeInsets margin = {},
                   EdgeInsets padding = {})
{
    return WidgetBuilder(std::move(name), makeText(w, h, margin, padding));
}

WidgetBuilder Expanded(int flex = 1)
{
    return WidgetBuilder("expanded", makeExpanded(flex));
}

/// 运行所有测试
bool runTests()
{
    return test_parent_constraints_win_over_fixed_child() &&
           test_fixed_size_child_inside_row() &&
           test_expanded_distributes_space_by_flex() &&
           test_stretch_overrides_fixed_height() && test_main_axis_min_shrinks_column() &&
           test_text_uses_measure_func_and_margin() && test_container_shrink_to_child() &&
           test_unbounded_container_shrinks_to_zero() &&
           test_expanded_outside_flex_throws() && test_container_with_padding_shrinks() &&
           test_nested_container_shrink() && test_row_stretch_only_affects_height() &&
           test_column_space_evenly_distribution() &&
           test_parent_tighter_than_fixed_override() &&
           test_text_works_in_unbounded_width() &&
           test_expanded_in_column_distributes_vertically() &&
           test_negative_margin_overlap() && test_expanded_with_margin() &&
           test_specific_layout_equivalence() && test_border() &&
           test_container_own_constraints() && test_row_rtl() && test_column_up() &&
           test_baseline_alignment() && test_nested_expanded();
}

int main()
{

    if (runTests())
    {
        std::cout << "All tests passed!\n";
        return 0;
    }

    return 1;
}
// NOLINTEND