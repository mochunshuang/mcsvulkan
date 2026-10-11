#pragma once

#include <inplace_vector>

namespace mcs::vulkan
{
    // ═══════════════════════════════════════════════════════════════
    // ring_buffer<T, N> — 固定容量窗口
    //
    // 语义：保存最近 N 个同类型元素。满了之后新元素覆盖最旧的。
    // 底层：std::inplace_vector<T, N>，零堆分配。
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
