#pragma once

#include "soa_member_pointer.hpp"
#include "nsdms_spec_transformed.hpp"
#include "nsdms_of.hpp"
#include "static_string.hpp"
#include "size_type.hpp"
#include "attr_no_unique_address.hpp"
#include <cassert>
#include <optional>
#include <vector>
#include <ranges>

namespace mcs::vulkan::ecs
{
    struct multiple_field
    {
        const char *name{}; // NOLINT
        std::meta::info type;
        size_t count;
        template <size_t N>
        consteval multiple_field(const char (&str)[N], std::meta::info type,
                                 size_t field_count = 1) noexcept // NOLINT
            : name{std::define_static_string(str)}, type{type}, count{field_count}
        {
        }
        [[nodiscard]] consteval auto field_name() const noexcept
        {
            return std::string_view{name};
        }
        [[nodiscard]] consteval auto field_type() const noexcept
        {
            return type;
        }
        [[nodiscard]] consteval auto field_count() const noexcept
        {
            return count;
        }
    };

    namespace detail
    {
        template <multiple_field... info>
        static consteval auto get_store_type_info()
        {
            constexpr auto N = sizeof...(info);
            std::array<std::meta::info, N> infos;
            template for (constexpr auto I : std::views::indices(N))
            {
                constexpr auto item = info...[I];
                constexpr auto name = item.field_name();
                constexpr auto type = item.field_type();
                constexpr auto count = item.field_count();
                static_assert(count > 0, "count==0 is meaningless");
                static_assert(std::meta::is_type(type));

                constexpr auto pointer = // clang-format off
                std::meta::substitute(^^soa_member_pointer, {type});
            using T = [:pointer:];
            constexpr auto count_info = std::meta::reflect_constant(count);
            constexpr auto member_type =
                count == 1 ? ^^T
                           : std::meta::substitute(^^std::array, { ^^T, count_info}); // clang-format on

                infos[I] = std::meta::data_member_spec(member_type, {.name = name});
            }
            return infos;
        }

        static consteval auto unique_field_name(auto input)
        {
            for (std::size_t i = 0; i < input.size(); ++i)
            {
                auto spec_i = input[i];
                for (std::size_t j = i + 1; j < input.size(); ++j)
                {
                    auto spec_j = input[j];
                    if (spec_i.field_name() == spec_j.field_name())
                        return false;
                }
            }
            return true;
        };

        template <multiple_field... info>
        struct gen_soa_pointers
        {

            struct type;
            consteval
            {
                std::meta::define_aggregate(^^type, get_store_type_info<info...>());
            }
        };

    }; // namespace detail

    template <multiple_field... info>
        requires(detail::unique_field_name(std::array{info...}))
    struct gen_soa_aggregate : detail::gen_soa_pointers<info...>::type
    {
        using base_type = detail::gen_soa_pointers<info...>::type;
        static constexpr auto members =
            std::define_static_array(std::meta::nonstatic_data_members_of(
                std::meta::dealias(^^base_type), std::meta::access_context::current()));

        using allocator_type =
            std::allocator<typename[:std::meta::type_of(members[0]):] ::value_type>;

        //NOTE: 核心设计是 物理内存仅仅在 dense_.size() 之内。因此允许批量析构和复制移动
        // 密集数组：按物理槽位顺序存储的存活实体 ID
        std::vector<size_type> dense_;
        // 稀疏数组：实体 ID -> 密集下标（物理槽位）
        std::vector<size_type> sparse_;
        // 可重用的实体 ID 池
        std::vector<size_type> free_entities_;
        // 下一个新实体 ID（当 free_entities_ 为空时分配）
        size_type next_entity_id_{};

        [[no_unique_address]] allocator_type allocator_;
        size_type capacity_;

        static constexpr size_type invalid_dense = ~size_type(0);
        static constexpr size_type invalid_id = ~0;

        [[nodiscard]] constexpr bool alive(size_type entity) const noexcept
        {
            return entity < sparse_.size() && sparse_[entity] != invalid_dense;
        }
        [[nodiscard]] constexpr size_type get_slot(size_type entity) const noexcept
        {
            assert(alive(entity));
            return sparse_[entity];
        }

