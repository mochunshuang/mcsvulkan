#include <cassert>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace cached_tree
{

    using Byte = std::byte;

    class TreeNode;

    class Data
    {
      public:
        virtual ~Data() = default;
        virtual size_t size() const noexcept = 0;
        virtual void copy_to(Byte *dst) const = 0;
        virtual std::unique_ptr<Data> clone() const = 0;

        void set_owner(TreeNode *node)
        {
            owner_ = node;
        }

        void notify_content_changed();
        void notify_size_changed();

        size_t offset = 0;
        size_t length = 0;

      private:
        TreeNode *owner_ = nullptr;
    };

    class TreeNode
    {
      public:
        std::unique_ptr<Data> data;
        std::vector<std::unique_ptr<TreeNode>> children;
        TreeNode *parent = nullptr;

        bool layout_dirty = true;
        bool content_dirty = false;

        TreeNode() = default;
        explicit TreeNode(std::unique_ptr<Data> d) : data(std::move(d))
        {
            if (data)
                data->set_owner(this);
        }

        void mark_layout_dirty()
        {
            TreeNode *p = this;
            while (p && !p->layout_dirty)
            {
                p->layout_dirty = true;
                p = p->parent;
            }
        }

        void mark_content_dirty()
        {
            if (layout_dirty)
                return;
            TreeNode *p = this;
            while (p)
            {
                if (!p->content_dirty)
                {
                    p->content_dirty = true;
                    p = p->parent;
                }
                else
                {
                    break;
                }
            }
        }

        void on_content_changed()
        {
            if (!data || data->length == 0)
            {
                on_size_changed();
                return;
            }
            mark_content_dirty();
        }

        void on_size_changed()
        {
            mark_layout_dirty();
            content_dirty = false;
        }

        void clear_dirty_recursive()
        {
            layout_dirty = false;
            content_dirty = false;
            for (auto &ch : children)
            {
                ch->clear_dirty_recursive();
            }
        }

        void set_data(std::unique_ptr<Data> d)
        {
            if (data)
                data->set_owner(nullptr);
            data = std::move(d);
            if (data)
                data->set_owner(this);
            mark_layout_dirty();
        }

        bool is_ancestor_of(const TreeNode *other) const
        {
            const TreeNode *p = other;
            while (p)
            {
                if (p == this)
                    return true;
                p = p->parent;
            }
            return false;
        }
    };

    inline void Data::notify_content_changed()
    {
        if (owner_)
            owner_->on_content_changed();
    }

    inline void Data::notify_size_changed()
    {
        if (owner_)
            owner_->on_size_changed();
    }

    class Tree
    {
      public:
        Tree() = default;
        explicit Tree(std::unique_ptr<TreeNode> root) : root_(std::move(root))
        {
            if (root_)
                root_->parent = nullptr;
        }

        Tree(const Tree &) = delete;
        Tree &operator=(const Tree &) = delete;
        Tree(Tree &&) noexcept = default;
        Tree &operator=(Tree &&) noexcept = default;

        TreeNode *root()
        {
            return root_.get();
        }
        const TreeNode *root() const
        {
            return root_.get();
        }

        void set_root(std::unique_ptr<TreeNode> r)
        {
            root_ = std::move(r);
            if (root_)
                root_->parent = nullptr;
        }

        TreeNode *add_child(TreeNode *parent, std::unique_ptr<TreeNode> child)
        {
            if (!parent || !child)
                return nullptr;
            if (parent->is_ancestor_of(child.get()))
                return nullptr;
            child->parent = parent;
            TreeNode *raw = child.get();
            parent->children.push_back(std::move(child));
            parent->mark_layout_dirty();
            return raw;
        }

        TreeNode *insert_child(TreeNode *parent, size_t index,
                               std::unique_ptr<TreeNode> child)
        {
            if (!parent || !child || index > parent->children.size())
                return nullptr;
            if (parent->is_ancestor_of(child.get()))
                return nullptr;
            child->parent = parent;
            TreeNode *raw = child.get();
            parent->children.insert(parent->children.begin() +
                                        static_cast<std::ptrdiff_t>(index),
                                    std::move(child));
            parent->mark_layout_dirty();
            return raw;
        }

        std::unique_ptr<TreeNode> remove_child(TreeNode *parent, size_t index)
        {
            if (!parent || index >= parent->children.size())
                return nullptr;
            auto it = parent->children.begin() + static_cast<std::ptrdiff_t>(index);
            auto removed = std::move(*it);
            parent->children.erase(it);
            removed->parent = nullptr;
            parent->mark_layout_dirty();
            return removed;
        }

        std::unique_ptr<TreeNode> detach(TreeNode *node)
        {
            if (!node || !node->parent)
                return nullptr;
            auto *p = node->parent;
            for (size_t i = 0; i < p->children.size(); ++i)
            {
                if (p->children[i].get() == node)
                {
                    return remove_child(p, i);
                }
            }
            return nullptr;
        }

        bool move_subtree(TreeNode *node, TreeNode *new_parent, size_t index = SIZE_MAX)
        {
            if (!node || !new_parent)
                return false;
            if (node == new_parent)
                return false;
            if (node->is_ancestor_of(new_parent))
                return false;
            if (node == root_.get())
                return false;

            size_t pos = (index == SIZE_MAX) ? new_parent->children.size() : index;
            if (pos > new_parent->children.size())
                return false;

            auto detached = detach(node);
            if (!detached)
                return false;

            detached->parent = new_parent;
            new_parent->children.insert(new_parent->children.begin() +
                                            static_cast<std::ptrdiff_t>(pos),
                                        std::move(detached));
            new_parent->mark_layout_dirty();
            return true;
        }

        void swap_data(TreeNode *a, TreeNode *b)
        {
            if (!a || !b || a == b)
                return;
            std::swap(a->data, b->data);
            if (a->data)
                a->data->set_owner(a);
            if (b->data)
                b->data->set_owner(b);
            a->mark_layout_dirty();
            b->mark_layout_dirty();
        }

        void set_data(TreeNode *node, std::unique_ptr<Data> d)
        {
            if (!node)
                return;
            node->set_data(std::move(d));
        }

        bool needs_sync() const noexcept
        {
            return root_ && (root_->layout_dirty || root_->content_dirty);
        }

        bool layout_dirty() const noexcept
        {
            return root_ && root_->layout_dirty;
        }

        void sync()
        {
            if (!root_)
                return;
            if (root_->layout_dirty)
            {
                rebuild();
                root_->clear_dirty_recursive();
            }
            else if (root_->content_dirty)
            {
                update_dirty_contents();
                clear_content_dirty_recursive(root_.get());
            }
        }

        const Byte *data()
        {
            sync();
            return buffer_.data();
        }

        size_t data_size()
        {
            sync();
            return total_size_;
        }

        const std::vector<Byte> &buffer()
        {
            sync();
            return buffer_;
        }

        size_t capacity() const noexcept
        {
            return buffer_.capacity();
        }
        void reserve(size_t cap)
        {
            buffer_.reserve(cap);
        }

        template <class F>
        void dfs(F &&f) const
        {
            dfs_impl(root_.get(), std::forward<F>(f));
        }

      private:
        template <class F>
        static void dfs_impl(TreeNode *node, F &&f)
        {
            if (!node)
                return;
            f(node);
            for (auto &ch : node->children)
            {
                dfs_impl(ch.get(), std::forward<F>(f));
            }
        }

        template <class F>
        static void dfs_impl(const TreeNode *node, F &&f)
        {
            if (!node)
                return;
            f(node);
            for (const auto &ch : node->children)
            {
                dfs_impl(ch.get(), std::forward<F>(f));
            }
        }

        void rebuild()
        {
            size_t total = compute_total_size(root_.get());

            if (total > buffer_.capacity())
            {
                size_t next_pow2 = 1;
                while (next_pow2 < total)
                    next_pow2 <<= 1;
                size_t new_cap =
                    std::max({total, buffer_.capacity() * 2, next_pow2, size_t(64)});
                buffer_.reserve(new_cap);
            }

            buffer_.clear();
            buffer_.resize(total);
            assign_offsets(root_.get(), 0);
            total_size_ = total;
        }

        void update_dirty_contents()
        {
            if (!root_)
                return;
            dfs_impl(root_.get(), [this](TreeNode *node) {
                if (node->content_dirty && node->data && node->data->length > 0)
                {
                    node->data->copy_to(buffer_.data() + node->data->offset);
                }
            });
        }

        void clear_content_dirty_recursive(TreeNode *node)
        {
            if (!node)
                return;
            node->content_dirty = false;
            for (auto &ch : node->children)
            {
                clear_content_dirty_recursive(ch.get());
            }
        }

        size_t compute_total_size(const TreeNode *node) const
        {
            if (!node)
                return 0;
            size_t s = node->data ? node->data->size() : 0;
            for (const auto &ch : node->children)
            {
                s += compute_total_size(ch.get());
            }
            return s;
        }

        size_t assign_offsets(TreeNode *node, size_t offset)
        {
            if (!node)
                return offset;
            size_t len = node->data ? node->data->size() : 0;
            if (node->data)
            {
                node->data->offset = offset;
                node->data->length = len;
                if (len > 0)
                {
                    node->data->copy_to(buffer_.data() + offset);
                }
            }
            offset += len;
            for (auto &ch : node->children)
            {
                offset = assign_offsets(ch.get(), offset);
            }
            return offset;
        }

        std::unique_ptr<TreeNode> root_;
        std::vector<Byte> buffer_;
        size_t total_size_ = 0;
    };

} // namespace cached_tree

