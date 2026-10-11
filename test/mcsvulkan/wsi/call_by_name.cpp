#include <concepts>
#include <iostream>
#include <exception>
#include <optional>
#include <print>
#include <type_traits>

#include "../../../include/detail/__meta.hpp"

using mcs::vulkan::meta::make_aggregate;
using mcs::vulkan::meta::field;
using mcs::vulkan::meta::method;
using mcs::vulkan::meta::static_string;
using mcs::vulkan::meta::name_spec;

// NOLINTBEGIN
template <static_string name, class T, class... Args>
constexpr auto call_by_name(T &obj, Args &&...args)
{
    constexpr auto ctx = std::meta::access_context::current();

    // template for (constexpr auto m : std::define_static_array(
    //                   std::meta::nonstatic_data_members_of(^^T, ctx)))
    // {
    //     // if constexpr (std::meta::identifier_of(m) == name)
    //     // {
    //     //     return obj.[:m:](std::forward<Args>(args)...); // ← 真正的调用
    //     // }
    //     return 1;
    // }
    // throw std::runtime_error("call_by_name: no such member function");

    return 1;
}
/*
consteval vector<info> members_of(info, access_context);           // 所有成员（含函数、类型、数据成员…）
consteval vector<info> static_data_members_of(info, access_context);   // 只含静态数据成员
consteval vector<info> nonstatic_data_members_of(info, access_context); // 只含非静态数据成员
*/
consteval std::meta::info find_nonstatic_data_member(auto ctx, std::meta::info class_ref,
                                                     static_string name)
{
    for (auto m : std::meta::nonstatic_data_members_of(class_ref, ctx))
        if (std::meta::has_identifier(m) && std::meta::identifier_of(m) == name)
            return m;
    return {};
}
consteval std::meta::info find_member(auto ctx, std::meta::info class_ref,
                                      static_string name)
{
    for (auto m : std::meta::members_of(class_ref, ctx))
        if (std::meta::has_identifier(m) && std::meta::identifier_of(m) == name)
            return m;
    return {};
}

consteval std::optional<std::meta::info> find_member_new(
    std::meta::info class_ref, static_string name, auto pred,
    std::meta::access_context ctx = std::meta::access_context::current())
    requires(requires() {
        { pred(std::meta::info{}) } -> std::same_as<bool>;
    })
{
    for (auto m : std::meta::members_of(class_ref, ctx))
        if (std::meta::has_identifier(m) && std::meta::identifier_of(m) == name &&
            pred(m))
            return std::optional<std::meta::info>{m};
    return std::optional<std::meta::info>{std::nullopt};
}
consteval auto find_member_new(
    std::meta::info class_ref, static_string name,
    std::meta::access_context ctx = std::meta::access_context::current())
{
    return find_member_new(class_ref, name, [](auto) { return true; }, ctx);
}

consteval std::optional<std::meta::info> find_member2(auto ctx, std::meta::info class_ref,
                                                      static_string name)
{
    for (auto m : std::meta::members_of(class_ref, ctx))
        if (std::meta::has_identifier(m) && std::meta::identifier_of(m) == name)
            return std::optional<std::meta::info>{m};
    return std::optional<std::meta::info>{std::nullopt};
}

// ---------- 找函数（只筛函数） ----------
// ---------- 严格：只找函数 / 函数模板 ----------
consteval std::optional<std::meta::info> find_function(std::meta::info cls,
                                                       static_string name)
{
    return find_member_new(
        cls, name,
        [](std::meta::info m) -> bool {
            return std::meta::is_function(m) || std::meta::is_function_template(m);
        },
        std::meta::access_context::current());
}

// ---------- 只找非静态数据成员（可能是 lambda / 函数对象） ----------
consteval std::optional<std::meta::info> find_nonstatic_data_member(std::meta::info cls,
                                                                    static_string name)
{
    return find_member_new(
        cls, name,
        [](std::meta::info m) -> bool { return std::meta::is_nonstatic_data_member(m); },
        std::meta::access_context::current());
}

