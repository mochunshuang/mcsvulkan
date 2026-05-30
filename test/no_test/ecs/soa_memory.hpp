#pragma once

#include <array>
#include <cassert>
#include <memory>
#include <span>
#include <ranges>
#include <utility>
#include <tuple>

#include "soa_member_pointer.hpp"
#include "nsdms_spec_transformed.hpp"
#include "nsdms_of.hpp"
#include "static_string.hpp"
#include "size_type.hpp"
#include "attr_no_unique_address.hpp"

template <typename T, template <typename> class Alloc = std::allocator>
// requires(std::is_trivially_copyable_v<T>)
struct soa_memory
{
    using allocator_type = Alloc<T>;
    struct Pointers;
    consteval
    {
        std::meta::define_aggregate(
            ^^Pointers, nsdms_spec_transformed(^^T, [](std::meta::info type) {
              return std::meta::substitute(^^soa_member_pointer, {
                                                                     type});
            }));
    }

    size_type capacity_{};
    Pointers pointers_{};
    ATTR_NO_UNIQUE_ADDRESS allocator_type alloc_{}; // 源分配器

    static constexpr auto ptr_members = nsdms_of(^^Pointers);
    static constexpr auto members = nsdms_of(^^T);

    static consteval auto get_field_by_name(static_string name) -> size_type
    {
        size_type idx = ~0u;
        for (size_type i{}; i < members.size(); ++i)
        {
            if (std::meta::identifier_of(members[i]) == name)
            {
                idx = i;
                break;
            }
        }
        if (idx == ~0u)
            throw std::meta::exception{"not find field", std::meta::current_function()};
        return idx;
    }

  public:
    // 构造函数：只分配内存，不构造
    constexpr explicit soa_memory(size_type capacity)
        : soa_memory(capacity, allocator_type{})
    {
    }
    constexpr soa_memory(size_type capacity, auto alloc)
        : capacity_{capacity}, alloc_{std::move(alloc)}
    {
        if (capacity > 0)
        {
            template for (constexpr auto I : std::views::indices(ptr_members.size()))
            {
                auto &field = pointers_.[:ptr_members[I]:];
                using FieldType = std::remove_reference_t<decltype(field)>::value_type;
                using RebAlloc = typename std::allocator_traits<
                    allocator_type>::template rebind_alloc<FieldType>;
                RebAlloc reb_alloc(alloc_);
                auto *new_data = reb_alloc.allocate(capacity);
                field = soa_member_pointer<FieldType>(new_data);
            }
        }
    }

    // 容量相关
    constexpr size_type capacity() const noexcept
    {
        return capacity_;
    }

    // 元素访问（按成员下标 I 和元素索引 idx）
    template <size_type I>
    decltype(auto) constexpr get(this auto &&self, size_type idx) noexcept
    {
        constexpr auto member_info = ptr_members[I];
        return std::forward_like<decltype(self)>(self.pointers_.[:member_info:][idx]);
    }

    template <static_string name>
    constexpr decltype(auto) get(this auto &&self, size_type idx) noexcept
    {
        constexpr auto I = soa_memory::get_field_by_name(name);
        return std::forward<decltype(self)>(self).template get<I>(idx);
    }

    template <size_type I>
    decltype(auto) constexpr field(this auto &&self) noexcept
    {
        constexpr auto member_info = ptr_members[I];
        return std::forward_like<decltype(self)>(self.pointers_.[:member_info:]);
    }

    template <static_string name>
    constexpr decltype(auto) field(this auto &&self) noexcept
    {
        constexpr auto I = soa_memory::get_field_by_name(name);
        return std::forward<decltype(self)>(self).template field<I>();
    }

    template <size_type I>
    constexpr decltype(auto) span(this auto &&self) noexcept
    {
        auto &&[... ptrs] = std::forward_like<decltype(self)>(self.pointers_);
        return std::span(ptrs...[I].data(), self.capacity_);
    }
    template <static_string name>
    constexpr decltype(auto) span(this auto &&self) noexcept
    {
        auto &&[... ptrs] = std::forward_like<decltype(self)>(self.pointers_);
        constexpr auto I = soa_memory::get_field_by_name(name);
        return std::span(ptrs...[I].data(), self.capacity_);
    }

    constexpr decltype(auto) view(this auto &&self) noexcept
    {
        auto &&[... ptrs] = std::forward_like<decltype(self)>(self.pointers_);
        return std::views::zip(std::span{ptrs.data(), self.capacity_}...);
    }
    template <static_string... name>
        requires(sizeof...(name) > 0)
    constexpr decltype(auto) view(this auto &&self) noexcept
    {
        return std::views::zip(
            std::span{std::forward<decltype(self)>(self).template field<name>().data(),
                      self.capacity_}...);
    }

