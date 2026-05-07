#include <meta>
#include <iostream>
#include <type_traits>
#include <string>

namespace meta = std::meta;

// ====================== data_member_options 与 data_member_spec ======================

// data_member_options 构造演示
consteval void test_options()
{
    meta::data_member_options o1 = {.name = "hello"};
    meta::data_member_options o2 = {.name = u8"你好"};
    meta::data_member_options o3 = {.bit_width = 7}; // 无名位域
    meta::data_member_options o4 = {
        .name = "x", .alignment = 16, .no_unique_address = true};
    (void)o1;
    (void)o2;
    (void)o3;
    (void)o4;
}
static_assert((test_options(), true));

// 1. 成员描述符（全部有名，避免无名位域带来的编译期验证问题）
constexpr meta::info desc_x = meta::data_member_spec(^^int, {
                                                                .name = "x"});
constexpr meta::info desc_y =
    meta::data_member_spec(^^double, {
                                         .name = "y", .alignment = 64});
constexpr meta::info desc_f =
    meta::data_member_spec(^^unsigned, {
                                           .name = "flag", .bit_width = 3});

// 2. 前置声明
struct MyAgg;

// 3. 在 consteval 块中注入类定义
consteval
{
    meta::define_aggregate(^^MyAgg, {
                                        desc_x, desc_y, desc_f});
}
//NOTE: 下面是不允许的
// [] consteval {
//     meta::define_aggregate(^^MyAgg, {
//                                         desc_x, desc_y, desc_f});
// }();

// 4. 验证类完整
static_assert(meta::is_complete_type(^^MyAgg));

// 5. 获取非静态数据成员的静态数组（关键修正：用 define_static_array）
constexpr auto members = define_static_array(
    meta::nonstatic_data_members_of(^^MyAgg, meta::access_context::unchecked()));

// 6. 编译期验证成员（数量为 3，索引 0~2 安全）
static_assert(members.size() == 3); // 无名位域不在此列
static_assert(meta::has_identifier(members[0]) && meta::identifier_of(members[0]) == "x");
static_assert(meta::has_identifier(members[1]) && meta::identifier_of(members[1]) == "y");
static_assert(meta::has_identifier(members[2]) &&
              meta::identifier_of(members[2]) == "flag");

// 7. 运行时使用

// NOTE: 模板生成
// 2. 模板：用 info 包定义内部 storage 类
template <meta::info... specs>
struct Injector
{
    struct storage;
    consteval
    {
        meta::define_aggregate(^^storage, {
                                              specs...});
    }
};

// 3. 用三个成员描述符实例化模板
using MyInjector = Injector<desc_x, desc_y, desc_f>;

// 4. 验证 storage 类是否正确定义并拥有预期成员
static_assert(meta::is_complete_type(^^MyInjector::storage));

constexpr auto inject_members = define_static_array(meta::nonstatic_data_members_of(
    ^^MyInjector::storage, meta::access_context::unchecked()));
static_assert(inject_members.size() == 3);
static_assert(meta::has_identifier(inject_members[0]) &&
              meta::identifier_of(inject_members[0]) == "x");
static_assert(meta::has_identifier(inject_members[1]) &&
              meta::identifier_of(inject_members[1]) == "y");
static_assert(meta::has_identifier(inject_members[2]) &&
              meta::identifier_of(inject_members[2]) == "flag");

int main()
{
    MyAgg a{1, 2.0, 7}; // 聚合初始化
    std::cout << "x=" << a.x << ", y=" << a.y << ", flag=" << a.flag << '\n';
    {
        MyInjector::storage obj{10, 3.14, 7}; // 聚合初始化
        std::cout << "obj.x = " << obj.x << ", obj.y = " << obj.y
                  << ", obj.flag = " << obj.flag << '\n';
        std::cout << "Injector test passed.\n";
    }
    std::cout << "All define_aggregate tests passed.\n";
    return 0;
}