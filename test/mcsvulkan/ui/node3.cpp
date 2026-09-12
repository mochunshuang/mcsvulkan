#include <cassert>
#include <cstddef>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <vector>
#include <bit>
#include <algorithm>
#include <string>
#include <random>

using Byte = std::byte;

// ========================= Chache: 紧凑缓冲区管理器 =========================
class Chache
{
  public:
    static constexpr size_t INVALID_ID = SIZE_MAX;
    static constexpr size_t INVALID_DENSE = SIZE_MAX;

    Chache(size_t capacity = 4)
        : data_(std::make_unique_for_overwrite<unsigned char[]>(std::bit_ceil(capacity))),
          capacity_(std::bit_ceil(capacity)), next_offset_(0), next_id_(0)
    {
        sparse_.resize(capacity_, INVALID_DENSE);
    }

    size_t allocate_span(size_t length)
    {
        size_t id;
        if (!free_ids_.empty())
        {
            id = free_ids_.back();
            free_ids_.pop_back();
        }
        else
        {
            id = next_id_++;
            if (id >= sparse_.size())
            {
                sparse_.resize(id + 1, INVALID_DENSE);
            }
        }
        size_t dense_index = dense_.size();
        dense_.push_back({id, next_offset_, length, 0});
        sparse_[id] = dense_index;

        size_t old_offset = next_offset_; // 保存扩容前的偏移
        next_offset_ += length;
        if (next_offset_ > capacity_)
        {
            grow_buffer(next_offset_, old_offset); // 传入旧偏移
        }
        return id;
    }

    void release_span(size_t id)
    {
        assert(span_valid(id));
        size_t index = sparse_[id];
        SpanInfo &info = dense_[index];

        size_t tail_start = info.offset + info.length;
        size_t tail_size = next_offset_ - tail_start;
        if (tail_size > 0)
        {
            std::memmove(data_.get() + info.offset, data_.get() + tail_start, tail_size);
        }
        next_offset_ -= info.length;
        for (size_t i = index + 1; i < dense_.size(); ++i)
        {
            dense_[i].offset -= info.length;
        }
        dense_.erase(dense_.begin() + index);
        sparse_[id] = INVALID_DENSE;
        free_ids_.push_back(id);
        rebuild_sparse_mapping();
    }

    void resize_span(size_t id, size_t new_length)
    {
        assert(span_valid(id));
        size_t index = sparse_[id];
        SpanInfo &info = dense_[index];
        size_t old_length = info.length;
        if (new_length == old_length)
            return;

        size_t old_tail_start = info.offset + old_length;
        size_t tail_size = next_offset_ - old_tail_start;
        size_t new_tail_start = info.offset + new_length;
        size_t required_size = new_tail_start + tail_size;

        if (required_size > capacity_)
        {
            grow_buffer_and_move(index, new_length, required_size);
        }
        else
        {
            std::memmove(data_.get() + new_tail_start, data_.get() + old_tail_start,
                         tail_size);
        }
        size_t delta = new_length - old_length;
        info.length = new_length;
        next_offset_ += delta;
        for (size_t i = index + 1; i < dense_.size(); ++i)
        {
            dense_[i].offset += delta;
        }
    }

    void move_span_range(size_t src_start, size_t count, size_t dst_start)
    {
        if (count == 0 || src_start == dst_start)
            return;
        if (dst_start == src_start + count)
            return; // 移动后位置不变，避免无谓操作
        assert(src_start + count <= dense_.size());
        assert(dst_start <= dense_.size());
        if (dst_start > src_start && dst_start < src_start + count)
            return; // 无效移动

        // 保存被移动的 span 信息
        std::vector<SpanInfo> moved;
        moved.reserve(count);
        size_t total_len = 0;
        for (size_t i = src_start; i < src_start + count; ++i)
        {
            moved.push_back(dense_[i]);
            total_len += dense_[i].length;
        }
        // 备份数据
        std::vector<unsigned char> temp_data(total_len);
        size_t src_data_offset = dense_[src_start].offset;
        std::memcpy(temp_data.data(), data_.get() + src_data_offset, total_len);

        // 从源位置删除：将尾部数据前移
        size_t tail_start = src_data_offset + total_len;
        size_t tail_size = next_offset_ - tail_start;
        if (tail_size > 0)
        {
            std::memmove(data_.get() + src_data_offset, data_.get() + tail_start,
                         tail_size);
        }
        next_offset_ -= total_len;
        for (size_t i = src_start; i < dense_.size(); ++i)
        {
            dense_[i].offset -= total_len;
        }
        dense_.erase(dense_.begin() + src_start, dense_.begin() + src_start + count);

        // 计算调整后的目标索引
        size_t adjusted_dst = dst_start;
        if (dst_start > src_start)
        {
            adjusted_dst -= count;
        }

        // 在目标位置插入：后移目标位置及之后的数据
        size_t dst_data_offset =
            (adjusted_dst < dense_.size()) ? dense_[adjusted_dst].offset : next_offset_;
        if (adjusted_dst < dense_.size())
        {
            size_t move_size = next_offset_ - dst_data_offset;
            if (move_size > 0)
            {
                std::memmove(data_.get() + dst_data_offset + total_len,
                             data_.get() + dst_data_offset, move_size);
            }
            for (size_t i = adjusted_dst; i < dense_.size(); ++i)
            {
                dense_[i].offset += total_len;
            }
        }
        next_offset_ += total_len;

        // 写入被移动的数据
        std::memcpy(data_.get() + dst_data_offset, temp_data.data(), total_len);

        // 更新被移动 span 的偏移并插入 dense 数组
        size_t cur_offset = dst_data_offset;
        for (auto &s : moved)
        {
            s.offset = cur_offset;
            cur_offset += s.length;
        }
        dense_.insert(dense_.begin() + adjusted_dst, moved.begin(), moved.end());
        rebuild_sparse_mapping();
    }

    void remove_span_range(size_t start, size_t count)
    {
        if (count == 0)
            return;
        assert(start + count <= dense_.size());
        size_t total_len = 0;
        for (size_t i = start; i < start + count; ++i)
        {
            total_len += dense_[i].length;
        }
        size_t data_offset = dense_[start].offset;
        size_t tail_start = data_offset + total_len;
        size_t tail_size = next_offset_ - tail_start;
        if (tail_size > 0)
        {
            std::memmove(data_.get() + data_offset, data_.get() + tail_start, tail_size);
        }
        next_offset_ -= total_len;
        for (size_t i = start + count; i < dense_.size(); ++i)
        {
            dense_[i].offset -= total_len;
        }
        for (size_t i = start; i < start + count; ++i)
        {
            sparse_[dense_[i].id] = INVALID_DENSE;
            free_ids_.push_back(dense_[i].id);
        }
        dense_.erase(dense_.begin() + start, dense_.begin() + start + count);
        rebuild_sparse_mapping();
    }

