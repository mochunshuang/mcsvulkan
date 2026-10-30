
#include <meta>
#include <iostream>

#include "head.hpp"

using mcs::vulkan::meta::composed_type;
using mcs::vulkan::meta::aggregate_type;
using mcs::vulkan::meta::concatenate_member_specs;
using mcs::vulkan::meta::named_member_group;
using mcs::vulkan::meta::compose_part;

// ======================================================================
// parts（测试用，实际项目中由用户定义）
// ======================================================================
consteval auto part_xy()
{
    return std::array<std::meta::info, 2>{
        std::meta::data_member_spec(^^int,
                                    {
                                        .name = "x"}),
        std::meta::data_member_spec(^^double,
                                    {
                                        .name = "y"}),
    };
}
consteval auto part_z()
{
    return std::array<std::meta::info, 1>{
        std::meta::data_member_spec(^^long, {
                                                .name = "z"})};
}
consteval auto part_str()
{
    return std::array<std::meta::info, 1>{
        std::meta::data_member_spec(^^const char *, {
                                                        .name = "s"})};
}

// ======================================================================
// tests
// ======================================================================

// 1. 拼接长度
static_assert(concatenate_member_specs(part_xy(), part_z(), part_str()).size() == 4);

// 2. 拼接结果直接作为 NTTP
template <auto v>
struct test_auto
{
};
using A = test_auto<concatenate_member_specs(part_xy(), part_z(), part_str())>;

// 3. 由拼接结果生成聚合体
using T1 [[maybe_unused]] = aggregate_type<part_xy(), part_z()>;
using T2 [[maybe_unused]] = aggregate_type<part_z(), part_str()>;
using T3 [[maybe_unused]] = aggregate_type<part_str(), part_xy(), part_z()>;

constexpr T1 a{1, 2.0, 3L};
constexpr T2 b{4L, "hi"};
constexpr T3 c{"yo", 5, 6.0, 7L};

static_assert(a.x == 1 && a.y == 2.0 && a.z == 3L);
static_assert(b.z == 4L && std::is_same_v<decltype(b.s), const char *>);
static_assert(c.s != nullptr && c.x == 5 && c.y == 6.0 && c.z == 7L);

static_assert(std::is_same_v<decltype(T1::x), int>);
static_assert(std::is_same_v<decltype(T1::y), double>);
static_assert(std::is_same_v<decltype(T1::z), long>);
static_assert(std::is_same_v<decltype(T2::s), const char *>);

// 4. 命名成员组合
constexpr named_member_group spec_xy{"xy", compose_part{part_xy()}};
constexpr named_member_group spec_z{"z", compose_part{part_z()}};

using T = composed_type<spec_xy, spec_z>;
// T == { struct{int x; double y;} xy; struct{long z;} z; }

constexpr T t{{1, 2.0}, {3L}};
static_assert(t.xy.x == 1 && t.xy.y == 2.0 && t.z.z == 3L);

// 5. 三个组合
constexpr named_member_group spec_str{"s", compose_part{part_str()}};
using T4 = composed_type<spec_xy, spec_z, spec_str>;
constexpr T4 t4{{1, 2.0}, {3L}, {"hi"}};
static_assert(t4.xy.x == 1 && t4.z.z == 3L && t4.s.s != nullptr);

// 6. 单段组合
using T5 = composed_type<spec_xy>;
constexpr T5 t5{{10, 20.0}};
static_assert(t5.xy.x == 10 && t5.xy.y == 20.0);

int main()
{
    std::cout << "all checks passed\n";
    return 0;
}