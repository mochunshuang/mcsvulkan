
#if __has_cpp_attribute(msvc::no_unique_address)
#define ATTR_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define ATTR_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

#include <iostream>
#include <exception>

int main()
try
{
    //gcc 是 0
    // clang: __has_cpp_attribute(msvc::no_unique_address): 201803
    std::cout << "__has_cpp_attribute(msvc::no_unique_address): "
              << __has_cpp_attribute(msvc::no_unique_address) << '\n';
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