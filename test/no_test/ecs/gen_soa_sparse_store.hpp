#pragma once
#include "soa_memory.hpp"
#include <_mingw_off_t.h>
#include <cassert>
#include <optional>
#include <vector>

template <typename T, template <typename> class Alloc = std::allocator,
          template <typename, template <typename> class> class Make = soa_memory>
struct gen_soa_sparse_store
{
  private:
    using soa_type = Make<T, Alloc>;

    soa_type data_;
    std::vector<size_type> used_;
    std::vector<size_type> used_idx_; // NOLINT 加速删除
    std::vector<size_type> free_;

    //NOTE: noexcept 因为扩容的时候已经保证内存足够不可能扩容
    // NOTE: 和末尾的最后一个元素交换。然后size--。 反正无序
    // NOTE: 更新值 以及 更新索引即可
    constexpr void remove_from_used(size_type used_idx) noexcept // NOLINT
    {
        assert(used_idx < used_.size());

        auto end = used_.size() - 1;
        size_type change_id = used_[end];

        used_[used_idx] = change_id;     // move
        used_idx_[change_id] = used_idx; // update
        used_.pop_back();                // pop used_idx
    }
    constexpr void recycle_to_free(size_type id) noexcept // NOLINT
    {
        data_.destroy_at(id);
        free_.push_back(id);
    }

  public:
    using value_type = T;
    using id_type = size_type;
    static constexpr size_type invalid_id = ~0; // NOLINT

    // NOTE: 初始化指定大小，才能 [] 访问
    constexpr gen_soa_sparse_store() : gen_soa_sparse_store(4) {}
    constexpr explicit gen_soa_sparse_store(size_type size)
        : data_(size), used_{}, used_idx_(size, invalid_id), free_{}
    {
        used_.reserve(size);
        free_.reserve(size);
        for (size_type i = size; i > 0; --i)
            free_.push_back(i - 1);
    }

    [[nodiscard]] constexpr size_type free_size() const noexcept // NOLINT
    {
        return free_.size();
    }
    [[nodiscard]] constexpr size_type size() const noexcept
    {
        return used_.size();
    }
    [[nodiscard]] auto capacity() const noexcept
    {
        return data_.capacity();
    }

    [[nodiscard]] constexpr std::optional<size_type> allocate() noexcept
    {
        if (free_.empty())
            return std::nullopt;
        auto id = free_.back();
        free_.pop_back();

        //NOTE: 为了保证 删除是常量复杂度
        //NOTE: 实现 id -> used_idx_[id] -> used_idx 加速查找 id 在 used中的索引
        assert(id < used_idx_.size());
        used_idx_[id] = used_.size();
        used_.push_back(id);
        return id; // 返回物理 id
    }

    constexpr auto release(size_type id)
    {
        assert(used_idx_[id] != invalid_id);
        assert(used_idx_[id] < used_.size());

        auto used_id = used_idx_[id];
        used_idx_[id] = invalid_id;
        remove_from_used(used_id);
        recycle_to_free(id);
    }

    constexpr void grow(size_type new_cap)
    {
        auto old_cap = capacity();
        if (new_cap <= old_cap)
            return;

        // 1. 为所有字段分配新内存
        typename soa_type::Pointers new_pointers{};
        template for (constexpr auto I :
                      std::views::indices(soa_type::ptr_members.size()))
        {
            using field_type = typename std::remove_reference_t<
                decltype(data_.pointers_.[:soa_type::ptr_members[I]:])>::value_type;
            using reb_alloc = typename std::allocator_traits<
                typename soa_type::allocator_type>::template rebind_alloc<field_type>;
            reb_alloc ra(data_.alloc_);
            new_pointers.[:soa_type::ptr_members[I]:] = soa_member_pointer<field_type>(
                                                          ra.allocate(new_cap));
        }

        // 2. 搬迁所有存活元素（只搬 used_ 中记录的槽位）
        for (size_type i = 0; i < used_.size(); ++i)
        {
            size_type slot = used_[i];
            template for (constexpr auto I :
                          std::views::indices(soa_type::ptr_members.size()))
            {
                auto &old_field = data_.pointers_.[:soa_type::ptr_members[I]:];
                auto &new_field = new_pointers.[:soa_type::ptr_members[I]:];
                std::construct_at(new_field.data() + slot, std::move(old_field[slot]));
                std::destroy_at(old_field.data() + slot);
            }
        }

        // 3. 释放旧字段内存
        template for (constexpr auto I :
                      std::views::indices(soa_type::ptr_members.size()))
        {
            using field_type = typename std::remove_reference_t<
                decltype(data_.pointers_.[:soa_type::ptr_members[I]:])>::value_type;
            using reb_alloc = typename std::allocator_traits<
                typename soa_type::allocator_type>::template rebind_alloc<field_type>;
            reb_alloc ra(data_.alloc_);
            ra.deallocate(data_.pointers_.[:soa_type::ptr_members[I]:].data(), old_cap);
        }

        // 4. 更新 状态
        data_.capacity_ = new_cap;
        data_.pointers_ = std::move(new_pointers);

        // 5. 扩展 used_idx_ / free
        used_idx_.resize(new_cap, invalid_id);
        for (size_type i = new_cap; i > old_cap; --i)
            free_.push_back(i - 1);
    }

