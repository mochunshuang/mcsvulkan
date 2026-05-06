#include <exception>
#include <iostream>

#include <meta>

using namespace std;
using namespace std::meta;

template <size_t N>
struct fixed_string
{
    char data[N]{};
    constexpr fixed_string(const char (&s)[N])
    {
        std::copy_n(s, N, data);
    }
    constexpr operator std::string_view() const
    {
        return {data, N - 1};
    }
};
template <fixed_string Pre, fixed_string Suf, size_t I>
consteval std::string_view make_name()
{
    static constexpr auto arr = [] {
        constexpr std::string_view pre = Pre;
        constexpr std::string_view suf = Suf;
        constexpr size_t digits = [] {
            size_t n = I, d = 1;
            while (n >= 10)
            {
                n /= 10;
                ++d;
            }
            return d;
        }();
        constexpr size_t total = pre.size() + digits + suf.size();
        std::array<char, total + 1> result{};
        size_t pos = 0;
        for (char c : pre)
            result[pos++] = c;
        char buf[20];
        size_t len = 0;
        size_t tmp = I;
        do
        {
            buf[len++] = '0' + (tmp % 10);
            tmp /= 10;
        } while (tmp);
        while (len--)
            result[pos++] = buf[len];
        for (char c : suf)
            result[pos++] = c;
        result[pos] = '\0';
        return result;
    }();
    return {arr.data(), arr.size() - 1};
}
template <size_t I>
consteval std::string_view get_member_name()
{
    return make_name<"__", "_", I>();
}

// NOLINTBEGIN
template <typename... Ts>
struct Tuple
{
    struct storage;
    consteval
    {
        constexpr std::size_t N = sizeof...(Ts);
        [&]<std::size_t... Is>(std::index_sequence<Is...>) { // clang-format off
            std::array<meta::info, N> specs = {
                data_member_spec(^^Ts, {.name = get_member_name<Is>()})...};
            define_aggregate(^^storage, specs);
        }(std::make_index_sequence<N>{}); // clang-format on
    }
    storage data;

    Tuple() : data{} {}
    Tuple(Ts const &...vs) : data{vs...} {}
};

template <typename... Ts>
struct std::tuple_size<Tuple<Ts...>> : public integral_constant<size_t, sizeof...(Ts)>
{
};

template <std::size_t I, typename... Ts>
struct std::tuple_element<I, Tuple<Ts...>>
{
    static constexpr std::array types = {^^Ts...};
    using type = [:types[I]:];
};

consteval std::meta::info get_nth_nsdm(std::meta::info r, std::size_t n)
{
    return nonstatic_data_members_of(r, std::meta::access_context::current())[n];
}

template <std::size_t I, typename... Ts>
constexpr auto get(Tuple<Ts...> &t) noexcept -> std::tuple_element_t<I, Tuple<Ts...>> &
{
    return t.data.[:get_nth_nsdm(^^decltype(t.data), I):];
}

template <std::size_t I, typename... Ts>
constexpr auto get(Tuple<Ts...> const &t) noexcept
    -> std::tuple_element_t<I, Tuple<Ts...>> const &
{
    return t.data.[:get_nth_nsdm(^^decltype(t.data), I):];
}

template <std::size_t I, typename... Ts>
constexpr auto get(Tuple<Ts...> &&t) noexcept -> std::tuple_element_t<I, Tuple<Ts...>> &&
{
    return std::move(t).data.[:get_nth_nsdm(^^decltype(t.data), I):];
}

int main()
try
{
    Tuple<int, double, char> t(1, 2.3, 'c');
    std::cout << get<0>(t) << '\n'; // 1
    std::cout << get<1>(t) << '\n'; // 2.3

    t.data.__0_ = 2;
    std::cout << get<0>(t) << '\n'; // 2

    auto [a, b, c] = t;
    std::cout << a << '\n'; // 2
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