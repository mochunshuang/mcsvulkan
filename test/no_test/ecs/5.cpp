#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <print>
#include <ranges>
#include <meta>
#include <cassert>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <algorithm>
#include <vector>

// ---------- 基础支持 ----------
using size_type = uint32_t;

struct static_string
{
    const char *value{};
    template <size_t N>
    consteval static_string(const char (&str)[N]) noexcept
        : value{std::define_static_string(str)}
    {
    }
    consteval explicit static_string(const char *p) noexcept : value{p} {}
    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return {value};
    }
    constexpr bool operator==(const static_string &o) const noexcept
    {
        return view() == o.view();
    }
    constexpr bool operator==(std::string_view o) const noexcept
    {
        return view() == o;
    }
    template <size_t N>
    consteval bool operator==(const char (&str)[N]) const noexcept
    {
        return view() == std::string_view{str, N - 1};
    }
};

struct name_spec
{
    static_string name;
    std::meta::info info;
};

static consteval auto nsdms_of(std::meta::info info)
{
    return std::define_static_array(
        std::meta::nonstatic_data_members_of(info, std::meta::access_context::current()));
}

static consteval auto nsdms_spec_transformed(std::meta::info input, auto f)
{
    return std::meta::nonstatic_data_members_of(input,
                                                std::meta::access_context::current()) |
           std::views::transform([=](auto m) -> std::meta::info {
               return std::meta::data_member_spec(f(std::meta::type_of(m)),
                                                  {.name = std::meta::identifier_of(m)});
           });
}

template <typename T>
class soa_member_pointer
{
    T *data_{nullptr};

  public:
    using value_type = T;
    constexpr soa_member_pointer() noexcept = default;
    constexpr explicit soa_member_pointer(T *data) noexcept : data_{data} {}
    constexpr decltype(auto) operator[](this auto &&self, size_t idx) noexcept
    {
        return std::forward_like<decltype(self)>(self.data_[idx]);
    }
    const T *data() const noexcept
    {
        return data_;
    }
    T *data() noexcept
    {
        return data_;
    }
};

namespace detail
{
    constexpr static doubleIncrease(size_type capacity) noexcept
    {
        return capacity * 2;
    }
} // namespace detail

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
    [[no_unique_address]] allocator_type alloc_{};

    static constexpr auto ptr_members = nsdms_of(^^Pointers);
    static constexpr auto members = nsdms_of(^^T);

    template <typename U>
    constexpr void grow_field(soa_member_pointer<U> &field, size_type old_cap,
                              size_type new_cap)
    {
        using RebAlloc =
            typename std::allocator_traits<allocator_type>::template rebind_alloc<U>;
        RebAlloc ra(alloc_);
        auto *old = field.data();
        auto *neW = ra.allocate(new_cap);
        if (old)
            ra.deallocate(old, old_cap);
        field = soa_member_pointer<U>(neW);
    }

    constexpr void grow(size_type new_cap)
    {
        if (new_cap <= capacity_)
            return;
        size_type old = capacity_;
        capacity_ = new_cap;
        template for (constexpr auto I : std::views::indices(ptr_members.size()))
        {
            auto &f = pointers_.[:ptr_members[I]:];
            grow_field(f, old, new_cap);
        }
    }

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
    constexpr size_type capacity() const noexcept
    {
        return capacity_;
    }
    constexpr void reserve(size_type n)
    {
        if (n > capacity_)
            grow(n);
    }

    template <size_type I>
    decltype(auto) constexpr get(this auto &&self, size_type idx) noexcept
    {
        return std::forward_like<decltype(self)>(self.pointers_.[:ptr_members[I]:][idx]);
    }

    constexpr void destroy_at(size_type idx) noexcept
    {
        template for (constexpr auto I : std::views::indices(ptr_members.size()))
        {
            auto &f = pointers_.[:ptr_members[I]:];
            std::destroy_at(f.data() + idx);
        }
    }

    template <size_type I, typename... Args>
    constexpr void construct_field(size_type idx, Args &&...args) noexcept(
        std::is_nothrow_constructible_v<
            typename std::remove_reference_t<
                decltype(pointers_.[:ptr_members[I]:])>::value_type,
            Args...>)
    {
        auto &f = pointers_.[:ptr_members[I]:];
        std::construct_at(f.data() + idx, std::forward<Args>(args)...);
    }

    // 多参数构造（逐个字段）
    template <typename... Args>
        requires(sizeof...(Args) == ptr_members.size())
    constexpr void construct_at(size_type idx, Args &&...args) noexcept(
        std::is_nothrow_constructible_v<T, Args...>)
    {
        template for (constexpr auto I : std::views::indices(ptr_members.size()))
        {
            construct_field<I>(idx, std::forward<Args...[I]>(args...[I]));
        }
    }

    // 接受 T 值进行移动构造
    constexpr void construct_at(size_type idx,
                                T t) noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        template for (constexpr auto I : std::views::indices(ptr_members.size()))
        {
            auto &field = t.[:members[I]:];
            construct_field<I>(idx, std::move(field));
        }
    }

    // 物理索引访问（返回临时 tuple，不用 decltype(auto) 避免引用问题）
    constexpr auto operator[](this auto &&self, size_type idx) noexcept
    {
        auto [... ptrs] = self.pointers_;
        using Tuple =
            std::tuple<decltype(std::forward_like<decltype(self)>(ptrs[idx]))...>;
        return Tuple{std::forward_like<decltype(self)>(ptrs[idx])...};
    }

    constexpr void destroy() noexcept
    {
        if (capacity_ == 0)
            return;
        template for (constexpr auto I :
                      std::views::indices(ptr_members.size()) | std::views::reverse)
        {
            auto &f = pointers_.[:ptr_members[I]:];
            using FT = typename std::remove_reference_t<decltype(f)>::value_type;
            typename std::allocator_traits<allocator_type>::template rebind_alloc<FT>(
                alloc_)
                .deallocate(f.data(), capacity_);
            f = {};
        }
        capacity_ = 0;
    }
    constexpr ~SoaVector() noexcept
    {
        destroy();
    }
    // 省略拷贝/移动以保持简洁，实际使用需根据需求实现
};

