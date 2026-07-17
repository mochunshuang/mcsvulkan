#include <array>
#include <assert.h>
#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <exception>
#include <print>
#include <span>
#include <string_view>

#include <format>
#include <type_traits>
#include <list>
#include <algorithm>

#include "head.hpp"

using mcs::vulkan::meta::has_all_annotations;
using mcs::vulkan::meta::static_string;
using mcs::vulkan::meta::fixed_string;
using mcs::vulkan::meta::identifier_view;
using mcs::vulkan::meta::type;
using mcs::vulkan::meta::contains_nonstatic_data_members;

struct my_struct // NOLINT
{
    static auto static_fun() {}
    auto fun() {}
    int m;
};
bool v;

// NOTE: 注解
template <auto V>
struct Derive
{
    constexpr bool operator==(const Derive &) const noexcept
    {
        return true;
    };
};
template <auto V>
inline constexpr Derive<V> derive;

// struct Debug_t
// {
// };
constexpr struct
{
} Debug;
struct myString
{
    const char *value;
    constexpr bool operator==(const myString &o) const noexcept
    {
        return std::string_view{value} == std::string_view{o.value};
    }
};
constexpr auto print_a = static_string{"rename: value"};
static_assert(print_a == static_string{"rename: value"});
struct [[nodiscard]][[= derive<Debug>]][[ = int{0}, = 1, = 2 ]]
                    [[= static_string{"print"}]] Point
{
    [[= print_a]] int x;
    [[= "rename: value"]] int y;
    [[= myString{"rename: value"}]] int z;
    [[= myString{"rename"}]] int w;
};

static_assert(static_string{"print"} == "print");
static_assert(static_string{"print"} == std::string_view{"print"});
static_assert(static_string{"print"} == static_string{"print"});
static_assert(myString{"print"} == myString{"print"});

static_assert(has_all_annotations(std::meta::annotations_of(^^Point), derive<Debug>,
                                  int{0}));
static_assert(has_all_annotations(std::meta::annotations_of(^^Point), 1, 2));
static_assert(has_all_annotations(std::meta::annotations_of(^^Point), derive<Debug>));
// static_assert(has_all_annotations(std::meta::annotations_of(^^Point),
//                                   static_string{"print"}));
static_assert(not has_all_annotations(std::meta::annotations_of(^^Point), "123"));
// NOTE: 类型非常严格

static_assert(not has_all_annotations(std::meta::annotations_of(^^Point), "print"));
static_assert(not has_all_annotations(std::meta::annotations_of(^^Point),
                                      std::string_view{"print"}));
static_assert(not std::meta::annotations_of(^^Point::x).empty());
static_assert(std::meta::annotations_of(^^Point::x).size() == 1);
// NOTE: bug
// static_assert(std::meta::extract<static_string>(
//                   std::meta::annotations_of (^^Point::x)[0]) == print_a);
static_assert(has_all_annotations(std::meta::annotations_of(^^Point::x), print_a));

// static_assert(std::meta::annotations_of(^^Point::y).size() == 1);
// static_assert(std::meta::extract<const char *const>(std::meta::annotations_of (
//                   ^^Point::y)[0]) == std::string_view{"rename: value"});

static_assert(std::meta::annotations_of(^^Point::z).size() == 1);
// static_assert(std::meta::extract<myString>(std::meta::annotations_of (^^Point::z)[0]) ==
//               myString{"rename: value"});

static_assert(std::meta::annotations_of(^^Point::z).size() == 1);
// static_assert(std::meta::extract<myString>(std::meta::annotations_of (^^Point::w)[0]) ==
//               myString{"rename"});

// ============================================= 字符串测试
struct HelpArg
{
    const char *msg; // 不能是 std::string_view（非结构性类型）
    constexpr bool operator==(const HelpArg &o) const noexcept
    {
        return std::string_view{msg} == std::string_view{o.msg};
    }
    constexpr bool operator==(const std::string_view &o) const noexcept
    {
        return std::string_view{msg} == o;
    }
};

