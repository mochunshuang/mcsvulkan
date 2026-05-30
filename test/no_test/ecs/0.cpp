#include <iostream>
#include <exception>
#include <print>
#include <tuple>
#include <utility>
#include <ranges>

// NOLINTBEGIN
template <typename T, int V>
struct type_value
{
    static constexpr auto value = V;
    using type = T;
}; // NOLINTEND

struct A
{
};
using T = type_value<A, 0>;

namespace detail
{
    template <typename type_meta, typename Receiver, typename... Args>
    static consteval auto &get_table() noexcept
    {
        static constexpr auto table = [] { // NOLINT
            static constexpr std::size_t N = std::tuple_size_v<type_meta>;
            using FuncPtr = void (*)(void *, Receiver &, Args &&...);
            std::array<FuncPtr, N> t{};
            template for (constexpr auto i : std::ranges::views::indices(N))
            {
                t[i] = [](void *d, Receiver &recv, Args &&...a) {
                    using T = std::tuple_element_t<i, type_meta>;
                    static_assert(
                        noexcept(recv(static_cast<T *>(d), std::forward<Args>(a)...)),
                        "need Receiver noexcept");
                    recv(static_cast<T *>(d), std::forward<Args>(a)...);
                };
            }
            return t;
        }();
        return table;
    }
}; // namespace detail

struct type_cast_forward
{
    using type_meta = std::tuple<A, int, int>;
    // 关键：将 Receiver 和 Args 作为模板参数
    template <typename Receiver, typename... Args>
    void operator()(int type_value, void *data, Receiver &receiver, Args &&...args) const
    {
        constexpr auto &table = detail::get_table<type_meta, Receiver, Args...>();

        // 运行时 O(1) 分发
        // if (type_value >= 0 && type_value < N)
        table[type_value](data, receiver, std::forward<Args>(args)...);
    }
};
void test_0()
{

    T::value;
}

int main()
try
{

    type_cast_forward dispatcher;
    A a;
    int x = 42, y = 99;

    // receiver 是一个泛型 lambda
    auto print = [](auto *ptr, int extra) noexcept {
        if constexpr (std::same_as<std::decay_t<decltype(*ptr)>, A>)
            std::println("A object with extra {}", extra);
        else
            std::println("int value: {}, extra {}", *ptr, extra);
    };

    dispatcher(0, &a, print, 10); // 输出 A object with extra 10
    dispatcher(1, &x, print, 20); // 输出 int value: 42, extra 20
    dispatcher(2, &y, print, 30); // 输出 int value: 99, extra 30

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