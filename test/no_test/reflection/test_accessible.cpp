#include <meta>
#include <iostream>
#include <string_view>
using namespace std::meta;

// ============================================================
// 辅助类，提供不同的访问控制环境
// ============================================================

class PrivateMembers
{
    int private_x;             // 私有非静态数据成员
    static int private_static; // 私有静态数据成员
  public:
    int public_y;             // 公有非静态数据成员
    static int public_static; // 公有静态数据成员

    // 类内静态成员函数 – 可以用来获取一个有权访问本类私有成员的 access_context
    // 当在该函数内部调用 current() 时，scope 为该函数，而该函数是类的成员，因此具备访问权限
    static consteval access_context member_ctx()
    {
        return access_context::current();
    }
};

// 基类
struct Base
{
    int base_val;
};

// 私有继承：基类子对象对外部不可访问
class DerivedPrivate : private Base
{
  public:
    DerivedPrivate(int v) : Base{v} {}
};

// 公有继承：基类子对象对外部可访问
class DerivedPublic : public Base
{
  public:
    DerivedPublic(int v) : Base{v} {}
};

// 空类（用于边界测试）
struct Empty
{
};

// ============================================================
// 辅助工具：反射访问成员（绕过访问控制，仅用于获取反射）
// ============================================================

// 通过索引获取非静态数据成员反射（依赖 nonstatic_data_members_of + unchecked）
consteval info get_nonstatic_data_member(info type, int idx)
{
    auto members = nonstatic_data_members_of(type, access_context::unchecked());
    int i = 0;
    for (info mem : members)
    {
        if (i == idx)
            return mem;
        ++i;
    }
    return info{};
}

// 通过名字查找任意成员反射（包括静态成员），使用 members_of + unchecked
consteval info get_member_by_name(info type, std::string_view name)
{
    auto members = members_of(type, access_context::unchecked());
    for (info mem : members)
    {
        if (has_identifier(mem) && identifier_of(mem) == name)
            return mem;
    }
    return info{};
}

// ============================================================
// 测试 1：is_accessible
// ============================================================
consteval bool test_is_accessible()
{
    // 1.1 私有非静态成员在外部（当前函数）上下文中不可访问
    {
        info private_x = get_nonstatic_data_member(^^PrivateMembers, 0);
        if (is_accessible(private_x, access_context::current()))
            return false; // 期望：false（不可访问）
    }

    // 1.2 同样的私有成员在类内上下文（member_ctx）中应当可访问
    {
        info private_x = get_nonstatic_data_member(^^PrivateMembers, 0);
        if (!is_accessible(private_x, PrivateMembers::member_ctx()))
            return false; // 期望：true
    }

    // 1.3 公有非静态成员在任何上下文中均可访问
    {
        info public_y = get_nonstatic_data_member(^^PrivateMembers, 1);
        if (!is_accessible(public_y, access_context::current()))
            return false; // 期望：true
    }

    // 1.4 私有静态成员 – 外部不可访问，类内可访问
    {
        info priv_static = get_member_by_name(^^PrivateMembers, "private_static");
        if (priv_static == info{})
            return false; // 必须找到该成员
        // 外部
        if (is_accessible(priv_static, access_context::current()))
            return false; // 期望：false
        // 类内
        if (!is_accessible(priv_static, PrivateMembers::member_ctx()))
            return false; // 期望：true
    }

    // 1.5 使用 unchecked() 上下文，所有成员均应视为可访问
    {
        info private_x = get_nonstatic_data_member(^^PrivateMembers, 0);
        if (!is_accessible(private_x, access_context::unchecked()))
            return false; // 期望：true
    }

    // 1.6 私有基类在外部不可访问
    {
        auto bases = bases_of(^^DerivedPrivate, access_context::unchecked());
        info base_ref = info{};
        for (info b : bases)
        {
            base_ref = b;
            break;
        } // 取出唯一的基类
        if (base_ref == info{})
            return false;
        if (is_accessible(base_ref, access_context::current()))
            return false; // 期望：false（私有基类对外不可访问）
    }

    // 1.7 公有基类在任何上下文中可访问
    {
        auto bases = bases_of(^^DerivedPublic, access_context::unchecked());
        info base_ref = info{};
        for (info b : bases)
        {
            base_ref = b;
            break;
        }
        if (base_ref == info{})
            return false;
        if (!is_accessible(base_ref, access_context::current()))
            return false; // 期望：true
    }

    return true;
}
static_assert(test_is_accessible());

// ============================================================
// 测试 2：has_inaccessible_nonstatic_data_members
// ============================================================
consteval bool test_has_inaccessible_nonstatic_data_members()
{
    // 2.1 PrivateMembers 在外部上下文中存在不可访问的非静态数据成员
    if (!has_inaccessible_nonstatic_data_members(^^PrivateMembers,
                                                 access_context::current()))
        return false; // 期望：true

    // 2.2 在类内上下文中，所有非静态数据成员均可访问，故应返回 false
    if (has_inaccessible_nonstatic_data_members(^^PrivateMembers,
                                                PrivateMembers::member_ctx()))
        return false; // 期望：false

    // 2.3 全公开成员的类，一定没有不可访问成员
    struct AllPublic
    {
        int a;
        double b;
    };
    if (has_inaccessible_nonstatic_data_members(^^AllPublic, access_context::current()))
        return false; // 期望：false

    // 2.4 空类没有非静态数据成员，应返回 false
    if (has_inaccessible_nonstatic_data_members(^^Empty, access_context::current()))
        return false; // 期望：false

    return true;
}
static_assert(test_has_inaccessible_nonstatic_data_members());

// ============================================================
// 测试 3：has_inaccessible_bases
// ============================================================
consteval bool test_has_inaccessible_bases()
{
    // 3.1 私有基类在外部上下文中不可访问
    if (!has_inaccessible_bases(^^DerivedPrivate, access_context::current()))
        return false; // 期望：true

    // 3.2 公有基类在外部上下文中可访问，因此没有不可访问的基类
    if (has_inaccessible_bases(^^DerivedPublic, access_context::current()))
        return false; // 期望：false

    // 3.3 无基类的类，显然没有不可访问的基类
    if (has_inaccessible_bases(^^Empty, access_context::current()))
        return false; // 期望：false

    return true;
}
static_assert(test_has_inaccessible_bases());

// ============================================================
// 测试 4：has_inaccessible_subobjects
// ============================================================
consteval bool test_has_inaccessible_subobjects()
{
    // 4.1 PrivateMembers 包含不可访问的非静态数据成员，因此具有不可访问子对象
    if (!has_inaccessible_subobjects(^^PrivateMembers, access_context::current()))
        return false; // 期望：true

    // 4.2 公有继承且所有成员均公开的类，没有任何不可访问子对象
    struct AllVisible : public Base
    {
        int x;
    };
    if (has_inaccessible_subobjects(^^AllVisible, access_context::current()))
        return false; // 期望：false

    // 4.3 空类没有子对象
    if (has_inaccessible_subobjects(^^Empty, access_context::current()))
        return false; // 期望：false

    return true;
}
static_assert(test_has_inaccessible_subobjects());

// ============================================================
int main()
{
    std::cout << "All access queries tests passed.\n";
    return 0;
}