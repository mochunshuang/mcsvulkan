// 原理: https://docs.flutter.dev/ui/layout/constraints
//      https://docs.flutter.dev/ui/layout#overview
// 概念：约束向下传递，尺寸向上传递，父节点决定子节点位置。
// 本实现将 Flutter 布局原则映射为 C++ 代码，所有行为均与官方一致。

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
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
    explicit Node(std::string name) : name(std::move(name)) {}
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

    // 遍历 API（可保留，但测试中未使用）
    void visit(const std::function<void(Node &)> &fn)
    {
        fn(*this);
        for (auto &child : children)
            child->visit(fn);
    }

    void visitConst(const std::function<void(const Node &)> &fn) const
    {
        fn(*this);
        for (const auto &child : children)
            child->visitConst(fn);
    }

    Node *findByName(const std::string &target)
    {
        if (name == target)
            return this;
        for (auto &child : children)
            if (Node *found = child->findByName(target))
                return found;
        return nullptr;
    }

    const Node *findByName(const std::string &target) const
    {
        if (name == target)
            return this;
        for (const auto &child : children)
            if (const Node *found = child->findByName(target))
                return found;
        return nullptr;
    }

    bool hasChildren() const
    {
        return !children.empty();
    }
    size_t childCount() const
    {
        return children.size();
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
// 辅助函数：构建子节点列表（支持声明式）
// ============================================================================
template <typename... Children>
std::vector<std::unique_ptr<Node>> makeChildren(Children &&...children)
{
    std::vector<std::unique_ptr<Node>> vec;
    (vec.push_back(std::forward<Children>(children)), ...);
    return vec;
}

// ============================================================================
// Expanded 节点（提前定义，供 FlexNode 使用 dynamic_cast）
// ============================================================================

class ExpandedNode : public Node
{
  public:
    explicit ExpandedNode(std::string name, int flex = 1)
        : Node(std::move(name)), flex(flex)
    {
    }

    explicit ExpandedNode(std::vector<std::unique_ptr<Node>> children, std::string name,
                          int flex = 1)
        : Node(std::move(name)), flex(flex)
    {
        this->children = std::move(children);
    }

    int flex = 1;

    void performLayout(Constraints borderBC) override
    {
        (void)borderBC;
        throw std::logic_error(
            "Expanded widget must be placed directly inside Row/Column/Flex.");
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
        : Node(std::move(name))
    {
        fixedWidth = width;
        fixedHeight = height;
        this->margin = margin;
        this->padding = padding;
        this->border = border;
        this->alignment = alignment;
        ownConstraints = constraints;
    }

    explicit ContainerNode(std::vector<std::unique_ptr<Node>> children, std::string name,
                           std::optional<float> width = std::nullopt,
                           std::optional<float> height = std::nullopt,
                           EdgeInsets margin = {}, EdgeInsets padding = {},
                           EdgeInsets border = {},
                           std::optional<Alignment> alignment = std::nullopt,
                           std::optional<Constraints> constraints = std::nullopt)
        : Node(std::move(name))
    {
        fixedWidth = width;
        fixedHeight = height;
        this->margin = margin;
        this->padding = padding;
        this->border = border;
        this->alignment = alignment;
        ownConstraints = constraints;
        this->children = std::move(children);
    }

    std::optional<Alignment> alignment;
    std::optional<Constraints> ownConstraints;
    EdgeInsets border;

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
    explicit FlexNode(std::string name) : Node(std::move(name)) {}

    MainAxisAlignment mainAlign = MainAxisAlignment::start;
    CrossAxisAlignment crossAlign = CrossAxisAlignment::start;
    MainAxisSize mainAxisSize = MainAxisSize::max;
    TextDirection textDirection = TextDirection::ltr;
    VerticalDirection verticalDirection = VerticalDirection::down;

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
                if (dynamic_cast<ExpandedNode *>(child.get()))
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

        // ========== 修正交叉轴尺寸计算（关键） ==========
        Size size;
        if (isRow_)
        {
            size.width = finalInnerMain + padding.horizontal();
            if (crossAlign == CrossAxisAlignment::stretch)
            {
                size.height = innerCross + padding.vertical();
            }
            else
            {
                float maxChildHeight = 0;
                for (const auto &info : infos)
                {
                    maxChildHeight = std::max(maxChildHeight, info.node->geometry.h);
                }
                size.height = maxChildHeight + padding.vertical();
            }
        }
        else
        {
            if (crossAlign == CrossAxisAlignment::stretch)
            {
                size.width = innerCross + padding.horizontal();
            }
            else
            {
                float maxChildWidth = 0;
                for (const auto &info : infos)
                {
                    maxChildWidth = std::max(maxChildWidth, info.node->geometry.w);
                }
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
    explicit RowNode(std::string name, std::optional<float> w = std::nullopt,
                     std::optional<float> h = std::nullopt,
                     MainAxisAlignment ma = MainAxisAlignment::start,
                     CrossAxisAlignment ca = CrossAxisAlignment::start,
                     MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {},
                     EdgeInsets p = {}, TextDirection td = TextDirection::ltr)
        : FlexNode<true>(std::move(name))
    {
        fixedWidth = w;
        fixedHeight = h;
        margin = m;
        padding = p;
        mainAlign = ma;
        crossAlign = ca;
        mainAxisSize = ms;
        textDirection = td;
    }

    explicit RowNode(std::vector<std::unique_ptr<Node>> children, std::string name,
                     std::optional<float> w = std::nullopt,
                     std::optional<float> h = std::nullopt,
                     MainAxisAlignment ma = MainAxisAlignment::start,
                     CrossAxisAlignment ca = CrossAxisAlignment::start,
                     MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {},
                     EdgeInsets p = {}, TextDirection td = TextDirection::ltr)
        : FlexNode<true>(std::move(name))
    {
        fixedWidth = w;
        fixedHeight = h;
        margin = m;
        padding = p;
        mainAlign = ma;
        crossAlign = ca;
        mainAxisSize = ms;
        textDirection = td;
        this->children = std::move(children);
    }
};

class ColumnNode : public FlexNode<false>
{
  public:
    explicit ColumnNode(std::string name, std::optional<float> w = std::nullopt,
                        std::optional<float> h = std::nullopt,
                        MainAxisAlignment ma = MainAxisAlignment::start,
                        CrossAxisAlignment ca = CrossAxisAlignment::start,
                        MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {},
                        EdgeInsets p = {}, VerticalDirection vd = VerticalDirection::down)
        : FlexNode<false>(std::move(name))
    {
        fixedWidth = w;
        fixedHeight = h;
        margin = m;
        padding = p;
        mainAlign = ma;
        crossAlign = ca;
        mainAxisSize = ms;
        verticalDirection = vd;
    }

    explicit ColumnNode(std::vector<std::unique_ptr<Node>> children, std::string name,
                        std::optional<float> w = std::nullopt,
                        std::optional<float> h = std::nullopt,
                        MainAxisAlignment ma = MainAxisAlignment::start,
                        CrossAxisAlignment ca = CrossAxisAlignment::start,
                        MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {},
                        EdgeInsets p = {}, VerticalDirection vd = VerticalDirection::down)
        : FlexNode<false>(std::move(name))
    {
        fixedWidth = w;
        fixedHeight = h;
        margin = m;
        padding = p;
        mainAlign = ma;
        crossAlign = ca;
        mainAxisSize = ms;
        verticalDirection = vd;
        this->children = std::move(children);
    }
};

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
        c = c.deflate(node.margin);
        node.layout(c);
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
// 新测试用例（与 Flutter 测试顺序一致）
// ============================================================================

// 1. Row fixed children
bool test_row_fixed_children()
{
    auto row = std::make_unique<RowNode>(
        makeChildren(std::make_unique<ContainerNode>("red", 50.0f, 30.0f),
                     std::make_unique<ContainerNode>("green", 70.0f, 40.0f)),
        "row", 300.0f, 100.0f, MainAxisAlignment::start, CrossAxisAlignment::start);
    if (!tryLayout(*row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row, 0, 0, 300, 100, "row") &&
           checkGeometry(*row->children[0], 0, 0, 50, 30, "red") &&
           checkGeometry(*row->children[1], 50, 0, 70, 40, "green");
}

// 2. Row with Expanded flex distribution
bool test_row_expanded_flex()
{
    auto row = std::make_unique<RowNode>(
        makeChildren(
            std::make_unique<ContainerNode>("red", 50.0f, 30.0f),
            std::make_unique<ExpandedNode>(makeChildren(std::make_unique<ContainerNode>(
                                               "green", std::nullopt, 20.0f)),
                                           "exp", 2),
            std::make_unique<ContainerNode>("blue", 70.0f, 40.0f)),
        "row", 300.0f, 100.0f, MainAxisAlignment::start, CrossAxisAlignment::start);
    if (!tryLayout(*row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row, 0, 0, 300, 100, "row") &&
           checkGeometry(*row->children[0], 0, 0, 50, 30, "red") &&
           checkGeometry(*row->children[1]->children[0], 50, 0, 180, 20, "green") &&
           checkGeometry(*row->children[2], 230, 0, 70, 40, "blue");
}

// 3. Row spaceBetween
bool test_row_space_between()
{
    auto row = std::make_unique<RowNode>(
        makeChildren(std::make_unique<ContainerNode>("red", 50.0f, 30.0f),
                     std::make_unique<ContainerNode>("green", 70.0f, 40.0f),
                     std::make_unique<ContainerNode>("blue", 40.0f, 20.0f)),
        "row", 300.0f, 100.0f, MainAxisAlignment::spaceBetween,
        CrossAxisAlignment::start);
    if (!tryLayout(*row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row->children[0], 0, 0, 50, 30, "red") &&
           checkGeometry(*row->children[1], 120, 0, 70, 40, "green") &&
           checkGeometry(*row->children[2], 260, 0, 40, 20, "blue");
}

// 4. Row spaceAround (tolerance)
bool test_row_space_around()
{
    auto row = std::make_unique<RowNode>(
        makeChildren(std::make_unique<ContainerNode>("red", 50.0f, 30.0f),
                     std::make_unique<ContainerNode>("green", 70.0f, 40.0f),
                     std::make_unique<ContainerNode>("blue", 40.0f, 20.0f)),
        "row", 300.0f, 100.0f, MainAxisAlignment::spaceAround, CrossAxisAlignment::start);
    if (!tryLayout(*row, {0, 300, 0, 100}))
        return false;
    return approx(row->children[0]->geometry.x, 23.333f) &&
           approx(row->children[1]->geometry.x, 120.0f) &&
           approx(row->children[2]->geometry.x, 236.667f) &&
           approx(row->children[0]->geometry.y, 0.0f) &&
           approx(row->children[1]->geometry.y, 0.0f) &&
           approx(row->children[2]->geometry.y, 0.0f);
}

// 5. Row spaceEvenly
bool test_row_space_evenly()
{
    auto row = std::make_unique<RowNode>(
        makeChildren(std::make_unique<ContainerNode>("red", 50.0f, 30.0f),
                     std::make_unique<ContainerNode>("green", 70.0f, 40.0f),
                     std::make_unique<ContainerNode>("blue", 40.0f, 20.0f)),
        "row", 300.0f, 100.0f, MainAxisAlignment::spaceEvenly, CrossAxisAlignment::start);
    if (!tryLayout(*row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row->children[0], 35, 0, 50, 30, "red") &&
           checkGeometry(*row->children[1], 120, 0, 70, 40, "green") &&
           checkGeometry(*row->children[2], 225, 0, 40, 20, "blue");
}

// 6. Column mainAxisSize.min
bool test_column_main_axis_min()
{
    auto col = std::make_unique<ColumnNode>(
        makeChildren(std::make_unique<ContainerNode>("red", 100.0f, 30.0f),
                     std::make_unique<ContainerNode>("green", 80.0f, 20.0f)),
        "col", std::nullopt, std::nullopt, // 宽度不固定，由子项决定
        MainAxisAlignment::start, CrossAxisAlignment::start, MainAxisSize::min);

    if (!tryLayout(*col, {0, 200, 0, Constraints::inf}))
        return false;
    return checkGeometry(*col, 0, 0, 100, 50, "col");
}

// 7. Row crossAxisAlignment.stretch
bool test_row_stretch_cross_axis()
{
    auto row = std::make_unique<RowNode>(
        makeChildren(std::make_unique<ContainerNode>("red", 50.0f, 30.0f),
                     std::make_unique<ContainerNode>("green", 70.0f, 40.0f)),
        "row", 300.0f, 100.0f, MainAxisAlignment::start, CrossAxisAlignment::stretch);
    if (!tryLayout(*row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row->children[0], 0, 0, 50, 100, "red") &&
           checkGeometry(*row->children[1], 50, 0, 70, 100, "green");
}

// 8. Column crossAxisAlignment.stretch
bool test_column_stretch_cross_axis()
{
    auto col = std::make_unique<ColumnNode>(
        makeChildren(std::make_unique<ContainerNode>("red", 50.0f, 30.0f),
                     std::make_unique<ContainerNode>("green", 70.0f, 40.0f)),
        "col", 200.0f, 300.0f, MainAxisAlignment::start, CrossAxisAlignment::stretch);
    if (!tryLayout(*col, {0, 200, 0, 300}))
        return false;
    return checkGeometry(*col->children[0], 0, 0, 200, 30, "red") &&
           checkGeometry(*col->children[1], 0, 30, 200, 40, "green");
}

// 9. Container alignment topLeft (no margin for simplicity)
bool test_container_alignment_top_left()
{
    auto container = std::make_unique<ContainerNode>(
        makeChildren(std::make_unique<ContainerNode>("child", 50.0f, 30.0f)), "container",
        100.0f, 100.0f, EdgeInsets{}, EdgeInsets{}, EdgeInsets{}, Align::topLeft);
    if (!tryLayout(*container, {0, 200, 0, 200}))
        return false;
    return checkGeometry(*container, 0, 0, 100, 100, "container") &&
           checkGeometry(*container->children[0], 0, 0, 50, 30, "child");
}

// 10. Container alignment center
bool test_container_alignment_center()
{
    auto container = std::make_unique<ContainerNode>(
        makeChildren(std::make_unique<ContainerNode>("child", 50.0f, 30.0f)), "container",
        100.0f, 100.0f, EdgeInsets{}, EdgeInsets{}, EdgeInsets{}, Align::center);
    if (!tryLayout(*container, {0, 200, 0, 200}))
        return false;
    return checkGeometry(*container, 0, 0, 100, 100, "container") &&
           checkGeometry(*container->children[0], 25, 35, 50, 30, "child");
}

// 11. Container padding border fixed size
bool test_container_padding_border_fixed()
{
    auto container = std::make_unique<ContainerNode>(
        makeChildren(std::make_unique<ContainerNode>("child", 30.0f, 20.0f)), "container",
        100.0f, 100.0f, EdgeInsets{}, EdgeInsets{10, 10, 10, 10}, EdgeInsets{5, 5, 5, 5},
        std::nullopt);
    if (!tryLayout(*container, {0, 200, 0, 200}))
        return false;
    return checkGeometry(*container, 0, 0, 100, 100, "container") &&
           checkGeometry(*container->children[0], 15, 15, 70, 70, "child");
}

// 12. Row contains Column directly
bool test_row_contains_column_directly()
{
    auto row = std::make_unique<RowNode>(
        makeChildren(
            std::make_unique<ContainerNode>("red", 50.0f, 50.0f),
            std::make_unique<ColumnNode>(
                makeChildren(std::make_unique<ContainerNode>("green", 100.0f, 100.0f),
                             std::make_unique<ContainerNode>("blue", 100.0f, 100.0f)),
                "col", std::nullopt, std::nullopt, MainAxisAlignment::start,
                CrossAxisAlignment::start, MainAxisSize::max, EdgeInsets{},
                EdgeInsets{})),
        "row", 300.0f, 200.0f, MainAxisAlignment::start, CrossAxisAlignment::start);
    if (!tryLayout(*row, {0, 300, 0, 200}))
        return false;
    return checkGeometry(*row, 0, 0, 300, 200, "row") &&
           checkGeometry(*row->children[0], 0, 0, 50, 50, "red") &&
           checkGeometry(*row->children[1], 50, 0, 100, 200, "col") &&
           checkGeometry(*row->children[1]->children[0], 0, 0, 100, 100, "green") &&
           checkGeometry(*row->children[1]->children[1], 0, 100, 100, 100, "blue");
}

// 13. Column contains Row directly
bool test_column_contains_row_directly()
{
    auto col = std::make_unique<ColumnNode>(
        makeChildren(
            std::make_unique<ContainerNode>("red", 50.0f, 50.0f),
            std::make_unique<RowNode>(
                makeChildren(std::make_unique<ContainerNode>("green", 100.0f, 100.0f),
                             std::make_unique<ContainerNode>("blue", 100.0f, 100.0f)),
                "row", std::nullopt, std::nullopt, MainAxisAlignment::start,
                CrossAxisAlignment::start, MainAxisSize::max, EdgeInsets{},
                EdgeInsets{})),
        "col", 300.0f, 300.0f, MainAxisAlignment::start, CrossAxisAlignment::start);
    if (!tryLayout(*col, {0, 300, 0, 300}))
        return false;
    return checkGeometry(*col, 0, 0, 300, 300, "col") &&
           checkGeometry(*col->children[0], 0, 0, 50, 50, "red") &&
           checkGeometry(*col->children[1], 0, 50, 300, 100, "row") &&
           checkGeometry(*col->children[1]->children[0], 0, 0, 100, 100, "green") &&
           checkGeometry(*col->children[1]->children[1], 100, 0, 100, 100, "blue");
}

// 14. Nested Expanded
bool test_nested_expanded()
{
    auto row = std::make_unique<RowNode>(
        makeChildren(
            std::make_unique<ContainerNode>("red", 50.0f, 50.0f),
            std::make_unique<ExpandedNode>(
                makeChildren(std::make_unique<ColumnNode>(
                    makeChildren(std::make_unique<ContainerNode>("green", 80.0f, 30.0f),
                                 std::make_unique<ExpandedNode>(
                                     makeChildren(std::make_unique<ContainerNode>(
                                         "blue", std::nullopt, std::nullopt)),
                                     "exp_col", 1)),
                    "col", std::nullopt, std::nullopt, MainAxisAlignment::start,
                    CrossAxisAlignment::stretch, MainAxisSize::max, EdgeInsets{},
                    EdgeInsets{})),
                "exp_row", 1)),
        "row", 300.0f, 200.0f);
    if (!tryLayout(*row, {0, 300, 0, 200}))
        return false;
    return checkGeometry(*row, 0, 0, 300, 200, "row") &&
           checkGeometry(*row->children[0], 0, 0, 50, 50, "red") &&
           checkGeometry(*row->children[1]->children[0], 50, 0, 250, 200, "col") &&
           checkGeometry(*row->children[1]->children[0]->children[0], 0, 0, 250, 30,
                         "green") &&
           checkGeometry(*row->children[1]->children[0]->children[1]->children[0], 0, 30,
                         250, 170, "blue");
}

// 15. Row RTL
bool test_row_rtl()
{
    auto row = std::make_unique<RowNode>(
        makeChildren(std::make_unique<ContainerNode>("red", 50.0f, 30.0f),
                     std::make_unique<ContainerNode>("green", 70.0f, 40.0f)),
        "row", 300.0f, 100.0f, MainAxisAlignment::start, CrossAxisAlignment::start,
        MainAxisSize::max, EdgeInsets{}, EdgeInsets{}, TextDirection::rtl);
    if (!tryLayout(*row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row->children[0], 250, 0, 50, 30, "red") &&
           checkGeometry(*row->children[1], 180, 0, 70, 40, "green");
}

// 16. Column up
bool test_column_up()
{
    auto col = std::make_unique<ColumnNode>(
        makeChildren(std::make_unique<ContainerNode>("red", 50.0f, 30.0f),
                     std::make_unique<ContainerNode>("green", 50.0f, 40.0f)),
        "col", 100.0f, 200.0f, MainAxisAlignment::start, CrossAxisAlignment::start,
        MainAxisSize::max, EdgeInsets{}, EdgeInsets{}, VerticalDirection::up);
    if (!tryLayout(*col, {0, 100, 0, 200}))
        return false;
    return checkGeometry(*col->children[0], 0, 170, 50, 30, "red") &&
           checkGeometry(*col->children[1], 0, 130, 50, 40, "green");
}

// 17. Row mainAxisAlignment center/end
bool test_row_main_axis_center_end()
{
    // center
    auto row_center = std::make_unique<RowNode>(
        makeChildren(std::make_unique<ContainerNode>("red", 50.0f, 30.0f),
                     std::make_unique<ContainerNode>("green", 70.0f, 40.0f)),
        "row_center", 300.0f, 100.0f, MainAxisAlignment::center,
        CrossAxisAlignment::start);
    if (!tryLayout(*row_center, {0, 300, 0, 100}))
        return false;
    if (!checkGeometry(*row_center->children[0], 90, 0, 50, 30, "red center") ||
        !checkGeometry(*row_center->children[1], 140, 0, 70, 40, "green center"))
        return false;

    // end
    auto row_end = std::make_unique<RowNode>(
        makeChildren(std::make_unique<ContainerNode>("red", 50.0f, 30.0f),
                     std::make_unique<ContainerNode>("green", 70.0f, 40.0f)),
        "row_end", 300.0f, 100.0f, MainAxisAlignment::end, CrossAxisAlignment::start);
    if (!tryLayout(*row_end, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row_end->children[0], 180, 0, 50, 30, "red end") &&
           checkGeometry(*row_end->children[1], 230, 0, 70, 40, "green end");
}

// 18. Row crossAxisAlignment center/end
bool test_row_cross_axis_center_end()
{
    // center
    auto row_center = std::make_unique<RowNode>(
        makeChildren(std::make_unique<ContainerNode>("red", 50.0f, 30.0f),
                     std::make_unique<ContainerNode>("green", 70.0f, 40.0f)),
        "row_center", 300.0f, 100.0f, MainAxisAlignment::start,
        CrossAxisAlignment::center);
    if (!tryLayout(*row_center, {0, 300, 0, 100}))
        return false;
    if (!checkGeometry(*row_center->children[0], 0, 35, 50, 30, "red center") ||
        !checkGeometry(*row_center->children[1], 50, 30, 70, 40, "green center"))
        return false;

    // end
    auto row_end = std::make_unique<RowNode>(
        makeChildren(std::make_unique<ContainerNode>("red", 50.0f, 30.0f),
                     std::make_unique<ContainerNode>("green", 70.0f, 40.0f)),
        "row_end", 300.0f, 100.0f, MainAxisAlignment::start, CrossAxisAlignment::end);
    if (!tryLayout(*row_end, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row_end->children[0], 0, 70, 50, 30, "red end") &&
           checkGeometry(*row_end->children[1], 50, 60, 70, 40, "green end");
}
// ============================================================================
// 19. 根节点无固定尺寸，子项决定画布大小
// ============================================================================
bool test_root_size_determined_by_child()
{
    auto container = std::make_unique<ContainerNode>(
        makeChildren(std::make_unique<ContainerNode>("child", 120.0f, 80.0f)),
        "container", std::nullopt, std::nullopt);
    if (!tryLayout(*container, {0, 800, 0, 600}))
        return false;
    // Flutter 断言：containerBox.size == Size(120, 80)
    return approx(container->size().width, 120.0f) &&
           approx(container->size().height, 80.0f);
}

// ============================================================================
// 20. Row MainAxisSize.min 带 padding（用 Container 模拟 Padding）
// ============================================================================
bool test_row_min_with_padding()
{
    auto paddingContainer = std::make_unique<ContainerNode>(
        makeChildren(std::make_unique<RowNode>(
            makeChildren(std::make_unique<ContainerNode>("red", 50.0f, 30.0f),
                         std::make_unique<ContainerNode>("green", 70.0f, 40.0f)),
            "row", std::nullopt, std::nullopt, MainAxisAlignment::start,
            CrossAxisAlignment::start, MainAxisSize::min)),
        "padding", std::nullopt, std::nullopt, EdgeInsets{}, EdgeInsets{10, 10, 10, 10});

    if (!tryLayout(*paddingContainer, {0, Constraints::inf, 0, 600}))
        return false;

    Node *row = paddingContainer->children[0].get();
    // Flutter 断言：rowBox.size == Size(120, 40)
    if (!approx(row->size().width, 120.0f) || !approx(row->size().height, 40.0f))
        return false;
    // Flutter 断言：paddingBox.size == Size(140, 60)
    return approx(paddingContainer->size().width, 140.0f) &&
           approx(paddingContainer->size().height, 60.0f);
}

// ============================================================================
// 21. Column MainAxisSize.min 且子项有 margin
// ============================================================================
bool test_column_min_with_child_margin()
{
    auto col = std::make_unique<ColumnNode>(
        makeChildren(
            std::make_unique<ContainerNode>("red", 100.0f, 30.0f,
                                            EdgeInsets{0, 0, 0, 10}), // bottom margin 10
            std::make_unique<ContainerNode>("green", 80.0f, 20.0f,
                                            EdgeInsets{0, 5, 0, 0}) // top margin 5
            ),
        "col", std::nullopt, std::nullopt, MainAxisAlignment::start,
        CrossAxisAlignment::start, MainAxisSize::min);

    if (!tryLayout(*col, {0, 300, 0, Constraints::inf}))
        return false;
    // Flutter 断言：columnBox.size == Size(100, 65)
    return approx(col->size().width, 100.0f) && approx(col->size().height, 65.0f);
}

// 22. Container margin fixed size（真正使用 margin）
bool test_container_margin_fixed_size()
{
    auto parent = std::make_unique<ColumnNode>(
        makeChildren(std::make_unique<ContainerNode>(
            makeChildren(std::make_unique<ContainerNode>("inner", 50.0f, 30.0f)),
            "fixed_container", 100.0f, 80.0f, EdgeInsets{15, 15, 15, 15} // margin 15
            )),
        "parent_col", std::nullopt, std::nullopt, MainAxisAlignment::start,
        CrossAxisAlignment::start, MainAxisSize::min);

    if (!tryLayout(*parent, {0, 300, 0, Constraints::inf}))
        return false;

    Node *fixed = parent->children[0].get();
    if (!approx(fixed->size().width, 100.0f) || !approx(fixed->size().height, 80.0f))
        return false;
    if (!approx(fixed->geometry.x, 15.0f) || !approx(fixed->geometry.y, 15.0f))
        return false;

    // 父容器宽度 = 子项宽度 100（不含水平 margin），高度 = 子项高度 80 + 垂直 margin 30 = 110
    if (!approx(parent->size().width, 100.0f) || !approx(parent->size().height, 110.0f))
        return false;

    return true;
}
// ============================================================================
// 23. 根容器收缩到 Column min 内容
// ============================================================================
bool test_root_shrink_to_column_min_content()
{
    auto container = std::make_unique<ContainerNode>(
        makeChildren(std::make_unique<ColumnNode>(
            makeChildren(std::make_unique<ContainerNode>("red", 80.0f, 30.0f),
                         std::make_unique<ContainerNode>("green", 120.0f, 40.0f)),
            "col", std::nullopt, std::nullopt, MainAxisAlignment::start,
            CrossAxisAlignment::start, MainAxisSize::min)),
        "root", std::nullopt, std::nullopt);

    if (!tryLayout(*container, {0, 300, 0, Constraints::inf}))
        return false;
    // Flutter 断言：containerBox.size == Size(120, 70)
    return approx(container->size().width, 120.0f) &&
           approx(container->size().height, 70.0f);
}

// ============================================================================
// 24. 嵌套容器带 border 收缩
// ============================================================================
bool test_nested_container_with_border_shrink()
{
    auto outer = std::make_unique<ContainerNode>(
        makeChildren(std::make_unique<ContainerNode>(
            makeChildren(std::make_unique<ContainerNode>("content", 60.0f, 40.0f)),
            "inner", std::nullopt, std::nullopt, EdgeInsets{},
            EdgeInsets{10, 10, 10, 10} // padding 10
            )),
        "outer", std::nullopt, std::nullopt, EdgeInsets{}, EdgeInsets{},
        EdgeInsets{5, 5, 5, 5} // border 5
    );

    if (!tryLayout(*outer, {0, 800, 0, 600}))
        return false;

    Node *inner = outer->children[0].get();
    // Flutter 断言：innerBox.size == Size(80, 60)
    if (!approx(inner->size().width, 80.0f) || !approx(inner->size().height, 60.0f))
        return false;
    // Flutter 断言：outerBox.size == Size(90, 70)
    return approx(outer->size().width, 90.0f) && approx(outer->size().height, 70.0f);
}

// ============================================================================
// 25. 空 Container 带 min constraints 放入固定父容器
// ============================================================================
bool test_empty_container_with_min_constraints()
{
    auto parent = std::make_unique<ContainerNode>(
        makeChildren(std::make_unique<ContainerNode>(
            "empty", std::nullopt, std::nullopt, EdgeInsets{}, EdgeInsets{}, EdgeInsets{},
            std::nullopt, Constraints{50, Constraints::inf, 30, Constraints::inf})),
        "parent", 100.0f, 80.0f);

    if (!tryLayout(*parent, {0, 100, 0, 80}))
        return false;
    Node *child = parent->children[0].get();
    // Flutter 断言：box.size == Size(100, 80)
    return approx(child->size().width, 100.0f) && approx(child->size().height, 80.0f);
}

// ============================================================================
// 26. Row 交叉轴尺寸收缩（非 stretch 时取子项最大高度）
// ============================================================================
bool test_row_cross_axis_shrink()
{
    auto row = std::make_unique<RowNode>(
        makeChildren(std::make_unique<ContainerNode>("red", 50.0f, 30.0f),
                     std::make_unique<ContainerNode>("green", 70.0f, 80.0f)),
        "row", std::nullopt, std::nullopt, MainAxisAlignment::start,
        CrossAxisAlignment::start);

    if (!tryLayout(*row, {0, 300, 0, 600}))
        return false;
    // Flutter 断言：rowBox.size == Size(300, 80)
    return approx(row->size().width, 300.0f) && approx(row->size().height, 80.0f);
}

// ============================================================================
// 27. 深层嵌套确定画布尺寸
// ============================================================================
bool test_deep_nested_canvas_size()
{
    auto container = std::make_unique<ContainerNode>(
        makeChildren(std::make_unique<ColumnNode>(
            makeChildren(
                std::make_unique<RowNode>(
                    makeChildren(std::make_unique<ContainerNode>("box1", 40.0f, 40.0f),
                                 std::make_unique<ContainerNode>("box2", 30.0f, 50.0f)),
                    "row", std::nullopt, std::nullopt, MainAxisAlignment::start,
                    CrossAxisAlignment::start, MainAxisSize::min),
                std::make_unique<ContainerNode>("box3", 100.0f, 20.0f)),
            "col", std::nullopt, std::nullopt, MainAxisAlignment::start,
            CrossAxisAlignment::start, MainAxisSize::min)),
        "root", std::nullopt, std::nullopt);

    if (!tryLayout(*container, {0, 600, 0, 600}))
        return false;
    // Flutter 断言：containerBox.size == Size(100, 70)
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
           test_row_cross_axis_center_end() &&
           // 新增 19~27
           test_root_size_determined_by_child() && test_row_min_with_padding() &&
           test_column_min_with_child_margin() && test_container_margin_fixed_size() &&
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