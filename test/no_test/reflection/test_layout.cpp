#include <meta>
#include <iostream>
#include <cstddef>
using namespace std::meta;

// ============================================================
// 辅助类定义（包含位域、基类、虚拟继承等）
// ============================================================

struct Base
{
    int b;
    char c;
};

struct Derived : public Base
{
    double d;
    unsigned int flag : 3; // 位域
    unsigned int mode : 5;
};

struct VirtualBase
{
    int v;
};

struct DerivedVirtual : virtual VirtualBase
{
    int x;
};

// 用于测试空基类大小
struct EmptyBase
{
};
struct WithEmpty : EmptyBase
{
    int data;
};

// 引用类型（用于 size_of 特殊规则）
struct RefTest
{
    int &ref; // 引用类型成员，size_of 返回指针大小
};

// ============================================================
// 测试 1：offset_of
// ============================================================
consteval bool test_offset_of()
{
    // 1.1 普通非静态数据成员偏移
    // offset_of 返回 member_offset{bytes, bits}
    info d_member = info{};
    for (info mem : nonstatic_data_members_of(^^Derived, access_context::unchecked()))
    {
        if (has_identifier(mem) && identifier_of(mem) == "d")
        {
            d_member = mem;
            break;
        }
    }
    if (d_member == info{})
        return false;
    member_offset off_d = offset_of(d_member);
    // d 是 double，对齐到 8，在 Base 后（b 4 + c 1 + 填充 3 = 8），所以 bytes 应为 8（假设 64 位）
    // 不硬编码具体值，只校验 total_bits() 是正数且偏移不是 0
    if (off_d.total_bits() == 0)
        return false;

    // 1.2 基类子对象偏移（非虚基类）
    // 获取 Derived 的基类关系
    info base_rel = info{};
    for (info b : bases_of(^^Derived, access_context::unchecked()))
    {
        // 基类关系反射，其 identifier 可能为空，用 type_of 来判断
        if (type_of(b) == ^^Base)
        {
            base_rel = b;
            break;
        }
    }
    if (base_rel == info{})
        return false;
    member_offset off_base = offset_of(base_rel);
    // 基类子对象偏移应从 0 开始（在派生类对象起始处）
    if (off_base.bytes != 0 || off_base.bits != 0)
        return false; // 期望为 0

    // 1.3 位域偏移
    info flag_member = info{};
    for (info mem : nonstatic_data_members_of(^^Derived, access_context::unchecked()))
    {
        if (has_identifier(mem) && identifier_of(mem) == "flag")
        {
            flag_member = mem;
            break;
        }
    }
    if (flag_member == info{})
        return false;
    member_offset off_flag = offset_of(flag_member);
    // 位域可能有字节内偏移（bits 不为 0），只检查 total_bits 合理
    if (off_flag.total_bits() < 0)
        return false;

    // 1.4 虚拟基类：offset_of 不应适用（根据标准，要求非虚基类或派生类不是抽象类）
    // 这里不做测试，避免编译错误，因为可能触发“Constant When”失败导致异常。
    return true;
}
static_assert(test_offset_of());

// ============================================================
// 测试 2：size_of
// ============================================================
consteval bool test_size_of()
{
    // 2.1 普通类型
    if (size_of(^^int) != sizeof(int))
        return false;
    if (size_of(^^double) != sizeof(double))
        return false;

    // 2.2 非静态数据成员：返回类型大小
    info b_member = info{};
    for (info mem : nonstatic_data_members_of(^^Base, access_context::unchecked()))
    {
        if (has_identifier(mem) && identifier_of(mem) == "b")
        {
            b_member = mem;
            break;
        }
    }
    if (b_member == info{})
        return false;
    if (size_of(b_member) != sizeof(int))
        return false;

    // 2.3 引用类型成员：size_of 应返回指针大小（见标准 Note 1）
    info ref_member = info{};
    for (info mem : nonstatic_data_members_of(^^RefTest, access_context::unchecked()))
    {
        if (has_identifier(mem) && identifier_of(mem) == "ref")
        {
            ref_member = mem;
            break;
        }
    }
    if (ref_member == info{})
        return false;
    // 引用类型 size_of 应与指针相同（实现定义，但通常等于 sizeof(void*)）
    if (size_of(ref_member) != sizeof(void *))
        return false; // 或 sizeof(int*) 等

    // 2.4 直接基类关系：size_of 应返回基类子对象的大小（>0，即使空基类）
    info empty_base_rel = info{};
    for (info b : bases_of(^^WithEmpty, access_context::unchecked()))
    {
        if (type_of(b) == ^^EmptyBase)
        {
            empty_base_rel = b;
            break;
        }
    }
    if (empty_base_rel == info{})
        return false;
    // 空基类通常占用 1 字节（除非 EBO），size_of 应 >0
    if (size_of(empty_base_rel) <= 0)
        return false;

    // 2.5 变量/对象：size_of(^^some_variable) 返回其类型大小
    constexpr int some_var = 42;
    if (size_of(^^some_var) != sizeof(int))
        return false;

    return true;
}
static_assert(test_size_of());

