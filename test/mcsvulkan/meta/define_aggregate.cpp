#include <array>
#include <vector>
#include <ranges>
#include <string_view>
#include <meta>
#include <type_traits>

struct static_string
{
    const char *value{}; // NOLINT

    static_string() = default;
    consteval explicit static_string(std::string_view view)
        : value{std::define_static_string(view)}
    {
    }
    consteval explicit static_string(const char *value) noexcept
        : static_string{std::string_view{value}}
    {
    }
    template <size_t N>
    consteval static_string(const char (&str)[N]) noexcept // NOLINT
        : static_string{std::string_view{str, N - 1}}
    {
    }
    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return std::string_view{value};
    }
    constexpr bool operator==(const static_string &o) const noexcept
    {
        if (value == o.value) // 指针相等快速路径
            return true;
        return view() == o.view();
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

    friend std::ostream &operator<<(std::ostream &os, const static_string &s)
    {
        return os << s.view();
    }
};

// 1. trait：识别 std::array<std::meta::info, N>
template <typename T>
struct is_info_array : std::false_type
{
};
template <std::size_t N>
struct is_info_array<std::array<std::meta::info, N>> : std::true_type
{
};
// 2. 概念
template <typename T>
concept info_array = is_info_array<std::remove_cvref_t<T>>::value;

// 3. 把概念约束放在模板参数上

consteval bool has_unique_member_names(std::span<const std::meta::info> specs)
{
    std::vector<std::string_view> seen;
    for (auto s : specs)
    {
        if (!std::meta::is_data_member_spec(s))
            return false;
        if (!std::meta::has_identifier(s))
            return false;
        std::string_view name = std::meta::identifier_of(s);
        for (auto prev : seen)
            if (prev == name)
                return false;
        seen.push_back(name);
    }
    return true;
}
template <info_array... V>
consteval auto compose_member_names(V... v)
{
    constexpr std::size_t N = (std::tuple_size_v<V> + ...);
    std::array<std::meta::info, N> data{};
    std::size_t idx = 0;
    auto append = [&](const auto &arr) {
        for (auto e : arr)
            data[idx++] = e;
    };
    (append(v), ...);

    if (not has_unique_member_names(data))
        throw std::meta::exception(u8"compose(): duplicate data member names",
                                   ^^compose_member_names);
    return data;
}

// 4. 工厂
template <auto member_specs>
struct define_aggregate_t
{
    struct type;
    consteval
    {
        std::meta::define_aggregate(^^type, member_specs);
    }
};

template <auto member_specs>
using define_aggregate = typename define_aggregate_t<member_specs>::type;

// 非模板 struct：任意 N 的零件都装得下
struct compose_part
{
    std::meta::info packed; // reflect_constant_array 的结果

    consteval compose_part(std::meta::info p) : packed(p) {}

    template <std::size_t N>
    consteval compose_part(std::array<std::meta::info, N> p)
        : compose_part(std::meta::reflect_constant_array(p))
    {
    }

    consteval std::size_t size() const
    {
        return std::meta::extent(std::meta::type_of(packed), 0);
    }
    consteval const std::meta::info *data() const
    {
        return std::meta::extract<const std::meta::info *>(packed);
    }
    friend consteval bool operator==(const compose_part &a, const compose_part &b)
    {
        return a.packed == b.packed;
    }
};

struct compose_member_specs
{
    static_string name;
    compose_part infos;
};

// 从 compose_part 解成 std::array<info, N>
template <compose_member_specs M>
consteval auto unpack_to_array()
{
    constexpr auto n = std::meta::extent(std::meta::type_of(M.infos.packed), 0);
    std::array<std::meta::info, n> arr{};
    auto ptr = std::meta::extract<const std::meta::info *>(M.infos.packed);
    for (std::size_t i = 0; i < n; ++i)
        arr[i] = ptr[i];
    return arr;
}

// 每个 M 对应一个独立的嵌套类型槽
template <compose_member_specs M>
struct nested_slot
{
    struct type;
    consteval
    {
        std::meta::define_aggregate(^^type, unpack_to_array<M>());
    }
};

template <compose_member_specs... member_specs>
struct define_compose_t
{
    struct type;
    consteval
    {
        constexpr auto N = sizeof...(member_specs);
        std::vector<std::meta::info> desc;
        template for (constexpr auto I : std::ranges::views::indices(N))
        {
            auto const &[name, infos] = member_specs...[I];
            constexpr auto nested_ty = ^^typename nested_slot<member_specs...[I]>::type;
            auto is_empty = std::meta::is_empty_type(nested_ty);
            desc.push_back(std::meta::data_member_spec(
                nested_ty, {.name = name.view(), .no_unique_address = is_empty}));
        }

        if (not has_unique_member_names(desc))
            throw std::meta::exception(u8"duplicate data member names",
                                       ^^define_compose_t);
        std::meta::define_aggregate(^^type, desc);
    }
};

template <compose_member_specs... member_specs>
using define_compose = typename define_compose_t<member_specs...>::type;

consteval auto part_xy()
{
    return std::array<std::meta::info, 2>{
        std::meta::data_member_spec(^^int,
                                    {
                                        .name = "x"}),
        std::meta::data_member_spec(^^double,
                                    {
                                        .name = "y"}),
    };
}
consteval auto part_z()
{
    return std::array<std::meta::info, 1>{
        std::meta::data_member_spec(^^long, {
                                                .name = "z"})};
}
consteval auto part_str()
{
    return std::array<std::meta::info, 1>{
        std::meta::data_member_spec(^^const char *, {
                                                        .name = "s"})};
}

static_assert(compose_member_names(part_xy(), part_z(), part_str()).size() == 4);

template <auto v>
struct test_auto
{
};

// 4. 直接传 compose 的结果，不要套 define_static_array
using A = test_auto<compose_member_names(part_xy(), part_z(), part_str())>;

// ============ 工厂：完全照搬你 gen_aggregate 的写法 ============

// ============ 用法 1：任意组合 ============
using T1 = define_aggregate<compose_member_names(part_xy(), part_z())>;
using T2 = define_aggregate<compose_member_names(part_z(), part_str())>;
using T3 = define_aggregate<compose_member_names(part_str(), part_xy(), part_z())>;

constexpr T1 a{1, 2.0, 3L};
constexpr T2 b{4L, "hi"};
constexpr T3 c{"yo", 5, 6.0, 7L};
static_assert(a.x == 1 && a.y == 2.0 && a.z == 3L);
static_assert(b.z == 4L && std::is_same_v<decltype(b.s), const char *>);
static_assert(c.z == 7L && c.x == 5 && c.y == 6.0);

constexpr compose_member_specs spec1{"xy",
                                     compose_part{part_xy()}}; // name = "xy", 内嵌 {x, y}
constexpr compose_member_specs spec2{"z",
                                     compose_part{part_z()}}; // name = "z",  内嵌 {z}

using T = define_compose<spec1, {"z", compose_part{part_z()}}>;
// T == { struct{int x; double y;} xy; struct{long z;} z; }

constexpr T t{{1, 2.0}, {3L}};
static_assert(t.xy.x == 1 && t.xy.y == 2.0 && t.z.z == 3L);

int main()
{

    return 0;
}