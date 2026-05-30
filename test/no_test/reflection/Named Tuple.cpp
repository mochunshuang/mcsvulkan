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
        nsdms.push_back(meta::data_member_spec(mem_type, {.name = name}));
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

consteval auto test2()
{
    struct R;

    // 3. 编译期定义 R 的成员：int x 和 double y
    consteval
    {
        make_named_tuple(^^R, {
                                  {^^int, "x"}, {^^double, "y"}});
    }
    return true;
}
static_assert(test2());

#if 0
consteval auto test3(std::string_view x, std::string_view y)
{
    struct R;

    // 3. 编译期定义 R 的成员：int x 和 double y
    consteval
    {
        make_named_tuple(^^R, {
                                  {^^int, x}, {^^double, y}});
    }
    return true;
}
static_assert(test3("x", "y"));
#endif

#if 0
// 利用模板参数在编译期传递字符串常量
template <auto x, auto y> // x 和 y 现在都是编译期常量
consteval auto test3()
{
    struct R;
    consteval
    {
        make_named_tuple(^^R, {
                                  {^^int,
                                   std::string_view{
                                       x}},
                                  {^^double, std::string_view{
                                                 y}}});
    }
    return true;
}
// 调用时，字符串可以作为非类型模板参数传递
static_assert(test3<"x", "y">());
#endif
template <size_t N>
struct fixed_string
{
    char data[N]{};
    constexpr fixed_string(const char (&str)[N])
    {
        std::copy_n(str, N, data);
    }
    constexpr operator std::string_view() const
    {
        return {data, N - 1};
    }
};

template <fixed_string x, fixed_string y>
consteval auto test3()
{
    struct R;
    consteval
    {
        make_named_tuple(^^R, {
                                  {^^int,
                                   std::string_view{
                                       x}},
                                  {^^double, std::string_view{
                                                 y}}});
    }
    return true;
}
static_assert(test3<"x", "y">());

//NOTE: 只能模板生成
struct static_string
{
    const char *value{}; // NOLINT

    template <size_t N>
    consteval static_string(const char (&str)[N]) noexcept // NOLINT
        : value{std::define_static_string(str)}
    {
    }
    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return std::string_view{value};
    }
    constexpr bool operator==(const static_string &o) const noexcept
    {
        return view() == o;
    }
    constexpr bool operator==(const std::string_view &o) const noexcept
    {
        return view() == o;
    }
    template <size_t N>
    consteval bool operator==(const char (&str)[N]) const noexcept // NOLINT
    {
        return view() == std::string_view{str, N - 1};
    }
};
consteval auto test4(std::meta::info x_info, std::meta::info y_info)
{
    struct R;
    consteval
    {
        // NOTE: 一点都不像。只能当模板使用，'x_info' is not captured
        // auto v = x_info;
        make_named_tuple(^^R, {
                              });
    }
    return true;
}
static_assert(test4(std::meta::reflect_constant(static_string{"x"}),
                    std::meta::reflect_constant(static_string{"y"})));

consteval auto test5(std::meta::info x_info, std::meta::info y_info)
{
    struct R;
    constexpr auto make_r = [] {
        make_named_tuple(^^R, {
                              });
    };

    // NOTE: 提示为 static 可以，但是不能捕获
    static auto make_r2 = [=] {
        // the value of 'make_r2' is not usable in a constant expression
        // auto v = x_info;
        make_named_tuple(^^R, {
                              });
    };
    struct parm
    {
        std::meta::info x_info;
        std::meta::info y_info;
    };
    auto v = std::define_static_object(parm{x_info, y_info});

    //  error: 'y_info' is not a constant expression . 失败
    // constexpr auto v2 = std::define_static_object(
    //     parm{std::meta::reflect_constant(x_info), std::meta::reflect_constant(y_info)});

    consteval
    {
        // NOTE: 一点都不像。只能当模板使用，'x_info' is not captured
        // auto v = x_info;
        // make_named_tuple(^^R, {
        //                       });

        // NOTE: 编译期常量也不可以 。'make_r' is not captured
        // make_r();

        // NOTE: OK 但是没什么用
        // make_r2();

        // 'v' is not captured
        // auto v2 = v;

        // 'v2' is not captured
        // auto v3 = v2;
    }
    //NOTE: 总结： 还是当模板使用比较好。变化不大。模板中，生成类型。
    return true;
}
static_assert(test4(std::meta::reflect_constant(static_string{"x"}),
                    std::meta::reflect_constant(static_string{"y"})));

int main()
{
    // 5. 运行时创建对象并打印，证明聚合初始化有效
    R r{.x = 42, .y = 3.14};
    std::cout << "x = " << r.x << ", y = " << r.y << '\n';
    std::cout << "Named tuple works.\n";
    return 0;
}