// ---------- 简化后的 gen_soa_store (无 sparse_) ----------
template <typename T, size_type init_size = 8, auto grow_algo = detail::doubleIncrease,
          template <typename> class Alloc = std::allocator,
          template <typename, auto, template <typename> class> class Make = SoaVector>
struct gen_soa_store
{
    using soa_type = Make<T, grow_algo, Alloc>;
    using value_type = T;
    using id_type = size_type;

    soa_type data_;
    std::vector<size_type> dense_; // 活跃槽位（物理 id）
    std::vector<size_type> free_;  // 空闲槽位

    static constexpr size_type invalid_id = ~0;

    constexpr gen_soa_store(size_type size = init_size) : data_(size)
    {
        dense_.reserve(size);
        free_.reserve(size);
        for (size_type i = size; i > 0; --i)
            free_.push_back(i - 1);
    }

    constexpr auto default_next_capacity() const noexcept
    {
        return data_.new_capacity();
    }

    constexpr std::optional<size_type> allocate() noexcept
    {
        if (free_.empty())
            return std::nullopt;
        auto id = free_.back();
        free_.pop_back();
        dense_.push_back(id);
        return id;
    }

    constexpr void release(size_type id) noexcept
    {
        auto it = std::ranges::find(dense_, id);
        if (it != dense_.end())
        {
            *it = dense_.back();
            dense_.pop_back();
            data_.destroy_at(id);
            free_.push_back(id);
        }
    }

