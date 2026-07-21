#include <algorithm>
#include <bits/utility.h>
#include <cmath>
#include <iostream>
#include <exception>
#include <limits>

// 原理: https://docs.flutter.dev/ui/layout/constraints
//      https://docs.flutter.dev/ui/layout#overview

/** @https://docs.flutter.cn/ui/layout
要点
Flutter 中的布局是用 widget 构建的。
Widget 是用于构建 UI 的类。
Widget 也用于构建 UI 元素。
通过组合简单的 widget 来构建复杂的 widget


Container 是一个 widget，让你自定义其子 widget
举几个例子，如果要添加 padding、margin、边框或背景颜色，你就可以用上 Container 了
每个 Text widget 都被放在一个 Container 以添加 margin。整个 Row 也被放在一个 Container 中，以便添加 padding。

UI 的其余部分由属性控制。通过 Icon 的 color 属性来设置它的颜色，通过 Text.style 属性来设置文字的字体、颜色、字重等等。

NOTE: 组合简单的 widget 来构建复杂的 widget。 一起都是 widget
1. 布局 widget
    1. 选择一个布局 widget: 例如，你可以使用 Center 布局 widget 将可见的 widget 水平和垂直居中。
    2. 创建一个可见 widget: 为你的应用程序选择 可见的 widget，以包含可见元素，如 text、images 或 icons。
    3. 将可见 widget 添加到布局 widget: 
        所有布局 widget 都具有以下任一项：
        一个 child 属性，如果它们只包含一个子项 — 例如 Center 和 Container
        一个 children 属性，如果它们包含多个子项 — 例如 Row、Column、ListView 和 Stack
    4. 将布局 widget 添加到页面: 应用的 build() 方法中实例化和返回一个 widget 会让它显示出来

2. 横向或纵向布局多个 widget: 最常见的布局模式之一是垂直或水平 widget。你可以使用 Row widget 水平排列 widget，使用 Column widget 垂直排列 widget。
    要点
    Row 和 Column 是两种最常用的布局模式。
    Row 和 Column 每个都有一个子 widget 列表。
    一个子 widget 本身可以是 Row、Column 或其他复杂 widget。
    可以指定 Row 或 Column 如何在垂直和水平方向上对齐其子项。
    可以拉伸或限制特定的子 widget。
    可以指定子 widget 如何占用 Row 或 Column 的可用空间。

3. 要在 Flutter 中创建行或列，可以将子 widget 列表添加到 Row 或 Column widget 中。反过来，每个子项本身可以是一行或一列，依此类推
    Row 和 Column 是水平和垂直布局的基本原始 widget — 这些低级 widget 允许最大程度的自定义。 
    Flutter 还提供专门的、更高级别的 widget，可能可以直接满足需求。
    例如，和 Row 相比你可能更喜欢 ListTile，这是一个易于使用的 widget，有属性可以设置头尾图标，最多可以显示 3 行文本；
    和 Column 相比你也可能更喜欢 ListView，这是一种类似于列的布局，但如果其内容太长导致可用空间不够容纳时会自动滚动。更多信息可以查看 通用布局 widget。

4. 对齐 widget: 你可以使用 mainAxisAlignment 和 crossAxisAlignment 属性控制行或列如何对齐其子项。对于一行来说，主轴水平延伸，交叉轴垂直延伸。对于一列来说，主轴垂直延伸，交叉轴水平延伸。
    当你将图像添加到项目中时，你需要更新 pubspec.yaml 文件来访问它们 — 本例使用 Image.asset 来显示图像。
    更多信息可以查看本例的 pubspec.yaml 文件，或文档：添加资源和图片。如果你正在使用 Image.network 引用在线图像，则不需要这些操作。


5. 调整 widget 大小: NOTE: 修复修改图片的大小，来铺满布局空间？？？
通过使用 Expanded widget，可以调整 widget 的大小以适合行或列。要修复上一个图像行对其渲染框来说太宽的示例，可以用 Expanded widget 把每个图像包起来。
也许你想要一个 widget 占用的空间是兄弟项的两倍。为了达到这个效果，可以使用 Expanded widget 的 flex 属性，这是一个用来确定 widget 的弹性系数的整数。默认的弹性系数为 1，以下代码将中间图像的弹性系数设置为 2
    Row(
        crossAxisAlignment: CrossAxisAlignment.center,
        children: [
            Expanded(child: Image.asset('images/pic1.jpg')),
            Expanded(flex: 2, child: Image.asset('images/pic2.jpg')),
            Expanded(child: Image.asset('images/pic3.jpg')),
        ],
    );

6. 组合 widget： 默认情况下，行或列沿其主轴会占用尽可能多的空间，但如果要将子项紧密组合在一起，请将其 mainAxisSize 设置为 MainAxisSize.min。以下示例使用此属性将星形图标组合在一起

7. 嵌套行和列：布局框架让你根据需要在行和列内嵌套行和列。让我们看看以下布局的概述部分的代码

8. 通用布局 widget：Flutter 有一个丰富的布局 widget 仓库，里面有很多经常会用到的布局 widget。

9. Container：许多布局都可以随意的用 Container，它可以将使用了 padding 或者增加了 borders/margins 的 widget 分开。你可以通过将整个布局放到一个 Container 中，并且改变它的背景色或者图片，来改变设备的背景。
    摘要 (Container)
        增加 padding、margins、borders
        改变背景色或者图片
        只包含一个子 widget，但是这个子 widget 可以是 Row、Column 或者是 widget 树的根 widget

10. GridView: 使用 GridView 将 widget 作为二维列表展示。 GridView 提供两个预制的列表，或者你可以自定义网格。当 GridView 检测到内容太长而无法适应渲染盒时，它就会自动支持滚动。
    摘要 (GridView)
        在网格中使用 widget
        当列的内容超出渲染容器的时候，它会自动支持滚动。
        创建自定义的网格，或者使用下面提供的网格的其中一个：
        GridView.count 让你制定列的数量
        GridView.extent 让你制定单元格的最大宽度
    当展示二维列表中的单元格所在的行和列的位置很重要的（例如，它是 “calorie” 行和 “avocado” 列的条目）的时候，使用 Table 或者 DataTable.。

11. ListView: ListView，一个和列很相似的 widget，当内容长于自己的渲染盒时，就会自动支持滚动。
    摘要 (ListView)
        一个用来组织盒子中列表的专用 Column
        可以水平或者垂直布局
        当监测到空间不足时，会提供滚动
        比 Column 的配置少，使用更容易，并且支持滚动

12. Stack: 可以使用 Stack 在基础 widget（通常是图片）上排列 widget， widget 可以完全或者部分覆盖基础 widget。
    摘要 (Stack)
        用于覆盖另一个 widget
        子列表中的第一个 widget 是基础 widget；后面的子项覆盖在基础 widget 的顶部
        Stack 的内容是无法滚动的
        你可以剪切掉超出渲染框的子项

13. Card: Card 只有一个子项，这个子项可以是列、行、列表、网格或者其他支持多个子项的 widget。默认情况下，Card 的大小是 0x0 像素。你可以使用 SizedBox 控制 card 的大小。
    摘要 (Card)
        实现一个 Material card
        用于呈现相关有价值的信息
        接收单个子项，但是子项可以是 Row、Column 或者其他可以包含列表子项的 widget
        显示圆角和阴影
        Card 的内容无法滚动
        来自 Material 库

14. ListTile: ListTile 是 Material 库 中专用的行 widget，它可以很轻松的创建一个包含三行文本以及可选的行前和行尾图标的行。 ListTile 在 Card 或者 ListView 中最常用，但是也可以在别处使用。
    摘要 (ListTile)
        一个可以包含最多 3 行文本和可选的图标的专用的行
        比 Row 更少的配置，更容易使用
        来自 Material 库

*/

