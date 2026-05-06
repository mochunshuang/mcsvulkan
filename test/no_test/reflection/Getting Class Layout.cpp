#include <algorithm>
#include <iostream>
#include <exception>
#include <meta>
#include <array>

// NOLINTBEGIN
struct member_descriptor
{
    std::size_t offset;
    std::size_t size;

    bool operator==(const member_descriptor &) const = default;
};

// returns std::array<member_descriptor, N>
template <typename S>
consteval auto get_layout()
{
    constexpr auto ctx = std::meta::access_context::current();
    constexpr size_t N = std::meta::nonstatic_data_members_of(^^S, ctx).size();
    auto members = std::meta::nonstatic_data_members_of(^^S, ctx);

    std::array<member_descriptor, N> layout;
    for (int i = 0; i < members.size(); ++i)
    {
        layout[i] = {.offset =
                         static_cast<std::size_t>(std::meta::offset_of(members[i]).bytes),
                     .size = std::meta::size_of(members[i])};
    }
    return layout;
}

struct X
{
    char a;
    int b;
    double c;
};

int main()
try
{
    constexpr auto Xd = get_layout<X>();
    constexpr std::array<member_descriptor, 3> expected{{{0, 1}, {4, 4}, {8, 8}}};
    static_assert(std::ranges::equal(Xd, expected)); //NOTE: 需要实现 == 运算符

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