    constexpr void grow(size_type new_cap)
    {
        if (new_cap <= data_.capacity())
            return;
        auto old_cap = data_.capacity();

        // 分配新内存并搬迁元素（保持物理槽位不变）
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
        for (size_type i = 0; i < dense_.size(); ++i)
        {
            size_type slot = dense_[i];
            template for (constexpr auto I :
                          std::views::indices(soa_type::ptr_members.size()))
            {
                auto &old = data_.pointers_.[:soa_type::ptr_members[I]:];
                auto &neW = new_pointers.[:soa_type::ptr_members[I]:];
                std::construct_at(neW.data() + slot, std::move(old[slot]));
                std::destroy_at(old.data() + slot);
            }
        }
        // 释放旧内存
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
        data_.capacity_ = new_cap;
        data_.pointers_ = std::move(new_pointers);
        // 扩展 free_
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

    constexpr size_type size() const noexcept
    {
        return dense_.size();
    }

    // 遍历活跃槽位
    constexpr auto view(this auto &&self) noexcept
    {
        return self.dense_ | std::views::transform([&self](size_type slot) {
                   return self.data_[slot]; // 按值返回临时 tuple
               });
    }

    // 物理索引访问（按值返回临时 tuple）
    constexpr auto phys(this auto &&self, size_type id) noexcept
    {
        return self.data_[id];
    }

    constexpr ~gen_soa_store() noexcept
    {
        for (auto id : dense_)
            data_.destroy_at(id);
    }

    gen_soa_store(const gen_soa_store &) = delete;
    gen_soa_store &operator=(const gen_soa_store &) = delete;
    gen_soa_store(gen_soa_store &&) noexcept = default;
    gen_soa_store &operator=(gen_soa_store &&) noexcept = default;
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
    NonTrivial(const NonTrivial &) = delete;
    NonTrivial &operator=(const NonTrivial &) = delete;
    NonTrivial(NonTrivial &&) noexcept = default;
    NonTrivial &operator=(NonTrivial &&) noexcept = default;
};

// ---------- proxy_value（测试用） ----------
template <typename Store>
struct proxy_value
{
    Store *store_;
    typename Store::id_type id_;
    proxy_value(Store &s, typename Store::id_type id) noexcept : store_{&s}, id_{id} {}
    proxy_value(const proxy_value &) = delete;
    proxy_value &operator=(const proxy_value &) = delete;
    proxy_value(proxy_value &&o) noexcept
        : store_{std::exchange(o.store_, nullptr)}, id_{std::exchange(o.id_, 0)}
    {
    }
    proxy_value &operator=(proxy_value &&o) noexcept
    {
        if (this != &o)
        {
            release();
            store_ = std::exchange(o.store_, nullptr);
            id_ = std::exchange(o.id_, 0);
        }
        return *this;
    }
    void release() noexcept
    {
        if (store_)
        {
            store_->release(id_);
            store_ = nullptr;
        }
    }
    ~proxy_value() noexcept
    {
        release();
    }
    auto operator*() noexcept
    {
        return store_->data_[id_];
    }
    auto operator*() const noexcept
    {
        return store_->data_[id_];
    }
};

// ---------- 测试 ----------
int main()
{
    using namespace std;

    // Test 1: 基本分配/释放/重用
    {
        gen_soa_store<SimplePod, 4> pool;
        auto id1 = pool.allocate().value();
        pool.construct_at(id1, SimplePod{10, 3.14, 'a'});
        assert(pool.size() == 1);

        auto id2 = pool.allocate().value();
        pool.construct_at(id2, SimplePod{20, 2.71, 'b'});
        assert(pool.size() == 2);

        // 通过 phys 访问
        auto [x1, y1, z1] = pool.phys(id1);
        assert(x1 == 10 && y1 == 3.14 && z1 == 'a');

        pool.release(id1);
        assert(pool.size() == 1);
        assert(pool.free_.back() == id1); // 回收

        auto id3 = pool.allocate().value(); // 重用
        assert(id3 == id1);
        pool.construct_at(id3, SimplePod{30, 0.0, 'c'});
        auto [x3, y3, z3] = pool.phys(id3);
        assert(x3 == 30);

        int count = 0;
        for (auto [x, y, z] : pool.view())
            ++count;
        assert(count == 2);
        println("Test1 passed.");
    }

    // Test 2: 扩容保持物理 id 不变
    {
        gen_soa_store<SimplePod, 2> pool;
        auto a = pool.allocate().value();
        auto b = pool.allocate().value();
        pool.construct_at(a, SimplePod{100, 1.0, 'A'});
        pool.construct_at(b, SimplePod{200, 2.0, 'B'});
        pool.grow(4);
        assert(pool.data_.capacity() == 4);
        auto [xa, ya, za] = pool.phys(a);
        assert(xa == 100);
        auto [xb, yb, zb] = pool.phys(b);
        assert(xb == 200);
        auto c = pool.allocate().value();
        pool.construct_at(c, SimplePod{300, 3.0, 'C'});
        auto [xc, yc, zc] = pool.phys(c);
        assert(xc == 300);
        println("Test2 passed.");
    }

    // Test 3: proxy 移动与自动释放
    {
        gen_soa_store<SimplePod, 4> pool;
        auto id = pool.allocate().value();
        pool.construct_at(id, SimplePod{5, 5.0, 'e'});
        {
            proxy_value pv(pool, id);
            auto [x, y, z] = *pv;
            assert(x == 5);
        } // 自动释放
        assert(pool.size() == 0);
        auto id2 = pool.allocate().value();
        assert(id2 == id); // 重用
        println("Test3 passed.");
    }

    // Test 4: 遍历
    {
        gen_soa_store<SimplePod, 4> pool;
        pool.construct_at(*pool.allocate(), SimplePod{1, 0.0, 'x'});
        pool.construct_at(*pool.allocate(), SimplePod{2, 0.0, 'y'});
        int sum = 0;
        for (auto [x, y, z] : pool.view())
            sum += x;
        assert(sum == 3);
        println("Test4 passed.");
    }

    println("\nAll tests passed!");
    return 0;
}