    bool span_valid(size_t id) const
    {
        return id < sparse_.size() && sparse_[id] != INVALID_DENSE;
    }
    size_t span_offset(size_t id) const
    {
        assert(span_valid(id));
        return dense_[sparse_[id]].offset;
    }
    size_t span_length(size_t id) const
    {
        assert(span_valid(id));
        return dense_[sparse_[id]].length;
    }
    size_t span_used(size_t id) const
    {
        assert(span_valid(id));
        return dense_[sparse_[id]].used;
    }
    size_t get_dense_index(size_t id) const
    {
        assert(span_valid(id));
        return sparse_[id];
    }
    size_t span_count() const
    {
        return dense_.size();
    }
    size_t size() const
    {
        return next_offset_;
    }
    size_t capacity() const
    {
        return capacity_;
    }
    unsigned char *data()
    {
        return data_.get();
    }
    const unsigned char *data() const
    {
        return data_.get();
    }

  private:
    struct SpanInfo
    {
        size_t id;
        size_t offset;
        size_t length;
        size_t used;
    };

    void grow_buffer(size_t required, size_t old_size)
    {
        size_t new_cap = std::bit_ceil(std::max(required, capacity_ * 2));
        auto new_data = std::make_unique_for_overwrite<unsigned char[]>(new_cap);
        if (data_ && old_size > 0)
        {
            std::memcpy(new_data.get(), data_.get(), old_size); // 只复制有效数据
        }
        data_ = std::move(new_data);
        capacity_ = new_cap;
    }

    void grow_buffer_and_move(size_t index, size_t new_length, size_t required)
    {
        size_t new_cap = std::bit_ceil(std::max(required, capacity_ * 2));
        auto new_data = std::make_unique_for_overwrite<unsigned char[]>(new_cap);
        SpanInfo &info = dense_[index];
        // 复制调整点之前的数据
        if (info.offset > 0)
        {
            std::memcpy(new_data.get(), data_.get(), info.offset);
        }
        // 复制被调整 span 的原始数据（长度仍为旧长度）
        if (info.length > 0)
        {
            std::memcpy(new_data.get() + info.offset, data_.get() + info.offset,
                        info.length);
        }
        // 复制尾部数据到新位置
        size_t old_tail_start = info.offset + info.length;
        size_t tail_size = next_offset_ - old_tail_start;
        size_t new_tail_start = info.offset + new_length;
        if (tail_size > 0)
        {
            std::memcpy(new_data.get() + new_tail_start, data_.get() + old_tail_start,
                        tail_size);
        }
        data_ = std::move(new_data);
        capacity_ = new_cap;
    }

    void rebuild_sparse_mapping()
    {
        std::fill(sparse_.begin(), sparse_.end(), INVALID_DENSE);
        for (size_t i = 0; i < dense_.size(); ++i)
        {
            sparse_[dense_[i].id] = i;
        }
    }

    std::unique_ptr<unsigned char[]> data_;
    size_t capacity_;
    size_t next_offset_;
    size_t next_id_;
    std::vector<SpanInfo> dense_;
    std::vector<size_t> sparse_;
    std::vector<size_t> free_ids_;
};

// ========================= Data 基类 =========================
class Data
{
  public:
    virtual ~Data() = default;
    virtual size_t size() const noexcept = 0;
    virtual void copy_to(Byte *dst) const = 0;
    virtual std::unique_ptr<Data> clone() const = 0;

    size_t span_id = Chache::INVALID_ID;
};

// ========================= TreeNode =========================
class TreeNode
{
  public:
    std::unique_ptr<Data> data;
    std::vector<std::unique_ptr<TreeNode>> children;
    TreeNode *parent = nullptr;

    TreeNode() = default;
    explicit TreeNode(std::unique_ptr<Data> d) : data(std::move(d)) {}
};

