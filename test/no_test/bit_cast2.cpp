#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <exception>

#include <limits>
#include <ranges>
#include <span>
#include <string>
#include <vector>
#include <meta>

#include <utility>

#include <string_view>

// NOLINTBEGIN
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
enum static_function_state
{
    step0,
    step1,
};

template <typename E>
    requires std::is_enum_v<E>
consteval auto enum_array_info()
{
    return std::define_static_array(std::meta::enumerators_of(^^E));
}

template <typename E>
    requires std::is_enum_v<E>
consteval auto enum_values()
{
    constexpr auto metas = enum_array_info<E>();
    constexpr auto N = metas.size();
    std::array<E, N> ret;
    template for (constexpr auto I : std::ranges::views::indices(N))
    {
        ret[I] = std::meta::extract<E>(metas[I]);
    }
    return ret;
}
template <typename E>
    requires std::is_enum_v<E>
consteval auto enum_underlying_values()
{
    constexpr auto metas = enum_array_info<E>();
    constexpr auto N = metas.size();
    using value_type = std::underlying_type_t<E>;
    std::array<value_type, N> ret;
    template for (constexpr auto I : std::ranges::views::indices(N))
    {
        ret[I] = std::to_underlying(std::meta::extract<E>(metas[I]));
    }
    return ret;
}
template <typename E>
    requires std::is_enum_v<E>
consteval auto enum_strings()
{
    constexpr auto metas = enum_array_info<E>();
    constexpr auto N = metas.size();
    std::array<static_string, N> ret;
    template for (constexpr auto I : std::ranges::views::indices(N))
    {
        ret[I] =
            static_string{std::define_static_string(std::meta::identifier_of(metas[I]))};
    }
    return ret;
}
template <typename E>
    requires std::is_enum_v<E>
consteval auto enum_pairs()
{
    using pair_type = std::pair<E, static_string>;
    constexpr auto metas = enum_array_info<E>();
    constexpr auto N = metas.size();
    std::array<pair_type, N> ret;
    template for (constexpr auto I : std::ranges::views::indices(N))
    {
        ret[I] = pair_type{std::meta::extract<E>(metas[I]),
                           std::define_static_string(std::meta::identifier_of(metas[I]))};
    }
    return ret;
}
template <typename E>
    requires std::is_enum_v<E>
consteval auto enum_underlying_pairs()
{
    using pair_type = std::pair<std::underlying_type_t<E>, static_string>;
    constexpr auto metas = enum_array_info<E>();
    constexpr auto N = metas.size();
    std::array<pair_type, N> ret;
    template for (constexpr auto I : std::ranges::views::indices(N))
    {
        ret[I] = pair_type{std::to_underlying(std::meta::extract<E>(metas[I])),
                           std::define_static_string(std::meta::identifier_of(metas[I]))};
    }
    return ret;
}

template <typename E>
    requires std::is_enum_v<E>
consteval auto get_enum_max_value()
{
    using underlying = std::underlying_type_t<E>;
    underlying max = (std::numeric_limits<underlying>::min)();
    template for (constexpr auto e : enum_array_info<E>())
    {
        if (auto v = std::to_underlying(std::meta::extract<E>(e)); v > max)
            max = v;
    }
    return max;
}
template <typename E>
    requires std::is_enum_v<E>
consteval auto get_enum_min_value()
{
    using underlying = std::underlying_type_t<E>;
    underlying min = (std::numeric_limits<underlying>::max)();
    template for (constexpr auto e : enum_array_info<E>())
    {
        if (auto v = std::to_underlying(std::meta::extract<E>(e)); v < min)
            min = v;
    }
    return min;
}

template <typename E>
    requires(get_enum_min_value<E>() >= 0 && get_enum_max_value<E>() < 64)
static constexpr bool contains_enum(E e) noexcept
{
    constexpr auto min_val = get_enum_min_value<E>();
    constexpr uint64_t mask = [&]() constexpr {
        uint64_t m = 0;
        template for (constexpr auto e : enum_array_info<E>())
        {
            auto v = std::to_underlying(std::meta::extract<E>(e));
            m |= (uint64_t(1) << (v - min_val));
        }
        return m;
    }();
    auto val = std::to_underlying(e);
    return (mask & (uint64_t(1) << (val - min_val))) != 0;
}
static_assert(get_enum_max_value<static_function_state>() == 1);
static_assert(get_enum_min_value<static_function_state>() == 0);
static_assert(contains_enum(static_function_state::step0));
static_assert(contains_enum(static_function_state::step1));

