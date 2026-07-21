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

// NOLINTBEGIN
// ============================================================================
// 基础几何与布局类型
// ============================================================================

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
// 枚举与样式（对应 Flutter 的各类配置）
// ============================================================================
// NOTE: 用途：对齐 widget。https://docs.flutter.cn/ui/layout#aligning-widgets
// NOTE: 对于一行来说，主轴水平延伸，交叉轴垂直延伸。对于一列来说，主轴垂直延伸，交叉轴水平延伸。
enum class MainAxisAlignment
{
    start,
    end,
    center,
    spaceBetween,
    spaceAround,
    spaceEvenly // 因此设置主轴对齐方式为 spaceEvenly 会将空余空间在每个图像之间、之前和之后均匀地划分
};
enum class CrossAxisAlignment
{
    start,
    end,
    center,
    stretch
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

/// 存储每个 Widget 的样式属性，全局池由索引引用。
struct ContainerStyle
{
    std::optional<float> width, height;
    EdgeInsets margin{}, padding{}, border{};
    std::optional<Alignment> alignment = std::nullopt;
};
struct RowStyle
{
    std::optional<float> width, height;
    EdgeInsets margin{}, padding{};
    MainAxisAlignment mainAlign = MainAxisAlignment::start;
    CrossAxisAlignment crossAlign = CrossAxisAlignment::start;
    MainAxisSize mainAxisSize = MainAxisSize::max;
};
struct ColumnStyle
{
    std::optional<float> width, height;
    EdgeInsets margin{}, padding{};
    MainAxisAlignment mainAlign = MainAxisAlignment::start;
    CrossAxisAlignment crossAlign = CrossAxisAlignment::start;
    MainAxisSize mainAxisSize = MainAxisSize::max;
};
struct ExpandedStyle
{
    int flex = 1;
};
struct TextStyle
{
    std::optional<float> width, height;
    EdgeInsets margin{}, padding{};
};

// 全局样式池（模拟 Widget 树中的样式存储）
std::vector<ContainerStyle> g_containers;
std::vector<RowStyle> g_rows;
std::vector<ColumnStyle> g_columns;
std::vector<ExpandedStyle> g_expandeds;
std::vector<TextStyle> g_texts;

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
};

// ============================================================================
// 访问器：从 WidgetRef 获取样式属性
// ============================================================================

const EdgeInsets &marginOf(const WidgetRef &ref)
{
    switch (ref.kind)
    {
    case WidgetKind::Container:
        return g_containers[ref.idx].margin;
    case WidgetKind::Row:
        return g_rows[ref.idx].margin;
    case WidgetKind::Column:
        return g_columns[ref.idx].margin;
    case WidgetKind::Text:
        return g_texts[ref.idx].margin;
    default: {
        static EdgeInsets zero;
        return zero;
    }
    }
}
const EdgeInsets &paddingOf(const WidgetRef &ref)
{
    switch (ref.kind)
    {
    case WidgetKind::Container:
        return g_containers[ref.idx].padding;
    case WidgetKind::Row:
        return g_rows[ref.idx].padding;
    case WidgetKind::Column:
        return g_columns[ref.idx].padding;
    case WidgetKind::Text:
        return g_texts[ref.idx].padding;
    default: {
        static EdgeInsets zero;
        return zero;
    }
    }
}
const EdgeInsets &borderOf(const WidgetRef &ref)
{
    if (ref.kind == WidgetKind::Container)
        return g_containers[ref.idx].border;
    static EdgeInsets zero;
    return zero;
}
std::optional<float> widthOf(const WidgetRef &ref)
{
    switch (ref.kind)
    {
    case WidgetKind::Container:
        return g_containers[ref.idx].width;
    case WidgetKind::Row:
        return g_rows[ref.idx].width;
    case WidgetKind::Column:
        return g_columns[ref.idx].width;
    case WidgetKind::Text:
        return g_texts[ref.idx].width;
    default:
        return {};
    }
}
std::optional<float> heightOf(const WidgetRef &ref)
{
    switch (ref.kind)
    {
    case WidgetKind::Container:
        return g_containers[ref.idx].height;
    case WidgetKind::Row:
        return g_rows[ref.idx].height;
    case WidgetKind::Column:
        return g_columns[ref.idx].height;
    case WidgetKind::Text:
        return g_texts[ref.idx].height;
    default:
        return {};
    }
}
std::optional<Alignment> alignmentOf(const WidgetRef &ref)
{
    if (ref.kind == WidgetKind::Container)
        return g_containers[ref.idx].alignment;
    return std::nullopt;
}