/**
绘制布局草图
1. 用以下问题将布局分解为基本元素。
    能否识别出行与列？
    布局是否包含网格？
    是否有重叠元素？
    UI 是否需要标签页？
    需要对齐、内边距或边框的是什么？

2. 识别较大的元素。本例中，你将图片、标题、按钮和描述排成一列。
3. 绘制每一行。NOTE: 先划分区域，在布局子区域，最终每个区域都有配置
    第 1 行 [Title 区域]有三个子节点：一列文字、星形图标和一个数字。其第一个子节点（列）包含两行文字，该列可能需要更多空间。
    第 2 行 [Button 区域]有三个子节点：每个子节点包含一列，列内再有图标和文字。
 */

/** @https://docs.flutter.cn/ui/layout/Constraints
1. 深入理解 Flutter 布局约束
熟记这条规则：
    1. 首先，上层 widget 向下层 widget 传递约束条件；
    2. 然后，下层 widget 向上层 widget 传递大小信息。
    3. 最后，上层 widget 决定下层 widget 的位置。
更多细节：
    1. Widget 会通过它的 父级 【获得自身的约束】。约束实际上就是 4 个浮点类型的集合：最大/最小宽度，以及最大/最小高度。
    2. 然后，这个 widget 将会逐个遍历它的 children 列表。【向子级传递 约束】（子级之间的约束可能会有所不同），然后询问它的每一个子级需要用于布局的大小。
    3. 然后，这个 widget 就会对它子级的 children 【逐个进行布局】。（水平方向是 x 轴，竖直是 y 轴）
    4. 最后，widget 将会把它的大小信息向【上传递至父 widget】（包括其原始约束条件）。

例如，如果一个 widget 中包含了一个具有 padding 的 Column，并且要对 Column 的子 widget 进行如下的布局：
那么谈判将会像这样：
Widget: "嘿！我的父级。我的约束是多少？"
Parent: "你的宽度必须在 0 到 300 像素之间，高度必须在 0 到 85 之间。"
Widget: "嗯...我想要 5 个像素的内边距，这样我的子级能最多拥有 290 个像素宽度和 75 个像素高度。"
Widget: "嘿，我的第一个子级，你的宽度必须要在 0 到 290，长度在 0 到 75 之间。"
First child: "OK，那我想要 290 像素的宽度，20 个像素的长度。"
Widget: "嗯...由于我想要将我的第二个子级放在第一个子级下面，所以我们仅剩 55 个像素的高度给第二个子级了。"
Widget: "嘿，我的第二个子级，你的宽度必须要在 0 到 290，长度在 0 到 55 之间。"
Second child: "OK，那我想要 140 像素的宽度，30 个像素的长度。"
Widget: "很好。我的第一个子级将被放在 x: 5 & y: 5 的位置，而我的第二个子级将在 x: 80 & y: 25 的位置。"
Widget: "嘿，我的父级，我决定我的大小为 300 像素宽度，60 像素高度。"


2. 限制
Flutter 的布局引擎的设计初衷是可以一次性完成整个布局的构建。这使得它非常高效，但同时会有一些限制：
    一个 widget 仅在其父级给其约束的情况下才能决定自身的大小。这意味着 widget 通常情况下 【不能任意获得其想要的大小】。
    一个 widget 【无法知道，也不需要决定其在屏幕中的位置】。因为它的位置是由其父级决定的。
    当轮到父级决定其大小和位置的时候，同样的也取决于它自身的父级。所以，在不考虑整棵树的情况下，几乎不可能精确定义任何 widget 的大小和位置。
    如果子级想要拥有和父级不同的大小，然而父级没有足够的空间对其进行布局的话，子级的设置的大小可能会【不生效】。 【这时请明确指定它的对齐方式】

在 Flutter 世界中，widget 是由它们的 RenderBox 对象进行实际渲染的。大部分特别是只有一个子节点的 box 会直接将限制传递下去。
总的来说，box 分成以下几类，它们有不同的处理大小限制的方式：
    1. 尽可能地撑满。例如 Center 和 ListView 使用的 box。
    2. 尽可能地保持与子节点一致。例如 Transform 和 Opacity 使用的 box。
    3. 尽可能地布局为指定大小。例如 Image 和 Text 使用的 box。
像 Container 这样的 widget 会根据不同的参数进行不同的布局。 Container 的默认构造会让其尽可能地撑满大小限制，但如果你设置了 width，它就会尽可能地遵照你设置的大小。
其他像 Row 和 Column（Flex 系列）这样的 widget 会根据它们的限制进行不同的布局。在 Flex 小节中有更详细的描述。
//NOTE: 由于 Container 没有固定大小但是有子级，所以它决定变成它 child 的大小。
//NOTE: ConstrainedBox 仅对其从其父级接收到的约束下施加其他约束
//NOTE: OverflowBox 与 UnconstrainedBox 类似，但不同的是，如果其子级超出该空间，它将不会显示任何警告。

3. 严格约束 (Tight) 与宽松约束 (loose)
严格约束 (Tight): 严格约束给你了一种获得确切大小的选择。换句话来说就是，它的最大/最小宽度是一致的，高度也一样。
    如果你重新阅读 样例 2，它告诉我们屏幕强制 Container 变得和屏幕一样大。为何屏幕能够做到这一点，原因就是给 Container 传递了严格约束。
    Container(width: 100, height: 100, color: red)

宽松约束 (loose): 宽松 约束的最小宽度/高度为 0。

Unbounded Constraints: In certain situations, a box's constraint is unbounded, or infinite. This means that either the maximum width or the maximum height is set to double.infinity.
The most common case where a render box ends up with an unbounded constraint is within a flex box (Row or Column), and within a scrollable region (such as ListView and other ScrollView subclasses).

Flex
A flex box (Row and Column) behaves differently depending on whether its constraint is bounded or unbounded in its primary direction.
A flex box with a bounded constraint in its primary direction tries to be as big as possible.
 

*/

