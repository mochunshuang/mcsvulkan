#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <exception>

#include <meta>
#include <print>
#include <ranges>
#include <tuple>
#include <vector>
#include <string_view>

// NOLINTBEGIN

struct static_string
{
    const char *value{};

    template <size_t N>
    constexpr static_string(const char (&str)[N]) noexcept // NOLINT
        : value{std::define_static_string(str)}
    {
    }
    constexpr bool operator==(const std::string_view &o) const noexcept
    {
        return std::string_view{value} == o;
    }
    constexpr operator std::string_view() const noexcept
    {
        return std::string_view{value};
    }
};

struct member_info
{
    std::meta::info info;
    static_string name;

    template <size_t N>
    consteval member_info(std::meta::info info, const char (&str)[N])
        : info{info}, name{str}
    {
    }
};
// NOLINTEND

template <member_info... infos>
struct make_aggregate
{
    struct aggregate_type;
    consteval
    {
        std::vector<std::meta::info> nsdms;
        template for (const auto &spec : {infos...})
        {
            nsdms.push_back(std::meta::data_member_spec(spec.info, {.name = spec.name}));
        }
        std::meta::define_aggregate(^^aggregate_type, nsdms);
    }
};
template <member_info... infos>
using make_aggregate_t = make_aggregate<infos...>::aggregate_type;

template <size_t N>
struct fixed_string
{
    char data[N]{};
    constexpr fixed_string(const char (&str)[N])
    {
        std::copy_n(str, N, data);
    }
    constexpr bool operator==(const std::string_view &o) const noexcept
    {
        return std::string_view{data, N - 1} == o;
    }
    constexpr operator std::string_view() const noexcept
    {
        return {data, N - 1};
    }
};
template <size_t N>
struct member_info2
{
    std::meta::info info;
    fixed_string<N> name;
    consteval member_info2(std::meta::info info, const char (&str)[N])
        : info{info}, name{str}
    {
    }
};
template <member_info2... infos>
struct make_aggregate2
{
    struct aggregate_type;
    consteval
    {
        std::vector<std::meta::info> nsdms;
        template for (const auto &spec : {infos...})
        {
            nsdms.push_back(std::meta::data_member_spec(spec.info, {.name = spec.name}));
        }
        std::meta::define_aggregate(^^aggregate_type, nsdms);
    }
};
template <member_info2... infos>
using make_aggregate_t2 = make_aggregate2<infos...>::aggregate_type;

template <typename... T>
consteval bool unique_type_set()
{
    constexpr std::size_t N = sizeof...(T);
    if constexpr (N <= 1)
        return true;
    std::array<std::meta::info, N> infos = {std::meta::dealias(^^T)...};
    for (std::size_t i = 0; i < N - 1; ++i)
        for (std::size_t j = i + 1; j < N; ++j)
            if (infos[i] == infos[j])
                return false;
    return true;
}

template <std::meta::info R>
consteval auto type_from_info()
{
    return std::type_identity<typename[:R:]>{};
}

template <std::meta::info R>
using type_of_info = typename decltype(type_from_info<R>())::type;

template <std::meta::info... infos>
struct type_registry_info
{
    template <size_t I>
    static consteval std::meta::info get() // NOLINT
    {
        return infos...[I];
    }
    template <size_t I>
    static consteval std::string_view get_name_nodealias() // NOLINT
    {
        return std::meta::display_string_of(infos...[I]);
    }

    template <size_t I>
    static consteval std::string_view get_name() // NOLINT
    {
        return std::meta::display_string_of(std::meta::dealias(infos...[I]));
    }
    static consteval size_t get_index_by_name(static_string name)
    {
        template for (constexpr auto I : std::define_static_array(
                          std::ranges::views::iota(std::size_t{0}, sizeof...(infos))))
        {
            if (name == get_name<I>())
                return I;
        }
        return ~0;
    }

    template <static_string name>
        requires(get_index_by_name(name) != ~0)
    using type = type_of_info<infos...[get_index_by_name(name)]>;
};

struct A
{
    int x;
};
struct B
{
    double y;
};