/// 测量函数类型：用于 Text 等需要自然尺寸的叶子 Widget
using MeasureFunc = Size (*)(const WidgetRef &ref, Constraints bc);
std::unordered_map<WidgetKind, MeasureFunc> g_measureFuncs;

// ============================================================================
// 工厂函数：创建 WidgetRef，并存入全局样式池
// ============================================================================

/// 唯一 Container 工厂，所有参数均为可选，消除重载歧义。
WidgetRef makeContainer(std::optional<float> width = std::nullopt,
                        std::optional<float> height = std::nullopt,
                        EdgeInsets margin = {}, EdgeInsets padding = {},
                        EdgeInsets border = {},
                        std::optional<Alignment> alignment = std::nullopt)
{
    uint32_t idx = g_containers.size();
    g_containers.push_back(
        ContainerStyle{width, height, margin, padding, border, alignment});
    return {WidgetKind::Container, idx};
}

/// Row 工厂（保留多个重载，因参数个数不同无歧义）
WidgetRef makeRow(std::optional<float> w, std::optional<float> h,
                  MainAxisAlignment ma = MainAxisAlignment::start,
                  CrossAxisAlignment ca = CrossAxisAlignment::start,
                  MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {},
                  EdgeInsets p = {})
{
    uint32_t idx = g_rows.size();
    g_rows.push_back(RowStyle{w, h, m, p, ma, ca, ms});
    return {WidgetKind::Row, idx};
}
WidgetRef makeRow(EdgeInsets m = {}, EdgeInsets p = {})
{
    return makeRow(std::nullopt, std::nullopt, MainAxisAlignment::start,
                   CrossAxisAlignment::start, MainAxisSize::max, m, p);
}
WidgetRef makeRow(float w, float h, MainAxisAlignment ma = MainAxisAlignment::start,
                  CrossAxisAlignment ca = CrossAxisAlignment::start,
                  MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {},
                  EdgeInsets p = {})
{
    return makeRow(std::optional<float>(w), std::optional<float>(h), ma, ca, ms, m, p);
}

/// Column 工厂（与 Row 对称）
WidgetRef makeColumn(std::optional<float> w, std::optional<float> h,
                     MainAxisAlignment ma = MainAxisAlignment::start,
                     CrossAxisAlignment ca = CrossAxisAlignment::start,
                     MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {},
                     EdgeInsets p = {})
{
    uint32_t idx = g_columns.size();
    g_columns.push_back(ColumnStyle{w, h, m, p, ma, ca, ms});
    return {WidgetKind::Column, idx};
}
WidgetRef makeColumn(EdgeInsets m = {}, EdgeInsets p = {})
{
    return makeColumn(std::nullopt, std::nullopt, MainAxisAlignment::start,
                      CrossAxisAlignment::start, MainAxisSize::max, m, p);
}
WidgetRef makeColumn(float w, float h, MainAxisAlignment ma = MainAxisAlignment::start,
                     CrossAxisAlignment ca = CrossAxisAlignment::start,
                     MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {},
                     EdgeInsets p = {})
{
    return makeColumn(std::optional<float>(w), std::optional<float>(h), ma, ca, ms, m, p);
}