// NOLINTBEGIN
#include <algorithm>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
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
    float x, y; // -1..1
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

    constexpr Constraints deflate(const EdgeInsets &e) const noexcept
    {
        float h = e.left + e.right, v = e.top + e.bottom;
        return {std::max(0.0f, minW - h), std::max(0.0f, maxW - h),
                std::max(0.0f, minH - v), std::max(0.0f, maxH - v)};
    }
    constexpr Constraints tightenWidth(float w) const noexcept
    {
        return {w, w, minH, maxH};
    }
    constexpr Constraints tightenHeight(float h) const noexcept
    {
        return {minW, maxW, h, h};
    }
    constexpr Size clamp(const Size &s) const noexcept
    {
        return {std::clamp(s.width, minW, maxW), std::clamp(s.height, minH, maxH)};
    }
};

// ========== 对齐枚举 ==========
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

// ========== Widget 类型与样式（数据池） ==========
enum class WidgetKind : uint8_t
{
    Container,
    Row,
    Column,
    Expanded
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
};
struct ColumnStyle
{
    std::optional<float> width, height;
    EdgeInsets margin{}, padding{};
    MainAxisAlignment mainAlign = MainAxisAlignment::start;
    CrossAxisAlignment crossAlign = CrossAxisAlignment::start;
};
struct ExpandedStyle
{
    int flex = 1;
};