        [[nodiscard]] constexpr size_type size() const noexcept
        {
            return dense_.size();
        }
        [[nodiscard]] constexpr auto capacity() const noexcept
        {
            return capacity_;
        }
        [[nodiscard]] constexpr auto free_size() const noexcept
        {
            return capacity() - size();
        }

        // 静态工具：释放 base_type 中所有字段的内存并置空
        template <typename Alloc>
        static constexpr void deallocate_fields(base_type &b, size_type cap,
                                                Alloc &alloc) noexcept
        {
            if (cap == 0)
                return;
            template for (constexpr auto I : std::views::indices(members.size()))
            {
                constexpr auto field_count = info...[I].field_count();
                using T = [:info...[I].field_type():];
                using RebAlloc =
                    typename std::allocator_traits<Alloc>::template rebind_alloc<T>;
                RebAlloc reb_alloc(alloc);

                auto &field = b.[:members[I]:];
                if constexpr (field_count == 1)
                {
                    if (auto *p = field.data())
                    {
                        reb_alloc.deallocate(p, cap);
                        field = {};
                    }
                }
                else
                {
                    // 使用第一个子指针作为整块内存的起始地址
                    if (auto *p = field[0].data())
                    {
                        reb_alloc.deallocate(p, field_count * cap);
                        template for (constexpr auto J : std::views::indices(field_count))
                            field[J] = {};
                    }
                }
            }
        }
        constexpr static base_type allocate_for_base(size_type cap,
                                                     const allocator_type &allocator)
        {
            allocator_type alloc(allocator);
            base_type result;
            if (cap == 0)
                return result;

            try
            {
                template for (constexpr auto I : std::views::indices(members.size()))
                {
                    constexpr auto field_count = info...[I].field_count();
                    using T = [:info...[I].field_type():];
                    using RebAlloc = typename std::allocator_traits<
                        allocator_type>::template rebind_alloc<T>;
                    RebAlloc reb_alloc(alloc);

                    if constexpr (field_count == 1)
                    {
                        T *ptr = reb_alloc.allocate(cap);
                        result.[:members[I]:] = soa_member_pointer<T>(ptr);
                    }
                    else
                    {
                        // 一次性分配 field_count * cap 个元素，保证子列连续
                        T *big = reb_alloc.allocate(field_count * cap);
                        template for (constexpr auto J : std::views::indices(field_count))
                            result.[:members[I]:][J] =
                                                     soa_member_pointer<T>(big + J * cap);
                    }
                }
            }
            catch (...)
            {
                deallocate_fields(result, cap, alloc); // 回滚已分配的部分
                throw;
            }
            return result;
        }

        template <size_type I>
        constexpr auto destroy_fields_before() noexcept
        {
            // if (dense_.size() == 0)
            //     return; //NOTE: destroy_n 内部已经判断
            template for (constexpr auto J : std::views::indices(I))
            {
                auto &field = (*this).[:members[J]:];
                constexpr auto fc = info...[J].field_count();
                if constexpr (fc == 1)
                    std::destroy_n(field.data(), dense_.size());
                else
                {
                    template for (constexpr auto K : std::views::indices(fc))
                    {
                        std::destroy_n(field[K].data(), dense_.size());
                    }
                }
            }
        }

        //  析构当前对象所有有效的元素
        constexpr auto destroy_fields() noexcept
        {
            destroy_fields_before<members.size()>();
        }

