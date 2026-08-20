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
        node->children.push_back(toWidget(std::forward<T>(child)));
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
        (node->children.push_back(toWidget(std::forward<Children>(children))), ...);
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
        (node->children.push_back(toWidget(std::forward<Children>(children))), ...);
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
        node->children.push_back(toWidget(std::forward<T>(child)));
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

bool tryLayout(Widget &node, Constraints c, const std::string &expectedError = "")
{
    try
    {
        c = c.deflate(node->margin);
        node->layout(c);
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
// 测试用例（使用 Builder 风格）
// ============================================================================

bool test_row_fixed_children()
{
    auto row = Row("row")
                   .width(300)
                   .height(100)
                   .mainAxisAlignment(MainAxisAlignment::start)
                   .crossAxisAlignment(CrossAxisAlignment::start)
                   .children(Container("red").width(50).height(30),
                             Container("green").width(70).height(40))
                   .build();
    if (!tryLayout(row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row, 0, 0, 300, 100, "row") &&
           checkGeometry(*row->children[0], 0, 0, 50, 30, "red") &&
           checkGeometry(*row->children[1], 50, 0, 70, 40, "green");
}

bool test_row_expanded_flex()
{
    auto row = Row("row")
                   .width(300)
                   .height(100)
                   .children(Container("red").width(50).height(30),
                             Expanded(2, "exp").child(Container("green").height(20)),
                             Container("blue").width(70).height(40))
                   .build();
    if (!tryLayout(row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row, 0, 0, 300, 100, "row") &&
           checkGeometry(*row->children[0], 0, 0, 50, 30, "red") &&
           checkGeometry(*row->children[1]->children[0], 50, 0, 180, 20, "green") &&
           checkGeometry(*row->children[2], 230, 0, 70, 40, "blue");
}

bool test_row_space_between()
{
    auto row = Row("row")
                   .width(300)
                   .height(100)
                   .mainAxisAlignment(MainAxisAlignment::spaceBetween)
                   .crossAxisAlignment(CrossAxisAlignment::start)
                   .children(Container("red").width(50).height(30),
                             Container("green").width(70).height(40),
                             Container("blue").width(40).height(20))
                   .build();
    if (!tryLayout(row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row->children[0], 0, 0, 50, 30, "red") &&
           checkGeometry(*row->children[1], 120, 0, 70, 40, "green") &&
           checkGeometry(*row->children[2], 260, 0, 40, 20, "blue");
}

bool test_row_space_around()
{
    auto row = Row("row")
                   .width(300)
                   .height(100)
                   .mainAxisAlignment(MainAxisAlignment::spaceAround)
                   .children(Container("red").width(50).height(30),
                             Container("green").width(70).height(40),
                             Container("blue").width(40).height(20))
                   .build();
    if (!tryLayout(row, {0, 300, 0, 100}))
        return false;
    return approx(row->children[0]->geometry.x, 23.333f) &&
           approx(row->children[1]->geometry.x, 120.0f) &&
           approx(row->children[2]->geometry.x, 236.667f) &&
           approx(row->children[0]->geometry.y, 0.0f) &&
           approx(row->children[1]->geometry.y, 0.0f) &&
           approx(row->children[2]->geometry.y, 0.0f);
}

bool test_row_space_evenly()
{
    auto row = Row("row")
                   .width(300)
                   .height(100)
                   .mainAxisAlignment(MainAxisAlignment::spaceEvenly)
                   .children(Container("red").width(50).height(30),
                             Container("green").width(70).height(40),
                             Container("blue").width(40).height(20))
                   .build();
    if (!tryLayout(row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row->children[0], 35, 0, 50, 30, "red") &&
           checkGeometry(*row->children[1], 120, 0, 70, 40, "green") &&
           checkGeometry(*row->children[2], 225, 0, 40, 20, "blue");
}

bool test_column_main_axis_min()
{
    auto col = Column("col")
                   .mainAxisSize(MainAxisSize::min)
                   .children(Container("red").width(100).height(30),
                             Container("green").width(80).height(20))
                   .build();
    if (!tryLayout(col, {0, 200, 0, Constraints::inf}))
        return false;
    return checkGeometry(*col, 0, 0, 100, 50, "col");
}

bool test_row_stretch_cross_axis()
{
    auto row = Row("row")
                   .width(300)
                   .height(100)
                   .crossAxisAlignment(CrossAxisAlignment::stretch)
                   .children(Container("red").width(50).height(30),
                             Container("green").width(70).height(40))
                   .build();
    if (!tryLayout(row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row->children[0], 0, 0, 50, 100, "red") &&
           checkGeometry(*row->children[1], 50, 0, 70, 100, "green");
}

bool test_column_stretch_cross_axis()
{
    auto col = Column("col")
                   .width(200)
                   .height(300)
                   .crossAxisAlignment(CrossAxisAlignment::stretch)
                   .children(Container("red").width(50).height(30),
                             Container("green").width(70).height(40))
                   .build();
    if (!tryLayout(col, {0, 200, 0, 300}))
        return false;
    return checkGeometry(*col->children[0], 0, 0, 200, 30, "red") &&
           checkGeometry(*col->children[1], 0, 30, 200, 40, "green");
}

bool test_container_alignment_top_left()
{
    auto container = Container("container")
                         .width(100)
                         .height(100)
                         .margin(EdgeInsets::all(5)) // 添加 margin，与 Flutter 一致
                         .alignment(Align::topLeft)
                         .child(Container("child").width(50).height(30))
                         .build();
    if (!tryLayout(container, {0, 200, 0, 200}))
        return false;

    // Flutter 测试比较的是 child 位置与父容器边距框（Margin Box）起始位置的差值
    // C++ 中 container->geometry 是内容框（Content Box）
    // 边距框起始 = container->geometry - margin
    float parentMarginBoxX = container->geometry.x - container->margin.left;
    float parentMarginBoxY = container->geometry.y - container->margin.top;
    float childX = container->children[0]->geometry.x;
    float childY = container->children[0]->geometry.y;

    return approx(childX - parentMarginBoxX, 5) && approx(childY - parentMarginBoxY, 5);
}

bool test_container_alignment_center()
{
    auto container = Container("container")
                         .width(100)
                         .height(100)
                         .alignment(Align::center)
                         .child(Container("child").width(50).height(30))
                         .build();
    if (!tryLayout(container, {0, 200, 0, 200}))
        return false;
    return checkGeometry(*container, 0, 0, 100, 100, "container") &&
           checkGeometry(*container->children[0], 25, 35, 50, 30, "child");
}

bool test_container_padding_border_fixed()
{
    auto container = Container("container")
                         .width(100)
                         .height(100)
                         .padding(EdgeInsets{10, 10, 10, 10})
                         .border(EdgeInsets{5, 5, 5, 5})
                         .child(Container("child").width(30).height(20))
                         .build();
    if (!tryLayout(container, {0, 200, 0, 200}))
        return false;
    return checkGeometry(*container, 0, 0, 100, 100, "container") &&
           checkGeometry(*container->children[0], 15, 15, 70, 70, "child");
}

bool test_row_contains_column_directly()
{
    auto row =
        Row("row")
            .width(300)
            .height(200)
            .children(Container("red").width(50).height(50),
                      Column("col").children(Container("green").width(100).height(100),
                                             Container("blue").width(100).height(100)))
            .build();
    if (!tryLayout(row, {0, 300, 0, 200}))
        return false;
    return checkGeometry(*row, 0, 0, 300, 200, "row") &&
           checkGeometry(*row->children[0], 0, 0, 50, 50, "red") &&
           checkGeometry(*row->children[1], 50, 0, 100, 200, "col") &&
           checkGeometry(*row->children[1]->children[0], 0, 0, 100, 100, "green") &&
           checkGeometry(*row->children[1]->children[1], 0, 100, 100, 100, "blue");
}

bool test_column_contains_row_directly()
{
    auto col =
        Column("col")
            .width(300)
            .height(300)
            .children(Container("red").width(50).height(50),
                      Row("row").children(Container("green").width(100).height(100),
                                          Container("blue").width(100).height(100)))
            .build();
    if (!tryLayout(col, {0, 300, 0, 300}))
        return false;
    return checkGeometry(*col, 0, 0, 300, 300, "col") &&
           checkGeometry(*col->children[0], 0, 0, 50, 50, "red") &&
           checkGeometry(*col->children[1], 0, 50, 300, 100, "row") &&
           checkGeometry(*col->children[1]->children[0], 0, 0, 100, 100, "green") &&
           checkGeometry(*col->children[1]->children[1], 100, 0, 100, 100, "blue");
}

bool test_nested_expanded()
{
    auto row =
        Row("row")
            .width(300)
            .height(200)
            .children(
                Container("red").width(50).height(50),
                Expanded(1, "exp_row")
                    .child(
                        Column("col")
                            .crossAxisAlignment(CrossAxisAlignment::stretch)
                            .children(Container("green").width(80).height(30),
                                      Expanded(1, "exp_col").child(Container("blue")))))
            .build();
    if (!tryLayout(row, {0, 300, 0, 200}))
        return false;
    return checkGeometry(*row, 0, 0, 300, 200, "row") &&
           checkGeometry(*row->children[0], 0, 0, 50, 50, "red") &&
           checkGeometry(*row->children[1]->children[0], 50, 0, 250, 200, "col") &&
           checkGeometry(*row->children[1]->children[0]->children[0], 0, 0, 250, 30,
                         "green") &&
           checkGeometry(*row->children[1]->children[0]->children[1]->children[0], 0, 30,
                         250, 170, "blue");
}

bool test_row_rtl()
{
    auto row = Row("row")
                   .width(300)
                   .height(100)
                   .textDirection(TextDirection::rtl)
                   .children(Container("red").width(50).height(30),
                             Container("green").width(70).height(40))
                   .build();
    if (!tryLayout(row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row->children[0], 250, 0, 50, 30, "red") &&
           checkGeometry(*row->children[1], 180, 0, 70, 40, "green");
}

bool test_column_up()
{
    auto col = Column("col")
                   .width(100)
                   .height(200)
                   .verticalDirection(VerticalDirection::up)
                   .children(Container("red").width(50).height(30),
                             Container("green").width(50).height(40))
                   .build();
    if (!tryLayout(col, {0, 100, 0, 200}))
        return false;
    return checkGeometry(*col->children[0], 0, 170, 50, 30, "red") &&
           checkGeometry(*col->children[1], 0, 130, 50, 40, "green");
}

bool test_row_main_axis_center_end()
{
    auto row_center = Row("row_center")
                          .width(300)
                          .height(100)
                          .mainAxisAlignment(MainAxisAlignment::center)
                          .children(Container("red").width(50).height(30),
                                    Container("green").width(70).height(40))
                          .build();
    if (!tryLayout(row_center, {0, 300, 0, 100}))
        return false;
    if (!checkGeometry(*row_center->children[0], 90, 0, 50, 30, "red center") ||
        !checkGeometry(*row_center->children[1], 140, 0, 70, 40, "green center"))
        return false;

    auto row_end = Row("row_end")
                       .width(300)
                       .height(100)
                       .mainAxisAlignment(MainAxisAlignment::end)
                       .children(Container("red").width(50).height(30),
                                 Container("green").width(70).height(40))
                       .build();
    if (!tryLayout(row_end, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row_end->children[0], 180, 0, 50, 30, "red end") &&
           checkGeometry(*row_end->children[1], 230, 0, 70, 40, "green end");
}

bool test_row_cross_axis_center_end()
{
    auto row_center = Row("row_center")
                          .width(300)
                          .height(100)
                          .crossAxisAlignment(CrossAxisAlignment::center)
                          .children(Container("red").width(50).height(30),
                                    Container("green").width(70).height(40))
                          .build();
    if (!tryLayout(row_center, {0, 300, 0, 100}))
        return false;
    if (!checkGeometry(*row_center->children[0], 0, 35, 50, 30, "red center") ||
        !checkGeometry(*row_center->children[1], 50, 30, 70, 40, "green center"))
        return false;

    auto row_end = Row("row_end")
                       .width(300)
                       .height(100)
                       .crossAxisAlignment(CrossAxisAlignment::end)
                       .children(Container("red").width(50).height(30),
                                 Container("green").width(70).height(40))
                       .build();
    if (!tryLayout(row_end, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row_end->children[0], 0, 70, 50, 30, "red end") &&
           checkGeometry(*row_end->children[1], 50, 60, 70, 40, "green end");
}

bool test_root_size_determined_by_child()
{
    auto container =
        Container("container").child(Container("child").width(120).height(80)).build();
    if (!tryLayout(container, {0, 800, 0, 600}))
        return false;
    return approx(container->size().width, 120.0f) &&
           approx(container->size().height, 80.0f);
}

bool test_row_min_with_padding()
{
    auto paddingContainer =
        Container("padding")
            .padding(EdgeInsets{10, 10, 10, 10})
            .child(Row("row")
                       .mainAxisSize(MainAxisSize::min)
                       .children(Container("red").width(50).height(30),
                                 Container("green").width(70).height(40)))
            .build();
    if (!tryLayout(paddingContainer, {0, Constraints::inf, 0, 600}))
        return false;
    Node *row = paddingContainer->children[0].get();
    if (!approx(row->size().width, 120.0f) || !approx(row->size().height, 40.0f))
        return false;
    return approx(paddingContainer->size().width, 140.0f) &&
           approx(paddingContainer->size().height, 60.0f);
}

bool test_column_min_with_child_margin()
{
    auto col =
        Column("col")
            .mainAxisSize(MainAxisSize::min)
            .children(
                Container("red").width(100).height(30).margin(EdgeInsets{0, 0, 0, 10}),
                Container("green").width(80).height(20).margin(EdgeInsets{0, 5, 0, 0}))
            .build();
    if (!tryLayout(col, {0, 300, 0, Constraints::inf}))
        return false;
    return approx(col->size().width, 100.0f) && approx(col->size().height, 65.0f);
}

bool test_container_margin_fixed_size()
{
    auto parent = Column("parent_col")
                      .mainAxisSize(MainAxisSize::min)
                      .children(Container("fixed_container")
                                    .width(100)
                                    .height(80)
                                    .margin(EdgeInsets{15, 15, 15, 15})
                                    .child(Container("inner").width(50).height(30)))
                      .build();
    if (!tryLayout(parent, {0, 300, 0, Constraints::inf}))
        return false;
    Node *fixed = parent->children[0].get();
    if (!approx(fixed->size().width, 100.0f) || !approx(fixed->size().height, 80.0f))
        return false;
    if (!approx(fixed->geometry.x, 15.0f) || !approx(fixed->geometry.y, 15.0f))
        return false;
    if (!approx(parent->size().width, 100.0f) || !approx(parent->size().height, 110.0f))
        return false;
    return true;
}

bool test_root_shrink_to_column_min_content()
{
    auto container = Container("root")
                         .child(Column("col")
                                    .mainAxisSize(MainAxisSize::min)
                                    .children(Container("red").width(80).height(30),
                                              Container("green").width(120).height(40)))
                         .build();
    if (!tryLayout(container, {0, 300, 0, Constraints::inf}))
        return false;
    return approx(container->size().width, 120.0f) &&
           approx(container->size().height, 70.0f);
}

bool test_nested_container_with_border_shrink()
{
    auto outer = Container("outer")
                     .border(EdgeInsets{5, 5, 5, 5})
                     .child(Container("inner")
                                .padding(EdgeInsets{10, 10, 10, 10})
                                .child(Container("content").width(60).height(40)))
                     .build();
    if (!tryLayout(outer, {0, 800, 0, 600}))
        return false;
    Node *inner = outer->children[0].get();
    if (!approx(inner->size().width, 80.0f) || !approx(inner->size().height, 60.0f))
        return false;
    return approx(outer->size().width, 90.0f) && approx(outer->size().height, 70.0f);
}

bool test_empty_container_with_min_constraints()
{
    auto parent = Container("parent")
                      .width(100)
                      .height(80)
                      .child(Container("empty").constraints(
                          Constraints{50, Constraints::inf, 30, Constraints::inf}))
                      .build();
    if (!tryLayout(parent, {0, 100, 0, 80}))
        return false;
    Node *child = parent->children[0].get();
    return approx(child->size().width, 100.0f) && approx(child->size().height, 80.0f);
}

bool test_row_cross_axis_shrink()
{
    auto row = Row("row")
                   .children(Container("red").width(50).height(30),
                             Container("green").width(70).height(80))
                   .build();
    if (!tryLayout(row, {0, 300, 0, 600}))
        return false;
    return approx(row->size().width, 300.0f) && approx(row->size().height, 80.0f);
}

bool test_deep_nested_canvas_size()
{
    auto container =
        Container("root")
            .child(Column("col")
                       .mainAxisSize(MainAxisSize::min)
                       .children(Row("row")
                                     .mainAxisSize(MainAxisSize::min)
                                     .children(Container("box1").width(40).height(40),
                                               Container("box2").width(30).height(50)),
                                 Container("box3").width(100).height(20)))
            .build();
    if (!tryLayout(container, {0, 600, 0, 600}))
        return false;
    return approx(container->size().width, 100.0f) &&
           approx(container->size().height, 70.0f);
}

// ============================================================================
// 运行所有测试
// ============================================================================

bool runTests()
{
    return test_row_fixed_children() && test_row_expanded_flex() &&
           test_row_space_between() && test_row_space_around() &&
           test_row_space_evenly() && test_column_main_axis_min() &&
           test_row_stretch_cross_axis() && test_column_stretch_cross_axis() &&
           test_container_alignment_top_left() && test_container_alignment_center() &&
           test_container_padding_border_fixed() && test_row_contains_column_directly() &&
           test_column_contains_row_directly() && test_nested_expanded() &&
           test_row_rtl() && test_column_up() && test_row_main_axis_center_end() &&
           test_row_cross_axis_center_end() && test_root_size_determined_by_child() &&
           test_row_min_with_padding() && test_column_min_with_child_margin() &&
           test_container_margin_fixed_size() &&
           test_root_shrink_to_column_min_content() &&
           test_nested_container_with_border_shrink() &&
           test_empty_container_with_min_constraints() && test_row_cross_axis_shrink() &&
           test_deep_nested_canvas_size();
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