#pragma once
#include "soa_memory.hpp"
#include <cassert>
#include <optional>
#include <vector>
#include <ranges>

namespace mcs::vulkan::ecs
{
    // 稀疏集 SoA 向量：提供稳定外部实体令牌，O(1) 删除，紧凑物理布局
    template <typename T, template <typename> class Alloc = std::allocator,
              template <typename, template <typename> class> class Make = soa_memory>
    struct gen_soa_vector
    {
      private:
        using soa_type = Make<T, Alloc>;
        soa_type data_;

        // 密集数组：按物理槽位顺序存储的存活实体 ID
        std::vector<size_type> dense_;
        // 稀疏数组：实体 ID -> 密集下标（物理槽位）
        std::vector<size_type> sparse_;
        // 可重用的实体 ID 池
        std::vector<size_type> free_entities_;
        // 下一个新实体 ID（当 free_entities_ 为空时分配）
        size_type next_entity_id_ = 0;

        static constexpr size_type invalid_dense = ~size_type(0);

        [[nodiscard]] constexpr bool alive(size_type entity) const noexcept
        {
            return entity < sparse_.size() && sparse_[entity] != invalid_dense;
        }

        [[nodiscard]] constexpr size_type get_slot(size_type entity) const noexcept
        {
            assert(alive(entity));
            return sparse_[entity];
        }

      public:
        using value_type = T;
        using id_type = size_type;
        static constexpr size_type invalid_id = ~0;

        // 构造：默认容量 4
        constexpr gen_soa_vector() : gen_soa_vector(0) {}
        constexpr explicit gen_soa_vector(size_type cap)
            : data_(cap), sparse_(cap, invalid_dense)
        {
            dense_.reserve(cap);
        }

        [[nodiscard]] constexpr size_type capacity() const noexcept
        {
            return data_.capacity();
        }
        [[nodiscard]] constexpr size_type size() const noexcept
        {
            return dense_.size();
        }
        [[nodiscard]] constexpr size_type free_size() const noexcept
        {
            return capacity() - size();
        }

        // 分配实体：返回稳定的外部 ID，不构造对象
        [[nodiscard]] constexpr std::optional<size_type> allocate() noexcept
        {
            if (dense_.size() >= data_.capacity())
                return std::nullopt;

            size_type entity;
            if (!free_entities_.empty())
            {
                entity = free_entities_.back();
                free_entities_.pop_back();
            }
            else
            {
                entity = next_entity_id_++;
                if (entity >= sparse_.size())
                    sparse_.resize(entity + 1, invalid_dense);
            }

            size_type slot = dense_.size();
            dense_.push_back(entity);
            sparse_[entity] = slot;
            return entity;
        }

        // 释放实体：O(1) swap-pop，保持密集紧凑
        constexpr void release(size_type entity) noexcept
        {
            assert(alive(entity));

            size_type slot = sparse_[entity];
            size_type last = dense_.size() - 1;

            if (slot != last)
            {
                size_type moved_entity = dense_[last];

                // 交换 slot 和 last 的数据（双方都是已构造的）
                template for (constexpr auto I :
                              std::views::indices(soa_type::ptr_members.size()))
                {
                    auto &field = data_.pointers_.[:soa_type::ptr_members[I]:];
                    using std::swap;
                    swap(field[slot], field[last]); // 安全：两个对象都活着
                }

                // 更新映射
                dense_[slot] = moved_entity;
                sparse_[moved_entity] = slot;
            }

            // 现在 entity 位于 last（若交换过）或原本就在 last，销毁它
            data_.destroy_at(last);
            dense_.pop_back();
            sparse_[entity] = invalid_dense;
            free_entities_.push_back(entity);
        }

