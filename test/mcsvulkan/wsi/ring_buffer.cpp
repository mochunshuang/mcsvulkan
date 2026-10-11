
// NOLINTBEGIN
#include <algorithm> // std::ranges::fold_left
#include <cassert>
#include <chrono>
#include <cstddef>
#include <inplace_vector>
#include <iterator>
#include <optional>
#include <print>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace mcs::vulkan
{
    // ═══════════════════════════════════════════════════════════════
    // ring_buffer<T, N> — 固定容量窗口
    //
    // 语义：保存最近 N 个同类型元素。满了之后新元素覆盖最旧的。
    // 底层：std::inplace_vector<T, N>，零堆分配。
    // 对外暴露为 forward_range，按逻辑序遍历（0 = 最旧）。
    // ═══════════════════════════════════════════════════════════════

    template <class T, std::size_t N>
    struct ring_buffer
    {
      public:
        static_assert(N > 0, "ring_buffer 容量必须大于 0");

        using value_type = T;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;

        constexpr ring_buffer() noexcept = default;

        // ── 状态 ────────────────────────────────────────────────
        [[nodiscard]] constexpr bool empty() const noexcept
        {
            return size_ == 0;
        }
        [[nodiscard]] constexpr bool full() const noexcept
        {
            return size_ == N;
        }
        [[nodiscard]] constexpr size_type size() const noexcept
        {
            return size_;
        }
        [[nodiscard]] static constexpr size_type capacity() noexcept
        {
            return N;
        }

        // ── 写入：满时覆盖最旧，返回被覆盖的元素（可选信息，不强制处理）──
        constexpr std::optional<T> push(T value) noexcept
        {
            std::optional<T> evicted;
            if (size_ == N)
            {
                evicted = std::move(data_[head_]);
                data_[head_] = std::move(value);
                head_ = (head_ + 1) % N;
            }
            else
            {
                data_.push_back(std::move(value));
                ++size_;
            }
            return evicted;
        }

        // ── 索引访问（逻辑序：0 = 最旧）──────────────────────────
        [[nodiscard]] constexpr T &operator[](size_type i) noexcept
        {
            return data_[(head_ + i) % N];
        }
        [[nodiscard]] constexpr const T &operator[](size_type i) const noexcept
        {
            return data_[(head_ + i) % N];
        }

        [[nodiscard]] constexpr T &front() noexcept
        {
            return (*this)[0];
        }
        [[nodiscard]] constexpr const T &front() const noexcept
        {
            return (*this)[0];
        }
        [[nodiscard]] constexpr T &back() noexcept
        {
            return (*this)[size_ - 1];
        }
        [[nodiscard]] constexpr const T &back() const noexcept
        {
            return (*this)[size_ - 1];
        }

        // ── 迭代器：逻辑序，从最旧到最新 ────────────────────────
        template <bool IsConst>
        struct iterator_base
        {
          public:
            using iterator_concept = std::forward_iterator_tag;
            using iterator_category = std::forward_iterator_tag;
            using value_type = T;
            using difference_type = std::ptrdiff_t;
            using pointer = std::conditional_t<IsConst, const T *, T *>;
            using reference = std::conditional_t<IsConst, const T &, T &>;
            using buffer_pointer =
                std::conditional_t<IsConst, const ring_buffer *, ring_buffer *>;

            constexpr iterator_base() noexcept = default;
            constexpr iterator_base(buffer_pointer buf, size_type idx) noexcept
                : buf_(buf), idx_(idx)
            {
            }

            template <bool OtherConst>
            constexpr explicit iterator_base(
                const iterator_base<OtherConst> &other) noexcept
                requires(IsConst && !OtherConst)
                : buf_(other.buf_), idx_(other.idx_)
            {
            }

            constexpr reference operator*() const noexcept
            {
                return (*buf_)[idx_];
            }
            constexpr pointer operator->() const noexcept
            {
                return &(*buf_)[idx_];
            }

            constexpr iterator_base &operator++() noexcept
            {
                ++idx_;
                return *this;
            }
            constexpr iterator_base operator++(int) noexcept
            {
                auto c = *this;
                ++idx_;
                return c;
            }

            friend constexpr bool operator==(const iterator_base &a,
                                             const iterator_base &b) noexcept
            {
                return a.buf_ == b.buf_ && a.idx_ == b.idx_;
            }

          private:
            template <bool>
            friend struct iterator_base;

            buffer_pointer buf_{nullptr};
            size_type idx_{0};
        };

        using iterator = iterator_base<false>;
        using const_iterator = iterator_base<true>;

        [[nodiscard]] constexpr iterator begin() noexcept
        {
            return {this, 0};
        }
        [[nodiscard]] constexpr iterator end() noexcept
        {
            return {this, size_};
        }
        [[nodiscard]] constexpr const_iterator begin() const noexcept
        {
            return {this, 0};
        }
        [[nodiscard]] constexpr const_iterator end() const noexcept
        {
            return {this, size_};
        }
        [[nodiscard]] constexpr const_iterator cbegin() const noexcept
        {
            return begin();
        }
        [[nodiscard]] constexpr const_iterator cend() const noexcept
        {
            return end();
        }

        // ── 清空 ────────────────────────────────────────────────
        constexpr void clear() noexcept
        {
            data_.clear();
            head_ = 0;
            size_ = 0;
        }

      private:
        std::inplace_vector<T, N> data_{};
        size_type head_{0};
        size_type size_{0};
    };

} // namespace mcs::vulkan

