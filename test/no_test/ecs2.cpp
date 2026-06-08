#include <cassert>
#include <cstdint>
#include <iostream>
#include <exception>
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
    consteval explicit static_string(const char *value) noexcept : value{value} {}
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
template <typename T, template <typename> class Alloc = std::allocator>
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
    allocator_type alloc_{}; // 源分配器

    static constexpr auto ptr_members = std::define_static_array(nsdms_of(^^Pointers));
    static constexpr auto members = std::define_static_array(nsdms_of(^^T));

    // static consteval auto get_field_by_name(static_string name) -> size_type
    // {
    //     size_type idx = ~0u;
    //     for (size_type i{}; i < members.size(); ++i)
    //     {
    //         if (std::meta::identifier_of(members[i]) == name)
    //         {
    //             idx = i;
    //             break;
    //         }
    //     }
    //     if (idx == ~0u)
    //         throw std::meta::exception{"not find field", std::meta::current_function()};
    //     return idx;
    // }

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

    // template <static_string name>
    // constexpr decltype(auto) get(this auto &&self, size_type idx) noexcept
    // {
    //     constexpr auto I = soa_memory::get_field_by_name(name);
    //     return std::forward<decltype(self)>(self).template get<I>(idx);
    // }

    template <size_type I>
    decltype(auto) constexpr field(this auto &&self) noexcept
    {
        constexpr auto member_info = ptr_members[I];
        return std::forward_like<decltype(self)>(self.pointers_.[:member_info:]);
    }

    // template <static_string name>
    // constexpr decltype(auto) field(this auto &&self) noexcept
    // {
    //     constexpr auto I = soa_memory::get_field_by_name(name);
    //     return std::forward<decltype(self)>(self).template field<I>();
    // }

    template <size_type I>
    constexpr decltype(auto) span(this auto &&self) noexcept
    {
        auto &&[... ptrs] = std::forward_like<decltype(self)>(self.pointers_);
        return std::span(ptrs...[I].data(), self.capacity_);
    }
    // template <static_string name>
    // constexpr decltype(auto) span(this auto &&self) noexcept
    // {
    //     auto &&[... ptrs] = std::forward_like<decltype(self)>(self.pointers_);
    //     constexpr auto I = soa_memory::get_field_by_name(name);
    //     return std::span(ptrs...[I].data(), self.capacity_);
    // }

    constexpr decltype(auto) view(this auto &&self) noexcept
    {
        auto &&[... ptrs] = std::forward_like<decltype(self)>(self.pointers_);
        return std::views::zip(std::span{ptrs.data(), self.capacity_}...);
    }
    // template <static_string... name>
    //     requires(sizeof...(name) > 0)
    // constexpr decltype(auto) view(this auto &&self) noexcept
    // {
    //     return std::views::zip(
    //         std::span{std::forward<decltype(self)>(self).template field<name>().data(),
    //                   self.capacity_}...);
    // }

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

struct component_id
{
    //world_id_type 存放执行真正store的 id 需要配合其他元信息才能解析,才能知道存放的是什么
    std::vector<world_id_type> fields;
};

struct world_id_manager final
{
    struct component_id_item
    {
        world_id_version_type version; // 记录每个id的版本，包括空洞
    };
    struct component_id_view
    {
        world_id_type index;
        world_id_version_type version;
    };

    using soa_type = soa_memory<component_id_item>;

  private:
    soa_type data_;                          // 存在空洞，不保证紧密
    std::vector<world_id_type> free_indexs_; // 记录可复用. free_ids[last] -> free_idx

  public:
    constexpr component_id_view get_current_view(world_id_type allocate_idx) noexcept
    {
        assert(allocate_idx < data_.capacity());
        [[assume(allocate_idx < data_.capacity())]];
        return {.index = allocate_idx, .version = data_.get<0>(allocate_idx)};
    }

