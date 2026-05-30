#include <algorithm>
#include <array>
#include <assert.h>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <exception>
#include <memory>
#include <meta>
#include <print>
#include <span>
#include <type_traits>
#include <vector>

#include <ranges>

//---------------------------------
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

consteval auto can_soa(std::meta::info type)
{
    return std::meta::is_aggregate_type(type) and
           std::meta::bases_of(type, std::meta::access_context::current()).empty();
}
template <typename T>
concept soa_type = can_soa(^^T);

//--------------------------------

using namespace std::meta;

// NOLINTBEGIN
template <name_spec... registration>
struct class_vec;

template <typename T> // 主模板
struct gen_world;

template <name_spec... specs> // 特化
struct gen_world<class_vec<specs...>>
{
    using index_type = uint32_t;

    static consteval bool unique_class_name()
    {
        auto args = std::meta::template_arguments_of(^^class_vec<specs...>);
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            auto spec_i = std::meta::extract<name_spec>(args[i]);
            for (std::size_t j = i + 1; j < args.size(); ++j)
            {
                auto spec_j = std::meta::extract<name_spec>(args[j]);
                if (spec_i.name == spec_j.name)
                    return false; // 发现重复
            }
        }
        return true;
    }
    static consteval bool class_vec_info_is_class()
    {
        return std::ranges::all_of(
            std::meta::template_arguments_of(^^class_vec<specs...>),
            [](std::meta::info v) {
                return std::meta::is_class_type(std::meta::extract<name_spec>(v).info);
            });
    }
    static_assert(unique_class_name(), "requires class'name unique.");
    static_assert(class_vec_info_is_class(), "requires class'info is class type.");

    struct world
    {
        template <static_string name>
        static consteval index_type get_index()
        {
            auto agrs = std::meta::template_arguments_of(^^class_vec<specs...>);
            for (index_type i = 0; i < agrs.size(); ++i)
            {
                auto spec = std::meta::extract<name_spec>(agrs[i]);
                if (name == spec.name)
                    return i;
            }
            return ~0;
        }
        template <static_string name>
            requires(get_index<name>() != ~0)
        static consteval std::meta::info get_class_info()
        {
            auto agrs = std::meta::template_arguments_of(^^class_vec<specs...>);
            return std::meta::extract<name_spec>(agrs[get_index<name>()]).info;
        }

        template <static_string name>
            requires(get_index<name>() != ~0)
        static consteval index_type get_class_id()
        {
            return get_index<name>();
        }

        template <static_string name>
            requires(get_index<name>() != ~0)
        using get_class_type = decltype([]() {
            constexpr auto info = get_class_info<name>();
            return std::type_identity<typename[:info:]>{};
        }())::type;
    };

    static consteval auto nsdms_of(std::meta::info info)
    {
        return std::define_static_array(std::meta::nonstatic_data_members_of(
            info, std::meta::access_context::current()));
    }

    template <soa_type T>
    class SoaVector
    {
        static consteval auto transformed(auto f)
        {
            return std::meta::nonstatic_data_members_of(
                       ^^T, std::meta::access_context::current()) //
                   | std::views::transform([=](auto m) -> std::meta::info {
                         return std::meta::data_member_spec(
                             f(std::meta::type_of(m)),
                             {.name = std::meta::identifier_of(m)});
                     });
        }

        struct Pointers;
        struct Spans;
        struct ConstSpans;
        consteval
        {
            std::meta::define_aggregate(^^Pointers, transformed(std::meta::add_pointer));
            // 非 const span：span<T>
            std::meta::define_aggregate(^^Spans, transformed([](std::meta::info info) {
                return std::meta::substitute(^^std::span, {
                                                              info});
            }));
            // const span：span<const T>
            std::meta::define_aggregate(
                ^^ConstSpans, transformed([](std::meta::info info) {
                    return std::meta::substitute(^^std::span,
                                                 {
                                                     std::meta::add_const(info)});
                }));
        }
        Pointers pointers_{};
        size_t size_{};
        size_t capactity_;

        static constexpr auto ptr_members = nsdms_of(^^Pointers);
        static constexpr auto members = nsdms_of(^^T);

      public:
        SoaVector(size_t init_capacity = 8) : capactity_{init_capacity}
        {
            // 立即分配对应的数组
            grow(init_capacity); // 或者为每个成员分配 init_capacity 个元素
        }
        ~SoaVector() noexcept
        {
            // 1. 销毁已构造元素
            template for (constexpr auto I : std::ranges::views::indices(members.size()))
            {
                auto p = pointers_.[:ptr_members[I]:];
                std::destroy(p, p + size_);
            }
            // 2. 释放内存
            template for (auto *&p : pointers_)
            {
                using PT = std::remove_pointer_t<std::decay_t<decltype(p)>>;
                std::allocator<PT>{}.deallocate(p, capactity_);
                p = nullptr;
            }
            size_ = capactity_ = 0;
        }
        SoaVector(const SoaVector &other)
            : size_{other.size_}, capactity_{other.capactity_}
        {
            // 为每个字段分配 capactity_ 的内存
            template for (auto *&p : pointers_)
            {
                using PT = std::remove_pointer_t<std::decay_t<decltype(p)>>;
                p = std::allocator<PT>{}.allocate(capactity_);
            }
            // 拷贝所有已构造元素
            template for (constexpr auto I : std::ranges::views::indices(members.size()))
            {
                std::uninitialized_copy_n(
                    other.pointers_.[:ptr_members[I]:], size_, pointers_.[:ptr_members[I
                ]:]);
            }
        }

        SoaVector(SoaVector &&other) noexcept
            : pointers_{other.pointers_}, size_{other.size_}, capactity_{other.capactity_}
        {
            // 置空源对象
            template for (auto *&p : other.pointers_) p = nullptr;
            other.size_ = other.capactity_ = 0;
        }

        SoaVector &operator=(const SoaVector &other)
        {
            if (this != &other)
            {
                // 释放现有资源
                this->~SoaVector();
                // 拷贝构造
                new (this) SoaVector(other);
            }
            return *this;
        }

        SoaVector &operator=(SoaVector &&other) noexcept
        {
            if (this != &other)
            {
                this->~SoaVector();
                pointers_ = other.pointers_;
                size_ = other.size_;
                capactity_ = other.capactity_;
                template for (auto *&p : other.pointers_) p = nullptr;
                other.size_ = other.capactity_ = 0;
            }
            return *this;
        }

        template <typename U>
        auto grow(U *src, size_t new_capacity)
        {
            auto alloc = std::allocator<U>();
            U *dst = alloc.allocate(new_capacity);
            std::uninitialized_move_n(src, size_, dst);
            std::destroy(src, src + size_);
            alloc.deallocate(src, capactity_);
            return dst;
        }

        auto grow(size_t new_capacity)
        {
            template for (auto *&p : pointers_)
            {
                p = grow(p, new_capacity);
            }
            capactity_ = new_capacity;
        }

        auto new_capacity() const noexcept
        {
            return capactity_ * 2;
        }

        auto push_back(const T &value)
        {
            if (size_ == capactity_)
            {
                grow(new_capacity());
            }

            template for (constexpr auto I : std::ranges::views::indices(members.size()))
            {
                std::construct_at(
                    pointers_.[:ptr_members[I]:] + size_, value.[:members[I]:]);
            }

            ++size_;
        }

        // 非 const：返回可修改的 span
        auto spans() noexcept
        {
            auto &[... ptrs] = pointers_;
            return Spans{std::span{ptrs, size_}...};
        }
        // const：返回只读的 span
        auto spans() const noexcept
        {
            auto &[... ptrs] = pointers_;
            return ConstSpans{std::span{ptrs, size_}...};
        }
        auto operator[](size_t idx) const
        {
            auto &[... ptrs] = pointers_;
            return T{ptrs[idx]...};
        }
    };
};

