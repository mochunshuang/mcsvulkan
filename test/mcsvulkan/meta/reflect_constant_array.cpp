#include <meta>
#include <array>
#include <cstdio>

namespace m = std::meta;

consteval auto part_xy()
{
    return std::array<m::info, 2>{
        m::data_member_spec(^^int,
                            {
                                .name = "x"}),
        m::data_member_spec(^^double,
                            {
                                .name = "y"}),
    };
}

consteval m::info pack()
{
    return m::reflect_constant_array(part_xy());
}

// N 未知，从类型反射推
static_assert(m::extent(m::type_of(pack()), 0) == 2);

consteval bool verify()
{
    const m::info *p = m::extract<const m::info *>(pack()); // 指针
    std::size_t n = m::extent(m::type_of(pack()), 0);       // N
    return n == 2 && m::identifier_of(p[0]) == "x" && m::identifier_of(p[1]) == "y";
}
static_assert(verify());

int main()
{
    std::puts("ok");
}