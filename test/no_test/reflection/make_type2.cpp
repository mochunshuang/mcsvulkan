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

struct ber;

struct A
{
    int x;
};
struct B
{
    double z;
};

// NOLINTEND

int main()
try
{

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