#include <algorithm>
#include <array>
#include <assert.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <exception>
#include <new>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <meta>
#include <ranges>
#include <memory>

#include <flat_map>

// NOLINTBEGIN

using world_id_type = uint32_t;
using world_id_version_type = uint16_t;
using size_type = world_id_type;

struct static_string
{
    const char *value{}; // NOLINT

    static_string() = default;
    consteval explicit static_string(const char *p) noexcept : value{p} {}
    template <size_t N>
    consteval static_string(const char (&str)[N]) noexcept // NOLINT
        : value{std::define_static_string(str)}
    {
    }
    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return std::string_view{value};
    }
    constexpr bool operator==(const static_string &o) const noexcept
    {
        return value == o.value || view() == o;
    }
    constexpr bool operator==(const std::string_view &o) const noexcept
    {
        return view() == o;
    }
    template <size_t N>
    consteval bool operator==(const char (&str)[N]) const noexcept // NOLINT
    {
        return view() == std::string_view{str, N - 1};
    }

    constexpr bool operator<(const static_string &o) const noexcept
    {
        return view() < o.view();
    }
};

static consteval auto nsdms_of(std::meta::info info)
{
    return std::meta::nonstatic_data_members_of(info,
                                                std::meta::access_context::current());
}

static consteval auto nsdms_spec_transformed(std::meta::info input, auto f)
{
    return std::meta::nonstatic_data_members_of(input,
                                                std::meta::access_context::current()) //
           | std::views::transform([=](auto m) -> std::meta::info {
                 return std::meta::data_member_spec(
                     f(std::meta::type_of(m)), {.name = std::meta::identifier_of(m)});
             });
}

template <typename T>
struct soa_member_pointer
{
  public:
    using value_type = T;

    constexpr soa_member_pointer() noexcept : data_{nullptr} {}
    constexpr explicit soa_member_pointer(T *data) noexcept : data_{data} {}

    constexpr decltype(auto) operator[](this auto &&self, size_type idx) noexcept
    {
        return std::forward_like<decltype(self)>(self.data_[idx]);
    }

    constexpr const T *data() const noexcept
    {
        return data_;
    }
    constexpr T *data() noexcept
    {
        return data_;
    }

  private:
    T *data_;
};

struct position
{
    int x;
    int y;
    int z;
    auto operator<=>(const position &) const noexcept = default;
};

struct Point
{
    int x;
    int y;
    auto operator<=>(const Point &) const = default;
    /* 非比较函数 */
};

struct component
{
    static_string component_name;
    std::vector<void *> fns;
};

struct entity
{
    static_string entity_name;
    std::vector<component> components;
};

struct position_manager
{
    std::vector<position> position_store;
    static auto update()
    {
        //
    }
    auto make_component() {}
};
struct point_manager
{
    std::vector<Point> point_store;
    static auto update()
    {
        //
    }
    auto make_component() {}
};

struct runtime_object
{
    static_string name;
};

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
                        result.[:members[I]:][J] = soa_member_pointer<T>(big + J * cap);
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
        : base_type(allocate_for_base(
              o.capacity_,
              std::allocator_traits<
                  allocator_type>::select_on_container_copy_construction(o.allocator_))),
          dense_(o.dense_), sparse_(o.sparse_), free_entities_(o.free_entities_),
          next_entity_id_(o.next_entity_id_),
          allocator_(
              std::allocator_traits<
                  allocator_type>::select_on_container_copy_construction(o.allocator_)),
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
          allocator_(std::move(o.allocator_)), capacity_(std::exchange(o.capacity_, 0))
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
        static_assert(
            (std::is_nothrow_move_constructible_v<typename[:info...[I].field_type():]> &&
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
                using value_type =
                    std::remove_cvref_t<decltype(std::declval<Arg...[I]>()[0])>;
                return std::is_nothrow_constructible_v<T, value_type>;
                // return std::is_constructible_v<std::array<T, field_count>, Arg...[I]>;
            }
        };
        return (nothrow_constructible.template operator()<I>() && ...);
    }

    template <typename... Arg>
        requires(sizeof...(Arg) == members.size())
    constexpr auto construct_at(this auto &self, size_type slot,
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
                        std::construct_at(ptr,
                                          std::forward_like<Arg...[I]>(args...[I][J]));
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
                                                           std::forward<Arg>(args)...)))
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
                    std::uninitialized_move_n(old_field.data(), count, new_field.data());
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

    // view
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
            std::forward<decltype(self)>(self).template get_slot_field<find_name(name)>(
                field_count, slot)...};
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
};