// ============================================================
// 测试 3：alignment_of
// ============================================================
consteval bool test_alignment_of()
{
    // 3.1 类型对齐
    if (alignment_of(^^double) != alignof(double))
        return false;

    // 3.2 数据成员对齐
    info d_member = info{};
    for (info mem : nonstatic_data_members_of(^^Derived, access_context::unchecked()))
    {
        if (has_identifier(mem) && identifier_of(mem) == "d")
        {
            d_member = mem;
            break;
        }
    }
    if (d_member == info{})
        return false;
    // 成员对齐应等于其类型的对齐要求
    if (alignment_of(d_member) != alignof(double))
        return false;

    // 3.3 基类关系对齐
    info base_rel = info{};
    for (info b : bases_of(^^Derived, access_context::unchecked()))
    {
        if (type_of(b) == ^^Base)
        {
            base_rel = b;
            break;
        }
    }
    if (base_rel == info{})
        return false;
    // 基类子对象对齐等于基类类型对齐
    if (alignment_of(base_rel) != alignof(Base))
        return false;

    // 3.4 变量对齐
    constexpr double some_var = 3.14;
    if (alignment_of(^^some_var) != alignof(double))
        return false;

    return true;
}
static_assert(test_alignment_of());

// ============================================================
// 测试 4：bit_size_of
// ============================================================
consteval bool test_bit_size_of()
{
    // 4.1 非位域成员：bit_size_of = CHAR_BIT * size_of
    info b_member = info{};
    for (info mem : nonstatic_data_members_of(^^Base, access_context::unchecked()))
    {
        if (has_identifier(mem) && identifier_of(mem) == "b")
        {
            b_member = mem;
            break;
        }
    }
    if (b_member == info{})
        return false;
    if (bit_size_of(b_member) != CHAR_BIT * sizeof(int))
        return false;

    // 4.2 位域成员：返回其宽度
    info flag_member = info{};
    for (info mem : nonstatic_data_members_of(^^Derived, access_context::unchecked()))
    {
        if (has_identifier(mem) && identifier_of(mem) == "flag")
        {
            flag_member = mem;
            break;
        }
    }
    if (flag_member == info{})
        return false;
    if (bit_size_of(flag_member) != 3)
        return false; // flag 宽度 3

    info mode_member = info{};
    for (info mem : nonstatic_data_members_of(^^Derived, access_context::unchecked()))
    {
        if (has_identifier(mem) && identifier_of(mem) == "mode")
        {
            mode_member = mem;
            break;
        }
    }
    if (mode_member == info{})
        return false;
    if (bit_size_of(mode_member) != 5)
        return false;

    // 4.3 类型：bit_size_of 应返回 CHAR_BIT * size_of
    if (bit_size_of(^^int) != CHAR_BIT * sizeof(int))
        return false;

    // 4.4 基类关系：bit_size_of 应等于 CHAR_BIT * size_of
    info base_rel = info{};
    for (info b : bases_of(^^Derived, access_context::unchecked()))
    {
        if (type_of(b) == ^^Base)
        {
            base_rel = b;
            break;
        }
    }
    if (base_rel == info{})
        return false;
    if (bit_size_of(base_rel) != CHAR_BIT * size_of(base_rel))
        return false;

    return true;
}
static_assert(test_bit_size_of());

// ============================================================
// 入口
// ============================================================
int main()
{
    std::cout << "All layout query tests passed.\n";
    return 0;
}