struct runtime_object
{
    using member_store = std::array<std::byte, 16>; // NOLINT
    std::vector<member_store> data;
    std::vector<std::function_ref<void(runtime_object *slef, void *bind_data) noexcept>>
        functions;
    std::function_ref<bool(static_function_state fix_fn) noexcept> functuon_infos;
};

struct test_type
{
    int age;
    std::string name;

    static void step0(int new_age, int new_data) {}
};

struct member_ptr
{
    void *soa;
    uint32_t idx;
};
auto input_member(auto obj) noexcept
{
    return std::bit_cast<std::array<std::byte, 16>>(obj);
}

template <auto... Candidates>
constexpr uint64_t enum_mask = []() constexpr {
    using E = std::common_type_t<decltype(Candidates)...>;
    static_assert(std::is_enum_v<E>);
    constexpr auto min_val = get_enum_min_value<E>();
    uint64_t m = 0;
    ((m |= (uint64_t(1) << (std::to_underlying(Candidates) - min_val))), ...);
    return m;
}();

template <static_function_state... s>
constexpr auto contain_fix_state =
    std::constant_wrapper<[](static_function_state fix_fn) noexcept -> bool {
        constexpr auto min_val = get_enum_min_value<static_function_state>();
        constexpr auto max_val = get_enum_max_value<static_function_state>();
        if constexpr (min_val >= 0 && max_val < std::numeric_limits<uint64_t>::digits)
        {
            constexpr auto mask = enum_mask<s...>();
            auto val = std::to_underlying(fix_fn);
            return (mask & (uint64_t(1) << (val - min_val))) != 0;
        }
    }>{};

void test_0()
{
    // 1. 测试 static_string 比较
    constexpr static_string s = "step0";
    static_assert(s == "step0");
    static_assert(s != "step1");

    // 2. 获取枚举值数组
    constexpr auto values = enum_values<static_function_state>();
    std::cout << "Enum values:\n";
    for (auto v : values)
    {
        std::cout << static_cast<int>(std::to_underlying(v)) << '\n';
    }

    // 3. 获取底层整型数组
    constexpr auto under_vals = enum_underlying_values<static_function_state>();
    std::cout << "Underlying values:\n";
    for (auto uv : under_vals)
    {
        std::cout << uv << '\n';
    }

    // 4. 获取字符串数组
    static constexpr auto strings = enum_strings<static_function_state>();
    std::cout << "Enum strings:\n";
    template for (constexpr auto str : strings)
    {
        std::cout << str.view() << '\n';
    }

    // 5. 遍历 (枚举值, 字符串) 对
    static constexpr auto pairs = enum_pairs<static_function_state>();
    std::cout << "Value-string pairs:\n";
    template for (constexpr auto p : pairs)
    {
        auto [e, name] = p;
        std::cout << static_cast<int>(std::to_underlying(e)) << " -> " << name.view()
                  << '\n';
    }

    // 6. 遍历 (底层值, 字符串) 对
    static constexpr auto u_pairs = enum_underlying_pairs<static_function_state>();
    std::cout << "Underlying-string pairs:\n";
    template for (constexpr auto pair : u_pairs)
    {
        auto uv = pair.first;
        auto name = pair.second;
        std::cout << uv << " -> " << name.view() << '\n';
    }
}
int main()
try
{
    test_0();
    std::vector<test_type> soa_{{test_type{20, "name"}, test_type{19, "my_name"}}};

    // runtime_object ref
    // {
    //     .data = {member_ptr{&soa_, 0}},
    //     .functions =
    //         {{std::constant_wrapper<[](runtime_object *slef, void *bind_data) noexcept {

    //         }>{}}},
    //     .functuon_infos = {
    //         std::constant_wrapper<[](static_function_state fix_fn) noexcept -> bool {
    //             return step0 == fix_fn;
    //         }>{}}};

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