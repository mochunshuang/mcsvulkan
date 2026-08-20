// 原理: https://docs.flutter.dev/ui/layout/constraints
//      https://docs.flutter.dev/ui/layout#overview
// 概念：约束向下传递，尺寸向上传递，父节点决定子节点位置。
// 本实现将 Flutter 布局原则映射为 C++ 代码，所有行为均与官方一致。
// 使用 Builder 风格 API，构造函数使用初始化列表。

#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

// ============================================================================
// 基础几何与布局类型
// ============================================================================

struct Size
{
    float width, height;
};

struct Offset
{
    float x, y;
};

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
    static constexpr EdgeInsets all(float v)
    {
        return {v, v, v, v};
    }
};

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
// 约束（Constraints）
// ============================================================================

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

    constexpr Constraints deflate(const EdgeInsets &e) const noexcept
    {
        float h = e.horizontal(), v = e.vertical();
        return {std::max(0.0f, minW - h), std::max(0.0f, maxW - h),
                std::max(0.0f, minH - v), std::max(0.0f, maxH - v)};
    }

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

    constexpr Constraints applyFixedWidth(float w) const noexcept
    {
        return Constraints{w, w, minH, maxH}.intersect(*this);
    }
    constexpr Constraints applyFixedHeight(float h) const noexcept
    {
        return Constraints{minW, maxW, h, h}.intersect(*this);
    }

    constexpr Size clamp(const Size &s) const noexcept
    {
        return {std::clamp(s.width, minW, maxW), std::clamp(s.height, minH, maxH)};
    }
};

// ============================================================================
// 方向与枚举
// ============================================================================

enum class TextDirection
{
    ltr,
    rtl
};
enum class VerticalDirection
{
    down,
    up
};
enum class MainAxisAlignment
{
    start,
    end,
    center,
    spaceBetween,
    spaceAround,
    spaceEvenly
};
enum class CrossAxisAlignment
{
    start,
    end,
    center,
    stretch,
    baseline
};
enum class MainAxisSize
{
    max,
    min
};
enum class AxisDirection
{
    right,
    left,
    down,
    up
};

// ============================================================================
// 抽象节点基类
// ============================================================================

struct BoxGeometry
{
    float x = 0, y = 0, w = 0, h = 0;
};

class Node
{
  public:
    explicit Node(std::string name, EdgeInsets margin = {}, EdgeInsets padding = {},
                  std::optional<float> fixedWidth = std::nullopt,
                  std::optional<float> fixedHeight = std::nullopt)
        : name(std::move(name)), margin(margin), padding(padding), fixedWidth(fixedWidth),
          fixedHeight(fixedHeight)
    {
    }

    virtual ~Node() = default;

    BoxGeometry geometry;
    std::string name;
    float baseline = 0.0f;
    EdgeInsets margin;
    EdgeInsets padding;
    std::optional<float> fixedWidth;
    std::optional<float> fixedHeight;
    std::vector<std::unique_ptr<Node>> children;
    Node *parent = nullptr; // 父节点裸指针，不拥有所有权
    bool dirty = false;     // 脏标记，用于测试遍历是否覆盖

    // 布局入口
    void layout(Constraints constraints)
    {
        if (fixedWidth)
            constraints = constraints.applyFixedWidth(*fixedWidth);
        if (fixedHeight)
            constraints = constraints.applyFixedHeight(*fixedHeight);
        performLayout(constraints);
        Size finalSize{geometry.w, geometry.h};
        if (finalSize.width > constraints.maxW + 1e-6f ||
            finalSize.height > constraints.maxH + 1e-6f)
        {
            std::cerr << "WARNING: Widget '" << name << "' overflows parent by ("
                      << finalSize.width - constraints.maxW << ", "
                      << finalSize.height - constraints.maxH << ")\n";
        }
    }

    Size size() const
    {
        return {geometry.w, geometry.h};
    }
    Offset position() const
    {
        return {geometry.x, geometry.y};
    }
    BoxGeometry getGeometry() const
    {
        return geometry;
    }

    // ==========================================================================
    // 节点移动 API（安全操作，维护 parent 指针）
    // ==========================================================================

    // 移除指定子节点，返回其 unique_ptr（所有权转移给调用者），parent 置空
    std::unique_ptr<Node> removeChild(Node *child)
    {
        auto it = std::ranges::find_if(children, [child](const std::unique_ptr<Node> &w) {
            return w.get() == child;
        });
        if (it == children.end())
            return nullptr;

        auto detached = std::move(*it);
        children.erase(it);
        detached->parent = nullptr;
        // 可在此标记布局脏，但当前测试不涉及
        return detached;
    }

    // 添加子节点（接收 unique_ptr），设置 parent 并加入 children
    void addChild(std::unique_ptr<Node> child)
    {

        if (!child)
            return;
        child->parent = this;
        children.push_back(std::move(child));
    }

    // 将已存在的子节点移动到新的父节点下
    static void moveNode(Node *child, Node *newParent)
    {
        if (!child || !newParent)
            return;
        Node *oldParent = child->parent;
        if (oldParent == newParent)
            return;

        // 从旧父节点移除（如果有）
        std::unique_ptr<Node> detached;
        if (oldParent)
        {
            detached = oldParent->removeChild(child);
        }
        else
        {
            // 如果 child 没有父节点，无法取得所有权，无法移动
            return;
        }

        if (detached)
        {
            newParent->addChild(std::move(detached));
        }
    }