// 全局样式池
std::vector<ContainerStyle> g_containers;
std::vector<RowStyle> g_rows;
std::vector<ColumnStyle> g_columns;
std::vector<ExpandedStyle> g_expandeds;

// Widget 引用
struct WidgetRef
{
    WidgetKind kind;
    uint32_t idx;
};

// 最终几何输出（相对于父内容区）
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

// 树节点
struct Node
{
    WidgetRef ref;
    std::vector<Node> children;
    BoxGeometry geometry;
    std::string name; // 调试
};

// ---------- 样式访问函数 ----------
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

// 工厂函数
WidgetRef makeContainer(ContainerStyle s)
{
    uint32_t idx = g_containers.size();
    g_containers.push_back(std::move(s));
    return {WidgetKind::Container, idx};
}
WidgetRef makeColumn(ColumnStyle s)
{
    uint32_t idx = g_columns.size();
    g_columns.push_back(std::move(s));
    return {WidgetKind::Column, idx};
}

// ========== 核心布局 ==========
void layout(Node &node, Constraints constraints, float parentContentX,
            float parentContentY);

void layoutContainer(Node &node, Constraints borderBC, const EdgeInsets &pad)
{
    auto &ref = node.ref;
    if (node.children.empty())
    {
        Size size = borderBC.clamp({widthOf(ref).value_or(borderBC.maxW),
                                    heightOf(ref).value_or(borderBC.maxH)});
        node.geometry =
            BoxGeometry::make({marginOf(ref).left, marginOf(ref).top}, size, pad);
        return;
    }
    Node &child = node.children[0];
    Constraints innerBC = borderBC.deflate(pad);
    layout(child, innerBC, pad.left, pad.top);
    const auto &childMargin = marginOf(child.ref);
    float childFullW = child.geometry.w + childMargin.horizontal();
    float childFullH = child.geometry.h + childMargin.vertical();
    Alignment align = alignmentOf(ref);
    float extraW = innerBC.maxW - childFullW;
    float extraH = innerBC.maxH - childFullH;
    child.geometry.x = pad.left + childMargin.left + extraW * (align.x + 1.0f) / 2.0f;
    child.geometry.y = pad.top + childMargin.top + extraH * (align.y + 1.0f) / 2.0f;

    Size size;
    size.width = widthOf(ref).value_or(childFullW + pad.horizontal());
    size.height = heightOf(ref).value_or(childFullH + pad.vertical());
    size = borderBC.clamp(size);
    node.geometry = BoxGeometry::make({marginOf(ref).left, marginOf(ref).top}, size, pad);
}

