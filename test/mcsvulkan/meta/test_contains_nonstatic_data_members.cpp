#include <iostream>
#include <exception>

#include "head.hpp"

using mcs::vulkan::meta::contains_nonstatic_data_members;

// NOLINTBEGIN
struct my_mystruct
{
    void fun() {}
    int x;
    float y;
    double z;
}; // NOLINTEND
static_assert(contains_nonstatic_data_members(^^my_mystruct, {
                                                                 "x"}));
static_assert(contains_nonstatic_data_members(^^my_mystruct, {
                                                                 "x", "y", "z"}));
static_assert(not contains_nonstatic_data_members(^^my_mystruct, {
                                                                     "x", "y", ""}));

consteval auto build_int_type()
{
    std::meta::info ret{};
    ret = ^^int;
    return ret;
}
static_assert(build_int_type() != std::meta::info{});

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