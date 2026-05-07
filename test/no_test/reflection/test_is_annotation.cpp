#include <meta>
using namespace std::meta;

// ========== is_bit_field & is_enumerator ==========
struct BF
{
    int x : 3;
    int y : 5;
    int z;
};
static_assert(is_bit_field(^^BF::x));
static_assert(is_bit_field(^^BF::y));
static_assert(!is_bit_field(^^BF::z));
static_assert(!is_bit_field(^^BF));
static_assert(!is_bit_field(^^int));

enum OldEnum
{
    e0,
    e1
};
enum class ScopedEnum
{
    a,
    b
};
static_assert(is_enumerator(^^e0));
static_assert(is_enumerator(^^ScopedEnum::a));
static_assert(!is_enumerator(^^OldEnum));
static_assert(!is_enumerator(^^int));
static_assert(!is_enumerator(^^BF::x));

// ========== is_annotation ==========
// 注解在不同实体上
struct[[= "class annotation"]] AnnotatedClass
{
};

[[= "global variable"]] int annotatedVar = 0;

[[= "global function"]] void annotatedFunc() {}

enum[[= "enum annotation"]] AnnotatedEnum
{
    e
};

// 成员注解
struct AnnotatedMembers
{
    int x[[= 42]];
    [[= "member function"]] void f();
};

// 测试类注解
consteval bool test_class_annotation()
{
    for (info ann : annotations_of(^^AnnotatedClass))
    {
        if (!is_annotation(ann))
            return false;
    }
    return !annotations_of(^^AnnotatedClass).empty() && !is_annotation(^^AnnotatedClass);
}

// 测试全局变量注解
consteval bool test_global_var_annotation()
{
    for (info ann : annotations_of(^^annotatedVar))
    {
        if (!is_annotation(ann))
            return false;
    }
    return !annotations_of(^^annotatedVar).empty() && !is_annotation(^^annotatedVar);
}

// 测试全局函数注解
consteval bool test_func_annotation()
{
    for (info ann : annotations_of(^^annotatedFunc))
    {
        if (!is_annotation(ann))
            return false;
    }
    return !annotations_of(^^annotatedFunc).empty() && !is_annotation(^^annotatedFunc);
}

// 测试枚举注解
consteval bool test_enum_annotation()
{
    for (info ann : annotations_of(^^AnnotatedEnum))
    {
        if (!is_annotation(ann))
            return false;
    }
    return !annotations_of(^^AnnotatedEnum).empty() && !is_annotation(^^AnnotatedEnum);
}

// 测试成员注解
consteval bool test_member_annotations()
{
    // 数据成员注解
    for (info ann : annotations_of(^^AnnotatedMembers::x))
    {
        if (!is_annotation(ann))
            return false;
    }
    if (annotations_of(^^AnnotatedMembers::x).empty())
        return false;
    if (is_annotation(^^AnnotatedMembers::x))
        return false;

    // 成员函数注解
    for (info ann : annotations_of(^^AnnotatedMembers::f))
    {
        if (!is_annotation(ann))
            return false;
    }
    return !annotations_of(^^AnnotatedMembers::f).empty() &&
           !is_annotation(^^AnnotatedMembers::f);
}

// 无注解实体
consteval bool test_no_annotation()
{
    return annotations_of(^^int).empty() && annotations_of(^^BF::x).empty() &&
           !is_annotation(^^int) && !is_annotation(^^BF::x);
}

// 汇总断言
static_assert(test_class_annotation());
static_assert(test_global_var_annotation());
static_assert(test_func_annotation());
static_assert(test_enum_annotation());
static_assert(test_member_annotations());
static_assert(test_no_annotation());

int main()
{
    return 0;
}