constexpr bool gen_soa_aggregate_test()
{
    // 0. 类型检查（原有测试）
    constexpr auto int_1 = gen_soa_aggregate<{"a", ^^int, 1}>{};
    static_assert(std::is_same_v<decltype(int_1.a), soa_member_pointer<int>>);
    constexpr auto int_2 = gen_soa_aggregate<{"a", ^^int, 2}>{};
    static_assert(
        std::is_same_v<decltype(int_2.a), std::array<soa_member_pointer<int>, 2>>);
    constexpr auto int_3 = gen_soa_aggregate<{"a", ^^int, 2}, {"b", ^^int}>{};
    static_assert(
        std::is_same_v<decltype(int_3.a), std::array<soa_member_pointer<int>, 2>>);
    static_assert(std::is_same_v<decltype(int_3.b), soa_member_pointer<int>>);
    static_assert(int_1.[:gen_soa_aggregate<{"a", ^^int, 1}>::members[0]:].data() ==
                                                                          nullptr);
    static_assert(std::is_same_v<gen_soa_aggregate<{"a", ^^int, 1}>::allocator_type,
                                 std::allocator<int>>);

    // 1. 基本构造与析构（非零容量，无元素）
    {
        gen_soa_aggregate<{"a", ^^int, 1}> a(5); // 分配 capacity=5，dense 为空
        gen_soa_aggregate<{"a", ^^int, 2}> b(3); // 数组字段，分配 2 个 int 数组各 3 个
        gen_soa_aggregate<{"a", ^^int, 2}, {"b", ^^int}> c(4);
    } // 离开作用域，析构函数释放所有内存

    // 2. 拷贝构造
    {
        gen_soa_aggregate<{"a", ^^int, 1}> a(4);
        auto b = a; // 拷贝：分配相同容量，dense 都是空的，所以只分配内存不拷贝元素
    } // 两个对象各自释放

    // 数组字段的拷贝
    {
        gen_soa_aggregate<{"x", ^^int, 2}> a(3);
        auto b = a;
    }

    // 3. 移动构造
    {
        gen_soa_aggregate<{"a", ^^int, 1}> a(6);
        auto b = std::move(a); // b 窃取 a 的指针，a 变为空容量
    } // b 正常释放，a 释放时容量为 0 无操作

    // 4. 移动赋值
    {
        gen_soa_aggregate<{"a", ^^int, 1}> a(5);
        gen_soa_aggregate<{"a", ^^int, 1}> b(3);
        b = std::move(a); // b 先释放自身，再窃取 a，a 变为空
    } // 两个对象安全析构

    // 5. 拷贝赋值
    {
        gen_soa_aggregate<{"a", ^^int, 1}> a(5);
        gen_soa_aggregate<{"a", ^^int, 1}> b(3);
        b = a; // copy‑and‑swap，中间临时对象，最终释放旧内存
    }

    // 6. 包含元素的拷贝（构造少量元素，然后拷贝，确保元素复制和销毁正确）
    {
        gen_soa_aggregate<{"a", ^^int, 1}> a(4);
        a.dense_.push_back(0); // 模拟实体存在，物理槽位 0 已用
        a.sparse_[0] = 0;
        // 在槽位 0 上构造 int
        std::construct_at(a.a.data(), 42); // a.a 是 soa_member_pointer<int>

        auto b = a; // 拷贝：分配新内存 + 拷贝元素

        // 验证拷贝的正确性
        assert(*b.a.data() == 42);
        // 手动析构元素（因为 dense 记录了元素，离开作用域时 destroy_fields 会调用）
    } // 离开作用域，a 和 b 各自析构，元素被 destroy 且内存释放

    // 7. 移动包含元素的构造
    {
        gen_soa_aggregate<{"a", ^^int, 1}> a(4);
        a.dense_.push_back(0);
        a.sparse_[0] = 0;
        std::construct_at(a.a.data(), 10);

        auto b = std::move(a);
        assert(*b.a.data() == 10);
        // a 的 dense 和 sparse 被移动后为空，没有元素需要销毁
    }

    // ---------- 测试 size() 与 view<...>() ----------
    {
        // 定义一个包含两个字段的聚合：int x 和 double y
        using Store = gen_soa_aggregate<{"x", ^^int}, {"y", ^^double}>;
        Store store(10);           // 容量 10，初始无实体
        assert(store.size() == 0); // size() 返回 0

        // 分配两个实体，并获得它们的 entity id
        auto e0 = store.allocate();
        auto e1 = store.allocate();
        assert(e0.has_value() && e1.has_value());
        assert(store.size() == 2); // 现在有两个实体

        // 查找它们在密集数组中的物理槽位（分配顺序保证为 0 和 1）
        auto slot0 = store.get_slot(*e0);
        auto slot1 = store.get_slot(*e1);

        // 在对应槽位上构造数据
        std::construct_at(store.x.data() + slot0, 42);
        std::construct_at(store.y.data() + slot0, 3.14);
        std::construct_at(store.x.data() + slot1, 100);
        std::construct_at(store.y.data() + slot1, 2.71);

        // --- 测试 view<"x", "y">()：返回两个字段的 zip 视图 ---
        {
            static_assert(Store::find_name("x") != ~0);
            static_assert(Store::find_name("y") != ~0);
            auto v = store.view<"x", "y">();
            auto it = v.begin();
            // 第一个实体
            {
                auto [x, y] = *it;
                assert(x == 42);
                assert(y == 3.14);
            }
            ++it;
            // 第二个实体
            {
                auto [x, y] = *it;
                assert(x == 100);
                assert(y == 2.71);
            }
            ++it;
            assert(it == v.end()); // 遍历结束
        }

        // --- 测试 view<"x">()：只选取一个字段（注意 zip 仍会包装为 tuple）---
        {
            auto vx = store.view<"x">();
            auto it = vx.begin();
            auto [x0] = *it; // 第一个实体的 x
            assert(x0 == 42);
            ++it;
            auto [x1] = *it; // 第二个实体的 x
            assert(x1 == 100);
            assert(++it == vx.end());
        }

        // --- 测试 view<"y">() ---
        {
            auto vy = store.view<"y">();
            auto it = vy.begin();
            auto [y0] = *it;
            assert(y0 == 3.14);
            ++it;
            auto [y1] = *it;
            assert(y1 == 2.71);
            assert(++it == vy.end());
        }

        // 离开作用域时 store 析构，自动调用 destroy_fields() 并释放内存
    }
    // ---------- 测试多 field_count 字段的 view 子索引 ----------
    {
        // 定义一个包含两个数组字段的聚合：vec3 position (x,y,z) 和 vec3 velocity (dx,dy,dz)
        using Store =
            gen_soa_aggregate<{"position", ^^float, 3}, {"velocity", ^^float, 3}>;
        Store store(10);
        assert(store.size() == 0);

        // 分配 3 个实体，手动填充每个字段的 3 个子数组
        auto e0 = store.allocate();
        auto e1 = store.allocate();
        auto e2 = store.allocate();
        assert(e0 && e1 && e2);
        assert(store.size() == 3);

        auto s0 = store.get_slot(*e0);
        auto s1 = store.get_slot(*e1);
        auto s2 = store.get_slot(*e2);

        // position 是一个 std::array<soa_member_pointer<float>, 3>
        // position[0] -> x, position[1] -> y, position[2] -> z
        std::construct_at(store.position[0].data() + s0, 1.0f);
        std::construct_at(store.position[1].data() + s0, 2.0f);
        std::construct_at(store.position[2].data() + s0, 3.0f);
        std::construct_at(store.velocity[0].data() + s0, 0.1f);
        std::construct_at(store.velocity[1].data() + s0, 0.2f);
        std::construct_at(store.velocity[2].data() + s0, 0.3f);

        std::construct_at(store.position[0].data() + s1, 10.0f);
        std::construct_at(store.position[1].data() + s1, 20.0f);
        std::construct_at(store.position[2].data() + s1, 30.0f);
        std::construct_at(store.velocity[0].data() + s1, 1.1f);
        std::construct_at(store.velocity[1].data() + s1, 1.2f);
        std::construct_at(store.velocity[2].data() + s1, 1.3f);

        std::construct_at(store.position[0].data() + s2, 100.0f);
        std::construct_at(store.position[1].data() + s2, 200.0f);
        std::construct_at(store.position[2].data() + s2, 300.0f);
        std::construct_at(store.velocity[0].data() + s2, 2.1f);
        std::construct_at(store.velocity[1].data() + s2, 2.2f);
        std::construct_at(store.velocity[2].data() + s2, 2.3f);

        // 1. 测试单字段视图，选择子索引 0（即所有实体的 position.x）
        {
            auto vx = store.view<"position">(0);
            auto it = vx.begin();
            {
                auto [x0] = *it;
                assert(x0 == 1.0f);
            }
            ++it;
            {
                auto [x1] = *it;
                assert(x1 == 10.0f);
            }
            ++it;
            {
                auto [x2] = *it;
                assert(x2 == 100.0f);
            }
            assert(++it == vx.end());
        }

        // 2. 测试单字段视图，子索引 1（position.y）
        {
            auto vy = store.view<"position">(1);
            auto it = vy.begin();
            auto [y0] = *it;
            assert(y0 == 2.0f);
            ++it;
            auto [y1] = *it;
            assert(y1 == 20.0f);
            ++it;
            auto [y2] = *it;
            assert(y2 == 200.0f);
            assert(++it == vy.end());
        }

        // 3. 测试单字段视图，子索引 2（position.z）
        {
            auto vz = store.view<"position">(2);
            auto it = vz.begin();
            auto [z0] = *it;
            assert(z0 == 3.0f);
            ++it;
            auto [z1] = *it;
            assert(z1 == 30.0f);
            ++it;
            auto [z2] = *it;
            assert(z2 == 300.0f);
        }

        // 4. 测试两个数组字段的视图，但使用相同的子索引（例如都取第 0 个分量）
        //    注意：此时 zip 返回 tuple<float&, float&>
        {
            auto v_pos_vel = store.view<"position", "velocity">(0);
            auto it = v_pos_vel.begin();
            {
                auto [px, vx] = *it;
                assert(px == 1.0f && vx == 0.1f);
            }
            ++it;
            {
                auto [px, vx] = *it;
                assert(px == 10.0f && vx == 1.1f);
            }
            ++it;
            {
                auto [px, vx] = *it;
                assert(px == 100.0f && vx == 2.1f);
            }
        }

        // 5. 测试混合字段：一个数组字段的某个子索引 + 一个普通字段（此处没有普通字段，但可混合）
        //    如果有一个非数组字段，传入的 field_count 会被忽略，依然正常工作。
        //    这里用另一个只有单字段的聚合来验证兼容性。
        {
            using Mixed = gen_soa_aggregate<{"id", ^^int}, {"color", ^^float, 3}>;
            Mixed m(10);
            auto e = m.allocate();
            assert(e && m.size() == 1);
            auto s = m.get_slot(*e);
            std::construct_at(m.id.data() + s, 42);
            std::construct_at(m.color[0].data() + s, 0.5f);
            std::construct_at(m.color[1].data() + s, 0.6f);
            std::construct_at(m.color[2].data() + s, 0.7f);

            // view<"id", "color">(1) : id 忽略 field_count，color 取索引 1
            auto vmixed = m.view<"id", "color">(1);
            auto it = vmixed.begin();
            auto [id, c1] = *it;
            assert(id == 42);
            assert(c1 == 0.6f);
            {
                auto vmixed = m.view<"id", "color">(2);
                auto it = vmixed.begin();
                auto [id, c1] = *it;
                assert(id == 42);
                assert(c1 == 0.7f);
            }
        }
        // NOTE: 类型查看
        {
            using Mixed = gen_soa_aggregate<{"id", ^^int}, {"color", ^^float, 3}>;
            Mixed m(10);
            static_assert(std::is_same_v<decltype(m.id), soa_member_pointer<int>>);
            static_assert(std::is_same_v<decltype(m.color),
                                         std::array<soa_member_pointer<float>, 3>>);

            for (auto turp : m.view(1))
            {
                assert(false);
            }
            for (auto [id, color] : m.view(1))
            {
                assert(false);
            }

            // NOTE: 初始化，填数据到使用的内存
            auto e = m.allocate(); //占用一个内容
            assert(e && m.size() == 1);
            auto s = m.get_slot(*e);
            std::construct_at(m.id.data() + s, 42);
            std::construct_at(m.color[0].data() + s, 0.5f);
            std::construct_at(m.color[1].data() + s, 0.6f);
            std::construct_at(m.color[2].data() + s, 0.7f);

            for (auto turp : m.view(1))
            {
                auto [id, color] = turp;
                static_assert(std::is_reference_v<decltype(id)>);
                assert(id == 42);
                assert(bool(color == 0.6F));
            }
            for (auto [id, color] : m.view(2))
            {
                assert(id == 42);
                static_assert(std::is_reference_v<decltype(id)>);
                assert(bool(color == 0.7F));
            }
            // update:
            for (auto [id, color] : m.view(2))
            {
                id = 0;
                color = 0;
            }
            for (auto [id, color] : m.view(2))
            {
                assert(id == 0);
                assert(bool(color == 0));
            }
        }

        // 离开作用域自动析构
    }
    // --- 测试 tie() 结构化绑定 ---
    {
        using Store = gen_soa_aggregate<{"x", ^^int}, {"y", ^^double}>;
        Store store(10);
        auto e0 = store.allocate();
        auto e1 = store.allocate();
        auto s0 = store.get_slot(*e0);
        auto s1 = store.get_slot(*e1);

        // 在对应槽位上构造数据
        std::construct_at(store.x.data() + s0, 10);
        std::construct_at(store.y.data() + s0, 1.5);
        std::construct_at(store.x.data() + s1, 20);
        std::construct_at(store.y.data() + s1, 2.5);

        // 1. 按名称绑定 tie<"x","y">()
        {
            auto t = store.tie<"x", "y">();
            // 检查返回类型：tuple<soa_member_pointer<int>&, soa_member_pointer<double>&>
            static_assert(
                std::is_same_v<decltype(t), std::tuple<soa_member_pointer<int> &,
                                                       soa_member_pointer<double> &>>);
            auto [x_field, y_field] = t; // 结构化绑定，获得字段指针的引用

            // 修改 x_field 指向的数据，应反映到原 store
            x_field[s0] = 100;
            assert(store.x[s0] == 100);

            y_field[s1] = 9.9;
            assert(store.y[s1] == 9.9);
        }

        // 2. 按索引绑定 tie<0,1>()
        {
            auto [x_field, y_field] = store.tie<0, 1>();
            static_assert(std::is_same_v<decltype(x_field), soa_member_pointer<int> &>);
            x_field[s1] = 200;
            assert(store.x[s1] == 200);
        }

        // 3. 全字段绑定 tie() (将所有字段打包成 tie)
        {
            auto [x_field, y_field] = store.tie();
            static_assert(std::is_same_v<decltype(x_field), soa_member_pointer<int> &>);
            y_field[s0] = 3.14;
            assert(store.y[s0] == 3.14);
        }

        // 4. 对包含数组成员字段的 tie 测试
        {
            using StoreArr = gen_soa_aggregate<{"pos", ^^float, 3}>;
            StoreArr sa(5);
            auto e = sa.allocate();
            auto slot = sa.get_slot(*e);
            // 构造数据：三个分量各设置一个值
            std::construct_at(sa.pos[0].data() + slot, 1.0f);
            std::construct_at(sa.pos[1].data() + slot, 2.0f);
            std::construct_at(sa.pos[2].data() + slot, 3.0f);

            // tie<"pos"> 得到整个数组的引用
            auto [pos_arr] = sa.tie<"pos">();
            static_assert(std::is_same_v<decltype(pos_arr),
                                         std::array<soa_member_pointer<float>, 3> &>);

            // 通过数组引用修改底层数据
            pos_arr[0][slot] = 10.0f;
            assert(sa.pos[0][slot] == 10.0f);
        }
    }
    {
        using Store = gen_soa_aggregate<{"id", ^^int}, {"color", ^^float, 3}>;
        constexpr auto cnt = Store::field_count_from_tie_type<
            decltype(std::declval<Store &>().tie<"id", "color">())>();
        // cnt == {1, 3}
        static_assert(std::ranges::equal(cnt, std::array{1, 3}));

        {
            constexpr auto cnt = Store::field_count_from_tie_type<
                decltype(std::declval<Store &>().tie<"color">())>();
            static_assert(std::ranges::equal(cnt, std::array{3}));
        }
    }
    // --- 测试 construct_entity(entity, args...) ---
    {
        // 1. 单字段 (field_count = 1)
        using Store1 = gen_soa_aggregate<{"a", ^^int}>;
        Store1 s1(5);
        auto e1 = s1.allocate();
        assert(e1.has_value());
        s1.construct_entity(*e1, 42);
        size_t slot1 = s1.get_slot(*e1);
        assert(s1.a[slot1] == 42);

        // 2. 多字段，均为普通类型
        using Store2 = gen_soa_aggregate<{"x", ^^int}, {"y", ^^double}>;
        Store2 s2(5);
        auto e2 = s2.allocate();
        s2.construct_entity(*e2, 10, 3.14);
        size_t slot2 = s2.get_slot(*e2);
        assert(s2.x[slot2] == 10);
        assert(s2.y[slot2] == 3.14);

        // 3. 单个数组字段 (field_count = 3)
        using Store3 = gen_soa_aggregate<{"color", ^^float, 3}>;
        Store3 s3(5);
        auto e3 = s3.allocate();
        s3.construct_entity(*e3, std::array{0.5f, 0.5f, 0.5f}); // 所有三个分量都设为 0.5
        size_t slot3 = s3.get_slot(*e3);
        assert(s3.color[0][slot3] == 0.5f);
        assert(s3.color[1][slot3] == 0.5f);
        assert(s3.color[2][slot3] == 0.5f);

        // 4. 混合：一个普通字段 + 一个数组字段
        using Store4 = gen_soa_aggregate<{"id", ^^int}, {"vec", ^^float, 2}>;
        Store4 s4(5);
        auto e4 = s4.allocate();
        s4.construct_entity(*e4, 100,
                            std::array{9.9f, 9.9f}); // id=100, vec[0]=9.9, vec[1]=9.9
        size_t slot4 = s4.get_slot(*e4);
        assert(s4.id[slot4] == 100);
        assert(s4.vec[0][slot4] == 9.9f);
        assert(s4.vec[1][slot4] == 9.9f);

        // 5. 确保参数数量必须等于字段总数，否则编译失败（无需显式验证）
        // 下面这条语句如果取消注释，应该无法编译：
        // s4.construct_entity(*e4, 1);  // 字段数=2，只给1个参数 → 错误
    }

    // ---------- 测试 is_noexcept_construct ----------
    {
        // 1. 简单类型构造都是 noexcept
        using Store1 = gen_soa_aggregate<{"a", ^^int}>;
        static_assert(Store1::is_noexcept_construct<int>());
        static_assert(Store1::is_noexcept_construct<int &&>()); // 转发引用不影响

        using Store2 = gen_soa_aggregate<{"x", ^^int}, {"y", ^^double}>;
        static_assert(Store2::is_noexcept_construct<int, double>());

        // 2. 数组字段同样检查底层类型
        using Store3 = gen_soa_aggregate<{"color", ^^float, 3}>;
        static_assert(Store3::is_noexcept_construct<std::array<float, 3>>());

        using Store4 = gen_soa_aggregate<{"id", ^^int}, {"vec", ^^float, 2}>;
        static_assert(Store4::is_noexcept_construct<int, std::array<float, 2>>());

        // 3. 如果类型构造可能抛异常，is_noexcept_construct 应为 false
        struct MayThrow
        {
            MayThrow(int) noexcept(false) {}
        };
        using Store5 = gen_soa_aggregate<{"mt", ^^MayThrow}>;
        static_assert(!Store5::is_noexcept_construct<int>());
    }

    // ===== 扩容测试 =====

    return true;
}