  protected:
    virtual void performLayout(Constraints borderBC) = 0;
    Size childFullSize(const Node &child) const
    {
        return {child.geometry.w + child.margin.horizontal(),
                child.geometry.h + child.margin.vertical()};
    }
    static Constraints makeLooseConstraints(const Constraints &c)
    {
        return {0.0f, c.maxW, 0.0f, c.maxH};
    }
};

// ============================================================================
// 类型别名
// ============================================================================

using Widget = std::unique_ptr<Node>;

// ============================================================================
// Expanded 节点
// ============================================================================

class ExpandedNode : public Node
{
  public:
    explicit ExpandedNode(int flex = 1, std::string name = "expanded")
        : Node(std::move(name)), flex(flex)
    {
    }
    int flex = 1;

  protected:
    void performLayout(Constraints) override
    {
        throw std::logic_error("Expanded must be direct child of Row/Column/Flex.");
    }
};

// ============================================================================
// Container 节点
// ============================================================================

class ContainerNode : public Node
{
  public:
    explicit ContainerNode(std::string name, std::optional<float> width = std::nullopt,
                           std::optional<float> height = std::nullopt,
                           EdgeInsets margin = {}, EdgeInsets padding = {},
                           EdgeInsets border = {},
                           std::optional<Alignment> alignment = std::nullopt,
                           std::optional<Constraints> constraints = std::nullopt)
        : Node(std::move(name), margin, padding, width, height), alignment(alignment),
          ownConstraints(constraints), border(border)
    {
    }

    std::optional<Alignment> alignment;
    std::optional<Constraints> ownConstraints;
    EdgeInsets border;

  protected:
    void performLayout(Constraints borderBC) override
    {
        if (ownConstraints)
            borderBC = borderBC.intersect(*ownConstraints);
        if (children.empty())
        {
            float w = fixedWidth.value_or(0.0f);
            float h = fixedHeight.value_or(0.0f);
            Size size = borderBC.clamp({w, h});
            geometry = BoxGeometry{0, 0, size.width, size.height};
            baseline = size.height;
            return;
        }
        if (children.size() != 1)
            throw std::logic_error("Container '" + name +
                                   "' must have exactly one child.");
        Node &child = *children[0];
        Constraints innerBC = borderBC.deflate(border).deflate(padding);
        Constraints childBC = alignment ? Node::makeLooseConstraints(innerBC) : innerBC;
        childBC = childBC.deflate(child.margin);
        child.layout(childBC);
        Size childFull = childFullSize(child);
        float baseW = childFull.width + padding.horizontal() + border.horizontal();
        float baseH = childFull.height + padding.vertical() + border.vertical();
        float containerW = fixedWidth.value_or(baseW);
        float containerH = fixedHeight.value_or(baseH);
        Size containerSize = borderBC.clamp({containerW, containerH});
        Constraints containerAsConstraints{0, containerSize.width, 0,
                                           containerSize.height};
        positionChildByAlignment(child, containerAsConstraints, padding, alignment,
                                 border);
        geometry = BoxGeometry{0, 0, containerSize.width, containerSize.height};
        baseline = child.baseline + child.geometry.y;
    }

  private:
    void positionChildByAlignment(Node &child, const Constraints &containerConstraints,
                                  const EdgeInsets &padding,
                                  const std::optional<Alignment> &align,
                                  const EdgeInsets &border)
    {
        Size childFull = childFullSize(child);
        float contentW =
            containerConstraints.maxW - border.horizontal() - padding.horizontal();
        float contentH =
            containerConstraints.maxH - border.vertical() - padding.vertical();
        float extraW = std::max(0.0f, contentW - childFull.width);
        float extraH = std::max(0.0f, contentH - childFull.height);
        Alignment al = align.value_or(Align::topLeft);
        Offset offset = {border.left + padding.left + child.margin.left +
                             extraW * (al.x + 1.0f) / 2.0f,
                         border.top + padding.top + child.margin.top +
                             extraH * (al.y + 1.0f) / 2.0f};
        child.geometry.x = offset.x;
        child.geometry.y = offset.y;
    }
};

// ============================================================================
// Flex 节点基类（Row 和 Column 共用）
// ============================================================================

template <bool isRow_>
class FlexNode : public Node
{
  public:
    MainAxisAlignment mainAlign = MainAxisAlignment::start;
    CrossAxisAlignment crossAlign = CrossAxisAlignment::start;
    MainAxisSize mainAxisSize = MainAxisSize::max;
    TextDirection textDirection = TextDirection::ltr;
    VerticalDirection verticalDirection = VerticalDirection::down;

  protected:
    FlexNode(std::string name, std::optional<float> fixedWidth,
             std::optional<float> fixedHeight, EdgeInsets margin, EdgeInsets padding,
             MainAxisAlignment mainAlign, CrossAxisAlignment crossAlign,
             MainAxisSize mainAxisSize, TextDirection textDirection = TextDirection::ltr,
             VerticalDirection verticalDirection = VerticalDirection::down)
        : Node(std::move(name), margin, padding, fixedWidth, fixedHeight),
          mainAlign(mainAlign), crossAlign(crossAlign), mainAxisSize(mainAxisSize),
          textDirection(textDirection), verticalDirection(verticalDirection)
    {
    }