void layoutFlex(Node &node, Constraints borderBC, const EdgeInsets &pad, bool isRow)
{
    auto &ref = node.ref;
    float mainSize = isRow ? borderBC.maxW : borderBC.maxH;
    float crossSize = isRow ? borderBC.maxH : borderBC.maxW;
    float padMainStart = isRow ? pad.left : pad.top;
    float padMainEnd = isRow ? pad.right : pad.bottom;
    float padCrossStart = isRow ? pad.top : pad.left;
    float padCrossEnd = isRow ? pad.bottom : pad.right;
    float innerMain = std::max(0.0f, mainSize - padMainStart - padMainEnd);
    float innerCross = std::max(0.0f, crossSize - padCrossStart - padCrossEnd);

    struct ChildInfo
    {
        Node *node;
        float flex;
        float marginMain;
        float marginCross;
        float fixedMain;
    };
    std::vector<ChildInfo> infos;
    float totalFlex = 0, usedMain = 0;

    for (auto &child : node.children)
    {
        if (child.ref.kind == WidgetKind::Expanded)
        {
            Node &real = child.children[0];
            float flexVal = g_expandeds[child.ref.idx].flex;
            const auto &m = marginOf(real.ref);
            float mM = isRow ? m.horizontal() : m.vertical();
            float mC = isRow ? m.vertical() : m.horizontal();
            infos.push_back({&real, flexVal, mM, mC, 0});
            totalFlex += flexVal;
        }
        else
        {
            const auto &m = marginOf(child.ref);
            float mM = isRow ? m.horizontal() : m.vertical();
            float mC = isRow ? m.vertical() : m.horizontal();
            float fixed =
                isRow ? widthOf(child.ref).value_or(0) : heightOf(child.ref).value_or(0);
            infos.push_back({&child, 0.0f, mM, mC, fixed});
            usedMain += fixed + mM;
        }
    }

    float freeMain = std::max(0.0f, innerMain - usedMain);

    for (auto &info : infos)
    {
        float childMainMin, childMainMax;
        if (info.flex > 0)
        {
            float flexSize = totalFlex > 0 ? freeMain * (info.flex / totalFlex) : 0;
            childMainMin = childMainMax = flexSize;
        }
        else
        {
            childMainMin = childMainMax = info.fixedMain;
            if (info.fixedMain == 0)
                childMainMax = innerMain;
        }
        float childCrossMax = std::max(0.0f, innerCross - info.marginCross);
        Constraints childBC;
        if (isRow)
            childBC = {childMainMin, childMainMax, 0, childCrossMax};
        else
            childBC = {0, childCrossMax, childMainMin, childMainMax};
        layout(*info.node, childBC, 0, 0);
    }

    float totalMainUsed = 0;
    for (auto &info : infos)
    {
        float childMain = isRow ? info.node->geometry.w : info.node->geometry.h;
        totalMainUsed += childMain + info.marginMain;
    }
    float extraMain = std::max(0.0f, innerMain - totalMainUsed);

    MainAxisAlignment mainAlign =
        isRow ? g_rows[ref.idx].mainAlign : g_columns[ref.idx].mainAlign;
    CrossAxisAlignment crossAlign =
        isRow ? g_rows[ref.idx].crossAlign : g_columns[ref.idx].crossAlign;

    float mainGap = 0, mainStartOff = 0;
    if (infos.size() > 1)
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
    for (auto &info : infos)
    {
        Node &child = *info.node;
        const auto &childMargin = marginOf(child.ref);
        float childMainLen = isRow ? child.geometry.w : child.geometry.h;
        float childCrossLen = isRow ? child.geometry.h : child.geometry.w;

        float crossExtra =
            std::max(0.0f, innerCross - (childCrossLen + info.marginCross));
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
        mainPos += childMainLen + info.marginMain + mainGap;
    }

    Size size;
    if (isRow)
    {
        size.width = totalMainUsed + pad.horizontal();
        size.height = innerCross + pad.vertical();
    }
    else
    {
        size.width = innerCross + pad.horizontal();
        size.height = totalMainUsed + pad.vertical();
    }
    if (auto w = widthOf(ref))
        size.width = *w;
    if (auto h = heightOf(ref))
        size.height = *h;
    size = borderBC.clamp(size);
    node.geometry = BoxGeometry::make({marginOf(ref).left, marginOf(ref).top}, size, pad);
}

