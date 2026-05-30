#include <cstddef>
#include <iostream>
#include <exception>
#include <meta>
#include <print>
#include <ranges>

// NOLINTBEGIN
template <typename T>
void do_something()
{
}

template <typename... Ts>
void f()
{
    template for (constexpr auto r : {^^Ts...})
    {
        using T = [:r:];
        do_something<T>();
    }
}

//-----------------------
consteval int f(auto const &...Containers)
{
    int result = 0;
    template for (auto const &c : {Containers...})
    { // OK, enumerating expansion statement
        result += c[0];
    }
    return result;
}
constexpr int c1[] = {1, 2, 3};
constexpr int c2[] = {4, 3, 2, 1};
static_assert(f(c1, c2) == 5);

//-----------------------
consteval int f()
{
    constexpr std::array<int, 3> arr{1, 2, 3};
    int result = 0;
    template for (int s : arr)
    { // OK, iterating expansion statement
        result += sizeof(char[s]);
    }
    return result;
}
static_assert(f() == 6);

//-------------------------------------------------
struct S
{
    int i;
    short s;
};
consteval long f(S s)
{
    long result = 0;
    template for (auto x : s)
    { // OK, destructuring expansion statement
        result += sizeof(x);
    }
    return result;
}
static_assert(f(S{}) == sizeof(int) + sizeof(short));

//-------------------------------------------------
void print_all(std::tuple<int, char> xs)
{
    template for (auto elem : xs)
    {
        std::println("{}", elem);
    }
}
//-------------------------------------------------
template <class... Ts>
void print_all(Ts... xs)
{
    template for (auto elem : {xs...})
    {
        std::println("{}", elem);
    }
}
//-------------------------------------------------
template <class T>
void print_members(T const &v)
{
    template for (constexpr auto e :
                  std::define_static_array(std::meta::nonstatic_data_members_of(
                      ^^T, std::meta::access_context::current())))
    {
        std::println(".{}={}", std::meta::identifier_of(e), v.[:e:]);
    }
}
// --------------------------------------------------
template <size_t N>
void print()
{
    template for (constexpr auto e :
                  std::define_static_array(std::ranges::views::iota(std::size_t{0}, N)))
    {
        std::println("{}", e);
    }
}
template <size_t N>
void print2()
{
    template for (constexpr auto e : std::ranges::views::indices(N))
    {
        std::println("{}", e);
    }
}

int main()
try
{
    print2<3>();
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
// NOLINTEND