#include <meta>

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