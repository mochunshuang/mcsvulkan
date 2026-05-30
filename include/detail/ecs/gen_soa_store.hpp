#pragma once
#include "soa_memory.hpp"
#include <cassert>
#include <optional>
#include <vector>

namespace mcs::vulkan::ecs
{
    template <typename T, template <typename> class Alloc = std::allocator,
              template <typename, template <typename> class> class Make = soa_memory>
    struct gen_soa_store
    {
      private:
        using soa_type = Make<T, Alloc>;

        soa_type data_;
        std::vector<size_type> used_;
        std::vector<size_type> free_;

        // 与末尾交换并弹出（无序删除），不维护 used_idx_
        constexpr void remove_from_used(size_type used_idx) noexcept // NOLINT
        {
            assert(used_idx < used_.size());
            if (used_idx != used_.size() - 1)
                used_[used_idx] = used_.back();
            used_.pop_back();
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

        // 默认构造函数（非 explicit，解决 world 初始化警告）a
        constexpr gen_soa_store() : gen_soa_store(4) {}
        constexpr explicit gen_soa_store(size_type size) : data_(size), used_{}, free_{}
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
            used_.push_back(id);
            return id;
        }

        // 释放指定槽位（物理 id）
        constexpr void release(size_type id)
        {
            // 在 used_ 中查找 id 的索引
            auto it = std::ranges::find(used_, id);
            assert(it != used_.end());
            size_type used_idx = std::distance(used_.begin(), it);
            remove_from_used(used_idx);
            recycle_to_free(id);
        }

        constexpr void grow(size_type new_cap)
        {
            auto old_cap = capacity();
            if (new_cap <= old_cap)
                return;

            // 1. 分配新内存
            typename soa_type::Pointers new_pointers{};
            template for (constexpr auto I :
                          std::views::indices(soa_type::ptr_members.size()))
            {
                using field_type = typename std::remove_reference_t<
                    decltype(data_.pointers_.[:soa_type::ptr_members[I]:])>::value_type;
                using reb_alloc = typename std::allocator_traits<
                    typename soa_type::allocator_type>::template rebind_alloc<field_type>;
                reb_alloc ra(data_.alloc_);
                new_pointers
                    .[:soa_type::ptr_members[I]:] = soa_member_pointer<field_type>(
                                                      ra.allocate(new_cap));
            }

            // 2. 搬迁存活元素
            for (size_type i = 0; i < used_.size(); ++i)
            {
                size_type slot = used_[i];
                template for (constexpr auto I :
                              std::views::indices(soa_type::ptr_members.size()))
                {
                    auto &old_field = data_.pointers_.[:soa_type::ptr_members[I]:];
                    auto &new_field = new_pointers.[:soa_type::ptr_members[I]:];
                    std::construct_at(new_field.data() + slot,
                                      std::move(old_field[slot]));
                    std::destroy_at(old_field.data() + slot);
                }
            }

            // 3. 释放旧内存
            template for (constexpr auto I :
                          std::views::indices(soa_type::ptr_members.size()))
            {
                using field_type = typename std::remove_reference_t<
                    decltype(data_.pointers_.[:soa_type::ptr_members[I]:])>::value_type;
                using reb_alloc = typename std::allocator_traits<
                    typename soa_type::allocator_type>::template rebind_alloc<field_type>;
                reb_alloc ra(data_.alloc_);
                ra.deallocate(data_.pointers_.[:soa_type::ptr_members[I]:].data(),
                                                                          old_cap);
            }

            // 4. 更新状态
            data_.capacity_ = new_cap;
            data_.pointers_ = std::move(new_pointers);

            // 5. 扩展 free_ 列表
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
                self.used_ |
                std::views::transform([&data](size_type slot) -> decltype(auto) {
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
            return std::forward_like<decltype(self)>(self.data_).template get<I>(idx);
        }

        template <static_string name>
        constexpr decltype(auto) get(this auto &&self, size_type idx) noexcept
        {
            return std::forward_like<decltype(self)>(self.data_).template get<name>(idx);
        }

        constexpr auto operator[](this auto &&self, size_type id) noexcept
        {
            return std::forward_like<decltype(self)>(self.data_[id]);
        }

        constexpr ~gen_soa_store() noexcept // NOLINT
        {
            for (size_type id : used_)
                data_.destroy_at(id);
        }

        // ---------- 复制语义 ----------
        constexpr gen_soa_store(const gen_soa_store &o)
            : data_(o.data_), used_(o.used_), free_(o.free_)
        {
            for (size_type slot : used_)
            {
                template for (constexpr auto I :
                              std::views::indices(soa_type::ptr_members.size()))
                {
                    data_.template construct_field<I>(slot,
                                                      o.data_.template get<I>(slot));
                }
            }
        }

        constexpr gen_soa_store &operator=(const gen_soa_store &o)
        {
            if (this != &o)
            {
                auto tmp(o);
                *this = std::move(tmp);
            }
            return *this;
        }

        // ---------- 移动语义 ----------
        constexpr gen_soa_store(gen_soa_store &&other) noexcept
            : data_(std::move(other.data_)), used_(std::move(other.used_)),
              free_(std::move(other.free_))
        {
        }

        constexpr gen_soa_store &operator=(gen_soa_store &&other) noexcept
        {
            if (this != &other)
            {
                for (size_type id : used_)
                    data_.destroy_at(id);

                data_ = std::move(other.data_);
                used_ = std::move(other.used_);
                free_ = std::move(other.free_);
            }
            return *this;
        }

        // ---------- 迭代器 ----------
        template <typename Store>
        struct store_iterator
        {
            Store *store;  // NOLINT
            size_type idx; // NOLINT

            constexpr auto operator*() const noexcept(noexcept(store->operator[](idx)))
            {
                return store->operator[](idx);
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

        using iterator = store_iterator<gen_soa_store>;
        using const_iterator = store_iterator<const gen_soa_store>;

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

        decltype(auto) read_only(this const auto &self) // NOLINT
        {
            return self;
        }
    };
}; // namespace mcs::vulkan::ecs
