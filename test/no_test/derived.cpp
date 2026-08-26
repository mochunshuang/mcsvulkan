#include <iostream>
#include <exception>
#include <type_traits>

struct base
{
    virtual ~base() = default;
};

template <class derivedImpl, auto fn>
struct derived : base
{
    static auto hover_fn()
    {
        fn(static_cast<derivedImpl *>(nullptr));
    }
};

int main()
try
{
    struct my_data : derived<my_data, [](auto *ptr) {
        using my_data = std::remove_pointer_t<decltype(ptr)>;
        int a = my_data{}->a;
    }>
    {
        int a;
    };

    // struct my_data2 : derived<my_data, [](my_data2 *ptr) {
    //     using my_data = std::remove_pointer_t<decltype(ptr)>;
    //     int a = my_data{}->a;
    // }>
    // {
    //     int a;
    // };
    struct static_type : base
    {
        static int fun(int a)
        {
            static int fn = a;
            return fn;
        }
    };

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