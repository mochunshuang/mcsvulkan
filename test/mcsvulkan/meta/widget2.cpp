#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// ========== 基础数学类型 ==========
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

    // 减去 padding/margin，确保不会小于 0（Flutter 中 deflate 会 clamp 到 0）
    constexpr Constraints deflate(const EdgeInsets &e) const noexcept
    {
        float h = e.left + e.right, v = e.top + e.bottom;
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
    Expanded,
    Text
};

struct ContainerStyle
{
    std::optional<float> width, height;
    EdgeInsets margin{}, padding{};
    Alignment alignment = Align::topLeft;
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

std::vector<ContainerStyle> g_containers;
std::vector<RowStyle> g_rows;
std::vector<ColumnStyle> g_columns;
std::vector<ExpandedStyle> g_expandeds;
std::vector<TextStyle> g_texts;

struct WidgetRef
{
    WidgetKind kind;
    uint32_t idx;
};

struct BoxGeometry
{
    float x, y, w, h;
    float padL, padR, padT, padB;
    static BoxGeometry make(const Offset &pos, const Size &size, const EdgeInsets &pad)
    {
        return {pos.x,    pos.y,     size.width, size.height,
                pad.left, pad.right, pad.top,    pad.bottom};
    }
};

struct Node
{
    WidgetRef ref;
    std::vector<Node> children;
    BoxGeometry geometry;
    std::string name;
};

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
Alignment alignmentOf(const WidgetRef &ref)
{
    if (ref.kind == WidgetKind::Container)
        return g_containers[ref.idx].alignment;
    return Align::topLeft;
}

using MeasureFunc = Size (*)(const WidgetRef &ref, Constraints bc);
std::unordered_map<WidgetKind, MeasureFunc> g_measureFuncs;

// ---------- 工厂函数（清晰区分可选与固定）----------
WidgetRef makeContainer(std::optional<float> w, std::optional<float> h, EdgeInsets m = {},
                        EdgeInsets p = {}, Alignment a = Align::topLeft)
{
    uint32_t idx = g_containers.size();
    g_containers.push_back(ContainerStyle{w, h, m, p, a});
    return {WidgetKind::Container, idx};
}
WidgetRef makeContainer(EdgeInsets m, EdgeInsets p = {}, Alignment a = Align::topLeft)
{
    return makeContainer(std::nullopt, std::nullopt, m, p, a);
}
WidgetRef makeContainer(float w, float h, EdgeInsets m = {}, EdgeInsets p = {},
                        Alignment a = Align::topLeft)
{
    return makeContainer(std::optional<float>(w), std::optional<float>(h), m, p, a);
}

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

// ========== 核心布局 ==========
void layout(Node &node, Constraints constraints);

void layoutContainer(Node &node, Constraints borderBC, const EdgeInsets &pad)
{
    auto &ref = node.ref;
    if (node.children.empty())
    {
        if (!widthOf(ref) && borderBC.hasUnboundedWidth())
            throw std::logic_error(
                "Container '" + node.name +
                "' has no explicit width and parent provides unbounded width.");
        if (!heightOf(ref) && borderBC.hasUnboundedHeight())
            throw std::logic_error(
                "Container '" + node.name +
                "' has no explicit height and parent provides unbounded height.");
        float w = widthOf(ref).value_or(borderBC.maxW);
        float h = heightOf(ref).value_or(borderBC.maxH);
        Size size = borderBC.clamp({w, h});
        node.geometry =
            BoxGeometry::make({marginOf(ref).left, marginOf(ref).top}, size, pad);
        return;
    }
    if (node.children.size() != 1)
        throw std::logic_error("Container '" + node.name +
                               "' must have exactly one child.");

    Node &child = node.children[0];
    // 子节点收到宽松约束（loose constraint），使其能自由决定尺寸
    Constraints innerBC = borderBC.deflate(pad);
    Constraints looseBC = {0.0f, innerBC.maxW, 0.0f, innerBC.maxH};
    layout(child, looseBC);

    const auto &childMargin = marginOf(child.ref);
    float childFullW = child.geometry.w + childMargin.horizontal();
    float childFullH = child.geometry.h + childMargin.vertical();

    // 有子节点且无固定宽高时，Container 收缩到子节点大小 + padding
    float containerW = widthOf(ref).value_or(childFullW + pad.horizontal());
    float containerH = heightOf(ref).value_or(childFullH + pad.vertical());
    Size containerSize = borderBC.clamp({containerW, containerH});

    // 在容器内容区内按 alignment 定位子节点
    Alignment align = alignmentOf(ref);
    float extraW = std::max(0.0f, containerSize.width - pad.horizontal() - childFullW);
    float extraH = std::max(0.0f, containerSize.height - pad.vertical() - childFullH);
    child.geometry.x = pad.left + childMargin.left + extraW * (align.x + 1.0f) / 2.0f;
    child.geometry.y = pad.top + childMargin.top + extraH * (align.y + 1.0f) / 2.0f;

    node.geometry =
        BoxGeometry::make({marginOf(ref).left, marginOf(ref).top}, containerSize, pad);
}

void layoutText(Node &node, Constraints borderBC, const EdgeInsets &pad)
{
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
    node.geometry = BoxGeometry::make({marginOf(ref).left, marginOf(ref).top}, size, pad);
}

void layoutFlex(Node &node, Constraints borderBC, const EdgeInsets &pad, bool isRow)
{
    auto &ref = node.ref;
    bool isBoundedMain = isRow ? borderBC.hasBoundedWidth() : borderBC.hasBoundedHeight();
    bool isBoundedCross =
        isRow ? borderBC.hasBoundedHeight() : borderBC.hasBoundedWidth();
    if (!isBoundedCross)
        throw std::logic_error("Row/Column '" + node.name +
                               "' has unbounded cross axis.");
    if (!isBoundedMain)
    {
        for (auto &child : node.children)
            if (child.ref.kind == WidgetKind::Expanded)
                throw std::logic_error("Row/Column '" + node.name +
                                       "' has unbounded main axis and Expanded child.");
    }

    float mainSize = isRow ? borderBC.maxW : borderBC.maxH;
    float crossSize = isRow ? borderBC.maxH : borderBC.maxW;
    float padMainStart = isRow ? pad.left : pad.top;
    float padMainEnd = isRow ? pad.right : pad.bottom;
    float padCrossStart = isRow ? pad.top : pad.left;
    float padCrossEnd = isRow ? pad.bottom : pad.right;

    float innerMain = mainSize - padMainStart - padMainEnd;
    if (!isBoundedMain)
        innerMain = Constraints::inf;
    float innerCross = crossSize - padCrossStart - padCrossEnd;

    struct ChildInfo
    {
        Node *node;
        float flex;
        float marginMain;
        float marginCross;
    };
    std::vector<ChildInfo> infos;
    float totalFlex = 0;

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
            totalFlex += flexVal;
        }
        else
        {
            const auto &m = marginOf(child.ref);
            float mM = isRow ? m.horizontal() : m.vertical();
            float mC = isRow ? m.vertical() : m.horizontal();
            infos.push_back({&child, 0.0f, mM, mC});
        }
    }

    CrossAxisAlignment crossAlign =
        isRow ? g_rows[ref.idx].crossAlign : g_columns[ref.idx].crossAlign;
    MainAxisSize axisSize =
        isRow ? g_rows[ref.idx].mainAxisSize : g_columns[ref.idx].mainAxisSize;

    std::vector<float> naturalMain;
    for (auto &info : infos)
    {
        float childMainMax =
            (info.flex > 0) ? 0.0f : (isBoundedMain ? innerMain : Constraints::inf);
        float childCrossMax = innerCross;
        float childCrossMin =
            (crossAlign == CrossAxisAlignment::stretch) ? childCrossMax : 0.0f;

        Constraints childBC;
        if (isRow)
            childBC = {0.0f, childMainMax, childCrossMin, childCrossMax};
        else
            childBC = {childCrossMin, childCrossMax, 0.0f, childMainMax};

        layout(*info.node, childBC);
        float cmain = isRow ? info.node->geometry.w : info.node->geometry.h;
        naturalMain.push_back(cmain);
    }

    float totalNatural = 0;
    for (size_t i = 0; i < infos.size(); ++i)
        totalNatural += naturalMain[i] + infos[i].marginMain;

    float finalInnerMain;
    if (!isBoundedMain)
        finalInnerMain = totalNatural;
    else if (axisSize == MainAxisSize::min)
        finalInnerMain = std::min(totalNatural, innerMain);
    else
        finalInnerMain = innerMain;

    float freeMain = std::max(0.0f, finalInnerMain - totalNatural);
    if (totalFlex > 0)
    {
        float flexUnit = freeMain / totalFlex;
        for (size_t i = 0; i < infos.size(); ++i)
        {
            if (infos[i].flex > 0)
            {
                naturalMain[i] = infos[i].flex * flexUnit;
                float boxMain = naturalMain[i] + infos[i].marginMain;
                float childCrossMax = innerCross;
                float childCrossMin =
                    (crossAlign == CrossAxisAlignment::stretch) ? childCrossMax : 0.0f;

                Constraints tightBC;
                if (isRow)
                    tightBC = {boxMain, boxMain, childCrossMin, childCrossMax};
                else
                    tightBC = {childCrossMin, childCrossMax, boxMain, boxMain};

                layout(*infos[i].node, tightBC);
                naturalMain[i] =
                    isRow ? infos[i].node->geometry.w : infos[i].node->geometry.h;
            }
        }
    }

    float totalMainUsed = 0;
    for (size_t i = 0; i < infos.size(); ++i)
        totalMainUsed += naturalMain[i] + infos[i].marginMain;
    float extraMain = std::max(0.0f, finalInnerMain - totalMainUsed);

    MainAxisAlignment mainAlign =
        isRow ? g_rows[ref.idx].mainAlign : g_columns[ref.idx].mainAlign;
    float mainGap = 0, mainStartOff = 0;
    if (infos.size() == 1)
    {
        if (mainAlign == MainAxisAlignment::spaceAround ||
            mainAlign == MainAxisAlignment::spaceEvenly)
            mainStartOff = extraMain / 2.0f;
    }
    else if (infos.size() > 1)
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
            mainStartOff = extraMain / 2.0f;
            break;
        case MainAxisAlignment::spaceBetween:
            mainGap = extraMain / (infos.size() - 1);
            break;
        case MainAxisAlignment::spaceAround:
            mainGap = extraMain / infos.size();
            mainStartOff = mainGap / 2.0f;
            break;
        case MainAxisAlignment::spaceEvenly:
            mainGap = extraMain / (infos.size() + 1);
            mainStartOff = mainGap;
            break;
        }
    }

    float mainPos = padMainStart + mainStartOff;
    for (size_t i = 0; i < infos.size(); ++i)
    {
        Node &child = *infos[i].node;
        const auto &childMargin = marginOf(child.ref);
        float childCrossLen = isRow ? child.geometry.h : child.geometry.w;
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
            crossStart = crossExtra / 2.0f;
            break;
        case CrossAxisAlignment::stretch:
            crossStart = 0;
            break;
        }
        if (isRow)
        {
            child.geometry.x = mainPos + childMargin.left;
            child.geometry.y = padCrossStart + crossStart + childMargin.top;
        }
        else
        {
            child.geometry.x = padCrossStart + crossStart + childMargin.left;
            child.geometry.y = mainPos + childMargin.top;
        }
        mainPos += naturalMain[i] + infos[i].marginMain + mainGap;
    }

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
    node.geometry = BoxGeometry::make({marginOf(ref).left, marginOf(ref).top}, size, pad);
}