// ========================= Tree 管理器 =========================
class Tree
{
  public:
    Tree() = default;
    ~Tree()
    {
        clear_all();
    }

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
        clear_all();
        root_ = std::move(r);
        if (root_)
        {
            root_->parent = nullptr;
            register_node_and_children(root_.get());
        }
    }

    TreeNode *add_child(TreeNode *parent, std::unique_ptr<TreeNode> child)
    {
        return insert_child(parent, parent->children.size(), std::move(child));
    }

    TreeNode *insert_child(TreeNode *parent, size_t index,
                           std::unique_ptr<TreeNode> child)
    {
        if (!parent || !child || index > parent->children.size())
            return nullptr;

        register_node_and_children(child.get());

        size_t src_start = get_subtree_start_index(child.get());
        size_t count = get_subtree_span_count(child.get());
        assert(src_start + count == cache_.span_count());

        size_t dst_start;
        if (index == 0)
        {
            dst_start = get_first_child_dense_index(parent);
        }
        else
        {
            TreeNode *prev = parent->children[index - 1].get();
            dst_start = get_subtree_end_index(prev);
        }

        if (src_start != dst_start)
        {
            cache_.move_span_range(src_start, count, dst_start);
        }

        child->parent = parent;
        parent->children.insert(parent->children.begin() + index, std::move(child));
        return parent->children[index].get();
    }

    std::unique_ptr<TreeNode> remove_child(TreeNode *parent, size_t index)
    {
        if (!parent || index >= parent->children.size())
            return nullptr;
        TreeNode *child = parent->children[index].get();

        size_t start = get_subtree_start_index(child);
        size_t count = get_subtree_span_count(child);
        if (count > 0)
        {
            cache_.remove_span_range(start, count);
        }

        auto removed = std::move(parent->children[index]);
        parent->children.erase(parent->children.begin() + index);
        removed->parent = nullptr;
        unregister_node_and_children(removed.get());
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
        if (node == root_.get())
            return false;
        TreeNode *p = new_parent;
        while (p)
        {
            if (p == node)
                return false;
            p = p->parent;
        }
        size_t pos = (index == SIZE_MAX) ? new_parent->children.size() : index;
        if (pos > new_parent->children.size())
            return false;

        size_t src_start = get_subtree_start_index(node);
        size_t count = get_subtree_span_count(node);
        if (count > 0)
        {
            size_t dst_start;
            if (pos == 0)
            {
                dst_start = get_first_child_dense_index(new_parent);
            }
            else
            {
                TreeNode *prev = new_parent->children[pos - 1].get();
                dst_start = get_subtree_end_index(prev);
            }
            cache_.move_span_range(src_start, count, dst_start);
        }

        auto *old_parent = node->parent;
        size_t old_index = 0;
        for (size_t i = 0; i < old_parent->children.size(); ++i)
        {
            if (old_parent->children[i].get() == node)
            {
                old_index = i;
                break;
            }
        }
        auto detached = std::move(old_parent->children[old_index]);
        old_parent->children.erase(old_parent->children.begin() + old_index);
        detached->parent = new_parent;
        new_parent->children.insert(new_parent->children.begin() + pos,
                                    std::move(detached));
        return true;
    }

    void set_data(TreeNode *node, std::unique_ptr<Data> d)
    {
        if (!node)
            return;
        if (node->data && node->data->span_id != Chache::INVALID_ID)
        {
            size_t old_id = node->data->span_id;
            size_t old_index = cache_.get_dense_index(old_id);
            cache_.release_span(old_id);
            node->data->span_id = Chache::INVALID_ID;

            node->data = std::move(d);
            if (node->data)
            {
                size_t len = node->data->size();
                size_t new_id = cache_.allocate_span(len);
                node->data->span_id = new_id;
                node->data->copy_to(
                    reinterpret_cast<Byte *>(cache_.data() + cache_.span_offset(new_id)));
                size_t current_index = cache_.get_dense_index(new_id);
                size_t target_index = old_index;
                if (target_index > cache_.span_count())
                    target_index = cache_.span_count();
                if (current_index != target_index)
                {
                    cache_.move_span_range(current_index, 1, target_index);
                }
            }
        }
        else
        {
            node->data = std::move(d);
            if (node->data)
            {
                size_t len = node->data->size();
                size_t new_id = cache_.allocate_span(len);
                node->data->span_id = new_id;
                node->data->copy_to(
                    reinterpret_cast<Byte *>(cache_.data() + cache_.span_offset(new_id)));
                size_t current_index = cache_.get_dense_index(new_id);
                size_t target_index = compute_target_dense_index(node);
                if (current_index != target_index)
                {
                    cache_.move_span_range(current_index, 1, target_index);
                }
            }
        }
    }

    // 交换两个节点的数据（仅支持大小相同的情况）
    void swap_data(TreeNode *a, TreeNode *b)
    {
        if (!a || !b || a == b)
            return;
        if (!a->data || !b->data)
            return;
        if (a->data->size() != b->data->size())
            return;

        size_t id_a = a->data->span_id;
        size_t id_b = b->data->span_id;

        // 交换缓冲区数据
        std::vector<Byte> temp(a->data->size());
        a->data->copy_to(temp.data());
        b->data->copy_to(
            reinterpret_cast<Byte *>(cache_.data() + cache_.span_offset(id_a)));
        std::memcpy(cache_.data() + cache_.span_offset(id_b), temp.data(), temp.size());

        // 交换数据对象指针
        std::swap(a->data, b->data);

        // 恢复 span_id，使数据对象与各自的 span 正确关联
        a->data->span_id = id_a;
        b->data->span_id = id_b;
    }

    void resize_data(TreeNode *node, size_t new_size)
    {
        if (!node || !node->data || node->data->span_id == Chache::INVALID_ID)
            return;
        cache_.resize_span(node->data->span_id, new_size);
    }

    void update_data(TreeNode *node)
    {
        if (!node || !node->data || node->data->span_id == Chache::INVALID_ID)
            return;
        size_t id = node->data->span_id;
        node->data->copy_to(
            reinterpret_cast<Byte *>(cache_.data() + cache_.span_offset(id)));
    }

    const Byte *data() const
    {
        return reinterpret_cast<const Byte *>(cache_.data());
    }
    size_t data_size() const
    {
        return cache_.size();
    }
    size_t capacity() const
    {
        return cache_.capacity();
    }

    // 获取节点数据的偏移（若无数据返回 SIZE_MAX）
    size_t get_node_offset(TreeNode *node) const
    {
        if (!node || !node->data || node->data->span_id == Chache::INVALID_ID)
            return SIZE_MAX;
        return cache_.span_offset(node->data->span_id);
    }

  private:
    void register_node_and_children(TreeNode *node)
    {
        if (!node)
            return;
        if (node->data)
        {
            size_t len = node->data->size();
            size_t id = cache_.allocate_span(len);
            node->data->span_id = id;
            node->data->copy_to(
                reinterpret_cast<Byte *>(cache_.data() + cache_.span_offset(id)));
        }
        for (auto &ch : node->children)
        {
            register_node_and_children(ch.get());
        }
    }

    void unregister_node_and_children(TreeNode *node)
    {
        if (!node)
            return;
        if (node->data)
        {
            node->data->span_id = Chache::INVALID_ID;
        }
        for (auto &ch : node->children)
        {
            unregister_node_and_children(ch.get());
        }
    }

    void clear_all()
    {
        if (root_)
        {
            clear_spans_recursive(root_.get());
            root_.reset();
        }
    }

    void clear_spans_recursive(TreeNode *node)
    {
        if (!node)
            return;
        if (node->data && node->data->span_id != Chache::INVALID_ID)
        {
            cache_.release_span(node->data->span_id);
            node->data->span_id = Chache::INVALID_ID;
        }
        for (auto &ch : node->children)
        {
            clear_spans_recursive(ch.get());
        }
    }

    size_t get_subtree_start_index(TreeNode *node) const
    {
        if (!node)
            return cache_.span_count();
        if (node->data && node->data->span_id != Chache::INVALID_ID)
        {
            return cache_.get_dense_index(node->data->span_id);
        }
        for (const auto &ch : node->children)
        {
            size_t idx = get_subtree_start_index(ch.get());
            if (idx != cache_.span_count())
                return idx;
        }
        return cache_.span_count();
    }

    size_t get_subtree_end_index(TreeNode *node) const
    {
        size_t start = get_subtree_start_index(node);
        size_t count = get_subtree_span_count(node);
        return start + count;
    }

    size_t get_subtree_span_count(TreeNode *node) const
    {
        if (!node)
            return 0;
        size_t count = 0;
        if (node->data && node->data->span_id != Chache::INVALID_ID)
        {
            count = 1;
        }
        for (const auto &ch : node->children)
        {
            count += get_subtree_span_count(ch.get());
        }
        return count;
    }

    size_t get_first_child_dense_index(TreeNode *parent) const
    {
        if (parent->children.empty())
        {
            if (parent->data && parent->data->span_id != Chache::INVALID_ID)
            {
                return cache_.get_dense_index(parent->data->span_id) + 1;
            }
            return get_subtree_start_index(parent);
        }
        return get_subtree_start_index(parent->children[0].get());
    }

    size_t compute_target_dense_index(TreeNode *node) const
    {
        if (!node->parent)
            return 0;
        TreeNode *parent = node->parent;
        size_t child_index = 0;
        for (size_t i = 0; i < parent->children.size(); ++i)
        {
            if (parent->children[i].get() == node)
            {
                child_index = i;
                break;
            }
        }
        if (child_index == 0)
        {
            if (parent->data && parent->data->span_id != Chache::INVALID_ID)
            {
                return cache_.get_dense_index(parent->data->span_id) + 1;
            }
            return get_first_child_dense_index(parent);
        }
        else
        {
            TreeNode *prev = parent->children[child_index - 1].get();
            return get_subtree_end_index(prev);
        }
    }

    std::unique_ptr<TreeNode> root_;
    Chache cache_;
};