consteval HelpArg Help(std::string_view msg)
{
    return HelpArg{std::define_static_string(msg)};
}
consteval auto make_static_string(static_string s)
{
    return s;
}
consteval auto to_constexpr(static_string v)
{
    return v;
}
// NOTE: 修改 static_string 类似  annotation_string 保证类型统一，不需要 annotation_string
struct annotation_string
{
    const char *value{}; // NOLINT

    annotation_string() = default;
    consteval explicit annotation_string(std::string_view view)
        : value{std::define_static_string(view)}
    {
    }
    consteval explicit annotation_string(const char *value) noexcept
        : annotation_string(std::string_view{value})
    {
    }
    template <size_t N>
    consteval explicit annotation_string(const char (&str)[N]) noexcept // NOLINT
        : annotation_string(std::string_view{&str, N - 1})
    {
    }
    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return std::string_view{value};
    }
    consteval bool operator==(const std::string_view &o) const noexcept
    {
        return view() == o;
    }
    template <size_t N>
    consteval bool operator==(const char (&str)[N]) const noexcept // NOLINT
    {
        return view() == std::string_view{str, N - 1};
    }
};
// 使用
struct Args
{
    [[= Help("Name of the person to greet")]] std::string name;
    [[= HelpArg("Name of the person to greet")]] std::string name2;
    [[= make_static_string("Name of the person to greet")]] std::string name3;
    [[= make_static_string(static_string{
        "Name of the person to greet"})]] std::string name4; //NOTE: 非常离谱说实话

    [[= annotation_string("Name of the person to greet")]] std::string name5;
    [[= static_string("Name of the person to greet")]] std::string name6;

    [[= ^^int]] std::string type;
};
constexpr auto ann = std::meta::annotations_of(^^Args::name)[0];
constexpr HelpArg help = std::meta::extract<HelpArg>(ann);
static_assert(std::string_view{help.msg} ==
              std::string_view{"Name of the person to greet"});
static_assert(help == std::string_view{"Name of the person to greet"});
static_assert(std::meta::extract<HelpArg>(std::meta::annotations_of (^^Args::name)[0]) ==
              std::string_view{"Name of the person to greet"});

// NOTE: 不是常量.....................
// static_assert(std::meta::extract<HelpArg>(std::meta::annotations_of (^^Args::name2)[0]) ==
//               std::string_view{"Name of the person to greet"});

static_assert(std::meta::extract<static_string>(std::meta::annotations_of (
                  ^^Args::name3)[0]) == std::string_view{"Name of the person to greet"});
// static_assert(has_all_annotations(std::meta::annotations_of(^^Args::name3),
//                                   static_string{"Name of the person to greet"}));
static_assert(has_all_annotations(std::meta::annotations_of(^^Args::name4),
                                  static_string{"Name of the person to greet"}));

static_assert(std::meta::extract<annotation_string>(std::meta::annotations_of (
                  ^^Args::name5)[0]) == std::string_view{"Name of the person to greet"});

static_assert(std::meta::extract<static_string>(std::meta::annotations_of (
                  ^^Args::name6)[0]) == std::string_view{"Name of the person to greet"});

static_assert(has_all_annotations(std::meta::annotations_of(^^Args::name6),
                                  static_string{"Name of the person to greet"}));

static_assert(has_all_annotations(std::meta::annotations_of(^^Args::type), ^^int));

consteval bool test_static_string_address()
{
    // 用三种方式构造，内容都是 "hello"
    constexpr static_string s1{"hello"};                   // ① 字面量构造函数
    constexpr static_string s2{std::string_view{"hello"}}; // ② string_view 构造函数
    constexpr static_string s3{
        static_cast<const char *>("hello")}; // ③ const char* 构造函数

    // 指针必须相同
    static_assert(s1.value == s2.value);
    static_assert(s1.value == s3.value);

    // 额外验证内容相等（其实指针相同已经隐含了）
    static_assert(s1.view() == "hello");
    static_assert(s2.view() == std::string_view{"hello"});
    static_assert(s3 == "hello");

    // 再验证：不同字符串指针必须不同
    constexpr static_string other{"world"};
    static_assert(s1.value != other.value);

    return true;
}
static_assert(test_static_string_address());

