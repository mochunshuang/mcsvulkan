#include <vector>
#include <string>
#include <iostream>
#include <functional>
#include <cassert>

// ---------- 扁平节点（现在携带自身索引） ----------
struct FlatNode
{
    std::string name;
    int firstChild = -1;
    int nextSibling = -1;
    int parent = -1;
    int selfIdx = -1; // 自己在数组中的索引
    // Config / Style 放这里
};

class FlatTree
{
  public:
    std::vector<FlatNode> nodes;

  private:
    int currentParent = -1;
    std::vector<int> parentStack;

    int addNode(const std::string &name, int parentIdx)
    {
        int idx = nodes.size();
        nodes.push_back({name, -1, -1, parentIdx, idx}); // selfIdx 记录自己
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
    FlatTree &root(const std::string &name)
    {
        nodes.clear();
        parentStack.clear();
        int idx = addNode(name, -1);
        currentParent = idx;
        return *this;
    }

    FlatTree &child(const std::string &name,
                    std::function<void(FlatTree &)> build = nullptr)
    {
        assert(currentParent != -1 && "call root() first");
        int childIdx = addNode(name, currentParent);

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

    // ---------- 遍历 ----------
    template <typename F>
    void forEachChild(int idx, F f) const
    {
        for (int c = nodes[idx].firstChild; c != -1; c = nodes[c].nextSibling)
            f(nodes[c], c);
    }

    void print(int idx = 0, int depth = 0) const
    {
        const auto &n = nodes[idx];
        std::cout << std::string(depth * 2, ' ') << n.name
                  << "  (idx=" << n.selfIdx // 节点自己的索引
                  << " first=" << n.firstChild << " next=" << n.nextSibling
                  << " parent=" << n.parent << ")\n";
        forEachChild(idx,
                     [&](const FlatNode &, int childIdx) { print(childIdx, depth + 1); });
    }
};

// ========== 使用 ==========
int main()
{
    FlatTree tree;

    tree.root("screen")
        .child("root", [](FlatTree &t) { t.child("child0").child("child1"); })
        .child("root2", [](FlatTree &t) { t.child("1"); })
        .child("text_display");

    std::cout << "Flat tree (with selfIdx):\n";
    tree.print();

    std::cout << "\nArray dump:\n";
    for (size_t i = 0; i < tree.nodes.size(); ++i)
    {
        const auto &n = tree.nodes[i];
        std::cout << "  [" << i << "] " << n.name << "  selfIdx=" << n.selfIdx
                  << "  firstChild=" << n.firstChild << "  nextSibling=" << n.nextSibling
                  << "  parent=" << n.parent << "\n";
    }

    // 验证每个节点的 selfIdx 确实等于数组下标
    for (size_t i = 0; i < tree.nodes.size(); ++i)
        assert(tree.nodes[i].selfIdx == (int)i);

    std::cout << "\nAll good.\n";
}