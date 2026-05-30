#include <cassert>
#include <iostream>
#include <exception>
#include <meta>

// NOLINTBEGIN
struct type_id_gen
{
    std::meta::info id; // int
};

constexpr type_id_gen g_infp = type_id_gen{}; //NOTE: 不能修改，无法储存运行时变量好像
// consteval auto get_nest()
// {
//     constexpr static int v = 0;
//     return v++;
// }
// static_assert(get_nest() == 0);

struct static_string
{
    const char *value{}; // NOLINT

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
struct member_info
{
    std::meta::info info{};
    static_string name;

    template <size_t N>
    consteval member_info(std::meta::info info, const char (&str)[N]) noexcept
        : info{info}, name{str}
    {
    }
};
template <member_info... infos>
struct make_aggregate
{
    struct type;
    consteval
    {
        std::vector<std::meta::info> nsdms;
        template for (const auto &spec : {infos...})
        {
            nsdms.push_back(
                std::meta::data_member_spec(spec.info, {.name = spec.name.view()}));
        }
        std::meta::define_aggregate(^^type, nsdms);
    }
};

template <member_info... infos>
using make_aggregate_t = make_aggregate<infos...>::type;

// NOLINTEND

int main()
try
{
    auto lambda = make_aggregate_t<
        {^^int, "x"}, {^^double, "y"}, {^^float, "z"},
        {^^decltype([](auto a, auto b) noexcept { return a + b; }), "add"},
        {^^decltype([](auto &self, auto change) noexcept { self.x += change; }),
         "update_x"}>{.x = 1, .y = 2, .z = 3};

    assert(lambda.add(1, 2) == 3);

    lambda.update_x(lambda, 2); //NOTE: 组合的方式？。去哪找呢？定义好当然可以找

    assert(lambda.x == 3);

    auto new_data = lambda;

    assert(new_data.x == 3);

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