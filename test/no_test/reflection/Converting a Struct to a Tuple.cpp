#include <iostream>
#include <meta>
#include <tuple>
#include <string>
#include <vector>

namespace meta = std::meta;

// 1. 生成目标元组类型：将 struct 的各成员类型（去 cvref）组合为 std::tuple
consteval meta::info type_struct_to_tuple(meta::info type)
{
    constexpr auto ctx = meta::access_context::current();
    std::vector<meta::info> member_types;

    for (auto mem : meta::nonstatic_data_members_of(type, ctx))
    {
        member_types.push_back(meta::remove_cvref(meta::type_of(mem)));
    }
    return meta::substitute(^^std::tuple, member_types);
}

// 2. 实际的转换函数模板：利用非类型模板参数包 members 来逐成员构造元组
template <typename To, typename From, meta::info... members>
constexpr auto struct_to_tuple_helper(From const &from) -> To
{
    return To(from.[:members:]...);
}

// 3. 编译期生成指向特定实例的函数指针
template <typename From>
consteval auto get_struct_to_tuple_helper()
{
    using To = typename[:type_struct_to_tuple(^^From):];
    auto ctx = meta::access_context::current();

    std::vector<meta::info> args = {^^To, ^^From};
    for (auto mem : meta::nonstatic_data_members_of(^^From, ctx))
    {
        args.push_back(meta::reflect_constant(mem));
    }

    return meta::extract<To (*)(From const &)>(
        meta::substitute(^^struct_to_tuple_helper, args));
}

// 4. 对外的接口
template <typename From>
constexpr auto struct_to_tuple(From const &from)
{
    return get_struct_to_tuple_helper<From>()(from);
}

// ---------- 测试用例 ----------
struct MyStruct
{
    int a;
    double b;
    std::string c;
};

int main()
{
    MyStruct s{42, 3.14, "hello reflection"};

    auto t = struct_to_tuple(s);

    // 验证类型
    static_assert(std::is_same_v<decltype(t), std::tuple<int, double, std::string>>);

    // 验证值
    std::cout << std::get<0>(t) << '\n'; // 42
    std::cout << std::get<1>(t) << '\n'; // 3.14
    std::cout << std::get<2>(t) << '\n'; // hello reflection

    std::cout << "struct_to_tuple works.\n";
    return 0;
}