struct ThrowOnConstruct
{
    int value;
    static inline int constructed = 0;
    static inline int destroyed = 0;

    ThrowOnConstruct(int v) noexcept(false) : value(v)
    {
        if (v < 0)
            throw std::runtime_error("negative");
        ++constructed;
    }
    ~ThrowOnConstruct()
    {
        ++destroyed;
    }
    ThrowOnConstruct(ThrowOnConstruct &&o) noexcept : value(o.value)
    {
        o.value = 0;
        ++constructed; // 移动构造创建了新对象
    }
    ThrowOnConstruct &operator=(ThrowOnConstruct &&o) noexcept
    {
        if (this != &o)
        {
            value = o.value;
            o.value = 0;
            // 移动赋值不改变对象数量
        }
        return *this;
    }
    friend bool operator==(const ThrowOnConstruct &lhs, int rhs)
    {
        return lhs.value == rhs;
    }
};

template <typename E, typename FnType = void() noexcept>
    requires std::is_enum_v<E>
class StageScheduler
{
    using underlying = std::underlying_type_t<E>;
    static constexpr auto infos =
        std::define_static_array(std::meta::enumerators_of(^^E));

  public:
    using Callback = std::move_only_function<FnType>;

    constexpr void add(E s, Callback cb)
    {
        lists_[static_cast<underlying>(s)].push_back(std::move(cb));
    }
    constexpr void run(E stage)
    {
        auto it = lists_.find(static_cast<underlying>(stage));
        if (it != lists_.end())
        {
            for (auto &cb : it->second)
                cb();
        }
    }

