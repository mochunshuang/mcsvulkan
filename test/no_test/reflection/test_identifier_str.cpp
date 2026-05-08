#include <iostream>
#include <exception>
// 这组 API 用于获取反射实体的名称／显示字符串

#include <meta>
#include <string_view>
using namespace std::meta;
using namespace std::string_view_literals;

// ===== 辅助声明 =====
struct S
{
};
enum E
{
    e0,
    e1
};
enum class EC
{
    x
};
namespace ns
{
}
int globalVar;
void func();
template <typename T>
struct Temp
{
};
template <typename T>
void tfunc()
{
}
using IntAlias = int;
struct M
{
    int x;
    static int y;
    void f();
    int operator+(int);
};

struct
{
    int a;
} anonStruct;
union {
    int x;
    double y;
} anonUnion;
enum
{
    ae = 0
} anonEnumVar;

// ===== identifier_of / u8identifier_of =====
// 具名类型
static_assert(identifier_of(^^S) == "S");
static_assert(u8identifier_of(^^S) == u8"S");

static_assert(identifier_of(^^E) == "E");
static_assert(identifier_of(^^e0) == "e0");
static_assert(identifier_of(^^EC) == "EC");
static_assert(identifier_of(^^EC::x) == "x"); // 枚举器名
static_assert(u8identifier_of(^^EC::x) == u8"x");

// 命名空间
static_assert(identifier_of(^^ns) == "ns");
// ^^:: 无标识符，不能调用 identifier_of，用 has_identifier 保护：
consteval bool global_ns_has_identifier()
{
    if (has_identifier(^^::))
        return identifier_of(^^::).size() > 0;
    return false;
}
static_assert(!global_ns_has_identifier()); // 全局命名空间无名字

// 变量 / 函数
static_assert(identifier_of(^^globalVar) == "globalVar");
static_assert(identifier_of(^^func) == "func");

// 模板（模板名本身）
static_assert(identifier_of(^^Temp) == "Temp");
static_assert(identifier_of(^^tfunc) == "tfunc");

// 别名
static_assert(identifier_of(^^IntAlias) == "IntAlias");

// 成员
static_assert(identifier_of(^^M::x) == "x");
static_assert(identifier_of(^^M::y) == "y");
static_assert(identifier_of(^^M::f) == "f");
// 运算符（若编译器支持，期望返回 "operator+"，当前 GCC 可能暂不支持）
// static_assert(identifier_of(^^M::operator+) == "operator+");

// Lambda 变量名
constexpr auto lam = []() {
};
static_assert(identifier_of(^^lam) == "lam");

// NOTE: identifier_of 能拿到 定义的字符串。 lambda 匿名变量 都可以。都是变量，都能拿
static_assert(identifier_of(^^anonStruct) == "anonStruct");

// ===== 无名实体：不能调用 identifier_of，但可用 display_string_of =====
// 基本类型
static_assert(!has_identifier(^^int));
static_assert(!has_identifier(^^double));

// NOTE: 过滤
// 无法直接 static_assert 调用 identifier_of，下面用常值测试展示错误（期望编译失败）
// 若要测试，可写一个辅助 consteval 函数，仅当 has_identifier 为 true 才调用
template <info R>
consteval auto safe_identifier_of()
{
    if constexpr (has_identifier(R))
        return identifier_of(R);
    else
        return ""sv; // 默认值
}
static_assert(safe_identifier_of<^^int>() == ""sv);
static_assert(safe_identifier_of<^^S>() == "S");

// 匿名类型
static_assert(safe_identifier_of<^^decltype(anonStruct)>() == ""sv);
static_assert(safe_identifier_of<^^decltype(anonUnion)>() == ""sv);
static_assert(safe_identifier_of<^^decltype(anonEnumVar)>() == ""sv);
static_assert(safe_identifier_of<^^decltype(lam)>() == ""sv); // lambda 类型无名

// 组合类型
int arr[3];
int *ptr = arr;
static_assert(safe_identifier_of<^^decltype(arr)>() == ""sv);
static_assert(safe_identifier_of<^^decltype(ptr)>() == ""sv);
static_assert(safe_identifier_of<^^decltype(func)>() == ""sv); // 函数类型
static_assert(safe_identifier_of<^^Temp<int>>() == ""sv);      // 模板实例化

// ===== display_string_of / u8display_string_of =====
// 对所有 info 均应产生非空字符串（实现定义）
static_assert(display_string_of(^^int) == "int"sv);
static_assert(display_string_of(^^int) == "int");
static_assert(display_string_of(^^int) == std::string_view{"int"});
static_assert(display_string_of(^^int).size() > 0);
static_assert(display_string_of(^^S).size() > 0);
static_assert(display_string_of(^^e0).size() > 0);
static_assert(display_string_of(^^ns).size() > 0);
static_assert(display_string_of(^^func).size() > 0);
static_assert(display_string_of(^^decltype(anonStruct)).size() > 0);
static_assert(display_string_of(^^decltype(anonUnion)).size() > 0);
static_assert(display_string_of(^^decltype(lam)).size() > 0); // lambda 类型
static_assert(display_string_of(^^decltype(arr)).size() > 0);
static_assert(display_string_of(^^Temp<int>).size() > 0);

// UTF-8 版本同样
static_assert(u8display_string_of(^^int) == u8"int");
static_assert(u8display_string_of(^^int).size() > 0);
static_assert(u8display_string_of(^^S).size() > 0);
static_assert(u8display_string_of(^^decltype(anonStruct)).size() > 0);

// ===== 模板别名反射测试 =====
// 模拟你的 id_type 与别名模板
template <uint32_t N>
struct my_id_type; // 主模板
template <>
struct my_id_type<0>
{
    using type = S;
}; // 特化：对应类型 S

// 别名模板：与你的 id_type_t 完全一致
template <uint32_t id>
using my_id_type_t = typename my_id_type<id>::type;

// 1. 直接反射底层类型 -> 获得真实类型名
static_assert(display_string_of(^^typename my_id_type<0>::type) == "S");
static_assert(identifier_of(^^typename my_id_type<0>::type) == "S");

// 2. 反射别名模板实例 -> 获得别名展开后的名字（实现定义，不会是 S）
//    正是这一点导致 match_name 失败
consteval bool alias_has_name_S()
{
    return display_string_of(^^my_id_type_t<0>) == "S";
}
// 很可能不会是 true（编译器可能返回 "my_id_type_t<0>" 之类的字符串）
// static_assert(alias_has_name_S());   // 大概率失败

// 3. 安全获取别名的标识符（别名本身有名字，但实例化的标识符不一定）
static_assert(has_identifier(^^my_id_type_t<0>)
                  ? (identifier_of(^^my_id_type_t<0>).size() >= 0)
                  : true);
// 至少 display_string 总能拿到内容
static_assert(display_string_of(^^my_id_type_t<0>).size() > 0);

int main()
try
{
    {
        int v{};
        auto lam = [=]() {
            return v;
        };
        static_assert(identifier_of(^^lam) == "lam");
    }
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