#include <iostream>
#include <print>
#include <yoga/Yoga.h>
#include <GLFW/glfw3.h>

void printNodeLayout(YGNodeRef node, int depth)
{
    if (node == nullptr)
        return;

    std::string indent(depth * 2, ' ');

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

    YGDirection dir = YGNodeLayoutGetDirection(node);
    bool overflow = YGNodeLayoutGetHadOverflow(node);
    std::println("{}  direction: {}", indent, (dir == YGDirectionLTR ? "LTR" : "RTL"));
    std::println("{}  hadOverflow: {}", indent, overflow ? "true" : "false");

    std::println("{}  margin: top={}, left={}, bottom={}, right={}", indent,
                 YGNodeLayoutGetMargin(node, YGEdgeTop),
                 YGNodeLayoutGetMargin(node, YGEdgeLeft),
                 YGNodeLayoutGetMargin(node, YGEdgeBottom),
                 YGNodeLayoutGetMargin(node, YGEdgeRight));

    std::println("{}  border: top={}, left={}, bottom={}, right={}", indent,
                 YGNodeLayoutGetBorder(node, YGEdgeTop),
                 YGNodeLayoutGetBorder(node, YGEdgeLeft),
                 YGNodeLayoutGetBorder(node, YGEdgeBottom),
                 YGNodeLayoutGetBorder(node, YGEdgeRight));

    std::println("{}  padding: top={}, left={}, bottom={}, right={}", indent,
                 YGNodeLayoutGetPadding(node, YGEdgeTop),
                 YGNodeLayoutGetPadding(node, YGEdgeLeft),
                 YGNodeLayoutGetPadding(node, YGEdgeBottom),
                 YGNodeLayoutGetPadding(node, YGEdgeRight));

    uint32_t childCount = YGNodeGetChildCount(node);
    for (uint32_t i = 0; i < childCount; ++i)
    {
        printNodeLayout(YGNodeGetChild(node, i), depth + 1);
    }
}

int main()
{
    // 1. 初始化 GLFW 并获取显示器的内容缩放因子
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    // 设置窗口提示（可选，但有助于高DPI行为）
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // 不显示窗口
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_TRUE);

    GLFWwindow *window = glfwCreateWindow(1, 1, "TempWindow", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return 1;
    }

    float xscale, yscale;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    float pointScaleFactor = xscale; // 通常 xscale == yscale

    // 清理 GLFW 资源
    glfwDestroyWindow(window);
    glfwTerminate();

    std::println("Detected point scale factor: {}", pointScaleFactor);

    // 2. 创建 Yoga 配置并设置 PointScaleFactor
    YGConfigRef config = YGConfigNew();
    YGConfigSetPointScaleFactor(config, pointScaleFactor);
    // 其他配置保持默认（Errata = None, UseWebDefaults = false）

    // 3. 使用配置创建节点树
    YGNodeRef root = YGNodeNewWithConfig(config);
    YGNodeStyleSetFlexDirection(root, YGFlexDirectionRow);
    YGNodeStyleSetWidth(root, 100.0f);
    YGNodeStyleSetHeight(root, 100.0f);

    YGNodeRef child0 = YGNodeNewWithConfig(config);
    YGNodeStyleSetFlexGrow(child0, 1.0f);
    YGNodeStyleSetMargin(child0, YGEdgeRight, 10.0f);
    YGNodeInsertChild(root, child0, 0);

    YGNodeRef child1 = YGNodeNewWithConfig(config);
    YGNodeStyleSetFlexGrow(child1, 1.0f);
    YGNodeInsertChild(root, child1, 1);

    // 4. 执行布局
    YGNodeCalculateLayout(root, YGUndefined, YGUndefined, YGDirectionLTR);

    // 5. 打印布局结果
    std::println("===== Layout Results with PointScaleFactor = {} =====",
                 pointScaleFactor);
    printNodeLayout(root, 0);

    // 6. 清理内存
    YGNodeFreeRecursive(root);
    YGConfigFree(config);

    return 0;
}