WidgetRef makeExpanded(int flex = 1)
{
    uint32_t idx = g_expandeds.size();
    g_expandeds.push_back(ExpandedStyle{flex});
    return {WidgetKind::Expanded, idx};
}

WidgetRef makeText(std::optional<float> w = std::nullopt,
                   std::optional<float> h = std::nullopt, EdgeInsets m = {},
                   EdgeInsets p = {})
{
    uint32_t idx = g_texts.size();
    g_texts.push_back(TextStyle{w, h, m, p});
    return {WidgetKind::Text, idx};
}

void clearPools()
{
    g_containers.clear();
    g_rows.clear();
    g_columns.clear();
    g_expandeds.clear();
    g_texts.clear();
    g_measureFuncs.clear();
}

// ============================================================================
// 核心布局函数（前向声明）
// ============================================================================
void layout(Node &node, Constraints constraints);

// ============================================================================
// 布局辅助函数（对应 Flutter 布局过程中的常见操作）
// ============================================================================

/// 生成宽松约束：min=0, max=原 max，让子节点可以自由选择尺寸
static Constraints makeLooseConstraints(const Constraints &c)
{
    return {0.0f, c.maxW, 0.0f, c.maxH};
}

/// 计算子节点占用的总尺寸（包括 margin）
static Size childFullSize(const Node &child)
{
    const auto &m = marginOf(child.ref);
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
static void positionChildByAlignment(Node &child, const Constraints &containerConstraints,
                                     const EdgeInsets &padding,
                                     const std::optional<Alignment> &align,
                                     const EdgeInsets &border)
{
    const auto &childMargin = marginOf(child.ref);
    Size childFull = childFullSize(child);
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

// ----------------- 主轴/交叉轴工具（用于 Flex 布局） -----------------
static float mainAxisSizeFromConstraints(const Constraints &bc, bool isRow)
{
    return isRow ? bc.maxW : bc.maxH;
}
static float crossAxisSizeFromConstraints(const Constraints &bc, bool isRow)
{
    return isRow ? bc.maxH : bc.maxW;
}
static float mainAxisPaddingStart(const EdgeInsets &pad, bool isRow)
{
    return isRow ? pad.left : pad.top;
}
static float mainAxisPaddingEnd(const EdgeInsets &pad, bool isRow)
{
    return isRow ? pad.right : pad.bottom;
}
static float crossAxisPaddingStart(const EdgeInsets &pad, bool isRow)
{
    return isRow ? pad.top : pad.left;
}
static float crossAxisPaddingEnd(const EdgeInsets &pad, bool isRow)
{
    return isRow ? pad.bottom : pad.right;
}
static float childMainAxisLength(const Node &child, bool isRow)
{
    return isRow ? child.geometry.w : child.geometry.h;
}
static float childCrossAxisLength(const Node &child, bool isRow)
{
    return isRow ? child.geometry.h : child.geometry.w;
}
static void setChildMainAxisPosition(Node &child, bool isRow, float pos)
{
    if (isRow)
        child.geometry.x = pos;
    else
        child.geometry.y = pos;
}
static void setChildCrossAxisPosition(Node &child, bool isRow, float pos)
{
    if (isRow)
        child.geometry.y = pos;
    else
        child.geometry.x = pos;
}
static bool mainAxisIsBounded(const Constraints &bc, bool isRow)
{
    return isRow ? bc.hasBoundedWidth() : bc.hasBoundedHeight();
}
static bool crossAxisIsBounded(const Constraints &bc, bool isRow)
{
    return isRow ? bc.hasBoundedHeight() : bc.hasBoundedWidth();
}
static Constraints makeFlexAxisConstraints(bool isRow, float minMain, float maxMain,
                                           float minCross, float maxCross)
{
    if (isRow)
        return {minMain, maxMain, minCross, maxCross};
    else
        return {minCross, maxCross, minMain, maxMain};
}

// ============================================================================
// Container 布局：实现 Flutter Container 的收缩/填充行为
// ============================================================================

/// 布局 Container：无 alignment 时填满父约束，有 alignment 时收缩到子节点尺寸。
void layoutContainer(Node &node, Constraints borderBC, const EdgeInsets &pad)
{
    auto &ref = node.ref;
    const auto &border = borderOf(ref);

    if (node.children.empty())
    {
        // 叶子 Container：宽高取固定值或 0，由约束收紧
        float w = widthOf(ref).value_or(0.0f);
        float h = heightOf(ref).value_or(0.0f);
        Size size = borderBC.clamp({w, h});
        node.geometry = BoxGeometry{0, 0, size.width, size.height};
        return;
    }

    if (node.children.size() != 1)
        throw std::logic_error("Container '" + node.name +
                               "' must have exactly one child.");

    Node &child = node.children[0];
    // 内容区约束 = 扣除 border 和 padding 后的剩余空间
    Constraints innerBC = borderBC.deflate(border).deflate(pad);

    auto align = alignmentOf(ref);
    // 有 alignment => 给子节点宽松约束，让子节点自由选择尺寸
    // 无 alignment => 传递紧约束，让子节点填满
    Constraints childBC = align.has_value() ? makeLooseConstraints(innerBC) : innerBC;
    childBC = childBC.deflate(marginOf(child.ref)); // 扣除子节点 margin
    layout(child, childBC);

    // Container 自身尺寸：优先取固定值，否则收缩到子节点总尺寸 + padding + border
    Size childFull = childFullSize(child);
    float containerW =
        widthOf(ref).value_or(childFull.width + pad.horizontal() + border.horizontal());
    float containerH =
        heightOf(ref).value_or(childFull.height + pad.vertical() + border.vertical());
    Size containerSize = borderBC.clamp({containerW, containerH});

    // 定位子节点（若有 alignment 则对齐，否则默认左上），考虑 border
    Constraints containerAsConstraints{0, containerSize.width, 0, containerSize.height};
    positionChildByAlignment(child, containerAsConstraints, pad, align, border);

    node.geometry = BoxGeometry{0, 0, containerSize.width, containerSize.height};
}

// ============================================================================
// Text 布局：使用注册的测量函数获取自然尺寸
// ============================================================================

/// 布局 Text：必须有测量函数，尺寸由测量函数 + 固定宽高决定。
void layoutText(Node &node, Constraints borderBC, const EdgeInsets &pad)
{
    (void)pad; // 当前未使用 padding，但保留接口
    auto &ref = node.ref;
    if (!node.children.empty())
        throw std::logic_error("Text widget '" + node.name + "' cannot have children.");

    auto it = g_measureFuncs.find(WidgetKind::Text);
    if (it == g_measureFuncs.end())
        throw std::logic_error("No measure function registered for Text.");

    Size natural = it->second(ref, borderBC);
    float w = widthOf(ref).value_or(natural.width);
    float h = heightOf(ref).value_or(natural.height);
    Size size = borderBC.clamp({w, h});
    node.geometry = BoxGeometry{0, 0, size.width, size.height};
}

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
static std::vector<FlexChildInfo> collectFlexChildren(Node &node, bool isRow,
                                                      float &outTotalFlex)
{
    std::vector<FlexChildInfo> infos;
    outTotalFlex = 0.0f;
    for (auto &child : node.children)
    {
        if (child.ref.kind == WidgetKind::Expanded)
        {
            if (child.children.empty())
                throw std::logic_error("Expanded widget has no child.");
            Node &real = child.children[0];
            float flexVal = g_expandeds[child.ref.idx].flex;
            const auto &m = marginOf(real.ref);
            float mM = isRow ? m.horizontal() : m.vertical();
            float mC = isRow ? m.vertical() : m.horizontal();
            infos.push_back({&real, flexVal, mM, mC});
            outTotalFlex += flexVal;
        }
        else
        {
            const auto &m = marginOf(child.ref);
            float mM = isRow ? m.horizontal() : m.vertical();
            float mC = isRow ? m.vertical() : m.horizontal();
            infos.push_back({&child, 0.0f, mM, mC});
        }
    }
    return infos;
}

/// 布局所有非弹性子节点，获取它们在主轴上的自然长度
static std::vector<float> layoutChildrenNaturally(const std::vector<FlexChildInfo> &infos,
                                                  bool isRow, bool isBoundedMain,
                                                  float innerMain, float innerCross,
                                                  CrossAxisAlignment crossAlign)
{
    std::vector<float> naturalMain;
    naturalMain.reserve(infos.size());
    for (const auto &info : infos)
    {
        // 弹性子节点暂不布局，自然长度记为 0（由后续分配）
        float childMainMax =
            (info.flex > 0) ? 0.0f : (isBoundedMain ? innerMain : Constraints::inf);
        float childCrossMax = innerCross;
        float childCrossMin =
            (crossAlign == CrossAxisAlignment::stretch) ? childCrossMax : 0.0f;

        Constraints childBC = makeFlexAxisConstraints(isRow, 0.0f, childMainMax,
                                                      childCrossMin, childCrossMax);
        if (info.flex == 0)
            childBC = childBC.deflate(
                EdgeInsets{info.marginMain, info.marginCross, 0.0f, 0.0f});

        layout(*info.node, childBC);
        naturalMain.push_back(childMainAxisLength(*info.node, isRow));
    }
    return naturalMain;
}

/// 将剩余主轴空间按 flex 比例分配给弹性子节点
static void distributeFlexSpace(const std::vector<FlexChildInfo> &infos,
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
            layout(*infos[i].node, tightBC);
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
static void positionChildrenInFlex(const std::vector<FlexChildInfo> &infos,
                                   const std::vector<float> &naturalMain, bool isRow,
                                   float padMainStart, float padCrossStart,
                                   float innerCross, CrossAxisAlignment crossAlign,
                                   float mainGap, float mainStartOff)
{
    float mainPos = padMainStart + mainStartOff;
    for (size_t i = 0; i < infos.size(); ++i)
    {
        Node &child = *infos[i].node;
        const auto &childMargin = marginOf(child.ref);
        float childCrossLen = childCrossAxisLength(child, isRow);
        float crossExtra =
            std::max(0.0f, innerCross - (childCrossLen + infos[i].marginCross));
        float crossStart = 0.0f;
        switch (crossAlign)
        {
        case CrossAxisAlignment::start:
            crossStart = 0.0f;
            break;
        case CrossAxisAlignment::end:
            crossStart = crossExtra;
            break;
        case CrossAxisAlignment::center:
            crossStart = crossExtra / 2.0f;
            break;
        case CrossAxisAlignment::stretch:
            crossStart = 0.0f;
            break;
        }

        if (isRow)
        {
            setChildMainAxisPosition(child, isRow, mainPos + childMargin.left);
            setChildCrossAxisPosition(child, isRow,
                                      padCrossStart + crossStart + childMargin.top);
        }
        else
        {
            setChildMainAxisPosition(child, isRow, mainPos + childMargin.top);
            setChildCrossAxisPosition(child, isRow,
                                      padCrossStart + crossStart + childMargin.left);
        }
        mainPos += naturalMain[i] + infos[i].marginMain + mainGap;
    }
}

/// 布局 Flex（Row/Column）的主函数
void layoutFlex(Node &node, Constraints borderBC, const EdgeInsets &pad, bool isRow)
{
    auto &ref = node.ref;
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

    float padMainStart = mainAxisPaddingStart(pad, isRow);
    float padMainEnd = mainAxisPaddingEnd(pad, isRow);
    float padCrossStart = crossAxisPaddingStart(pad, isRow);
    float padCrossEnd = crossAxisPaddingEnd(pad, isRow);

    float innerMain = mainSize - padMainStart - padMainEnd;
    if (!isBoundedMain)
        innerMain = Constraints::inf;
    float innerCross = crossSize - padCrossStart - padCrossEnd;

    float totalFlex = 0;
    auto infos = collectFlexChildren(node, isRow, totalFlex);

    CrossAxisAlignment crossAlign =
        isRow ? g_rows[ref.idx].crossAlign : g_columns[ref.idx].crossAlign;
    MainAxisSize axisSize =
        isRow ? g_rows[ref.idx].mainAxisSize : g_columns[ref.idx].mainAxisSize;

    // 1. 布局非弹性子节点，获得自然主轴尺寸
    auto naturalMain = layoutChildrenNaturally(infos, isRow, isBoundedMain, innerMain,
                                               innerCross, crossAlign);

    // 2. 计算所有子节点占用的总主轴长度（自然尺寸 + margin）
    float totalNatural = 0.0f;
    for (size_t i = 0; i < infos.size(); ++i)
        totalNatural += naturalMain[i] + infos[i].marginMain;

    // 3. 根据 MainAxisSize 决定最终内容区主轴尺寸
    float finalInnerMain =
        computeFinalMainSize(isBoundedMain, totalNatural, innerMain, axisSize);
    float freeMain = std::max(0.0f, finalInnerMain - totalNatural);

    // 4. 将剩余空间分配给弹性子节点
    distributeFlexSpace(infos, naturalMain, isRow, freeMain, totalFlex, innerCross,
                        crossAlign);

    // 5. 重新计算实际占用主轴长度（弹性子节点可能改变）
    float totalMainUsed = 0.0f;
    for (size_t i = 0; i < infos.size(); ++i)
        totalMainUsed += naturalMain[i] + infos[i].marginMain;

    // 6. 计算主轴对齐所需的间隙和偏移
    float extraMain = std::max(0.0f, finalInnerMain - totalMainUsed);
    MainAxisAlignment mainAlign =
        isRow ? g_rows[ref.idx].mainAlign : g_columns[ref.idx].mainAlign;
    float mainGap = 0.0f, mainStartOff = 0.0f;
    computeMainGapAndStart(infos.size(), extraMain, mainAlign, mainGap, mainStartOff);

    // 7. 定位所有子节点
    positionChildrenInFlex(infos, naturalMain, isRow, padMainStart, padCrossStart,
                           innerCross, crossAlign, mainGap, mainStartOff);

    // 8. 计算 Flex 自身尺寸
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
    if (auto w = widthOf(ref))
        size.width = *w;
    if (auto h = heightOf(ref))
        size.height = *h;
    size = borderBC.clamp(size);

    node.geometry = BoxGeometry{0, 0, size.width, size.height};
}

// ============================================================================
// 顶层布局入口：约束向下传递，最终调用具体布局函数
// ============================================================================

/// 布局入口：接收父约束，处理固定宽高，分发到具体 Widget 布局函数。
/// 完全符合 Flutter "约束向下，尺寸向上" 的原则。
void layout(Node &node, Constraints constraints)
{
    const auto &ref = node.ref;
    if (ref.kind == WidgetKind::Expanded)
        throw std::logic_error(
            "Expanded widget must be placed directly inside Row/Column/Flex.");

    Constraints borderBC = constraints;

    // 如果 Widget 自身有固定宽高，收紧约束
    if (auto w = widthOf(ref))
        borderBC = borderBC.applyFixedWidth(*w);
    if (auto h = heightOf(ref))
        borderBC = borderBC.applyFixedHeight(*h);

    // 叶子节点且存在测量函数：使用自然尺寸作为默认宽高
    if (node.children.empty() && (!widthOf(ref) || !heightOf(ref)))
    {
        auto it = g_measureFuncs.find(ref.kind);
        if (it != g_measureFuncs.end())
        {
            Size natural = it->second(ref, borderBC);
            if (!widthOf(ref))
                borderBC = borderBC.applyFixedWidth(natural.width);
            if (!heightOf(ref))
                borderBC = borderBC.applyFixedHeight(natural.height);
        }
    }

    const auto &padding = paddingOf(ref);
    switch (ref.kind)
    {
    case WidgetKind::Container:
        layoutContainer(node, borderBC, padding);
        break;
    case WidgetKind::Text:
        layoutText(node, borderBC, padding);
        break;
    case WidgetKind::Row:
        layoutFlex(node, borderBC, padding, true);
        break;
    case WidgetKind::Column:
        layoutFlex(node, borderBC, padding, false);
        break;
    default:
        throw std::logic_error("Unhandled WidgetKind in layout");
    }

    // 溢出检测（若最终尺寸超出父约束则警告）
    Size finalSize{node.geometry.w, node.geometry.h};
    if (finalSize.width > constraints.maxW + 1e-6f ||
        finalSize.height > constraints.maxH + 1e-6f)
    {
        std::cerr << "WARNING: Widget '" << node.name << "' overflows parent by ("
                  << finalSize.width - constraints.maxW << ", "
                  << finalSize.height - constraints.maxH << ")\n";
    }
}

// ============================================================================
// 测试工具
// ============================================================================

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
        c = c.deflate(marginOf(node.ref)); // 根节点 margin 由父级模拟扣除
        layout(node, c);
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

// ============================================================================
// 测试用例：每个用例验证 Flutter 布局中的一种特定行为
// ============================================================================

/// 测试1：父容器固定尺寸且显式 alignment，子容器保持固定尺寸并带 margin。
/// 通过说明：父约束优先，但 alignment 给子节点宽松约束，子节点固定尺寸生效。
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

/// 测试2：Row 中放置固定尺寸子节点，子节点保持自身大小，位置由 Row 决定。
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
    Node row = {
        makeRow(300.0f, 100.0f, MainAxisAlignment::start, CrossAxisAlignment::stretch),
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
    Node col = {makeColumn(200.0f, std::nullopt, MainAxisAlignment::start,
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
    g_measureFuncs[WidgetKind::Text] = [](const WidgetRef &, Constraints) -> Size {
        return {120, 25};
    };
    Node col = {makeColumn(300.0f, 200.0f), {}, {}, "col"};
    col.children.push_back(
        {makeText(std::nullopt, std::nullopt, {5, 5, 5, 5}), {}, {}, "text"});
    if (!tryLayout(col, {0, 300, 0, 200}))
        return false;
    return checkGeometry(col.children[0], 5, 5, 120, 25, "text");
}

/// 测试7：Container 无固定尺寸且无 alignment 时，在紧约束下填满父容器（不会收缩）。
/// 通过说明：验证 Flutter 中无 alignment 的 Container 默认填充父约束。
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
    // 无 alignment，child 被拉伸到 180x180（父约束 200x200 减去 margin）
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
    Node row = {
        makeRow(300.0f, 100.0f, MainAxisAlignment::start, CrossAxisAlignment::stretch),
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
    Node col = {
        makeColumn(100.0f, 200.0f, MainAxisAlignment::spaceEvenly), {}, {}, "col"};
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
    g_measureFuncs[WidgetKind::Text] = [](const WidgetRef &, Constraints) -> Size {
        return {120, 25};
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

/// 测试17：负 margin 导致子节点重叠（验证 margin 可产生负偏移）。
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

/// 测试18：Expanded 子节点带有 margin，剩余空间分配时考虑 margin 但不占用内容区。
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

// ============================================================================
// 提取 ScreenGeometry（供 Vulkan 渲染使用）
// ============================================================================

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
    std::function<void(const Node &, float, float)> dfs =
        [&](const Node &node, float parentX, float parentY) {
            float x = parentX + node.geometry.x;
            float y = parentY + node.geometry.y;
            float w = node.geometry.w;
            float h = node.geometry.h;
            // 只提取 Container 节点（用户定义的节点），跳过辅助布局节点（如 Column）
            if (node.ref.kind == WidgetKind::Container)
            {
                const auto &pad = paddingOf(node.ref);
                const auto &border = borderOf(node.ref);
                geos.push_back({x, y, w, h, pad.left, pad.right, pad.top, pad.bottom,
                                border.left, border.right, border.top, border.bottom});
            }
            for (const auto &child : node.children)
                dfs(child, x, y);
        };
    dfs(root, 0.0f, 0.0f);
    return geos;
}

/// 测试 19：引擎布局与 Yoga 输出等价（基于您提供的 layout.print 结果）
bool test_yoga_equivalence()
{
    clearPools();

    // 构建引擎树（与 Yoga 树结构一致）
    Node screen{makeContainer(800.0f, 600.0f, {}, {}), {}, {}, "screen"};

    // root (200x200, padding 10, alignment 使子节点保持尺寸)
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
    Node root_column{makeColumn(std::nullopt, std::nullopt, MainAxisAlignment::start,
                                CrossAxisAlignment::start, MainAxisSize::max, {}, {}),
                     {},
                     {},
                     "root_column"};
    root_column.children.push_back(std::move(child0));
    root_column.children.push_back(std::move(node1_in_root));
    root.children.push_back(std::move(root_column));

    // root2 (200x200, padding 10, alignment)
    Node root2{makeContainer(200.0f, 200.0f, {}, {10, 10, 10, 10}, {}, Align::topLeft),
               {},
               {},
               "root2"};
    Node node1_in_root2{makeContainer(100.0f, 20.0f, {5, 5, 5, 5}, {}), {}, {}, "1"};
    Node root2_column{makeColumn(std::nullopt, std::nullopt, MainAxisAlignment::start,
                                 CrossAxisAlignment::start, MainAxisSize::max, {}, {}),
                      {},
                      {},
                      "root2_column"};
    root2_column.children.push_back(std::move(node1_in_root2));
    root2.children.push_back(std::move(root2_column));

    // text_display (200x200, padding 10) 叶子，无子节点
    Node text_display{
        makeContainer(200.0f, 200.0f, {}, {10, 10, 10, 10}), {}, {}, "text_display"};

    // screen 的 Column 排列三个顶级节点
    Node screen_column{makeColumn(std::nullopt, std::nullopt, MainAxisAlignment::start,
                                  CrossAxisAlignment::start, MainAxisSize::max, {}, {}),
                       {},
                       {},
                       "screen_column"};
    screen_column.children.push_back(std::move(root));
    screen_column.children.push_back(std::move(root2));
    screen_column.children.push_back(std::move(text_display));
    screen.children.push_back(std::move(screen_column));

    if (!tryLayout(screen, {0, 800, 0, 600}))
        return false;

    auto geos = extractScreenGeometries(screen); // 只提取 Container

    // 期望值（绝对坐标）
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

/// 测试20：验证 border 对 Container 尺寸和子组件位置的影响
bool test_border()
{
    clearPools();
    // Container with fixed 100x100, border 5 on all sides, padding 10, child 30x20
    Node parent{
        makeContainer(100.0f, 100.0f, {}, {10, 10, 10, 10}, {5, 5, 5, 5}, Align::topLeft),
        {},
        {},
        "parent"};
    Node child{makeContainer(30.0f, 20.0f), {}, {}, "child"};
    parent.children.push_back(std::move(child));
    if (!tryLayout(parent, {0, 200, 0, 200}))
        return false;
    // parent size should be exactly 100x100 (fixed), border + padding reduces content area
    // content area = (100 - 5*2 - 10*2) = 100-10-20 = 70 width, 70 height
    // child is 30x20, positioned at border.left+pad.left = 5+10=15, 15
    return checkGeometry(parent, 0, 0, 100, 100, "parent with border") &&
           checkGeometry(parent.children[0], 15, 15, 30, 20, "child inside border");
}

/// 运行所有测试，返回是否全部通过
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
           test_yoga_equivalence() && test_border();
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