// ========================= 测试数据类型 =========================
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
            std::memcpy(dst, value_.data(), value_.size());
    }
    std::unique_ptr<Data> clone() const override
    {
        return std::make_unique<StringData>(value_);
    }
    void set_value(const std::string &new_val)
    {
        value_ = new_val;
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
    void set_value(int v)
    {
        value_ = v;
    }
    int value() const
    {
        return value_;
    }

  private:
    int value_;
};

// ========================= 测试辅助 =========================
static std::unique_ptr<TreeNode> make_node(std::unique_ptr<Data> data = nullptr)
{
    auto n = std::make_unique<TreeNode>();
    if (data)
        n->data = std::move(data);
    return n;
}

static std::string tree_string(Tree &tree)
{
    const Byte *buf = tree.data();
    size_t size = tree.data_size();
    return std::string(reinterpret_cast<const char *>(buf), size);
}

static std::string dfs_expected_string(TreeNode *root)
{
    std::string s;
    if (!root)
        return s;
    if (root->data)
    {
        if (auto *sd = dynamic_cast<StringData *>(root->data.get()))
        {
            s += sd->value();
        }
        else if (auto *id = dynamic_cast<IntData *>(root->data.get()))
        {
            int v = id->value();
            s.append(reinterpret_cast<char *>(&v), sizeof(v));
        }
    }
    for (auto &ch : root->children)
    {
        s += dfs_expected_string(ch.get());
    }
    return s;
}

// ========================= 基础单元测试 =========================
void test_chache_allocate_and_release()
{
    Chache cache;
    size_t id1 = cache.allocate_span(5);
    size_t id2 = cache.allocate_span(3);
    assert(cache.span_count() == 2);
    assert(cache.span_offset(id1) == 0);
    assert(cache.span_offset(id2) == 5);
    assert(cache.size() == 8);
    assert(cache.span_valid(id1));
    assert(cache.span_valid(id2));

    cache.release_span(id1);
    assert(cache.span_count() == 1);
    assert(!cache.span_valid(id1));
    assert(cache.span_valid(id2));
    assert(cache.span_offset(id2) == 0);
    assert(cache.size() == 3);
    std::cout << "test_chache_allocate_and_release passed\n";
}

void test_chache_resize_grow_and_shrink()
{
    Chache cache;
    size_t id1 = cache.allocate_span(4);
    size_t id2 = cache.allocate_span(2);
    std::memcpy(cache.data() + cache.span_offset(id1), "ABCD", 4);
    std::memcpy(cache.data() + cache.span_offset(id2), "XY", 2);

    cache.resize_span(id1, 8);
    assert(cache.span_length(id1) == 8);
    assert(cache.span_offset(id2) == 8);
    assert(cache.size() == 10);
    assert(std::string(reinterpret_cast<const char *>(cache.data()), 4) == "ABCD");
    assert(std::string(reinterpret_cast<const char *>(cache.data() + 8), 2) == "XY");

    cache.resize_span(id1, 2);
    assert(cache.span_length(id1) == 2);
    assert(cache.span_offset(id2) == 2);
    assert(cache.size() == 4);
    assert(std::string(reinterpret_cast<const char *>(cache.data()), 2) == "AB");
    assert(std::string(reinterpret_cast<const char *>(cache.data() + 2), 2) == "XY");
    std::cout << "test_chache_resize_grow_and_shrink passed\n";
}

void test_chache_resize_trigger_capacity_growth()
{
    Chache cache(4);
    size_t id1 = cache.allocate_span(3);
    size_t id2 = cache.allocate_span(1);
    std::memcpy(cache.data() + cache.span_offset(id1), "abc", 3);
    std::memcpy(cache.data() + cache.span_offset(id2), "d", 1);

    cache.resize_span(id1, 6);
    assert(cache.span_length(id1) == 6);
    assert(cache.span_offset(id2) == 6);
    assert(cache.size() == 7);
    assert(std::string(reinterpret_cast<const char *>(cache.data()), 3) == "abc");
    assert(std::string(reinterpret_cast<const char *>(cache.data() + 6), 1) == "d");
    std::cout << "test_chache_resize_trigger_capacity_growth passed\n";
}

void test_chache_move_span_range()
{
    Chache cache;
    size_t id1 = cache.allocate_span(2);
    size_t id2 = cache.allocate_span(3);
    size_t id3 = cache.allocate_span(1);
    std::memcpy(cache.data() + cache.span_offset(id1), "ab", 2);
    std::memcpy(cache.data() + cache.span_offset(id2), "cde", 3);
    std::memcpy(cache.data() + cache.span_offset(id3), "f", 1);

    // 移动到末尾
    cache.move_span_range(0, 1, 3);
    assert(cache.get_dense_index(id2) == 0);
    assert(cache.get_dense_index(id3) == 1);
    assert(cache.get_dense_index(id1) == 2);
    assert(cache.span_offset(id2) == 0);
    assert(cache.span_offset(id3) == 3);
    assert(cache.span_offset(id1) == 4);
    assert(std::string(reinterpret_cast<const char *>(cache.data()), 6) == "cdefab");

    // 移动 id3 (索引1) 到开头
    cache.move_span_range(1, 1, 0);
    assert(cache.get_dense_index(id3) == 0);
    assert(cache.get_dense_index(id2) == 1);
    assert(cache.get_dense_index(id1) == 2);
    assert(cache.span_offset(id3) == 0);
    assert(cache.span_offset(id2) == 1);
    assert(cache.span_offset(id1) == 4);
    assert(std::string(reinterpret_cast<const char *>(cache.data()), 6) == "fcdeab");
    std::cout << "test_chache_move_span_range passed\n";
}

