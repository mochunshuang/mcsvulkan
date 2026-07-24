#include <vector>
#include <string>
#include <iostream>
#include <functional>
#include <cassert>
#include <optional>

// NOLINTBEGIN
// ==================== 1. 组件类型 + 数据 ====================
enum class ComponentKind : uint8_t
{
    None,
    Container,
    Text
};

struct ContainerData
{
    std::optional<float> width, height;
};

struct TextData
{
    std::string content;
    float fontSize = 14.0f;
};

// ==================== 2. 全局数据池（每种类型一个 vector） ====================
struct GlobalPool
{
    std::vector<ContainerData> containers;
    std::vector<TextData> texts;

    uint32_t addContainer(ContainerData d)
    {
        uint32_t idx = containers.size();
        containers.push_back(std::move(d));
        return idx;
    }
    uint32_t addText(TextData d)
    {
        uint32_t idx = texts.size();
        texts.push_back(std::move(d));
        return idx;
    }
};
GlobalPool g_pool; // 全局单例（你的项目里可以改成传引用）

// ==================== 3. 轻量引用（节点只存这个） ====================
struct ComponentRef
{
    ComponentKind kind = ComponentKind::None;
    uint32_t index = 0;
};

// ==================== 4. 扁平节点（加一个 ComponentRef） ====================
struct FlatNode
{
    std::string name;
    ComponentRef comp; // 关联的组件数据
    int firstChild = -1;
    int nextSibling = -1;
    int parent = -1;
    int selfIdx = -1; // 数组下标
};

// ==================== 5. 树管理器 ====================
class FlatTree
{
  public:
    std::vector<FlatNode> nodes;

  private:
    int currentParent = -1;
    std::vector<int> parentStack;

    int addNode(const std::string &name, ComponentRef comp, int parentIdx)
    {
        int idx = nodes.size();
        nodes.push_back({name, comp, -1, -1, parentIdx, idx});
        if (parentIdx != -1)
        {
            FlatNode &p = nodes[parentIdx];
            if (p.firstChild == -1)
            {
                p.firstChild = idx;
            }
            else
            {
                int sib = p.firstChild;
                while (nodes[sib].nextSibling != -1)
                    sib = nodes[sib].nextSibling;
                nodes[sib].nextSibling = idx;
            }
        }
        return idx;
    }

  public:
    // 根节点（可带组件）
    FlatTree &root(const std::string &name, ComponentRef comp = {})
    {
        nodes.clear();
        parentStack.clear();
        int idx = addNode(name, comp, -1);
        currentParent = idx;
        return *this;
    }

    // 子节点（可带组件 + 可选的子树构建 lambda）
    FlatTree &child(const std::string &name, ComponentRef comp = {},
                    std::function<void(FlatTree &)> build = nullptr)
    {
        assert(currentParent != -1);
        int childIdx = addNode(name, comp, currentParent);

        if (build)
        {
            parentStack.push_back(currentParent);
            currentParent = childIdx;
            build(*this);
            currentParent = parentStack.back();
            parentStack.pop_back();
        }
        return *this;
    }

    // ---------- 遍历兄弟 ----------
    template <typename F>
    void forEachChild(int idx, F f) const
    {
        for (int c = nodes[idx].firstChild; c != -1; c = nodes[c].nextSibling)
            f(nodes[c], c);
    }

    // ---------- 打印（递归深度优先，自动解码组件类型） ----------
    void print(int idx = 0, int depth = 0) const
    {
        const auto &n = nodes[idx];
        // 缩进 + 名称
        std::cout << std::string(depth * 2, ' ') << n.name;

        // 根据组件类型，从全局池取出数据并打印
        switch (n.comp.kind)
        {
        case ComponentKind::Container: {
            const auto &d = g_pool.containers[n.comp.index];
            std::cout << " [Container]";
            if (d.width)
                std::cout << " w=" << *d.width;
            if (d.height)
                std::cout << " h=" << *d.height;
            break;
        }
        case ComponentKind::Text: {
            const auto &d = g_pool.texts[n.comp.index];
            std::cout << " [Text] \"" << d.content << "\"";
            if (d.fontSize != 14.0f)
                std::cout << " fontSize=" << d.fontSize;
            break;
        }
        default:
            break; // None 类型不打印额外信息
        }

        std::cout << " (idx=" << n.selfIdx << " first=" << n.firstChild
                  << " next=" << n.nextSibling << " parent=" << n.parent << ")\n";

        // 递归子节点
        forEachChild(idx,
                     [&](const FlatNode &, int childIdx) { print(childIdx, depth + 1); });
    }
};

// ==================== 6. 工厂函数（方便创建组件引用） ====================
ComponentRef makeContainer(float w, float h)
{
    return {ComponentKind::Container, g_pool.addContainer({w, h})};
}

ComponentRef makeContainer(std::optional<float> w = std::nullopt,
                           std::optional<float> h = std::nullopt)
{
    return {ComponentKind::Container, g_pool.addContainer({w, h})};
}

ComponentRef makeText(std::string content, float fontSize = 14.0f)
{
    return {ComponentKind::Text, g_pool.addText({std::move(content), fontSize})};
}

// ==================== 7. 使用示例 ====================
int main()
{
    FlatTree tree;

    tree.root("screen", makeContainer(800, 600))
        .child("header", makeContainer(800, 60),
               [](FlatTree &t) { t.child("title", makeText("My App", 24)); })
        .child("body", makeContainer(),
               [](FlatTree &t) {
                   t.child("list", makeContainer(200, 400))
                       .child("detail", makeContainer(600, 400));
               })
        .child("footer", makeText("© 2026", 12));

    std::cout << "Flat tree with typed components:\n";
    tree.print();

    std::cout << "\nArray dump:\n";
    for (size_t i = 0; i < tree.nodes.size(); ++i)
    {
        const auto &n = tree.nodes[i];
        std::cout << "  [" << i << "] " << n.name << "  comp.kind=" << (int)n.comp.kind
                  << "  comp.idx=" << n.comp.index << "  selfIdx=" << n.selfIdx
                  << "  firstChild=" << n.firstChild << "  nextSibling=" << n.nextSibling
                  << "  parent=" << n.parent << "\n";
    }

    // 验证 selfIdx
    for (size_t i = 0; i < tree.nodes.size(); ++i)
        assert(tree.nodes[i].selfIdx == (int)i);

    std::cout << "\nAll good.\n";
    return 0;
} // NOLINTEND