    constexpr void run_all()
    {
        template for (constexpr auto I : std::ranges::views::indices(infos.size()))
        {
            constexpr E val = std::meta::extract<E>(infos[I]);
            auto it = lists_.find(std::to_underlying(val));
            if (it != lists_.end())
            {
                for (auto &cb : it->second)
                    cb();
            }
        }
    }

  private:
    std::flat_map<underlying, std::vector<Callback>> lists_;
};

constexpr bool gen_soa_aggregate_new_entity_test()
{
    // ===== 1. 单字段 (field_count = 1) =====
    {
        using Store = gen_soa_aggregate<{"a", ^^int}>;
        Store s(3);
        auto e = s.new_entity(42);
        assert(s.size() == 1);
        size_t slot = s.get_slot(e);
        assert(s.a[slot] == 42);
        // 再次分配
        auto e2 = s.new_entity(99);
        assert(s.size() == 2);
        size_t slot2 = s.get_slot(e2);
        assert(s.a[slot2] == 99);
        assert(s.a[slot] == 42); // 第一个实体不受影响
    }

    // ===== 2. 多字段，均为普通类型 =====
    {
        using Store = gen_soa_aggregate<{"x", ^^int}, {"y", ^^double}>;
        Store s(5);
        auto e = s.new_entity(10, 3.14);
        size_t slot = s.get_slot(e);
        assert(s.x[slot] == 10);
        assert(s.y[slot] == 3.14);
    }

    // ===== 3. 单个数组字段 (field_count = 3) =====
    {
        using Store = gen_soa_aggregate<{"color", ^^float, 3}>;
        Store s(5);
        // 必须传入 std::array<float, 3>
        auto e = s.new_entity(std::array<float, 3>{0.1f, 0.2f, 0.3f});
        size_t slot = s.get_slot(e);
        assert(s.color[0][slot] == 0.1f);
        assert(s.color[1][slot] == 0.2f);
        assert(s.color[2][slot] == 0.3f);
    }

    // ===== 4. 混合：普通字段 + 数组字段 =====
    {
        using Store = gen_soa_aggregate<{"id", ^^int}, {"vec", ^^float, 2}>;
        Store s(5);
        auto e = s.new_entity(100, std::array<float, 2>{9.9f, 8.8f});
        size_t slot = s.get_slot(e);
        assert(s.id[slot] == 100);
        assert(s.vec[0][slot] == 9.9f);
        assert(s.vec[1][slot] == 8.8f);
    }

    // ===== 5. 容量耗尽时抛出 std::bad_alloc =====
    {
        using Store = gen_soa_aggregate<{"a", ^^int}>;
        Store s(2);
        s.new_entity(1);
        s.new_entity(2);
        bool threw = false;
        try
        {
            s.new_entity(3); // 容量满，应抛出
        }
        catch (const std::bad_alloc &)
        {
            threw = true;
        }
        assert(threw);
    }

    // ===== 6. 多个数组字段同时构造 =====
    {
        using Store = gen_soa_aggregate<{"pos", ^^float, 3}, {"vel", ^^float, 3}>;
        Store s(5);
        auto e = s.new_entity(std::array<float, 3>{1.0f, 2.0f, 3.0f},
                              std::array<float, 3>{0.1f, 0.2f, 0.3f});
        size_t slot = s.get_slot(e);
        for (int i = 0; i < 3; ++i)
        {
            assert(s.pos[i][slot] == (i + 1) * 1.0f);
            assert(s.vel[i][slot] == (i + 1) * 0.1f);
        }
    }

    // ===== 7. 移动语义测试（右值数组应移动而非拷贝） =====
    {
        // 使用一个可探测移动次数的类型（简化：使用 int，无法直接探测，但可以验证值正确）
        using Store = gen_soa_aggregate<{"arr", ^^int, 2}>;
        Store s(2);
        std::array<int, 2> src{7, 8};
        auto e = s.new_entity(std::move(src)); // 右值
        size_t slot = s.get_slot(e);
        assert(s.arr[0][slot] == 7);
        assert(s.arr[1][slot] == 8);
        // 如果源数组元素被移动（对于 int 无影响），但至少不会拷贝源对象两次
    }

    // ===== 8. 参数数量必须与字段总数匹配（编译期检查，此处省略） =====
    // 错误调用示例（取消注释将无法编译）:
    // s.new_entity(1, 2, 3); // 字段总数不匹配

    // ===== 测试 view_entity (单实体视图) =====
    {
        using Store = gen_soa_aggregate<{"id", ^^int}, {"value", ^^double}>;
        Store store(5);
        auto e0 = store.new_entity(1, 1.1);
        auto e1 = store.new_entity(2, 2.2);

        // 1. 获取 entity 的视图，并读取数据
        auto v0 = store.view_entity<"id", "value">(0, e0);
        // v0 应该是一个聚合，包含两个引用成员，名称分别对应字段名
        static_assert(std::is_reference_v<decltype(v0.id)>);
        static_assert(std::is_reference_v<decltype(v0.value)>);
        assert(v0.id == 1);
        assert(v0.value == 1.1);

        // 2. 通过视图修改值，应反映回原存储
        v0.id = 10;
        v0.value = 10.1;
        assert(store.id[store.get_slot(e0)] == 10);
        assert(store.value[store.get_slot(e0)] == 10.1);

        // 3. 另一个实体视图，互不干扰
        auto v1 = store.view_entity<"id", "value">(0, e1);
        assert(v1.id == 2);
        assert(v1.value == 2.2);

        // 4. 只选择单个字段的视图
        auto v_id_only = store.view_entity<"id">(0, e0);
        static_assert(std::is_reference_v<decltype(v_id_only.id)>);
        assert(v_id_only.id == 10);
        v_id_only.id = 100;
        assert(store.id[store.get_slot(e0)] == 100);

        // 5. 测试多分量数组字段的 view_entity（指定子索引）
        using VecStore = gen_soa_aggregate<{"pos", ^^float, 3}, {"vel", ^^float, 3}>;
        VecStore vs(5);
        auto ve0 =
            vs.new_entity(std::array{1.0f, 2.0f, 3.0f}, std::array{0.1f, 0.2f, 0.3f});
        // 查看 pos 的 y 分量和 vel 的 y 分量（field_count=1 表示取第二分量）
        auto v_y = vs.view_entity<"pos", "vel">(1, ve0);
        // 由于 pos 和 vel 都是 3 分量，取索引 1 得到各自的 y 分量
        // 类型应该是 float&
        static_assert(std::is_reference_v<decltype(v_y.pos)>);
        static_assert(std::is_reference_v<decltype(v_y.vel)>);
        assert(v_y.pos == 2.0f);
        assert(v_y.vel == 0.2f);
        // 修改 y 分量
        v_y.pos = 20.0f;
        v_y.vel = 2.0f;
        // 验证原存储对应位置
        size_t slot_v = vs.get_slot(ve0);
        assert(vs.pos[1][slot_v] == 20.0f);
        assert(vs.vel[1][slot_v] == 2.0f);

        // 6. const 实体视图（只读）
        const Store &cstore = store;
        auto cv = cstore.view_entity<"id", "value">(0, e1);
        static_assert(std::is_reference_v<decltype(cv.id)>);
        static_assert(std::is_const_v<std::remove_reference_t<decltype(cv.id)>>);
        // 编译期验证不可修改：若取消下面注释应编译失败
        // cv.id = 5;
        assert(cv.id == 2);
        assert(cv.value == 2.2);

        {
            auto [id, val] = store.view_entity<"id", "value">(0, e0);
            static_assert(std::is_reference_v<decltype(cv.id)>);
            static_assert(std::is_reference_v<decltype(id)>);
        }
    }

    return true;
}
// 此测试放在 main 中执行（非 constexpr）
constexpr bool gen_soa_aggregate_runtime_test()
{
    // 使用已有的 ThrowOnConstruct（需保证移动构造为 noexcept）
    // 注意：ThrowOnConstruct 的移动构造是 noexcept，满足 new_entity 的静态断言
    using Store = gen_soa_aggregate<{"x", ^^ThrowOnConstruct}>;
    Store s(5);

    // 记录初始计数（这里借用 static 计数器，但需要手动重置）
    ThrowOnConstruct::constructed = 0;
    ThrowOnConstruct::destroyed = 0;

    // 正常分配一个实体
    auto e1 = s.new_entity(42);
    assert(s.size() == 1);
    assert(s.x[s.get_slot(e1)].value == 42);
    int c1 = ThrowOnConstruct::constructed;
    int d1 = ThrowOnConstruct::destroyed;
    assert(c1 > d1); // 有一个存活对象

    // 尝试分配一个会抛异常的实体（-1 触发异常）
    bool caught = false;
    try
    {
        s.new_entity(-1);
    }
    catch (const std::runtime_error &)
    {
        caught = true;
    }
    assert(caught);
    // 构造和析构计数应保持不变（临时对象被销毁，槽位未被修改）
    assert(ThrowOnConstruct::constructed == c1);
    assert(ThrowOnConstruct::destroyed == d1);
    // 实体数不变
    assert(s.size() == 1);
    assert(s.alive(e1));

    // 再次正常分配
    auto e2 = s.new_entity(100);
    assert(s.size() == 2);
    assert(s.x[s.get_slot(e2)].value == 100);

    // 释放所有实体
    s.release_entity(e1);
    s.release_entity(e2);
    // 构造与析构应平衡
    assert(ThrowOnConstruct::constructed == ThrowOnConstruct::destroyed);

    // 测试多字段且包含数组的异常安全
    {
        using Store2 = gen_soa_aggregate<{"id", ^^ThrowOnConstruct}, {"vec", ^^float, 2}>;
        Store2 s2(5);
        ThrowOnConstruct::constructed = 0;
        ThrowOnConstruct::destroyed = 0;
        auto e = s2.new_entity(10, std::array<float, 2>{1.0f, 2.0f});
        size_t slot = s2.get_slot(e);
        assert(s2.id[slot].value == 10);
        assert(s2.vec[0][slot] == 1.0f && s2.vec[1][slot] == 2.0f);

        // 构造失败回滚：id 字段抛异常（值 -1）
        bool caught2 = false;
        try
        {
            s2.new_entity(-1, std::array<float, 2>{3.0f, 4.0f});
        }
        catch (const std::runtime_error &)
        {
            caught2 = true;
        }
        assert(caught2);
        assert(s2.size() == 1); // 实体数不变
        assert(ThrowOnConstruct::constructed ==
               ThrowOnConstruct::destroyed + 1); // 仅一个活对象
    }

    return true;
}

