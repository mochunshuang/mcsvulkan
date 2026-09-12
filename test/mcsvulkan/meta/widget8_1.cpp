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

    // 辅助初始化类型
    struct _Initializer
    {
        float x, y;
        constexpr operator Alignment() const noexcept
        {
            return Alignment{x, y};
        }
    };

    constexpr static _Initializer topLeft{-1, -1}, topCenter{0, -1}, topRight{1, -1};
    constexpr static _Initializer centerLeft{-1, 0}, center{0, 0}, centerRight{1, 0};
    constexpr static _Initializer bottomLeft{-1, 1}, bottomCenter{0, 1},
        bottomRight{1, 1};
};

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
    stretch
    // baseline 已移除
};
enum class FlexFit
{
    loose,
    tight
};

enum class WrapAlignment
{
    start,
    end,
    center,
    spaceBetween,
    spaceAround,
    spaceEvenly
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
    // float baseline 已删除
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
            float w =
                fixedWidth.value_or(borderBC.hasBoundedWidth() ? borderBC.maxW : 0.0f);
            float h =
                fixedHeight.value_or(borderBC.hasBoundedHeight() ? borderBC.maxH : 0.0f);
            Size size = borderBC.clamp({w, h});
            geometry = BoxGeometry{0, 0, size.width, size.height};
            // baseline 赋值已删除
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
        // baseline 赋值已删除
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
        Alignment al = align.value_or(Alignment::topLeft);
        Offset offset = {border.left + padding.left + child.margin.left +
                             extraW * (al.x + 1.0f) / 2.0f,
                         border.top + padding.top + child.margin.top +
                             extraH * (al.y + 1.0f) / 2.0f};
        child.geometry.x = offset.x;
        child.geometry.y = offset.y;
    }
};

// ============================================================================
// Spacer 节点（类似 Expanded，但无 child）
// ============================================================================
class SpacerNode : public Node
{
  public:
    explicit SpacerNode(int flex = 1, std::string name = "spacer")
        : Node(std::move(name)), flex(flex)
    {
    }

    int flex = 1;

  protected:
    void performLayout(Constraints) override
    {
        // Spacer 不应被直接布局，由 FlexNode 特殊处理
        throw std::logic_error("Spacer must be direct child of Row/Column/Flex.");
    }
};

class FlexibleNode : public Node
{
  public:
    explicit FlexibleNode(int flex = 1, FlexFit fit = FlexFit::loose,
                          std::string name = "flexible")
        : Node(std::move(name)), flex(flex), fit(fit)
    {
    }
    int flex = 1;
    FlexFit fit = FlexFit::loose;