        // 在实体上构造组件（参数包版本，避免反射访问成员）
        template <typename... Args>
        constexpr void construct_at(size_type entity, Args &&...args) noexcept(
            std::is_nothrow_constructible_v<T, Args...>)
        {
            size_type slot = get_slot(entity);
            data_.construct_at(slot, std::forward<Args>(args)...);
        }
        constexpr void construct_at(size_type entity,
                                    T t) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            size_type slot = get_slot(entity);
            data_.construct_at(slot, std::move(t));
        }

        constexpr void destroy_at(size_type entity) noexcept
        {
            size_type slot = get_slot(entity);
            data_.destroy_at(slot);
        }

        // ---------- 字段访问（通过实体 ID）----------
        template <size_type I>
        decltype(auto) constexpr get(this auto &&self, size_type entity) noexcept
        {
            size_type slot = self.get_slot(entity);
            return std::forward_like<decltype(self)>(self.data_).template get<I>(slot);
        }

        template <static_string name>
        constexpr decltype(auto) get(this auto &&self, size_type entity) noexcept
        {
            size_type slot = self.get_slot(entity);
            return std::forward_like<decltype(self)>(self.data_).template get<name>(slot);
        }

        // 返回 soa_memory::operator[] 的临时 bind_result，字段是引用
        constexpr auto operator[](this auto &&self, size_type entity) noexcept
        {
            size_type slot = self.get_slot(entity);
            return std::forward_like<decltype(self)>(self.data_[slot]);
        }

        // ---------- 遍历视图 ----------
        // 多字段视图（返回 tuple，支持结构化绑定）
        template <static_string... name>
            requires(sizeof...(name) > 0)
        constexpr auto view(this auto &&self) noexcept
        {
            auto &&data = std::forward<decltype(self)>(self).data_;
            return std::views::iota(size_type(0), self.size()) |
                   std::views::transform([&data](size_type slot) {
                       return std::forward_as_tuple(data.template get<name>(slot)...);
                   });
        }

        // 完整实体视图（返回 bind_result 临时对象）
        constexpr auto view(this auto &&self) noexcept
        {
            auto &&data = std::forward<decltype(self)>(self).data_;
            return std::views::iota(size_type(0), self.size()) |
                   std::views::transform([&data](size_type slot) {
                       return std::forward_like<decltype(self)>(data[slot]);
                   });
        }

        [[nodiscard]] const std::vector<size_type> &used_slots() const noexcept
        {
            return dense_;
        }

        // ---------- 扩容 ----------
        constexpr void reserve(size_type new_cap)
        {
            auto old_cap = capacity();
            if (new_cap <= old_cap)
                return;

            // 0. 申请内存
            typename soa_type::Pointers new_pointers{};
            template for (constexpr auto I :
                          std::views::indices(soa_type::ptr_members.size()))
            {
                using field_type = typename std::remove_reference_t<
                    decltype(data_.pointers_.[:soa_type::ptr_members[I]:])>::value_type;
                using reb_alloc = typename std::allocator_traits<
                    typename soa_type::allocator_type>::template rebind_alloc<field_type>;
                reb_alloc ra(data_.alloc_);

                try
                {
                    new_pointers
                        .[:soa_type::ptr_members[I]:] = soa_member_pointer<field_type>(
                                                          ra.allocate(new_cap));
                }
                catch (...)
                {
                    template for (constexpr auto J : std::views::indices(I))
                    {
                        using field_type = typename std::remove_reference_t<
                            decltype(data_.pointers_
                                         .[:soa_type::ptr_members[J]:])>::value_type;
                        using reb_alloc = typename std::allocator_traits<
                            typename soa_type::allocator_type>::
                            template rebind_alloc<field_type>;
                        reb_alloc ra(data_.alloc_);

                        ra.deallocate(new_pointers.[:soa_type::ptr_members[J]:].data(),
                                                                               new_cap);
                    }
                    throw;
                }
            }

            // 1. 批量移动构造所有元素到新内存（不销毁旧数据）
            template for (constexpr auto I :
                          std::views::indices(soa_type::ptr_members.size()))
            {
                auto &old = data_.pointers_.[:soa_type::ptr_members[I]:];
                auto &nw = new_pointers.[:soa_type::ptr_members[I]:];

                using field_type = std::remove_reference_t<decltype(old)>::value_type;
                static_assert(std::is_nothrow_move_constructible_v<field_type>,
                              "gen_soa_vector requires nothrow move constructible types");
                // 将旧内存的 [0, size()) 范围的元素移动到新内存
                std::uninitialized_move(old.data(), old.data() + size(), nw.data());
            }
            // 2. 批量调用旧内存中的元素的析构
            template for (constexpr auto I :
                          std::views::indices(soa_type::ptr_members.size()))
            {
                auto &old = data_.pointers_.[:soa_type::ptr_members[I]:];
                std::destroy(old.data(), old.data() + size());
            }

            // 3. 真的释放旧内存
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

            data_.capacity_ = new_cap;
            data_.pointers_ = std::move(new_pointers);
            // dense/sparse 关系不变，无需调整
        }
        constexpr void expansion_size(size_type expansion) // NOLINT
        {
            reserve(std::bit_ceil(capacity() + expansion));
        }

