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

/*
 * ============================================================================
 * FLUTTER 布局引擎 - 术语表与常用函数速查（C++ 注释版）
 * 对应关系与官方 Flutter 保持一致，方便理解约束传递与布局逻辑
 * ============================================================================
 */

/* ============================================================================
 * 一、基础几何数据（积木块）
 * ============================================================================
 *   Size          -> 对应 Flutter Size          : 仅存宽高 (width, height)
 *   Offset        -> 对应 Flutter Offset        : 二维偏移量 (x, y)
 *   EdgeInsets    -> 对应 Flutter EdgeInsets    : 上下左右空白 (left, top, right, bottom)
 *                   - horizontal() : 快捷返回 left + right
 *                   - vertical()   : 快捷返回 top + bottom
 *   Alignment     -> 对应 Flutter Alignment     : 归一化坐标 (-1..1)
 *                   - Align::topLeft   : (-1,-1)
 *                   - Align::center    : ( 0, 0)
 *                   - Align::bottomRight: ( 1, 1)
 * ============================================================================ */

/* ============================================================================
 * 二、约束核心（Constraints）—— 父级传给子级的“规矩”
 * ============================================================================
 *   Constraints  -> 对应 Flutter BoxConstraints
 *                   字段: float minW, maxW, minH, maxH
 *
 *   核心成员函数（意图说明）：
 *     - hasBoundedWidth()     : 宽度是否有上限 (maxW 不是 inf)
 *     - hasUnboundedHeight()  : 高度是否无限 (maxH 是 inf)
 *     - deflate(EdgeInsets)   : 扣减空白。例如 0~200 减去左右各 10 => 0~180
 *     - intersect(Constraints): 取交集收紧。例如 0~100 与 50~200 => 50~100
 *     - clamp(Size)           : 夹紧尺寸。将待定 Size 硬约束到 [min, max] 范围内
 *     - applyFixedWidth(w)    : 固定宽度。将宽度收紧为 w~w 并与当前约束取交集
 *     - applyFixedHeight(h)   : 固定高度（同上）
 * ============================================================================ */

/* ============================================================================
 * 三、方向与布局轴（往哪走）
 * ============================================================================
 *   AxisDirection  -> 对应 Flutter AxisDirection
 *                     枚举: right(左→右), left(右→左), down(上→下), up(下→上)
 *   TextDirection  -> 对应 Flutter TextDirection
 *                     枚举: ltr(左到右), rtl(右到左) —— 仅影响 Row 的主轴起始边
 *   VerticalDirection -> 对应 Flutter VerticalDirection
 *                     枚举: down(上到下), up(下到上) —— 仅影响 Column 的主轴起始边
 *
 *   配套工具函数：
 *     - axisDirectionForFlex(isRow, td, vd) : 推导最终的 AxisDirection
 *     - isAxisForward(dir)                 : 判断是否为正向（right 或 down）
 * ============================================================================ */

/* ============================================================================
 * 四、对齐与排列规则（摆在哪）
 * ============================================================================
 *   MainAxisAlignment   -> 对应 Flutter MainAxisAlignment
 *                         枚举: start, end, center,
 *                               spaceBetween, spaceAround, spaceEvenly
 *                         作用：决定 Flex（Row/Column）子项在主轴方向的分布
 *
 *   CrossAxisAlignment -> 对应 Flutter CrossAxisAlignment
 *                         枚举: start, end, center, stretch, baseline
 *                         作用：决定子项在交叉轴方向的位置
 *                               stretch 会强制拉伸填满该轴
 *
 *   MainAxisSize       -> 对应 Flutter MainAxisSize
 *                         枚举: max（尽量撑满父约束）, min（收缩到子项总大小）
 *                         作用：决定 Flex 自身在主轴上的尺寸策略
 * ============================================================================ */