        constexpr gen_soa_aggregate() : gen_soa_aggregate(0) {}
        constexpr explicit gen_soa_aggregate(size_type cap, allocator_type allocator = {})
            : base_type(allocate_for_base(cap, allocator)), sparse_(cap, invalid_dense),
              allocator_{std::move(allocator)}, capacity_{cap}
        {
            dense_.reserve(cap);
        }
        constexpr ~gen_soa_aggregate() noexcept
        {
            destroy_fields();
            deallocate_fields(*this, capacity_, allocator_);
        }
        constexpr gen_soa_aggregate(const gen_soa_aggregate &o)
            : base_type(allocate_for_base(o.capacity_, o.allocator_)), dense_(o.dense_),
              sparse_(o.sparse_), free_entities_(o.free_entities_),
              next_entity_id_(o.next_entity_id_),
              allocator_(std::allocator_traits<allocator_type>::
                             select_on_container_copy_construction(o.allocator_)),
              capacity_(o.capacity_)
        {
            try
            {
                template for (constexpr auto I : std::views::indices(members.size()))
                {
                    auto &src_field = o.[:members[I]:];
                    auto &dst_field = (*this).[:members[I]:];
                    const auto size = dense_.size();
                    constexpr auto fc = info...[I].field_count();
                    try
                    {
                        if constexpr (fc == 1)
                            std::uninitialized_copy_n(src_field.data(), size,
                                                      dst_field.data());
                        else
                        {
                            std::size_t constructed_fields = 0;
                            try
                            {
                                template for (constexpr auto J : std::views::indices(fc))
                                {
                                    std::uninitialized_copy_n(src_field[J].data(), size,
                                                              dst_field[J].data());
                                    ++constructed_fields;
                                }
                            }
                            catch (...)
                            {
                                for (std::size_t j = 0; j < constructed_fields; ++j)
                                    std::destroy_n(dst_field[j].data(), size);
                                throw;
                            }
                        }
                    }
                    catch (...)
                    {
                        destroy_fields_before<I>();
                        throw;
                    }
                }
            }
            catch (...)
            {
                deallocate_fields(*this, capacity_, allocator_);
                throw;
            }
        }
        constexpr gen_soa_aggregate &operator=(const gen_soa_aggregate &o)
        {
            if (this != &o)
            {
                auto tmp(o);            // 调用拷贝构造（可能抛异常）
                *this = std::move(tmp); // 移动赋值（noexcept）
            }
            return *this;
        }
        constexpr gen_soa_aggregate(gen_soa_aggregate &&o) noexcept
            : dense_(std::move(o.dense_)), sparse_(std::move(o.sparse_)),
              free_entities_(std::move(o.free_entities_)),
              next_entity_id_(std::exchange(o.next_entity_id_, 0)),
              allocator_(std::move(o.allocator_)),
              capacity_(std::exchange(o.capacity_, 0))
        {
            // 逐字段窃取基类指针。 NOTE: 避免切片
            template for (constexpr auto I : std::views::indices(members.size()))
            {
                auto &dst = (*this).[:members[I]:];
                auto &src = o.[:members[I]:];
                constexpr auto fc = info...[I].field_count();
                if constexpr (fc == 1)
                {
                    dst = std::exchange(src, {});
                }
                else
                {
                    template for (constexpr auto J : std::views::indices(fc))
                    {
                        dst[J] = std::exchange(src[J], {});
                    }
                }
            }
        }
        constexpr gen_soa_aggregate &operator=(gen_soa_aggregate &&other) noexcept
        {
            if (this != &other)
            {
                destroy_fields();
                deallocate_fields(*this, capacity_, allocator_);

                // 窃取 other 的指针
                template for (constexpr auto I : std::views::indices(members.size()))
                {
                    auto &dst = (*this).[:members[I]:];
                    auto &src = other.[:members[I]:];
                    constexpr auto fc = info...[I].field_count();
                    if constexpr (fc == 1)
                    {
                        dst = std::exchange(src, {});
                    }
                    else
                    {
                        template for (constexpr auto J : std::views::indices(fc))
                        {
                            dst[J] = std::exchange(src[J], {});
                        }
                    }
                }
                dense_ = std::exchange(other.dense_, {});
                sparse_ = std::exchange(other.sparse_, {});
                free_entities_ = std::exchange(other.free_entities_, {});
                next_entity_id_ = std::exchange(other.next_entity_id_, 0);
                allocator_ = std::exchange(other.allocator_, {});
                capacity_ = std::exchange(other.capacity_, 0);
            }
            return *this;
        }

