#include <iostream>
#include <exception>
#include <print>

int main()
try
{
    std::println("main done\n");
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