void test_chache_move_span_range_no_op()
{
    Chache cache;
    size_t id1 = cache.allocate_span(2);
    size_t id2 = cache.allocate_span(3);
    size_t id3 = cache.allocate_span(1);
    std::memcpy(cache.data() + cache.span_offset(id1), "ab", 2);
    std::memcpy(cache.data() + cache.span_offset(id2), "cde", 3);
    std::memcpy(cache.data() + cache.span_offset(id3), "f", 1);

    cache.move_span_range(0, 1, 1);
    assert(cache.get_dense_index(id1) == 0);
    assert(cache.get_dense_index(id2) == 1);
    assert(cache.get_dense_index(id3) == 2);
    assert(std::string(reinterpret_cast<const char *>(cache.data()), 6) == "abcdef");
    std::cout << "test_chache_move_span_range_no_op passed\n";
}

void test_chache_move_span_range_multiple_spans()
{
    Chache cache;
    size_t id1 = cache.allocate_span(1);
    size_t id2 = cache.allocate_span(2);
    size_t id3 = cache.allocate_span(3);
    size_t id4 = cache.allocate_span(4);
    // 数据：A BB CCC DDDD
    std::memcpy(cache.data() + cache.span_offset(id1), "A", 1);
    std::memcpy(cache.data() + cache.span_offset(id2), "BB", 2);
    std::memcpy(cache.data() + cache.span_offset(id3), "CCC", 3);
    std::memcpy(cache.data() + cache.span_offset(id4), "DDDD", 4);

    // 移动 id2+id3 (索引1, 2个span) 到末尾
    cache.move_span_range(1, 2, 4);
    assert(cache.get_dense_index(id1) == 0);
    assert(cache.get_dense_index(id4) == 1);
    assert(cache.get_dense_index(id2) == 2);
    assert(cache.get_dense_index(id3) == 3);
    assert(std::string(reinterpret_cast<const char *>(cache.data()), 10) == "ADDDDBBCCC");
    std::cout << "test_chache_move_span_range_multiple_spans passed\n";
}

void test_chache_remove_span_range()
{
    Chache cache;
    size_t id1 = cache.allocate_span(2);
    size_t id2 = cache.allocate_span(3);
    size_t id3 = cache.allocate_span(1);
    std::memcpy(cache.data() + cache.span_offset(id1), "ab", 2);
    std::memcpy(cache.data() + cache.span_offset(id2), "cde", 3);
    std::memcpy(cache.data() + cache.span_offset(id3), "f", 1);

    cache.remove_span_range(1, 1);
    assert(cache.span_count() == 2);
    assert(cache.span_valid(id1));
    assert(!cache.span_valid(id2));
    assert(cache.span_valid(id3));
    assert(cache.span_offset(id3) == 2);
    assert(std::string(reinterpret_cast<const char *>(cache.data()), 3) == "abf");
    std::cout << "test_chache_remove_span_range passed\n";
}

void test_chache_remove_span_range_multiple()
{
    Chache cache;
    size_t id1 = cache.allocate_span(1);
    size_t id2 = cache.allocate_span(2);
    size_t id3 = cache.allocate_span(3);
    size_t id4 = cache.allocate_span(4);
    std::memcpy(cache.data() + cache.span_offset(id1), "A", 1);
    std::memcpy(cache.data() + cache.span_offset(id2), "BB", 2);
    std::memcpy(cache.data() + cache.span_offset(id3), "CCC", 3);
    std::memcpy(cache.data() + cache.span_offset(id4), "DDDD", 4);

    // 删除 id2 和 id3 (索引1,2)
    cache.remove_span_range(1, 2);
    assert(cache.span_count() == 2);
    assert(cache.span_valid(id1));
    assert(!cache.span_valid(id2));
    assert(!cache.span_valid(id3));
    assert(cache.span_valid(id4));
    assert(cache.span_offset(id1) == 0);
    assert(cache.span_offset(id4) == 1);
    assert(cache.size() == 5);
    assert(std::string(reinterpret_cast<const char *>(cache.data()), 5) == "ADDDD");
    std::cout << "test_chache_remove_span_range_multiple passed\n";
}

void test_tree_basic_operations()
{
    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("R")));
    auto *root = tree.root();
    auto *a = tree.add_child(root, make_node(std::make_unique<StringData>("A")));
    tree.add_child(root, make_node(std::make_unique<StringData>("B")));
    tree.add_child(a, make_node(std::make_unique<StringData>("A1")));
    assert(tree_string(tree) == "RAA1B");

    auto *c = tree.insert_child(root, 1, make_node(std::make_unique<StringData>("C")));
    assert(c != nullptr);
    assert(tree_string(tree) == "RAA1CB");

    bool ok = tree.move_subtree(a, c, 0);
    assert(ok);
    assert(tree_string(tree) == "RCAA1B");

    auto removed = tree.detach(a);
    assert(removed != nullptr);
    assert(tree_string(tree) == "RCB");
    std::cout << "test_tree_basic_operations passed\n";
}

void test_tree_set_data_and_swap()
{
    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("root")));
    auto *r = tree.root();
    auto *a = tree.add_child(r, make_node(std::make_unique<StringData>("AAA")));
    auto *b = tree.add_child(r, make_node(std::make_unique<StringData>("BBB")));

    tree.set_data(a, std::make_unique<StringData>("CCC"));
    assert(tree_string(tree) == "rootCCCBBB");

    tree.set_data(b, nullptr);
    assert(tree_string(tree) == "rootCCC");

    auto *c = tree.add_child(r, make_node(std::make_unique<StringData>("DDD")));
    tree.swap_data(a, c);
    assert(tree_string(tree) == "rootDDDCCC");
    std::cout << "test_tree_set_data_and_swap passed\n";
}