    constexpr decltype(auto) begin(this auto &&self) noexcept
    {
        return std::forward<decltype(self)>(self).view().begin();
    }
    constexpr decltype(auto) end(this auto &&self) noexcept
    {
        return std::forward<decltype(self)>(self).view().end();
    }

    // NOTE: 没有 size 无法 std::destroy
    constexpr void destroy() noexcept
    {
        if (capacity_ == 0)
            return;
        template for (constexpr auto I :
                      std::views::indices(ptr_members.size()) | std::views::reverse)
        {
            auto &field = pointers_.[:ptr_members[I]:];
            using FieldType = std::remove_reference_t<decltype(field)>::value_type;
            using RebAlloc = typename std::allocator_traits<
                allocator_type>::template rebind_alloc<FieldType>;
            RebAlloc reb_alloc(alloc_);
            reb_alloc.deallocate(field.data(), capacity_);
            field = {};
        }
        capacity_ = 0;
    }

    constexpr ~soa_memory() noexcept
    {
        destroy();
    }

    constexpr soa_memory(const soa_memory &o)
        : capacity_{o.capacity_}, pointers_{},
          alloc_{std::allocator_traits<
              allocator_type>::select_on_container_copy_construction(o.alloc_)}
    {
        if (capacity_ > 0)
        {
            template for (constexpr auto I : std::views::indices(ptr_members.size()))
            {
                auto &field = pointers_.[:ptr_members[I]:];
                using U = std::remove_cvref_t<decltype(field)>::value_type;
                using RebAlloc = typename std::allocator_traits<
                    allocator_type>::template rebind_alloc<U>;

                RebAlloc reb_alloc(alloc_);
                auto *new_data = reb_alloc.allocate(capacity_);
                field = soa_member_pointer<U>(new_data);
            }
        }
    }
    constexpr soa_memory &operator=(const soa_memory &o)
    {
        //copy‑and‑swap
        if (this != &o)
        {
            auto tmp(o);            // 调用拷贝构造
            *this = std::move(tmp); // 调用移动赋值 operator=(soa_memory&&)
        }
        return *this;
    }

    constexpr soa_memory(soa_memory &&o) noexcept
        : capacity_{std::exchange(o.capacity_, 0)}, pointers_{std::move(o.pointers_)},
          alloc_{std::move(o.alloc_)}
    {
    }
    constexpr soa_memory &operator=(soa_memory &&o) noexcept
    {
        if (this != &o)
        {
            destroy();
            capacity_ = std::exchange(o.capacity_, 0);
            // 逐成员移动赋值：pointers_ 和 alloc_
            // 注意：polymorphic_allocator 的移动赋值被删除！
            // 因此改用 destroy + construct
            std::destroy_at(&pointers_);
            std::construct_at(&pointers_, std::move(o.pointers_));
            std::destroy_at(&alloc_);
            std::construct_at(&alloc_, std::move(o.alloc_));
        }
        return *this;
    }

    constexpr void destroy_at(size_type id) noexcept
    {
        template for (constexpr auto I : std::views::indices(ptr_members.size()))
        {
            auto &field = pointers_.[:ptr_members[I]:];
            std::destroy_at(field.data() + id);
        }
    }

    // 元素级构造（placement new）
    template <size_type I, typename... Args>
    constexpr void construct_field(size_type idx, Args &&...args) noexcept(
        std::is_nothrow_constructible_v<
            typename std::remove_reference_t<
                decltype(pointers_.[:ptr_members[I]:])>::value_type,
            Args...>)
    {
        auto &field = pointers_.[:ptr_members[I]:];
        std::construct_at(field.data() + idx, std::forward<Args>(args)...);
    }
    template <typename... Args>
    constexpr void construct_at(size_type idx, Args &&...args) noexcept(
        std::is_nothrow_constructible_v<T, Args...>)
    {
        template for (constexpr auto I : std::views::indices(ptr_members.size()))
        {
            construct_field<I>(idx, std::forward<Args...[I]>(args...[I]));
        }
    }
    constexpr void construct_at(size_type idx,
                                T t) noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        template for (constexpr auto I : std::views::indices(ptr_members.size()))
        {
            auto &field = t.[:members[I]:];
            construct_field<I>(idx, std::move(field));
        }
    }

    constexpr auto operator[](this auto &&self, size_type idx) noexcept
    {
        assert(idx < self.capacity());
        auto [... ptrs] = self.pointers_;
        struct bind_result;
        consteval
        {
            constexpr std::array<std::meta::info, members.size()> type_array{
                (^^decltype(std::forward_like<decltype(self)>(ptrs[idx])))...};
            std::vector<std::meta::info> specs_;
            for (int i = 0; i < members.size(); ++i)
            {
                specs_.push_back(std::meta::data_member_spec(
                    type_array[i], {.name = std::meta::identifier_of(members[i])}));
            }
            std::meta::define_aggregate(^^bind_result, specs_);
        }
        return bind_result(std::forward_like<decltype(self)>(ptrs[idx])...);
    }
};