    constexpr void destroy_at(size_type id) noexcept // NOLINT
    {
        data_.destroy_at(id);
    }
    template <typename... Args>
    constexpr void construct_at(size_type idx, Args &&...args) noexcept( // NOLINT
        std::is_nothrow_constructible_v<T, Args...>)
    {
        data_.construct_at(idx, std::forward<Args>(args)...);
    }
    constexpr void construct_at(size_type idx, // NOLINT
                                T t) noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        data_.construct_at(idx, std::move(t));
    }

    template <static_string... name>
        requires(sizeof...(name) > 0)
    constexpr auto view(this auto &&self) noexcept
    {
        auto &&data = std::forward<decltype(self)>(self).data_;
        return std::views::zip(
            self.used_ | std::views::transform([&data](size_type slot) -> decltype(auto) {
                return data.template get<name>(slot);
            })...);
    }
    constexpr auto view(this auto &&self) noexcept
    {
        return self.used_ | std::views::transform([&self](size_type slot) -> auto {
                   return std::forward_like<decltype(self)>(self.data_[slot]);
               });
    }
    [[nodiscard]] const std::vector<size_type> &used_slots() const noexcept // NOLINT
    {
        return used_;
    }
    template <size_type I>
    decltype(auto) constexpr get(this auto &&self, size_type idx) noexcept
    {
        return std::forward_like<decltype(self)>(self.data_.template get<I>(idx));
    }

    template <static_string name>
    constexpr decltype(auto) get(this auto &&self, size_type idx) noexcept
    {
        return std::forward_like<decltype(self)>(self.data_.template get<name>(idx));
    }

    constexpr auto operator[](this auto &&self, size_type id) noexcept
    {
        return std::forward_like<decltype(self)>(self.data_[id]);
    }
    constexpr ~gen_soa_sparse_store() noexcept
    {
        // 销毁所有存活元素
        for (size_type used_id = 0; used_id < used_.size(); ++used_id)
        {
            size_type id = used_[used_id];
            data_.destroy_at(id);
        }
    }
    constexpr gen_soa_sparse_store(const gen_soa_sparse_store &o)
        : data_(o.data_), used_(o.used_), used_idx_(o.used_idx_), free_(o.free_)
    {
        for (size_type slot : used_)
        {
            template for (constexpr auto I :
                          std::views::indices(soa_type::ptr_members.size()))
            {
                data_.template construct_field<I>(slot, o.data_.template get<I>(slot));
            }
        }
    }
    constexpr gen_soa_sparse_store &operator=(const gen_soa_sparse_store &o)
    {
        if (this != &o)
        {
            auto tmp(o);
            *this = std::move(tmp);
        }
        return *this;
    }
    constexpr gen_soa_sparse_store(gen_soa_sparse_store &&other) noexcept
        : data_(std::move(other.data_)), used_(std::move(other.used_)),
          used_idx_(std::move(other.used_idx_)), free_(std::move(other.free_))
    {
    }
    constexpr gen_soa_sparse_store &operator=(gen_soa_sparse_store &&other) noexcept
    {
        if (this != &other)
        {
            // 先销毁当前存活元素
            for (size_type id : used_)
                data_.destroy_at(id);

            // 移动数据成员
            data_ = std::move(other.data_);
            used_ = std::move(other.used_);
            used_idx_ = std::move(other.used_idx_);
            free_ = std::move(other.free_);
        }
        return *this;
    }

    template <typename Store>
    struct store_iterator
    {
        Store *store;  // NOLINT
        size_type idx; // NOLINT

        constexpr auto operator*() const noexcept(noexcept(store->operator[](idx)))
        {
            return store->operator[](idx); // 返回临时 tuple
        }
        constexpr store_iterator &operator++() noexcept
        {
            ++idx;
            return *this;
        }
        constexpr store_iterator operator++(int) noexcept
        {
            auto tmp = *this;
            ++idx;
            return tmp;
        }
        constexpr bool operator==(const store_iterator &) const = default;
    };

    // 在 gen_soa_sparse_store 类内添加：
    using iterator = store_iterator<gen_soa_sparse_store>;
    using const_iterator = store_iterator<const gen_soa_sparse_store>;

    constexpr iterator begin() noexcept
    {
        return {this, 0};
    }
    constexpr const_iterator begin() const noexcept
    {
        return {this, 0};
    }
    constexpr iterator end() noexcept
    {
        return {this, size()};
    }
    constexpr const_iterator end() const noexcept
    {
        return {this, size()};
    }

    // NOTE: view()  是零时的，内部有引用解引用会错误
    // constexpr auto begin(this auto &&self) noexcept
    // {
    //     return self.view().begin();
    // }
    // constexpr auto end(this auto &&self) noexcept
    // {
    //     return self.view().end();
    // }

    decltype(auto) read_only(this const auto &self) // NOLINT
    {
        return self;
    }
};