void layout(Node &node, Constraints constraints, float parentContentX,
            float parentContentY)
{
    const auto &ref = node.ref;
    const auto &margin = marginOf(ref);
    const auto &padding = paddingOf(ref);

    Constraints borderBC = constraints.deflate(margin);
    if (auto w = widthOf(ref))
        borderBC = borderBC.tightenWidth(*w);
    if (auto h = heightOf(ref))
        borderBC = borderBC.tightenHeight(*h);

    switch (ref.kind)
    {
    case WidgetKind::Container:
        layoutContainer(node, borderBC, padding);
        break;
    case WidgetKind::Row:
        layoutFlex(node, borderBC, padding, true);
        break;
    case WidgetKind::Column:
        layoutFlex(node, borderBC, padding, false);
        break;
    case WidgetKind::Expanded:
        if (!node.children.empty())
        {
            layout(node.children[0], constraints, parentContentX, parentContentY);
            node.geometry = node.children[0].geometry;
        }
        break;
    }
}

// ========== 调试打印（模拟 Yoga 风格） ==========
void printNode(const Node &node, int depth = 0)
{
    std::string indent(depth * 2, ' ');
    std::cout << indent << "Node Layout [" << node.name << "]:\n";
    std::cout << indent << "  position: left=" << node.geometry.x
              << ", top=" << node.geometry.y << "\n";
    std::cout << indent << "  size: width=" << node.geometry.w
              << ", height=" << node.geometry.h << "\n";
    // 固定边框
    std::cout << indent << "  border: top=0, left=0, bottom=0, right=0\n";
    std::cout << indent << "  padding: top=" << node.geometry.padT
              << ", left=" << node.geometry.padL << ", bottom=" << node.geometry.padB
              << ", right=" << node.geometry.padR << "\n";

    const auto &margin = marginOf(node.ref);
    std::cout << indent << "  margin: top=" << margin.top << ", left=" << margin.left
              << ", bottom=" << margin.bottom << ", right=" << margin.right << "\n";

    for (const auto &child : node.children)
    {
        printNode(child, depth + 1);
    }
}