using namespace cached_tree;

// ---------------- 测试数据类型 ----------------

class StringData : public Data
{
  public:
    explicit StringData(std::string s) : value_(std::move(s)) {}

    size_t size() const noexcept override
    {
        return value_.size();
    }

    void copy_to(Byte *dst) const override
    {
        if (!value_.empty())
        {
            std::memcpy(dst, value_.data(), value_.size());
        }
    }

    std::unique_ptr<Data> clone() const override
    {
        return std::make_unique<StringData>(value_);
    }

    void set_value(const std::string &new_val)
    {
        size_t old_size = value_.size();
        value_ = new_val;
        if (value_.size() == old_size)
        {
            notify_content_changed();
        }
        else
        {
            notify_size_changed();
        }
    }

    const std::string &value() const
    {
        return value_;
    }

  private:
    std::string value_;
};

class IntData : public Data
{
  public:
    explicit IntData(int v) : value_(v) {}

    size_t size() const noexcept override
    {
        return sizeof(int);
    }

    void copy_to(Byte *dst) const override
    {
        std::memcpy(dst, &value_, sizeof(int));
    }

    std::unique_ptr<Data> clone() const override
    {
        return std::make_unique<IntData>(value_);
    }

    void set_value(int new_val)
    {
        value_ = new_val;
        notify_content_changed();
    }