void layout(Node &node, Constraints constraints)
{
    const auto &ref = node.ref;
    if (ref.kind == WidgetKind::Expanded)
        throw std::logic_error(
            "Expanded widget must be placed directly inside Row/Column/Flex.");

    const auto &margin = marginOf(ref);
    const auto &padding = paddingOf(ref);

    Constraints borderBC = constraints.deflate(margin);
    if (auto w = widthOf(ref))
        borderBC = borderBC.applyFixedWidth(*w);
    if (auto h = heightOf(ref))
        borderBC = borderBC.applyFixedHeight(*h);

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
        break;
    }

    Size finalSize = {node.geometry.w, node.geometry.h};
    if (finalSize.width > constraints.maxW + 1e-6f ||
        finalSize.height > constraints.maxH + 1e-6f)
    {
        std::cerr << "WARNING: Widget '" << node.name << "' overflows parent by ("
                  << finalSize.width - constraints.maxW << ", "
                  << finalSize.height - constraints.maxH << ")\n";
    }
}

// ========== 测试辅助（详细输出） ==========
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
                  << "," << h << ")"
                  << " got (" << node.geometry.x << "," << node.geometry.y << ","
                  << node.geometry.w << "," << node.geometry.h << ")\n";
        return false;
    }
    return true;
}

