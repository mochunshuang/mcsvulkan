#include <iostream>
#include <meta>
#include <cstddef>

namespace meta = std::meta;

// 辅助类型
struct TestClass
{
    int mem;
};
enum TestEnum
{
    A,
    B
};
enum class TestEnumClass : unsigned char
{
    X,
    Y
};
union TestUnion {
    int a;
    double b;
};

// 成员指针别名
using MemObjPtr = int TestClass::*;
using MemFuncPtr = void (TestClass::*)(int);

// 引用别名
using LRef = int &;
using RRef = int &&;

// 函数指针别名
using FuncPtr = void (*)(int);

// ─────────────────────────────────────────────────────────
// [meta.unary.comp] 复合类型类别
// ─────────────────────────────────────────────────────────

// 1. is_reference_type (左值引用 或 右值引用)
static_assert(meta::is_reference_type(^^LRef));
static_assert(meta::is_reference_type(^^RRef));
static_assert(meta::is_reference_type(^^const int &));
static_assert(!meta::is_reference_type(^^int));
static_assert(!meta::is_reference_type(^^int *));

// 2. is_arithmetic_type (整型 或 浮点类型)
static_assert(meta::is_arithmetic_type(^^int));
static_assert(meta::is_arithmetic_type(^^float));
static_assert(meta::is_arithmetic_type(^^bool));
static_assert(meta::is_arithmetic_type(^^long double));
static_assert(!meta::is_arithmetic_type(^^int *));
static_assert(!meta::is_arithmetic_type(^^TestEnum)); // 枚举不算
static_assert(!meta::is_arithmetic_type(^^std::nullptr_t));

// 3. is_fundamental_type (算术类型、void、nullptr_t)
static_assert(meta::is_fundamental_type(^^int));
static_assert(meta::is_fundamental_type(^^double));
static_assert(meta::is_fundamental_type(^^void));
static_assert(meta::is_fundamental_type(^^std::nullptr_t));
static_assert(!meta::is_fundamental_type(^^int *)); // 指针不是基础类型
static_assert(!meta::is_fundamental_type(^^TestEnum));

// 4. is_object_type (数组、类、联合、标量类型? 实际上不包含函数、引用、void)
static_assert(meta::is_object_type(^^int));
static_assert(meta::is_object_type(^^TestClass));
static_assert(meta::is_object_type(^^TestUnion));
static_assert(meta::is_object_type(^^int[5]));
static_assert(meta::is_object_type(^^int *));
static_assert(!meta::is_object_type(^^void));   // void 不是对象
static_assert(!meta::is_object_type(^^int &));  // 引用不是对象
static_assert(!meta::is_object_type(^^void())); // 函数不是对象

// 5. is_scalar_type (算术、枚举、指针、成员指针、nullptr_t)
static_assert(meta::is_scalar_type(^^int));
static_assert(meta::is_scalar_type(^^double));
static_assert(meta::is_scalar_type(^^TestEnum));
static_assert(meta::is_scalar_type(^^int *));
static_assert(meta::is_scalar_type(^^MemObjPtr));
static_assert(meta::is_scalar_type(^^MemFuncPtr));
static_assert(meta::is_scalar_type(^^std::nullptr_t));
static_assert(!meta::is_scalar_type(^^int &));     // 引用不是标量
static_assert(!meta::is_scalar_type(^^TestClass)); // 类类型不是标量
static_assert(!meta::is_scalar_type(^^int[5]));

// 6. is_compound_type (非基础类型：数组、函数、指针、引用、类、联合、枚举、成员指针等)
static_assert(meta::is_compound_type(^^int *));
static_assert(meta::is_compound_type(^^int &));
static_assert(meta::is_compound_type(^^int[5]));
static_assert(meta::is_compound_type(^^void()));
static_assert(meta::is_compound_type(^^TestClass));
static_assert(meta::is_compound_type(^^TestUnion));
static_assert(meta::is_compound_type(^^TestEnum));
static_assert(meta::is_compound_type(^^MemObjPtr));
static_assert(!meta::is_compound_type(^^int)); // 基础类型
static_assert(!meta::is_compound_type(^^double));
static_assert(
    !meta::is_compound_type(^^void)); // 虽然 void 复合？提案中 compound 通常不包括 void

// 7. is_member_pointer_type (成员对象指针 或 成员函数指针)
static_assert(meta::is_member_pointer_type(^^MemObjPtr));
static_assert(meta::is_member_pointer_type(^^MemFuncPtr));
static_assert(!meta::is_member_pointer_type(^^int *));
static_assert(!meta::is_member_pointer_type(^^FuncPtr));
static_assert(!meta::is_member_pointer_type(^^void()));

// ─────────────────────────────────────────────────────────
int main()
{
    std::cout << "Testing composite type categories\n";

    return 0;
}