    constexpr world_id_type allocate()
    {
        [[assume(not free_indexs_.empty())]];

        if (not free_indexs_.empty()) [[likely]]
        {
            auto id = free_indexs_.back();
            free_indexs_.pop_back();
            return id;
        }
        else [[unlikely]]
            throw std::logic_error{"free_indexs_ is empty!"};
    }
    constexpr component_id_view allocate_with_view()
    {
        assert(not free_indexs_.empty());
        [[assume(not free_indexs_.empty())]];

        if (not free_indexs_.empty()) [[likely]]
        {
            auto id = free_indexs_.back();
            free_indexs_.pop_back();
            return get_current_view(id);
        }
        else [[unlikely]]
            throw std::logic_error{"free_indexs_ is empty!"};
    }
    constexpr void release(world_id_type index)
    {
        assert(index < data_.capacity());
        [[assume(not free_indexs_.empty())]];

        auto [version] = data_[index];
        // id.fields.clear();
        ++version;
        free_indexs_.emplace_back(index);
    }
    [[nodiscard]] constexpr bool expire(component_id_view view) const noexcept
    {
        auto [index, version] = view;
        assert(index < data_.capacity());
        [[assume(not free_indexs_.empty())]];

        auto cur_version = data_.get<0>(index);
        return cur_version == version;
    }

    constexpr world_id_manager(world_id_type capacity) : data_{capacity}
    {
        free_indexs_.reserve(capacity);
        for (auto i = capacity; i > 0;)
        {
            free_indexs_.emplace_back(--i);
        }
    }
    ~world_id_manager() = default;
    // ---------- 移动语义 ----------
    constexpr world_id_manager(world_id_manager &&other) noexcept
        : data_(std::move(other.data_)), free_indexs_(std::move(other.free_indexs_))
    {
    }

    constexpr world_id_manager &operator=(world_id_manager &&other) noexcept
    {
        if (this != &other)
        {
            data_ = std::move(other.data_);
            free_indexs_ = std::move(other.free_indexs_);
        }
        return *this;
    }
    world_id_manager(const world_id_manager &other) = delete;
    world_id_manager &operator=(const world_id_manager &other) = delete;

    [[nodiscard]] auto capacity() const noexcept
    {
        return data_.capacity();
    }
    [[nodiscard]] constexpr size_type free_size() const noexcept // NOLINT
    {
        return free_indexs_.size();
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
            new_pointers.[:soa_type::ptr_members[I]:] = soa_member_pointer<field_type>(
                                                          ra.allocate(new_cap));
        }

        // 2. 搬迁数据
        template for (constexpr auto I :
                      std::views::indices(soa_type::ptr_members.size()))
        {
            auto &src = data_.pointers_.[:soa_type::ptr_members[I]:];
            auto &dst = new_pointers.[:soa_type::ptr_members[I]:];
            std::uninitialized_move_n(src.data(), old_cap, dst.data());
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
            ra.deallocate(data_.pointers_.[:soa_type::ptr_members[I]:].data(), old_cap);
        }

        // 4. 更新状态
        data_.capacity_ = new_cap;
        data_.pointers_ = std::move(new_pointers);

        // 5. 扩展 free_ 列表
        for (size_type i = new_cap; i > old_cap; --i)
            free_indexs_.push_back(i - 1);
    }
    constexpr void extend(size_type additionalSize)
    {
        grow(capacity() + additionalSize);
    }
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

int main()
try
{
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

    position_manager positions{
        std::vector<position>{position{0, 1, 2}, position{2, 3, 2}}};
    point_manager points{std::vector<Point>{Point{0, 1}, Point{2, 3}}};

    // component c{.class_name = "position",
    //             .components = {component::component_id_type{0}},
    //             .fns = {reinterpret_cast<void *>(&position_manager::update)}};
    // assert(c.fns[0] == reinterpret_cast<void *>(&position_manager::update));
    // assert(c.component_name == static_string{"position"});
    // assert(bool(positions.position_store[c.components[0]] == position{0, 1, 2}));

    // {
    //     component c{.class_name = "position_and_points",
    //                 .components = {component::component_id_type{0},
    //                                component::component_id_type{1}},
    //                 .fns = {reinterpret_cast<void *>(&position_manager::update),
    //                         reinterpret_cast<void *>(&point_manager::update)}};
    // }

    {
        auto str = "123";
        // static_string s = static_string{str}; //NOTE: 不允许
        static_string s = "123";
        static_string s2 = "123";
        assert(s == s2);
        {
            static_string s = "1234";
            static_string s2 = "1234";
            constexpr auto str2 = "1234";
            static_string s3 = static_string{str2};
            assert(s == s2);
            assert(s == s3); //NOTE: constexpr 和 static 存储的位置不同
        }
        constexpr static_string s3 = "123";
        // static_assert(s3 == s2); //NOTE: 不允许
    }
    static_string class_info;

    // NOTE: world
    int a = 0;
    {
    }

    uint16_t x = 65535;
    ++x;                    // 溢出
    std::cout << x << '\n'; // 输出 0
    assert(x == 0);

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