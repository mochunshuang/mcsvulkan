#include <iostream>
#include <print>
#include <yoga/Yoga.h>

void printNodeLayout(YGNodeRef node, int depth)
{
    if (node == nullptr)
        return;

    // 缩进
    std::string indent(depth * 2, ' ');

    // 位置 & 尺寸
    float left = YGNodeLayoutGetLeft(node);
    float top = YGNodeLayoutGetTop(node);
    float right = YGNodeLayoutGetRight(node);
    float bottom = YGNodeLayoutGetBottom(node);
    float width = YGNodeLayoutGetWidth(node);
    float height = YGNodeLayoutGetHeight(node);

    std::println("{}Node Layout:", indent);
    std::println("{}  position: left={}, top={}, right={}, bottom={}", indent, left, top,
                 right, bottom);
    std::println("{}  size: width={}, height={}", indent, width, height);

    // 方向 & 溢出
    YGDirection dir = YGNodeLayoutGetDirection(node);
    bool overflow = YGNodeLayoutGetHadOverflow(node);
    std::println("{}  direction: {}", indent, (dir == YGDirectionLTR ? "LTR" : "RTL"));
    std::println("{}  hadOverflow: {}", indent, overflow ? "true" : "false");

    // margin
    std::println("{}  margin: top={}, left={}, bottom={}, right={}", indent,
                 YGNodeLayoutGetMargin(node, YGEdgeTop),
                 YGNodeLayoutGetMargin(node, YGEdgeLeft),
                 YGNodeLayoutGetMargin(node, YGEdgeBottom),
                 YGNodeLayoutGetMargin(node, YGEdgeRight));

    // border
    std::println("{}  border: top={}, left={}, bottom={}, right={}", indent,
                 YGNodeLayoutGetBorder(node, YGEdgeTop),
                 YGNodeLayoutGetBorder(node, YGEdgeLeft),
                 YGNodeLayoutGetBorder(node, YGEdgeBottom),
                 YGNodeLayoutGetBorder(node, YGEdgeRight));

    // padding
    std::println("{}  padding: top={}, left={}, bottom={}, right={}", indent,
                 YGNodeLayoutGetPadding(node, YGEdgeTop),
                 YGNodeLayoutGetPadding(node, YGEdgeLeft),
                 YGNodeLayoutGetPadding(node, YGEdgeBottom),
                 YGNodeLayoutGetPadding(node, YGEdgeRight));

    // 递归打印子节点
    uint32_t childCount = YGNodeGetChildCount(node);
    for (uint32_t i = 0; i < childCount; ++i)
    {
        printNodeLayout(YGNodeGetChild(node, i), depth + 1);
    }
}

int main()
{
    try
    {
        // 构建节点树
        YGNodeRef root = YGNodeNew();
        YGNodeStyleSetFlexDirection(root, YGFlexDirectionRow);
        YGNodeStyleSetWidth(root, 100.0f);
        YGNodeStyleSetHeight(root, 100.0f);

        YGNodeRef child0 = YGNodeNew();
        YGNodeStyleSetFlexGrow(child0, 1.0f);
        YGNodeStyleSetMargin(child0, YGEdgeRight, 10.0f);
        YGNodeInsertChild(root, child0, 0);

        YGNodeRef child1 = YGNodeNew();
        YGNodeStyleSetFlexGrow(child1, 1.0f);
        YGNodeInsertChild(root, child1, 1);

        // 执行布局
        YGNodeCalculateLayout(root, YGUndefined, YGUndefined, YGDirectionLTR);

        // 打印所有结果
        std::println("===== Layout Results =====");
        printNodeLayout(root, 0);

        // 清理内存
        YGNodeFreeRecursive(root);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception: " << e.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "Unknown exception\n";
        return 1;
    }

    return 0;
}