    int value() const
    {
        return value_;
    }

  private:
    int value_;
};

// ---------------- 测试辅助 ----------------

static std::unique_ptr<TreeNode> make_node(std::unique_ptr<Data> data = nullptr)
{
    auto n = std::make_unique<TreeNode>();
    if (data)
        n->set_data(std::move(data));
    return n;
}

static int read_int(const std::byte *p)
{
    int v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

static std::string tree_string(Tree &tree)
{
    const auto &buf = tree.buffer();
    return std::string(reinterpret_cast<const char *>(buf.data()), tree.data_size());
}

// 从缓存中按 offset/length 提取字符串
static std::string read_string_at(const std::byte *buffer, const Data *data)
{
    if (!data || data->length == 0)
        return {};
    return std::string(reinterpret_cast<const char *>(buffer + data->offset),
                       data->length);
}

// ---------------- 测试用例（带报告） ----------------

static int passed_count = 0;
static int total_count = 0;

void test_report(const std::string &name, const std::string &description)
{
    ++total_count;
    std::cout << "\n=== Test: " << name << " ===\n";
    std::cout << "Description: " << description << "\n";
}

void pass()
{
    ++passed_count;
    std::cout << "Result: PASS\n";
}

void test_basic_dfs_order()
{
    test_report("Basic DFS Order",
                "验证前序遍历时数据按根-子顺序紧凑排列，空节点不占空间。");

    Tree tree;
    tree.set_root(make_node()); // 根无数据

    auto *r = tree.root();
    tree.add_child(r, make_node(std::make_unique<StringData>("A")));
    auto *b = tree.add_child(r, make_node(std::make_unique<StringData>("B")));
    tree.add_child(r, make_node(std::make_unique<StringData>("C")));
    tree.add_child(b, make_node(std::make_unique<StringData>("D")));

    // 前序遍历：根(空) -> A -> B -> D -> C
    assert(tree_string(tree) == "ABDC");
    pass();
}

void test_initial_layout_and_offsets()
{
    test_report("Initial Layout and Offsets",
                "首次同步后，每个 Data 的 offset/length "
                "应连续且与前序顺序一致，缓存内容与数据内容一致。");

    Tree tree;
    tree.set_root(make_node(std::make_unique<IntData>(1)));

    auto *r = tree.root();
    auto *a = tree.add_child(r, make_node(std::make_unique<StringData>("Hello")));
    tree.add_child(r, make_node(std::make_unique<IntData>(42)));
    tree.add_child(a, make_node(std::make_unique<StringData>("World")));

    tree.sync();

    // 检查 offset 连续性
    size_t expected_offset = 0;
    tree.dfs([&](const TreeNode *n) {
        if (!n->data)
            return;
        assert(n->data->offset == expected_offset);
        assert(n->data->length == n->data->size());
        expected_offset += n->data->length;
    });
    assert(expected_offset == tree.data_size());
    assert(tree.data_size() == sizeof(int) + 5 + 5 + sizeof(int));

    // 通过 offset/length 读取根节点 int
    const std::byte *p = tree.data();
    assert(read_int(p + tree.root()->data->offset) == 1);

    pass();
}

void test_content_update_same_size()
{
    test_report("Content Update (Same Size)",
                "数据内容改变但大小不变时，应仅标记内容脏，同步时只更新对应区间，缓冲区地"
                "址和总大小不变，其他节点 offset 不受影响。");

    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("root")));

    auto *r = tree.root();
    auto *a = tree.add_child(r, make_node(std::make_unique<StringData>("AAA")));
    auto *b = tree.add_child(r, make_node(std::make_unique<StringData>("BBB")));

    tree.sync();
    const Byte *old_buffer = tree.data();
    size_t old_size = tree.data_size();
    assert(tree_string(tree) == "rootAAABBB");

    // 修改 a 的内容，保持长度不变
    auto *a_data = dynamic_cast<StringData *>(a->data.get());
    a_data->set_value("CCC");
    assert(tree.needs_sync());
    assert(!tree.layout_dirty()); // 不应布局脏

    tree.sync();
    assert(tree_string(tree) == "rootCCCBBB");
    assert(tree.data() == old_buffer);    // 缓冲区地址不变
    assert(tree.data_size() == old_size); // 总大小不变

    // 通过 offset/length 直接读取 b 的数据，确认未被移动
    auto *b_node = r->children[1].get();
    auto *b_data = dynamic_cast<StringData *>(b_node->data.get());
    assert(b_data->offset == 7); // root(4) + CCC(3) = 7
    assert(read_string_at(tree.data(), b_data) == "BBB");

    // 通过 offset/length 读取 a 的新数据
    assert(read_string_at(tree.data(), a_data) == "CCC");

    pass();
}