        //NOTE: 必须固定位置分配
        // ======================== 实体管理 ========================
        [[nodiscard]] constexpr std::optional<std::pair<size_type, size_type>>
        allocate_with_slot()
        {
            if (dense_.size() >= capacity_) [[unlikely]]
                return std::nullopt;
            else [[likely]]
            {
                size_type entity;
                if (!free_entities_.empty())
                {
                    entity = free_entities_.back();
                    free_entities_.pop_back();
                }
                else
                {
                    if (next_entity_id_ >= sparse_.size())
                        sparse_.resize(next_entity_id_ + 1, invalid_dense);
                    entity = next_entity_id_++;
                }

                size_type slot = dense_.size(); //NOTE: 必须是真正的连续
                dense_.push_back(entity);
                sparse_[entity] = slot;
                return std::pair<size_type, size_type>{entity, slot};
            }
        }
        [[nodiscard]] constexpr std::optional<size_type> allocate()
        {
            if (auto ret = allocate_with_slot(); ret) [[likely]]
                return (*ret).first;
            else [[unlikely]]
                return std::nullopt;
        }

        template <typename... Arg>
            requires(sizeof...(Arg) == members.size())
        static consteval bool is_noexcept_construct()
        {
            constexpr auto [... I] = std::make_index_sequence<members.size()>{};
            static_assert((std::is_nothrow_move_constructible_v<
                               typename[:info...[I].field_type():]> &&
                           ...),
                          "All field types must have nothrow move constructors");
            constexpr auto nothrow_constructible = []<size_t I>() {
                constexpr auto field_count = info...[I].field_count();
                using T = [:info...[I].field_type():];
                if constexpr (field_count == 1)
                {
                    return std::is_nothrow_constructible_v<T, Arg...[I]>;
                }
                else
                {
                    // using value_type =
                    //     std::remove_cvref_t<decltype(std::declval<Arg...[I]>()[0])>;
                    // return std::is_nothrow_constructible_v<T, value_type>;
                    return std::is_constructible_v<std::array<T, field_count>, Arg...[I]>;
                }
            };
            return (nothrow_constructible.template operator()<I>() && ...);
        }

        template <typename... Arg>
            requires(sizeof...(Arg) == members.size())
        constexpr auto construct_at(
            this auto &self, size_type slot,
            Arg &&...args) noexcept(is_noexcept_construct<Arg...>())
        {
            constexpr auto is_noexcept = is_noexcept_construct<Arg...>();
            if constexpr (is_noexcept)
            {
                template for (constexpr auto I : std::views::indices(members.size()))
                {
                    constexpr auto field_count = info...[I].field_count();
                    auto &field = self.[:members[I]:];
                    if constexpr (field_count > 1)
                    {
                        // NOTE: 必须一次给够
                        template for (constexpr auto J : std::views::indices(field_count))
                        {
                            auto *ptr = field[J].data() + slot;
                            std::construct_at(
                                ptr, std::forward_like<Arg...[I]>(args...[I][J]));
                        }
                    }
                    else
                    {
                        auto *ptr = field.data() + slot;
                        std::construct_at(ptr, std::forward<Arg...[I]>(args...[I]));
                    }
                }
            }
            else
            {
                template for (constexpr auto I : std::views::indices(members.size()))
                {
                    try
                    {
                        auto &field = self.[:members[I]:];
                        constexpr auto field_count = info...[I].field_count();
                        if constexpr (field_count > 1)
                        {
                            std::size_t constructed_fields = 0;
                            try
                            {
                                template for (constexpr auto J :
                                              std::views::indices(field_count))
                                {
                                    auto *ptr = field[J].data() + slot;
                                    std::construct_at(
                                        ptr, std::forward_like<Arg...[I]>(args...[I][J]));
                                    ++constructed_fields;
                                }
                            }
                            catch (...)
                            {
                                for (std::size_t j = 0; j < constructed_fields; ++j)
                                {
                                    auto *ptr = field[j].data() + slot;
                                    std::destroy_at(ptr);
                                }
                                throw;
                            }
                        }
                        else
                        {
                            auto *ptr = field.data() + slot;
                            std::construct_at(ptr, std::forward<Arg...[I]>(args...[I]));
                        }
                    }
                    catch (...)
                    {
                        // NOTE: 销毁前面的已经构造的字段
                        self.template destroy_fields_before<I>();
                        throw;
                    }
                }
            }
        }
        template <typename... Arg>
            requires(sizeof...(Arg) == members.size())
        constexpr auto construct_entity(
            this auto &self, size_type entity,
            Arg &&...args) noexcept(noexcept(self.construct_at(self.get_slot(entity),
                                                               std::forward<Arg>(
                                                                   args)...)))
        {
            self.construct_at(self.get_slot(entity), std::forward<Arg>(args)...);
        }

