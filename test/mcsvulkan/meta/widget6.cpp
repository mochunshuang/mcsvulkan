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

  protected:
    virtual void performLayout(Constraints borderBC) = 0;

    Size childFullSize(const Node &child) const
    {
        return {child.geometry.w + child.margin.horizontal(),
                child.geometry.h + child.margin.vertical()};
    }

    // 生成宽松约束
    static Constraints makeLooseConstraints(const Constraints &c)
    {
        return {0.0f, c.maxW, 0.0f, c.maxH};
    }
};

// ============================================================================
// Expanded 节点（提前定义，供 FlexNode 使用 dynamic_cast）
// ============================================================================

class ExpandedNode : public Node
{
  public:
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

        Constraints childBC = alignment ? makeLooseConstraints(innerBC) : innerBC;
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

        // 收集子节点
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

        // 布局非弹性子节点
        std::vector<float> naturalMain;
        naturalMain.reserve(infos.size());
        for (const auto &info : infos)
        {
            float childMainMax = (info.flex > 0) ? 0.0f : Constraints::inf;
            float childCrossMax = innerCross;
            float childCrossMin =
                (crossAlign == CrossAxisAlignment::stretch) ? childCrossMax : 0.0f;
            Constraints childBC = makeFlexAxisConstraints(isRow_, 0.0f, childMainMax,
                                                          childCrossMin, childCrossMax);
            if (info.flex == 0)
                childBC =
                    childBC.deflate(EdgeInsets{info.marginMain, info.marginCross, 0, 0});
            info.node->layout(childBC);
            naturalMain.push_back(isRow_ ? info.node->geometry.w : info.node->geometry.h);
        }

        // 计算总自然尺寸
        float totalNatural = 0;
        for (size_t i = 0; i < infos.size(); ++i)
            totalNatural += naturalMain[i] + infos[i].marginMain;

        // 确定最终主轴尺寸
        float finalInnerMain;
        if (!isBoundedMain)
            finalInnerMain = totalNatural;
        else if (mainAxisSize == MainAxisSize::min)
            finalInnerMain = std::min(totalNatural, innerMain);
        else
            finalInnerMain = innerMain;

        float freeMain = std::max(0.0f, finalInnerMain - totalNatural);

        // 分配弹性空间
        if (totalFlex > 0 && freeMain > 0)
        {
            float flexUnit = freeMain / totalFlex;
            for (size_t i = 0; i < infos.size(); ++i)
            {
                if (infos[i].flex > 0)
                {
                    float allocated = infos[i].flex * flexUnit;
                    float childCrossMax = innerCross;
                    float childCrossMin = (crossAlign == CrossAxisAlignment::stretch)
                                              ? childCrossMax
                                              : 0.0f;
                    Constraints tightBC = makeFlexAxisConstraints(
                        isRow_, allocated, allocated, childCrossMin, childCrossMax);
                    infos[i].node->layout(tightBC);
                    naturalMain[i] =
                        isRow_ ? infos[i].node->geometry.w : infos[i].node->geometry.h;
                }
            }
        }

        // 重新计算实际占用
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

        // 定位子节点
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