struct A
{
    int x;
};
struct B
{
    double z;
};
static_assert((bool)soa_type<A>);
static_assert((bool)soa_type<B>);

using gen_world_type =
    gen_world<class_vec<{.name = "A", .info = ^^A}, /*{.name = "A", .info = ^^A},*/
                        {.name = "B", .info = ^^B},
                        {.name = "add",
                         .info = ^^decltype([](int v) constexpr { return v + 1; })}>>;
using world = gen_world_type::world;
// using gen_world_type::SoaVector;
static_assert(world::get_class_info<"A">() != std::meta::info());
static_assert(world::get_class_id<"A">() == 0);
static_assert(std::is_same_v<A, world::get_class_type<"A">>);
static_assert(std::is_same_v<B, world::get_class_type<"B">>);

static_assert(world::get_class_type<"add">{}(1) == 2);
static_assert(world::get_class_type<"A">{.x = 1}.x == 1);

struct Squeare
{
    char x;
    long y;
};

// NOLINTEND

int main()
try
{
    // ---------- 测试用聚合体 ----------
    struct Point
    {
        int x;
        int y;
    };
    static_assert(soa_type<Point>);

    // --- 测试 1：基本构造与 push_back ---
    {
        gen_world_type::SoaVector<Point> v;
        v.push_back({1, 2});
        v.push_back({3, 4});
        auto s = v.spans();
        assert(s.x[0] == 1 && s.y[0] == 2);
        assert(s.x[1] == 3 && s.y[1] == 4);
        assert(v[0].x == 1 && v[0].y == 2);
        assert(v[1].x == 3 && v[1].y == 4);
        std::println("Test 1 (construction/push) passed.");
    }

    // --- 测试 2：拷贝构造（深拷贝）---
    {
        gen_world_type::SoaVector<Point> v1(2);
        v1.push_back({10, 20});
        v1.push_back({30, 40});

        auto v2 = v1;
        assert(v2[0].x == 10 && v2[1].x == 30);
        auto s2 = v2.spans();
        s2.x[0] = 999;         // 非 const 版本允许修改
        assert(v1[0].x == 10); // v1 不变
        v2.push_back({50, 60});
        assert(v1.spans().x.size() == 2);
        assert(v2.spans().x.size() == 3);
        std::println("Test 2 (copy construction) passed.");
    }

    // --- 测试 3：移动构造 ---
    {
        gen_world_type::SoaVector<Point> v1;
        v1.push_back({1, 1});
        v1.push_back({2, 2});
        const int *original_x_ptr = v1.spans().x.data();

        auto v2 = std::move(v1);
        assert(v2[0].x == 1 && v2[1].x == 2);
        assert(v1.spans().x.size() == 0);
        assert(v2.spans().x.data() == original_x_ptr);
        std::println("Test 3 (move construction) passed.");
    }

    // --- 测试 4：拷贝赋值 ---
    {
        gen_world_type::SoaVector<Point> v1, v2;
        v1.push_back({5, 6});
        v2.push_back({100, 200});
        v2.push_back({300, 400});

        v1 = v2;
        assert(v1[0].x == 100 && v1[1].x == 300);
        v1.push_back({500, 600});
        assert(v2.spans().x.size() == 2);
        auto s2 = v2.spans();
        s2.x[0] = -1;
        assert(v1[0].x == 100);
        std::println("Test 4 (copy assignment) passed.");
    }

    // --- 测试 5：移动赋值 ---
    {
        gen_world_type::SoaVector<Point> v1, v2;
        v1.push_back({7, 8});
        v2.push_back({70, 80});
        v2.push_back({90, 100});

        const int *v2_y_ptr = v2.spans().y.data(); // 修正为 const int*

        v1 = std::move(v2);
        assert(v1[0].y == 80 && v1[1].y == 100);
        assert(v2.spans().y.size() == 0);
        assert(v1.spans().y.data() == v2_y_ptr);
        std::println("Test 5 (move assignment) passed.");
    }

    // --- 测试 6：自赋值 ---
    {
        gen_world_type::SoaVector<Point> v;
        v.push_back({1, 2});
        const auto &ref = v;
        v = ref; // 拷贝自赋值
        assert(v[0].x == 1);
        v = std::move(v); // 移动自赋值（有 warning 但安全）
        assert(v[0].x == 1);
        std::println("Test 6 (self-assignment) passed.");
    }

    // --- 测试 7：const 对象访问 ---
    {
        const gen_world_type::SoaVector<Point> v = [] {
            gen_world_type::SoaVector<Point> tmp;
            tmp.push_back({9, 10});
            return tmp;
        }();
        auto sp = v.spans(); // 现在调用 const 重载，返回 ConstSpans
        assert(sp.x[0] == 9);
        // sp.x[0] = 0;               // 编译错误：不能修改 const span
        Point val = v[0];
        assert(val.x == 9);
        std::println("Test 7 (const access) passed.");
    }

    // --- 测试 8：不同类型 Squeare ---
    {
        gen_world_type::SoaVector<Squeare> v;
        v.push_back({'a', 100L});
        v.push_back({'b', 200L});
        auto sp = v.spans();
        assert(sp.x[0] == 'a');
        assert(sp.y[1] == 200L);
        std::println("Test 8 (Squeare) passed.");
    }

    std::cout << "All special function tests passed.\n";
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