// move‑only 类型：只移动，不可拷贝，移动为 noexcept
struct MoveOnly
{
    int *ptr;
    constexpr MoveOnly(int v) : ptr(new int(v)) {}
    constexpr MoveOnly(MoveOnly &&o) noexcept : ptr(o.ptr)
    {
        o.ptr = nullptr;
    }
    constexpr MoveOnly &operator=(MoveOnly &&o) noexcept
    {
        if (this != &o)
        {
            delete ptr;
            ptr = o.ptr;
            o.ptr = nullptr;
        }
        return *this;
    }
    constexpr ~MoveOnly()
    {
        delete ptr;
    }
    constexpr MoveOnly(const MoveOnly &) = delete;
    constexpr MoveOnly &operator=(const MoveOnly &) = delete;
    constexpr bool operator==(int v) const
    {
        return ptr && *ptr == v;
    }
};

// 辅助 move‑only 数组类型（用于多列字段的每个子列）
struct MoveOnlyArrayElement
{
    std::unique_ptr<int> data;
    constexpr MoveOnlyArrayElement(int v) : data(std::make_unique<int>(v)) {}
    constexpr MoveOnlyArrayElement(MoveOnlyArrayElement &&) noexcept = default;
    constexpr MoveOnlyArrayElement &operator=(MoveOnlyArrayElement &&) noexcept = default;
    constexpr bool operator==(int v) const
    {
        return data && *data == v;
    }
};
constexpr bool gen_soa_aggregate_moveonly_test()
{
    // 1. 单 move‑only 字段
    {
        using Store = gen_soa_aggregate<{"mo", ^^MoveOnly}>;
        Store s(3);
        auto e = s.new_entity(MoveOnly(42)); // 右值
        size_t slot = s.get_slot(e);
        assert(s.mo[slot] == 42);
    }

    // 2. 混合：一个普通 int + 一个 move‑only 字段
    {
        using Store = gen_soa_aggregate<{"id", ^^int}, {"val", ^^MoveOnly}>;
        Store s(3);
        auto e = s.new_entity(10, MoveOnly(99));
        size_t slot = s.get_slot(e);
        assert(s.id[slot] == 10);
        assert(s.val[slot] == 99);
    }

    // 3. 单个数组字段，元素类型为 move‑only (field_count=2)
    {
        using Store = gen_soa_aggregate<{"pair", ^^MoveOnlyArrayElement, 2}>;
        Store s(3);
        // 必须传递 std::array<MoveOnlyArrayElement, 2>
        auto e = s.new_entity(std::array<MoveOnlyArrayElement, 2>{
            MoveOnlyArrayElement(111), MoveOnlyArrayElement(222)});
        size_t slot = s.get_slot(e);
        assert(s.pair[0][slot] == 111);
        assert(s.pair[1][slot] == 222);
    }

    // 4. 混合：一个普通 int + 一个 move‑only 数组字段
    {
        using Store =
            gen_soa_aggregate<{"id", ^^int}, {"vec", ^^MoveOnlyArrayElement, 3}>;
        Store s(3);
        auto e = s.new_entity(7, std::array<MoveOnlyArrayElement, 3>{
                                     MoveOnlyArrayElement(1), MoveOnlyArrayElement(2),
                                     MoveOnlyArrayElement(3)});
        size_t slot = s.get_slot(e);
        assert(s.id[slot] == 7);
        for (int j = 0; j < 3; ++j)
            assert(s.vec[j][slot] == j + 1);
    }

    // 5. 多个 move‑only 字段（非数组+数组）
    {
        using Store =
            gen_soa_aggregate<{"a", ^^MoveOnly}, {"b", ^^MoveOnlyArrayElement, 2}>;
        Store s(3);
        auto e = s.new_entity(MoveOnly(100),
                              std::array<MoveOnlyArrayElement, 2>{
                                  MoveOnlyArrayElement(200), MoveOnlyArrayElement(300)});
        size_t slot = s.get_slot(e);
        assert(s.a[slot] == 100);
        assert(s.b[0][slot] == 200);
        assert(s.b[1][slot] == 300);
    }

    // 6. 验证被移动后的源对象不会影响目标（移动后原 unique_ptr 为空，但目标正常）
    {
        using Store = gen_soa_aggregate<{"mo", ^^MoveOnly}>;
        Store s(1);
        MoveOnly source(500);
        auto e = s.new_entity(std::move(source)); // 移动构造到槽位
        assert(s.mo[s.get_slot(e)] == 500);
        // source 被移走，ptr 应为空（这是 move‑only 的正常行为）
        assert(source.ptr == nullptr);
    }

    return true;
}

