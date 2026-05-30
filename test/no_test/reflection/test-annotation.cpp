#include <assert.h>
#include <cstddef>
#include <meta>
#include <type_traits>

namespace meta = std::meta;
using namespace std::meta;

// ===== 辅助类型与注解实体 =====
struct[[= "class note"]] AnnotatedClass
{
};

[[= 42]] int annotatedGlobal = 0;

[[= "func annot"]] void annotatedFunction() {}

enum[[= "enum note"]] AnnotatedEnum
{
    e
};

struct Members
{
    int x[[= 100]];
    [[= "member func"]] void f() {}
};

// 多注解测试：同实体多种类型注解
struct MultiAnnot
{
    int a[[= 3.14]][[= "pi"]]; // double 和 const char*
};

// ===== 测试 is_annotation() =====
consteval bool test_is_annotation_class()
{
    for (info ann : meta::annotations_of(^^AnnotatedClass))
        if (!meta::is_annotation(ann))
            return false;
    return !meta::annotations_of(^^AnnotatedClass).empty() &&
           !meta::is_annotation(^^AnnotatedClass);
}
static_assert(test_is_annotation_class());

consteval bool test_is_annotation_global_var()
{
    for (info ann : meta::annotations_of(^^annotatedGlobal))
        if (!meta::is_annotation(ann))
            return false;
    return !meta::annotations_of(^^annotatedGlobal).empty() &&
           !meta::is_annotation(^^annotatedGlobal);
}
static_assert(test_is_annotation_global_var());

consteval bool test_is_annotation_func()
{
    for (info ann : meta::annotations_of(^^annotatedFunction))
        if (!meta::is_annotation(ann))
            return false;
    return !meta::annotations_of(^^annotatedFunction).empty() &&
           !meta::is_annotation(^^annotatedFunction);
}
static_assert(test_is_annotation_func());

consteval bool test_is_annotation_enum()
{
    for (info ann : meta::annotations_of(^^AnnotatedEnum))
        if (!meta::is_annotation(ann))
            return false;
    return !meta::annotations_of(^^AnnotatedEnum).empty() &&
           !meta::is_annotation(^^AnnotatedEnum);
}
static_assert(test_is_annotation_enum());

consteval bool test_is_annotation_member()
{
    // 数据成员
    for (info ann : meta::annotations_of(^^Members::x))
        if (!meta::is_annotation(ann))
            return false;
    if (meta::annotations_of(^^Members::x).empty())
        return false;
    if (meta::is_annotation(^^Members::x))
        return false;
    // 成员函数
    for (info ann : meta::annotations_of(^^Members::f))
        if (!meta::is_annotation(ann))
            return false;
    return !meta::annotations_of(^^Members::f).empty() &&
           !meta::is_annotation(^^Members::f);
}
static_assert(test_is_annotation_member());

consteval bool test_no_annotation()
{
    return meta::annotations_of(^^int).empty() && !meta::is_annotation(^^int);
}
static_assert(test_no_annotation());

// ===== 测试 annotations_of_with_type() =====
consteval bool test_annotations_of_with_type_filtering()
{
    auto all = meta::annotations_of(^^MultiAnnot::a);
    // 应有两个注解
    if (all.size() != 2)
        return false;

    // 按 double 类型过滤（注解 3.14）
    auto double_ann = meta::annotations_of_with_type(^^MultiAnnot::a, ^^double);
    if (double_ann.size() != 1)
        return false;
    if (!meta::is_annotation(double_ann[0]))
        return false;

    // 按 const char* 类型过滤（注解 "pi"）
    auto cstr_ann = meta::annotations_of_with_type(^^MultiAnnot::a, ^^const char *);
    if (cstr_ann.size() != 1)
        return false;
    if (!meta::is_annotation(cstr_ann[0]))
        return false;

    // 错误类型匹配
    auto int_ann = meta::annotations_of_with_type(^^MultiAnnot::a, ^^int);
    return int_ann.empty();
}
static_assert(test_annotations_of_with_type_filtering());

// 测试没有注解的实体按类型查询
consteval bool test_no_annotation_filter()
{
    auto res = meta::annotations_of_with_type(^^int, ^^int);
    return res.empty();
}
static_assert(test_no_annotation_filter());

// --------------------------------------------------
template <auto V>
struct Derive
{
    constexpr bool operator==(const Derive &) const = default;
};
template <auto V>
inline constexpr Derive<V> derive;

