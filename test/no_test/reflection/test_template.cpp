#include <meta>
#include <iostream>
#include <print>
using namespace std::meta;

// ===== 全局模板 =====
template <typename T>
struct MyClass
{
}; // 类模板
template <typename T>
void myFunc()
{
} // 函数模板
template <typename T>
T myVar = T{}; // 变量模板
template <typename T>
using MyAlias = T; // 别名模板
template <typename T>
concept MyConcept = true; // 概念

// 全局字面量运算符模板（正确的形式）
template <char... Chars>
constexpr auto operator""_lit()
{
    return sizeof...(Chars);
}

// ===== 类，包含成员模板 =====
struct S
{
    template <typename T>
    void f()
    {
    } // 函数模板
    template <typename T>
    operator T()
    {
        return T{};
    } // 转换函数模板
    template <typename T>
    void operator+(T)
    {
    } // 运算符函数模板
    template <typename T>
    S(T)
    {
    } // 构造函数模板
};

// 辅助函数：从 S 的成员中提取指定模板反射
consteval info get_member_template(info class_info, bool (*pred)(info))
{
    for (info mem : members_of(class_info, access_context::current()))
        if (pred(mem))
            return mem;
    return {};
}

consteval info get_S_func_template()
{
    return get_member_template(^^S, [](info m) {
      return has_identifier(m) && identifier_of(m) == "f" && is_function_template(m);
    });
}
consteval info get_S_conv_template()
{
    return get_member_template(^^S, [](info m) {
      return is_conversion_function_template(m);
    });
}
consteval info get_S_op_template()
{
    return get_member_template(^^S, [](info m) {
      return is_operator_function_template(m);
    });
}
consteval info get_S_ctor_template()
{
    return get_member_template(^^S, [](info m) {
      return is_constructor_template(m);
    });
}

// ===== 静态断言 =====

// 全局模板的 is_template
static_assert(is_template(^^MyClass));
static_assert(is_template(^^myFunc));
static_assert(is_template(^^myVar));
static_assert(is_template(^^MyAlias));
static_assert(is_template(^^MyConcept));
static_assert(is_template(^^operator""_lit));
static_assert(!is_template(^^MyClass<int>));

// 细分类型
static_assert(is_class_template(^^MyClass));
static_assert(is_function_template(^^myFunc));
static_assert(is_variable_template(^^myVar));
static_assert(is_alias_template(^^MyAlias));
static_assert(is_concept(^^MyConcept));
static_assert(is_literal_operator_template(^^operator""_lit));

// 成员模板
constexpr info S_func_tmpl = get_S_func_template();
constexpr info S_conv_tmpl = get_S_conv_template();
constexpr info S_op_tmpl = get_S_op_template();
constexpr info S_ctor_tmpl = get_S_ctor_template();

// 这些 info 有效（函数模板有标识符）
static_assert(has_identifier(S_func_tmpl));
static_assert(!has_identifier(S_op_tmpl));
// 转换函数模板可能无标识符，不强制 has_identifier

static_assert(is_template(S_func_tmpl));
static_assert(is_template(S_conv_tmpl));
static_assert(is_template(S_op_tmpl));
static_assert(is_template(S_ctor_tmpl));

static_assert(is_function_template(S_func_tmpl));
static_assert(is_function_template(S_ctor_tmpl)); // 构造函数模板也是函数模板
static_assert(is_constructor_template(S_ctor_tmpl));
static_assert(is_conversion_function_template(S_conv_tmpl));
static_assert(is_operator_function_template(S_op_tmpl));

// 一个简单的函数模板
template <typename T>
void greet(T val, const char *msg)
{
    std::cout << "Greet: " << val << ", " << msg << '\n';
}

//substitute 需要产生一个外部可链接的实体信息，而 static 函数模板的特化仍然具有内部链接，反射元信息可能拒绝生成。.所以放在
struct fun
{
    template <typename... T>
    static constexpr auto print_count(T &...args)
    {
        static_assert((std::is_lvalue_reference_v<decltype(args)> && ...));
        std::println("{}", sizeof...(args));
    }
};
int main()
{
    using namespace std::meta;

    // 1. 在编译期用 substitute 得到 greet<int> 的信息
    constexpr info instantiated = substitute(^^greet, {
                                                          ^^int});

    // 2. 用 [:...:] 将信息“拼”回代码，变成一个可调用实体
    //    然后用 auto&&...args 完美转发调用
    auto callable = [](auto &&...args) {
        return [:instantiated:](std::forward<decltype(args)>(args)...);
    };

    // 3. 调用
    callable(100, "hello world"); // 输出 Greet: 100, hello world

    {
        struct A
        {
        };
        struct B
        {
        };
        A a;
        B b;
        auto printable = [](A &a, B &b) {
            constexpr info print_count_impl =
                substitute(^^fun::print_count, {
                                                   ^^A, ^^B});
            return [:print_count_impl:](a, b);
        };
        using P = decltype(printable);
        struct task
        {
            P p;
        };
        task{}.p(a, b);
        {
            auto printable = [](auto &&...args) {
                constexpr info print_info = substitute(
                    ^^fun::print_count, {
                                            ^^std::remove_cvref_t<decltype(args)>...});
                [:print_info:](std::forward<decltype(args)>(args)...);
            };
            printable(a, b);
            using P = decltype(printable);
            struct task
            {
                P p;
            };
            task{}.p(a, b);
        }
        {
            auto printable = [](auto &&...args) {
                constexpr info print_info =
                    substitute(^^fun::print_count,
                               {
                                   std::meta::remove_cvref(^^decltype(args))...});
                [:print_info:](std::forward<decltype(args)>(args)...);
            };
            printable(a, b);
        }
        {
            auto printable = [](auto &&...args) {
                constexpr info print_info =
                    substitute(^^fun::print_count, {
                                                       ^^decltype(args)...});
                [:print_info:](std::forward<decltype(args)>(args)...);
            };
            printable(a, b);
            using P = decltype(printable);
            struct task
            {
                P p;
            };
            task{}.p(a, b);
        }
        {
            auto printable = [](auto &&...args) {
                static_assert((std::is_lvalue_reference_v<decltype(args)> && ...));
                std::println("{}", sizeof...(args));
            };
            printable(a, b);
            using P = decltype(printable);
            struct task
            {
                P p;
            };
            task{}.p(a, b);

            struct storage;
            consteval
            {
                std::meta::define_aggregate(
                    ^^storage, {
                                   std::meta::data_member_spec(^^P, {
                                                                        .name = "p"})});
            }
            int c;
            storage{}.p(a, b, c);
        }
    }
    // NOTE:我要干嘛？？？？ 肯定的面向对象啊。问题是 面向集合的 使用 static 非常合理啊

    std::cout << "done\n";
}