    void performLayout(Constraints borderBC) override
    {
        AxisDirection dir = axisDirection();
        bool isBoundedMain =
            isRow_ ? borderBC.hasBoundedWidth() : borderBC.hasBoundedHeight();
        bool isBoundedCross =
            isRow_ ? borderBC.hasBoundedHeight() : borderBC.hasBoundedWidth();
        if (!isBoundedCross)
            throw std::logic_error("Row/Column '" + name + "' has unbounded cross axis.");
        if (!isBoundedMain)
        {
            for (const auto &child : children)
                if (dynamic_cast<ExpandedNode *>(child.get()) != nullptr)
                    throw std::logic_error(
                        "Row/Column '" + name +
                        "' has unbounded main axis and Expanded child.");
        }

        float mainSize = mainAxisSizeFromConstraints(borderBC);
        float crossSize = crossAxisSizeFromConstraints(borderBC);
        float padMainStart = mainAxisPaddingStart(padding, dir);
        float padMainEnd = mainAxisPaddingEnd(padding, dir);
        float padCrossStart = crossAxisPaddingStart(padding, dir);
        float padCrossEnd = crossAxisPaddingEnd(padding, dir);
        float innerMain = mainSize - padMainStart - padMainEnd;
        if (!isBoundedMain)
            innerMain = Constraints::inf;
        float innerCross = crossSize - padCrossStart - padCrossEnd;

        struct FlexChildInfo
        {
            Node *node;
            float flex;
            float marginMain;
            float marginCross;
        };
        std::vector<FlexChildInfo> infos;
        float totalFlex = 0;
        for (auto &childPtr : children)
        {
            Node *child = childPtr.get();
            if (auto expanded = dynamic_cast<ExpandedNode *>(child))
            {
                if (expanded->children.empty())
                    throw std::logic_error("Expanded widget has no child.");
                Node *real = expanded->children[0].get();
                float flexVal = expanded->flex;
                EdgeInsets m = real->margin;
                float mM = isRow_ ? m.horizontal() : m.vertical();
                float mC = isRow_ ? m.vertical() : m.horizontal();
                infos.push_back({real, flexVal, mM, mC});
                totalFlex += flexVal;
            }
            else
            {
                EdgeInsets m = child->margin;
                float mM = isRow_ ? m.horizontal() : m.vertical();
                float mC = isRow_ ? m.vertical() : m.horizontal();
                infos.push_back({child, 0.0f, mM, mC});
            }
        }

        std::vector<float> naturalMain;
        naturalMain.reserve(infos.size());
        for (const auto &info : infos)
        {
            float childMainMax =
                (info.flex > 0) ? 0.0f : (isBoundedMain ? innerMain : Constraints::inf);
            float childCrossMax = innerCross;
            float childCrossMin =
                (crossAlign == CrossAxisAlignment::stretch) ? childCrossMax : 0.0f;
            Constraints childBC = makeFlexAxisConstraints(isRow_, 0.0f, childMainMax,
                                                          childCrossMin, childCrossMax);
            if (info.flex == 0)
            {
                EdgeInsets deflateMargin =
                    isRow_ ? EdgeInsets{info.marginMain, info.marginCross, 0, 0}
                           : EdgeInsets{info.marginCross, info.marginMain, 0, 0};
                childBC = childBC.deflate(deflateMargin);
            }
            info.node->layout(childBC);
            naturalMain.push_back(isRow_ ? info.node->geometry.w : info.node->geometry.h);
        }

        float totalNatural = 0;
        for (size_t i = 0; i < infos.size(); ++i)
            totalNatural += naturalMain[i] + infos[i].marginMain;

        float finalInnerMain;
        if (!isBoundedMain)
            finalInnerMain = totalNatural;
        else if (mainAxisSize == MainAxisSize::min)
            finalInnerMain = std::min(totalNatural, innerMain);
        else
            finalInnerMain = innerMain;

        float freeMain = std::max(0.0f, finalInnerMain - totalNatural);
        if (totalFlex > 0 && freeMain > 0)
        {
            float flexUnit = freeMain / totalFlex;
            for (size_t i = 0; i < infos.size(); ++i)
            {
                if (infos[i].flex > 0)
                {
                    float allocated = infos[i].flex * flexUnit;
                    float childMainSize = std::max(0.0f, allocated - infos[i].marginMain);
                    float childCrossMax = innerCross;
                    float childCrossMin = (crossAlign == CrossAxisAlignment::stretch)
                                              ? childCrossMax
                                              : 0.0f;
                    Constraints tightBC =
                        makeFlexAxisConstraints(isRow_, childMainSize, childMainSize,
                                                childCrossMin, childCrossMax);
                    infos[i].node->layout(tightBC);
                    naturalMain[i] =
                        isRow_ ? infos[i].node->geometry.w : infos[i].node->geometry.h;
                }
            }
        }

        float totalMainUsed = 0;
        for (size_t i = 0; i < infos.size(); ++i)
            totalMainUsed += naturalMain[i] + infos[i].marginMain;

        float extraMain = std::max(0.0f, finalInnerMain - totalMainUsed);
        float mainGap = 0, mainStartOff = 0;
        size_t childCount = infos.size();
        if (childCount == 1)
        {
            if (mainAlign == MainAxisAlignment::spaceAround ||
                mainAlign == MainAxisAlignment::spaceEvenly)
                mainStartOff = extraMain / 2;
        }
        else if (childCount > 1)
        {
            switch (mainAlign)
            {
            case MainAxisAlignment::start:
                mainStartOff = 0;
                break;
            case MainAxisAlignment::end:
                mainStartOff = extraMain;
                break;
            case MainAxisAlignment::center:
                mainStartOff = extraMain / 2;
                break;
            case MainAxisAlignment::spaceBetween:
                mainGap = extraMain / (childCount - 1);
                break;
            case MainAxisAlignment::spaceAround:
                mainGap = extraMain / childCount;
                mainStartOff = mainGap / 2;
                break;
            case MainAxisAlignment::spaceEvenly:
                mainGap = extraMain / (childCount + 1);
                mainStartOff = mainGap;
                break;
            }
        }

        float mainPos = mainStartOff;
        float maxBaseline = 0;
        if (crossAlign == CrossAxisAlignment::baseline && isRow_)
        {
            for (const auto &info : infos)
                maxBaseline = std::max(maxBaseline, info.node->baseline);
        }

        for (size_t i = 0; i < infos.size(); ++i)
        {
            Node &child = *infos[i].node;
            float childMainLen = naturalMain[i];
            float childCrossLen = isRow_ ? child.geometry.h : child.geometry.w;
            float leadingMargin = isRow_ ? child.margin.left : child.margin.top;
            setChildMainAxisPosition(child, dir, mainPos + leadingMargin, childMainLen,
                                     mainSize, padMainStart, padMainEnd);
            float crossExtra =
                std::max(0.0f, innerCross - (childCrossLen + infos[i].marginCross));
            float crossStart = 0;
            switch (crossAlign)
            {
            case CrossAxisAlignment::start:
                crossStart = 0;
                break;
            case CrossAxisAlignment::end:
                crossStart = crossExtra;
                break;
            case CrossAxisAlignment::center:
                crossStart = crossExtra / 2;
                break;
            case CrossAxisAlignment::stretch:
                crossStart = 0;
                break;
            case CrossAxisAlignment::baseline:
                if (isRow_)
                    crossStart = maxBaseline - child.baseline;
                break;
            }
            float crossLeadingMargin = isRow_ ? child.margin.top : child.margin.left;
            setChildCrossAxisPosition(child, dir, crossStart + crossLeadingMargin,
                                      childCrossLen, crossSize, padCrossStart,
                                      padCrossEnd);
            mainPos += childMainLen + infos[i].marginMain + mainGap;
        }

        Size size;
        if (isRow_)
        {
            size.width = finalInnerMain + padding.horizontal();
            if (crossAlign == CrossAxisAlignment::stretch)
                size.height = innerCross + padding.vertical();
            else
            {
                float maxChildHeight = 0;
                for (const auto &info : infos)
                    maxChildHeight = std::max(maxChildHeight, info.node->geometry.h);
                size.height = maxChildHeight + padding.vertical();
            }
        }
        else
        {
            if (crossAlign == CrossAxisAlignment::stretch)
                size.width = innerCross + padding.horizontal();
            else
            {
                float maxChildWidth = 0;
                for (const auto &info : infos)
                    maxChildWidth = std::max(maxChildWidth, info.node->geometry.w);
                size.width = maxChildWidth + padding.horizontal();
            }
            size.height = finalInnerMain + padding.vertical();
        }
        if (fixedWidth)
            size.width = *fixedWidth;
        if (fixedHeight)
            size.height = *fixedHeight;
        size = borderBC.clamp(size);
        geometry = BoxGeometry{0, 0, size.width, size.height};
        if (!infos.empty())
        {
            Node &first = *infos[0].node;
            baseline = isRow_ ? first.baseline + first.geometry.y
                              : first.baseline + first.geometry.x;
        }
        else
        {
            baseline = isRow_ ? size.height : size.width;
        }
    }

