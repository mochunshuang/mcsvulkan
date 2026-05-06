#include <iostream>
#include <exception>
#include <algorithm>
#include <array>
#include <meta>

// NOLINTBEGIN

template <class...>
struct list
{
};

int main()
try
{
    // Here, sizes will be a std::array<std::size_t, 3> initialized with {sizeof(int), sizeof(float), sizeof(double)}:
    constexpr std::array types = {^^int, ^^float, ^^double};
    constexpr std::array sizes = [=] {
        std::array<std::size_t, types.size()> r;
        std::ranges::transform(types, r.begin(), std::meta::size_of);
        return r;
    }();
    static_assert(std::ranges::equal(sizes, std::array{4, 4, 8}));

    //NOTE: 前后对比就知道 计算能力增强了
    // 将此与以下基于类型的方法进行比较，该方法产生相同的数组大小：
    {

        using types = list<int, float, double>;
        constexpr auto sizes = []<template <class...> class L, class... T>(L<T...>) {
            return std::array<std::size_t, sizeof...(T)>{{sizeof(T)...}};
        }(types{});
        static_assert(std::ranges::equal(sizes, std::array{4, 4, 8}));
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
} // NOLINTEND