void test_content_update_size_change()
{
    test_report("Content Update (Size Change)",
                "数据内容改变且大小变化时，应触发布局脏，同步时重新计算所有 "
                "offset/length 并重建缓冲区。");

    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("root")));

    auto *r = tree.root();
    auto *a = tree.add_child(r, make_node(std::make_unique<StringData>("AAA")));
    auto *b = tree.add_child(r, make_node(std::make_unique<StringData>("BBB")));

    tree.sync();
    assert(tree_string(tree) == "rootAAABBB");

    // 修改 a，长度改变
    auto *a_data = dynamic_cast<StringData *>(a->data.get());
    a_data->set_value("AAAAA");
    assert(tree.layout_dirty());

    tree.sync();
    assert(tree_string(tree) == "rootAAAAABBB");

    // 验证 b 的 offset 已更新
    auto *b_node = r->children[1].get();
    auto *b_data = dynamic_cast<StringData *>(b_node->data.get());
    assert(b_data->offset == 4 + 5); // root(4) + AAAAA(5) = 9
    assert(read_string_at(tree.data(), b_data) == "BBB");

    pass();
}

void test_swap_data()
{
    test_report(
        "Swap Data",
        "交换两个节点的 Data 指针后，缓存顺序应反映交换后的数据，且拥有者关系正确更新。");

    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("root")));

    auto *r = tree.root();
    auto *a = tree.add_child(r, make_node(std::make_unique<StringData>("AAA")));
    auto *b = tree.add_child(r, make_node(std::make_unique<StringData>("BBB")));

    tree.swap_data(a, b);
    assert(tree_string(tree) == "rootBBBAAA");

    // 通过 offset/length 验证缓存数据属于新节点
    auto *a_data = dynamic_cast<StringData *>(a->data.get());
    auto *b_data = dynamic_cast<StringData *>(b->data.get());
    assert(read_string_at(tree.data(), a_data) == "BBB");
    assert(read_string_at(tree.data(), b_data) == "AAA");

    pass();
}