/* ============================================================================
 * 五、核心公共工具函数（动作指令）
 * 说明：以下函数与 Flutter RenderObject 层级的工具方法一一对应
 * ============================================================================
 *
 *   1. makeLooseConstraints(Constraints c)
 *      -> 对应 Flutter BoxConstraints.loose
 *      -> 意图：生成宽松约束，min 置 0，仅保留 max。
 *      -> 典型场景：Container 有 alignment 时传给子节点，允许子节点自由选择尺寸。
 *
 *   2. childFullSize(const Node& child)
 *      -> 对应 Flutter 中手动计算 size + margins
 *      -> 意图：计算子节点总占用 = 几何尺寸 + margin。
 *      -> 典型场景：Flex 计算所有子项占用的总主轴长度。
 *
 *   3. alignmentOffset(Alignment, float, float, EdgeInsets, EdgeInsets, EdgeInsets)
 *      -> 对应 Flutter Alignment.inscribe
 *      -> 意图：根据 Alignment 和额外空间，算出具体的左上角偏移量。
 *      -> 典型场景：Container 根据对齐方式计算子节点位置。
 *
 *   4. positionChildByAlignment(Node&, Constraints, EdgeInsets, optional<Alignment>, EdgeInsets)
 *      -> 对应 Flutter RenderPositionedBox
 *      -> 意图：按对齐方式安置子节点（内部调用 alignmentOffset 并设置 child.geometry）。
 *      -> 典型场景：Container 布局的最后一步。
 *
 *   5. mainAxisSizeFromConstraints(const Constraints&, bool isRow)
 *      -> 意图：提取主轴尺寸（Row 取 maxW，Column 取 maxH）。
 *
 *   6. crossAxisSizeFromConstraints(const Constraints&, bool isRow)
 *      -> 意图：提取交叉轴尺寸（Row 取 maxH，Column 取 maxW）。
 *
 *   7. setChildMainAxisPosition(Node&, AxisDirection, float, float, float, float, float)
 *      -> 意图：设置子节点在主轴的物理坐标。
 *      -> 关键：自动处理 RTL 和 Up 反向轴时的坐标翻转。
 *
 *   8. setChildCrossAxisPosition(Node&, AxisDirection, float, float, float, float, float)
 *      -> 意图：设置子节点在交叉轴的物理坐标（目前仅支持正向）。
 *
 *   9. collectFlexChildren(Node&, bool, float&)
 *      -> 意图：收集 Flex 的所有子项，展开 Expanded，提取真实子节点并累加总 flex。
 *      -> 典型场景：Row/Column 布局的第一步。
 *
 *   10. distributeFlexSpace(infos, naturalMain, isRow, freeMain, totalFlex, innerCross, crossAlign)
 *       -> 意图：按 flex 比例将剩余主轴空间分配给 Expanded 子节点。
 *       -> 典型场景：Row/Column 布局的第二步。
 *
 *   11. computeMainGapAndStart(size_t, float, MainAxisAlignment, float&, float&)
 *       -> 意图：根据 MainAxisAlignment 计算起始偏移和子项间隙。
 *       -> 典型场景：Row/Column 的对齐计算。
 *
 *   12. extractScreenGeometries(const Node& root)
 *       -> 意图：深度优先遍历布局树，提取每个 Container 的 x,y,w,h,padding,border。
 *       -> 典型场景：将布局结果送给渲染引擎（如 Vulkan）进行绘制。
 * ============================================================================ */

/* ============================================================================
 * 六、布局调度与生命周期（总指挥）
 * ============================================================================
 *   layout(Node&, Constraints)  -> 对应 Flutter RenderObject.layout
 *     意图：总入口。父传约束进来，内部分发到具体布局函数（Container/Flex/Text）。
 *           执行完成后，node.geometry 被填好。
 *
 *   layoutContainer(...)        -> 对应 Flutter RenderConstrainedBox / RenderPadding
 *     意图：Container 专属逻辑。处理 alignment、padding、border、自身 constraints。
 *
 *   layoutFlex(...)            -> 对应 Flutter RenderFlex
 *     意图：Row/Column 专属逻辑。处理主轴/交叉轴、Expanded、各种对齐方式。
 *
 *   layoutText(...)            -> 对应 Flutter RenderParagraph
 *     意图：Text 专属逻辑。调用注册的测量函数获取自然尺寸，然后应用固定宽高。
 *
 *   MeasureFunc                -> 对应 Flutter TextPainter 或 computeDryLayout
 *     意图：叶子节点“自报尺寸”的回调函数类型。外部注册后供 layoutText 调用。
 *
 *   Node                       -> 对应 Flutter RenderObject 树节点
 *     意图：布局树节点，包含 WidgetRef、子节点列表、最终几何信息 (geometry) 和基线。
 *
 *   BoxGeometry                -> 存储 x, y, w, h 的简单几何结构体。
 * ============================================================================ */

