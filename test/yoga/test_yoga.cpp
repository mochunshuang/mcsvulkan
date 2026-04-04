#include <iostream>
#include <vector>
#include <exception>

#include <yoga/Yoga.h>
#include <glm/glm.hpp>

// 3D 顶点：位置 (vec3) + 纹理坐标 (vec2)
struct Vertex3D
{
    glm::vec3 position;
    glm::vec2 texCoord;
};

// 将 Yoga 计算的矩形转换为 3D 空间中的两个三角形（6 个顶点）
// 参数：矩形在屏幕上的 left, top, width, height（像素）
//      窗口尺寸 windowWidth, windowHeight
//      矩形在 3D 空间中的 Z 值（默认为 0）
std::vector<Vertex3D> rectToVertices3D(float left, float top, float width, float height,
                                       float windowWidth, float windowHeight,
                                       float z = 0.0f)
{
    // 将屏幕坐标（左上角为原点，Y 向下）转换到 Vulkan NDC 坐标（[-1,1]，Y 向下）
    // 同时将 NDC 的 X,Y 作为 3D 空间的 X,Y 坐标（正交投影下直接对应屏幕位置）
    float x0 = (left / windowWidth) * 2.0f - 1.0f;            // 左边缘 NDC X
    float x1 = ((left + width) / windowWidth) * 2.0f - 1.0f;  // 右边缘 NDC X
    float y0 = 1.0f - (top / windowHeight) * 2.0f;            // 上边缘 NDC Y (翻转)
    float y1 = 1.0f - ((top + height) / windowHeight) * 2.0f; // 下边缘 NDC Y

    // 纹理坐标 (0,0) 左上角，(1,1) 右下角
    glm::vec2 uv00(0.0f, 0.0f);
    glm::vec2 uv10(1.0f, 0.0f);
    glm::vec2 uv01(0.0f, 1.0f);
    glm::vec2 uv11(1.0f, 1.0f);

    // 四个顶点的 3D 位置（Z 相同，形成与屏幕平行的平面）
    glm::vec3 v00(x0, y0, z);
    glm::vec3 v10(x1, y0, z);
    glm::vec3 v01(x0, y1, z);
    glm::vec3 v11(x1, y1, z);

    // 两个三角形组成矩形
    return {
        {v00, uv00}, {v10, uv10}, {v01, uv01}, // 三角形1
        {v01, uv01}, {v10, uv10}, {v11, uv11}  // 三角形2
    };
}

int main()
try
{
    // ========== 1. 创建 Yoga 布局树 ==========
    YGConfigRef config = YGConfigNew();
    YGNodeRef root = YGNodeNewWithConfig(config);
    YGNodeRef child1 = YGNodeNewWithConfig(config);
    YGNodeRef child2 = YGNodeNewWithConfig(config);

    YGNodeStyleSetFlexDirection(root, YGFlexDirectionColumn);
    YGNodeStyleSetWidth(root, 1024.0f);
    YGNodeStyleSetHeight(root, 768.0f);

    YGNodeStyleSetWidth(child1, 200.0f);
    YGNodeStyleSetHeight(child1, 100.0f);
    YGNodeStyleSetMargin(child1, YGEdgeAll, 10.0f);

    YGNodeStyleSetWidth(child2, 150.0f);
    YGNodeStyleSetHeight(child2, 80.0f);
    YGNodeStyleSetMargin(child2, YGEdgeAll, 10.0f);

    YGNodeInsertChild(root, child1, 0);
    YGNodeInsertChild(root, child2, 1);

    YGNodeCalculateLayout(root, YGNodeStyleGetWidth(root).value,
                          YGNodeStyleGetHeight(root).value, YGDirectionLTR);

    // ========== 2. 收集 3D 顶点数据 ==========
    std::vector<Vertex3D> allVertices;
    const float windowWidth = YGNodeLayoutGetWidth(root);
    const float windowHeight = YGNodeLayoutGetHeight(root);

    // 辅助处理函数：传入节点和自定义的 Z 深度（可以按层级或数据指定）
    auto processNode = [&](YGNodeRef node, const char *name, float zDepth) {
        float left = YGNodeLayoutGetLeft(node);
        float top = YGNodeLayoutGetTop(node);
        float width = YGNodeLayoutGetWidth(node);
        float height = YGNodeLayoutGetHeight(node);

        std::cout << "节点 " << name << " : 位置(" << left << ", " << top << ") 尺寸("
                  << width << " x " << height << ")  Z = " << zDepth << "\n";

        auto vertices =
            rectToVertices3D(left, top, width, height, windowWidth, windowHeight, zDepth);
        allVertices.insert(allVertices.end(), vertices.begin(), vertices.end());
    };

    // 示例：为不同的矩形指定不同的 Z 值（模拟 3D 层级）
    processNode(child1, "Child1", 0.0f); // 放在 Z=0 平面
    processNode(child2, "Child2", 0.5f); // 放在 Z=0.5 平面（更靠近相机）

    // ========== 3. 输出 3D 顶点数据 ==========
    std::cout << "\n生成的 3D 顶点数据（位置 XYZ + 纹理 UV）共 " << allVertices.size()
              << " 个:\n";
    for (size_t i = 0; i < allVertices.size(); ++i)
    {
        const auto &v = allVertices[i];
        std::cout << "顶点 " << i << " : pos(" << v.position.x << ", " << v.position.y
                  << ", " << v.position.z << ")  uv(" << v.texCoord.x << ", "
                  << v.texCoord.y << ")\n";
    }

    // 清理
    YGNodeFreeRecursive(root);
    YGConfigFree(config);

    std::cout << "\nmain done\n";
    return 0;
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