  protected:
    void performLayout(Constraints) override
    {
        throw std::logic_error("Flexible must be direct child of Row/Column/Flex.");
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

        // 检查无界主轴 + flex 子项（仅在 mainAxisSize == max 时抛异常）
        if (!isBoundedMain && mainAxisSize == MainAxisSize::max)
        {
            for (const auto &child : children)
            {
                if (dynamic_cast<ExpandedNode *>(child.get()) != nullptr ||
                    dynamic_cast<FlexibleNode *>(child.get()) != nullptr ||
                    dynamic_cast<SpacerNode *>(child.get()) != nullptr)
                {
                    throw std::logic_error("Row/Column '" + name +
                                           "' has unbounded main axis and a flex child "
                                           "with mainAxisSize.max.");
                }
            }
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
        float innerCross =
            isBoundedCross ? crossSize - padCrossStart - padCrossEnd : Constraints::inf;

        // 子项信息结构
        struct FlexChildInfo
        {
            Node *
                node; // 实际参与布局的节点（对于 Spacer 就是它本身，对于 Expanded/Flexible 是它们唯一的子节点）
            float flex;        // flex 因子（0 表示非 flex 子项）
            float marginMain;  // 主轴方向的 margin 总和
            float marginCross; // 交叉轴方向的 margin 总和
            bool isSpacer;     // 是否为 Spacer 节点
            bool isLooseFlex;  // 是否为 Flexible 且 fit == loose

            FlexChildInfo(Node *n, float f, float mM, float mC, bool spacer = false,
                          bool looseFlex = false)
                : node(n), flex(f), marginMain(mM), marginCross(mC), isSpacer(spacer),
                  isLooseFlex(looseFlex)
            {
            }
        };

        std::vector<FlexChildInfo> infos;
        float totalFlex = 0;

        // 收集子项信息
        for (auto &childPtr : children)
        {
            Node *child = childPtr.get();

            // Spacer：本质是 Expanded(child: SizedBox.shrink())
            if (auto spacer = dynamic_cast<SpacerNode *>(child))
            {
                infos.push_back(
                    {spacer, static_cast<float>(spacer->flex), 0.0f, 0.0f, true, false});
                totalFlex += spacer->flex;
                continue;
            }

            // Expanded
            if (auto expanded = dynamic_cast<ExpandedNode *>(child))
            {
                if (expanded->children.empty())
                    throw std::logic_error("Expanded widget has no child.");
                Node *real = expanded->children[0].get();
                float flexVal = expanded->flex;
                EdgeInsets m = real->margin;
                float mM = isRow_ ? m.horizontal() : m.vertical();
                float mC = isRow_ ? m.vertical() : m.horizontal();
                infos.push_back({real, flexVal, mM, mC, false, false});
                totalFlex += flexVal;
                continue;
            }

            // Flexible
            if (auto flexible = dynamic_cast<FlexibleNode *>(child))
            {
                if (flexible->children.empty())
                    throw std::logic_error("Flexible widget has no child.");
                Node *real = flexible->children[0].get();
                float flexVal = flexible->flex;
                FlexFit fit = flexible->fit;
                bool loose = (fit == FlexFit::loose);
                EdgeInsets m = real->margin;
                float mM = isRow_ ? m.horizontal() : m.vertical();
                float mC = isRow_ ? m.vertical() : m.horizontal();
                infos.push_back({real, flexVal, mM, mC, false, loose});
                totalFlex += flexVal;
                continue;
            }

            // 普通子节点
            EdgeInsets m = child->margin;
            float mM = isRow_ ? m.horizontal() : m.vertical();
            float mC = isRow_ ? m.vertical() : m.horizontal();
            infos.push_back({child, 0.0f, mM, mC, false, false});
        }

        // 第一遍：只布局非弹性子项（flex == 0）
        std::vector<float> naturalMain;
        naturalMain.reserve(infos.size());
        for (const auto &info : infos)
        {
            if (info.isSpacer || info.flex > 0)
            {
                // 弹性子项（包括 Spacer）：跳过布局，自然尺寸记为 0
                naturalMain.push_back(0.0f);
                continue;
            }

            // 非弹性子项：使用 unbounded 主轴约束 + 交叉轴约束
            float childCrossMin =
                (crossAlign == CrossAxisAlignment::stretch && isBoundedCross) ? innerCross
                                                                              : 0.0f;
            float childCrossMax = isBoundedCross ? innerCross : Constraints::inf;
            Constraints childBC = makeFlexAxisConstraints(isRow_, 0.0f, Constraints::inf,
                                                          childCrossMin, childCrossMax);

            // 扣除子项 margin
            EdgeInsets deflateMargin =
                isRow_ ? EdgeInsets{info.marginMain, info.marginCross, 0, 0}
                       : EdgeInsets{info.marginCross, info.marginMain, 0, 0};
            childBC = childBC.deflate(deflateMargin);

            info.node->layout(childBC);
            naturalMain.push_back(isRow_ ? info.node->geometry.w : info.node->geometry.h);
        }

        // 计算自然总主轴尺寸（非弹性子项 + 它们的 margin）
        float totalNatural = 0;
        for (size_t i = 0; i < infos.size(); ++i)
            totalNatural += naturalMain[i] + infos[i].marginMain;

        // 确定最终主轴内部尺寸
        float finalInnerMain;
        if (!isBoundedMain)
            finalInnerMain = totalNatural;
        else if (mainAxisSize == MainAxisSize::min)
            finalInnerMain = std::min(totalNatural, innerMain);
        else
            finalInnerMain = innerMain;

        // 分配剩余空间给弹性子项
        float freeMain = std::max(0.0f, finalInnerMain - totalNatural);
        if (totalFlex > 0 && freeMain > 0)
        {
            float flexUnit = freeMain / totalFlex;
            for (size_t i = 0; i < infos.size(); ++i)
            {
                if (infos[i].flex > 0)
                {
                    // Spacer：直接设置主轴尺寸，不布局
                    if (infos[i].isSpacer)
                    {
                        naturalMain[i] = infos[i].flex * flexUnit;
                        continue;
                    }

                    // Expanded 或 Flexible
                    float allocated = infos[i].flex * flexUnit;

                    // 交叉轴约束与第一步完全相同
                    float childCrossMin =
                        (crossAlign == CrossAxisAlignment::stretch && isBoundedCross)
                            ? innerCross
                            : 0.0f;
                    float childCrossMax = isBoundedCross ? innerCross : Constraints::inf;

                    Constraints childBC;
                    if (infos[i].isLooseFlex)
                    {
                        // Flexible fit.loose：主轴 loose 约束 (min=0, max=allocated)
                        childBC = makeFlexAxisConstraints(isRow_, 0.0f, allocated,
                                                          childCrossMin, childCrossMax);
                    }
                    else
                    {
                        // Expanded 或 Flexible fit.tight：主轴 tight 约束
                        childBC = makeFlexAxisConstraints(isRow_, allocated, allocated,
                                                          childCrossMin, childCrossMax);
                    }

                    // 扣除子项 margin
                    EdgeInsets deflateMargin =
                        isRow_
                            ? EdgeInsets{infos[i].marginMain, infos[i].marginCross, 0, 0}
                            : EdgeInsets{infos[i].marginCross, infos[i].marginMain, 0, 0};
                    childBC = childBC.deflate(deflateMargin);

                    infos[i].node->layout(childBC);
                    naturalMain[i] =
                        isRow_ ? infos[i].node->geometry.w : infos[i].node->geometry.h;
                }
            }
        }

        // 重新计算实际使用的主轴尺寸
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

        // 定位所有子项
        float mainPos = mainStartOff;
        for (size_t i = 0; i < infos.size(); ++i)
        {
            Node &childOrSpacer = *infos[i].node;
            float childMainLen = naturalMain[i];
            float childCrossLen = 0;
            if (infos[i].isSpacer)
            {
                // Spacer 的交叉轴尺寸：stretch -> innerCross，否则 0
                childCrossLen =
                    (crossAlign == CrossAxisAlignment::stretch && isBoundedCross)
                        ? innerCross
                        : 0;
            }
            else
            {
                childCrossLen =
                    isRow_ ? childOrSpacer.geometry.h : childOrSpacer.geometry.w;
            }

            float leadingMargin = infos[i].isSpacer ? 0
                                                    : (isRow_ ? childOrSpacer.margin.left
                                                              : childOrSpacer.margin.top);
            setChildMainAxisPosition(childOrSpacer, dir, mainPos + leadingMargin,
                                     childMainLen, mainSize, padMainStart, padMainEnd);

            float crossStart = 0;
            if (isBoundedCross)
            {
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
                    crossStart = crossExtra / 2;
                    break;
                case CrossAxisAlignment::stretch:
                    crossStart = 0;
                    break;
                }
            }
            float crossLeadingMargin =
                infos[i].isSpacer
                    ? 0
                    : (isRow_ ? childOrSpacer.margin.top : childOrSpacer.margin.left);
            setChildCrossAxisPosition(childOrSpacer, dir, crossStart + crossLeadingMargin,
                                      childCrossLen, crossSize, padCrossStart,
                                      padCrossEnd);

            // Spacer 需要显式设置尺寸（因为它没有 layout 过程）
            if (infos[i].isSpacer)
            {
                if (isRow_)
                {
                    childOrSpacer.geometry.w = childMainLen;
                    childOrSpacer.geometry.h = childCrossLen;
                }
                else
                {
                    childOrSpacer.geometry.w = childCrossLen;
                    childOrSpacer.geometry.h = childMainLen;
                }
            }

            mainPos += childMainLen + infos[i].marginMain + mainGap;
        }

        // 计算 Flex 自身尺寸
        Size size;
        if (isRow_)
        {
            size.width = finalInnerMain + padding.horizontal();
            if (crossAlign == CrossAxisAlignment::stretch && isBoundedCross)
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
            if (crossAlign == CrossAxisAlignment::stretch && isBoundedCross)
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
    explicit RowNode(
        std::string name, std::optional<float> width = std::nullopt,
        std::optional<float> height = std::nullopt,
        MainAxisAlignment ma = MainAxisAlignment::start,
        CrossAxisAlignment ca = CrossAxisAlignment::center, // 修改默认值为 center
        MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {}, EdgeInsets p = {},
        TextDirection td = TextDirection::ltr)
        : FlexNode<true>(std::move(name), width, height, m, p, ma, ca, ms, td)
    {
    }
};

class ColumnNode : public FlexNode<false>
{
  public:
    explicit ColumnNode(
        std::string name, std::optional<float> width = std::nullopt,
        std::optional<float> height = std::nullopt,
        MainAxisAlignment ma = MainAxisAlignment::start,
        CrossAxisAlignment ca = CrossAxisAlignment::center, // 修改默认值为 center
        MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {}, EdgeInsets p = {},
        VerticalDirection vd = VerticalDirection::down)
        : FlexNode<false>(std::move(name), width, height, m, p, ma, ca, ms,
                          TextDirection::ltr, vd)
    {
    }
};

// ============================================================================
// SizedBox 节点
// ============================================================================

class SizedBoxNode : public Node
{
  public:
    // 确保 SizedBoxNode 构造函数不再将 width/height 传给基类 Node，而是保持默认 nullopt
    explicit SizedBoxNode(std::string name) : Node(std::move(name)) {}

  protected:
    void performLayout(Constraints borderBC) override
    {
        if (children.empty())
        {
            // 显式使用 fixedWidth/fixedHeight，若无固定尺寸则取 0
            float w = fixedWidth.value_or(0.0f);
            float h = fixedHeight.value_or(0.0f);
            Size size = borderBC.clamp({w, h});
            geometry = BoxGeometry{0, 0, size.width, size.height};
        }
        else
        {
            // 原有 child 逻辑不变
            if (children.size() != 1)
                throw std::logic_error("SizedBox '" + name +
                                       "' must have exactly one child.");
            Node &child = *children[0];
            child.layout(borderBC);
            geometry = BoxGeometry{0, 0, child.geometry.w, child.geometry.h};
        }
    }
};

// ============================================================================
// Padding 节点
// ============================================================================
class PaddingNode : public Node
{
  public:
    explicit PaddingNode(std::string name, EdgeInsets padding = {})
        : Node(std::move(name), {}, padding)
    {
    }

  protected:
    void performLayout(Constraints borderBC) override
    {
        if (children.empty())
        {
            Size size = borderBC.clamp({padding.horizontal(), padding.vertical()});
            geometry = BoxGeometry{0, 0, size.width, size.height};
            return;
        }
        if (children.size() != 1)
            throw std::logic_error("Padding '" + name + "' must have exactly one child.");

        Node &child = *children[0];
        Constraints childBC = borderBC.deflate(padding);
        childBC = childBC.deflate(child.margin);
        child.layout(childBC);

        float childX = padding.left + child.margin.left;
        float childY = padding.top + child.margin.top;
        child.geometry.x = childX;
        child.geometry.y = childY;

        Size childFull = childFullSize(child);
        float w = childFull.width + padding.horizontal();
        float h = childFull.height + padding.vertical();
        Size size = borderBC.clamp({w, h});
        geometry = BoxGeometry{0, 0, size.width, size.height};
    }
};

// ============================================================================
// Align 节点
// ============================================================================
class AlignNode : public Node
{
  public:
    explicit AlignNode(std::string name, Alignment alignment = Alignment::topLeft)
        : Node(std::move(name)), alignment(alignment)
    {
    }

    Alignment alignment;

  protected:
    void performLayout(Constraints borderBC) override
    {
        if (children.empty())
        {
            Size size =
                borderBC.clamp({borderBC.hasBoundedWidth() ? borderBC.maxW : 0.0f,
                                borderBC.hasBoundedHeight() ? borderBC.maxH : 0.0f});
            geometry = BoxGeometry{0, 0, size.width, size.height};
            return;
        }
        if (children.size() != 1)
            throw std::logic_error("Align '" + name + "' must have exactly one child.");

        Node &child = *children[0];
        // 关键修复：传递 loose 约束
        Constraints childBC = Node::makeLooseConstraints(borderBC);
        childBC = childBC.deflate(child.margin);
        child.layout(childBC);

        Size childFull = childFullSize(child);
        float selfW = borderBC.hasBoundedWidth() ? borderBC.maxW : childFull.width;
        float selfH = borderBC.hasBoundedHeight() ? borderBC.maxH : childFull.height;
        Size selfSize = borderBC.clamp({selfW, selfH});
        geometry = BoxGeometry{0, 0, selfSize.width, selfSize.height};

        float extraW = std::max(0.0f, selfSize.width - childFull.width);
        float extraH = std::max(0.0f, selfSize.height - childFull.height);
        float childX = child.margin.left + extraW * (alignment.x + 1.0f) / 2.0f;
        float childY = child.margin.top + extraH * (alignment.y + 1.0f) / 2.0f;
        child.geometry.x = childX;
        child.geometry.y = childY;
    }
};

// ============================================================================
// Center 节点（继承 Align，alignment 固定为 center）
// ============================================================================
class CenterNode : public AlignNode
{
  public:
    explicit CenterNode(std::string name) : AlignNode(std::move(name), Alignment::center)
    {
    }
};

class PositionedNode : public Node
{
  public:
    std::optional<float> left, top, right, bottom;
    bool fill = false;

    explicit PositionedNode(std::string name = "positioned") : Node(std::move(name)) {}

    PositionedNode &setLeft(float v)
    {
        left = v;
        return *this;
    }
    PositionedNode &setTop(float v)
    {
        top = v;
        return *this;
    }
    PositionedNode &setRight(float v)
    {
        right = v;
        return *this;
    }
    PositionedNode &setBottom(float v)
    {
        bottom = v;
        return *this;
    }
    PositionedNode &setFill()
    {
        fill = true;
        left = top = right = bottom = 0;
        return *this;
    }

  protected:
    void performLayout(Constraints) override
    {
        throw std::logic_error("Positioned must be direct child of Stack.");
    }
};

class StackNode : public Node
{
  public:
    explicit StackNode(std::string name = "stack") : Node(std::move(name)) {}

  protected:
    void performLayout(Constraints bc) override
    {
        // 分离 positioned 和非 positioned 子节点
        std::vector<Node *> nonPos;
        std::vector<PositionedNode *> posChildren;
        for (auto &child : children)
        {
            if (auto pos = dynamic_cast<PositionedNode *>(child.get()))
            {
                posChildren.push_back(pos);
            }
            else
            {
                nonPos.push_back(child.get());
            }
        }

        // 先布局所有非定位子节点（使用松散约束）
        for (Node *child : nonPos)
        {
            Constraints childBC = Node::makeLooseConstraints(bc);
            childBC = childBC.deflate(child->margin);
            child->layout(childBC);
        }

        // 确定 Stack 自身尺寸
        Size stackSize;
        bool tight = (bc.minW == bc.maxW && bc.minH == bc.maxH);
        if (tight)
        {
            stackSize = bc.clamp({bc.maxW, bc.maxH});
        }
        else
        {
            if (nonPos.empty())
            {
                stackSize = bc.clamp({bc.hasBoundedWidth() ? bc.maxW : 0.0f,
                                      bc.hasBoundedHeight() ? bc.maxH : 0.0f});
            }
            else
            {
                float maxW = 0, maxH = 0;
                for (Node *child : nonPos)
                {
                    Size full = childFullSize(*child);
                    maxW = std::max(maxW, full.width);
                    maxH = std::max(maxH, full.height);
                }
                stackSize = bc.clamp({maxW, maxH});
            }
        }

        geometry = BoxGeometry{0, 0, stackSize.width, stackSize.height};

        // 设置非定位子节点位置（已在上面布局过）
        for (Node *child : nonPos)
        {
            child->geometry.x = child->margin.left;
            child->geometry.y = child->margin.top;
        }

        // 布局 Positioned 子节点
        for (PositionedNode *pos : posChildren)
        {
            if (pos->children.empty())
                continue;
            Node &child = *pos->children[0];

            // 计算约束
            float left = pos->left.value_or(0);
            float top = pos->top.value_or(0);
            float right = pos->right.value_or(0);
            float bottom = pos->bottom.value_or(0);

            bool hasLeft = pos->left.has_value() || pos->fill;
            bool hasRight = pos->right.has_value() || pos->fill;
            bool hasTop = pos->top.has_value() || pos->fill;
            bool hasBottom = pos->bottom.has_value() || pos->fill;

            float availableW = stackSize.width - left - right;
            float availableH = stackSize.height - top - bottom;

            // 确定子节点约束
            float minW = 0, maxW = availableW;
            float minH = 0, maxH = availableH;

            if (hasLeft && hasRight)
            {
                minW = maxW = availableW; // tight
            }
            else if (hasLeft || hasRight)
            {
                // 单边约束，宽高为 loose，但最大可用
                maxW = availableW;
            }
            if (hasTop && hasBottom)
            {
                minH = maxH = availableH;
            }
            else if (hasTop || hasBottom)
            {
                maxH = availableH;
            }

            Constraints childBC{minW, maxW, minH, maxH};
            childBC = childBC.deflate(child.margin);
            child.layout(childBC);

            // 定位
            float childX, childY;
            if (hasLeft)
            {
                childX = left + child.margin.left;
            }
            else if (hasRight)
            {
                childX = stackSize.width - right - child.margin.right - child.geometry.w;
            }
            else
            {
                // 默认靠左
                childX = left + child.margin.left;
            }

            if (hasTop)
            {
                childY = top + child.margin.top;
            }
            else if (hasBottom)
            {
                childY =
                    stackSize.height - bottom - child.margin.bottom - child.geometry.h;
            }
            else
            {
                childY = top + child.margin.top;
            }

            child.geometry.x = childX;
            child.geometry.y = childY;

            // Positioned 节点自身尺寸设为子节点实际尺寸（便于测试）
            pos->geometry.w = child.geometry.w + child.margin.horizontal();
            pos->geometry.h = child.geometry.h + child.margin.vertical();
            pos->geometry.x = childX;
            pos->geometry.y = childY;
        }
    }
};

// ============================================================================
// Wrap 节点
// ============================================================================
class WrapNode : public Node
{
  public:
    float spacing = 0;
    float runSpacing = 0;
    WrapAlignment alignment = WrapAlignment::start;
    WrapAlignment runAlignment = WrapAlignment::start;

    explicit WrapNode(std::string name = "wrap") : Node(std::move(name)) {}

  protected:
    void performLayout(Constraints bc) override
    {
        // 处理空子项：Wrap 尺寸收缩为 0
        if (children.empty())
        {
            Size size = bc.clamp({0, 0});
            geometry = BoxGeometry{0, 0, size.width, size.height};
            return;
        }

        float maxMain = bc.hasBoundedWidth() ? bc.maxW : Constraints::inf;
        // 布局所有子节点，使用松散约束（0~maxMain, 0~maxCross）
        std::vector<Size> childSizes;
        std::vector<EdgeInsets> childMargins;
        for (auto &child : children)
        {
            Constraints childBC = Node::makeLooseConstraints(bc);
            childBC = childBC.deflate(child->margin);
            child->layout(childBC);
            childSizes.push_back({child->geometry.w, child->geometry.h});
            childMargins.push_back(child->margin);
        }

        // 分行
        struct Run
        {
            std::vector<int> indices;
            float mainSize = 0;
            float crossSize = 0;
        };
        std::vector<Run> runs;
        Run current;
        float currentMain = 0;
        for (size_t i = 0; i < children.size(); ++i)
        {
            float childMain = childSizes[i].width + childMargins[i].horizontal();
            if (!current.indices.empty() && currentMain + spacing + childMain > maxMain &&
                maxMain != Constraints::inf)
            {
                runs.push_back(current);
                current = Run{};
                currentMain = 0;
            }
            if (!current.indices.empty())
                currentMain += spacing;
            current.indices.push_back(static_cast<int>(i));
            currentMain += childMain;
        }
        if (!current.indices.empty())
            runs.push_back(current);

        // 计算每行实际尺寸
        for (auto &run : runs)
        {
            run.mainSize = 0;
            run.crossSize = 0;
            for (int idx : run.indices)
            {
                run.mainSize += childSizes[idx].width + childMargins[idx].horizontal();
                run.crossSize = std::max(run.crossSize, childSizes[idx].height +
                                                            childMargins[idx].vertical());
            }
            run.mainSize += spacing * (run.indices.size() - 1);
        }

        // 计算总交叉轴尺寸
        float totalCross = 0;
        for (auto &run : runs)
            totalCross += run.crossSize;
        if (runs.size() > 1)
            totalCross += runSpacing * (runs.size() - 1);

        // 确定 Wrap 自身尺寸
        float wrapWidth;
        if (bc.hasBoundedWidth())
            wrapWidth = bc.maxW; // 主轴有界时填满
        else
        {
            // 无界时取最大行宽
            float maxRunMain = 0;
            for (auto &run : runs)
                maxRunMain = std::max(maxRunMain, run.mainSize);
            wrapWidth = maxRunMain;
        }

        float wrapHeight = totalCross; // 交叉轴始终收缩到内容
        Size wrapSize = bc.clamp({wrapWidth, wrapHeight});
        geometry = BoxGeometry{0, 0, wrapSize.width, wrapSize.height};

        // 定位每行
        float crossPos = 0;
        float extraCross = std::max(0.0f, wrapSize.height - totalCross);
        switch (runAlignment)
        {
        case WrapAlignment::start:
            crossPos = 0;
            break;
        case WrapAlignment::end:
            crossPos = extraCross;
            break;
        case WrapAlignment::center:
            crossPos = extraCross / 2;
            break;
        default:
            crossPos = extraCross / 2; // 其他近似 center
            break;
        }

        for (auto &run : runs)
        {
            float mainExtra = std::max(0.0f, wrapSize.width - run.mainSize);
            float mainStart = 0;
            switch (alignment)
            {
            case WrapAlignment::start:
                mainStart = 0;
                break;
            case WrapAlignment::end:
                mainStart = mainExtra;
                break;
            case WrapAlignment::center:
                mainStart = mainExtra / 2;
                break;
            default:
                mainStart = mainExtra / 2;
                break;
            }

            float mainPos = mainStart;
            for (int idx : run.indices)
            {
                Node &child = *children[idx];
                child.geometry.x = mainPos + child.margin.left;
                child.geometry.y = crossPos + child.margin.top;
                mainPos += childSizes[idx].width + child.margin.horizontal() + spacing;
            }
            crossPos += run.crossSize + runSpacing;
        }
    }
};

// ============================================================================
// AspectRatio 节点
// ============================================================================
class AspectRatioNode : public Node
{
  public:
    float aspectRatio;
    explicit AspectRatioNode(float ratio, std::string name = "aspectRatio")
        : Node(std::move(name)), aspectRatio(ratio)
    {
    }

  protected:
    void performLayout(Constraints bc) override
    {
        float width, height;
        bool tight = (bc.minW == bc.maxW && bc.minH == bc.maxH);
        if (tight)
        {
            width = bc.maxW;
            height = bc.maxH;
        }
        else
        {
            if (bc.hasBoundedWidth() && bc.hasBoundedHeight())
            {
                width = bc.maxW;
                height = width / aspectRatio;
                if (height > bc.maxH)
                {
                    height = bc.maxH;
                    width = height * aspectRatio;
                }
                else if (height < bc.minH)
                {
                    height = bc.minH;
                    width = height * aspectRatio;
                }
                width = std::clamp(width, bc.minW, bc.maxW);
                height = std::clamp(height, bc.minH, bc.maxH);
            }
            else if (bc.hasBoundedWidth())
            {
                width = bc.maxW;
                height = width / aspectRatio;
                height = std::clamp(height, bc.minH, bc.maxH);
            }
            else if (bc.hasBoundedHeight())
            {
                height = bc.maxH;
                width = height * aspectRatio;
                width = std::clamp(width, bc.minW, bc.maxW);
            }
            else
            {
                width = 0;
                height = 0;
            }
        }
        geometry = BoxGeometry{0, 0, width, height};

        if (!children.empty())
        {
            if (children.size() != 1)
                throw std::logic_error("AspectRatio must have exactly one child.");
            Node &child = *children[0];
            Constraints childBC{width, width, height, height};
            childBC = childBC.deflate(child.margin);
            child.layout(childBC);
            child.geometry.x = child.margin.left;
            child.geometry.y = child.margin.top;
        }
    }
};

// ============================================================================
// ConstrainedBox 节点
// ============================================================================
class ConstrainedBoxNode : public Node
{
  public:
    Constraints additionalConstraints;
    explicit ConstrainedBoxNode(Constraints c, std::string name = "constrainedBox")
        : Node(std::move(name)), additionalConstraints(c)
    {
    }

  protected:
    void performLayout(Constraints bc) override
    {
        Constraints effective = bc.intersect(additionalConstraints);
        if (children.empty())
        {
            Size s = effective.clamp({effective.minW, effective.minH});
            geometry = BoxGeometry{0, 0, s.width, s.height};
            return;
        }
        if (children.size() != 1)
            throw std::logic_error("ConstrainedBox must have exactly one child.");
        Node &child = *children[0];
        child.layout(effective.deflate(child.margin));
        child.geometry.x = child.margin.left;
        child.geometry.y = child.margin.top;
        Size full = childFullSize(child);
        geometry = BoxGeometry{0, 0, full.width, full.height};
    }
};

// ============================================================================
// FractionallySizedBox 节点
// ============================================================================
class FractionallySizedBoxNode : public Node
{
  public:
    std::optional<float> widthFactor;
    std::optional<float> heightFactor;
    Alignment alignment = Alignment::center;

    explicit FractionallySizedBoxNode(std::optional<float> wf, std::optional<float> hf,
                                      std::string name = "fractionallySizedBox")
        : Node(std::move(name)), widthFactor(wf), heightFactor(hf)
    {
    }

  protected:
    void performLayout(Constraints bc) override
    {
        float selfWidth = bc.hasBoundedWidth() ? bc.maxW : 0;
        float selfHeight = bc.hasBoundedHeight() ? bc.maxH : 0;
        if (widthFactor)
            selfWidth = bc.maxW * (*widthFactor);
        if (heightFactor)
            selfHeight = bc.maxH * (*heightFactor);
        selfWidth = std::clamp(selfWidth, bc.minW, bc.maxW);
        selfHeight = std::clamp(selfHeight, bc.minH, bc.maxH);
        geometry = BoxGeometry{0, 0, selfWidth, selfHeight};

        if (!children.empty())
        {
            if (children.size() != 1)
                throw std::logic_error(
                    "FractionallySizedBox must have exactly one child.");
            Node &child = *children[0];
            Constraints childBC{selfWidth, selfWidth, selfHeight, selfHeight};
            childBC = childBC.deflate(child.margin);
            child.layout(childBC);
            Size full = childFullSize(child);
            float extraW = std::max(0.0f, selfWidth - full.width);
            float extraH = std::max(0.0f, selfHeight - full.height);
            child.geometry.x = child.margin.left + extraW * (alignment.x + 1) / 2;
            child.geometry.y = child.margin.top + extraH * (alignment.y + 1) / 2;
        }
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

class SizedBoxBuilder
{
    std::unique_ptr<SizedBoxNode> node;

  public:
    explicit SizedBoxBuilder(std::string name)
        : node(std::make_unique<SizedBoxNode>(std::move(name)))
    {
    }
    SizedBoxBuilder &width(float w)
    {
        node->fixedWidth = w;
        return *this;
    }
    SizedBoxBuilder &height(float h)
    {
        node->fixedHeight = h;
        return *this;
    }
    template <typename T>
    SizedBoxBuilder &child(T &&child)
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

class PaddingBuilder
{
    std::unique_ptr<PaddingNode> node;

  public:
    explicit PaddingBuilder(std::string name, EdgeInsets padding = {})
        : node(std::make_unique<PaddingNode>(std::move(name), padding))
    {
    }

    PaddingBuilder &padding(EdgeInsets p)
    {
        node->padding = p;
        return *this;
    }
    template <typename T>
    PaddingBuilder &child(T &&child)
    {
        node->children.push_back(toWidget(std::forward<T>(child)));
        return *this;
    }
    Widget build()
    {
        assert(node);
        return std::move(node);
    }
};

class AlignBuilder
{
    std::unique_ptr<AlignNode> node;

  public:
    explicit AlignBuilder(std::string name, Alignment alignment = Alignment::topLeft)
        : node(std::make_unique<AlignNode>(std::move(name), alignment))
    {
    }

    AlignBuilder &alignment(Alignment a)
    {
        node->alignment = a;
        return *this;
    }
    template <typename T>
    AlignBuilder &child(T &&child)
    {
        node->children.push_back(toWidget(std::forward<T>(child)));
        return *this;
    }
    Widget build()
    {
        assert(node);
        return std::move(node);
    }
};

class CenterBuilder
{
    std::unique_ptr<CenterNode> node;

  public:
    explicit CenterBuilder(std::string name)
        : node(std::make_unique<CenterNode>(std::move(name)))
    {
    }

    template <typename T>
    CenterBuilder &child(T &&child)
    {
        node->children.push_back(toWidget(std::forward<T>(child)));
        return *this;
    }
    Widget build()
    {
        assert(node);
        return std::move(node);
    }
};

class SpacerBuilder
{
    std::unique_ptr<SpacerNode> node;

  public:
    explicit SpacerBuilder(int flex = 1, std::string name = "spacer")
        : node(std::make_unique<SpacerNode>(flex, std::move(name)))
    {
    }

    SpacerBuilder &flex(int f)
    {
        node->flex = f;
        return *this;
    }
    Widget build()
    {
        assert(node);
        return std::move(node);
    }
};

// FlexibleBuilder
class FlexibleBuilder
{
    std::unique_ptr<FlexibleNode> node;

  public:
    explicit FlexibleBuilder(int flex = 1, FlexFit fit = FlexFit::loose,
                             std::string name = "flexible")
        : node(std::make_unique<FlexibleNode>(flex, fit, std::move(name)))
    {
    }
    FlexibleBuilder &flex(int f)
    {
        node->flex = f;
        return *this;
    }
    FlexibleBuilder &fit(FlexFit f)
    {
        node->fit = f;
        return *this;
    }
    template <typename T>
    FlexibleBuilder &child(T &&child)
    {
        node->children.push_back(toWidget(std::forward<T>(child)));
        return *this;
    }
    Widget build()
    {
        return std::move(node);
    }
};

// StackBuilder
class StackBuilder
{
    std::unique_ptr<StackNode> node;

  public:
    explicit StackBuilder(std::string name = "stack")
        : node(std::make_unique<StackNode>(std::move(name)))
    {
    }
    template <typename... Children>
    StackBuilder &children(Children &&...children)
    {
        (node->children.push_back(toWidget(std::forward<Children>(children))), ...);
        return *this;
    }
    Widget build()
    {
        return std::move(node);
    }
};

// PositionedBuilder
class PositionedBuilder
{
    std::unique_ptr<PositionedNode> node;

  public:
    explicit PositionedBuilder(std::string name = "positioned")
        : node(std::make_unique<PositionedNode>(std::move(name)))
    {
    }
    PositionedBuilder &left(float v)
    {
        node->left = v;
        return *this;
    }
    PositionedBuilder &top(float v)
    {
        node->top = v;
        return *this;
    }
    PositionedBuilder &right(float v)
    {
        node->right = v;
        return *this;
    }
    PositionedBuilder &bottom(float v)
    {
        node->bottom = v;
        return *this;
    }
    PositionedBuilder &fill()
    {
        node->setFill();
        return *this;
    }
    template <typename T>
    PositionedBuilder &child(T &&child)
    {
        node->children.push_back(toWidget(std::forward<T>(child)));
        return *this;
    }
    Widget build()
    {
        return std::move(node);
    }
};

// WrapBuilder
class WrapBuilder
{
    std::unique_ptr<WrapNode> node;

  public:
    explicit WrapBuilder(std::string name = "wrap")
        : node(std::make_unique<WrapNode>(std::move(name)))
    {
    }
    WrapBuilder &spacing(float s)
    {
        node->spacing = s;
        return *this;
    }
    WrapBuilder &runSpacing(float s)
    {
        node->runSpacing = s;
        return *this;
    }
    WrapBuilder &alignment(WrapAlignment a)
    {
        node->alignment = a;
        return *this;
    }
    WrapBuilder &runAlignment(WrapAlignment a)
    {
        node->runAlignment = a;
        return *this;
    }
    template <typename... Children>
    WrapBuilder &children(Children &&...children)
    {
        (node->children.push_back(toWidget(std::forward<Children>(children))), ...);
        return *this;
    }
    Widget build()
    {
        return std::move(node);
    }
};

// AspectRatioBuilder
class AspectRatioBuilder
{
    std::unique_ptr<AspectRatioNode> node;

  public:
    explicit AspectRatioBuilder(float ratio, std::string name = "aspectRatio")
        : node(std::make_unique<AspectRatioNode>(ratio, std::move(name)))
    {
    }
    template <typename T>
    AspectRatioBuilder &child(T &&child)
    {
        node->children.push_back(toWidget(std::forward<T>(child)));
        return *this;
    }
    Widget build()
    {
        return std::move(node);
    }
};

// ConstrainedBoxBuilder
class ConstrainedBoxBuilder
{
    std::unique_ptr<ConstrainedBoxNode> node;

  public:
    explicit ConstrainedBoxBuilder(Constraints c, std::string name = "constrainedBox")
        : node(std::make_unique<ConstrainedBoxNode>(c, std::move(name)))
    {
    }
    template <typename T>
    ConstrainedBoxBuilder &child(T &&child)
    {
        node->children.push_back(toWidget(std::forward<T>(child)));
        return *this;
    }
    Widget build()
    {
        return std::move(node);
    }
};

// FractionallySizedBoxBuilder
class FractionallySizedBoxBuilder
{
    std::unique_ptr<FractionallySizedBoxNode> node;

  public:
    explicit FractionallySizedBoxBuilder(std::optional<float> wf = std::nullopt,
                                         std::optional<float> hf = std::nullopt,
                                         std::string name = "fractionallySizedBox")
        : node(std::make_unique<FractionallySizedBoxNode>(wf, hf, std::move(name)))
    {
    }
    FractionallySizedBoxBuilder &widthFactor(float v)
    {
        node->widthFactor = v;
        return *this;
    }
    FractionallySizedBoxBuilder &heightFactor(float v)
    {
        node->heightFactor = v;
        return *this;
    }
    FractionallySizedBoxBuilder &alignment(Alignment a)
    {
        node->alignment = a;
        return *this;
    }
    template <typename T>
    FractionallySizedBoxBuilder &child(T &&child)
    {
        node->children.push_back(toWidget(std::forward<T>(child)));
        return *this;
    }
    Widget build()
    {
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
inline SizedBoxBuilder SizedBox(std::string name)
{
    return SizedBoxBuilder(std::move(name));
}
inline PaddingBuilder Padding(std::string name, EdgeInsets padding = {})
{
    return PaddingBuilder(std::move(name), padding);
}
inline AlignBuilder Align(std::string name, Alignment alignment = Alignment::topLeft)
{
    return AlignBuilder(std::move(name), alignment);
}
inline CenterBuilder Center(std::string name)
{
    return CenterBuilder(std::move(name));
}
inline SpacerBuilder Spacer(int flex = 1, std::string name = "spacer")
{
    return SpacerBuilder(flex, std::move(name));
}

inline FlexibleBuilder Flexible(int flex = 1, FlexFit fit = FlexFit::loose,
                                std::string name = "flexible")
{
    return FlexibleBuilder(flex, fit, std::move(name));
}
inline StackBuilder Stack(std::string name = "stack")
{
    return StackBuilder(std::move(name));
}
inline PositionedBuilder Positioned(std::string name = "positioned")
{
    return PositionedBuilder(std::move(name));
}
inline WrapBuilder Wrap(std::string name = "wrap")
{
    return WrapBuilder(std::move(name));
}
inline AspectRatioBuilder AspectRatio(float ratio, std::string name = "aspectRatio")
{
    return AspectRatioBuilder(ratio, std::move(name));
}
inline ConstrainedBoxBuilder ConstrainedBox(Constraints c,
                                            std::string name = "constrainedBox")
{
    return ConstrainedBoxBuilder(c, std::move(name));
}
inline FractionallySizedBoxBuilder FractionallySizedBox(
    std::optional<float> wf = std::nullopt, std::optional<float> hf = std::nullopt,
    std::string name = "fractionallySizedBox")
{
    return FractionallySizedBoxBuilder(wf, hf, std::move(name));
}

// ============================================================================
// 测试工具（与 baseline 无关，保留）
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
// 测试用例（全部保留，无 baseline 相关）
// ============================================================================

bool test_row_fixed_children()
{
    auto row = Row("row")
                   .width(300)
                   .height(100)
                   .crossAxisAlignment(
                       CrossAxisAlignment::start) // 显式设置 start 以保持原有期望
                   .mainAxisAlignment(MainAxisAlignment::start)
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
                   .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
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
                   .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
                   .mainAxisAlignment(MainAxisAlignment::spaceBetween)
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
                   .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
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
                   .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
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
                   .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
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
                         .margin(EdgeInsets::all(5))
                         .alignment(Alignment::topLeft)
                         .child(Container("child").width(50).height(30))
                         .build();
    if (!tryLayout(container, {0, 200, 0, 200}))
        return false;
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
                         .alignment(Alignment::center)
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
            .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
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
            .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
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
            .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
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
                   .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
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
                   .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
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
                          .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
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
                       .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
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
                       .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
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
            .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
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
                      .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
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
    auto container =
        Container("root")
            .child(Column("col")
                       .mainAxisSize(MainAxisSize::min)
                       .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
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
                   .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
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
                       .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
                       .children(Row("row")
                                     .mainAxisSize(MainAxisSize::min)
                                     .crossAxisAlignment(
                                         CrossAxisAlignment::start) // 显式设置 start
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
// 补充测试：对应 Flutter 测试 29-46（跳过状态测试 28）
// ============================================================================

// 29. Container 无 child 无固定尺寸，有颜色（即无特殊属性），在松散约束下填满
bool test29_container_no_child_no_size_fills_constraints()
{
    auto container = Container("c").build();
    if (!tryLayout(container, {0, 800, 0, 600}))
        return false;
    return checkGeometry(*container, 0, 0, 800, 600, "container 29");
}

// 30. Container 无 child 无固定尺寸，无颜色，同样填满松散约束
bool test30_container_no_child_no_size_without_color_fills_constraints()
{
    auto container = Container("c").build();
    if (!tryLayout(container, {0, 800, 0, 600}))
        return false;
    return checkGeometry(*container, 0, 0, 800, 600, "container 30");
}

// 31. Container 有 child，child 为 SizedBox 无尺寸，外层收缩到 0
bool test31_container_with_child_sizedbox_shrinks_to_zero()
{
    auto container = Container("outer")
                         .child(SizedBox("inner")) // 无 child，无尺寸
                         .build();
    if (!tryLayout(container, {0, 800, 0, 600}))
        return false;
    return checkGeometry(*container, 0, 0, 0, 0, "outer 31") &&
           checkGeometry(*container->children[0], 0, 0, 0, 0, "inner 31");
}

// 32. Row 在 Center 中，默认 mainAxisSize.max，填满宽度，高度取子项最大值
bool test32_row_default_max_center()
{
    auto row = Row("row")
                   .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
                   .children(Container("red").width(50).height(30),
                             Container("green").width(70).height(40))
                   .build();
    if (!tryLayout(row, {0, 800, 0, 600}))
        return false;
    return checkGeometry(*row, 0, 0, 800, 40, "row 32");
}

// 33. Column 在 Center 中，默认 mainAxisSize.max，填满高度，宽度取子项最大值
bool test33_column_default_max_center()
{
    auto col = Column("col")
                   .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
                   .children(Container("red").width(50).height(30),
                             Container("green").width(70).height(40))
                   .build();
    if (!tryLayout(col, {0, 800, 0, 600}))
        return false;
    return checkGeometry(*col, 0, 0, 70, 600, "col 33");
}

// 34. Row mainAxisSize.min，收缩到子项总宽度
bool test34_row_min_center()
{
    auto row = Row("row")
                   .mainAxisSize(MainAxisSize::min)
                   .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
                   .children(Container("red").width(50).height(30),
                             Container("green").width(70).height(40))
                   .build();
    if (!tryLayout(row, {0, 800, 0, 600}))
        return false;
    return checkGeometry(*row, 0, 0, 120, 40, "row 34");
}

// 35. Column mainAxisSize.min，收缩到子项总高度
bool test35_column_min_center()
{
    auto col = Column("col")
                   .mainAxisSize(MainAxisSize::min)
                   .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
                   .children(Container("red").width(50).height(30),
                             Container("green").width(70).height(40))
                   .build();
    if (!tryLayout(col, {0, 800, 0, 600}))
        return false;
    return checkGeometry(*col, 0, 0, 70, 70, "col 35");
}

// 36. 空 Row 在 Center 中，宽度填满，高度 0
bool test36_empty_row_center()
{
    auto row = Row("row")
                   .crossAxisAlignment(CrossAxisAlignment::start)
                   .build(); // 显式设置 start
    if (!tryLayout(row, {0, 800, 0, 600}))
        return false;
    return checkGeometry(*row, 0, 0, 800, 0, "row 36");
}

// 37. 空 Column 在 Center 中，高度填满，宽度 0
bool test37_empty_column_center()
{
    auto col = Column("col")
                   .crossAxisAlignment(CrossAxisAlignment::start)
                   .build(); // 显式设置 start
    if (!tryLayout(col, {0, 800, 0, 600}))
        return false;
    return checkGeometry(*col, 0, 0, 0, 600, "col 37");
}

// 38. Container 在 tight 约束下，无固定尺寸但有 child，外层填满，child 被拉伸
bool test38_container_tight_constraints_fills_parent_child_stretched()
{
    auto container = Container("outer").child(Container("inner")).build();
    if (!tryLayout(container, {200, 200, 150, 150})) // tight 200x150
        return false;
    return checkGeometry(*container, 0, 0, 200, 150, "outer 38") &&
           checkGeometry(*container->children[0], 0, 0, 200, 150, "inner 38");
}

// 39. 嵌套 Container：外层固定 300x300，无 alignment，内层被强制为相同尺寸，其 child 也拉伸
bool test39_nested_container_outer_fixed_inner_forced()
{
    auto outer =
        Container("outer")
            .width(300)
            .height(300)
            .child(Container("inner").child(Container("content").width(50).height(40)))
            .build();
    if (!tryLayout(outer, {0, 800, 0, 600}))
        return false;
    Node *inner = outer->children[0].get();
    Node *content = inner->children[0].get();
    return checkGeometry(*outer, 0, 0, 300, 300, "outer 39") &&
           checkGeometry(*inner, 0, 0, 300, 300, "inner 39") &&
           checkGeometry(*content, 0, 0, 300, 300, "content 39");
}

// 40. Container 有 alignment，固定尺寸，child 无固定尺寸，松散约束下 child 扩展至最大
bool test40_container_alignment_loose_child_expands()
{
    auto outer = Container("outer")
                     .width(200)
                     .height(200)
                     .alignment(Alignment::center)
                     .child(Container("inner"))
                     .build();
    if (!tryLayout(outer, {0, 800, 0, 600}))
        return false;
    return checkGeometry(*outer, 0, 0, 200, 200, "outer 40") &&
           checkGeometry(*outer->children[0], 0, 0, 200, 200, "inner 40");
}

// 41. Container 无固定尺寸但有 padding，child 固定尺寸，收缩到 child+padding
bool test41_container_padding_child_fixed()
{
    auto container = Container("container")
                         .padding(EdgeInsets::all(10))
                         .child(Container("child").width(60).height(40))
                         .build();
    if (!tryLayout(container, {0, 800, 0, 600}))
        return false;
    return checkGeometry(*container, 0, 0, 80, 60, "container 41");
}

// 42. Row 在 Center 中，包含 Expanded，主轴填满，分配剩余空间
bool test42_row_expanded_center()
{
    auto row = Row("row")
                   .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
                   .children(Container("red").width(50).height(30),
                             Expanded(1, "exp").child(Container("green").height(40)),
                             Container("blue").width(70).height(40))
                   .build();
    if (!tryLayout(row, {0, 800, 0, 600}))
        return false;
    Node *green = row->children[1]->children[0].get();
    return checkGeometry(*row, 0, 0, 800, 40, "row 42") &&
           checkGeometry(*green, 50, 0, 680, 40, "green 42");
}

// 43. Column 在 Center 中，包含 Expanded，主轴填满，分配剩余空间
bool test43_column_expanded_center()
{
    auto col = Column("col")
                   .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
                   .children(Container("red").width(100).height(30),
                             Expanded(1, "exp").child(Container("green").width(80)),
                             Container("blue").width(100).height(40))
                   .build();
    if (!tryLayout(col, {0, 800, 0, 600}))
        return false;
    Node *green = col->children[1]->children[0].get();
    return checkGeometry(*col, 0, 0, 100, 600, "col 43") &&
           checkGeometry(*green, 0, 30, 80, 530, "green 43");
}

// 44. 多层嵌套：Column > Row > Expanded，验证约束传递
bool test44_nested_column_row_expanded_constraint_propagation()
{
    auto col =
        Column("col")
            .crossAxisAlignment(CrossAxisAlignment::stretch)
            .children(
                Container("red").height(50),
                Expanded(1, "exp_col")
                    .child(
                        Row("row")
                            .crossAxisAlignment(CrossAxisAlignment::stretch)
                            .children(Container("green").width(40),
                                      Expanded(1, "exp_row").child(Container("blue")))))
            .build();
    if (!tryLayout(col, {300, 300, 200, 200})) // tight 300x200
        return false;
    Node *row = col->children[1]->children[0].get();
    Node *blue = row->children[1]->children[0].get();
    return checkGeometry(*col, 0, 0, 300, 200, "col 44") &&
           checkGeometry(*row, 0, 50, 300, 150, "row 44") &&
           checkGeometry(*blue, 40, 0, 260, 150, "blue 44");
}

// 45. 父节点无固定尺寸，子节点无 child，两者都扩展至最大
bool test45_parent_child_no_size_expand()
{
    auto outer = Container("outer").child(Container("inner")).build();
    if (!tryLayout(outer, {0, 800, 0, 600}))
        return false;
    return checkGeometry(*outer, 0, 0, 800, 600, "outer 45") &&
           checkGeometry(*outer->children[0], 0, 0, 800, 600, "inner 45");
}

// 46. Row 固定宽度，子项无固定宽度（自然宽度 0）但高度固定
bool test46_row_child_no_width_fixed_height()
{
    auto row = Row("row")
                   .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
                   .children(Container("red").height(40),
                             Container("green").width(70).height(30))
                   .build();
    if (!tryLayout(row, {300, 300, 0, 600})) // 宽度 tight 300，高度松散
        return false;
    Node *red = row->children[0].get();
    Node *green = row->children[1].get();
    return checkGeometry(*row, 0, 0, 300, 40, "row 46") &&
           checkGeometry(*red, 0, 0, 0, 40, "red 46") &&
           checkGeometry(*green, 0, 0, 70, 30, "green 46");
}

// 47. Padding 添加内边距
bool test47_padding_insets()
{
    auto padding = Padding("padding", EdgeInsets::all(10))
                       .child(SizedBox("child").width(50).height(30))
                       .build();
    if (!tryLayout(padding, {0, 800, 0, 600}))
        return false;
    return checkGeometry(*padding, 0, 0, 70, 50, "padding 47") &&
           checkGeometry(*padding->children[0], 10, 10, 50, 30, "child 47");
}

// 48. Center 在有界松散约束下填满并居中子组件
bool test48_center_fills_and_centers()
{
    auto outer = Center("outer")
                     .child(Center("inner").child(SizedBox("child").width(60).height(40)))
                     .build();
    if (!tryLayout(outer, {0, 800, 0, 600}))
        return false;
    Node *inner = outer->children[0].get();
    Node *child = inner->children[0].get();
    return checkGeometry(*outer, 0, 0, 800, 600, "outer center 48") &&
           checkGeometry(*inner, 0, 0, 800, 600, "inner center 48") &&
           checkGeometry(*child, 370, 280, 60, 40, "child 48");
}

// 49. Align 在 tight 约束下填满并按 alignment 定位
bool test49_align_bottom_right()
{
    auto align = Align("align", Alignment::bottomRight)
                     .child(SizedBox("child").width(50).height(30))
                     .build();
    if (!tryLayout(align, {200, 200, 100, 100})) // tight 200x100
        return false;
    return checkGeometry(*align, 0, 0, 200, 100, "align 49") &&
           checkGeometry(*align->children[0], 150, 70, 50, 30, "child 49");
}

// 50. Spacer 在 Row 中占据剩余空间（通过子项位置和 Spacer 尺寸验证）
bool test50_spacer_in_row()
{
    auto row = Row("row")
                   .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
                   .children(Container("red").width(50).height(30), Spacer(),
                             Container("green").width(70).height(40))
                   .build();
    if (!tryLayout(row, {300, 300, 100, 100})) // tight 300x100
        return false;
    Node *spacer = row->children[1].get();
    return checkGeometry(*row, 0, 0, 300, 100, "row 50") &&
           checkGeometry(*row->children[0], 0, 0, 50, 30, "red 50") &&
           checkGeometry(*spacer, 50, 0, 180, 0,
                         "spacer 50") && // Spacer 尺寸：宽 180，高 0（crossAlign start）
           checkGeometry(*row->children[2], 230, 0, 70, 40, "green 50");
}

// 51. Spacer 在 Column 中占据剩余空间
bool test51_spacer_in_column()
{
    auto col = Column("col")
                   .crossAxisAlignment(CrossAxisAlignment::start) // 显式设置 start
                   .children(Container("red").width(100).height(50), Spacer(),
                             Container("green").width(100).height(60))
                   .build();
    if (!tryLayout(col, {200, 200, 300, 300})) // tight 200x300
        return false;
    Node *spacer = col->children[1].get();
    return checkGeometry(*col, 0, 0, 200, 300, "col 51") &&
           checkGeometry(*col->children[0], 0, 0, 100, 50, "red 51") &&
           checkGeometry(*spacer, 0, 50, 0, 190,
                         "spacer 51") && // Spacer 尺寸：宽 0，高 190
           checkGeometry(*col->children[2], 0, 240, 100, 60, "green 51");
}

// ============================================================================
// 新增测试用例 52～74
// ============================================================================

// 52. Stack 非定位子元素：左上角堆叠，尺寸由最大子元素决定（tight 约束）
bool test52_stack_nonpositioned_tight()
{
    auto stack = Stack("stack")
                     .children(Container("red").width(50).height(50),
                               Container("green").width(80).height(30))
                     .build();
    if (!tryLayout(stack, {200, 200, 200, 200})) // tight 200x200
        return false;
    return checkGeometry(*stack, 0, 0, 200, 200, "stack52") &&
           checkGeometry(*stack->children[0], 0, 0, 50, 50, "red52") &&
           checkGeometry(*stack->children[1], 0, 0, 80, 30, "green52");
}

// 53. Stack 在松散约束下（无固定尺寸）收缩到最大非定位子元素尺寸
bool test53_stack_shrink_to_max_nonpositioned()
{
    auto stack = Stack("stack")
                     .children(Container("red").width(50).height(70),
                               Container("green").width(80).height(40))
                     .build();
    if (!tryLayout(stack, {0, 800, 0, 600})) // 松散约束
        return false;
    return checkGeometry(*stack, 0, 0, 80, 70, "stack53");
}

// 54. Positioned 四边定位：子元素尺寸由 left/right/top/bottom 决定
bool test54_positioned_all_sides()
{
    auto stack =
        Stack("stack")
            .children(Positioned("pos").left(10).top(20).right(30).bottom(40).child(
                Container("child").width(100).height(80)) // 固定尺寸但会被拉伸
                      )
            .build();
    if (!tryLayout(stack, {200, 200, 150, 150})) // tight 200x150
        return false;
    Node *child = stack->children[0]->children[0].get();
    return checkGeometry(*stack, 0, 0, 200, 150, "stack54") &&
           checkGeometry(*child, 10, 20, 160, 90, "child54");
}

// 55. Positioned 仅 left/top：子元素保持自身尺寸，定位到指定偏移
bool test55_positioned_left_top()
{
    auto stack = Stack("stack")
                     .children(Positioned("pos").left(30).top(40).child(
                         Container("child").width(60).height(50)))
                     .build();
    if (!tryLayout(stack, {200, 200, 200, 200})) // tight 200x200
        return false;
    Node *child = stack->children[0]->children[0].get();
    return checkGeometry(*stack, 0, 0, 200, 200, "stack55") &&
           checkGeometry(*child, 30, 40, 60, 50, "child55");
}

// 56. Positioned.fill 填满 Stack
bool test56_positioned_fill()
{
    auto stack = Stack("stack")
                     .children(Positioned("pos").fill().child(Container("child")))
                     .build();
    if (!tryLayout(stack, {180, 180, 120, 120})) // tight 180x120
        return false;
    Node *child = stack->children[0]->children[0].get();
    return checkGeometry(*stack, 0, 0, 180, 120, "stack56") &&
           checkGeometry(*child, 0, 0, 180, 120, "child56");
}

// 57. Stack 在 Center 中（松散约束）收缩到最大子元素尺寸
bool test57_stack_in_center_shrink()
{
    auto stack = Stack("stack")
                     .children(Container("red").width(100).height(50),
                               Container("green").width(80).height(40))
                     .build();
    if (!tryLayout(stack, {0, 800, 0, 600})) // 松散约束
        return false;
    return checkGeometry(*stack, 0, 0, 100, 50, "stack57");
}

// 58. Flexible 默认 fit.loose：主轴保持自身尺寸，交叉轴受父约束影响
bool test58_flexible_loose()
{
    auto row =
        Row("row")
            .width(300)
            .height(100)
            .crossAxisAlignment(CrossAxisAlignment::center) // 默认就是 center，显式指明
            .children(Container("red").width(50).height(30),
                      Flexible(1, FlexFit::loose, "flex")
                          .child(Container("green").width(80).height(40)),
                      Container("blue").width(70).height(40))
            .build();
    if (!tryLayout(row, {300, 300, 100, 100})) // tight 300x100
        return false;
    Node *green = row->children[1]->children[0].get();
    // 交叉轴居中：red y=(100-30)/2=35, green y=(100-40)/2=30, blue y=(100-40)/2=30
    // 主轴从 start 开始：red x=0, green x=50, blue x=50+80=130
    return checkGeometry(*row, 0, 0, 300, 100, "row58") &&
           checkGeometry(*row->children[0], 0, 35, 50, 30, "red58") &&
           checkGeometry(*green, 50, 30, 80, 40, "green58") &&
           checkGeometry(*row->children[2], 130, 30, 70, 40, "blue58");
}

// 59. Flexible fit.tight 强制拉伸主轴（类似 Expanded）
bool test59_flexible_tight()
{
    auto row = Row("row")
                   .width(300)
                   .height(100)
                   .children(Container("red").width(50).height(30),
                             Flexible(1, FlexFit::tight, "flex")
                                 .child(Container("green")), // 无固定尺寸
                             Container("blue").width(70).height(40))
                   .build();
    if (!tryLayout(row, {300, 300, 100, 100})) // tight 300x100
        return false;
    Node *green = row->children[1]->children[0].get();
    return checkGeometry(*row, 0, 0, 300, 100, "row59") &&
           checkGeometry(*green, 50, 0, 180, 100, "green59");
}

// 60. 多个 Flexible 按 flex 比例分配剩余空间
bool test60_multiple_flexible()
{
    auto row =
        Row("row")
            .width(300)
            .height(100)
            .children(Flexible(2, FlexFit::tight, "flex1").child(Container("green")),
                      Flexible(1, FlexFit::tight, "flex2").child(Container("orange")))
            .build();
    if (!tryLayout(row, {300, 300, 100, 100})) // tight 300x100
        return false;
    Node *green = row->children[0]->children[0].get();
    Node *orange = row->children[1]->children[0].get();
    return checkGeometry(*row, 0, 0, 300, 100, "row60") &&
           checkGeometry(*green, 0, 0, 200, 100, "green60") &&
           checkGeometry(*orange, 200, 0, 100, 100, "orange60");
}

// 61. Wrap 自动换行
bool test61_wrap_wrap()
{
    auto wrap = Wrap("wrap")
                    .children(Container("red").width(60).height(30),
                              Container("green").width(60).height(30),
                              Container("blue").width(60).height(30))
                    .build();
    if (!tryLayout(wrap, {0, 150, 0, 600})) // 宽度 tight 150，高度松散
        return false;
    return checkGeometry(*wrap, 0, 0, 150, 60, "wrap61") &&
           checkGeometry(*wrap->children[0], 0, 0, 60, 30, "red61") &&
           checkGeometry(*wrap->children[1], 60, 0, 60, 30, "green61") &&
           checkGeometry(*wrap->children[2], 0, 30, 60, 30, "blue61");
}

// 62. Wrap 的 spacing 和 runSpacing
bool test62_wrap_spacing()
{
    auto wrap = Wrap("wrap")
                    .spacing(10)
                    .runSpacing(5)
                    .children(Container("red").width(50).height(30),
                              Container("green").width(50).height(30),
                              Container("blue").width(50).height(30))
                    .build();
    if (!tryLayout(wrap, {0, 150, 0, 600})) // 宽度 150
        return false;
    return checkGeometry(*wrap, 0, 0, 150, 65, "wrap62") &&
           checkGeometry(*wrap->children[0], 0, 0, 50, 30, "red62") &&
           checkGeometry(*wrap->children[1], 60, 0, 50, 30, "green62") &&
           checkGeometry(*wrap->children[2], 0, 35, 50, 30, "blue62");
}

// 63. Wrap 的 alignment 和 runAlignment
bool test63_wrap_alignment()
{
    auto wrap = Wrap("wrap")
                    .alignment(WrapAlignment::center)
                    .runAlignment(WrapAlignment::center)
                    .spacing(10)
                    .runSpacing(10)
                    .children(Container("red").width(50).height(30),
                              Container("green").width(50).height(30),
                              Container("blue").width(50).height(30),
                              Container("orange").width(50).height(30))
                    .build();
    if (!tryLayout(wrap, {0, 200, 0, 150})) // 宽度 200，高度 150
        return false;
    // 验证位置，因为浮点，使用 approx
    if (!approx(wrap->geometry.w, 200) || !approx(wrap->geometry.h, 150))
        return false;
    Node *red = wrap->children[0].get();
    Node *green = wrap->children[1].get();
    Node *blue = wrap->children[2].get();
    Node *orange = wrap->children[3].get();
    // 第一行：红绿蓝，总宽 50+10+50+10+50=170，父宽200，偏移15
    // 垂直：内容高度 30+10+30=70，父高150，偏移40
    return approx(red->geometry.x, 15) && approx(red->geometry.y, 40) &&
           approx(green->geometry.x, 75) && approx(green->geometry.y, 40) &&
           approx(blue->geometry.x, 135) && approx(blue->geometry.y, 40) &&
           approx(orange->geometry.x, 75) && approx(orange->geometry.y, 80);
}

// 64. AspectRatio 在宽度约束下：高度 = 宽度 / aspectRatio
bool test64_aspectratio_width()
{
    auto aspect = AspectRatio(16.0f / 9.0f, "aspect").child(Container("red")).build();
    if (!tryLayout(aspect, {200, 200, 0, Constraints::inf})) // 宽度 tight 200，高度松散
        return false;
    return checkGeometry(*aspect, 0, 0, 200, 112.5f, "aspect64") &&
           checkGeometry(*aspect->children[0], 0, 0, 200, 112.5f, "red64");
}

// 65. AspectRatio 在高度约束下：宽度 = 高度 * aspectRatio
bool test65_aspectratio_height()
{
    auto aspect = AspectRatio(4.0f / 3.0f, "aspect").child(Container("green")).build();
    if (!tryLayout(aspect, {0, Constraints::inf, 100, 100})) // 高度 tight 100，宽度松散
        return false;
    float expectedW = 100 * 4.0f / 3.0f;
    return checkGeometry(*aspect, 0, 0, expectedW, 100, "aspect65") &&
           checkGeometry(*aspect->children[0], 0, 0, expectedW, 100, "green65");
}

// 66. AspectRatio 在 tight 约束下：自身尺寸等于父约束（填满父容器）
bool test66_aspectratio_tight()
{
    auto aspect = AspectRatio(1.0f, "aspect").child(Container("blue")).build();
    if (!tryLayout(aspect, {200, 200, 150, 150})) // tight 200x150
        return false;
    return checkGeometry(*aspect, 0, 0, 200, 150, "aspect66") &&
           checkGeometry(*aspect->children[0], 0, 0, 200, 150, "blue66");
}

// 67. ConstrainedBox 最小尺寸约束：子项被拉伸
bool test67_constrainedbox_min()
{
    auto constrained =
        ConstrainedBox(Constraints{100, Constraints::inf, 80, Constraints::inf}, "con")
            .child(Container("red").width(50).height(30))
            .build();
    if (!tryLayout(constrained, {0, 800, 0, 600})) // 松散约束
        return false;
    return checkGeometry(*constrained, 0, 0, 100, 80, "con67") &&
           checkGeometry(*constrained->children[0], 0, 0, 100, 80, "red67");
}

// 68. ConstrainedBox 最大尺寸约束：子项被压缩
bool test68_constrainedbox_max()
{
    auto constrained = ConstrainedBox(Constraints{0, 60, 0, 40}, "con")
                           .child(Container("green").width(100).height(80))
                           .build();
    if (!tryLayout(constrained, {0, 800, 0, 600})) // 松散约束
        return false;
    return checkGeometry(*constrained, 0, 0, 60, 40, "con68") &&
           checkGeometry(*constrained->children[0], 0, 0, 60, 40, "green68");
}

// 69. ConstrainedBox 约束取交集：父约束更严格时生效
bool test69_constrainedbox_intersect()
{
    auto constrained =
        ConstrainedBox(Constraints{100, Constraints::inf, 100, Constraints::inf}, "con")
            .child(Container("blue"))
            .build();
    if (!tryLayout(constrained, {50, 50, 50, 50})) // tight 50x50
        return false;
    return checkGeometry(*constrained, 0, 0, 50, 50, "con69") &&
           checkGeometry(*constrained->children[0], 0, 0, 50, 50, "blue69");
}

// 70. ConstrainedBox.expand() 填满父容器
bool test70_constrainedbox_expand()
{
    auto constrained = ConstrainedBox(Constraints{Constraints::inf, Constraints::inf,
                                                  Constraints::inf, Constraints::inf},
                                      "con")
                           .child(Container("yellow"))
                           .build();
    if (!tryLayout(constrained, {120, 120, 90, 90})) // tight 120x90
        return false;
    return checkGeometry(*constrained, 0, 0, 120, 90, "con70") &&
           checkGeometry(*constrained->children[0], 0, 0, 120, 90, "yellow70");
}

// 71. FractionallySizedBox 宽度因子
bool test71_fractionally_width_factor()
{
    auto frac =
        FractionallySizedBox(0.5f, std::nullopt, "frac").child(Container("red")).build();
    if (!tryLayout(frac, {200, 200, 100, 100})) // tight 200x100
        return false;
    return checkGeometry(*frac, 0, 0, 200, 100, "frac71") &&
           checkGeometry(*frac->children[0], 0, 0, 100, 100, "red71");
}

// 72. FractionallySizedBox 高度因子
bool test72_fractionally_height_factor()
{
    auto frac = FractionallySizedBox(std::nullopt, 0.6f, "frac")
                    .child(Container("green"))
                    .build();
    if (!tryLayout(frac, {200, 200, 150, 150})) // tight 200x150
        return false;
    return checkGeometry(*frac, 0, 0, 200, 150, "frac72") &&
           checkGeometry(*frac->children[0], 0, 0, 200, 90, "green72");
}

// 73. FractionallySizedBox 宽高因子同时设置，子项居中（默认 alignment.center）
bool test73_fractionally_both_center()
{
    auto frac = FractionallySizedBox(0.4f, 0.5f, "frac").child(Container("blue")).build();
    if (!tryLayout(frac, {300, 300, 200, 200})) // tight 300x200
        return false;
    Node *child = frac->children[0].get();
    // 预期子项尺寸 120x100，位置 (90,50)
    return checkGeometry(*frac, 0, 0, 300, 200, "frac73") &&
           checkGeometry(*child, 90, 50, 120, 100, "blue73");
}

// 74. FractionallySizedBox 在松散约束下（父有界）使用约束最大值乘因子
bool test74_fractionally_loose()
{
    auto frac = FractionallySizedBox(0.5f, 0.5f, "frac").child(Container("pink")).build();
    if (!tryLayout(frac, {0, 800, 0, 600})) // 松散 800x600
        return false;
    // 预期子项尺寸 400x300，位置 (0,0) 因为父尺寸就是 400x300
    return checkGeometry(*frac, 0, 0, 400, 300, "frac74") &&
           checkGeometry(*frac->children[0], 0, 0, 400, 300, "pink74");
}

// ============================================================================
// 更新 runTests 调用新增测试
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
           test_deep_nested_canvas_size() &&
           test29_container_no_child_no_size_fills_constraints() &&
           test30_container_no_child_no_size_without_color_fills_constraints() &&
           test31_container_with_child_sizedbox_shrinks_to_zero() &&
           test32_row_default_max_center() && test33_column_default_max_center() &&
           test34_row_min_center() && test35_column_min_center() &&
           test36_empty_row_center() && test37_empty_column_center() &&
           test38_container_tight_constraints_fills_parent_child_stretched() &&
           test39_nested_container_outer_fixed_inner_forced() &&
           test40_container_alignment_loose_child_expands() &&
           test41_container_padding_child_fixed() && test42_row_expanded_center() &&
           test43_column_expanded_center() &&
           test44_nested_column_row_expanded_constraint_propagation() &&
           test45_parent_child_no_size_expand() &&
           test46_row_child_no_width_fixed_height() && test47_padding_insets() &&
           test48_center_fills_and_centers() && test49_align_bottom_right() &&
           test50_spacer_in_row() && test51_spacer_in_column() &&
           // 新增测试 52~74
           test52_stack_nonpositioned_tight() &&
           test53_stack_shrink_to_max_nonpositioned() && test54_positioned_all_sides() &&
           test55_positioned_left_top() && test56_positioned_fill() &&
           test57_stack_in_center_shrink() && test58_flexible_loose() &&
           test59_flexible_tight() && test60_multiple_flexible() && test61_wrap_wrap() &&
           test62_wrap_spacing() && test63_wrap_alignment() &&
           test64_aspectratio_width() && test65_aspectratio_height() &&
           test66_aspectratio_tight() && test67_constrainedbox_min() &&
           test68_constrainedbox_max() && test69_constrainedbox_intersect() &&
           test70_constrainedbox_expand() && test71_fractionally_width_factor() &&
           test72_fractionally_height_factor() && test73_fractionally_both_center() &&
           test74_fractionally_loose();
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