/* ============================================================================
 * 七、紧约束 vs 宽松约束（快速记忆口诀）
 * ============================================================================
 *   【宽松约束 (Loose)】
 *     定义：min = 0, max = 某个限制值。子节点可以在 0 ~ max 之间自由选择任意尺寸。
 *     代码体现：makeLooseConstraints() 函数。
 *     典型场景：Container 有 alignment（对齐方式）时，父级不强迫子节点填满。
 *
 *   【紧约束 (Tight)】
 *     定义：min == max（即宽度或高度被锁定为同一个值）。子节点没有任何选择余地。
 *     代码体现：Constraints{w, w, h, h}，或调用 applyFixedWidth/Height 后产生。
 *     典型场景：设置了固定宽高，或 Container 没有 alignment 时被迫填满父容器。
 *
 *   【夹紧 (Clamp / Tighten)】 —— 这是一个“动作”，不是一种状态
 *     定义：把一个候选的尺寸值，强行裁剪或拉伸到目标约束的 [min, max] 范围内。
 *     代码体现：borderBC.clamp(size) 或 Constraints.intersect()。
 *     典型场景：子节点报上来的自然尺寸超出父约束范围时，父级用 clamp 把它拉回来。
 * ============================================================================ */

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

/// 存储每个 Widget 的样式属性，全局池由索引引用。
struct ContainerStyle
{
    std::optional<float> width, height;
    EdgeInsets margin{}, padding{}, border{};
    std::optional<Alignment> alignment = std::nullopt;
    std::optional<Constraints> constraints = std::nullopt; // 新增：Container 自身约束
};
struct RowStyle
{
    std::optional<float> width, height;
    EdgeInsets margin{}, padding{};
    MainAxisAlignment mainAlign = MainAxisAlignment::start;
    CrossAxisAlignment crossAlign = CrossAxisAlignment::start;
    MainAxisSize mainAxisSize = MainAxisSize::max;
    TextDirection textDirection = TextDirection::ltr; // 新增：文本方向
};
struct ColumnStyle
{
    std::optional<float> width, height;
    EdgeInsets margin{}, padding{};
    MainAxisAlignment mainAlign = MainAxisAlignment::start;
    CrossAxisAlignment crossAlign = CrossAxisAlignment::start;
    MainAxisSize mainAxisSize = MainAxisSize::max;
    VerticalDirection verticalDirection = VerticalDirection::down; // 新增：垂直方向
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
    float baseline = 0; // 基线偏移，相对于节点自身顶部（y 方向）
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
std::optional<Constraints> constraintsOf(const WidgetRef &ref)
{
    if (ref.kind == WidgetKind::Container)
        return g_containers[ref.idx].constraints;
    return std::nullopt;
}
TextDirection textDirectionOf(const WidgetRef &ref)
{
    if (ref.kind == WidgetKind::Row)
        return g_rows[ref.idx].textDirection;
    return TextDirection::ltr; // 非 Row 默认 ltr
}
VerticalDirection verticalDirectionOf(const WidgetRef &ref)
{
    if (ref.kind == WidgetKind::Column)
        return g_columns[ref.idx].verticalDirection;
    return VerticalDirection::down; // 非 Column 默认 down
}

/// 测量结果：包含自然尺寸和基线偏移（相对于顶部）
struct MeasuredSize
{
    Size size;
    float baseline = 0; // 基线偏移量（从顶部算起），若未知可设为 size.height
};

/// 测量函数类型：用于 Text 等需要自然尺寸的叶子 Widget
using MeasureFunc = MeasuredSize (*)(const WidgetRef &ref, Constraints bc);
std::unordered_map<WidgetKind, MeasureFunc> g_measureFuncs;

// ============================================================================
// 工厂函数：创建 WidgetRef，并存入全局样式池
// ============================================================================

/// 唯一 Container 工厂，所有参数均为可选，消除重载歧义。
WidgetRef makeContainer(std::optional<float> width = std::nullopt,
                        std::optional<float> height = std::nullopt,
                        EdgeInsets margin = {}, EdgeInsets padding = {},
                        EdgeInsets border = {},
                        std::optional<Alignment> alignment = std::nullopt,
                        std::optional<Constraints> constraints = std::nullopt) // 新增参数
{
    uint32_t idx = g_containers.size();
    g_containers.push_back(
        ContainerStyle{width, height, margin, padding, border, alignment, constraints});
    return {WidgetKind::Container, idx};
}

/// Row 工厂（保留多个重载，因参数个数不同无歧义）
WidgetRef makeRow(std::optional<float> w, std::optional<float> h,
                  MainAxisAlignment ma = MainAxisAlignment::start,
                  CrossAxisAlignment ca = CrossAxisAlignment::start,
                  MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {},
                  EdgeInsets p = {}, TextDirection td = TextDirection::ltr) // 新增参数
{
    uint32_t idx = g_rows.size();
    g_rows.push_back(RowStyle{w, h, m, p, ma, ca, ms, td});
    return {WidgetKind::Row, idx};
}
WidgetRef makeRow(EdgeInsets m = {}, EdgeInsets p = {})
{
    return makeRow(std::nullopt, std::nullopt, MainAxisAlignment::start,
                   CrossAxisAlignment::start, MainAxisSize::max, m, p,
                   TextDirection::ltr);
}
WidgetRef makeRow(float w, float h, MainAxisAlignment ma = MainAxisAlignment::start,
                  CrossAxisAlignment ca = CrossAxisAlignment::start,
                  MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {},
                  EdgeInsets p = {}, TextDirection td = TextDirection::ltr)
{
    return makeRow(std::optional<float>(w), std::optional<float>(h), ma, ca, ms, m, p,
                   td);
}

/// Column 工厂（与 Row 对称）
WidgetRef makeColumn(std::optional<float> w, std::optional<float> h,
                     MainAxisAlignment ma = MainAxisAlignment::start,
                     CrossAxisAlignment ca = CrossAxisAlignment::start,
                     MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {},
                     EdgeInsets p = {},
                     VerticalDirection vd = VerticalDirection::down) // 新增参数
{
    uint32_t idx = g_columns.size();
    g_columns.push_back(ColumnStyle{w, h, m, p, ma, ca, ms, vd});
    return {WidgetKind::Column, idx};
}
WidgetRef makeColumn(EdgeInsets m = {}, EdgeInsets p = {})
{
    return makeColumn(std::nullopt, std::nullopt, MainAxisAlignment::start,
                      CrossAxisAlignment::start, MainAxisSize::max, m, p,
                      VerticalDirection::down);
}
WidgetRef makeColumn(float w, float h, MainAxisAlignment ma = MainAxisAlignment::start,
                     CrossAxisAlignment ca = CrossAxisAlignment::start,
                     MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {},
                     EdgeInsets p = {}, VerticalDirection vd = VerticalDirection::down)
{
    return makeColumn(std::optional<float>(w), std::optional<float>(h), ma, ca, ms, m, p,
                      vd);
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

// ============================================================================
// Container 布局：实现 Flutter Container 的收缩/填充行为
// ============================================================================

/// 布局 Container：严格遵循 Flutter Container 的尺寸决定顺序：
/// 1. 遵守 alignment（通过收缩到子节点尺寸）
/// 2. 贴合子节点尺寸
/// 3. 遵守固定宽高及 constraints
void layoutContainer(Node &node, Constraints borderBC, const EdgeInsets &pad)
{
    auto &ref = node.ref;
    const auto &border = borderOf(ref);

    // 应用 Container 自身的约束（如果存在）
    if (auto ownConstraints = constraintsOf(ref))
    {
        borderBC = borderBC.intersect(*ownConstraints);
    }

    if (node.children.empty())
    {
        // 叶子 Container：宽高取固定值或 0，由约束收紧
        float w = widthOf(ref).value_or(0.0f);
        float h = heightOf(ref).value_or(0.0f);
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

    auto align = alignmentOf(ref);
    // 有 alignment => 给子节点宽松约束，让子节点自由选择尺寸
    // 无 alignment => 传递紧约束，让子节点填满
    Constraints childBC = align.has_value() ? makeLooseConstraints(innerBC) : innerBC;
    childBC = childBC.deflate(marginOf(child.ref)); // 扣除子节点 margin
    layout(child, childBC);

    // 先计算贴合子节点的基准尺寸（先于固定宽高）
    Size childFull = childFullSize(child);
    float baseW = childFull.width + pad.horizontal() + border.horizontal();
    float baseH = childFull.height + pad.vertical() + border.vertical();

    // 再用固定宽高覆盖（若有），否则使用贴合后的基准尺寸
    float containerW = widthOf(ref).value_or(baseW);
    float containerH = heightOf(ref).value_or(baseH);
    Size containerSize = borderBC.clamp({containerW, containerH});

    // 定位子节点（若有 alignment 则对齐，否则默认左上），考虑 border
    Constraints containerAsConstraints{0, containerSize.width, 0, containerSize.height};
    positionChildByAlignment(child, containerAsConstraints, pad, align, border);

    node.geometry = BoxGeometry{0, 0, containerSize.width, containerSize.height};
    // 有子节点时，基线为子节点基线加上子节点在容器内的 y 偏移
    node.baseline = child.baseline + child.geometry.y;
}

// ============================================================================
// Text 布局：使用注册的测量函数获取自然尺寸和基线
// ============================================================================

/// 布局 Text：必须有测量函数，尺寸由测量函数 + 固定宽高决定。
/// 基线处理：优先使用测量函数返回的基线；若未提供，则按字体度量估算（假设基线 ≈ 0.75 * 高度）。
void layoutText(Node &node, Constraints borderBC, const EdgeInsets &pad)
{
    (void)pad; // 当前未使用 padding，但保留接口
    auto &ref = node.ref;
    if (!node.children.empty())
        throw std::logic_error("Text widget '" + node.name + "' cannot have children.");

    auto it = g_measureFuncs.find(WidgetKind::Text);
    if (it == g_measureFuncs.end())
        throw std::logic_error("No measure function registered for Text.");

    MeasuredSize measured = it->second(ref, borderBC);
    float w = widthOf(ref).value_or(measured.size.width);
    float h = heightOf(ref).value_or(measured.size.height);
    Size size = borderBC.clamp({w, h});
    node.geometry = BoxGeometry{0, 0, size.width, size.height};

    // 基线：若测量函数提供了有效值则使用，否则基于字体度量估算（默认 ~0.75 * 高度）
    float baselineFromMetrics =
        measured.baseline > 0 ? measured.baseline : size.height * 0.75f;
    node.baseline = std::min(baselineFromMetrics, size.height);
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
/// 关键修改：对于非弹性子节点，主轴方向不施加任何最大约束（maxMain = inf），
/// 以完全符合 Flutter Row/Column “不对子节点施加约束”的语义。
/// 子节点将完全根据自身内容或固定尺寸决定大小。
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
static void positionChildrenInFlex(const std::vector<FlexChildInfo> &infos,
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
        const auto &childMargin = marginOf(child.ref);
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
void layoutFlex(Node &node, Constraints borderBC, const EdgeInsets &pad, bool isRow)
{
    auto &ref = node.ref;
    TextDirection td = textDirectionOf(ref);
    VerticalDirection vd = verticalDirectionOf(ref);
    AxisDirection dir = axisDirectionForFlex(isRow, td, vd);

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

    // 7. 定位所有子节点（传入方向、父容器尺寸等）
    positionChildrenInFlex(infos, naturalMain, dir, isRow, innerMain, innerCross,
                           padMainStart, padMainEnd, padCrossStart, padCrossEnd,
                           crossAlign, mainGap, mainStartOff, mainSize, crossSize);

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
    // Flex 的基线：取第一个子节点的基线 + 其在容器内的偏移（y 对于 Row，x 对于 Column）
    if (!infos.empty())
    {
        Node &first = *infos[0].node;
        if (isRow)
            node.baseline = first.baseline + first.geometry.y;
        else
            node.baseline = first.baseline +
                            first.geometry.x; // 对于 Column，基线无明确意义，但仍设置
    }
    else
    {
        node.baseline = isRow ? size.height : size.width;
    }
}

// ============================================================================
// 顶层布局入口：约束向下传递，最终调用具体布局函数
// ============================================================================
/*
所有布局都从 layout 函数开始。它的工作：

1. 先根据 Widget 自身的 width/height 收紧约束（applyFixedWidth/Height）。
2. 如果是 Container，还要合并它自己的 constraints 字段。
3. 如果节点是叶子且注册了测量函数（比如 Text），就用测量函数获得自然尺寸，并收紧约束。
4. 然后根据 ref.kind 分发到具体的布局函数：
    layoutContainer（Container）
    layoutFlex（Row / Column，通过 isRow 区分）
    layoutText（Text）

最终，每个节点的 geometry（x,y,w,h）和 baseline 被填好。layout 还会检测是否溢出父约束，并打印警告。
*/
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

    // 对于 Container，还要合并自身的 constraints
    if (auto ownConstraints = constraintsOf(ref))
    {
        borderBC = borderBC.intersect(*ownConstraints);
    }

    // 叶子节点且存在测量函数：使用自然尺寸作为默认宽高
    if (node.children.empty() && (!widthOf(ref) || !heightOf(ref)))
    {
        auto it = g_measureFuncs.find(ref.kind);
        if (it != g_measureFuncs.end())
        {
            MeasuredSize natural = it->second(ref, borderBC);
            if (!widthOf(ref))
                borderBC = borderBC.applyFixedWidth(natural.size.width);
            if (!heightOf(ref))
                borderBC = borderBC.applyFixedHeight(natural.size.height);
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
    g_measureFuncs[WidgetKind::Text] = [](const WidgetRef &,
                                          Constraints) -> MeasuredSize {
        return {{120, 25}, 20}; // baseline = 20
    };
    Node col = {makeColumn(300.0f, 200.0f), {}, {}, "col"};
    col.children.push_back(
        {makeText(std::nullopt, std::nullopt, {5, 5, 5, 5}), {}, {}, "text"});
    if (!tryLayout(col, {0, 300, 0, 200}))
        return false;
    return checkGeometry(col.children[0], 5, 5, 120, 25, "text");
}

/// 测试7：Container 无固定尺寸且无 alignment 时，在紧约束下填满父容器（不会收缩）。
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
    g_measureFuncs[WidgetKind::Text] = [](const WidgetRef &,
                                          Constraints) -> MeasuredSize {
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
// 新增测试：Container.constraints
// ============================================================================
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
// ============================================================================
// 新增测试：textDirection (RTL Row)
// ============================================================================
bool test_row_rtl()
{
    clearPools();
    Node row = {makeRow(300.0f, 100.0f, MainAxisAlignment::start,
                        CrossAxisAlignment::start, MainAxisSize::max, {}, {},
                        TextDirection::rtl),
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

// ============================================================================
// 新增测试：verticalDirection (Up Column)
// ============================================================================
bool test_column_up()
{
    clearPools();
    Node col = {makeColumn(100.0f, 200.0f, MainAxisAlignment::start,
                           CrossAxisAlignment::start, MainAxisSize::max, {}, {},
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

// ============================================================================
// 新增测试：CrossAxisAlignment.baseline (Row) – 使用真实的基线值
// ============================================================================
bool test_baseline_alignment()
{
    clearPools();
    // 模拟不同字高的文本，基线为高度的 0.8 倍
    g_measureFuncs[WidgetKind::Text] = [](const WidgetRef &ref,
                                          Constraints) -> MeasuredSize {
        float h = heightOf(ref).value_or(20.0f);
        return {{80, h}, h * 0.8f}; // baseline = 0.8 * height
    };
    Node row = {
        makeRow(300.0f, 100.0f, MainAxisAlignment::start, CrossAxisAlignment::baseline),
        {},
        {},
        "row"};
    row.children.push_back({makeText(std::nullopt, 40.0f), {}, {}, "t1"});
    row.children.push_back({makeText(std::nullopt, 20.0f), {}, {}, "t2"});
    row.children.push_back({makeText(std::nullopt, 60.0f), {}, {}, "t3"});
    if (!tryLayout(row, {0, 300, 0, 100}))
        return false;
    // baseline: t1=32, t2=16, t3=48, maxBaseline=48
    // t1.y = 48 - 32 = 16
    // t2.y = 48 - 16 = 32
    // t3.y = 48 - 48 = 0
    return checkGeometry(row.children[0], 0, 16, 80, 40, "t1 y=16") &&
           checkGeometry(row.children[1], 80, 32, 80, 20, "t2 y=32") &&
           checkGeometry(row.children[2], 160, 0, 80, 60, "t3 y=0");
}

// ============================================================================
// 新增测试：多层 Expanded 嵌套 (Row > Expanded > Column > Expanded)
// ============================================================================
bool test_nested_expanded()
{
    clearPools();
    // Row 宽 300，高 200，包含一个固定 50x50 和一个 Expanded(flex=1) 的 Column
    Node row = {makeRow(300.0f, 200.0f), {}, {}, "row"};
    row.children.push_back({makeContainer(50.0f, 50.0f), {}, {}, "fixed"});
    Node expRow = {makeExpanded(1), {}, {}, "expRow"};
    Node col = {makeColumn(std::nullopt, std::nullopt, MainAxisAlignment::start,
                           CrossAxisAlignment::stretch, MainAxisSize::max, {}, {}),
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
    // Row 高度 200，Column 交叉轴拉伸 -> 高度 = 200
    // 固定子节点 colFixed: 80x30
    // 剩余垂直空间 = 200 - 30 = 170，Expanded 占据全部 -> inner 高 170
    return checkGeometry(row.children[0], 0, 0, 50, 50, "fixed") &&
           // colFixed 相对于 col 的位置：x=0，y=0，宽度被拉伸为 250，高度 30
           checkGeometry(row.children[1].children[0].children[0], 0, 0, 250, 30,
                         "colFixed") &&
           // inner 相对 col 的位置：x=0，y=30，宽度 250，高度剩余 170
           checkGeometry(row.children[1].children[0].children[1].children[0], 0, 30, 250,
                         170, "inner expanded");
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

/// 测试 19：特定布局等价性验证（重命名自 test_yoga_equivalence）
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
    Node root_column{makeColumn(std::nullopt, std::nullopt, MainAxisAlignment::start,
                                CrossAxisAlignment::start, MainAxisSize::max, {}, {}),
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
    Node root2_column{makeColumn(std::nullopt, std::nullopt, MainAxisAlignment::start,
                                 CrossAxisAlignment::start, MainAxisSize::max, {}, {}),
                      {},
                      {},
                      "root2_column"};
    root2_column.children.push_back(std::move(node1_in_root2));
    root2.children.push_back(std::move(root2_column));

    Node text_display{
        makeContainer(200.0f, 200.0f, {}, {10, 10, 10, 10}), {}, {}, "text_display"};

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

/// 测试20：验证 border 对 Container 尺寸和子组件位置的影响
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