        // ---------- 复制 / 移动 ----------data_.p
        constexpr gen_soa_vector(const gen_soa_vector &o)
            : data_(o.capacity()), dense_(o.dense_), sparse_(o.sparse_),
              free_entities_(o.free_entities_), next_entity_id_(o.next_entity_id_)
        {
            for (size_type i = 0; i < dense_.size(); ++i)
            {
                size_type slot = i;
                template for (constexpr auto I :
                              std::views::indices(soa_type::ptr_members.size()))
                {
                    data_.template construct_field<I>(slot,
                                                      o.data_.template get<I>(slot));
                }
            }
        }

        constexpr gen_soa_vector &operator=(const gen_soa_vector &o)
        {
            if (this != &o)
            {
                auto tmp(o);
                *this = std::move(tmp);
            }
            return *this;
        }

        constexpr gen_soa_vector(gen_soa_vector &&other) noexcept
            : data_(std::move(other.data_)), dense_(std::move(other.dense_)),
              sparse_(std::move(other.sparse_)),
              free_entities_(std::move(other.free_entities_)),
              next_entity_id_(other.next_entity_id_)
        {
        }

        constexpr gen_soa_vector &operator=(gen_soa_vector &&other) noexcept
        {
            if (this != &other)
            {
                for (size_type entity : dense_)
                    data_.destroy_at(sparse_[entity]);
                data_ = std::move(other.data_);
                dense_ = std::move(other.dense_);
                sparse_ = std::move(other.sparse_);
                free_entities_ = std::move(other.free_entities_);
                next_entity_id_ = other.next_entity_id_;
            }
            return *this;
        }

        ~gen_soa_vector() noexcept
        {
            // for (size_type entity : dense_)
            //     data_.destroy_at(sparse_[entity]);
            for (auto idx : std::views::iota(size_type(0), size()))
            {
                data_.destroy_at(idx);
            }
        }

        // ---------- 迭代器（遍历物理槽位）----------
        template <typename Store>
        struct vec_iterator
        {
            Store *store;
            size_type idx; // 物理槽位索引

            constexpr decltype(auto) operator*() const noexcept
            {
                return store->data_[idx]; // bind_result 临时对象
            }
            constexpr vec_iterator &operator++() noexcept
            {
                ++idx;
                return *this;
            }
            constexpr vec_iterator operator++(int) noexcept
            {
                auto t = *this;
                ++idx;
                return t;
            }
            constexpr bool operator==(const vec_iterator &) const = default;
        };

        using iterator = vec_iterator<gen_soa_vector>;
        using const_iterator = vec_iterator<const gen_soa_vector>;

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
    };
} // namespace mcs::vulkan::ecs