bool tryLayout(Node &node, Constraints c, const std::string &expectedError = "")
{
    try
    {
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

// ========== 原有测试用例 ==========
bool test_parent_wins()
{
    clearPools();
    // 父容器固定 100x100，子容器固定 50x50（无子节点），子容器应保持 50x50，不被父约束拉伸
    Node parent = {makeContainer(100.0f, 100.0f), {}, {}, "parent"};
    Node child = {makeContainer(50.0f, 50.0f, {5.0f, 5.0f, 5.0f, 5.0f}), {}, {}, "child"};
    parent.children.push_back(std::move(child));
    if (!tryLayout(parent, {0.0f, 800.0f, 0.0f, 600.0f}))
        return false;
    if (!checkGeometry(parent, 0.0f, 0.0f, 100.0f, 100.0f, "parent"))
        return false;
    if (!checkGeometry(parent.children[0], 5.0f, 5.0f, 50.0f, 50.0f, "child fixed"))
        return false;
    return true;
}

bool test_fixed_inside_flex()
{
    clearPools();
    Node row = {makeRow(300.0f, 100.0f), {}, {}, "row"};
    row.children.push_back({makeContainer(50.0f, 30.0f), {}, {}, "c1"});
    if (!tryLayout(row, {0.0f, 500.0f, 0.0f, 200.0f}))
        return false;
    if (!checkGeometry(row, 0.0f, 0.0f, 300.0f, 100.0f, "row"))
        return false;
    if (!checkGeometry(row.children[0], 0.0f, 0.0f, 50.0f, 30.0f, "c1"))
        return false;
    return true;
}

bool test_expanded()
{
    clearPools();
    Node row = {makeRow(300.0f, 100.0f), {}, {}, "row"};
    Node c1 = {makeContainer(50.0f, 30.0f), {}, {}, "c1"};
    Node exp = {makeExpanded(2), {}, {}, "expanded"};
    Node c2 = {makeContainer(std::nullopt, 20.0f), {}, {}, "c2"};
    Node c3 = {makeContainer(70.0f, 40.0f), {}, {}, "c3"};
    exp.children.push_back(std::move(c2));
    row.children.push_back(std::move(c1));
    row.children.push_back(std::move(exp));
    row.children.push_back(std::move(c3));
    if (!tryLayout(row, {0.0f, 300.0f, 0.0f, 100.0f}))
        return false;
    if (!checkGeometry(row, 0.0f, 0.0f, 300.0f, 100.0f, "row"))
        return false;
    if (!checkGeometry(row.children[0], 0.0f, 0.0f, 50.0f, 30.0f, "c1"))
        return false;
    if (!checkGeometry(row.children[1].children[0], 50.0f, 0.0f, 180.0f, 20.0f,
                       "c2 (expanded)"))
        return false;
    if (!checkGeometry(row.children[2], 230.0f, 0.0f, 70.0f, 40.0f, "c3"))
        return false;
    return true;
}

bool test_stretch_overrides_fixed()
{
    clearPools();
    Node row = {
        makeRow(300.0f, 100.0f, MainAxisAlignment::start, CrossAxisAlignment::stretch),
        {},
        {},
        "row"};
    row.children.push_back({makeContainer(50.0f, std::nullopt), {}, {}, "c1"});
    row.children.push_back({makeContainer(70.0f, 40.0f), {}, {}, "c2"});
    if (!tryLayout(row, {0.0f, 300.0f, 0.0f, 100.0f}))
        return false;
    if (!checkGeometry(row.children[0], 0.0f, 0.0f, 50.0f, 100.0f, "c1 stretched"))
        return false;
    if (!checkGeometry(row.children[1], 50.0f, 0.0f, 70.0f, 100.0f, "c2 forced"))
        return false;
    return true;
}

bool test_main_axis_min()
{
    clearPools();
    Node col = {makeColumn(200.0f, std::nullopt, MainAxisAlignment::start,
                           CrossAxisAlignment::start, MainAxisSize::min),
                {},
                {},
                "col"};
    col.children.push_back({makeContainer(100.0f, 30.0f), {}, {}, "c1"});
    col.children.push_back({makeContainer(80.0f, 20.0f), {}, {}, "c2"});
    if (!tryLayout(col, {0.0f, 200.0f, 0.0f, Constraints::inf}))
        return false;
    if (!approx(col.geometry.h, 50.0f))
    {
        std::cerr << "FAIL: min height expected 50 got " << col.geometry.h << "\n";
        return false;
    }
    return true;
}

bool test_text()
{
    clearPools();
    g_measureFuncs[WidgetKind::Text] = [](const WidgetRef &, Constraints) -> Size {
        return {120.0f, 25.0f};
    };
    Node col = {makeColumn(300.0f, 200.0f), {}, {}, "col"};
    col.children.push_back(
        {makeText(std::nullopt, std::nullopt, {5.0f, 5.0f, 5.0f, 5.0f}), {}, {}, "text"});
    if (!tryLayout(col, {0.0f, 300.0f, 0.0f, 200.0f}))
        return false;
    if (!checkGeometry(col.children[0], 5.0f, 5.0f, 120.0f, 25.0f, "text"))
        return false;
    return true;
}

bool test_container_shrink_to_child()
{
    clearPools();
    // 中间 Container 无固定尺寸，有子节点（50x30），margin=10，center 对齐
    // 它应收缩到子节点大小 50x30，加上 margin 后占据 70x50 的盒子区域，但在父容器 200x200 中不会居中产生偏移（因为已经缩小）
    Node parent = {makeContainer(200.0f, 200.0f), {}, {}, "parent"};
    Node child = {
        makeContainer({10.0f, 10.0f, 10.0f, 10.0f}, {}, Align::center), {}, {}, "child"};
    child.children.push_back({makeContainer(50.0f, 30.0f), {}, {}, "inner"});
    parent.children.push_back(std::move(child));
    if (!tryLayout(parent, {0.0f, 200.0f, 0.0f, 200.0f}))
        return false;

    Node &container = parent.children[0];
    if (!checkGeometry(container, 10.0f, 10.0f, 50.0f, 30.0f,
                       "container shrink to 50x30"))
        return false;
    if (!checkGeometry(container.children[0], 0.0f, 0.0f, 50.0f, 30.0f,
                       "inner at origin"))
        return false;
    return true;
}

bool test_unbounded_error()
{
    clearPools();
    Node c = {makeContainer({}), {}, {}, "c"};
    return tryLayout(c, {0.0f, Constraints::inf, 0.0f, 100.0f}, "unbounded width");
}

bool test_expanded_outside_flex()
{
    clearPools();
    Node cont = {makeContainer(200.0f, 200.0f), {}, {}, "cont"};
    Node exp = {makeExpanded(), {}, {}, "exp"};
    exp.children.push_back({makeContainer(50.0f, 50.0f), {}, {}, "inner"});
    cont.children.push_back(std::move(exp));
    return tryLayout(cont, {0.0f, 200.0f, 0.0f, 200.0f}, "Expanded widget");
}

// ========== 新增测试用例（更全面的场景） ==========

// 1. Container 有 padding 时收缩到子节点 + padding
bool test_container_with_padding()
{
    clearPools();
    Node parent = {makeContainer(200.0f, 200.0f), {}, {}, "parent"};
    Node child = {makeContainer(std::nullopt, std::nullopt, EdgeInsets{},
                                EdgeInsets{10, 10, 10, 10}),
                  {},
                  {},
                  "child"};
    child.children.push_back({makeContainer(50.0f, 30.0f), {}, {}, "inner"});
    parent.children.push_back(std::move(child));
    if (!tryLayout(parent, {0.0f, 200.0f, 0.0f, 200.0f}))
        return false;
    if (!checkGeometry(parent.children[0], 0, 0, 70, 50, "container with padding"))
        return false;
    if (!checkGeometry(parent.children[0].children[0], 10, 10, 50, 30,
                       "inner with padding"))
        return false;
    return true;
}

// 2. 多层 Container 嵌套 shrink
bool test_nested_container_shrink()
{
    clearPools();
    Node outer = {makeContainer(200.0f, 200.0f), {}, {}, "outer"};
    Node mid = {
        makeContainer(std::nullopt, std::nullopt, EdgeInsets{}, EdgeInsets{5, 5, 5, 5}),
        {},
        {},
        "mid"};
    mid.children.push_back({makeContainer(30.0f, 20.0f), {}, {}, "inner"});
    outer.children.push_back(std::move(mid));
    if (!tryLayout(outer, {0.0f, 200.0f, 0.0f, 200.0f}))
        return false;
    if (!checkGeometry(outer.children[0], 0, 0, 40, 30, "mid shrink to 40x30"))
        return false;
    if (!checkGeometry(outer.children[0].children[0], 5, 5, 30, 20, "inner at (5,5)"))
        return false;
    return true;
}

// 3. Row 中 stretch 覆盖固定高度，但宽度保持不变
bool test_row_stretch_with_fixed_width()
{
    clearPools();
    Node row = {
        makeRow(300.0f, 100.0f, MainAxisAlignment::start, CrossAxisAlignment::stretch),
        {},
        {},
        "row"};
    row.children.push_back({makeContainer(50.0f, std::nullopt), {}, {}, "c1"});
    row.children.push_back({makeContainer(70.0f, 40.0f), {}, {}, "c2"});
    if (!tryLayout(row, {0.0f, 300.0f, 0.0f, 100.0f}))
        return false;
    if (!checkGeometry(row.children[0], 0, 0, 50, 100,
                       "c1 width fixed, height stretched"))
        return false;
    if (!checkGeometry(row.children[1], 50, 0, 70, 100,
                       "c2 width fixed, height overridden"))
        return false;
    return true;
}

// 4. Column 中 spaceEvenly 分布
bool test_column_space_evenly()
{
    clearPools();
    Node col = {
        makeColumn(100.0f, 200.0f, MainAxisAlignment::spaceEvenly), {}, {}, "col"};
    col.children.push_back({makeContainer(50.0f, 30.0f), {}, {}, "c1"});
    col.children.push_back({makeContainer(50.0f, 30.0f), {}, {}, "c2"});
    col.children.push_back({makeContainer(50.0f, 30.0f), {}, {}, "c3"});
    if (!tryLayout(col, {0.0f, 100.0f, 0.0f, 200.0f}))
        return false;
    // 总高度 200，3 个子节点各 30，总 90，剩余 110
    // spaceEvenly: 4 个间隙各 27.5，起始偏移 27.5
    // c1: 27.5, c2: 27.5+30+27.5=85, c3: 85+30+27.5=142.5
    if (!checkGeometry(col.children[0], 0, 27.5, 50, 30, "c1 at 27.5"))
        return false;
    if (!checkGeometry(col.children[1], 0, 85, 50, 30, "c2 at 85"))
        return false;
    if (!checkGeometry(col.children[2], 0, 142.5, 50, 30, "c3 at 142.5"))
        return false;
    return true;
}

// 5. 固定尺寸被父约束覆盖（父约束更紧）
bool test_fixed_overridden_by_parent()
{
    clearPools();
    Node parent = {makeContainer(50.0f, 50.0f), {}, {}, "parent"};
    parent.children.push_back({makeContainer(100.0f, 100.0f), {}, {}, "child"});
    if (!tryLayout(parent, {0.0f, 200.0f, 0.0f, 200.0f}))
        return false;
    if (!checkGeometry(parent.children[0], 0, 0, 50, 50, "child forced to 50x50"))
        return false;
    return true;
}

// 6. Text 在无界约束下正常工作（测量函数返回固定值）
bool test_text_unbounded()
{
    clearPools();
    g_measureFuncs[WidgetKind::Text] = [](const WidgetRef &, Constraints) -> Size {
        return {120.0f, 25.0f};
    };
    Node text = {makeText(), {}, {}, "text"};
    if (!tryLayout(text, {0.0f, Constraints::inf, 0.0f, 100.0f}))
        return false;
    if (!checkGeometry(text, 0, 0, 120, 25, "text in unbounded width"))
        return false;
    return true;
}

// 7. Expanded 在 Column 中分配剩余空间
bool test_expanded_in_column()
{
    clearPools();
    Node col = {makeColumn(100.0f, 200.0f), {}, {}, "col"};
    col.children.push_back({makeContainer(50.0f, 30.0f), {}, {}, "c1"});
    Node exp = {makeExpanded(2), {}, {}, "exp"};
    exp.children.push_back({makeContainer(50.0f, std::nullopt), {}, {}, "c2"});
    col.children.push_back(std::move(exp));
    if (!tryLayout(col, {0.0f, 100.0f, 0.0f, 200.0f}))
        return false;
    if (!checkGeometry(col.children[0], 0, 0, 50, 30, "c1"))
        return false;
    if (!checkGeometry(col.children[1].children[0], 0, 30, 50, 170, "expanded c2"))
        return false;
    return true;
}

// 8. 负 margin 允许重叠
bool test_negative_margin()
{
    clearPools();
    Node row = {makeRow(200.0f, 100.0f), {}, {}, "row"};
    row.children.push_back({makeContainer(50.0f, 50.0f, {0, 0, 0, 0}), {}, {}, "c1"});
    row.children.push_back({makeContainer(50.0f, 50.0f, {-10, 0, 0, 0}), {}, {}, "c2"});
    if (!tryLayout(row, {0.0f, 200.0f, 0.0f, 100.0f}))
        return false;
    if (!checkGeometry(row.children[0], 0, 0, 50, 50, "c1"))
        return false;
    if (!checkGeometry(row.children[1], 40, 0, 50, 50, "c2 with negative margin"))
        return false;
    return true;
}

// ========== 运行所有测试 ==========
bool runTests()
{
    return test_parent_wins() && test_fixed_inside_flex() && test_expanded() &&
           test_stretch_overrides_fixed() && test_main_axis_min() && test_text() &&
           test_container_shrink_to_child() && test_unbounded_error() &&
           test_expanded_outside_flex() && test_container_with_padding() &&
           test_nested_container_shrink() && test_row_stretch_with_fixed_width() &&
           test_column_space_evenly() && test_fixed_overridden_by_parent() &&
           test_text_unbounded() && test_expanded_in_column() && test_negative_margin();
}

int main()
{
    if (runTests())
    {
        std::cout << "All tests passed!\n";
        return 0;
    }
    else
    {
        return 1;
    }
}