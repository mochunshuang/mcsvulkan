#include <cstdint>
#include <iostream>
#include <exception>
#include <memory>
#include <vector>

// NOLINTBEGIN
template <typename T>
struct type_pool
{
    std::unique_ptr<T[]> pool_;
    std::vector<uint32_t> used_;
    std::vector<uint32_t> free_;
    type_pool(uint32_t size) {}
};

// NOLINTEND
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
}