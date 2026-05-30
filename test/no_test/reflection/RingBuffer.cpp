#include <cassert>
#include <iostream>
#include <memory>
#include <new>
#include <utility>
#include <cstddef>
#include <type_traits>

template <typename T, typename Alloc = std::allocator<T>>
class RingBuffer
{
  public:
    using allocator_type = Alloc;
    using value_type = T;

    explicit RingBuffer(size_t capacity, const Alloc &alloc = Alloc())
        : capacity_(capacity), alloc_(alloc), data_(nullptr)
    {
        if (capacity_ > 0)
        {
            data_ = alloc_.allocate(capacity_);
        }
    }

    ~RingBuffer()
    {
        destroy_elements();
        alloc_.deallocate(data_, capacity_);
    }

    RingBuffer(const RingBuffer &) = delete;
    RingBuffer &operator=(const RingBuffer &) = delete;

    RingBuffer(RingBuffer &&other) noexcept
        : capacity_(other.capacity_), head_(other.head_), tail_(other.tail_),
          count_(other.count_), alloc_(std::move(other.alloc_)), data_(other.data_)
    {
        other.capacity_ = 0;
        other.head_ = other.tail_ = other.count_ = 0;
        other.data_ = nullptr;
    }

    RingBuffer &operator=(RingBuffer &&other) noexcept
    {
        if (this != &other)
        {
            destroy_elements();
            alloc_.deallocate(data_, capacity_);

            alloc_ = std::move(other.alloc_);
            capacity_ = other.capacity_;
            head_ = other.head_;
            tail_ = other.tail_;
            count_ = other.count_;
            data_ = other.data_;

            other.capacity_ = 0;
            other.head_ = other.tail_ = other.count_ = 0;
            other.data_ = nullptr;
        }
        return *this;
    }

    bool empty() const noexcept
    {
        return count_ == 0;
    }
    bool full() const noexcept
    {
        return count_ == capacity_;
    }
    size_t size() const noexcept
    {
        return count_;
    }
    size_t capacity() const noexcept
    {
        return capacity_;
    }

    bool push(const T &value)
    {
        if (full())
            return false;
        std::allocator_traits<Alloc>::construct(alloc_, &data_[tail_], value);
        tail_ = (tail_ + 1) % capacity_;
        ++count_;
        return true;
    }

    bool push(T &&value)
    {
        if (full())
            return false;
        std::allocator_traits<Alloc>::construct(alloc_, &data_[tail_], std::move(value));
        tail_ = (tail_ + 1) % capacity_;
        ++count_;
        return true;
    }

    bool pop(T &out)
    {
        if (empty())
            return false;
        out = std::move(data_[head_]);
        std::allocator_traits<Alloc>::destroy(alloc_, &data_[head_]);
        head_ = (head_ + 1) % capacity_;
        --count_;
        return true;
    }

    T &front() noexcept
    {
        return data_[head_];
    }
    const T &front() const noexcept
    {
        return data_[head_];
    }

    T &back() noexcept
    {
        size_t idx = (tail_ == 0) ? capacity_ - 1 : tail_ - 1;
        return data_[idx];
    }
    const T &back() const noexcept
    {
        size_t idx = (tail_ == 0) ? capacity_ - 1 : tail_ - 1;
        return data_[idx];
    }

  private:
    void destroy_elements() noexcept
    {
        if (count_ == 0)
            return;
        for (size_t i = 0, idx = head_; i < count_; ++i)
        {
            std::allocator_traits<Alloc>::destroy(alloc_, &data_[idx]);
            idx = (idx + 1) % capacity_;
        }
    }

    size_t capacity_ = 0;
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;
    Alloc alloc_;
    T *data_;
};

// ===== 测试辅助对象 =====
struct Tracker
{
    inline static int alive = 0;
    int value;

    Tracker(int v = 0) : value(v)
    {
        ++alive;
    }
    Tracker(const Tracker &o) : value(o.value)
    {
        ++alive;
    }
    Tracker(Tracker &&o) noexcept : value(o.value)
    {
        o.value = -1;
        ++alive;
    }

