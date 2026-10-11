#include <iostream>
#include <exception>
// NOLINTBEGIN

template <class... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;

    template <class F>
    constexpr void apply(F &&f) noexcept(
        noexcept(std::forward<F>(f)(static_cast<Ts &>(*this)...)))
    {
        std::forward<F>(f)(static_cast<Ts &>(*this)...);
    }
};

void test()
{
    overloaded<decltype([]() {}), decltype([]() {})>{}.apply([](auto &&...a) {});
}

int main()
try
{
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