  private:
    AxisDirection axisDirection() const
    {
        if (isRow_)
            return (textDirection == TextDirection::ltr) ? AxisDirection::right
                                                         : AxisDirection::left;
        else
            return (verticalDirection == VerticalDirection::down) ? AxisDirection::down
                                                                  : AxisDirection::up;
    }
    bool isAxisForward(AxisDirection dir) const
    {
        return dir == AxisDirection::right || dir == AxisDirection::down;
    }
    float mainAxisSizeFromConstraints(const Constraints &bc) const
    {
        return isRow_ ? bc.maxW : bc.maxH;
    }
    float crossAxisSizeFromConstraints(const Constraints &bc) const
    {
        return isRow_ ? bc.maxH : bc.maxW;
    }
    float mainAxisPaddingStart(const EdgeInsets &pad, AxisDirection dir) const
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
    float mainAxisPaddingEnd(const EdgeInsets &pad, AxisDirection dir) const
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
    float crossAxisPaddingStart(const EdgeInsets &pad, AxisDirection dir) const
    {
        return (dir == AxisDirection::right || dir == AxisDirection::left) ? pad.top
                                                                           : pad.left;
    }
    float crossAxisPaddingEnd(const EdgeInsets &pad, AxisDirection dir) const
    {
        return (dir == AxisDirection::right || dir == AxisDirection::left) ? pad.bottom
                                                                           : pad.right;
    }
    Constraints makeFlexAxisConstraints(bool isRow, float minMain, float maxMain,
                                        float minCross, float maxCross) const
    {
        return isRow ? Constraints{minMain, maxMain, minCross, maxCross}
                     : Constraints{minCross, maxCross, minMain, maxMain};
    }
    void setChildMainAxisPosition(Node &child, AxisDirection dir, float pos,
                                  float childMainLen, float parentMainSize,
                                  float padStart, float padEnd)
    {
        float physical;
        if (isAxisForward(dir))
            physical = padStart + pos;
        else
            physical = parentMainSize - padEnd - pos - childMainLen;
        if (dir == AxisDirection::right || dir == AxisDirection::left)
            child.geometry.x = physical;
        else
            child.geometry.y = physical;
    }
    void setChildCrossAxisPosition(Node &child, AxisDirection dir, float pos,
                                   float childCrossLen, float parentCrossSize,
                                   float padCrossStart, float padCrossEnd)
    {
        (void)childCrossLen;
        (void)parentCrossSize;
        (void)padCrossEnd;
        if (dir == AxisDirection::right || dir == AxisDirection::left)
            child.geometry.y = padCrossStart + pos;
        else
            child.geometry.x = padCrossStart + pos;
    }
};

