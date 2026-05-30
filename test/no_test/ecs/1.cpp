#include <cstddef>
#include <cstdint>
#include <iostream>
#include <exception>
#include <memory>
#include <ranges>
#include <meta>

#include <cassert>
#include <string>
#include <memory_resource>
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

    size_type size_{};
    size_type capacity_{};
    Pointers pointers_{};
    [[no_unique_address]] allocator_type alloc_{}; // 源分配器

    static_assert(std::is_standard_layout_v<Pointers>, "check Pointers faild.");

    static constexpr auto ptr_members = nsdms_of(^^Pointers);

    template <typename U>
    constexpr void grow_field(soa_member_pointer<U> &field, size_type old_cap,
                              size_type new_cap)
    {
        using RebAlloc =
            typename std::allocator_traits<allocator_type>::template rebind_alloc<U>;
        RebAlloc reb_alloc(alloc_); // 拷贝构造，对 pmr 就是复制 memory_resource*

        auto *old_data = field.data();
        auto *new_data = reb_alloc.allocate(new_cap);

        if (old_data)
        {
            std::uninitialized_move_n(old_data, size_, new_data);
            std::destroy(old_data, old_data + size_);
            reb_alloc.deallocate(old_data, old_cap);
        }
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

    constexpr void default_construct_all()
    {
        template for (constexpr auto I : std::views::indices(ptr_members.size()))
        {
            auto &field = pointers_.[:ptr_members[I]:];
            std::uninitialized_default_construct(field.data(), field.data() + size_);
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
    constexpr explicit SoaVector(size_type size) : SoaVector(size, allocator_type{}) {}
    constexpr SoaVector(size_type size, auto alloc)
        : size_{size}, alloc_{std::move(alloc)}
    {
        if (size == 0)
            return;
        grow(size);
        default_construct_all();
    }
    constexpr auto new_capacity() const noexcept
    {
        return grow_algo(capacity_);
    }
    // 容量相关
    constexpr size_type size() const noexcept
    {
        return size_;
    }
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

    void resize(size_type new_size)
    {
        if (new_size <= size_)
        {
            // 缩小：销毁多余元素
            template for (constexpr auto I : std::views::indices(ptr_members.size()))
            {
                auto &field = pointers_.[:ptr_members[I]:];
                std::destroy(field.data() + new_size, field.data() + size_);
            }
        }
        else
        {
            // 扩大：先预留容量
            reserve(new_size);
            // 默认构造新元素
            template for (constexpr auto I : std::views::indices(ptr_members.size()))
            {
                auto &field = pointers_.[:ptr_members[I]:];
                std::uninitialized_default_construct(field.data() + size_,
                                                     field.data() + new_size);
            }
        }
        size_ = new_size;
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
        return std::span(ptrs...[I].data(), self.size_);
    }
    template <static_string name>
    constexpr decltype(auto) span(this auto &&self) noexcept
    {
        auto &&[... ptrs] = std::forward_like<decltype(self)>(self.pointers_);
        constexpr auto I = SoaVector::get_field_by_name(name);
        return std::span(ptrs...[I].data(), self.size_);
    }

    constexpr decltype(auto) view(this auto &&self) noexcept
    {
        auto &&[... ptrs] = std::forward_like<decltype(self)>(self.pointers_);
        return std::views::zip(std::span{ptrs.data(), self.size_}...);
    }
    template <static_string... name>
        requires(sizeof...(name) > 0)
    constexpr decltype(auto) view(this auto &&self) noexcept
    {
        return std::views::zip(
            std::span{std::forward<decltype(self)>(self).template field<name>().data(),
                      self.size_}...);
    }

    constexpr decltype(auto) begin(this auto &&self) noexcept
    {
        return std::forward<decltype(self)>(self).view().begin();
    }
    constexpr decltype(auto) end(this auto &&self) noexcept
    {
        return std::forward<decltype(self)>(self).view().end();
    }

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

            std::destroy(field.data(), field.data() + size_);
            reb_alloc.deallocate(field.data(), capacity_);
            field = {};
        }
        size_ = 0;
        capacity_ = 0;
    }

    constexpr ~SoaVector() noexcept
    {
        destroy();
    }

    constexpr SoaVector(const SoaVector &o)
        : size_{o.size_}, capacity_{o.size_},
          alloc_{std::allocator_traits<
              allocator_type>::select_on_container_copy_construction(o.alloc_)}
    {
        if (size_ == 0)
            return;
        // 为每个字段分配并复制
        template for (constexpr auto I : std::views::indices(ptr_members.size()))
        {
            auto &dst_field = pointers_.[:ptr_members[I]:];
            auto &src_field = o.pointers_.[:ptr_members[I]:];
            using FieldType =
                typename std::remove_reference_t<decltype(dst_field)>::value_type;
            using RebAlloc = typename std::allocator_traits<
                allocator_type>::template rebind_alloc<FieldType>;
            RebAlloc reb_alloc(alloc_);
            auto *new_data = reb_alloc.allocate(capacity_);
            std::uninitialized_copy_n(src_field.data(), size_, new_data);

            dst_field = soa_member_pointer<FieldType>(new_data);
        }
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
        : size_{std::exchange(o.size_, 0)}, capacity_{std::exchange(o.capacity_, 0)},
          pointers_{std::move(o.pointers_)}, alloc_{std::move(o.alloc_)}
    {
    }
    constexpr SoaVector &operator=(SoaVector &&o) noexcept
    {
        if (this != &o)
        {
            destroy();
            size_ = std::exchange(o.size_, 0);
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

    constexpr void shrink_to_fit() noexcept
    {
        if (capacity_ == size_)
            return;

        const size_type new_cap = size_;
        if (new_cap == 0)
        {
            destroy();
            return;
        }

        // 阶段1：为每个字段分配新内存（可能抛 std::bad_alloc）
        Pointers new_pointers{};
        try
        {
            template for (constexpr auto I : std::views::indices(ptr_members.size()))
            {
                auto &old_field = pointers_.[:ptr_members[I]:];
                using FieldType =
                    typename std::remove_reference_t<decltype(old_field)>::value_type;
                using RebAlloc = typename std::allocator_traits<
                    allocator_type>::template rebind_alloc<FieldType>;
                RebAlloc reb_alloc(alloc_);
                auto *new_data = reb_alloc.allocate(new_cap);
                new_pointers.[:ptr_members[I]:] = soa_member_pointer<FieldType>(new_data);
            }
        }
        catch (...)
        {
            // 回滚：释放已成功分配的字段
            template for (constexpr auto J : std::views::indices(ptr_members.size()))
            {
                auto &field = new_pointers.[:ptr_members[J]:];
                if (field.data())
                {
                    using BadType =
                        typename std::remove_reference_t<decltype(field)>::value_type;
                    using BadAlloc = typename std::allocator_traits<
                        allocator_type>::template rebind_alloc<BadType>;
                    BadAlloc bad_alloc(alloc_);
                    bad_alloc.deallocate(field.data(), new_cap);
                }
            }
            return;
        }

        // 阶段2：移动元素到新内存（可能抛元素移动构造异常）
        try
        {
            template for (constexpr auto I : std::views::indices(ptr_members.size()))
            {
                auto &old_field = pointers_.[:ptr_members[I]:];
                auto &new_field = new_pointers.[:ptr_members[I]:];
                std::uninitialized_move_n(old_field.data(), size_, new_field.data());
            }
        }
        catch (...)
        {
            // 释放所有新内存（uninitialized_move_n 在抛出前已销毁已构造元素）
            template for (constexpr auto I : std::views::indices(ptr_members.size()))
            {
                auto &new_field = new_pointers.[:ptr_members[I]:];
                if (new_field.data())
                {
                    using FieldType =
                        typename std::remove_reference_t<decltype(new_field)>::value_type;
                    using RebAlloc = typename std::allocator_traits<
                        allocator_type>::template rebind_alloc<FieldType>;
                    RebAlloc reb_alloc(alloc_);
                    reb_alloc.deallocate(new_field.data(), new_cap);
                }
            }
            return;
        }

        // 阶段3：销毁旧元素并释放旧内存
        template for (constexpr auto I : std::views::indices(ptr_members.size()))
        {
            auto &old_field = pointers_.[:ptr_members[I]:];
            using FieldType =
                typename std::remove_reference_t<decltype(old_field)>::value_type;
            std::destroy(old_field.data(), old_field.data() + size_);
            using RebAlloc = typename std::allocator_traits<
                allocator_type>::template rebind_alloc<FieldType>;
            RebAlloc reb_alloc(alloc_);
            reb_alloc.deallocate(old_field.data(), capacity_);
        }

        // 阶段4：替换指针和容量
        pointers_ = std::move(new_pointers);
        capacity_ = new_cap;
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

    constexpr gen_soa_store() : data_(init_size), dense_{}, sparse_{}, free_(init_size)
    {
        size_type slot_value = init_size - 1;
        for (auto &slot : free_)
        {
            slot = slot_value--;
        }
    }

    auto allocate()
    {
        if (free_.empty())
        {
        }
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

// NOLINTEND

int main()
try
{
    using namespace std;

    // 1. 空容器构造与析构
    {
        SoaVector<SimplePod> empty(0);
        assert(empty.size() == 0);
        assert(empty.capacity() == 0);
        // 析构不应出错
    }
    cout << "[PASS] Empty container\n";

    // 2. 基本构造与元素访问
    {
        SoaVector<SimplePod> soa(3); // 分配3个元素
        assert(soa.size() == 3);
        assert(soa.capacity() >= soa.size());

        // 写入 x, y, z
        soa.get<0>(0) = 10;   // int x
        soa.get<1>(0) = 3.14; // double y
        soa.get<2>(0) = 'A';  // char z

        soa.get<0>(1) = 20;
        soa.get<1>(1) = 2.71;
        soa.get<2>(1) = 'B';

        soa.get<0>(2) = 30;
        soa.get<1>(2) = 1.62;
        soa.get<2>(2) = 'C';

        // 检查值
        assert(soa.get<0>(0) == 10 && soa.get<1>(0) == 3.14 && soa.get<2>(0) == 'A');
        assert(soa.get<0>(1) == 20 && soa.get<1>(1) == 2.71 && soa.get<2>(1) == 'B');
        assert(soa.get<0>(2) == 30 && soa.get<1>(2) == 1.62 && soa.get<2>(2) == 'C');
    }
    cout << "[PASS] Construction and element access\n";

    // 3. resize 扩大
    {
        SoaVector<SimplePod> soa(2);
        soa.get<0>(0) = 100;
        soa.get<0>(1) = 200;

        soa.resize(5); // 扩大到5，新元素默认构造
        assert(soa.size() == 5);
        assert(soa.capacity() >= 5);

        // 原有数据应保留
        assert(soa.get<0>(0) == 100);
        assert(soa.get<0>(1) == 200);

        //NOTE: 不会默认初始化： uninitialized_default_construct
        // 新元素应默认初始化 (int = 0, double = 0.0, char = 0)
        // assert(soa.get<0>(2) == 0 && soa.get<1>(2) == 0.0 && soa.get<2>(2) == 0);
    }
    cout << "[PASS] Resize to larger\n";

    // 4. resize 缩小
    {
        SoaVector<SimplePod> soa(4);
        // 给所有元素设值
        for (size_t i = 0; i < 4; ++i)
            soa.get<0>(i) = int(i);
        soa.resize(2); // 缩小到2
        assert(soa.size() == 2);
        assert(soa.get<0>(0) == 0);
        assert(soa.get<0>(1) == 1);
        // 析构不应出错
    }
    cout << "[PASS] Resize to smaller\n";

    // 5. reserve 与容量增长
    {
        SoaVector<SimplePod> soa(1);
        size_t initial_cap = soa.capacity();
        soa.reserve(100);
        assert(soa.capacity() >= 100);
        assert(soa.size() == 1); // size 不变
        // 原有数据可用
        soa.get<0>(0) = 42;
        assert(soa.get<0>(0) == 42);
    }
    cout << "[PASS] Reserve\n";

    // 6. 移动构造
    {
        SoaVector<SimplePod> src(3);
        src.get<0>(0) = 11;
        src.get<0>(1) = 22;
        src.get<0>(2) = 33;

        SoaVector<SimplePod> dst(std::move(src));
        assert(dst.size() == 3);
        assert(dst.get<0>(0) == 11 && dst.get<0>(1) == 22 && dst.get<0>(2) == 33);
        // 源对象应被清空（可析构）
        assert(src.size() == 0);
        assert(src.capacity() == 0);
    }
    cout << "[PASS] Move constructor\n";

    // 7. 移动赋值
    {
        SoaVector<SimplePod> a(2);
        a.get<0>(0) = 99;
        SoaVector<SimplePod> b(3);
        b.get<0>(0) = 77;

        b = std::move(a);
        assert(b.size() == 2);
        assert(b.get<0>(0) == 99);
        // a 被重置
        assert(a.size() == 0);
    }
    cout << "[PASS] Move assignment\n";

    // 8. 自定义分配器（跟踪分配次数）
    {
        // 重置计数器（按字段类型）
        TrackingAlloc<int>::alloc_count = 0;
        TrackingAlloc<int>::dealloc_count = 0;
        TrackingAlloc<int>::allocated_bytes = 0;
        TrackingAlloc<double>::alloc_count = 0;
        TrackingAlloc<double>::dealloc_count = 0;
        TrackingAlloc<double>::allocated_bytes = 0;
        TrackingAlloc<char>::alloc_count = 0;
        TrackingAlloc<char>::dealloc_count = 0;
        TrackingAlloc<char>::allocated_bytes = 0;

        {
            using CustomAlloc = TrackingAlloc<SimplePod>; // 具体分配器类型
            SoaVector<SimplePod, [](uint32_t cap) { return cap == 0 ? 2 : cap * 2; },
                      TrackingAlloc>
                soa(4);

            // 3个字段各分配一次
            assert(TrackingAlloc<int>::alloc_count == 1);
            assert(TrackingAlloc<double>::alloc_count == 1);
            assert(TrackingAlloc<char>::alloc_count == 1);

            soa.get<0>(1) = 123;
            assert(soa.get<0>(1) == 123);

            soa.resize(6); // 扩容，重新分配一次（各字段再分配+释放）
            assert(soa.get<0>(1) == 123);
        }
        // 析构后，分配次数 == 释放次数，且字节数相等
        assert(TrackingAlloc<int>::alloc_count == TrackingAlloc<int>::dealloc_count);
        assert(TrackingAlloc<int>::allocated_bytes ==
               TrackingAlloc<int>::deallocated_bytes);
        assert(TrackingAlloc<double>::alloc_count ==
               TrackingAlloc<double>::dealloc_count);
        assert(TrackingAlloc<double>::allocated_bytes ==
               TrackingAlloc<double>::deallocated_bytes);
        assert(TrackingAlloc<char>::alloc_count == TrackingAlloc<char>::dealloc_count);
        assert(TrackingAlloc<char>::allocated_bytes ==
               TrackingAlloc<char>::deallocated_bytes);
        cout << "[PASS] Custom allocator and growth policy\n";
    }

    // 9. 与 std::pmr 分配器集成
    {
        std::pmr::unsynchronized_pool_resource pool;
        SoaVector<SimplePod, [](uint32_t c) { return c == 0 ? 4 : c * 2; },
                  std::pmr::polymorphic_allocator> // 具体类型
            soa(2, &pool);                         // &pool → polymorphic_allocator<int>
        soa.get<0>(0) = 55;
        soa.resize(8);
        assert(soa.get<0>(0) == 55);
    }
    cout << "[PASS] std::pmr polymorphic allocator\n";

    // 10. 只可移动类型成员
    {
        SoaVector<NonTrivial> soa(2);
        soa.get<0>(0) = 1;        // id
        soa.get<1>(0) = "hello"s; // name（通过移动赋值）

        assert(soa.get<0>(0) == 1);
        assert(soa.get<1>(0) == "hello");

        SoaVector<NonTrivial> soa2(std::move(soa));
        assert(soa2.get<0>(0) == 1);
        assert(soa2.get<1>(0) == "hello");
    }
    cout << "[PASS] Move-only member type (with default constructor)\n";

    // 11. 大容量压测
    {
        constexpr size_t N = 10000;
        SoaVector<SimplePod> big(N);
        for (size_t i = 0; i < N; ++i)
        {
            big.get<0>(i) = static_cast<int>(i);
            big.get<1>(i) = static_cast<double>(i) * 0.5;
        }
        for (size_t i = 0; i < N; ++i)
        {
            assert(big.get<0>(i) == static_cast<int>(i));
            assert(big.get<1>(i) == static_cast<double>(i) * 0.5);
        }
    }
    cout << "[PASS] Large array (10k elements)\n";

    // 12. 多次 resize 交替
    {
        SoaVector<SimplePod> soa(3);
        soa.resize(6);
        soa.resize(2);
        soa.resize(5);
        soa.resize(5); // 相同大小
        soa.resize(0);
        soa.resize(4);
        soa.get<0>(0) = -1;
        assert(soa.get<0>(0) == -1);
    }
    cout << "[PASS] Repeated resize up/down\n";

    // 13. 成员类型别名检查（反射生成正确）
    static_assert(std::is_same_v<decltype(SoaVector<SimplePod>{0}.get<0>(0)), int &&>);
    static_assert(std::is_same_v<decltype(SoaVector<SimplePod>{0}.get<1>(0)), double &&>);
    static_assert(std::is_same_v<decltype(SoaVector<SimplePod>{0}.get<2>(0)), char &&>);
    cout << "[PASS] Static type reflection correctness\n";

    // 14. 拷贝构造
    {
        SoaVector<SimplePod> src(3);
        src.get<0>(0) = 10;
        src.get<0>(1) = 20;
        src.get<0>(2) = 30;
        src.get<1>(0) = 1.1;
        src.get<1>(1) = 2.2;
        src.get<1>(2) = 3.3;
        src.get<2>(0) = 'a';
        src.get<2>(1) = 'b';
        src.get<2>(2) = 'c';

        SoaVector<SimplePod> dst(src); // 拷贝构造

        assert(dst.size() == 3);
        assert(dst.capacity() == 3); // 容量精确匹配（或 >= size）
        // 值完全相同
        assert(dst.get<0>(0) == 10 && dst.get<0>(1) == 20 && dst.get<0>(2) == 30);
        assert(dst.get<1>(0) == 1.1 && dst.get<1>(1) == 2.2 && dst.get<1>(2) == 3.3);
        assert(dst.get<2>(0) == 'a' && dst.get<2>(1) == 'b' && dst.get<2>(2) == 'c');

        // 修改副本，不影响原对象
        dst.get<0>(0) = 999;
        assert(src.get<0>(0) == 10); // 原数据未变
    }
    cout << "[PASS] Copy constructor\n";

    // 15. 拷贝赋值
    {
        SoaVector<SimplePod> a(2);
        a.get<0>(0) = 77;
        a.get<0>(1) = 88;
        a.get<1>(0) = 7.7;
        a.get<1>(1) = 8.8;

        SoaVector<SimplePod> b(0); // 空容器
        b = a;                     // 拷贝赋值

        assert(b.size() == 2);
        assert(b.get<0>(0) == 77 && b.get<0>(1) == 88);
        assert(b.get<1>(0) == 7.7 && b.get<1>(1) == 8.8);

        // 修改 b，不影响 a
        b.get<0>(0) = 0;
        assert(a.get<0>(0) == 77);
    }
    cout << "[PASS] Copy assignment\n";

    // 16. 自赋值安全
    {
        SoaVector<SimplePod> x(1);
        x.get<0>(0) = 42;
        x = x; // 自赋值
        assert(x.size() == 1 && x.get<0>(0) == 42);
    }
    cout << "[PASS] Self-assignment\n";

    // 17. find 按条件查找（直接作用于 SoaVector）
    {
        SoaVector<SimplePod> soa(5);
        soa.get<0>(0) = 10;
        soa.get<1>(0) = 1.0;
        soa.get<2>(0) = 'a';
        soa.get<0>(1) = 20;
        soa.get<1>(1) = 2.0;
        soa.get<2>(1) = 'b';
        soa.get<0>(2) = 30;
        soa.get<1>(2) = 3.0;
        soa.get<2>(2) = 'c';
        soa.get<0>(3) = 40;
        soa.get<1>(3) = 4.0;
        soa.get<2>(3) = 'd';
        soa.get<0>(4) = 50;
        soa.get<1>(4) = 5.0;
        soa.get<2>(4) = 'e';

        // 使用标准算法直接作用于 soa
        auto idx = std::ranges::find_if(soa,
                                        [](const auto &row) {
                                            return std::get<0>(row) == 30; // x == 30
                                        }) -
                   soa.begin();
        assert(idx == 2);

        // 查找 y > 3.0
        idx = std::ranges::find_if(
                  soa, [](const auto &row) { return std::get<1>(row) > 3.0; }) -
              soa.begin();
        assert(idx == 3);

        // 未找到
        idx = std::ranges::find_if(
                  soa, [](const auto &row) { return std::get<0>(row) == 999; }) -
              soa.begin();
        assert(idx == soa.size());
    }
    cout << "[PASS] find with standard algorithms\n";

    // 18. get<I>/get<name> 和 field<I>/field<name> 测试
    {
        SoaVector<SimplePod> soa(3);
        soa.get<0>(0) = 42;
        soa.get<1>(0) = 3.14;
        soa.get<2>(0) = 'X';

        // 按索引 get
        assert(soa.get<0>(0) == 42);
        assert(soa.get<1>(0) == 3.14);
        assert(soa.get<2>(0) == 'X');

        // 按名称 get
        soa.get<"x">(0) = 100;
        assert(soa.get<"x">(0) == 100);
        assert(soa.get<"y">(0) == 3.14);
        assert(soa.get<"z">(0) == 'X');

        // 测试 const 版本
        const auto &csoa = soa;
        assert(csoa.get<0>(0) == 100);
        assert(csoa.get<"x">(0) == 100);

        // 获取整个字段数组 field<I>
        auto &x_field = soa.field<0>(); // soa_member_pointer<int>&
        x_field[1] = 200;
        assert(soa.get<0>(1) == 200);
        // field 支持直接下标（通过 soa_member_pointer 的 operator[]）
        soa.field<1>()[1] = 2.71;
        assert(soa.get<1>(1) == 2.71);
        soa.field<"z">()[1] = 'Y';
        assert(soa.get<2>(1) == 'Y');

        // const field 返回 const 指针
        const auto &cf = csoa.field<0>();
        static_assert(std::is_same_v<decltype(cf), const soa_member_pointer<int> &>);
        assert(cf[0] == 100);

        cout << "[PASS] get and field by index/name\n";
    }

    // 19. view() 和 view<names...> 测试
    {
        SoaVector<SimplePod> soa(3);
        // 填充
        soa.get<0>(0) = 1;
        soa.get<1>(0) = 1.0;
        soa.get<2>(0) = 'a';
        soa.get<0>(1) = 2;
        soa.get<1>(1) = 2.0;
        soa.get<2>(1) = 'b';
        soa.get<0>(2) = 3;
        soa.get<1>(2) = 3.0;
        soa.get<2>(2) = 'c';

        // 全字段视图 view()
        auto full_view = soa.view();
        int count = 0;
        for (auto &&[x, y, z] : full_view)
        {
            if (count == 0)
            {
                assert(x == 1 && y == 1.0 && z == 'a');
            }
            else if (count == 1)
            {
                assert(x == 2 && y == 2.0 && z == 'b');
            }
            else if (count == 2)
            {
                assert(x == 3 && y == 3.0 && z == 'c');
            }
            ++count;
        }
        assert(count == 3);

        // 部分字段视图 view<"x", "y">()
        auto partial_view = soa.view<"x", "y">();
        count = 0;
        for (auto &&[x, y] : partial_view)
        {
            if (count == 0)
                assert(x == 1 && y == 1.0);
            else if (count == 1)
                assert(x == 2 && y == 2.0);
            else if (count == 2)
                assert(x == 3 && y == 3.0);
            ++count;
        }

        // 修改视图中的值应反映到原容器（因为 view 返回的是引用）
        for (auto &&[x, y] : soa.view<"x", "y">())
        {
            x += 10;
        }
        assert(soa.get<0>(0) == 11);
        assert(soa.get<0>(1) == 12);
        assert(soa.get<0>(2) == 13);

        {
            const soa_member_pointer<int> ptr{nullptr};

            // 当前实现返回什么？
            auto result = ptr.data();
            static_assert(std::is_same_v<decltype(result), const int *>); // 应该通过
        }

        {
            const auto &csoa = soa;
            auto sp = csoa.span<"x">(); // 或 span<0>()
            static_assert(std::is_same_v<decltype(sp), std::span<const int>>); // 应该通过
        }
        // const 视图
        const auto &csoa = soa;
        auto cview = csoa.view<"x">();
        auto it = cview.begin();
        //NOTE: 结构化绑定 不丢失 const
        auto [x_val] = *it; // 或 使用 auto&& 绑定临时 tuple. b
        static_assert(std::is_same_v<decltype(x_val), const int &>); // 现在通过
        assert(x_val == 11);

        // 空容器 view
        SoaVector<SimplePod> empty_soa(0);
        auto ev = empty_soa.view();
        assert(ev.begin() == ev.end());

        cout << "[PASS] view() and partial view\n";
    }
    {
        // 已有临时对象测试：get<0>(0) 是 int&&
        // 补充：对已存在的对象使用 std::move
        SoaVector<SimplePod> soa(1);
        static_assert(std::is_same_v<decltype(std::move(soa).get<0>(0)), int &&>);

        static_assert(std::is_same_v<decltype(std::move(soa).field<0>()),
                                     soa_member_pointer<int> &&>);

        // 右值调用 view 仍应产生左值引用（因为底层数据不能移动）
        auto rv_view = std::move(soa).view();
        auto it = rv_view.begin();
        auto &&[x, y, z] = *it;
        static_assert(std::is_same_v<decltype(x), int &>); // 仍是 int&，不是 int&&
        static_assert(std::is_same_v<decltype(y), double &>);
        static_assert(std::is_same_v<decltype(z), char &>);

        const auto &csoa = soa;
        auto crv_view = std::move(csoa).view();
        auto crv_it = crv_view.begin();
        auto &&[cx, cy, cz] = *crv_it;
        static_assert(std::is_same_v<decltype(cx), const int &>);
        static_assert(std::is_same_v<decltype(cy), const double &>);
        static_assert(std::is_same_v<decltype(cz), const char &>);

        soa_member_pointer<int> ptr(new int(42));
        static_assert(std::is_same_v<decltype(std::move(ptr)[0]), int &&>);
        delete ptr.data(); // 清理

        {
            SoaVector<SimplePod> x(1);
            x.get<0>(0) = 10;
            x = std::move(x); // 自移动
            assert(x.size() == 1 && x.get<0>(0) == 10);
        }
    }
    // 20. shrink_to_fit 测试
    {
        // 基本缩容
        SoaVector<SimplePod> soa(4);
        soa.get<0>(0) = 1;
        soa.get<0>(1) = 2;
        soa.get<0>(2) = 3;
        soa.get<0>(3) = 4;
        soa.get<1>(0) = 0.1;
        soa.get<1>(1) = 0.2;
        soa.get<1>(2) = 0.3;
        soa.get<1>(3) = 0.4;

        soa.reserve(10); // 扩容
        soa.shrink_to_fit();
        assert(soa.capacity() == soa.size());
        assert(soa.get<0>(0) == 1);
        assert(soa.get<0>(3) == 4);
        assert(soa.get<1>(0) == 0.1);
        assert(soa.get<1>(3) == 0.4);
        std::cout << "[PASS] shrink_to_fit basic\n";
    }

    {
        // 空容器，size==0
        SoaVector<SimplePod> empty(0);
        empty.shrink_to_fit();
        assert(empty.capacity() == 0 && empty.size() == 0);
        std::cout << "[PASS] shrink_to_fit empty\n";
    }

    {
        // 容量 == size，无操作
        SoaVector<SimplePod> soa(3);
        size_t cap = soa.capacity();
        soa.shrink_to_fit();
        assert(soa.capacity() == cap);
        std::cout << "[PASS] shrink_to_fit no-op\n";
    }

    {
        // 自定义分配器跟踪释放次数 —— 必须在容器析构后检查
        TrackingAlloc<int>::alloc_count = 0;
        TrackingAlloc<int>::dealloc_count = 0;
        TrackingAlloc<double>::alloc_count = 0;
        TrackingAlloc<double>::dealloc_count = 0;
        TrackingAlloc<char>::alloc_count = 0;
        TrackingAlloc<char>::dealloc_count = 0;

        {
            SoaVector<SimplePod, detail::doubleIncrease, TrackingAlloc> soa(4);
            soa.reserve(8);
            soa.shrink_to_fit();
        } // 离开此作用域，soa 析构，释放所有内存

        // 现在所有分配都被释放，计数器应相等
        assert(TrackingAlloc<int>::alloc_count == TrackingAlloc<int>::dealloc_count);
        assert(TrackingAlloc<double>::alloc_count ==
               TrackingAlloc<double>::dealloc_count);
        assert(TrackingAlloc<char>::alloc_count == TrackingAlloc<char>::dealloc_count);
        std::cout << "[PASS] shrink_to_fit with allocator tracking\n";
    }

    {
        // 移动语义成员
        SoaVector<NonTrivial> soa(2);
        soa.get<0>(0) = 10;
        soa.get<1>(0) = "hello";
        soa.get<0>(1) = 20;
        soa.get<1>(1) = "world";

        soa.reserve(5);
        soa.shrink_to_fit();
        assert(soa.size() == 2);
        assert(soa.capacity() == 2);
        assert(soa.get<0>(0) == 10);
        assert(soa.get<1>(0) == "hello");
        assert(soa.get<0>(1) == 20);
        assert(soa.get<1>(1) == "world");
        std::cout << "[PASS] shrink_to_fit move-only type\n";
    }

    cout << "All tests passed!\n";

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
}