template <static_string name, class T>
consteval bool has_member_function()
{
    return find_function(^^T, name).has_value();
}

template <static_string name, class T>
consteval bool has_member_data()
{
    return find_nonstatic_data_member(^^T, name).has_value();
}

template <static_string fn_name, class Obj, class... Args>
    requires(has_member_function<fn_name, std::remove_cvref_t<Obj>>() ||
             has_member_data<fn_name, std::remove_cvref_t<Obj>>())
constexpr decltype(auto) invoke_member(Obj &&obj, Args &&...args)
{
    using T = std::remove_cvref_t<Obj>;

    if constexpr (has_member_function<fn_name, T>())
    {
        // ===== 分支 1：函数 / 函数模板 =====
        constexpr auto m = *find_function(^^T, fn_name);

        if constexpr (requires { obj.[:m:](std::forward<Args>(args)...); })
            return obj.[:m:](std::forward<Args>(args)...);
        else if constexpr (requires { obj.template[:m:](std::forward<Args>(args)...); })
            return obj.template[:m:](std::forward<Args>(args)...);
        else
            static_assert(sizeof(Obj) == 0,
                          "invoke_member: function exists but not callable "
                          "with the given arguments");
    }
    else
    {
        // ===== 分支 2：数据成员（lambda / 函数对象 / 普通数据） =====
        constexpr auto m = *find_nonstatic_data_member(^^T, fn_name);

        if constexpr (requires { obj.[:m:](std::forward<Args>(args)...); })
            return obj.[:m:](std::forward<Args>(args)...);
        else
            static_assert(sizeof(Obj) == 0,
                          "invoke_member: data member exists but not callable "
                          "with the given arguments");
    }
}

// ---------- invoke_member ----------
struct my_class
{
    constexpr void print(auto &&...)
    {
        std::println("my_class:: print");
    }
    constexpr auto return_value(auto ret)
    {
        return ret;
    }

    struct PrintFn
    {
        constexpr auto operator()(auto &&...) const
        {
            return 1;
        }
    };
    PrintFn print_lambda{};

    int mb;
};

template <static_string fn_name, class Obj, class... Args>
consteval bool can_invoke()
{
    using T = std::remove_cvref_t<Obj>;

    if constexpr (has_member_function<fn_name, T>())
    {
        constexpr auto m = *find_function(^^T, fn_name);
        return requires(T &o, Args &&...as) { o.[:m:](std::forward<Args>(as)...); };
    }
    else if constexpr (has_member_data<fn_name, T>())
    {
        constexpr auto m = *find_nonstatic_data_member(^^T, fn_name);
        return requires(T &o, Args &&...as) { o.[:m:](std::forward<Args>(as)...); };
    }
    else
    {
        return false;
    }
}

// ---- has_member_function：严格函数语义 ----
static_assert(has_member_function<"print", my_class>());
static_assert(has_member_function<"return_value", my_class>());
static_assert(!has_member_function<"mb", my_class>());           // ✅ 成立
static_assert(!has_member_function<"print_lambda", my_class>()); // ✅ 成立
static_assert(!has_member_function<"return_va", my_class>());

// ---- has_member_data：只数据成员 ----
static_assert(has_member_data<"mb", my_class>());
static_assert(has_member_data<"print_lambda", my_class>());
static_assert(!has_member_data<"print", my_class>());
static_assert(!has_member_data<"return_value", my_class>());

// ---- invoke_member：三条路各自走通 ----
static_assert(invoke_member<"return_value">(my_class{}, int{1}) == 1); // 函数模板
static_assert(invoke_member<"print_lambda">(my_class{}, int{1}) == 1); // lambda 数据成员
static_assert(invoke_member<"print_lambda">(my_class{}) == 1);         // lambda，无参
static_assert(!has_member_function<"mb", my_class>()); // 名字存在但走数据分支