// ============================================================================
// Row 和 Column 具体节点
// ============================================================================

class RowNode : public FlexNode<true>
{
  public:
    explicit RowNode(std::string name, std::optional<float> width = std::nullopt,
                     std::optional<float> height = std::nullopt,
                     MainAxisAlignment ma = MainAxisAlignment::start,
                     CrossAxisAlignment ca = CrossAxisAlignment::start,
                     MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {},
                     EdgeInsets p = {}, TextDirection td = TextDirection::ltr)
        : FlexNode<true>(std::move(name), width, height, m, p, ma, ca, ms, td)
    {
    }
};

class ColumnNode : public FlexNode<false>
{
  public:
    explicit ColumnNode(std::string name, std::optional<float> width = std::nullopt,
                        std::optional<float> height = std::nullopt,
                        MainAxisAlignment ma = MainAxisAlignment::start,
                        CrossAxisAlignment ca = CrossAxisAlignment::start,
                        MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {},
                        EdgeInsets p = {}, VerticalDirection vd = VerticalDirection::down)
        : FlexNode<false>(std::move(name), width, height, m, p, ma, ca, ms,
                          TextDirection::ltr, vd)
    {
    }
};

// ============================================================================
// Builder 类
// ============================================================================

// 辅助函数：将任意类型转换为 Widget（支持 Widget 和 Builder）
template <typename T>
Widget toWidget(T &&arg)
{
    if constexpr (std::is_convertible_v<T, Widget>)
    {
        return std::forward<T>(arg);
    }
    else
    {
        return std::forward<T>(arg).build();
    }
}

class ContainerBuilder
{
    std::unique_ptr<ContainerNode> node;

  public:
    explicit ContainerBuilder(std::string name)
        : node(std::make_unique<ContainerNode>(std::move(name)))
    {
    }
    ContainerBuilder &width(float w)
    {
        node->fixedWidth = w;
        return *this;
    }
    ContainerBuilder &height(float h)
    {
        node->fixedHeight = h;
        return *this;
    }
    ContainerBuilder &margin(EdgeInsets m)
    {
        node->margin = m;
        return *this;
    }
    ContainerBuilder &padding(EdgeInsets p)
    {
        node->padding = p;
        return *this;
    }
    ContainerBuilder &border(EdgeInsets b)
    {
        node->border = b;
        return *this;
    }
    ContainerBuilder &alignment(Alignment a)
    {
        node->alignment = a;
        return *this;
    }
    ContainerBuilder &constraints(Constraints c)
    {
        node->ownConstraints = c;
        return *this;
    }
    template <typename T>
    ContainerBuilder &child(T &&child)
    {
        Widget childWidget = toWidget(std::forward<T>(child));
        node->addChild(std::move(childWidget)); // 使用 addChild 自动设置 parent
        return *this;
    }
    Widget build()
    {
        assert(node != nullptr);
        return std::move(node);
    }
};

class RowBuilder
{
    std::unique_ptr<RowNode> node;

