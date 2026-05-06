#include <cassert>
#include <iostream>
#include <exception>

#include <meta>

// NOLINTBEGIN
struct S
{
    unsigned i : 2, j : 6;
};

consteval auto member_number(int n)
{
    if (n == 0)
        return ^^S::i;
    else if (n == 1)
        return ^^S::j;
}

consteval auto member_number2(int n)
{
    return std::meta::nonstatic_data_members_of(^^S,
                                                std::meta::access_context::current())[n];
}

int main()
try
{
    S s{0, 0};
    s.[:member_number(1):] = 42; // Same as: s.j = 42;
    // s.[:member_number(5):] = 0;  // Error (member_number(5) is not a constant).

    assert(s.j == 42);

    s.[:member_number2(0):] = 1;
    assert(s.i == 1);

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
// NOLINTEND