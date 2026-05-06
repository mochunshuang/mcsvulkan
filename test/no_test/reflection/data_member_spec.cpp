#include <assert.h>
#include <iostream>
#include <exception>

#include <meta>

using namespace std::meta;

union U;
consteval
{
    define_aggregate(^^U, {
                              data_member_spec(^^int,
                                               {
                                                   .name = "a"}),
                              data_member_spec(^^char,
                                               {
                                                   .name = "b"}),
                              data_member_spec(^^double,
                                               {
                                                   .name = "c"}),
                          });
}

struct A;
consteval
{
    define_aggregate(^^A, {
                              data_member_spec(^^int,
                                               {
                                                   .name = "a"}),
                              data_member_spec(^^char,
                                               {
                                                   .name = "b"}),
                              data_member_spec(^^double,
                                               {
                                                   .name = "c"}),
                          });
}

template <typename T>
struct S;
consteval
{
    define_aggregate(^^S<int>, {
                                   data_member_spec(^^int,
                                                    {
                                                        .name = "i", .alignment = 64}),
                                   data_member_spec(^^int,
                                                    {
                                                        .name = u8"こんにち"}),
                               });
}
// NOLINTBEGIN
int main()
try
{
    constexpr U a{0};
    static_assert(a.a == 0);
    constexpr A b{1, 'a', 0.1};
    static_assert(b.a == 1 && b.b == 'a' && b.c == 0.1);

    {
        constexpr S<int> a{.i = 1, .こんにち = 2};
        static_assert(a.i == 1 && a.こんにち == 2);
        static_assert(sizeof(decltype(a)) == 64);
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