  public:
    explicit RowBuilder(std::string name)
        : node(std::make_unique<RowNode>(std::move(name)))
    {
    }
    RowBuilder &width(float w)
    {
        node->fixedWidth = w;
        return *this;
    }
    RowBuilder &height(float h)
    {
        node->fixedHeight = h;
        return *this;
    }
    RowBuilder &margin(EdgeInsets m)
    {
        node->margin = m;
        return *this;
    }
    RowBuilder &padding(EdgeInsets p)
    {
        node->padding = p;
        return *this;
    }
    RowBuilder &mainAxisAlignment(MainAxisAlignment ma)
    {
        node->mainAlign = ma;
        return *this;
    }
    RowBuilder &crossAxisAlignment(CrossAxisAlignment ca)
    {
        node->crossAlign = ca;
        return *this;
    }
    RowBuilder &mainAxisSize(MainAxisSize ms)
    {
        node->mainAxisSize = ms;
        return *this;
    }
    RowBuilder &textDirection(TextDirection td)
    {
        node->textDirection = td;
        return *this;
    }

    template <typename... Children>
    RowBuilder &children(Children &&...children)
    {
        (
            [&] {
                Widget childWidget = toWidget(std::forward<Children>(children));
                node->addChild(std::move(childWidget)); // 使用 addChild
            }(),
            ...);
        return *this;
    }

    Widget build()
    {
        assert(node != nullptr);
        return std::move(node);
    }
};

class ColumnBuilder
{
    std::unique_ptr<ColumnNode> node;

  public:
    explicit ColumnBuilder(std::string name)
        : node(std::make_unique<ColumnNode>(std::move(name)))
    {
    }
    ColumnBuilder &width(float w)
    {
        node->fixedWidth = w;
        return *this;
    }
    ColumnBuilder &height(float h)
    {
        node->fixedHeight = h;
        return *this;
    }
    ColumnBuilder &margin(EdgeInsets m)
    {
        node->margin = m;
        return *this;
    }
    ColumnBuilder &padding(EdgeInsets p)
    {
        node->padding = p;
        return *this;
    }
    ColumnBuilder &mainAxisAlignment(MainAxisAlignment ma)
    {
        node->mainAlign = ma;
        return *this;
    }
    ColumnBuilder &crossAxisAlignment(CrossAxisAlignment ca)
    {
        node->crossAlign = ca;
        return *this;
    }
    ColumnBuilder &mainAxisSize(MainAxisSize ms)
    {
        node->mainAxisSize = ms;
        return *this;
    }
    ColumnBuilder &verticalDirection(VerticalDirection vd)
    {
        node->verticalDirection = vd;
        return *this;
    }

    template <typename... Children>
    ColumnBuilder &children(Children &&...children)
    {
        (
            [&] {
                Widget childWidget = toWidget(std::forward<Children>(children));
                node->addChild(std::move(childWidget)); // 使用 addChild
            }(),
            ...);
        return *this;
    }

    Widget build()
    {
        assert(node != nullptr);
        return std::move(node);
    }
};

class ExpandedBuilder
{
    std::unique_ptr<ExpandedNode> node;

  public:
    explicit ExpandedBuilder(int flex = 1, std::string name = "expanded")
        : node(std::make_unique<ExpandedNode>(flex, std::move(name)))
    {
    }
    ExpandedBuilder &flex(int f)
    {
        node->flex = f;
        return *this;
    }
    template <typename T>
    ExpandedBuilder &child(T &&child)
    {
        Widget childWidget = toWidget(std::forward<T>(child));
        node->addChild(std::move(childWidget)); // 使用 addChild
        return *this;
    }
    Widget build()
    {
        assert(node != nullptr);
        return std::move(node);
    }
};

// ============================================================================
// 顶层入口函数
// ============================================================================

inline ContainerBuilder Container(std::string name)
{
    return ContainerBuilder(std::move(name));
}
inline RowBuilder Row(std::string name)
{
    return RowBuilder(std::move(name));
}
inline ColumnBuilder Column(std::string name)
{
    return ColumnBuilder(std::move(name));
}
inline ExpandedBuilder Expanded(int flex = 1, std::string name = "expanded")
{
    return ExpandedBuilder(flex, std::move(name));
}

// ============================================================================
// 测试辅助函数
// ============================================================================

// 递归重置所有节点的 dirty 为 false
void resetDirtyRecursive(Node *node)
{
    if (!node)
        return;
    node->dirty = false;
    for (auto &child : node->children)
        resetDirtyRecursive(child.get());
}

// 收集所有后代节点（前序，不包括自身），并设置 dirty 标记
void collectDescendants(Node *node, std::vector<Node *> &out)
{
    if (!node)
        return;
    for (auto &child : node->children)
    {
        Node *childPtr = child.get();
        childPtr->dirty = true;
        out.push_back(childPtr);
        collectDescendants(childPtr, out);
    }
}

// 收集所有祖先节点（沿 parent 链向上，不包括自身），并设置 dirty 标记
void collectAncestors(Node *node, std::vector<Node *> &out)
{
    if (!node)
        return;
    Node *cur = node->parent;
    while (cur)
    {
        cur->dirty = true;
        out.push_back(cur);
        cur = cur->parent;
    }
}

// 比较节点名称向量与预期字符串向量是否一致
bool compareNodeNames(const std::vector<Node *> &nodes,
                      const std::vector<std::string> &expected, const char *testName)
{
    if (nodes.size() != expected.size())
    {
        std::cerr << "FAIL [" << testName << "]: size mismatch, expected "
                  << expected.size() << " but got " << nodes.size() << "\n";
        return false;
    }
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        if (nodes[i]->name != expected[i])
        {
            std::cerr << "FAIL [" << testName << "]: at index " << i << " expected '"
                      << expected[i] << "' but got '" << nodes[i]->name << "'\n";
            return false;
        }
    }
    return true;
}