// 测试类
struct bind : static_string
{
};
struct component : static_string
{
};
struct TestParams
{
    void plain_func([[= bind{"x"}]][[= component{"TestParams"}]] const int &x) {}
    void explicit_obj_func(this TestParams &self, int y) {}
    void func_with_default_param(int a, int b = 5) {}
    static void static_func(int x) {}
    int x;
};

struct map_type
{
    component key;
    std::meta::info type;
};

// template <component{"TestParams"}, TestParams>

constexpr auto map_types =
    std::array<map_type, 1>{{component{"TestParams"}, ^^TestParams}};
consteval std::meta::info get_type_info(const component &key)
{
    for (const auto &entry : map_types)
    {
        if (entry.key == key)
        {
            return entry.type;
        }
    }
    throw std::meta::exception{"component not found map type",
                               std::meta::current_function()};
}

// ---- is_function_parameter ----
consteval bool test_function_parameter(std::meta::info info)
{
    if (!std::meta::is_function(info))
        throw std::meta::exception{"info not function", std::meta::current_function()};

    std::vector<std::meta::info> p_infos = std::meta::parameters_of(info);
    for (std::meta::info p : p_infos)
        if (!std::meta::is_function_parameter(p))
            return false;
    if (p_infos.size() != 1)
        return false;
    std::vector<std::meta::info> binds =
        std::meta::annotations_of_with_type(p_infos[0], ^^bind);
    if (binds.size() != 1)
        return false;
    std::vector<std::meta::info> comps =
        std::meta::annotations_of_with_type(p_infos[0], ^^component);
    if (comps.size() != 1)
        return false;

    std::meta::info out_p_infp = {};
    try
    {
        if (std::meta::extract<bind>(binds[0]) != "x")
            return false;
        // 查询
        std::vector<std::meta::info> mbs = nonstatic_data_members_of(
            get_type_info(std::meta::extract<component>(comps[0])),
            std::meta::access_context::current());
        if (mbs.size() != 1)
            return false;
        if (identifier_view(mbs[0]) != std::meta::extract<bind>(binds[0]))
            return false;

        //NOTE: 得到引用 或 测试填值？
        if (not std::meta::is_same_type(type(p_infos[0]), ^^const int &))
            return false;
        if (not std::meta::is_same_type(type(mbs[0]), ^^int))
            return false;

        if (not std::meta::is_same_type(std::meta::remove_cvref(type(mbs[0])),
                                        std::meta::remove_cvref(type(p_infos[0]))))
            return false;

        out_p_infp = p_infos[0];
    }
    catch (...)
    {
        return false;
    }
    //NOTE: 类型是不可知的
    // using T = typename[:std::meta::reflect_constant(out_p_infp):];

    //NOTE: info 是可以重新赋值
    {
        info = ^^int;
        if (std::meta::is_function(info))
            return false;
    }

    return true;
}
static_assert(test_function_parameter(^^TestParams::plain_func));

static_assert(contains_nonstatic_data_members(^^TestParams, {
                                                                "x"}));
static_assert(not contains_nonstatic_data_members(^^TestParams, {
                                                                    "y"}));

struct my_mystruct
{
    int x;
    float y;
    double z;
};
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

// template <size_t N>
// struct name_spec
// {
//     fixed_string<N> name;
//     std::meta::info info;
//     constexpr name_spec(const char (&str)[N], std::meta::info info)
//         : name{str}, info{info}
//     {
//     }
// };

struct name_spec
{
    static_string name;
    std::meta::info info;
};

template <name_spec... infos>
struct static_fun_set
{
  private:
    template <static_string name>
    static consteval bool has_exactly_one()
    {
        return ((infos.name == name ? 1 : 0) + ...) == 1;
    }
    static consteval auto span()
    {
        // return std::define_static_array(
        //     std::array<name_spec, sizeof...(infos)>{infos...});

        //NOTE: 外面就不能使用了. 需要提升为常量。不过模板肯定是固定，可以不使用define_static_array
        return std::array<name_spec, sizeof...(infos)>{infos...};
    }

  public:
    template <static_string name>
        requires(has_exactly_one<name>())
    static consteval decltype(auto) spec()
    {
        // return std::ranges::find_if(span(), [](const auto &n) { return n.name == name; })
        //     ->info;

        constexpr auto span = std::array<name_spec, sizeof...(infos)>{infos...};
        return std::ranges::find_if(span, [](const auto &n) { return n.name == name; })
            ->info;
    }