struct Debug_t
{
};
constexpr Debug_t Debug;
struct[[= derive<Debug>]] Point
{
    int x, y;
};

consteval bool test_extract_derive()
{
    constexpr auto d = derive<Debug>;
    auto info_d = std::meta::reflect_constant(d);
    auto extracted = std::meta::extract<Derive<Debug>>(info_d);
    return extracted == d;
}
static_assert(test_extract_derive());

// 检查是否存在 derive<Debug> 注解
consteval bool has_any_annotation(std::meta::info r)
{
    return !std::meta::annotations_of(r).empty();
}
static_assert(has_any_annotation(^^Point));

template <size_t N>
consteval bool has_cout_annotation(std::meta::info r)
{
    auto all = meta::annotations_of(r);
    return all.size() == N;
}
static_assert(has_cout_annotation<1>(^^Point));
static_assert(has_cout_annotation<2>(^^MultiAnnot::a));

template <std::meta::info r>
using get_type =
    std::type_identity_t<typename[:std::meta::is_type(r) ? r : std::meta::type_of(r):]>;

static_assert(std::is_same_v<get_type<^^int>, int>);
// decltype()
// equeal_annotation
consteval
{
    constexpr meta::info target = meta::annotations_of(^^Point)[0];
    using test_type = decltype(derive<Debug>);
    static_assert(meta::extract<test_type>(target) == derive<Debug>);
    static_assert(meta::annotations_of(^^Point).size() == 1);

    constexpr int a = 0;
    constexpr int b = 0;
    constexpr int c = 1;

    const int d = 1;

    static_assert(^^a != ^^b);
    static_assert(constant_of(^^a) == constant_of(^^b));
    static_assert(constant_of(^^a) != constant_of(^^c));

    static_assert(std::is_same_v<get_type<^^a>, get_type<^^b>>);
    static_assert(std::is_same_v<get_type<^^a>, get_type<^^c>>);

    static_assert(std::is_same_v<get_type<^^a>, get_type<^^d>>);

    struct type
    {
    };
    type e;
    const type f;
    static_assert(not std::is_same_v<get_type<^^e>, get_type<^^f>>);
    static_assert(std::is_same_v<std::remove_cvref_t<get_type<^^e>>,
                                 std::remove_cvref_t<get_type<^^f>>>);

    //NOTE: 反射限制很大 'reflection does not represent a type'
    // static_assert(
    //     not std::is_same_v<get_type<std::meta::remove_cvref(^^e)>, get_type<^^f>>);

    //NOTE: 语法错误
    // constexpr auto t = std::meta::remove_cvref(^^e);
    // constexpr auto t = std::meta::remove_cvref(std::meta::reflect_constant(e));
    // constexpr auto t = std::meta::remove_cvref(std::meta::reflect_constant(type{}));

    // constexpr auto t = std::meta::remove_cvref(std::meta::reflect_constant(1));

    constexpr auto ce = std::meta::reflect_constant(e);
    static_assert(not std::meta::is_type(ce));
    constexpr auto t = std::meta::remove_cvref(std::meta::type_of(ce));
}

template <auto an>
consteval bool has_annotations(std::meta::info r)
{
    // error: 'r' is not a constant expression
    // constexpr auto big = std::define_static_array(meta::annotations_of(r));
    std::vector<info> big = meta::annotations_of(r);
    constexpr auto type =
        std::meta::is_type(big[0]) ? big[0] : std::meta::type_of(big[0]);

    return std::is_same_v<std::remove_cvref_t<typename[:type:]>,
                          std::remove_cvref_t<decltype(an)>>;

    // return std::is_same_v<std::remove_cvref_t<get_type<meta::reflect_constant(big[0])>>,
    //                       std::remove_cvref_t<get_type<an...[0]>>>;
    // return meta::extract<decltype(an...[0])>(big[0]) == an...[0];

    // template for (const auto &x : {an...})
    // {
    //     bool found = false;
    //     for (std::size_t i = 0; i < big.size(); ++i)
    //     {
    //         if (std::is_same_v<
    //                 std::remove_cvref_t<get_type<^^x>>,
    //                 std::remove_cvref_t<get_type<big[meta::reflect_constant(i)]>>> &&
    //             meta::extract<decltype(x)>(big[i]) == x)
    //         {
    //             found = true;
    //             break;
    //         }
    //     }
    //     if (!found)
    //         return false;
    // }
    // return true;
}
// static_assert(has_annotations<derive<Debug>>(^^Point));

#include <iostream>
#include <exception>

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
}