void test_tree_resize_and_update()
{
    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("R")));
    auto *r = tree.root();
    auto *a = tree.add_child(r, make_node(std::make_unique<StringData>("A")));
    tree.add_child(r, make_node(std::make_unique<StringData>("B")));

    auto *a_data = dynamic_cast<StringData *>(a->data.get());
    a_data->set_value("AAAAA");
    tree.resize_data(a, a_data->size());
    tree.update_data(a);
    assert(tree_string(tree) == "RAAAAAB");
    std::cout << "test_tree_resize_and_update passed\n";
}

void test_tree_move_subtree_multiple_cases()
{
    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("R")));
    auto *r = tree.root();
    auto *a = tree.add_child(r, make_node(std::make_unique<StringData>("A")));
    auto *b = tree.add_child(r, make_node(std::make_unique<StringData>("B")));
    auto *c = tree.add_child(r, make_node(std::make_unique<StringData>("C")));
    tree.add_child(a, make_node(std::make_unique<StringData>("A1")));
    tree.add_child(a, make_node(std::make_unique<StringData>("A2")));
    tree.add_child(b, make_node(std::make_unique<StringData>("B1")));

    bool ok = tree.move_subtree(a, b);
    assert(ok);
    assert(tree_string(tree) == "RBB1AA1A2C");

    ok = tree.move_subtree(a, c, 0);
    assert(ok);
    assert(tree_string(tree) == "RBB1CAA1A2");

    ok = tree.move_subtree(b, r, 0);
    assert(ok);
    assert(tree_string(tree) == "RBB1CAA1A2");
    std::cout << "test_tree_move_subtree_multiple_cases passed\n";
}

void test_tree_set_data_with_different_sizes()
{
    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("R")));
    auto *r = tree.root();
    auto *a = tree.add_child(r, make_node(std::make_unique<StringData>("A")));
    auto *b = tree.add_child(r, make_node(std::make_unique<StringData>("BB")));
    tree.add_child(r, make_node(std::make_unique<StringData>("CCC")));

    tree.set_data(a, std::make_unique<StringData>("AAAAA"));
    assert(tree_string(tree) == "RAAAAABBCCC");

    tree.set_data(b, std::make_unique<StringData>("B"));
    assert(tree_string(tree) == "RAAAAABCCC");
    std::cout << "test_tree_set_data_with_different_sizes passed\n";
}

// ========================= 原有测试用例 =========================
void test_basic_dfs_order()
{
    Tree tree;
    tree.set_root(make_node());
    auto *r = tree.root();
    tree.add_child(r, make_node(std::make_unique<StringData>("A")));
    auto *b = tree.add_child(r, make_node(std::make_unique<StringData>("B")));
    tree.add_child(r, make_node(std::make_unique<StringData>("C")));
    tree.add_child(b, make_node(std::make_unique<StringData>("D")));
    assert(tree_string(tree) == "ABDC");
    std::cout << "test_basic_dfs_order passed\n";
}

void test_initial_layout_and_offsets()
{
    Tree tree;
    tree.set_root(make_node(std::make_unique<IntData>(1)));
    auto *r = tree.root();
    auto *a = tree.add_child(r, make_node(std::make_unique<StringData>("Hello")));
    tree.add_child(r, make_node(std::make_unique<IntData>(42)));
    tree.add_child(a, make_node(std::make_unique<StringData>("World")));

    const Byte *p = tree.data();
    assert(*(reinterpret_cast<const int *>(p)) == 1);
    assert(std::string(reinterpret_cast<const char *>(p + sizeof(int)), 5) == "Hello");
    assert(std::string(reinterpret_cast<const char *>(p + sizeof(int) + 5), 5) ==
           "World");
    assert(*(reinterpret_cast<const int *>(p + sizeof(int) + 5 + 5)) == 42);
    assert(tree.get_node_offset(r) == 0);
    assert(tree.get_node_offset(a) == sizeof(int));
    std::cout << "test_initial_layout_and_offsets passed\n";
}

void test_content_update_same_size()
{
    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("root")));
    auto *r = tree.root();
    auto *a = tree.add_child(r, make_node(std::make_unique<StringData>("AAA")));
    tree.add_child(r, make_node(std::make_unique<StringData>("BBB")));
    assert(tree_string(tree) == "rootAAABBB");

    auto *a_data = dynamic_cast<StringData *>(a->data.get());
    a_data->set_value("CCC");
    tree.update_data(a);
    assert(tree_string(tree) == "rootCCCBBB");
    std::cout << "test_content_update_same_size passed\n";
}

void test_content_update_size_change()
{
    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("root")));
    auto *r = tree.root();
    auto *a = tree.add_child(r, make_node(std::make_unique<StringData>("AAA")));
    tree.add_child(r, make_node(std::make_unique<StringData>("BBB")));
    assert(tree_string(tree) == "rootAAABBB");

    auto *a_data = dynamic_cast<StringData *>(a->data.get());
    a_data->set_value("AAAAA");
    tree.resize_data(a, a_data->size());
    tree.update_data(a);
    assert(tree_string(tree) == "rootAAAAABBB");
    std::cout << "test_content_update_size_change passed\n";
}

void test_swap_data()
{
    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("root")));
    auto *r = tree.root();
    auto *a = tree.add_child(r, make_node(std::make_unique<StringData>("AAA")));
    auto *b = tree.add_child(r, make_node(std::make_unique<StringData>("BBB")));
    tree.swap_data(a, b);
    assert(tree_string(tree) == "rootBBBAAA");
    std::cout << "test_swap_data passed\n";
}

void test_move_subtree()
{
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
    std::cout << "test_move_subtree passed\n";
}

void test_remove_node()
{
    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("R")));
    auto *r = tree.root();
    tree.add_child(r, make_node(std::make_unique<StringData>("A")));
    auto *b = tree.add_child(r, make_node(std::make_unique<StringData>("B")));
    tree.add_child(b, make_node(std::make_unique<StringData>("B1")));
    assert(tree_string(tree) == "RABB1");

    auto removed = tree.remove_child(r, 0);
    assert(removed);
    assert(tree_string(tree) == "RBB1");
    std::cout << "test_remove_node passed\n";
}

void test_empty_data_nodes()
{
    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("X")));
    auto *r = tree.root();
    tree.add_child(r, make_node());
    tree.add_child(r, make_node(std::make_unique<StringData>("Y")));
    assert(tree_string(tree) == "XY");
    std::cout << "test_empty_data_nodes passed\n";
}