void test_move_subtree()
{
    test_report(
        "Move Subtree",
        "移动子树后，前序遍历顺序改变，所有相关节点的 offset/length 需重新计算。");

    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("R")));

    auto *r = tree.root();
    auto *a = tree.add_child(r, make_node(std::make_unique<StringData>("A")));
    auto *b = tree.add_child(r, make_node(std::make_unique<StringData>("B")));

    tree.add_child(a, make_node(std::make_unique<StringData>("A1")));
    tree.add_child(b, make_node(std::make_unique<StringData>("B1")));
    tree.add_child(b, make_node(std::make_unique<StringData>("B2")));

    assert(tree_string(tree) == "RAA1BB1B2");

    bool ok = tree.move_subtree(a, b, 0);
    assert(ok);
    assert(tree_string(tree) == "RBAA1B1B2");

    // 验证移动后节点数据在缓存中位置正确（通过 offset）
    auto *a_node = tree.root()->children[0]->children[0].get(); // B 的第一个孩子是 A
    auto *a_data = dynamic_cast<StringData *>(a_node->data.get());
    assert(read_string_at(tree.data(), a_data) == "A");

    pass();
}

void test_remove_node()
{
    test_report("Remove Node",
                "删除节点后，其子树从缓存中移除，后续数据前移，offset 更新。");

    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("R")));

    auto *r = tree.root();
    tree.add_child(r, make_node(std::make_unique<StringData>("A")));
    auto *b = tree.add_child(r, make_node(std::make_unique<StringData>("B")));
    tree.add_child(b, make_node(std::make_unique<StringData>("B1")));

    assert(tree_string(tree) == "RABB1");

    auto removed = tree.remove_child(r, 0); // 移除 A
    assert(removed);
    assert(removed->parent == nullptr);
    assert(removed->data->size() == 1);

    assert(tree_string(tree) == "RBB1");

    // B 现在应该是 root 的第一个孩子
    auto *b_node = tree.root()->children[0].get();
    auto *b_data = dynamic_cast<StringData *>(b_node->data.get());
    assert(b_data->offset == 1); // R 占 1 字节
    assert(read_string_at(tree.data(), b_data) == "B");

    pass();
}

