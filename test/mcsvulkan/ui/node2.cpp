#include <cassert>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>
#include <bit>
#include <algorithm>

struct Chache
{
    static constexpr size_t INVALID_ID = SIZE_MAX;
    static constexpr size_t INVALID_DENSE = SIZE_MAX;

    struct Span
    {
        constexpr Span(Chache *ptr, size_t id) noexcept : ptr_{ptr}, id_{id} {}

        Span(const Span &) = delete;
        Span &operator=(const Span &) = delete;

        Span(Span &&other) noexcept : ptr_{other.ptr_}, id_{other.id_}
        {
            other.ptr_ = nullptr;
            other.id_ = INVALID_ID;
        }

        Span &operator=(Span &&other) noexcept
        {
            if (this != &other)
            {
                release();
                ptr_ = other.ptr_;
                id_ = other.id_;
                other.ptr_ = nullptr;
                other.id_ = INVALID_ID;
            }
            return *this;
        }

        ~Span()
        {
            release();
        }

        void release()
        {
            if (ptr_ && id_ != INVALID_ID)
            {
                ptr_->release(id_);
                ptr_ = nullptr;
                id_ = INVALID_ID;
            }
        }

        size_t offset() const noexcept
        {
            assert(valid());
            return ptr_->dense_[ptr_->sparse_[id_]].offset;
        }
        size_t length() const noexcept
        {
            assert(valid());
            return ptr_->dense_[ptr_->sparse_[id_]].length;
        }
        size_t size() const noexcept
        {
            assert(valid());
            return ptr_->dense_[ptr_->sparse_[id_]].used;
        }

        void reset()
        {
            assert(valid());
            ptr_->dense_[ptr_->sparse_[id_]].used = 0;
        }

        void append(const void *data, size_t len)
        {
            assert(valid());
            auto &info = ptr_->dense_[ptr_->sparse_[id_]];
            assert(ptr_->data_.get() != nullptr);
            assert(info.used + len <= info.length);
            std::memcpy(ptr_->data_.get() + info.offset + info.used, data, len);
            info.used += len;
        }

        void resize(size_t new_length)
        {
            assert(valid());
            ptr_->resize_span(id_, new_length);
        }
        bool valid() const noexcept
        {
            return ptr_ != nullptr && id_ != INVALID_ID && id_ < ptr_->sparse_.size() &&
                   ptr_->sparse_[id_] != INVALID_DENSE;
        }

      private:
        Chache *ptr_;
        size_t id_;
    };

    // 构造函数优化
    Chache(size_t capacity = 4)
        : capacity_{std::bit_ceil(capacity)},
          data_{std::make_unique_for_overwrite<unsigned char[]>(capacity_)},
          next_offset_{0}, next_id_{0}
    {
        sparse_.resize(capacity_, INVALID_DENSE);
    }