void test_capacity_growth()
{
    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>(std::string(1000, 'a'))));
    auto *r = tree.root();
    tree.add_child(r, make_node(std::make_unique<StringData>(std::string(10, 'b'))));
    assert(tree.data_size() == 1010);
    assert(tree.capacity() >= 1010);
    std::cout << "test_capacity_growth passed\n";
}

void test_polymorphism_mixed_types()
{
    Tree tree;
    tree.set_root(make_node(std::make_unique<IntData>(7)));
    auto *r = tree.root();
    auto *a = tree.add_child(r, make_node(std::make_unique<StringData>("mix")));
    tree.add_child(r, make_node(std::make_unique<IntData>(99)));
    tree.add_child(a, make_node(std::make_unique<StringData>("ed")));

    const Byte *p = tree.data();
    int int1, int2;
    std::memcpy(&int1, p, sizeof(int));
    std::memcpy(&int2, p + sizeof(int) + 3 + 2, sizeof(int));
    assert(int1 == 7);
    assert(int2 == 99);
    assert(std::string(reinterpret_cast<const char *>(p + sizeof(int)), 3) == "mix");
    assert(std::string(reinterpret_cast<const char *>(p + sizeof(int) + 3), 2) == "ed");
    std::cout << "test_polymorphism_mixed_types passed\n";
}

void test_set_data_replacement()
{
    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("R")));
    auto *r = tree.root();
    tree.add_child(r, make_node(std::make_unique<StringData>("A")));
    auto *b = tree.add_child(r, make_node(std::make_unique<StringData>("B")));
    tree.set_data(b, std::make_unique<StringData>("XYZ"));
    assert(tree_string(tree) == "RAXYZ");
    std::cout << "test_set_data_replacement passed\n";
}

void test_large_tree_random_operations()
{
    std::mt19937 rng(42);
    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("R")));
    TreeNode *root = tree.root();
    std::vector<TreeNode *> all_nodes;
    all_nodes.push_back(root);

    for (int i = 0; i < 100; ++i)
    {
        size_t parent_idx = rng() % all_nodes.size();
        TreeNode *parent = all_nodes[parent_idx];
        std::string val = "N" + std::to_string(i);
        auto node = tree.add_child(parent, make_node(std::make_unique<StringData>(val)));
        all_nodes.push_back(node);
    }

    for (int i = 0; i < 20; ++i)
    {
        if (all_nodes.size() <= 1)
            break;
        size_t idx = 1 + (rng() % (all_nodes.size() - 1));
        TreeNode *node = all_nodes[idx];
        auto removed = tree.detach(node);
        std::vector<TreeNode *> to_remove;
        std::function<void(TreeNode *)> collect = [&](TreeNode *n) {
            to_remove.push_back(n);
            for (auto &ch : n->children)
                collect(ch.get());
        };
        collect(removed.get());
        for (auto *n : to_remove)
        {
            auto it = std::find(all_nodes.begin(), all_nodes.end(), n);
            if (it != all_nodes.end())
                all_nodes.erase(it);
        }
    }

    std::string expected = dfs_expected_string(root);
    assert(tree_string(tree) == expected);
    std::cout << "test_large_tree_random_operations passed\n";
}

void test_complex_state_transitions()
{
    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("root")));
    TreeNode *root = tree.root();

    auto verify = [&]() {
        std::string expected = dfs_expected_string(root);
        std::string actual = tree_string(tree);
        if (actual != expected)
        {
            std::cerr << "Verify failed:\n";
            std::cerr << "  Expected: [" << expected << "]\n";
            std::cerr << "  Actual:   [" << actual << "]\n";
        }
        assert(actual == expected);

        size_t expected_offset = 0;
        std::function<void(TreeNode *)> dfs_check = [&](TreeNode *node) {
            if (!node)
                return;
            if (node->data)
            {
                assert(tree.get_node_offset(node) == expected_offset);
                expected_offset += node->data->size();
            }
            for (auto &ch : node->children)
            {
                dfs_check(ch.get());
            }
        };
        dfs_check(root);
        assert(expected_offset == tree.data_size());
    };

    verify();

    auto *a = tree.add_child(root, make_node(std::make_unique<StringData>("A")));
    auto *b = tree.add_child(root, make_node(std::make_unique<StringData>("B")));
    auto *c = tree.add_child(root, make_node(std::make_unique<StringData>("C")));
    tree.add_child(a, make_node(std::make_unique<StringData>("A1")));
    tree.add_child(a, make_node(std::make_unique<StringData>("A2")));
    tree.add_child(b, make_node(std::make_unique<StringData>("B1")));
    verify();

    auto x = make_node(std::make_unique<StringData>("X"));
    auto x1 = make_node(std::make_unique<StringData>("X1"));
    x->children.push_back(std::move(x1));
    tree.insert_child(root, 2, std::move(x));
    assert(tree_string(tree) == "rootAA1A2BB1XX1C");
    verify();

    TreeNode *a_ptr = root->children[0].get();
    bool ok = tree.move_subtree(a_ptr, root->children[3].get(), 0);
    assert(ok);
    assert(tree_string(tree) == "rootBB1XX1CAA1A2");
    verify();

    TreeNode *a_new = root->children[2]->children[0].get();
    auto *a_data = dynamic_cast<StringData *>(a_new->data.get());
    a_data->set_value("AAAAA");
    tree.resize_data(a_new, a_data->size());
    tree.update_data(a_new);
    assert(tree_string(tree) == "rootBB1XX1CAAAAAA1A2");
    verify();

    TreeNode *b_node = root->children[0].get();
    tree.set_data(b_node, nullptr);
    assert(tree_string(tree) == "rootB1XX1CAAAAAA1A2");
    verify();

    TreeNode *b1_node = root->children[0]->children[0].get();
    TreeNode *a1_node = root->children[2]->children[0]->children[0].get();
    tree.swap_data(b1_node, a1_node);
    assert(tree_string(tree) == "rootA1XX1CAAAAAB1A2");
    verify();

    TreeNode *x_to_remove = root->children[1].get();
    auto removed = tree.detach(x_to_remove);
    assert(removed != nullptr);
    assert(tree_string(tree) == "rootA1CAAAAAB1A2");
    verify();

    std::cout << "test_complex_state_transitions passed\n";
}