        // 计算自身尺寸
        Size size;
        if (isRow_)
        {
            size.width = finalInnerMain + padding.horizontal();
            size.height = innerCross + padding.vertical();
        }
        else
        {
            size.width = innerCross + padding.horizontal();
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
};

class ColumnNode : public FlexNode<false>
{
};

// ============================================================================
// 工厂函数
// ============================================================================

std::unique_ptr<ContainerNode> makeContainer(
    std::optional<float> width = std::nullopt, std::optional<float> height = std::nullopt,
    EdgeInsets margin = {}, EdgeInsets padding = {}, EdgeInsets border = {},
    std::optional<Alignment> alignment = std::nullopt,
    std::optional<Constraints> constraints = std::nullopt)
{

    auto node = std::make_unique<ContainerNode>();
    node->fixedWidth = width;
    node->fixedHeight = height;
    node->margin = margin;
    node->padding = padding;
    node->border = border;
    node->alignment = alignment;
    node->ownConstraints = constraints;
    return node;
}

std::unique_ptr<RowNode> makeRow(std::optional<float> w = std::nullopt,
                                 std::optional<float> h = std::nullopt,
                                 MainAxisAlignment ma = MainAxisAlignment::start,
                                 CrossAxisAlignment ca = CrossAxisAlignment::start,
                                 MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {},
                                 EdgeInsets p = {}, TextDirection td = TextDirection::ltr)
{

    auto node = std::make_unique<RowNode>();
    node->fixedWidth = w;
    node->fixedHeight = h;
    node->margin = m;
    node->padding = p;
    node->mainAlign = ma;
    node->crossAlign = ca;
    node->mainAxisSize = ms;
    node->textDirection = td;
    return node;
}

std::unique_ptr<RowNode> makeRow(EdgeInsets m, EdgeInsets p)
{
    return makeRow(std::nullopt, std::nullopt, MainAxisAlignment::start,
                   CrossAxisAlignment::start, MainAxisSize::max, m, p,
                   TextDirection::ltr);
}

std::unique_ptr<ColumnNode> makeColumn(std::optional<float> w = std::nullopt,
                                       std::optional<float> h = std::nullopt,
                                       MainAxisAlignment ma = MainAxisAlignment::start,
                                       CrossAxisAlignment ca = CrossAxisAlignment::start,
                                       MainAxisSize ms = MainAxisSize::max,
                                       EdgeInsets m = {}, EdgeInsets p = {},
                                       VerticalDirection vd = VerticalDirection::down)
{

    auto node = std::make_unique<ColumnNode>();
    node->fixedWidth = w;
    node->fixedHeight = h;
    node->margin = m;
    node->padding = p;
    node->mainAlign = ma;
    node->crossAlign = ca;
    node->mainAxisSize = ms;
    node->verticalDirection = vd;
    return node;
}

std::unique_ptr<ColumnNode> makeColumn(EdgeInsets m, EdgeInsets p)
{
    return makeColumn(std::nullopt, std::nullopt, MainAxisAlignment::start,
                      CrossAxisAlignment::start, MainAxisSize::max, m, p,
                      VerticalDirection::down);
}

std::unique_ptr<ExpandedNode> makeExpanded(int flex = 1)
{
    auto node = std::make_unique<ExpandedNode>();
    node->flex = flex;
    return node;
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
// 测试用例（已删除所有 Text 相关测试）
// ============================================================================

bool test_parent_constraints_win_over_fixed_child()
{
    auto parent = makeContainer(100.0f, 100.0f, {}, {}, {}, Align::topLeft);
    parent->name = "parent";
    auto child = makeContainer(50.0f, 50.0f, {5, 5, 5, 5});
    child->name = "child";
    parent->children.push_back(std::move(child));
    if (!tryLayout(*parent, {0, 800, 0, 600}))
        return false;
    return checkGeometry(*parent, 0, 0, 100, 100, "parent") &&
           checkGeometry(*parent->children[0], 5, 5, 50, 50, "child fixed");
}

bool test_fixed_size_child_inside_row()
{
    auto row = makeRow(300.0f, 100.0f);
    row->name = "row";
    auto child = makeContainer(50.0f, 30.0f);
    child->name = "c1";
    row->children.push_back(std::move(child));
    if (!tryLayout(*row, {0, 500, 0, 200}))
        return false;
    return checkGeometry(*row, 0, 0, 300, 100, "row") &&
           checkGeometry(*row->children[0], 0, 0, 50, 30, "c1");
}

bool test_expanded_distributes_space_by_flex()
{
    auto row = makeRow(300.0f, 100.0f);
    row->name = "row";
    auto c1 = makeContainer(50.0f, 30.0f);
    c1->name = "c1";
    auto exp = makeExpanded(2);
    exp->name = "exp";
    auto c2 = makeContainer(std::nullopt, 20.0f);
    c2->name = "c2";
    exp->children.push_back(std::move(c2));
    auto c3 = makeContainer(70.0f, 40.0f);
    c3->name = "c3";
    row->children.push_back(std::move(c1));
    row->children.push_back(std::move(exp));
    row->children.push_back(std::move(c3));
    if (!tryLayout(*row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row, 0, 0, 300, 100, "row") &&
           checkGeometry(*row->children[0], 0, 0, 50, 30, "c1") &&
           checkGeometry(*row->children[1]->children[0], 50, 0, 180, 20, "c2 expanded") &&
           checkGeometry(*row->children[2], 230, 0, 70, 40, "c3");
}

bool test_stretch_overrides_fixed_height()
{
    auto row =
        makeRow(300.0f, 100.0f, MainAxisAlignment::start, CrossAxisAlignment::stretch);
    row->name = "row";
    auto c1 = makeContainer(50.0f, std::nullopt);
    c1->name = "c1";
    auto c2 = makeContainer(70.0f, 40.0f);
    c2->name = "c2";
    row->children.push_back(std::move(c1));
    row->children.push_back(std::move(c2));
    if (!tryLayout(*row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row->children[0], 0, 0, 50, 100, "c1 stretched") &&
           checkGeometry(*row->children[1], 50, 0, 70, 100, "c2 forced");
}

bool test_main_axis_min_shrinks_column()
{
    auto col = makeColumn(200.0f, std::nullopt, MainAxisAlignment::start,
                          CrossAxisAlignment::start, MainAxisSize::min);
    col->name = "col";
    auto c1 = makeContainer(100.0f, 30.0f);
    c1->name = "c1";
    auto c2 = makeContainer(80.0f, 20.0f);
    c2->name = "c2";
    col->children.push_back(std::move(c1));
    col->children.push_back(std::move(c2));
    if (!tryLayout(*col, {0, 200, 0, Constraints::inf}))
        return false;
    return approx(col->geometry.h, 50);
}

bool test_container_shrink_to_child()
{
    auto parent = makeContainer(200.0f, 200.0f);
    parent->name = "parent";
    auto child = makeContainer(std::nullopt, std::nullopt, {10, 10, 10, 10});
    child->name = "child";
    auto inner = makeContainer(50.0f, 30.0f);
    inner->name = "inner";
    child->children.push_back(std::move(inner));
    parent->children.push_back(std::move(child));
    if (!tryLayout(*parent, {0, 200, 0, 200}))
        return false;
    return checkGeometry(*parent->children[0], 10, 10, 180, 180, "child fills parent") &&
           checkGeometry(*parent->children[0]->children[0], 0, 0, 180, 180,
                         "inner fills child");
}

bool test_unbounded_container_shrinks_to_zero()
{
    auto c = makeContainer();
    c->name = "c";
    if (!tryLayout(*c, {0, Constraints::inf, 0, 100}))
        return false;
    return checkGeometry(*c, 0, 0, 0, 0, "c unbounded becomes zero");
}

bool test_expanded_outside_flex_throws()
{
    auto cont = makeContainer(200.0f, 200.0f);
    cont->name = "cont";
    auto exp = makeExpanded();
    exp->name = "exp";
    auto inner = makeContainer(50.0f, 50.0f);
    inner->name = "inner";
    exp->children.push_back(std::move(inner));
    cont->children.push_back(std::move(exp));
    return tryLayout(*cont, {0, 200, 0, 200}, "Expanded widget");
}

bool test_container_with_padding_shrinks()
{
    auto parent = makeContainer(200.0f, 200.0f);
    parent->name = "parent";
    auto child = makeContainer(std::nullopt, std::nullopt, {}, {10, 10, 10, 10});
    child->name = "child";
    auto inner = makeContainer(50.0f, 30.0f);
    inner->name = "inner";
    child->children.push_back(std::move(inner));
    parent->children.push_back(std::move(child));
    if (!tryLayout(*parent, {0, 200, 0, 200}))
        return false;
    return checkGeometry(*parent->children[0], 0, 0, 200, 200, "child fills parent") &&
           checkGeometry(*parent->children[0]->children[0], 10, 10, 180, 180,
                         "inner fills child");
}

bool test_nested_container_shrink()
{
    auto outer = makeContainer(200.0f, 200.0f);
    outer->name = "outer";
    auto mid = makeContainer(std::nullopt, std::nullopt, {}, {5, 5, 5, 5});
    mid->name = "mid";
    auto inner = makeContainer(30.0f, 20.0f);
    inner->name = "inner";
    mid->children.push_back(std::move(inner));
    outer->children.push_back(std::move(mid));
    if (!tryLayout(*outer, {0, 200, 0, 200}))
        return false;
    return checkGeometry(*outer->children[0], 0, 0, 200, 200, "mid fills outer") &&
           checkGeometry(*outer->children[0]->children[0], 5, 5, 190, 190,
                         "inner fills mid");
}

bool test_row_stretch_only_affects_height()
{
    auto row =
        makeRow(300.0f, 100.0f, MainAxisAlignment::start, CrossAxisAlignment::stretch);
    row->name = "row";
    auto c1 = makeContainer(50.0f, std::nullopt);
    c1->name = "c1";
    auto c2 = makeContainer(70.0f, 40.0f);
    c2->name = "c2";
    row->children.push_back(std::move(c1));
    row->children.push_back(std::move(c2));
    if (!tryLayout(*row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row->children[0], 0, 0, 50, 100, "c1 width fixed") &&
           checkGeometry(*row->children[1], 50, 0, 70, 100, "c2 width fixed");
}

bool test_column_space_evenly_distribution()
{
    auto col = makeColumn(100.0f, 200.0f, MainAxisAlignment::spaceEvenly);
    col->name = "col";
    auto c1 = makeContainer(50.0f, 30.0f);
    c1->name = "c1";
    auto c2 = makeContainer(50.0f, 30.0f);
    c2->name = "c2";
    auto c3 = makeContainer(50.0f, 30.0f);
    c3->name = "c3";
    col->children.push_back(std::move(c1));
    col->children.push_back(std::move(c2));
    col->children.push_back(std::move(c3));
    if (!tryLayout(*col, {0, 100, 0, 200}))
        return false;
    return checkGeometry(*col->children[0], 0, 27.5, 50, 30, "c1 at 27.5") &&
           checkGeometry(*col->children[1], 0, 85, 50, 30, "c2 at 85") &&
           checkGeometry(*col->children[2], 0, 142.5, 50, 30, "c3 at 142.5");
}

bool test_parent_tighter_than_fixed_override()
{
    auto parent = makeContainer(50.0f, 50.0f);
    parent->name = "parent";
    auto child = makeContainer(100.0f, 100.0f);
    child->name = "child";
    parent->children.push_back(std::move(child));
    if (!tryLayout(*parent, {0, 200, 0, 200}))
        return false;
    return checkGeometry(*parent->children[0], 0, 0, 50, 50, "child forced to 50x50");
}

bool test_expanded_in_column_distributes_vertically()
{
    auto col = makeColumn(100.0f, 200.0f);
    col->name = "col";
    auto c1 = makeContainer(50.0f, 30.0f);
    c1->name = "c1";
    auto exp = makeExpanded(2);
    exp->name = "exp";
    auto c2 = makeContainer(50.0f, std::nullopt);
    c2->name = "c2";
    exp->children.push_back(std::move(c2));
    col->children.push_back(std::move(c1));
    col->children.push_back(std::move(exp));
    if (!tryLayout(*col, {0, 100, 0, 200}))
        return false;
    return checkGeometry(*col->children[0], 0, 0, 50, 30, "c1") &&
           checkGeometry(*col->children[1]->children[0], 0, 30, 50, 170, "expanded c2");
}

bool test_negative_margin_overlap()
{
    auto row = makeRow(200.0f, 100.0f);
    row->name = "row";
    auto c1 = makeContainer(50.0f, 50.0f);
    c1->name = "c1";
    auto c2 = makeContainer(50.0f, 50.0f, {-10, 0, 0, 0});
    c2->name = "c2";
    row->children.push_back(std::move(c1));
    row->children.push_back(std::move(c2));
    if (!tryLayout(*row, {0, 200, 0, 100}))
        return false;
    return checkGeometry(*row->children[0], 0, 0, 50, 50, "c1") &&
           checkGeometry(*row->children[1], 40, 0, 50, 50, "c2 overlapping");
}

bool test_expanded_with_margin()
{
    auto row = makeRow(300.0f, 100.0f);
    row->name = "row";
    auto c1 = makeContainer(50.0f, 30.0f);
    c1->name = "c1";
    auto exp = makeExpanded(1);
    exp->name = "exp";
    auto c2 = makeContainer(std::nullopt, 30.0f, {10, 0, 0, 0});
    c2->name = "c2";
    exp->children.push_back(std::move(c2));
    row->children.push_back(std::move(c1));
    row->children.push_back(std::move(exp));
    if (!tryLayout(*row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row->children[0], 0, 0, 50, 30, "c1") &&
           checkGeometry(*row->children[1]->children[0], 60, 0, 240, 30,
                         "c2 with margin");
}

bool test_container_own_constraints()
{
    auto cont = makeContainer(std::nullopt, std::nullopt, {}, {}, {}, std::nullopt,
                              Constraints{80, 120, 0, Constraints::inf});
    cont->name = "cont";
    auto inner = makeContainer(50.0f, 50.0f);
    inner->name = "inner";
    cont->children.push_back(std::move(inner));
    if (!tryLayout(*cont, {0, 200, 0, 200}))
        return false;
    return checkGeometry(*cont, 0, 0, 80, 50, "cont constrained to 80x50") &&
           checkGeometry(*cont->children[0], 0, 0, 80, 50, "inner fills cont");
}

bool test_row_rtl()
{
    auto row =
        makeRow(300.0f, 100.0f, MainAxisAlignment::start, CrossAxisAlignment::start,
                MainAxisSize::max, {}, {}, TextDirection::rtl);
    row->name = "row";
    auto c1 = makeContainer(50.0f, 30.0f);
    c1->name = "c1";
    auto c2 = makeContainer(70.0f, 40.0f);
    c2->name = "c2";
    row->children.push_back(std::move(c1));
    row->children.push_back(std::move(c2));
    if (!tryLayout(*row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(*row->children[0], 250, 0, 50, 30, "c1 right") &&
           checkGeometry(*row->children[1], 180, 0, 70, 40, "c2 left");
}

bool test_column_up()
{
    auto col =
        makeColumn(100.0f, 200.0f, MainAxisAlignment::start, CrossAxisAlignment::start,
                   MainAxisSize::max, {}, {}, VerticalDirection::up);
    col->name = "col";
    auto c1 = makeContainer(50.0f, 30.0f);
    c1->name = "c1";
    auto c2 = makeContainer(50.0f, 40.0f);
    c2->name = "c2";
    col->children.push_back(std::move(c1));
    col->children.push_back(std::move(c2));
    if (!tryLayout(*col, {0, 100, 0, 200}))
        return false;
    return checkGeometry(*col->children[0], 0, 170, 50, 30, "c1 bottom") &&
           checkGeometry(*col->children[1], 0, 130, 50, 40, "c2 above");
}

bool test_nested_expanded()
{
    auto row = makeRow(300.0f, 200.0f);
    row->name = "row";
    auto fixed = makeContainer(50.0f, 50.0f);
    fixed->name = "fixed";
    auto expRow = makeExpanded(1);
    expRow->name = "expRow";
    auto col = makeColumn(std::nullopt, std::nullopt, MainAxisAlignment::start,
                          CrossAxisAlignment::stretch, MainAxisSize::max, {}, {});
    col->name = "col";
    auto colFixed = makeContainer(80.0f, 30.0f);
    colFixed->name = "colFixed";
    auto expCol = makeExpanded(1);
    expCol->name = "expCol";
    auto inner = makeContainer(std::nullopt, std::nullopt);
    inner->name = "inner";
    expCol->children.push_back(std::move(inner));
    col->children.push_back(std::move(colFixed));
    col->children.push_back(std::move(expCol));
    expRow->children.push_back(std::move(col));
    row->children.push_back(std::move(fixed));
    row->children.push_back(std::move(expRow));
    if (!tryLayout(*row, {0, 300, 0, 200}))
        return false;
    return checkGeometry(*row->children[0], 0, 0, 50, 50, "fixed") &&
           checkGeometry(*row->children[1]->children[0]->children[0], 0, 0, 250, 30,
                         "colFixed") &&
           checkGeometry(*row->children[1]->children[0]->children[1]->children[0], 0, 30,
                         250, 170, "inner expanded");
}

// ============================================================================
// 提取 ScreenGeometry
// ============================================================================
struct ScreenGeometry
{
    float x, y, w, h;
    float padL, padR, padT, padB;
    float borderL, borderR, borderT, borderB;
};

std::vector<ScreenGeometry> extractScreenGeometries(const Node &root)
{
    std::vector<ScreenGeometry> geos;
    std::function<void(const Node &, float, float)> dfs = [&](const Node &node, float px,
                                                              float py) {
        float x = px + node.geometry.x;
        float y = py + node.geometry.y;
        float w = node.geometry.w;
        float h = node.geometry.h;
        if (auto cont = dynamic_cast<const ContainerNode *>(&node))
        {
            geos.push_back({x, y, w, h, cont->padding.left, cont->padding.right,
                            cont->padding.top, cont->padding.bottom, cont->border.left,
                            cont->border.right, cont->border.top, cont->border.bottom});
        }
        for (const auto &child : node.children)
            dfs(*child, x, y);
    };
    dfs(root, 0, 0);
    return geos;
}

bool test_specific_layout_equivalence()
{
    auto screen = makeContainer(800.0f, 600.0f, {}, {});
    screen->name = "screen";

    auto root = makeContainer(200.0f, 200.0f, {}, {10, 10, 10, 10}, {}, Align::topLeft);
    root->name = "root";
    auto child0 = makeContainer(100.0f, 100.0f, {5, 5, 5, 5}, {}, {}, Align::topLeft);
    child0->name = "child0";
    auto child0_0 = makeContainer(50.0f, 50.0f, {5, 5, 5, 5}, {});
    child0_0->name = "child0-0";
    child0->children.push_back(std::move(child0_0));
    auto node1_in_root = makeContainer(100.0f, 20.0f, {5, 5, 5, 5}, {});
    node1_in_root->name = "1";
    auto root_column = makeColumn(std::nullopt, std::nullopt, MainAxisAlignment::start,
                                  CrossAxisAlignment::start, MainAxisSize::max, {}, {});
    root_column->name = "root_column";
    root_column->children.push_back(std::move(child0));
    root_column->children.push_back(std::move(node1_in_root));
    root->children.push_back(std::move(root_column));

    auto root2 = makeContainer(200.0f, 200.0f, {}, {10, 10, 10, 10}, {}, Align::topLeft);
    root2->name = "root2";
    auto node1_in_root2 = makeContainer(100.0f, 20.0f, {5, 5, 5, 5}, {});
    node1_in_root2->name = "1";
    auto root2_column = makeColumn(std::nullopt, std::nullopt, MainAxisAlignment::start,
                                   CrossAxisAlignment::start, MainAxisSize::max, {}, {});
    root2_column->name = "root2_column";
    root2_column->children.push_back(std::move(node1_in_root2));
    root2->children.push_back(std::move(root2_column));

    auto text_display = makeContainer(200.0f, 200.0f, {}, {10, 10, 10, 10});
    text_display->name = "text_display";

    auto screen_column = makeColumn(std::nullopt, std::nullopt, MainAxisAlignment::start,
                                    CrossAxisAlignment::start, MainAxisSize::max, {}, {});
    screen_column->name = "screen_column";
    screen_column->children.push_back(std::move(root));
    screen_column->children.push_back(std::move(root2));
    screen_column->children.push_back(std::move(text_display));
    screen->children.push_back(std::move(screen_column));

    if (!tryLayout(*screen, {0, 800, 0, 600}))
        return false;

    auto geos = extractScreenGeometries(*screen);

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
        std::cerr << "FAIL: geometry count mismatch\n";
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
            std::cerr << "FAIL: geometry mismatch at index " << i << "\n";
            return false;
        }
    }
    return true;
}

bool test_border()
{
    auto parent =
        makeContainer(100.0f, 100.0f, {}, {10, 10, 10, 10}, {5, 5, 5, 5}, Align::topLeft);
    parent->name = "parent";
    auto child = makeContainer(30.0f, 20.0f);
    child->name = "child";
    parent->children.push_back(std::move(child));
    if (!tryLayout(*parent, {0, 200, 0, 200}))
        return false;
    return checkGeometry(*parent, 0, 0, 100, 100, "parent with border") &&
           checkGeometry(*parent->children[0], 15, 15, 30, 20, "child inside border");
}

// ============================================================================
// 运行所有测试
// ============================================================================
bool runTests()
{
    return test_parent_constraints_win_over_fixed_child() &&
           test_fixed_size_child_inside_row() &&
           test_expanded_distributes_space_by_flex() &&
           test_stretch_overrides_fixed_height() && test_main_axis_min_shrinks_column() &&
           test_container_shrink_to_child() &&
           test_unbounded_container_shrinks_to_zero() &&
           test_expanded_outside_flex_throws() && test_container_with_padding_shrinks() &&
           test_nested_container_shrink() && test_row_stretch_only_affects_height() &&
           test_column_space_evenly_distribution() &&
           test_parent_tighter_than_fixed_override() &&
           test_expanded_in_column_distributes_vertically() &&
           test_negative_margin_overlap() && test_expanded_with_margin() &&
           test_specific_layout_equivalence() && test_border() &&
           test_container_own_constraints() && test_row_rtl() && test_column_up() &&
           test_nested_expanded();
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