// NOTE: 配合 requires
static_assert(requires() { invoke_member<"print_lambda">(my_class{}); });
// static_assert(!requires() { invoke_member<"none">(my_class{}); });
// 名字不存在 → false（不报硬错）
static_assert(!can_invoke<"none", my_class>());

// 函数模板，实参可推 → true
static_assert(can_invoke<"return_value", my_class, int>());
static_assert(can_invoke<"return_value", my_class, double>());

// 数据成员 lambda，实参匹配 → true
static_assert(can_invoke<"print_lambda", my_class>());      // 无参 OK
static_assert(can_invoke<"print_lambda", my_class, int>()); // 有参 OK

// 数据成员，但不是可调用对象 → false
static_assert(!can_invoke<"mb", my_class>());

// 函数存在但实参不匹配 → false（这才是「真正可调用」的判定）
struct only_int
{
    void f(int) {}
};
static_assert(can_invoke<"f", only_int, int>());
static_assert(!can_invoke<"f", only_int>());              // 缺参数
static_assert(!can_invoke<"f", only_int, double, int>()); // 多参数

// -----------------------------------------------------------------------
// -----------------------------------------------------------------------
// -----------------------------------------------------------------------

// find_nonstatic_data_member 只遍历 nonstatic_data_members_of，即非静态数据成员。
// 而 my_class::print 是一个成员函数（而且是一个模板函数），不是数据成员。
static_assert(find_nonstatic_data_member(std::meta::access_context::current(), ^^my_class,
                                         "print") == std::meta::info{});
static_assert(find_member(std::meta::access_context::current(), ^^my_class, "print") !=
              std::meta::info{});

static_assert(find_nonstatic_data_member(std::meta::access_context::current(), ^^my_class,
                                         "mb") != std::meta::info{});
static_assert(find_member(std::meta::access_context::current(), ^^my_class, "mb") !=
              std::meta::info{});

// ============ 直接展开的链式判断（基于 my_class） ============

// ---- mb：非静态数据成员 ----
constexpr auto mb_r =
    find_member2(std::meta::access_context::current(), ^^my_class, "mb")
        .and_then([](std::meta::info m) -> std::optional<std::meta::info> {
            return std::meta::is_nonstatic_data_member(m) ? std::optional{m}
                                                          : std::nullopt;
        });
static_assert(mb_r.has_value());

static_assert(find_member_new(
    ^^my_class, "mb",
    [](std::meta::info m) -> bool {
      return std::meta::is_nonstatic_data_member(m);
    },
    std::meta::access_context::current()));
static_assert(find_member_new(^^my_class, "mb"));

// ---- print：成员函数，不是数据成员 → 卡在第 2 层 ----
constexpr auto print_r =
    find_member2(std::meta::access_context::current(), ^^my_class, "print")
        .and_then([](std::meta::info m) -> std::optional<std::meta::info> {
            return std::meta::is_nonstatic_data_member(m) ? std::optional{m}
                                                          : std::nullopt;
        });
static_assert(!print_r.has_value());

// ---- return_value：同上 → 卡在第 2 层 ----
constexpr auto rv_r =
    find_member2(std::meta::access_context::current(), ^^my_class, "return_value")
        .and_then([](std::meta::info m) -> std::optional<std::meta::info> {
            return std::meta::is_nonstatic_data_member(m) ? std::optional{m}
                                                          : std::nullopt;
        });
static_assert(!rv_r.has_value());

// ---- 名字不存在 → 卡在第 1 层 ----
constexpr auto none_r =
    find_member2(std::meta::access_context::current(), ^^my_class, "nope")
        .and_then([](std::meta::info m) -> std::optional<std::meta::info> {
            return std::meta::is_nonstatic_data_member(m) ? std::optional{m}
                                                          : std::nullopt;
        });
static_assert(!none_r.has_value());

int main()
try
{
    constexpr auto obj = my_class{};
    static_assert(call_by_name<"return_value">(obj, int{1}) == 1);

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