void test_chache_id_reuse()
{
    Chache cache;
    size_t id1 = cache.allocate_span(4);
    cache.release_span(id1);
    size_t id2 = cache.allocate_span(3);
    assert(id1 == id2); // ID 被复用
    assert(cache.span_valid(id2));
    assert(cache.span_offset(id2) == 0);
}
void test_tree_set_root_multiple()
{
    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("first")));
    tree.set_root(make_node(std::make_unique<StringData>("second")));
    assert(tree_string(tree) == "second");
    assert(tree.data_size() == 6);
}
void test_tree_empty_data_subtree_operations()
{
    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("R")));
    auto *r = tree.root();
    auto *empty = tree.add_child(r, make_node()); // 空数据节点
    auto *child = tree.add_child(empty, make_node(std::make_unique<StringData>("X")));
    assert(tree_string(tree) == "RX");
    tree.move_subtree(empty, r, 0);
    assert(tree_string(tree) == "RX");
    tree.detach(empty);
    assert(tree_string(tree) == "R");
}
void test_chache_extreme_growth()
{
    Chache cache(4);
    std::vector<size_t> ids;
    for (int i = 0; i < 20; ++i)
    {
        size_t len = (i % 5) + 1;
        ids.push_back(cache.allocate_span(len));
        std::memset(cache.data() + cache.span_offset(ids.back()), 'A' + i, len);
    }
    // 验证总大小
    size_t expected_size = 0;
    for (size_t i = 0; i < ids.size(); ++i)
        expected_size += (i % 5) + 1;
    assert(cache.size() == expected_size);
    // 验证数据未损坏（抽查）
    assert(*(cache.data() + cache.span_offset(ids[10])) == 'A' + 10);
}
void test_tree_random_mixed_operations_stress()
{
    std::mt19937 rng(12345);
    Tree tree;
    tree.set_root(make_node(std::make_unique<StringData>("R")));
    TreeNode *root = tree.root();
    std::vector<TreeNode *> nodes = {root};

    auto verify = [&]() {
        std::string expected = dfs_expected_string(root);
        assert(tree_string(tree) == expected);
        // 可选：校验每个节点的偏移
    };

    for (int step = 0; step < 500; ++step)
    {
        int op = rng() % 5;
        if (op == 0)
        { // 添加子节点
            size_t parent_idx = rng() % nodes.size();
            TreeNode *parent = nodes[parent_idx];
            std::string val = "N" + std::to_string(step);
            auto *node =
                tree.add_child(parent, make_node(std::make_unique<StringData>(val)));
            nodes.push_back(node);
        }
        else if (op == 1 && nodes.size() > 1)
        { // 删除随机节点（非根）
            size_t idx = 1 + rng() % (nodes.size() - 1);
            TreeNode *node = nodes[idx];
            auto removed = tree.detach(node);
            // 从 nodes 中移除该子树
            std::function<void(TreeNode *)> remove_subtree = [&](TreeNode *n) {
                auto it = std::find(nodes.begin(), nodes.end(), n);
                if (it != nodes.end())
                    nodes.erase(it);
                for (auto &ch : n->children)
                    remove_subtree(ch.get());
            };
            remove_subtree(removed.get());
        }
        else if (op == 2 && nodes.size() > 2)
        { // 移动子树
            size_t src_idx = 1 + rng() % (nodes.size() - 1);
            size_t dst_idx = rng() % nodes.size();
            TreeNode *src = nodes[src_idx];
            TreeNode *dst = nodes[dst_idx];
            if (src != dst && src->parent != dst && dst != src)
            {
                tree.move_subtree(src, dst, rng() % (dst->children.size() + 1));
            }
        }
        else if (op == 3 && nodes.size() > 1)
        { // 修改数据大小并更新
            size_t idx = 1 + rng() % (nodes.size() - 1);
            TreeNode *node = nodes[idx];
            if (node->data)
            {
                auto *sd = dynamic_cast<StringData *>(node->data.get());
                if (sd)
                {
                    std::string new_val = "Data" + std::to_string(step);
                    sd->set_value(new_val);
                    tree.resize_data(node, new_val.size());
                    tree.update_data(node);
                }
            }
        }
        else if (op == 4 && nodes.size() > 1)
        { // 交换数据
            size_t idx1 = 1 + rng() % (nodes.size() - 1);
            size_t idx2 = 1 + rng() % (nodes.size() - 1);
            if (idx1 != idx2)
            {
                tree.swap_data(nodes[idx1], nodes[idx2]);
            }
        }
        verify();
    }
}
void test_grow_buffer_out_of_bounds_read()
{
    Chache cache(4);                     // 初始容量 4
    size_t id1 = cache.allocate_span(4); // 分配 4 字节，恰好填满，next_offset_ = 4
    size_t id2 = cache.allocate_span(1); // 再分配 1 字节，next_offset_ 变为 5 > capacity_

    // 这里已经触发了 grow_buffer(5)，旧缓冲区只有 4 字节，
    // 但 grow_buffer 中 memcpy 了 5 字节 → 越界读
    (void)id1;
    (void)id2;
}
// ========================= 主函数 =========================
int main()
{
    {
        test_chache_id_reuse();
        test_tree_set_root_multiple();
        test_tree_empty_data_subtree_operations();
        test_chache_extreme_growth();
        test_tree_random_mixed_operations_stress();
        test_grow_buffer_out_of_bounds_read();
    }
    // 运行新增的基础单元测试
    test_chache_allocate_and_release();
    test_chache_resize_grow_and_shrink();
    test_chache_resize_trigger_capacity_growth();
    test_chache_move_span_range();
    test_chache_move_span_range_no_op();
    test_chache_move_span_range_multiple_spans();
    test_chache_remove_span_range();
    test_chache_remove_span_range_multiple();

    test_tree_basic_operations();
    test_tree_set_data_and_swap();
    test_tree_resize_and_update();
    test_tree_move_subtree_multiple_cases();
    test_tree_set_data_with_different_sizes();

    // 运行原有测试
    test_basic_dfs_order();
    test_initial_layout_and_offsets();
    test_content_update_same_size();
    test_content_update_size_change();
    test_swap_data();
    test_move_subtree();
    test_remove_node();
    test_empty_data_nodes();
    test_capacity_growth();
    test_polymorphism_mixed_types();
    test_set_data_replacement();
    test_large_tree_random_operations();
    test_complex_state_transitions();

    std::cout << "\nAll tests passed successfully.\n";
    return 0;
}