        constexpr void release_entity(size_type entity) noexcept
        {
            assert(alive(entity));
            size_type slot = sparse_[entity];
            size_type last = dense_.size() - 1;

            if (slot != last)
            {
                size_type moved_entity = dense_[last];
                // 交换两个槽位的全部字段数据
                template for (constexpr auto I : std::views::indices(members.size()))
                {
                    auto &field = (*this).[:members[I]:];
                    using std::swap;
                    constexpr auto fc = info...[I].field_count();
                    if constexpr (fc == 1)
                        swap(field[slot], field[last]);
                    else
                        template for (constexpr auto J : std::views::indices(fc))
                            swap(field[J][slot], field[J][last]);
                }
                dense_[slot] = moved_entity;
                sparse_[moved_entity] = slot;
            }

            // 销毁原 entity（现在位于 last）
            destroy_at(last);
            dense_.pop_back();
            sparse_[entity] = invalid_dense;
            free_entities_.push_back(entity);
        }
        constexpr void destroy_at(size_type slot) noexcept // 物理槽位
        {
            template for (constexpr auto I : std::views::indices(members.size()))
            {
                auto &field = (*this).[:members[I]:];
                constexpr auto fc = info...[I].field_count();
                if constexpr (fc == 1)
                    std::destroy_at(field.data() + slot);
                else
                    template for (constexpr auto J : std::views::indices(fc))
                        std::destroy_at(field[J].data() + slot);
            }
        }

        constexpr void reserve(size_type new_cap)
        {
            auto old_cap = capacity();
            if (new_cap <= old_cap)
                return;

            // 1. 使用 allocate_for_base 分配新内存块
            base_type new_base = allocate_for_base(new_cap, allocator_);

            // 2. 移动现有实体数据到新内存
            size_type count = size();
            if (count > 0)
            {
                template for (constexpr auto I : std::views::indices(members.size()))
                {
                    constexpr auto field_count = info...[I].field_count();
                    using T = [:info...[I].field_type():];
                    static_assert(std::is_nothrow_move_constructible_v<T>,
                                  "reserve requires nothrow move constructible types");

                    auto &old_field = (*this).[:members[I]:];
                    auto &new_field = new_base.[:members[I]:];

                    if constexpr (field_count == 1)
                        std::uninitialized_move_n(old_field.data(), count,
                                                  new_field.data());
                    else
                        template for (constexpr auto J : std::views::indices(field_count))
                            std::uninitialized_move_n(old_field[J].data(), count,
                                                      new_field[J].data());
                }

                // 3. 析构旧内存中的对象（已被移动）
                template for (constexpr auto I : std::views::indices(members.size()))
                {
                    constexpr auto field_count = info...[I].field_count();
                    auto &old_field = (*this).[:members[I]:];

                    if constexpr (field_count == 1)
                        std::destroy_n(old_field.data(), count);
                    else
                        template for (constexpr auto J : std::views::indices(field_count))
                            std::destroy_n(old_field[J].data(), count);
                }
            }

            // 4. 释放旧内存（使用 deallocate_fields 一次性释放数组字段）
            deallocate_fields(*this, old_cap, allocator_);

            // 5. 替换基类指针
            template for (constexpr auto I : std::views::indices(members.size()))
            {
                (*this).[:members[I]:] = std::move(new_base.[:members[I]:]);
            }
            // 6. 更新容量和辅助结构
            capacity_ = new_cap;

            if (new_cap > sparse_.size())
                sparse_.resize(new_cap, invalid_dense);

            for (size_type i = new_cap; i > old_cap; --i)
                free_entities_.push_back(i - 1);
        }
        void clear() noexcept
        {
            // 从 dense_ 末尾开始释放，避免频繁交换
            while (!dense_.empty())
                release_entity(dense_.back()); // release_entity 本身是 noexcept

            // 重置所有辅助容器和计数器
            sparse_.clear();
            free_entities_.clear();
            next_entity_id_ = 0;
        }