// ═══════════════════════════════════════════════════════════════════
// 测试
// ═══════════════════════════════════════════════════════════════════

namespace rb = mcs::vulkan;

// ── 基础语义 ─────────────────────────────────────────────────────

void test_empty()
{
    rb::ring_buffer<int, 8> w;
    assert(w.empty());
    assert(!w.full());
    assert(w.size() == 0);
    assert(w.capacity() == 8);
    assert(std::ranges::distance(w) == 0);
}

void test_append_before_full()
{
    rb::ring_buffer<int, 4> w;
    for (int i = 0; i < 4; ++i)
        assert(!w.push(i).has_value());

    assert(w.full());
    assert(w.size() == 4);
    assert(w.front() == 0);
    assert(w.back() == 3);
}

void test_overwrite_when_full()
{
    rb::ring_buffer<int, 4> w;
    for (int i = 0; i < 4; ++i)
        w.push(i);

    auto evicted = w.push(4);
    assert(evicted.has_value());
    assert(*evicted == 0);

    assert(w.front() == 1);
    assert(w.back() == 4);
    assert(w.size() == 4);
}

// ── 逻辑序访问 ────────────────────────────────────────────────────

void test_logical_order_after_wrap()
{
    rb::ring_buffer<int, 4> w;
    for (int i = 0; i < 6; ++i)
        w.push(i); // 2,3,4,5

    assert(w[0] == 2);
    assert(w[1] == 3);
    assert(w[2] == 4);
    assert(w[3] == 5);
}

void test_range_for()
{
    rb::ring_buffer<int, 4> w;
    for (int i = 0; i < 6; ++i)
        w.push(i);

    std::vector<int> got;
    for (int x : w)
        got.push_back(x);

    assert((got == std::vector{2, 3, 4, 5}));
}

void test_const_iteration()
{
    rb::ring_buffer<int, 4> w;
    w.push(1);
    w.push(2);
    w.push(3);

    const auto &cw = w;
    std::vector<int> got(cw.begin(), cw.end());
    assert((got == std::vector{1, 2, 3}));
}

// ── ranges 管道 ──────────────────────────────────────────────────

void test_filter_pipeline()
{
    rb::ring_buffer<int, 8> w;
    for (int i = 0; i < 8; ++i)
        w.push(i);

    auto evens = w | std::views::filter([](int x) { return x % 2 == 0; });
    std::vector<int> got(evens.begin(), evens.end());
    assert((got == std::vector{0, 2, 4, 6}));
}

