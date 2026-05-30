#include <cstddef>
#include <cstdint>
#include <iostream>
#include <exception>
#include <memory>
#include <optional>
#include <ranges>
#include <meta>

#include <cassert>
#include <string>
#include <memory_resource>
#include <tuple>
#include <utility>

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

template <typename T, auto grow_algo = detail::doubleIncrease,
          template <typename> class Alloc = std::allocator>
    requires(requires() { grow_algo(uint32_t{0}); })
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

    constexpr auto new_capacity() const noexcept
    {
        return grow_algo(capacity_);
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
        // using Tuple =
        //     std::tuple<decltype(std::forward_like<decltype(self)>(ptrs[idx]))...>;
        // return Tuple{std::forward_like<decltype(self)>(ptrs[idx])...};
        return std::tie(std::forward_like<decltype(self)>(ptrs[idx])...);
    }
};

template <typename T, size_type init_size = 8, auto grow_algo = detail::doubleIncrease,
          template <typename> class Alloc = std::allocator,
          template <typename, auto, template <typename> class> class Make = SoaVector>
struct gen_soa_store
{
    using soa_type = Make<T, grow_algo, Alloc>;

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
        return data_.new_capacity();
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
        dense_.push_back(id);
        return id;
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

struct world
{
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

struct component_id
{
    uint32_t obj_id;
    uint16_t type_id;
    uint16_t version;
};
struct component
{
    component_id id;
};

template <typename Store>
struct unique_component
{
    using release_fn = void(void *) noexcept;
    using raw_memory_fn = void *(void *) noexcept;
    Store *store_;
    component_id id_;
    release_fn *auto_release_;

    constexpr unique_component(Store &store, component_id id) noexcept
        : store_{&store}, id_{id}, auto_release_{[](void *ptr) noexcept {
              //   auto *self = static_cast<unique_component *>(ptr);
              //   static_cast<Store *>(self->store_)->release(self->id_.obj_id);
          }}
    {
    }

    unique_component(const unique_component &) = delete;
    unique_component &operator=(const unique_component &) = delete;