    template <static_string name>
        requires(has_exactly_one<name>())
    static constexpr auto bind()
    {
        constexpr auto invoke_info = spec<name>();
        constexpr auto type_info = type(invoke_info);
        using invoke_type = typename[:type_info:];
        return std::meta::extract<invoke_type>(invoke_info);
    }
};

consteval auto test_invoke()
{

    constexpr auto update = [](int v) -> int {
        v = v + 1;
        return v;
    };
    using T = static_fun_set<{.name = "update", .info = ^^update}>;
    constexpr auto bind = T::bind<"update">();
    static_assert(
        std::is_same_v<std::decay_t<decltype(update)>, std::decay_t<decltype(bind)>>);

    static_assert(bind(0) == 1);

    {
        // constexpr auto bind = T::bind<"update2">();
    }
    {
        //NOTE: 重名有意想不到的麻烦
        constexpr auto update2 = [a = 2](int v) -> int {
            v = v + 1;
            return a + v;
        };
        using T = static_fun_set<{"update2", ^^update2}>;
        constexpr auto bind2 = T::bind<"update2">();
        static_assert(std::is_same_v<std::decay_t<decltype(update2)>,
                                     std::decay_t<decltype(bind2)>>);

        static_assert(bind2(0) == 1 + 2);
    }

    return true;
}
static_assert(test_invoke());

template <name_spec... infos>
struct make_aggregate
{
    struct aggregate_type;
    consteval
    {
        std::vector<std::meta::info> nsdms;
        template for (constexpr auto &spec : {infos...})
        {
            nsdms.push_back(
                std::meta::data_member_spec(spec.info, {.name = spec.name.view()}));
        }
        std::meta::define_aggregate(^^aggregate_type, nsdms);
    }
};
template <name_spec... infos>
using make_aggregate_t = make_aggregate<infos...>::aggregate_type;

void test_conm()
{
    struct world
    {
        struct point
        {
            int x;
            int y;
        };

        struct soa_point
        {
            std::vector<decltype(point::x)> x;
            std::vector<decltype(point::y)> y;
        };
        soa_point soa_point_;

        auto view_x()
        {
            return std::span<decltype(soa_point::x)::value_type>{soa_point_.x.begin(),
                                                                 soa_point_.x.end()};
        }
        // 只读/读写访问 x 和 y 的 zip 视图
        auto view_x_y()
        {
            return std::views::zip(soa_point_.x, soa_point_.y);
        }

        // 如果还需要 const 版本
        auto view_x_y() const
        {
            return std::views::zip(std::span<const int>(soa_point_.x),
                                   std::span<const int>(soa_point_.y));
        }
    };

    world w;
    w.soa_point_.x = {1, 2, 3};
    w.soa_point_.y = {4, 5, 6};
    std::println("w.soa_point_.x: {}", w.soa_point_.x);
    for (auto [x, y] : w.view_x_y())
    {
        // x 为 int&, y 为 int&，可直接修改
        static_assert(std::is_same_v<int &, decltype(x)>);
        x += y;
    }
    std::println("w.soa_point_.x: {}", w.soa_point_.x);
    {
        auto print = [](auto const rem, auto const &r) {
            std::cout << rem << '{';
            for (char o[]{0, ' ', 0}; auto const &e : r)
                std::cout << o << e, *o = ',';
            std::cout << "}\n";
        };
        auto v1 = std::vector<float>{1, 2, 3};
        auto v2 = std::list<short>{1, 2, 3, 4};
        auto v3 = std::to_array({1, 2, 3, 4, 5});

        auto add = [](auto a, auto b, auto c) {
            return a + b + c;
        };

        // NOTE: 新集合。 前后没用改变
        auto sum = std::views::zip_transform(add, v1, v2, v3);

        print("v1:  ", v1);
        print("v2:  ", v2);
        print("v3:  ", v3);
        print("sum: ", sum);

        print("v1:  ", v1);
        print("v2:  ", v2);
        print("v3:  ", v3);
        {
            auto x = std::vector{1, 2, 3, 4};
            auto y = std::list<std::string>{"α", "β", "γ", "δ", "ε"};
            auto z = std::array{'A', 'B', 'C', 'D', 'E', 'F'};

            print("源视图:", "");
            print("x: ", x);
            print("y: ", y);
            print("z: ", z);

            print("\nzip(x,y,z):", "");

            for (std::tuple<int &, std::string &, char &> elem : std::views::zip(x, y, z))
            {
                std::cout << std::get<0>(elem) << ' ' << std::get<1>(elem) << ' '
                          << std::get<2>(elem) << '\n';

                std::get<char &>(elem) += ('a' - 'A'); // 修改 z 的元素
            }

            print("\n修改后, z: ", z);
        }
    }
};