        constexpr void expansion_size(size_type additional) // NOLINT
        {
            reserve(std::bit_ceil(capacity() + additional));
        }

        template <typename... Arg>
            requires(sizeof...(Arg) == members.size())
        constexpr size_type new_entity(Arg &&...args) noexcept(false)
        {
            if (auto ret = allocate_with_slot(); ret)
            {
                auto [entity, slot] = *ret;
                constexpr bool is_noexcept =
                    noexcept(construct_at(slot, std::forward<Arg>(args)...));
                if constexpr (is_noexcept)
                {
                    construct_at(slot, std::forward<Arg>(args)...);
                }
                else
                {
                    try
                    {
                        construct_at(slot, std::forward<Arg>(args)...);
                    }
                    catch (...)
                    {
                        // 构造临时对象失败，回滚分配
                        dense_.pop_back();
                        sparse_[entity] = invalid_dense;
                        free_entities_.push_back(entity);
                        throw;
                    }
                }
                return entity;
            }
            throw std::bad_alloc{};
        }

        static consteval size_type find_name(static_string name)
        {
            template for (constexpr auto I : std::views::indices(members.size()))
            {
                if (name.view() == info...[I].field_name())
                    return I;
            }
            return ~0;
        }

        template <size_type I, typename Self>
        static constexpr auto get_field_span(Self &&self, size_type field_count) noexcept
        {
            if constexpr (info...[I].field_count() > 1)
            {
                // 字段类型为 std::array<soa_member_pointer<T>, N>，取第 field_count 个底层指针
                auto &arr = std::forward_like<Self>(self.[:members[I]:]);
                return std::span{arr[field_count].data(), self.size()};
            }
            else
            {
                auto &ptr = std::forward_like<Self>(self.[:members[I]:]);
                return std::span{ptr.data(), self.size()};
            }
        }
        template <size_type I, typename Self>
        constexpr decltype(auto) raw_field(this Self &&self,
                                           size_type field_count) noexcept
        {
            if constexpr (info...[I].field_count() > 1)
            {
                auto &arr = std::forward_like<Self>(self.[:members[I]:]);
                return arr[field_count];
            }
            else
            {
                auto &ptr = std::forward_like<Self>(self.[:members[I]:]);
                return ptr;
            }
        }
        template <static_string name, typename Self>
        constexpr decltype(auto) raw_field(this Self &&self,
                                           size_type field_count) noexcept
        {
            return std::forward_like<Self>(
                self.template raw_field<find_name(name)>(field_count));
        }
        template <size_type... I, typename Self>
        constexpr auto get(this Self &&self, size_type field_count) noexcept
        {
            return std::tie(
                std::forward_like<Self>(self.template raw_field<I>(field_count))...);
        }
        template <static_string... name, typename Self>
            requires(sizeof...(name) > 0 && ((find_name(name) != ~0) && ...))
        constexpr auto get(this Self &&self, size_type field_count) noexcept
        {
            return std::forward_like<Self>(
                self.template get<find_name(name)...>(field_count));
        }

        template <static_string... name>
            requires(sizeof...(name) > 0 && ((find_name(name) != ~0) && ...))
        constexpr auto view(this auto &&self, size_type field_count = 0) noexcept
        {
            return std::views::zip(get_field_span<find_name(name)>(
                std::forward<decltype(self)>(self), field_count)...);
        }
        template <size_t... I>
            requires(sizeof...(I) > 0)
        constexpr auto view(this auto &&self, size_type field_count = 0) noexcept
        {
            return std::views::zip(
                get_field_span<I>(std::forward<decltype(self)>(self), field_count)...);
        }
        constexpr auto view(this auto &&self, size_type field_count = 0) noexcept
        {
            constexpr auto [... I] = std::make_index_sequence<members.size()>{};
            return std::views::zip(
                get_field_span<I>(std::forward<decltype(self)>(self), field_count)...);
        }