    Tracker &operator=(const Tracker &o)
    {
        if (this != &o)
            value = o.value;
        return *this;
    }

    Tracker &operator=(Tracker &&o) noexcept
    {
        if (this != &o)
        {
            value = o.value;
            o.value = -1;
        }
        return *this;
    }

    ~Tracker()
    {
        --alive;
    }

    bool operator==(const Tracker &o) const
    {
        return value == o.value;
    }
};

// ===== 测试用例 =====
int main()
{
    // 1. 基本 push/pop (int)
    {
        RingBuffer<int> buf(3);
        assert(buf.empty());
        assert(buf.capacity() == 3);
        assert(buf.size() == 0);

        assert(buf.push(1));
        assert(buf.push(2));
        assert(buf.push(3));
        assert(buf.full());
        assert(!buf.push(4));

        assert(buf.front() == 1);
        assert(buf.back() == 3);

        int val;
        assert(buf.pop(val) && val == 1);
        assert(buf.size() == 2);
        assert(!buf.full());

        assert(buf.pop(val) && val == 2);
        assert(buf.pop(val) && val == 3);
        assert(buf.empty());
        assert(!buf.pop(val));
    }

    // 2. 环绕行为
    {
        RingBuffer<int> buf(3);
        buf.push(10);
        buf.push(20);
        buf.push(30);
        int v;
        buf.pop(v);
        assert(v == 10);
        buf.push(40);
        assert(buf.front() == 20);
        assert(buf.back() == 40);
        buf.pop(v);
        assert(v == 20);
        buf.pop(v);
        assert(v == 30);
        buf.pop(v);
        assert(v == 40);
        assert(buf.empty());
    }

    // 3. std::string
    {
        RingBuffer<std::string> buf(2);
        std::string s1 = "hello";
        buf.push(std::move(s1));
        assert(s1.empty());
        buf.push("world");
        std::string out;
        buf.pop(out);
        assert(out == "hello");
        buf.pop(out);
        assert(out == "world");
    }

    // 4. Tracker 生命周期
    {
        const int initial_alive = Tracker::alive;
        {
            RingBuffer<Tracker> buf(2);
            buf.push(Tracker(100));
            buf.push(Tracker(200));
            assert(Tracker::alive == initial_alive + 2); // buf 中两个对象

            Tracker t; // 这里 alive 增加 1
            buf.pop(t);
            assert(t.value == 100);
            // pop 后：buf 中剩余一个对象 + t 本身 = 2，所以 alive = initial_alive + 2
            assert(Tracker::alive == initial_alive + 2); // 修正断言
        }
        // buf 析构，销毁剩余对象，alive 回到 initial_alive + 1
        assert(Tracker::alive == initial_alive); // t 析构，回到初始
    }

    // 5. 移动构造与移动赋值
    {
        RingBuffer<int> buf1(4);
        buf1.push(1);
        buf1.push(2);
        buf1.push(3);
        RingBuffer<int> buf2(std::move(buf1));
        assert(buf1.empty());
        assert(buf2.size() == 3);
        assert(buf2.front() == 1);

        RingBuffer<int> buf3(2);
        buf3 = std::move(buf2);
        assert(buf2.empty());
        assert(buf3.size() == 3);
        int v;
        buf3.pop(v);
        assert(v == 1);
        buf3.pop(v);
        assert(v == 2);
        buf3.pop(v);
        assert(v == 3);
    }

    // 6. 自定义分配器
    {
        std::allocator<int> alloc;
        RingBuffer<int, std::allocator<int>> buf(5, alloc);
        buf.push(42);
        assert(buf.front() == 42);
    }

    // 7. 容量为 0
    {
        RingBuffer<int> buf(0);
        assert(buf.empty());
        assert(buf.full());
        assert(buf.size() == 0);
        assert(!buf.push(1));
        int v;
        assert(!buf.pop(v));
    }

    std::cout << "All tests passed successfully.\n";
    return 0;
}