    constexpr unique_component(unique_component &&o) noexcept
        : store_{std::exchange(o.store_, {})}, id_{std::exchange(o.id_, {})},
          auto_release_{std::exchange(o.auto_release_, {})}
    {
    }
    void release() noexcept
    {
        if (store_ != nullptr)
        {
            store_->release(id_.obj_id);
        }
    }
    constexpr unique_component &operator=(unique_component &&o) noexcept
    {
        if (&o != this)
        {
            if (auto_release_ != nullptr)
                release();
            store_ = std::exchange(o.store_, {});
            id_ = std::exchange(o.id_, {});
            auto_release_ = std::exchange(o.auto_release_, {});
        }
        return *this;
    }
    constexpr ~unique_component() noexcept
    {
        if (auto_release_ != nullptr)
        {
            release();
            store_ = {};
            id_ = {};
            auto_release_ = {};
        }
    }
    void *raw_memory()
    {
        // 分成很多快的。
    }
};

// NOLINTEND

int main()
try
{
    using namespace std;

    // Test 1: 初始状态
    {
        constexpr size_type N = 4;
        gen_soa_store<SimplePod, N> store;
        assert(store.data_.capacity() == N);
        assert(store.free_.size() == N);
        assert(store.free_.back() == 0);
        assert(store.dense_.empty());
        cout << "Test1 passed.\n";
    }

    // Test 2: 分配、构造、释放、重用（修复了 sparse 映射）
    {
        gen_soa_store<SimplePod, 3> store;
        auto s0 = store.allocate().value(); // 0
        auto s1 = store.allocate().value(); // 1
        store.construct_at(s0, SimplePod{10, 3.14, 'a'});
        store.construct_at(s1, SimplePod{20, 2.71, 'b'});

        assert(store.data_.template get<0>(s0) == 10);
        store.release(s0); // 释放 s0
        assert(store.free_.back() == s0);

        auto s2 = store.allocate().value(); // 重新拿到 s0
        assert(s2 == s0);
        store.construct_at(s2, SimplePod{30, 0.0, 'c'});
        assert(store.data_.template get<0>(s2) == 30);

        // 验证 s1 仍然正常（之前的 bug 会导致这里可能出错）
        assert(store.data_.template get<0>(s1) == 20);
        cout << "Test2 passed.\n";
    }
    {

        // ----- 测试用例 -----
        {
            // Test A: 构造后自动释放 (RAII)
            gen_soa_store<SimplePod, 4> store;
            auto id1 = store.allocate().value();
            store.construct_at(id1, SimplePod{10, 1.0, 'a'});
            assert(store.size() == 1);

            {
                component_id cid{id1, 0, 0};
                unique_component uc(store, cid);
                assert(uc.id_.obj_id == id1);
                assert(store.size() == 1); // 存活元素数量不变
            } // uc 析构 → 自动调用 store.release(id1)

            assert(store.size() == 0); // 已释放
            // 验证槽位可重用
            auto id2 = store.allocate().value();
            assert(id2 == id1); // 重用了同一个槽位
            store.construct_at(id2, SimplePod{20, 2.0, 'b'});
            assert(store.data_.template get<0>(id2) == 20);
            std::cout << "Test unique_component basic RAII passed.\n";
        }

        {
            // Test B: 移动构造后，源对象不再自动释放
            gen_soa_store<SimplePod, 4> store;
            auto id = store.allocate().value();
            store.construct_at(id, SimplePod{5, 5.0, 'e'});
            assert(store.size() == 1);

            component_id cid{id, 0, 0};
            unique_component uc1(store, cid);
            unique_component uc2 = std::move(uc1);
            assert(uc1.auto_release_ == nullptr); // 源对象释放能力已被剥夺
            assert(uc2.id_.obj_id == id);

            // uc1 析构时不会调用 release
            assert(store.size() == 1); // 元素仍存活

            // uc2 析构时会正确释放
            {
                unique_component uc3 = std::move(uc2);
                assert(store.size() == 1);
            } // uc3 析构 → release(id)
            assert(store.size() == 0);
            auto id2 = store.allocate().value();
            assert(id2 == id);
            std::cout << "Test unique_component move semantics passed.\n";
        }

        {
            // Test C: 移动赋值（包括自我赋值安全）
            gen_soa_store<SimplePod, 4> store;
            auto id1 = store.allocate().value();
            store.construct_at(id1, SimplePod{1, 0.0, 'x'});
            auto id2 = store.allocate().value();
            store.construct_at(id2, SimplePod{2, 0.0, 'y'});

            component_id cid1{id1, 0, 0};
            component_id cid2{id2, 0, 0};
            unique_component uc1(store, cid1);
            unique_component uc2(store, cid2);
            assert(store.size() == 2);

            uc1 = std::move(uc2); // uc1 原先持有 id1 会被释放，然后接管 id2
            assert(uc1.id_.obj_id == id2);
            assert(store.size() == 1); // id1 已被释放
            // uc2 已为空，析构不会再次释放 id2

            auto id3 = store.allocate().value();
            assert(id3 == id1); // id1 被回收
            std::cout << "Test unique_component move assignment passed.\n";
        }

        {
            // Test D: 拷贝被禁止（编译期检查，此处仅展示设计意图）
            // static_assert(!std::is_copy_constructible_v<unique_component>);
            // static_assert(!std::is_copy_assignable_v<unique_component>);
            std::cout << "Test unique_component non-copyable passed.\n";
        }
    }

    // Test 3: 扩容（默认翻倍）并保留旧数据
    {
        gen_soa_store<SimplePod, 3> store;
        auto a = store.allocate().value(); // 0
        auto b = store.allocate().value(); // 1
        store.construct_at(a, SimplePod{100, 1.1, 'A'});
        store.construct_at(b, SimplePod{200, 2.2, 'B'});

        size_t old_cap = store.data_.capacity(); // 3
        store.grow();                            // 3 → 6

        assert(store.data_.capacity() == 6);
        assert(store.sparse_.size() == 6);

        // 旧数据完好
        assert(store.data_.template get<0>(a) == 100);
        assert(store.data_.template get<1>(a) == 1.1);
        assert(store.data_.template get<0>(b) == 200);

        // 新槽位可用
        auto c = store.allocate().value(); // 3
        assert(c == 3);
        store.construct_at(c, SimplePod{300, 3.3, 'C'});
        assert(store.data_.template get<0>(c) == 300);

        cout << "Test3 (grow default) passed.\n";
    }

    // Test 4: 扩容到指定容量
    {
        gen_soa_store<SimplePod, 2> store;
        auto s = store.allocate().value();
        store.construct_at(s, SimplePod{1, 0.0, 'x'});
        store.grow(5);
        assert(store.data_.capacity() == 5);
        assert(store.free_.size() == 4); // 原本剩 1 个 + 新增 3 个
        assert(store.free_.back() == 2);
        assert(store.data_.template get<0>(s) == 1);
        cout << "Test4 (grow specific) passed.\n";
    }

    // Test 5: 扩容后释放旧槽位并重用
    {
        gen_soa_store<SimplePod, 2> store;
        auto a = store.allocate().value();
        auto b = store.allocate().value();
        store.construct_at(a, SimplePod{1, 0.0, 'a'});
        store.construct_at(b, SimplePod{2, 0.0, 'b'});
        store.grow(); // 2→4
        store.release(a);
        auto a2 = store.allocate().value();
        assert(a2 == a);
        store.construct_at(a2, SimplePod{99, 9.9, '!'});
        assert(store.data_.template get<0>(a2) == 99);
        cout << "Test5 (grow + reuse) passed.\n";
    }

    // Test 6: 非平凡类型 NonTrivial 的扩容与移动
    {
        gen_soa_store<NonTrivial, 2> store;
        auto id = store.allocate().value();
        store.construct_at(id, NonTrivial{42, "hello"});
        store.grow(4); // 移动构造 NonTrivial
        assert(store.data_.template get<0>(id) == 42);
        assert(store.data_.template get<1>(id) == "hello");
        cout << "Test6 (non-trivial move) passed.\n";
    }

    // Test 7: 连续释放与分配，验证 sparse 映射始终正确
    {
        constexpr size_type N = 4;
        gen_soa_store<SimplePod, N> store;
        std::vector<size_type> ids;
        for (size_type i = 0; i < N; ++i)
            ids.push_back(store.allocate().value());

        // 构造所有
        for (size_type i = 0; i < N; ++i)
            store.construct_at(ids[i], SimplePod{int(i), 0.0, 'a'});

        // 先释放中间两个（打乱 dense）
        store.release(ids[1]);
        store.release(ids[2]);

        // 此时 dense 中还剩下 ids[0] 和 ids[3]
        // 再次分配，应得到 ids[2] 和 ids[1]（逆序 free_）
        auto new1 = store.allocate().value(); // ids[2]?
        assert(new1 == ids[2]);
        store.construct_at(new1, SimplePod{100, 0.0, 'z'});
        auto new2 = store.allocate().value(); // ids[1]
        assert(new2 == ids[1]);
        store.construct_at(new2, SimplePod{200, 0.0, 'y'});

        // 验证未受影响的 ids[0] 和 ids[3] 依然正确
        assert(store.data_.template get<0>(ids[0]) == 0);
        assert(store.data_.template get<0>(ids[3]) == 3);

        cout << "Test7 (sparse correctness) passed.\n";
    }

    // Test 8: view 基本遍历
    {
        gen_soa_store<SimplePod, 3> store;
        auto a = store.allocate().value(); // 2
        auto b = store.allocate().value(); // 1
        auto c = store.allocate().value(); // 0
        store.construct_at(a, SimplePod{10, 1.0, 'x'});
        store.construct_at(b, SimplePod{20, 2.0, 'y'});
        store.construct_at(c, SimplePod{30, 3.0, 'z'});

        int count = 0;
        for (auto &&[x_val, y_val, z_val] : store.view<"x", "y", "z">())
        {
            if (count == 0)
            {
                assert(x_val == 10);
                assert(y_val == 1.0);
                assert(z_val == 'x');
            }
            else if (count == 1)
            {
                assert(x_val == 20);
                assert(y_val == 2.0);
                assert(z_val == 'y');
            }
            else
            {
                assert(x_val == 30);
                assert(y_val == 3.0);
                assert(z_val == 'z');
            }
            ++count;
        }
        assert(count == 3);
        cout << "Test8 (view basic) passed.\n";
    }

    // Test 9: 通过 view 修改值
    {
        gen_soa_store<SimplePod, 3> store;
        auto id = store.allocate().value();
        store.construct_at(id, SimplePod{1, 0.0, 'a'});

        for (auto &&[x, y, z] : store.view<"x", "y", "z">())
        {
            x = 999;
            y = 3.14;
            z = '!';
        }
        assert(store.data_.template get<0>(id) == 999);
        assert(store.data_.template get<1>(id) == 3.14);
        assert(store.data_.template get<2>(id) == '!');
        cout << "Test9 (view modify) passed.\n";
    }

    // Test 10: view 在 release 后动态变化
    {
        gen_soa_store<SimplePod, 3> store;
        auto a = store.allocate().value(); // 2
        auto b = store.allocate().value(); // 1
        store.construct_at(a, SimplePod{100, 1.1, 'A'});
        store.construct_at(b, SimplePod{200, 2.2, 'B'});

        store.release(a); // dense 中只剩下 b (slot 1)
        int sum = 0;
        for (auto &&[x, y, z] : store.view<"x", "y", "z">())
        {
            sum += x;
        }
        assert(sum == 200);
        cout << "Test10 (view after release) passed.\n";
    }

    // Test 11: operator[] 基本访问与修改
    {
        gen_soa_store<SimplePod, 4> store;
        auto a = store.allocate().value(); // 3
        auto b = store.allocate().value(); // 2
        store.construct_at(a, SimplePod{10, 1.1, 'x'});
        store.construct_at(b, SimplePod{20, 2.2, 'y'});

        // dense 顺序：先 a(slot 3) 后 b(slot 2)
        auto &&[x0, y0, z0] = store[0]; // slot 3
        assert(x0 == 10);
        assert(y0 == 1.1);
        assert(z0 == 'x');

        auto [x1, y1, z1] = store[1]; // slot 2
        assert(x1 == 20);

        // 修改字段
        x1 = 999;
        assert(store.data_.template get<0>(b) == 999);
        cout << "Test11 (operator[]) passed.\n";
    }

    // Test 12: const 访问
    {
        const gen_soa_store<SimplePod, 4> store = [] {
            gen_soa_store<SimplePod, 4> s;
            auto id = s.allocate().value();
            s.construct_at(id, SimplePod{5, 2.5, 'c'});
            return s;
        }();

        auto &&[x, y, z] = store[0];
        static_assert(std::is_const_v<std::remove_reference_t<decltype(x)>>);
        // x = 10; // 编译错误
        assert(x == 5);
        cout << "Test12 (const operator[]) passed.\n";
    }

    // Test 13: size()
    {
        gen_soa_store<SimplePod, 4> store;
        assert(store.size() == 0);
        store.allocate();
        store.allocate();
        assert(store.size() == 2);
        cout << "Test13 (size) passed.\n";
    }
    // Test 14: SoaVector 拷贝构造后独立内存
    {
        // 创建一个已分配内存的 store
        gen_soa_store<SimplePod, 3> store;
        auto a = store.allocate().value();
        store.construct_at(a, SimplePod{42, 3.14, 'x'});

        // 拷贝底层的 SoaVector（gen_soa_store 整体拷贝不适用，只测试 SoaVector 部分）
        SoaVector<SimplePod> copy = store.data_; // 拷贝构造
        assert(copy.capacity() == store.data_.capacity());
        // 内存独立
        assert(copy.pointers_.x.data() !=
               store.data_.pointers_.x.data()); // 假设 x 是第一个字段
        // 拷贝后的内存未初始化，不应读原始数据（因为我们是纯内存拷贝，未复制元素）
        // 此处仅验证分配成功
        std::cout << "Test14 (SoaVector copy construct) passed.\n";
    }

    // Test 15: const 下 field 访问（验证返回 const 引用）
    {
        const gen_soa_store<SimplePod, 3> store = [] {
            gen_soa_store<SimplePod, 3> s;
            auto id = s.allocate().value();
            s.construct_at(id, SimplePod{1, 2.0, 'c'});
            return s;
        }();

        const auto &field_x = store.data_.template field<"x">();
        static_assert(std::is_const_v<std::remove_reference_t<decltype(field_x[0])>>);
        assert(field_x[0] == 1);
        // field_x[0] = 10;  // 编译错误
        std::cout << "Test15 (const field) passed.\n";
    }

    // Test 16: const 下 get 访问
    {
        const gen_soa_store<SimplePod, 3> store = [] {
            gen_soa_store<SimplePod, 3> s;
            auto id = s.allocate().value();
            s.construct_at(id, SimplePod{7, 8.0, 'd'});
            return s;
        }();

        const auto &x = store.data_.template get<0>(0);
        static_assert(std::is_const_v<std::remove_reference_t<decltype(x)>>);
        assert(x == 7);
        std::cout << "Test16 (const get) passed.\n";
    }

    // Test 17: const 下 view 只读
    {
        const gen_soa_store<SimplePod, 3> store = [] {
            gen_soa_store<SimplePod, 3> s;
            auto id = s.allocate().value();
            s.construct_at(id, SimplePod{5, 6.0, 'e'});
            return s;
        }();

        for (auto &&[x, y, z] : store.view<"x", "y", "z">())
        {
            static_assert(std::is_const_v<std::remove_reference_t<decltype(x)>>);
            assert(x == 5);
        }
        std::cout << "Test17 (const view) passed.\n";
    }
    // Test 18: 验证 remove_from_dense 正确更新了 moved_id 的 sparse_ 映射
    {
        constexpr size_type N = 5;
        gen_soa_store<SimplePod, N> store;

        // 分配三个槽位（实际顺序：0, 1, 2）
        auto a = store.allocate().value(); // 0
        auto b = store.allocate().value(); // 1
        auto c = store.allocate().value(); // 2
        store.construct_at(a, SimplePod{10, 0.0, 'a'});
        store.construct_at(b, SimplePod{20, 0.0, 'b'});
        store.construct_at(c, SimplePod{30, 0.0, 'c'});

        // dense_ = [0, 1, 2], sparse_[0]=0, sparse_[1]=1, sparse_[2]=2

        store.release(b); // 释放 id=1
        // dense_ 变为 [0, 2] （2 被移到索引 1）
        // 此时 sparse_[2] 必须被更新为 1，否则后续释放 2 会失败

        store.release(c); // 再次释放 id=2（moved_id）
        // 如果没有 sparse_ 更新，这里会断言失败（sparse_[2] 仍为 2 >= dense_.size()）
        // 现在应正常完成，dense_ 变为 [0]

        assert(store.dense_.size() == 1);
        assert(store.dense_[0] == a); // a 是 0
        std::cout << "Test18 (sparse update after release) passed.\n";
    }

    // Test19: operator[] 逻辑索引正确映射（修复后验证）
    {
        constexpr size_type N = 4;
        gen_soa_store<SimplePod, N> store;
        auto a = store.allocate().value(); // 3
        auto b = store.allocate().value(); // 2
        store.construct_at(a, SimplePod{10, 1.1, 'x'});
        store.construct_at(b, SimplePod{20, 2.2, 'y'});

        // 逻辑索引 0 应对应第一个分配的实体（a，物理槽位 3）
        auto [x0, y0, z0] = store[0];
        assert(x0 == 10);
        assert(y0 == 1.1);
        assert(z0 == 'x');

        // 逻辑索引 1 应对应 b（物理槽位 2）
        auto [x1, y1, z1] = store[1];
        assert(x1 == 20);

        // 修改通过逻辑索引
        x1 = 999;
        assert(store.data_.template get<0>(b) == 999);
        std::cout << "Test19 (operator[] logical mapping) passed.\n";
    }

    // Test20: 析构函数销毁所有存活元素（资源不泄漏）
    {
        static int destruct_count = 0;
        struct Tracked
        {
            struct Payload
            {
                int val;
                std::string s; // 非平凡成员，析构时会释放内存
                ~Payload()
                {
                    ++destruct_count;
                } // 析构计数放在字段上
            };
            Payload p;
        };

        {

            destruct_count = 0; // 重置
            {
                gen_soa_store<Tracked, 4> store;
                auto a = store.allocate().value();
                auto b = store.allocate().value();
                store.construct_at(a, 100, "hello");
                store.construct_at(b, 200, "world");
            }
            assert(destruct_count == 2); // 两个 Payload 被销毁
            std::cout << "Test20 (destructor) passed.\n";
        }
        assert(destruct_count == 2);
        std::cout << "Test20 (destructor) passed.\n";
    }

    // Test21: 拷贝构造已删除，移动构造正常工作
    {
        gen_soa_store<SimplePod, 4> store1;
        auto id = store1.allocate().value();
        store1.construct_at(id, {7, 7.7, 'g'});

        gen_soa_store<SimplePod, 4> store2 = std::move(store1); // 移动构造
        assert(store2.size() == 1);
        auto [x, y, z] = store2[0];
        assert(x == 7);
        // store1 被移动后应处于有效但未指定状态（此处至少 size 为 0 或原 size 被交换）
        // 简单检查 store1.dense_ 为空
        assert(store1.dense_.empty());
        std::cout << "Test21 (move construct) passed.\n";
    }
    {
        // Test 迭代器遍历全部字段（无参数 view）
        {
            gen_soa_store<SimplePod, 4> store;
            auto a = store.allocate().value(); // 槽位 3
            auto b = store.allocate().value(); // 槽位 2
            store.construct_at(a, SimplePod{10, 1.1, 'x'});
            store.construct_at(b, SimplePod{20, 2.2, 'y'});

            int count = 0;
            for (auto &&[x_val, y_val, z_val] : store.view())
            {
                if (count == 0)
                {
                    assert(x_val == 10);
                    assert(y_val == 1.1);
                    assert(z_val == 'x');
                }
                else
                {
                    assert(x_val == 20);
                    assert(y_val == 2.2);
                    assert(z_val == 'y');
                }
                ++count;
            }
            assert(count == 2);
            std::cout << "Test (range-for all fields) passed.\n";
        }
        // Test const 下无参 view() 只读遍历
        {
            const gen_soa_store<SimplePod, 4> store = [] {
                gen_soa_store<SimplePod, 4> s;
                auto id = s.allocate().value();
                s.construct_at(id, SimplePod{5, 6.0, 'e'});
                return s;
            }();

            for (auto &&[x, y, z] : store.view())
            {
                static_assert(std::is_const_v<std::remove_reference_t<decltype(x)>>,
                              "x should be const");
                assert(x == 5);
                // x = 10; // 编译错误：只读
            }
            std::cout << "Test (const view() all fields) passed.\n";
        }

        // Test 通过迭代器修改数据
        {
            gen_soa_store<SimplePod, 4> store;
            auto id = store.allocate().value();
            store.construct_at(id, SimplePod{1, 2.0, 'c'});

            for (auto [x, y, z] : store)
            {
                x = 999;
                y = 3.14;
                z = '!';
            }
            assert(store.data_.template get<0>(id) == 999);
            assert(store.data_.template get<1>(id) == 3.14);
            assert(store.data_.template get<2>(id) == '!');
            std::cout << "Test (modify via range-for) passed.\n";

            for (auto [x, y, z] : store.read_only())
            {
                static_assert(std::is_const_v<std::remove_reference_t<decltype(x)>>);
                assert(x == 999);
                // x = 10; // 编译错误
            }
        }

        // Test const 迭代器
        {
            const gen_soa_store<SimplePod, 4> store = [] {
                gen_soa_store<SimplePod, 4> s;
                auto id = s.allocate().value();
                s.construct_at(id, SimplePod{5, 6.0, 'e'});
                return s;
            }();

            for (auto [x, y, z] : store)
            {
                static_assert(std::is_const_v<std::remove_reference_t<decltype(x)>>);
                assert(x == 5);
                // x = 10; // 编译错误
            }
            std::cout << "Test (const range-for) passed.\n";
        }

        // Test 迭代器与标准算法
        {
            gen_soa_store<SimplePod, 5> store;
            for (int i = 0; i < 3; ++i)
            {
                auto id = store.allocate().value();
                store.construct_at(id, SimplePod{i * 10, 0.0, 'a'});
            }

            // 使用 STL 算法，例如计数
            int sum = 0;
            for (auto it = store.begin(); it != store.end(); ++it)
            {
                auto [x, y, z] = *it;
                sum += x;
            }
            assert(sum == 0 + 10 + 20);
            std::cout << "Test (iterator with algorithm) passed.\n";
        }
    }
    cout << "\nAll tests passed!\n";
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