void test_transform_pipeline()
{
    rb::ring_buffer<int, 4> w;
    for (int i = 0; i < 4; ++i)
        w.push(i);

    auto squares = w | std::views::transform([](int x) { return x * x; });
    assert(std::ranges::fold_left(squares, 0, std::plus{}) == 14);
}

void test_filter_transform_chain()
{
    rb::ring_buffer<int, 8> w;
    for (int i = 0; i < 8; ++i)
        w.push(i);

    auto result = w | std::views::filter([](int x) { return x % 2 == 1; }) |
                  std::views::transform([](int x) { return x * 10; });

    std::vector<int> got(result.begin(), result.end());
    assert((got == std::vector{10, 30, 50, 70}));
}

// ── 时间窗口过滤 ──────────────────────────────────────────────────

struct sample
{
    double x, y;
    std::chrono::steady_clock::time_point t;
};

void test_time_window_filter()
{
    using clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;

    rb::ring_buffer<sample, 16> w;

    auto base = clock::time_point{};
    for (int i = 0; i < 10; ++i)
        w.push({double(i), 0.0, base + i * 10ms});

    // 最近 50ms：base+40ms .. base+90ms
    auto cutoff = w.back().t - 50ms;
    auto recent =
        w | std::views::filter([cutoff](const sample &s) { return s.t >= cutoff; });

    std::vector<double> xs;
    for (const auto &s : recent)
        xs.push_back(s.x);

    assert(xs.size() == 6);
    assert(xs.front() == 4.0);
    assert(xs.back() == 9.0);
}

// ── 非平凡类型 ───────────────────────────────────────────────────

void test_non_trivial_type()
{
    rb::ring_buffer<std::string, 3> w;
    w.push("a");
    w.push("bb");
    w.push("ccc");

    auto evicted = w.push("dddd");
    assert(evicted.has_value());
    assert(*evicted == "a");

    std::vector<std::string> got(w.begin(), w.end());
    assert((got == std::vector<std::string>{"bb", "ccc", "dddd"}));
}

// ── 边界 ────────────────────────────────────────────────────────

void test_capacity_one()
{
    rb::ring_buffer<int, 1> w;
    assert(w.push(1).has_value() == false);
    assert(w.front() == 1);

    auto evicted = w.push(2);
    assert(evicted.has_value() && *evicted == 1);
    assert(w.front() == 2);
    assert(w.size() == 1);
}

void test_power_of_two_capacity()
{
    rb::ring_buffer<int, 64> w;
    for (int i = 0; i < 200; ++i)
        w.push(i);

    assert(w.size() == 64);
    assert(w.front() == 136);
    assert(w.back() == 199);
}

// ── 清空 ────────────────────────────────────────────────────────

void test_clear()
{
    rb::ring_buffer<int, 4> w;
    for (int i = 0; i < 4; ++i)
        w.push(i);
    w.clear();

    assert(w.empty());
    assert(w.size() == 0);
    assert(std::ranges::distance(w) == 0);

    w.push(42);
    assert(w.front() == 42);
    assert(w.size() == 1);
}

// ── constexpr 可用性 ─────────────────────────────────────────────

constexpr bool test_constexpr()
{
    rb::ring_buffer<int, 4> w;
    w.push(1);
    w.push(2);
    w.push(3);
    w.push(4);
    w.push(5);

    return w.front() == 2 && w.back() == 5 && w.size() == 4;
}

static_assert(test_constexpr(), "ring_buffer 应支持 constexpr");

// ═══════════════════════════════════════════════════════════════════

int main()
{
    test_empty();
    test_append_before_full();
    test_overwrite_when_full();
    test_logical_order_after_wrap();
    test_range_for();
    test_const_iteration();
    test_filter_pipeline();
    test_transform_pipeline();
    test_filter_transform_chain();
    test_time_window_filter();
    test_non_trivial_type();
    test_capacity_one();
    test_power_of_two_capacity();
    test_clear();
    std::println("done");
    return 0;
} // NOLINTEND