constexpr bool noexcept_test()
{
    using Store2 = gen_soa_aggregate<{"x", ^^int}, {"y", ^^double}>;
    static_assert(Store2::is_noexcept_construct<int, double>());

    using StoreArr = gen_soa_aggregate<{"color", ^^float, 3}>;
    // 传入 std::array<float,3>，构造 float 是 noexcept 的
    static_assert(StoreArr::is_noexcept_construct<std::array<float, 3>>());

    // 验证 construct_entity 的 noexcept 是否正确（通过编译期表达式）
    using Store = gen_soa_aggregate<{"a", ^^int}>;
    StoreArr s{5};
    static_assert(noexcept(s.construct_entity(0, std::array<float, 3>{1, 2, 3})) ==
                  StoreArr::is_noexcept_construct<std::array<float, 3> &&>());
    return true;
}

int main()
try
{
    static_assert(gen_soa_aggregate_test());
    static_assert(noexcept_test());

    static_assert(gen_soa_aggregate_new_entity_test()); // 编译期测试
    assert(gen_soa_aggregate_runtime_test());           // 运行时异常安全测试
    static_assert(gen_soa_aggregate_moveonly_test());   // move‑only 测试

    {
        enum class Stage
        {
            Input,
            Update,
            Physics,
            Render
        };
        StageScheduler<Stage> scheduler;
        // NOTE: 小对象优化？
        static_assert(sizeof(StageScheduler<Stage>::Callback) == 40);

        float dt = 0.016f;
        int result = 0;

        // 注册系统
        scheduler.add(Stage::Input, [&] noexcept { result += 1; });
        scheduler.add(Stage::Physics, [&] noexcept { result *= 2; });
        scheduler.add(Stage::Render, [&] noexcept { result += 100; });

        // 单阶段运行
        scheduler.run(Stage::Input);
        assert(result == 1);

        // 运行全部
        scheduler.run_all(); // Input → Update(无) → Physics → Render → Count(无)
        // result = 1 (初始) + 1 (Input) = 2; *2 (Physics) = 4; +100 (Render) = 104
        assert(result == 104);
    }

    {
        std::flat_map<static_string, std::string> fm;
        fm["1"] = "one";
        fm["3"] = "three";
        fm["2"] = "two"; // 自动排序：1,2,3

        for (const auto &[k, v] : fm) // 顺序遍历，内存连续，极快
            std::cout << k.view() << ": " << v << '\n';

        auto it = fm.find("2"); // 二分查找，缓存友好

        auto world = std::constant_wrapper<[](auto world) {

        }>{};
    }

    std::cout << "main done\n";
    return 0;
}
catch (const std::exception &e)
{
    std::cerr << "Exception: " << e.what() << '\n';
    return 1;
}
catch (...)
{
    std::cerr << "Unknown exception\n";
    return 1;
} // NOLINTEND