        template <size_type I>
        constexpr decltype(auto) get_slot_field(this auto &&self,
                                                [[maybe_unused]] size_type field_count,
                                                size_type slot) noexcept
        {
            auto &field = self.[:members[I]:];
            if constexpr (info...[I].field_count() > 1)
                return std::forward_like<decltype(self)>(field[field_count][slot]);
            else
                return std::forward_like<decltype(self)>(field[slot]);
        }
        template <static_string... name>
            requires(sizeof...(name) > 0 && ((find_name(name) != ~0) && ...))
        constexpr auto view_slot(this auto &&self, size_type field_count,
                                 size_type slot) noexcept
        {
            assert(slot < self.size());
            struct view_result;
            consteval
            {
                std::array<std::meta::info, sizeof...(name)> infos;
                int idx = 0;
                template for (constexpr auto I : std::array{find_name(name)...})
                {
                    using T = decltype(std::forward<decltype(self)>(self)
                                           .template get_slot_field<I>(0, 0));
                    static_assert(std::is_reference_v<T>, "should reference type");
                    infos[idx++] = std::meta::data_member_spec(
                        ^^T, {
                                 .name = info...[I].field_name()});
                }
                std::meta::define_aggregate(^^view_result, infos);
            }
            return view_result{
                std::forward<decltype(self)>(self)
                    .template get_slot_field<find_name(name)>(field_count, slot)...};
        }
        template <static_string... name>
            requires(sizeof...(name) > 0 && ((find_name(name) != ~0) && ...))
        constexpr auto view_entity(this auto &&self, size_type field_count,
                                   size_type entity) noexcept
        {
            return std::forward<decltype(self)>(self).template view_slot<name...>(
                field_count, self.get_slot(entity));
        }

        template <static_string... name>
            requires(sizeof...(name) > 0 && ((find_name(name) != ~0) && ...))
        constexpr auto tie(this auto &&self) noexcept
        {
            return std::tie(
                std::forward_like<decltype(self)>(self.[:members[find_name(name)]:])...);
        }
        template <size_t... I>
            requires(sizeof...(I) > 0)
        constexpr auto tie(this auto &&self) noexcept
        {
            return std::tie(std::forward_like<decltype(self)>(self.[:members[I]:])...);
        }
        constexpr auto tie(this auto &&self) noexcept
        {
            constexpr auto [... I] = std::make_index_sequence<members.size()>{};
            return std::tie(std::forward_like<decltype(self)>(self.[:members[I]:])...);
        }

        static consteval auto field_count() noexcept
        {
            constexpr auto [... I] = std::make_index_sequence<members.size()>{};
            return std::array{(info...[I].field_count(), ...)};
        }
        template <size_t... I>
            requires(sizeof...(I) > 0 && ((I < members.size()) && ...))
        static consteval auto field_count() noexcept
        {
            return std::array{(info...[I].field_count(), ...)};
        }
        template <static_string... name>
            requires(sizeof...(name) > 0 && ((find_name(name) < members.size()) && ...))
        static consteval auto field_count() noexcept
        {
            return std::array{(info...[find_name(name)].field_count(), ...)};
        }

        template <typename TieTuple>
        static consteval auto field_count_from_tie_type()
        {
            constexpr std::size_t N = std::tuple_size_v<TieTuple>;
            std::array<std::size_t, N> counts{};
            template for (constexpr auto I : std::views::indices(N))
            {
                using Elem = std::remove_cvref_t<std::tuple_element_t<I, TieTuple>>;
                if constexpr (requires { typename std::tuple_size<Elem>::type; })
                    counts[I] = std::tuple_size_v<Elem>; // 是 std::array，返回 N
                else
                    counts[I] = 1; // 普通 soa_member_pointer
            }
            return counts;
        }
        constexpr size_type nextEntityId() const noexcept
        {
            return !free_entities_.empty() ? free_entities_.back() : next_entity_id_;
        }
    };
}; // namespace mcs::vulkan::ecs