// ========== 提取绝对几何数组（供 Vulkan 使用） ==========
struct ScreenGeometry
{
    float x, y, w, h;
    float padL, padR, padT, padB;
};
std::vector<ScreenGeometry> extractGeometries(const Node &root)
{
    std::vector<ScreenGeometry> geos;
    // 用栈模拟遍历，同时传递父内容区原点
    struct StackFrame
    {
        const Node *node;
        float parentContentX, parentContentY;
    };
    std::vector<StackFrame> stack;
    stack.push_back({&root, 0.0f, 0.0f});
    while (!stack.empty())
    {
        auto [node, pcx, pcy] = stack.back();
        stack.pop_back();
        float absX = pcx + node->geometry.x;
        float absY = pcy + node->geometry.y;
        geos.push_back({absX, absY, node->geometry.w, node->geometry.h,
                        node->geometry.padL, node->geometry.padR, node->geometry.padT,
                        node->geometry.padB});
        float contentX = absX + node->geometry.padL;
        float contentY = absY + node->geometry.padT;
        // 逆序推入以保持原顺序
        for (int i = node->children.size() - 1; i >= 0; --i)
        {
            stack.push_back({&node->children[i], contentX, contentY});
        }
    }
    return geos;
}

// ========== 构建测试树 ==========
Node buildTestTree()
{
    auto screenRef = makeColumn(ColumnStyle{.width = 800, .height = 600});
    auto rootRef =
        makeColumn(ColumnStyle{.width = 200, .height = 200, .padding = {10, 10, 10, 10}});
    auto child0Ref = makeContainer(
        ContainerStyle{.width = 100, .height = 100, .margin = {5, 5, 5, 5}});
    auto child00Ref =
        makeContainer(ContainerStyle{.width = 50, .height = 50, .margin = {5, 5, 5, 5}});
    auto oneRef1 =
        makeContainer(ContainerStyle{.width = 100, .height = 20, .margin = {5, 5, 5, 5}});
    auto root2Ref =
        makeColumn(ColumnStyle{.width = 200, .height = 200, .padding = {10, 10, 10, 10}});
    auto oneRef2 =
        makeContainer(ContainerStyle{.width = 100, .height = 20, .margin = {5, 5, 5, 5}});
    auto textRef = makeContainer(
        ContainerStyle{.width = 200, .height = 200, .padding = {10, 10, 10, 10}});

    Node screen{screenRef, {}, {}, "screen"};
    Node root{rootRef, {}, {}, "root"};
    Node child0{child0Ref, {}, {}, "child0"};
    Node child00{child00Ref, {}, {}, "child0-0"};
    Node one1{oneRef1, {}, {}, "1"};
    Node root2{root2Ref, {}, {}, "root2"};
    Node one2{oneRef2, {}, {}, "1"};
    Node text{textRef, {}, {}, "text_display"};

    child0.children.push_back(std::move(child00));
    root.children.push_back(std::move(child0));
    root.children.push_back(std::move(one1));

    root2.children.push_back(std::move(one2));

    screen.children.push_back(std::move(root));
    screen.children.push_back(std::move(root2));
    screen.children.push_back(std::move(text));

    return screen;
}

int main()
{
    Node rootNode = buildTestTree();
    Constraints screenConstraints = {800, 800, 600, 600};
    layout(rootNode, screenConstraints, 0, 0);

    std::cout << "layout.print [start]:\n";
    printNode(rootNode);
    std::cout << "layout.print [end]\n";

    // 生成绝对坐标几何数组示例
    auto geos = extractGeometries(rootNode);
    // 可使用 geos 传递至 Vulkan
    return 0;
} // NOLINTEND