    Span allocation(size_t length)
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
        next_offset_ += length;
        if (next_offset_ > capacity_)
        {
            grow_buffer(next_offset_);
        }
        return Span{this, id};
    }

    void release(size_t id)
    {
        assert(id < sparse_.size() && sparse_[id] != INVALID_DENSE);
        size_t index = sparse_[id];
        SpanInfo &info = dense_[index];

        // 搬移尾部数据，保持紧凑
        size_t tail_start = info.offset + info.length;
        size_t tail_size = next_offset_ - tail_start;
        if (tail_size > 0)
        {
            std::memmove(data_.get() + info.offset, data_.get() + tail_start, tail_size);
        }
        next_offset_ -= info.length;

        // 更新后续 span 的偏移
        for (size_t i = index + 1; i < dense_.size(); ++i)
        {
            dense_[i].offset -= info.length;
        }

        // swap-pop
        size_t last = dense_.size() - 1;
        if (index != last)
        {
            dense_[index] = std::move(dense_[last]);
            sparse_[dense_[index].id] = index;
        }
        dense_.pop_back();
        sparse_[id] = INVALID_DENSE;
        free_ids_.push_back(id);
    }

    unsigned char *data()
    {
        return data_.get();
    }
    const unsigned char *data() const
    {
        return data_.get();
    }
    size_t size() const
    {
        return next_offset_;
    }
    size_t capacity() const
    {
        return capacity_;
    }
    size_t span_count() const
    {
        return dense_.size();
    }

  private:
    struct SpanInfo
    {
        size_t id;
        size_t offset;
        size_t length;
        size_t used;
    };

    void resize_span(size_t id, size_t new_length)
    {
        assert(id < sparse_.size() && sparse_[id] != INVALID_DENSE);
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

    void grow_buffer(size_t required)
    {
        size_t new_cap = std::bit_ceil(std::max(required, capacity_ * 2));
        auto new_data = std::make_unique_for_overwrite<unsigned char[]>(new_cap);
        if (data_ && next_offset_ > 0)
        {
            std::memcpy(new_data.get(), data_.get(), next_offset_);
        }
        data_ = std::move(new_data);
        capacity_ = new_cap;
    }

    void grow_buffer_and_move(size_t index, size_t new_length, size_t required)
    {
        size_t new_cap = std::bit_ceil(std::max(required, capacity_ * 2));
        auto new_data = std::make_unique_for_overwrite<unsigned char[]>(new_cap);
        SpanInfo &info = dense_[index];

        if (info.offset > 0)
        {
            std::memcpy(new_data.get(), data_.get(), info.offset);
        }

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

    std::unique_ptr<unsigned char[]> data_;
    size_t capacity_;
    size_t next_offset_;
    size_t next_id_;
    std::vector<SpanInfo> dense_;  // 物理顺序
    std::vector<size_t> sparse_;   // id -> dense 下标
    std::vector<size_t> free_ids_; // 可重用 id
};

// ---------------- 测试 ----------------
int main()

try
{
    Chache cache(8);

    // 1. 分配三个 span
    auto s1 = cache.allocation(3);
    auto s2 = cache.allocation(2);
    auto s3 = cache.allocation(4);

    s1.append("ABC", 3);
    s2.append("DE", 2);
    s3.append("FGHI", 4);

    assert(cache.size() == 9);
    assert(cache.span_count() == 3);
    assert(std::memcmp(cache.data(), "ABCDEFGHI", 9) == 0);
    std::cout << "Test 1 passed: initial allocation and writing.\n";

    assert(s1.offset() == 0 && s1.length() == 3);
    assert(s2.offset() == 3 && s2.length() == 2);
    assert(s3.offset() == 5 && s3.length() == 4);

    // 2. 调整 s1 大小：3 -> 5
    s1.resize(5);
    s1.reset();
    s1.append("ABCDE", 5);

    assert(cache.size() == 11);
    assert(std::memcmp(cache.data(), "ABCDEDEFGHI", 11) == 0);
    assert(s1.offset() == 0 && s1.length() == 5);
    assert(s2.offset() == 5 && s2.length() == 2);
    assert(s3.offset() == 7 && s3.length() == 4);
    std::cout << "Test 2 passed: span growth (in-place).\n";

    // 3. 调整 s3 大小：4 -> 2
    s3.resize(2);
    assert(cache.size() == 9);
    assert(std::memcmp(cache.data(), "ABCDEDEFG", 9) == 0);
    assert(s3.offset() == 7 && s3.length() == 2);
    std::cout << "Test 3 passed: span shrink.\n";

    // 4. 显式释放 s2
    s2.release();
    assert(cache.span_count() == 2);
    assert(cache.size() == 7);
    assert(std::memcmp(cache.data(), "ABCDEFG", 7) == 0);
    assert(s3.offset() == 5 && s3.length() == 2);
    std::cout << "Test 4 passed: explicit release (O(1) removal).\n";

    // 5. 触发扩容：分配大 span
    auto s4 = cache.allocation(100);
    assert(cache.capacity() >= cache.size());
    assert(s4.offset() == 7);
    assert(cache.size() == 107);
    s4.append("X", 1);
    assert(cache.size() == 107);
    assert(*(cache.data() + s4.offset()) == 'X');
    std::cout << "Test 5 passed: allocation triggers buffer growth.\n";

    // 6. 析构自动释放
    {
        auto temp = cache.allocation(10);
        assert(cache.span_count() == 4); // s1, s3, s4, temp
        assert(cache.size() == 117);
    }
    assert(cache.span_count() == 3);
    assert(cache.size() == 107);
    std::cout << "Test 6 passed: RAII destructor release.\n";

    // 7. 移动语义
    auto s5 = cache.allocation(20);
    auto s6 = std::move(s5);
    s6.append("Y", 1);
    assert(cache.size() == 127);
    std::cout << "Test 7 passed: move semantics.\n";

    std::cout << "\nAll tests passed successfully.\n";
    return 0;
}
catch (const std::exception &e)
{
    std::cerr << "Exception: " << e.what() << '\n';
    return 1;
}