void test_empty_data_nodes()
{
    test_report("Empty Data Nodes",
                "没有数据的节点不占用缓存空间，但其它节点的 offset 应正确跳过它们。");

    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("X")));

    auto *r = tree.root();
    tree.add_child(r, make_node()); // 空 data
    tree.add_child(r, make_node(std::make_unique<StringData>("Y")));

    assert(tree_string(tree) == "XY");

    tree.sync();
    size_t empty_count = 0;
    tree.dfs([&](const TreeNode *n) {
        if (!n->data)
            ++empty_count;
    });
    assert(empty_count == 1);

    // 验证 Y 的 offset 跳过了空节点（X 占 1 字节，空节点占 0，所以 Y 的 offset 为 1）
    auto *y_node = tree.root()->children[1].get();
    auto *y_data = dynamic_cast<StringData *>(y_node->data.get());
    assert(y_data->offset == 1);
    assert(read_string_at(tree.data(), y_data) == "Y");

    pass();
}

void test_capacity_growth()
{
    test_report("Capacity Growth",
                "缓冲区扩容采用 2 倍或下一个 2 次幂，避免频繁搬移；容量不足时自动扩容。");

    Tree tree;
    tree.reserve(8);

    tree.set_root(make_node(std::make_unique<StringData>(std::string(1000, 'a'))));
    tree.sync();
    size_t cap_before = tree.capacity();
    assert(cap_before >= 1000);

    auto *r = tree.root();
    tree.add_child(r, make_node(std::make_unique<StringData>(std::string(10, 'b'))));
    tree.sync();
    size_t cap_after = tree.capacity();

    assert(cap_after == cap_before); // 1000 -> 1024，新增 10 字节未触发再次扩容
    assert(tree.data_size() == 1010);

    pass();
}

void test_dirty_merging()
{
    test_report("Dirty Merging",
                "多次结构修改只标记脏，直到 sync 才重建缓存，减少不必要的计算。");

    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("A")));

    assert(tree.needs_sync());
    tree.sync();
    assert(!tree.needs_sync());

    auto *r = tree.root();
    tree.add_child(r, make_node(std::make_unique<StringData>("B")));
    assert(tree.needs_sync());
    tree.add_child(r, make_node(std::make_unique<StringData>("C")));
    assert(tree.needs_sync());

    tree.sync();
    assert(!tree.needs_sync());
    assert(tree_string(tree) == "ABC");

    pass();
}

void test_polymorphism_mixed_types()
{
    test_report("Polymorphism Mixed Types",
                "不同类型的数据（int 和 string）共存时，offset/length 根据各自 size "
                "正确计算，缓存按前序混合存放。");

    Tree tree;
    tree.set_root(make_node(std::make_unique<IntData>(7)));

    auto *r = tree.root();
    auto *a = tree.add_child(r, make_node(std::make_unique<StringData>("mix")));
    tree.add_child(r, make_node(std::make_unique<IntData>(99)));
    tree.add_child(a, make_node(std::make_unique<StringData>("ed")));

    tree.sync();

    size_t expected_size = sizeof(int) + 3 + sizeof(int) + 2;
    assert(tree.data_size() == expected_size);

    const std::byte *p = tree.data();

    // 通过 offset/length 读取每个节点
    std::vector<std::string> strings;
    std::vector<int> ints;
    tree.dfs([&](const TreeNode *n) {
        if (auto *d = dynamic_cast<const StringData *>(n->data.get()))
        {
            strings.push_back(read_string_at(p, d));
        }
        else if (auto *d = dynamic_cast<const IntData *>(n->data.get()))
        {
            ints.push_back(read_int(p + d->offset));
        }
    });

    assert((strings == std::vector<std::string>{"mix", "ed"}));
    assert((ints == std::vector<int>{7, 99}));

    pass();
}

int main()
{
    test_basic_dfs_order();
    test_initial_layout_and_offsets();
    test_content_update_same_size();
    test_content_update_size_change();
    test_swap_data();
    test_move_subtree();
    test_remove_node();
    test_empty_data_nodes();
    test_capacity_growth();
    test_dirty_merging();
    test_polymorphism_mixed_types();

    std::cout << "\n====================\n";
    std::cout << "Tests passed: " << passed_count << "/" << total_count << "\n";
    if (passed_count == total_count)
        std::cout << "ALL TESTS PASSED\n";
    else
        std::cout << "SOME TESTS FAILED\n";

    return 0;
}