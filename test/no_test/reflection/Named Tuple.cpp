#include <iostream>
#include <meta>
#include <string>
#include <string_view>
#include <vector>
#include <initializer_list>
#include <utility> // std::pair

namespace meta = std::meta;

// 1. 核心函数：根据成员描述创建命名元组（聚合体）
consteval auto make_named_tuple(
    meta::info type,
    std::initializer_list<std::pair<meta::info, std::string_view>> members)
{
    std::vector<meta::info> nsdms;
    for (auto [mem_type, name] : members)
    {
        nsdms.push_back(meta::data_member_spec(mem_type, {.name = std::string(name)}));
    }
    return meta::define_aggregate(type, nsdms);
}

// 2. 声明一个结构体（仅声明，稍后在 consteval 块中定义）
struct R;

// 3. 编译期定义 R 的成员：int x 和 double y
consteval
{
    make_named_tuple(^^R, {
                              {^^int, "x"}, {^^double, "y"}});
}

// 4. 验证成员类型是否正确
constexpr auto ctx = std::meta::access_context::unchecked();
static_assert(meta::type_of(meta::nonstatic_data_members_of(^^R, ctx)[0]) == ^^int);
static_assert(meta::type_of(meta::nonstatic_data_members_of(^^R, ctx)[1]) == ^^double);

int main()
{
    // 5. 运行时创建对象并打印，证明聚合初始化有效
    R r{.x = 42, .y = 3.14};
    std::cout << "x = " << r.x << ", y = " << r.y << '\n';
    std::cout << "Named tuple works.\n";
    return 0;
}