struct world
{

    template <name_spec... item>
    struct class_container;

    template <static_string key>
    using T = int;
};

template <std::meta::info Spec>
consteval auto gen_type()
{

    struct ret_type;
    consteval
    {
        // std::vector<std::meta::info> nsdms;
        // for (const auto &spec : std::meta::extract<std::vector<name_spec>>(Spec))
        // {
        //     nsdms.push_back(
        //         std::meta::data_member_spec(spec.info, {.name = spec.name.view()}));
        // }
        // std::meta::define_aggregate(^^ret_type, nsdms);
    }
};

consteval std::meta::info make_world(std::meta::info classs)
{
    std::vector<std::meta::info> vec = std::meta::template_arguments_of(classs);

    if (vec.size() != 2)
        throw std::meta::exception{"all_class.size() != 2",
                                   std::meta::current_function()};

    bool all_is_name_spec_type = std::ranges::all_of(vec, [](std::meta::info v) {
        return std::meta::is_same_type(type(v), ^^name_spec);
    });
    if (all_is_name_spec_type)
        throw std::meta::exception{"all_is_name_spec_type",
                                   std::meta::current_function()};
    auto name_spec_vec = vec | std::views::transform([](std::meta::info v) {
                             return std::meta::extract<name_spec>(v);
                         }) |
                         std::ranges::to<std::vector<name_spec>>();

    // constexpr auto class_container = std::define_static_array(name_spec_vec);

    // struct class_container_type;
    // gen_type<std::meta::reflect_constant(class_container)>();

    struct world
    {
    };

    return {};
}

// NOTE: view 和 tag  +  soa

int main()
try
{
    test_conm();
    using T = make_aggregate_t<{"x", ^^int}, {"y", ^^double}, {"z", ^^float}>;
    T a{.x = 1, .y = 2, .z = 3};
    std::println("identifier_view(^^T): {}", identifier_view(^^T));
    std::println("identifier_view(std::meta::dealias(^^T)): {}",
                 identifier_view(std::meta::dealias(^^T)));
    std::println("std::meta::display_string_of(std::meta::dealias(^^T)): {}",
                 std::meta::display_string_of(std::meta::dealias(^^T)));
    std::println("std::meta::display_string_of(^^T): {}",
                 std::meta::display_string_of(^^T));

    {
        constexpr auto update = [](int v) -> int {
            v = v + 1;
            return v;
        };
        using T = static_fun_set<{"update", ^^update}>;
        constexpr auto bind = T::bind<"update">();
        static_assert(
            std::is_same_v<std::decay_t<decltype(update)>, std::decay_t<decltype(bind)>>);

        static_assert(bind(0) == 1);
    }
    {
        constexpr auto update2 = [](int v) -> int {
            v = v + 1;
            return v;
        };
        using T = static_fun_set<{"update2", ^^update2}>;
        constexpr auto bind2 = T::bind<"update2">();
        bool same = std::is_same_v<std::decay_t<decltype(update2)>,
                                   std::decay_t<decltype(bind2)>>;
        assert(same);
        assert(bind2(0) == 1);

        // using T = static_fun_set<{"update2", ^^([](int v) -> int {
        //                               v = v + 1;
        //                               return v;
        //                           })}>;
        // constexpr auto bind2 = T::bind<"update2">();
        // assert(bind2(0) == 1);
    }
    //NOTE: 非运行时可用。不过也是很好的。至少 main中可可以使用
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