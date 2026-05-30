#include <array>
#include <assert.h>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <exception>
#include <memory>
#include <optional>
#include <print>
#include <ranges>
#include <meta>

#include <cassert>
#include <set>
#include <string>
#include <memory_resource>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <algorithm>
#include <vector>

// NOLINTBEGIN

struct empty
{
};
static_assert(sizeof(empty) == 1);

struct my_empty
{
    [[no_unique_address]] std::allocator<int> alloc;
};
static_assert(sizeof(my_empty) == 1);
static_assert(sizeof(std::allocator<int>) == 1);
struct int_empty
{
    int a;
    [[no_unique_address]] std::allocator<int> alloc;
};
static_assert(sizeof(int_empty) == sizeof(int));

struct static_string // NOLINT
{
    const char *value{}; // NOLINT

    template <size_t N>
    consteval static_string(const char (&str)[N]) noexcept // NOLINT
        : value{std::define_static_string(str)}
    {
    }
    consteval explicit static_string(const char *p) noexcept : value{p} {}
    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return std::string_view{value};
    }
    constexpr bool operator==(const static_string &o) const noexcept
    {
        return view() == o;
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
};

struct name_spec // NOLINT
{
    static_string name;
    std::meta::info info;
};

// 应该存放的是类别
static consteval auto nsdms_of(std::meta::info info)
{
    return std::define_static_array(
        std::meta::nonstatic_data_members_of(info, std::meta::access_context::current()));
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
class soa_member_pointer
{
  public:
    using value_type = T;

    constexpr soa_member_pointer() noexcept : data_{nullptr} {}
    constexpr explicit soa_member_pointer(T *data) noexcept : data_{data} {}

    constexpr decltype(auto) operator[](this auto &&self, size_t idx) noexcept
    {
        return std::forward_like<decltype(self)>(self.data_[idx]);
    }

    // forward_like 适用场景：转发"对象引用"
    // forward_like 不适用场景：转发"裸指针"（需要穿透 const 到指向类型时）
    // 如果 T* 需要变成 const T*，请手动处理，不要用 forward_like。

    //NOTE: forward_like 无法穿透指针的间接层。 因此用错了，就是错误的来源
    // data() - 转发指针 ❌
    // return std::forward_like<decltype(self)>(self.data_);
    //                                        ^^^^^^^^^^
    //                                        int* — 指针类型
    // NOTE: 下面是错误的的实践
    // auto data(this auto &&self) noexcept
    // {
    //     return std::forward_like<decltype(self)>(self.data_);
    // }

    const T *data() const noexcept
    {
        return data_;
    }
    T *data() noexcept
    {
        return data_;
    }

  private:
    T *data_;
};

using size_type = uint32_t;
namespace detail
{
    constexpr static doubleIncrease(size_type capactity) noexcept
    {
        return capactity * 2;
    };
}; // namespace detail

template <typename T, template <typename> class Alloc = std::allocator>
struct SoaVector
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
    [[no_unique_address]] allocator_type alloc_{}; // 源分配器

    static_assert(std::is_standard_layout_v<Pointers>, "check Pointers faild.");

    static constexpr auto ptr_members = nsdms_of(^^Pointers);
    static constexpr auto members = nsdms_of(^^T);

    // grow_field 不再移动元素，只分配新内存并释放旧内存（移动已交给 gen_soa_store）
    template <typename U>
    constexpr void grow_field(soa_member_pointer<U> &field, size_type old_cap,
                              size_type new_cap)
    {
        using RebAlloc =
            typename std::allocator_traits<allocator_type>::template rebind_alloc<U>;
        RebAlloc reb_alloc(alloc_);
        auto *old_data = field.data();
        auto *new_data = reb_alloc.allocate(new_cap);
        if (old_data)
            reb_alloc.deallocate(old_data, old_cap);
        field = soa_member_pointer<U>(new_data);
    }

    constexpr void grow(size_type new_cap)
    {
        if (new_cap <= capacity_)
            return;

        size_type old_cap = capacity_;
        capacity_ = new_cap;

        template for (constexpr auto I : std::views::indices(ptr_members.size()))
        {
            auto &field = pointers_.[:ptr_members[I]:];
            grow_field(field, old_cap, new_cap);
        }
    }

    static consteval auto get_field_by_name(static_string name) -> size_type
    {
        constexpr auto members = nsdms_of(^^T);
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
    constexpr explicit SoaVector(size_type capacity)
        : SoaVector(capacity, allocator_type{})
    {
    }
    constexpr SoaVector(size_type capacity, auto alloc) : alloc_{std::move(alloc)}
    {
        if (capacity > 0)
            grow(capacity);
    }

    // 容量相关
    constexpr size_type capacity() const noexcept
    {
        return capacity_;
    }
    constexpr void reserve(size_type new_cap)
    {
        if (new_cap <= capacity_)
            return;
        grow(new_cap);
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
        constexpr auto I = SoaVector::get_field_by_name(name);
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
        constexpr auto I = SoaVector::get_field_by_name(name);
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
        constexpr auto I = SoaVector::get_field_by_name(name);
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

    // 纯内存释放（不再销毁元素）
    constexpr void destroy() noexcept
    {
        if (capacity_ == 0)
            return;
        template for (constexpr auto I :
                      std::views::indices(ptr_members.size()) | std::views::reverse)
        {
            auto &field = pointers_.[:ptr_members[I]:];
            using FieldType =
                typename std::remove_reference_t<decltype(field)>::value_type;
            using RebAlloc = typename std::allocator_traits<
                allocator_type>::template rebind_alloc<FieldType>;
            RebAlloc reb_alloc(alloc_);
            reb_alloc.deallocate(field.data(), capacity_);
            field = {};
        }
        capacity_ = 0;
    }

    constexpr ~SoaVector() noexcept
    {
        destroy();
    }

    constexpr SoaVector(const SoaVector &o)
        : capacity_{0}, // 必须初始为 0，让 grow 正确分配
          alloc_{std::allocator_traits<
              allocator_type>::select_on_container_copy_construction(o.alloc_)}
    {
        if (o.capacity_ > 0)
            grow(o.capacity_); // 只分配内存，不移动任何元素
    }
    constexpr SoaVector &operator=(const SoaVector &o)
    {
        //copy‑and‑swap
        if (this != &o)
        {
            auto tmp(o);            // 调用拷贝构造
            *this = std::move(tmp); // 调用移动赋值 operator=(SoaVector&&)
        }
        return *this;
    }

    constexpr SoaVector(SoaVector &&o) noexcept
        : capacity_{std::exchange(o.capacity_, 0)}, pointers_{std::move(o.pointers_)},
          alloc_{std::move(o.alloc_)}
    {
    }
    constexpr SoaVector &operator=(SoaVector &&o) noexcept
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

    constexpr decltype(auto) operator[](this auto &&self, size_type idx) noexcept
    {
        auto [... ptrs] = self.pointers_;
        using Tuple =
            std::tuple<decltype(std::forward_like<decltype(self)>(ptrs[idx]))...>;
        return Tuple{std::forward_like<decltype(self)>(ptrs[idx])...};
    }
};

template <typename T, size_type init_size = 8, auto grow_algo = detail::doubleIncrease,
          template <typename> class Alloc = std::allocator,
          template <typename, template <typename> class> class Make = SoaVector>
struct gen_soa_store
{
    using soa_type = Make<T, Alloc>;
    using value_type = T;
    using id_type = size_type;

    soa_type data_;
    std::vector<size_type> dense_;
    std::vector<size_type> sparse_;
    std::vector<size_type> free_;

    static constexpr size_type invalid_id = ~0; // NOLINT

    // NOTE: 初始化指定大小，才能 [] 访问
    constexpr gen_soa_store(size_type size = init_size)
        : data_(size), dense_{}, sparse_(size, invalid_id), free_{}
    {
        dense_.reserve(size);

        free_.reserve(size);
        for (size_type i = size; i > 0; --i)
            free_.push_back(i - 1);
    }

    // NOTE: 和末尾的最后一个元素交换。然后size--。 反正无序
    constexpr void remove_from_dense(size_type dense_id)
    {
        auto last = dense_.size() - 1;
        size_type moved_id = dense_[last];
        dense_[dense_id] = moved_id;
        sparse_[moved_id] = dense_id; // <--- 必须添加这一行
        dense_.pop_back();
    }
    constexpr void recycle_to_free(size_type id)
    {
        data_.destroy_at(id);
        free_.push_back(id);
    }

    constexpr auto default_next_capacity() const noexcept
    {
        return grow_algo(data_.capacity_);
    }

    constexpr bool empty() const noexcept
    {
        return free_.empty();
    }

    constexpr std::optional<size_type> allocate() noexcept
    {
        if (free_.empty())
            return std::nullopt;
        auto id = free_.back();
        free_.pop_back();

        //NOTE: 实现 id -> sparse_[id] -> dense_id -> data_的物理位置索引 slot
        sparse_[id] = dense_.size();
        dense_.push_back(id); // 逻辑索引 = dense_.size()-1
        return id;            // 返回物理 id
    }

    constexpr auto release(size_type id)
    {
        assert(sparse_[id] != invalid_id);
        assert(sparse_[id] < dense_.size());

        auto dense_id = sparse_[id];
        sparse_[id] = invalid_id;
        remove_from_dense(dense_id);
        recycle_to_free(id);
    }

    constexpr void grow(size_type new_cap)
    {
        if (new_cap <= data_.capacity())
            return;

        auto old_cap = data_.capacity();

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

        // 2. 搬迁所有存活元素（只搬 dense_ 中记录的槽位）
        for (size_type i = 0; i < dense_.size(); ++i)
        {
            size_type slot = dense_[i];
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

        // 4. 更新 SoaVector 状态
        data_.capacity_ = new_cap;
        data_.pointers_ = std::move(new_pointers);

        // 5. 扩展 sparse / free
        sparse_.resize(new_cap, invalid_id);
        for (size_type i = new_cap; i > old_cap; --i)
            free_.push_back(i - 1);
    }

    constexpr void grow()
    {
        grow(default_next_capacity());
    }

    constexpr void destroy_at(size_type id) noexcept
    {
        data_.destroy_at(id);
    }
    template <typename... Args>
    constexpr void construct_at(size_type idx, Args &&...args) noexcept(
        std::is_nothrow_constructible_v<T, Args...>)
    {
        data_.construct_at(idx, std::forward<Args>(args)...);
    }
    constexpr void construct_at(size_type idx,
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
            self.dense_ |
            std::views::transform([&data](size_type slot) -> decltype(auto) {
                return data.template get<name>(slot);
            })...);
    }
    constexpr auto view(this auto &&self) noexcept
    {
        return self.dense_ | std::views::transform([&self](size_type slot) -> auto {
                   return std::forward_like<decltype(self)>(self.data_[slot]);
               });
    }

    constexpr size_type size() const noexcept
    {
        return dense_.size();
    }

    // 随机访问：store[i] 返回 std::tuple<Fields&...>（或 const&）
    constexpr auto operator[](this auto &&self, size_type logical_idx) noexcept
    {
        size_type slot = self.dense_[logical_idx]; // 逻辑索引 → 物理槽位
        return std::forward_like<decltype(self)>(self.data_[slot]);
    }
    constexpr ~gen_soa_store() noexcept
    {
        // 销毁所有存活元素
        for (size_type dense_id = 0; dense_id < dense_.size(); ++dense_id)
        {
            size_type id = dense_[dense_id];
            data_.destroy_at(id);
        }
    }
    gen_soa_store(const gen_soa_store &) = delete;
    gen_soa_store &operator=(const gen_soa_store &) = delete;
    gen_soa_store(gen_soa_store &&) noexcept = default; // 移动构造可默认
    gen_soa_store &operator=(gen_soa_store &&) noexcept = default;

    template <typename Store>
    struct store_iterator
    {
        Store *store;
        size_type logical_idx;

        using value_type = decltype(std::declval<Store &>().operator[](0));
        using reference = value_type; // 返回临时 tuple
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::input_iterator_tag; // 因为是 prvalue 代理

        constexpr auto operator*() const
            noexcept(noexcept(store->operator[](logical_idx)))
        {
            return store->operator[](logical_idx); // 返回临时 tuple
        }

        constexpr store_iterator &operator++() noexcept
        {
            ++logical_idx;
            return *this;
        }

        constexpr store_iterator operator++(int) noexcept
        {
            auto tmp = *this;
            ++logical_idx;
            return tmp;
        }

        constexpr bool operator==(const store_iterator &) const = default;
    };

    // 在 gen_soa_store 类内添加：
  public:
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

    // NOTE: view()  是零时的，内部有引用解引用会错误
    // constexpr auto begin(this auto &&self) noexcept
    // {
    //     return self.view().begin();
    // }
    // constexpr auto end(this auto &&self) noexcept
    // {
    //     return self.view().end();
    // }

    decltype(auto) read_only(this const auto &self)
    {
        return self;
    }
};

template <name_spec... registration>
struct soa_class;

static consteval bool unique_class_name(std::vector<std::meta::info> input)
{
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        auto spec_i = std::meta::extract<name_spec>(input[i]);
        for (std::size_t j = i + 1; j < input.size(); ++j)
        {
            auto spec_j = std::meta::extract<name_spec>(input[j]);
            if (spec_i.name == spec_j.name)
                return false; // 发现重复
        }
    }
    return true;
}

static consteval bool all_of_info_is_class(std::span<const std::meta::info> input)
{
    return std::ranges::all_of(input, [](std::meta::info v) {
        return std::meta::is_class_type(std::meta::extract<name_spec>(v).info);
    });
}

template <typename soa_class>
    requires(unique_class_name(std::meta::template_arguments_of(^^soa_class)))
struct world
{
    struct soa_member_type;
    consteval
    {
        static_assert(
            all_of_info_is_class(std::meta::template_arguments_of(^^soa_class)));

        auto soa_members =
            std::meta::template_arguments_of(^^soa_class) |
            std::views::transform([](std::meta::info arg) -> std::meta::info {
                auto spec = std::meta::extract<name_spec>(arg);
                return std::meta::data_member_spec(spec.info, {.name = spec.name.view()});
            });
        std::meta::define_aggregate(^^soa_member_type, soa_members);
    }
    soa_member_type soa_sets_{};

    static constexpr auto soa_members = nsdms_of(^^soa_member_type);
    static constexpr auto soa_value_members = [] {
        std::vector<std::meta::info> result;
        template for (constexpr auto m : soa_members)
        {
            using M = [:std::meta::type_of(m):];
            using value_type = M::value_type;
            result.push_back(std::meta::dealias(^^value_type));
        }
        return std::define_static_array(result);
    }();

    struct soa_meta_info
    {
        using registration_type = [:std::meta::substitute(
                                        ^^std::tuple,
                                        soa_members |
                                            std::views::transform([](std::meta::info arg)
                                                                      -> std::meta::info {
                                                return std::meta::type_of(arg);
                                            })):];
        // 编译期：类型 -> ID
        template <typename T>
        static consteval size_type id_of()
        {
            constexpr auto N = std::tuple_size_v<registration_type>;
            template for (constexpr auto I : std::ranges::views::indices(N))
            {
                if (std::same_as<T, std::tuple_element_t<I, registration_type>>)
                    return I;
            }
            throw std::meta::exception{"Type not in tuple",
                                       std::meta::current_function()};
        }

        template <static_string name>
        static consteval size_type id_by_name()
        {
            for (size_type i{}; i < soa_members.size(); ++i)
            {
                if (std::meta::identifier_of(soa_members[i]) == name.view())
                    return i;
            }
            throw std::meta::exception{"Name not in tuple",
                                       std::meta::current_function()};
        }

        // 编译期：ID -> 类型
        template <size_type I>
        using type_at = std::tuple_element_t<I, registration_type>;

        auto operator()(size_type type_id)
        {
            //NOTE: 返回值能 返回多个类型吗？ type_id 是运行时数据
        }
    };

    static consteval auto get_sao_types()
    {
        return soa_meta_info{};
    }
    consteval static size_type get_name_idex(std::span<const std::meta::info> input,
                                             static_string name)
    {
        for (size_type i{}; i < input.size(); ++i)
            if (std::meta::identifier_of(input[i]) == name.view())
                return i;
        return ~0;
    }

    consteval static size_type find_info_value(std::span<const std::meta::info> input,
                                               std::meta::info value)
    {
        for (size_type i{}; i < input.size(); ++i)
            if (input[i] == value)
                return i;
        return ~0;
    }

    template <static_string name>
        requires(get_name_idex(soa_members, name) != ~0)
    constexpr decltype(auto) get_soa(this auto &&self) noexcept
    {
        constexpr auto I = get_name_idex(soa_members, name);
        return std::forward_like<decltype(self)>(self.soa_sets_.[:soa_members[I]:]);
    }
    template <typename T>
        requires(find_info_value(soa_value_members, std::meta::dealias(^^T)) != ~0)
    constexpr decltype(auto) get_soa(this auto &&self) noexcept
    {
        constexpr auto I = find_info_value(soa_value_members, std::meta::dealias(^^T));
        return std::forward_like<decltype(self)>(self.soa_sets_.[:soa_members[I]:]);
    }

    template <typename Store>
    struct proxy_value
    {
        using value_type = Store::value_type;
        using id_type = Store::id_type;
        constexpr proxy_value(Store &store, id_type id) noexcept : store_{&store}, id_{id}
        {
        }
        proxy_value(const proxy_value &) = delete;
        proxy_value &operator=(const proxy_value &) = delete;
        constexpr proxy_value(proxy_value &&o) noexcept
            : store_{std::exchange(o.store_, {})}, id_{std::exchange(o.id_, {})}
        {
        }
        constexpr proxy_value &operator=(proxy_value &&o) noexcept
        {
            if (&o != this)
            {
                if (store_ != nullptr)
                    store_->release(id_);
                store_ = std::exchange(o.store_, {});
                id_ = std::exchange(o.id_, {});
            }
            return *this;
        }
        constexpr void release() noexcept
        {
            if (store_ != nullptr)
            {
                store_->destroy_at(id_);
                store_->release(id_);
                store_ = {};
                id_ = {};
            }
        }
        constexpr ~proxy_value() noexcept
        {
            release();
        }
        auto operator*() noexcept
        {
            assert(store_ != nullptr);
            return store_->data_[id_]; // 直接物理访问
        }
        auto operator*() const noexcept
        {
            assert(store_ != nullptr);
            return static_cast<const Store *>(store_)->data_[id_];
        }

      private:
        Store *store_;
        id_type id_;
    };

    template <typename T>
        requires(find_info_value(soa_value_members, std::meta::dealias(^^T)) != ~0)
    constexpr decltype(auto) make_soa_value(this auto &&self, auto &&...args)
    {
        auto &store = std::forward_like<decltype(self)>(self.template get_soa<T>());
        using S = std::decay_t<decltype(store)>;
        using proxy_type = proxy_value<S>;

        auto slot = store.allocate();
        if (not slot)
        {
            return std::optional<proxy_type>{std::nullopt};
        }
        auto id = *slot;
        store.construct_at(id, std::forward<decltype(args)>(args)...);
        return std::optional<proxy_type>{proxy_type{store, id}};
    }

    static consteval auto unique_soa_component_type()
    {
        std::vector<std::meta::info> result;
        constexpr auto N = soa_members.size();
        template for (constexpr auto I : std::ranges::views::indices(N))
        {
            using T = [:std::meta::type_of(soa_members[I]):];
            using value_type = T::value_type;
            auto components = nsdms_of(^^value_type);
            for (auto c : components)
            {
                auto type = std::meta::type_of(c);
                bool find{};
                for (auto item : result)
                    if (item == type)
                    {
                        find = true;
                        break;
                    }
                if (not find)
                    result.push_back(type);
            }
        }
        return result;
    }
    struct soa_component_meta_info
    {
        using registration_type = [:std::meta::substitute(^^std::tuple,
                                                          unique_soa_component_type()):];
        // 编译期：类型 -> ID
        template <typename T>
        static consteval size_type id_of()
        {
            constexpr auto N = unique_soa_component_type().size();
            template for (constexpr auto I : std::ranges::views::indices(N))
            {
                if (std::same_as<T, std::tuple_element_t<I, registration_type>>)
                    return I;
            }
            throw std::meta::exception{"Type not in tuple",
                                       std::meta::current_function()};
        }
        template <static_string name>
        static consteval size_type id_by_name()
        {
            auto components = unique_soa_component_type();
            for (size_type i{}; i < components.size(); ++i)
            {
                // NOTE: identifier_of 对基本类型失效
                if (std::meta::display_string_of(components[i]) == name.view())
                    return i;
            }
            throw std::meta::exception{"Name not in tuple",
                                       std::meta::current_function()};
        }

        // 编译期：ID -> 类型
        template <size_type I>
        using type_at = std::tuple_element_t<I, registration_type>;
    };
};

// ---------- 测试辅助：带计数器的分配器 ----------
template <typename T>
struct TrackingAlloc
{
    using value_type = T;
    inline static std::size_t allocated_bytes = 0;
    inline static std::size_t deallocated_bytes = 0;
    inline static std::size_t alloc_count = 0;
    inline static std::size_t dealloc_count = 0;

    TrackingAlloc() = default;
    template <class U>
    TrackingAlloc(const TrackingAlloc<U> &) noexcept
    {
    }

    T *allocate(std::size_t n)
    {
        alloc_count++;
        allocated_bytes += n * sizeof(T);
        return static_cast<T *>(::operator new(n * sizeof(T)));
    }

    void deallocate(T *p, std::size_t n) noexcept
    {
        dealloc_count++;
        deallocated_bytes += n * sizeof(T);
        ::operator delete(p);
    }

    friend bool operator==(const TrackingAlloc &, const TrackingAlloc &) noexcept
    {
        return true;
    }
    friend bool operator!=(const TrackingAlloc &, const TrackingAlloc &) noexcept
    {
        return false;
    }
};

// ---------- 测试结构体 ----------
struct SimplePod
{
    int x;
    double y;
    char z;
};

struct NonTrivial
{
    int id;
    std::string name;

    NonTrivial(int i = 0, std::string n = "") : id(i), name(std::move(n)) {}
    NonTrivial(const NonTrivial &) = delete; // 只可移动
    NonTrivial &operator=(const NonTrivial &) = delete;
    NonTrivial(NonTrivial &&) noexcept = default;
    NonTrivial &operator=(NonTrivial &&) noexcept = default;
};

static_assert(^^SimplePod::x != ^^NonTrivial::id);
static_assert(std::meta::type_of(^^SimplePod::x) == std::meta::type_of(^^NonTrivial::id));

// NOLINTEND

int main()
try
{
    using soa_list = soa_class<{"soa_SimplePod", ^^gen_soa_store<SimplePod, 4>},
                               {"soa_NonTrivial", ^^gen_soa_store<NonTrivial>}>;
    using world_type = world<soa_list>;

    world_type world{};

    auto &soa_SimplePod = world.get_soa<"soa_SimplePod">();
    auto &soa_SimplePod2 = world.get_soa<SimplePod>();
    assert(&soa_SimplePod == &soa_SimplePod2);

    static_assert(
        std::is_same_v<std::decay_t<decltype(soa_SimplePod)>::value_type, SimplePod>);
    static_assert(not std::meta::is_copy_constructor(^^soa_SimplePod));
    static_assert(not std::meta::is_move_constructor(^^soa_SimplePod));

    {
        struct component_id
        {
            uint32_t obj_id;
            uint16_t type_id;
            uint16_t version;
        };
        struct component
        {
            using property_type = std::vector<component_id>;
            property_type propertys;
        };
        struct A
        {
            int x;
            int y;
        };
        struct B
        {
            int y;
            int x;
        };

        int x = 0;
        int y = 1;

        constexpr std::array<uint16_t, 2> property_a{100, 100};
        static_assert(property_a[0] == 100);
        static_assert(property_a[1] == 100);
        constexpr auto members_A = nsdms_of(^^A);

        constexpr std::array<uint16_t, 2> property_b{100, 100};
        constexpr auto members_B = nsdms_of(^^B);
        //NOTE: 错误的，无法区分。真的。 还是要命名好一点真的。
    }
    {
        struct component_id
        {
            uint32_t obj_id;
            uint16_t type_id;
            uint16_t version;
        };
        struct property_type
        {
            static_string name;
            component_id id;
        };
        struct component
        {
            component_id id;
            std::vector<property_type> propertys;
        };
        struct A
        {
            int x;
            int y;
            bool operator==(const A &) const = default;
        };
        struct B
        {
            int y;
            int x;
            bool operator==(const B &) const = default;
        };
        constexpr auto type_A_id = 0;
        constexpr auto type_B_id = 1;
        constexpr auto type_int_id = 2;

        //NOTE: 构造的时候必须确定 顺序，然后不能再变化了。动态变化，必须生成新的类型
        component a_component = component{
            .id = {.obj_id = 0, .type_id = type_A_id, .version = 0},
            .propertys = {
                {.name = "x", .id = {.obj_id = 0, .type_id = type_int_id, .version = 0}},
                {.name = "y",
                 .id = {.obj_id = 1, .type_id = type_int_id, .version = 0}}}};
        component b_component = component{
            .id = {.obj_id = 0, .type_id = type_B_id, .version = 0},
            .propertys = {
                {.name = "y", .id = {.obj_id = 3, .type_id = type_int_id, .version = 0}},
                {.name = "x", .id = {.obj_id = 2, .type_id = type_int_id, .version = 0}},
            }};

        std::vector<int> store_int = {0, 1, 2, 3};
        //期望世界生成 A a{0,1}; B b{3,2};

        std::println(" std::meta::display_string_of(members[i]): {}",
                     std::meta::display_string_of(nsdms_of(^^A)[0]));
        std::println(" std::meta::identifier_of(members[i]): {}",
                     std::meta::identifier_of(nsdms_of(^^A)[0]));

        //NOTE: 找到 A 或者生成 A 就行，非常简单
        // 需要捕获 store_int
        auto make_a = [&](const component &comp) -> A {
            constexpr auto members = nsdms_of(^^A);
            constexpr auto N = members.size();
            assert(comp.propertys.size() == N);
            return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                return A{store_int[comp.propertys[Is].id.obj_id]...};
            }(std::make_index_sequence<members.size()>{});
        };

        auto make_b = [&](const component &comp) -> B {
            constexpr auto members = nsdms_of(^^B);
            constexpr auto N = members.size();
            assert(comp.propertys.size() == N);
            return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                return B{store_int[comp.propertys[Is].id.obj_id]...};
            }(std::make_index_sequence<members.size()>{});
        };
        A a = make_a(a_component);
        B b = make_b(b_component);
        // A a{0,1}; B b{3,2};
        assert((bool)(a == A{0, 1}));
        assert((b == B{3, 2}));

        //NOTE: store_int 已经确定类型了。问题是很难的除非唯一。比如 SOA那种
        //NOTE: 其实也确定了的。只要真的是确实 一个类型 一个 id。那就很好办了。 肯定要转发的，但是返回值不可能统一
        auto make_ref_a = [&](const component &comp) -> auto {
            constexpr auto members = nsdms_of(^^A);
            constexpr auto N = members.size();
            assert(comp.propertys.size() == N);
            return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                return std::tie(store_int[comp.propertys[Is].id.obj_id]...);
            }(std::make_index_sequence<members.size()>{});
        };
        auto [x, y] = make_ref_a(a_component);
        assert(a.x == x);
        assert(a.y == y);

        //NOTE: 应该保持一些约束。 这样可以实现 模式匹配真的，非常简单说实话

        //NOTE: 如何.name 不需要，确实可以合并说实话。只要保证 类型存在。索引0是盒子，内容呢？
        // SOA 和 普通的 分散的多数据如何处理？

        //修改如何修改？？？？仅仅修改某个 属性，SOA很方便的做到的。为什么不仅仅SOA数据？

        // 多个类组合。很简单的啊。保持索引即可，object_id, soa_id。 就可以了啊？
        // NOTE: 回收id，那就是多个版本号很容易就能解决了

        //这样保留 vector 方便组合，如何？？？？。 模式匹配只能这样了，这个就说需要模式匹配了。运行时模式匹配？
        //NOTE: 保留 SOA面向对象的 id,filed基本的id 不需要了，真的。 我们可以通过 version 来判断 外部持有的 是否发生写或读
        //NOTE: 还是面向对象，组件的添加还是保留，生成 grop嘛。是否要求顺序呢？肯定是要求的啊。从 insert 生成得了。保证 vector的顺序和struct的顺序编译期确定即可
        //NOTE: 组还是需要的，还是面向对象，面向对象池简单的这样确实简单一些。能不能运行时生成对象呢？ 我们也简单 run_times的类型就行了
        //NOTE: 运行时的类型，我们也要记录，，必须编译期确定。名字还是类型 生成 grop呢？是否允许名字，应该是允许名字的，这样简单有意义
        //NOTE: 总之，类型的注册，必须编译期确定范围，必须编译期确定一切。编译期的数据 直接走SOA，运行时的数据，grop仅仅是 SOA的组合，经此而已
        //NOTE: 我们有注解，很方便的，我们通过名字，可以方便的跨单元

        //NOTE: component 改成 runtime_component.严格注意顺序生成的时候

        x = 99;
        assert(store_int[0] == 99);
    }
    {
        // ====== Test make_soa_value ======
        std::println("\n--- Testing make_soa_value ---");

        world_type w;

        // ---- Test 1: 正常分配与构造，通过 *proxy 访问 ----
        {
            auto opt1 = w.make_soa_value<SimplePod>(10, 3.14, 'A');
            assert(opt1.has_value());
            auto proxy = std::move(*opt1);
            auto [x, y, z] = *proxy; // 通过 *proxy 返回 tuple
            assert(x == 10);
            assert(y == 3.14);
            assert(z == 'A');
            // 离开作用域 proxy 析构 → 自动释放槽位
        }
        // 槽位已释放，应该可以重用
        {
            auto opt2 = w.make_soa_value<SimplePod>(20, 2.71, 'B');
            assert(opt2.has_value());
            auto proxy = std::move(*opt2);
            auto [x, y, z] = *proxy;
            assert(x == 20);
        }
        std::println("Test1 (basic allocate/construct/access) passed.");

        // ---- Test 2: 分配满时返回 std::nullopt ----
        {
            // 将 soa_SimplePod 的容量填满（capacity = 4，当前存活的 1 个来自 Test1 的 proxy 已析构，空闲槽位 4）
            // 先分配 4 个不释放的 proxy 以耗尽容量
            auto p1 = w.make_soa_value<SimplePod>(1, 0.0, '1');
            auto p2 = w.make_soa_value<SimplePod>(2, 0.0, '2');
            auto p3 = w.make_soa_value<SimplePod>(3, 0.0, '3');
            auto p4 = w.make_soa_value<SimplePod>(4, 0.0, '4');
            assert(p1.has_value() && p2.has_value() && p3.has_value() && p4.has_value());

            // 再分配应失败
            auto p5 = w.make_soa_value<SimplePod>(5, 0.0, '5');
            assert(!p5.has_value());
        }
        std::println("Test2 (exhaust capacity) passed.");

        // ---- Test 3: proxy 移动语义，源对象不再释放 ----
        {
            auto &store = w.get_soa<SimplePod>();
            auto n_before = store.size();

            auto opt = w.make_soa_value<SimplePod>(100, 1.1, 'x');
            assert(opt.has_value());
            auto proxy1 = std::move(*opt);
            assert(store.size() == n_before + 1);

            auto proxy2 = std::move(proxy1);
            // proxy1 移后为空，析构不释放
            assert(store.size() == n_before + 1); // 仍然存活

            // 通过 proxy2 访问
            auto [x, y, z] = *proxy2;
            assert(x == 100);
        } // proxy2 析构，释放元素
        assert(w.get_soa<SimplePod>().size() == 0);
        std::println("Test3 (move semantics) passed.");

        // ---- Test 4: const 世界无法调用 make_soa_value（编译期验证，此处仅注释说明） ----
        // const world_type cw;
        // cw.make_soa_value<SimplePod>(1,2.0,'a');  // 编译错误：allocate() 需要非 const
        // 该行为符合预期，无需运行时测试。
        std::println("Test4 (const restriction) checked at compile-time.");

        // ---- Test 5: 使用 NonTrivial 类型（只可移动） ----
        {
            auto opt = w.make_soa_value<NonTrivial>(42, "hello");
            assert(opt.has_value());
            auto proxy = std::move(*opt);
            auto [id, name] = *proxy;
            assert(id == 42);
            assert(name == "hello");
            // proxy 析构时正确调用 store.release，销毁 NonTrivial
        }
        std::println("Test5 (NonTrivial type) passed.");

        // ---- Test 6: 大量分配后释放，验证槽位重用 ----
        {
            auto &store = w.get_soa<SimplePod>();
            store.grow(6); // 扩容确保容量足够
            std::vector<decltype(w.make_soa_value<SimplePod>(0, 0.0, ' '))> proxies;
            for (int i = 0; i < 6; ++i)
            {
                auto opt = w.make_soa_value<SimplePod>(i, i * 1.0, 'a' + i);
                assert(opt.has_value());
                proxies.push_back(std::move(*opt));
            }
            // 释放中间两个
            proxies.erase(proxies.begin() + 2);
            proxies.erase(proxies.begin() + 2); // 现在存活 4 个
            // 再分配，应成功
            auto opt_new = w.make_soa_value<SimplePod>(99, 9.9, '!');
            assert(opt_new.has_value());
            auto proxy_new = std::move(*opt_new);
            auto [x, y, z] = *proxy_new;
            assert(x == 99);
        }
        std::println("Test6 (slot reuse) passed.");

        std::println("\nAll make_soa_value tests passed!\n");
    }

    std::cout << "\nAll tests passed!\n";
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
}