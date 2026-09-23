#include <cassert>
#include <iostream>
#include <exception>
// NOLINTBEGIN

template <typename T>
struct runtime_id
{
    static constexpr std::uint8_t value = 0;
};
static constexpr auto invoke_filter(void * /*ptr*/, const void *type_id)
{
    if (&runtime_id<int>::value == type_id)
    {
        std::cout << "pass value\n";
        return true;
    }
    else
    {
        std::cout << "reject value\n";
        return false;
    }
}
template <typename T>
static constexpr auto invoke(T v)
{
    return invoke_filter(&v, &runtime_id<T>::value);
}

int main()
try
{
    {
        assert(invoke(0));
        assert(not invoke(0.0));
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