// 检查给定节点向量中所有节点的 dirty 是否为 true
bool checkDirty(const std::vector<Node *> &nodes, const char *testName)
{
    for (Node *n : nodes)
    {
        if (!n->dirty)
        {
            std::cerr << "FAIL [" << testName << "]: node '" << n->name
                      << "' was not marked dirty\n";
            return false;
        }
    }
    return true;
}

// 检查给定节点向量中所有节点的 dirty 是否为 false
bool checkNotDirty(const std::vector<Node *> &nodes, const char *testName)
{
    for (Node *n : nodes)
    {
        if (n->dirty)
        {
            std::cerr << "FAIL [" << testName << "]: node '" << n->name
                      << "' should not be dirty\n";
            return false;
        }
    }
    return true;
}

// ============================================================================
// 测试用例（细分）
// ============================================================================

// 测试1: 验证 parent 指针正确设置
bool test_parent_pointers_set_correctly()
{
    auto root = Row("root")
                    .children(Column("child1").children(Container("grandchild1"),
                                                        Container("grandchild2")),
                              Row("child2").children(Container("grandchild3")))
                    .build();

    Node *rootNode = root.get();
    Node *child1 = rootNode->children[0].get();
    Node *child2 = rootNode->children[1].get();
    Node *grandchild1 = child1->children[0].get();
    Node *grandchild2 = child1->children[1].get();
    Node *grandchild3 = child2->children[0].get();

    bool ok = true;
    ok &= (child1->parent == rootNode);
    ok &= (child2->parent == rootNode);
    ok &= (grandchild1->parent == child1);
    ok &= (grandchild2->parent == child1);
    ok &= (grandchild3->parent == child2);
    ok &= (rootNode->parent == nullptr);

    if (!ok)
    {
        std::cerr << "FAIL [parent_pointers]: some parent pointers are incorrect\n";
        return false;
    }
    return true;
}

// 测试2: 从根节点向下收集所有后代（前序），顺序正确，脏标记正确
bool test_collect_descendants_from_root()
{
    auto root = Row("root")
                    .children(Column("child1").children(Container("grandchild1"),
                                                        Container("grandchild2")),
                              Row("child2").children(Container("grandchild3")))
                    .build();

    Node *rootNode = root.get();
    resetDirtyRecursive(rootNode);

    std::vector<Node *> descendants;
    collectDescendants(rootNode, descendants);

    std::vector<std::string> expected = {"child1", "grandchild1", "grandchild2", "child2",
                                         "grandchild3"};
    if (!compareNodeNames(descendants, expected, "collect_descendants_from_root"))
        return false;
    if (!checkDirty(descendants, "collect_descendants_from_root"))
        return false;

    // 根节点自身不应被设置 dirty
    if (rootNode->dirty)
    {
        std::cerr
            << "FAIL [collect_descendants_from_root]: root node should not be dirty\n";
        return false;
    }
    return true;
}

// 测试3: 从中间节点向下收集后代（child1）
bool test_collect_descendants_from_middle_node()
{
    auto root = Row("root")
                    .children(Column("child1").children(Container("grandchild1"),
                                                        Container("grandchild2")),
                              Row("child2").children(Container("grandchild3")))
                    .build();

    Node *rootNode = root.get();
    Node *child1 = rootNode->children[0].get();
    resetDirtyRecursive(rootNode);

    std::vector<Node *> descendants;
    collectDescendants(child1, descendants);

    std::vector<std::string> expected = {"grandchild1", "grandchild2"};
    if (!compareNodeNames(descendants, expected, "collect_descendants_from_middle"))
        return false;
    if (!checkDirty(descendants, "collect_descendants_from_middle"))
        return false;

    // child1 自身不应被设置 dirty
    if (child1->dirty)
    {
        std::cerr
            << "FAIL [collect_descendants_from_middle]: child1 should not be dirty\n";
        return false;
    }
    // 其他未访问节点（root, child2, grandchild3）不应 dirty
    std::vector<Node *> notVisited = {rootNode, rootNode->children[1].get(),
                                      rootNode->children[1]->children[0].get()};
    if (!checkNotDirty(notVisited, "collect_descendants_from_middle"))
        return false;
    return true;
}

// 测试4: 从叶子节点向下收集后代（应为空）
bool test_collect_descendants_from_leaf()
{
    auto root = Row("root")
                    .children(Column("child1").children(Container("grandchild1"),
                                                        Container("grandchild2")),
                              Row("child2").children(Container("grandchild3")))
                    .build();

    Node *rootNode = root.get();
    Node *grandchild3 = rootNode->children[1]->children[0].get();
    resetDirtyRecursive(rootNode);

    std::vector<Node *> descendants;
    collectDescendants(grandchild3, descendants);

    if (!descendants.empty())
    {
        std::cerr << "FAIL [collect_descendants_from_leaf]: expected empty, got "
                  << descendants.size() << " nodes\n";
        return false;
    }
    // 叶子节点自身不应被设置 dirty
    if (grandchild3->dirty)
    {
        std::cerr
            << "FAIL [collect_descendants_from_leaf]: leaf node should not be dirty\n";
        return false;
    }
    return true;
}

