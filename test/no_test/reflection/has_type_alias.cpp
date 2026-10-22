#include <meta>
#include <string_view>

namespace meta = std::meta;

struct HasUsing
{
    using value_type = int;
    using ptr = int *;
    typedef double real; // typedef 也算 type alias
    struct Nested
    {
    }; // 不是 using
    int x; // 不是类型
    template <class T>
    using vec = T[3]; // 别名模板
};

struct NoUsing
{
    int x;
};

namespace ns
{
    using id_t = unsigned;
    namespace fs = std; // 命名空间别名
} // namespace ns

// ---------- 判定函数 ----------
consteval bool has_using_alias(meta::info scope, std::string_view name)
{
    constexpr auto ctx = meta::access_context::unchecked();
    for (auto m : meta::members_of(scope, ctx))
    {
        if (!meta::has_identifier(m) || meta::identifier_of(m) != name)
            continue;
        if (meta::is_type_alias(m) || meta::is_alias_template(m) ||
            meta::is_namespace_alias(m))
            return true;
    }
    return false;
}

// ---------- 类作用域 ----------
static_assert(has_using_alias(^^HasUsing, "value_type"));
static_assert(has_using_alias(^^HasUsing, "ptr"));
static_assert(has_using_alias(^^HasUsing, "real"));    // typedef
static_assert(has_using_alias(^^HasUsing, "vec"));     // 别名模板
static_assert(!has_using_alias(^^HasUsing, "Nested")); // class，不是 using
static_assert(!has_using_alias(^^HasUsing, "x"));      // 变量
static_assert(!has_using_alias(^^NoUsing, "value_type"));

// ---------- 命名空间作用域 ----------
static_assert(has_using_alias(^^ns, "id_t"));
static_assert(has_using_alias(^^ns, "fs")); // namespace alias

// ---------- 对"某个对象"：先 type_of 再查 ----------
struct Obj
{
    using tag = char;
} obj;
static_assert(has_using_alias(meta::type_of(^^obj), "tag"));

// ---------- 取出别名目标 ----------
consteval meta::info find_alias_target(meta::info scope, std::string_view name)
{
    for (auto m : meta::members_of(scope, meta::access_context::unchecked()))
        if (meta::has_identifier(m) && meta::identifier_of(m) == name &&
            meta::is_type_alias(m))
            return meta::dealias(m);
    return meta::info{};
}
constexpr meta::info vt = find_alias_target(^^HasUsing, "value_type");
static_assert(std::is_same_v<typename[:vt:], int>); // ✅

#include <iostream>
#include <exception>
// NOLINTBEGIN

int main()
try
{
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