void test_type_of_info()
{
    struct inner_type
    {
    };

    using D = double;

    using MyRegistry = type_registry_info<^^A, ^^B, ^^int, ^^char, ^^inner_type, ^^D>;

    // 测试 get<I>
    static_assert(MyRegistry::get<0>() == ^^A);
    static_assert(MyRegistry::get<1>() == ^^B);
    static_assert(MyRegistry::get<2>() == ^^int);
    static_assert(MyRegistry::get<3>() == ^^char);
    static_assert(MyRegistry::get<4>() == ^^inner_type);
    static_assert(MyRegistry::get<5>() == ^^D);

    std::println("MyRegistry::get_name<0>(): {}", MyRegistry::get_name<0>());
    std::println("MyRegistry::get_name<4>(): {}", MyRegistry::get_name<4>());

    std::println("MyRegistry::get_name<5>(): {}", MyRegistry::get_name<5>());
    std::println("MyRegistry::get_name_nodealias<5>(): {}",
                 MyRegistry::get_name_nodealias<5>());

    //NOTE: display_string_of 默认保留别名，保留 using 名字
    std::println("std::meta::display_string_of(^^type_of_info<^^inner_type>): {}",
                 std::meta::display_string_of(^^type_of_info<^^inner_type>));

    // // 测试 get_name<I>
    static_assert(MyRegistry::get_name<0>() == "A");
    static_assert(MyRegistry::get_name<1>() == "B");
    static_assert(MyRegistry::get_name<2>() == "int");
    static_assert(MyRegistry::get_name<3>() == "char");
    static_assert(MyRegistry::get_name<4>() == "test_type_of_info()::inner_type");
    //NOTE: 小心别名
    static_assert(MyRegistry::get_name<5>() != "test_type_of_info()::D");
    std::println("std::meta::display_string_of(^^test_type_of_info): {}",
                 std::meta::display_string_of(^^test_type_of_info));
    std::println("MyRegistry::get_name<5>(): {}", MyRegistry::get_name<5>());

    // // 测试 get_index_by_name
    static_assert(MyRegistry::get_index_by_name("A") == 0);
    static_assert(MyRegistry::get_index_by_name("B") == 1);
    static_assert(MyRegistry::get_index_by_name("int") == 2);
    static_assert(MyRegistry::get_index_by_name("char") == 3);
    static_assert(MyRegistry::get_index_by_name("nonexistent") == ~size_t{0});

    // 测试 type<name> 别名
    using T0 = MyRegistry::type<static_string("A")>;
    using T1 = MyRegistry::type<static_string("B")>;
    using T2 = MyRegistry::type<static_string("int")>;
    using T3 = MyRegistry::type<static_string("char")>;

    static_assert(std::is_same_v<T0, A>);
    static_assert(std::is_same_v<T1, B>);
    static_assert(std::is_same_v<T2, int>);
    static_assert(std::is_same_v<T3, char>);
}

template <typename... T>
    requires(sizeof...(T) > 0 && unique_type_set<T...>())
struct type_registry
{
    using type_tuple = std::tuple<T...>;
    using registry_info = type_registry_info<^^T...>;
};

struct world // NOLINT
{
};

int main()
try
{
    test_type_of_info();
    using T = make_aggregate_t<{^^int, "x"}, {^^double, "y"}, {^^float, "z"}>;
    T a{.x = 1, .y = 2, .z = 3};
    {
        using T = make_aggregate_t2<{^^int, "x"}, {^^double, "y"}, {^^float, "z"}>;
        T a{.x = 1, .y = 2, .z = 3};
    }
    {
        static_string str{"123"};
    }
    {
        fixed_string str{"223"};
        fixed_string str2 = str;
        assert(str2 == "223");
    }
    {
        //NOTE: 地址是相同的
        std::println("hellp ptr: {}", (void *)("hello"));
        std::println("hellp ptr: {}", (void *)("hello"));

        //NOTE: 下面相同
        std::println("hellp ptr: {}", (void *)(std::define_static_string("hello")));
        std::println("hellp ptr: {}", (void *)(std::define_static_string("hello")));
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
}