// 测试5: 从叶子节点向上收集所有祖先（grandchild2）
bool test_collect_ancestors_from_leaf()
{
    auto root = Row("root")
                    .children(Column("child1").children(Container("grandchild1"),
                                                        Container("grandchild2")),
                              Row("child2").children(Container("grandchild3")))
                    .build();

    Node *rootNode = root.get();
    Node *child1 = rootNode->children[0].get();
    Node *grandchild2 = child1->children[1].get();
    resetDirtyRecursive(rootNode);

    std::vector<Node *> ancestors;
    collectAncestors(grandchild2, ancestors);

    std::vector<std::string> expected = {"child1", "root"};
    if (!compareNodeNames(ancestors, expected, "collect_ancestors_from_leaf"))
        return false;
    if (!checkDirty(ancestors, "collect_ancestors_from_leaf"))
        return false;

    // 当前叶子节点自身不应被设置 dirty
    if (grandchild2->dirty)
    {
        std::cerr
            << "FAIL [collect_ancestors_from_leaf]: leaf node should not be dirty\n";
        return false;
    }
    return true;
}

// 测试6: 从根节点向上收集祖先（应为空）
bool test_collect_ancestors_from_root()
{
    auto root = Row("root").children(Container("child")).build();

    Node *rootNode = root.get();
    resetDirtyRecursive(rootNode);

    std::vector<Node *> ancestors;
    collectAncestors(rootNode, ancestors);

    if (!ancestors.empty())
    {
        std::cerr << "FAIL [collect_ancestors_from_root]: expected empty, got "
                  << ancestors.size() << " nodes\n";
        return false;
    }
    // 根节点自身不应被设置 dirty
    if (rootNode->dirty)
    {
        std::cerr << "FAIL [collect_ancestors_from_root]: root should not be dirty\n";
        return false;
    }
    return true;
}

// 测试7: 从中间节点向上收集祖先（child1）
bool test_collect_ancestors_from_middle_node()
{
    auto root =
        Row("root")
            .children(Column("child1").children(Container("grandchild1")), Row("child2"))
            .build();

    Node *rootNode = root.get();
    Node *child1 = rootNode->children[0].get();
    resetDirtyRecursive(rootNode);

    std::vector<Node *> ancestors;
    collectAncestors(child1, ancestors);

    std::vector<std::string> expected = {"root"};
    if (!compareNodeNames(ancestors, expected, "collect_ancestors_from_middle"))
        return false;
    if (!checkDirty(ancestors, "collect_ancestors_from_middle"))
        return false;

    // child1 自身不应被设置 dirty
    if (child1->dirty)
    {
        std::cerr << "FAIL [collect_ancestors_from_middle]: child1 should not be dirty\n";
        return false;
    }
    return true;
}

// 测试8: 节点移动后 parent 指针更新正确
bool test_move_node_updates_parent()
{
    auto root = Row("root")
                    .children(Column("childA").children(Container("grandchildA1"),
                                                        Container("grandchildA2")),
                              Row("childB").children(Container("grandchildB1")))
                    .build();

    Node *rootNode = root.get();
    Node *childA = rootNode->children[0].get();
    Node *childB = rootNode->children[1].get();
    Node *grandchildA2 = childA->children[1].get();

    // 移动 grandchildA2 从 childA 到 childB
    Node::moveNode(grandchildA2, childB);

    // 验证 parent 指针
    if (grandchildA2->parent != childB)
    {
        std::cerr << "FAIL [move_node]: grandchildA2 parent should be childB\n";
        return false;
    }

    // 验证 childA 不再包含 grandchildA2
    bool foundInA = false;
    for (auto &c : childA->children)
        if (c.get() == grandchildA2)
        {
            foundInA = true;
            break;
        }
    if (foundInA)
    {
        std::cerr << "FAIL [move_node]: grandchildA2 still in childA\n";
        return false;
    }

    // 验证 childB 包含 grandchildA2
    bool foundInB = false;
    for (auto &c : childB->children)
        if (c.get() == grandchildA2)
        {
            foundInB = true;
            break;
        }
    if (!foundInB)
    {
        std::cerr << "FAIL [move_node]: grandchildA2 not in childB\n";
        return false;
    }

    // 验证其他节点 parent 不受影响
    if (childA->parent != rootNode || childB->parent != rootNode)
    {
        std::cerr << "FAIL [move_node]: root children parent changed\n";
        return false;
    }

    // 移动后 grandchildA2 的子节点 parent 应该不变（这里它没有子节点）
    return true;
}

// ============================================================================
// 运行所有测试
// ============================================================================

bool runTests()
{
    return test_parent_pointers_set_correctly() && test_collect_descendants_from_root() &&
           test_collect_descendants_from_middle_node() &&
           test_collect_descendants_from_leaf() && test_collect_ancestors_from_leaf() &&
           test_collect_ancestors_from_root() &&
           test_collect_ancestors_from_middle_node() &&
           test_move_node_updates_parent(); // 新增移动测试
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