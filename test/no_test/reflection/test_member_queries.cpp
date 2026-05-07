#include <meta>
#include <iostream>
#include <string_view>
using namespace std::meta;

// ============================================================
// 辅助实体：类、命名空间、枚举
// ============================================================

class SimpleClass
{
  public:
    int pub_var;
    static double pub_static;
    void pub_func() {}

  private:
    char priv_var;
    static long priv_static;
};

class DerivedPub : public SimpleClass
{
};
class DerivedPriv : private SimpleClass
{
};

enum Color
{
    Red,
    Green,
    Blue
};

// 在命名空间内声明一些成员，测试命名空间的 members_of
namespace TestNS
{
    int a = 1;
    static int b = 2;
    export struct Exported
    {
    }; // 使用 export 模拟模块，但此处仅测试常规行为
} // namespace TestNS

// 静态成员定义
double SimpleClass::pub_static = 0.0;
long SimpleClass::priv_static = 0;

// ============================================================
// 测试 1：members_of 对类和命名空间的基本查询
// ============================================================
consteval bool test_members_of_basic()
{
    // 1.1 对类查询成员，使用当前上下文（非友元），私有成员应被过滤
    {
        auto members = members_of(^^SimpleClass, access_context::current());
        // 公有成员应包含：pub_var, pub_static, pub_func + 特殊成员（构造函数、析构函数、赋值运算符等）
        // 简单检查：应至少包含非静态数据成员 pub_var 和函数 pub_func
        bool has_pub_var = false, has_pub_func = false;
        for (info m : members)
        {
            if (has_identifier(m))
            {
                auto name = identifier_of(m);
                if (name == "pub_var" && is_nonstatic_data_member(m))
                    has_pub_var = true;
                if (name == "pub_func" && is_function(m))
                    has_pub_func = true;
            }
        }
        if (!has_pub_var || !has_pub_func)
            return false; // 期望：能看到公有成员

        // 私有成员 priv_var 不应该出现
        for (info m : members)
        {
            if (has_identifier(m) && identifier_of(m) == "priv_var")
                return false; // 期望：不可见
        }
    }

    // 1.2 使用 unchecked 上下文，应能看到所有成员（包括私有）
    {
        auto members = members_of(^^SimpleClass, access_context::unchecked());
        bool has_priv_var = false;
        for (info m : members)
        {
            if (has_identifier(m) && identifier_of(m) == "priv_var")
            {
                has_priv_var = true;
                break;
            }
        }
        if (!has_priv_var)
            return false; // 期望：能通过 unchecked 看到私有成员
    }

    // 1.3 对命名空间查询，应看到所有可代表成员（当前上下文）
    {
        auto members = members_of(^^TestNS, access_context::current());
        // 应至少包含 a (int) 和 Exported (struct)
        bool has_a = false, has_Exported = false;
        for (info m : members)
        {
            if (has_identifier(m))
            {
                auto name = identifier_of(m);
                if (name == "a" && is_variable(m))
                    has_a = true;
                if (name == "Exported" && is_type(m))
                    has_Exported = true;
            }
        }
        if (!has_a || !has_Exported)
            return false;
        // 静态变量 b 由于是内部链接（static），在当前 TU 中可见（因为未跨模块）
        // 标准示例中跨模块时不可见，这里我们仅检查能在 unchecked 下看到 b
        auto members_unchecked = members_of(^^TestNS, access_context::unchecked());
        bool has_b = false;
        for (info m : members_unchecked)
        {
            if (has_identifier(m) && identifier_of(m) == "b")
                has_b = true;
        }
        if (!has_b)
            return false;
    }

    return true;
}
static_assert(test_members_of_basic());

// ============================================================
// 测试 2：bases_of 与访问控制
// ============================================================
consteval bool test_bases_of()
{
    // 1. 公有派生：基类在 current() 上下文中可访问，应返回 1 个基类
    {
        auto bases = bases_of(^^DerivedPub, access_context::current());
        if (bases.size() != 1)
            return false;
        info base = *bases.begin();
        // 用名称（而非直接反射对象比较）验证基类是 SimpleClass
        if (!has_identifier(base) || identifier_of(base) != "SimpleClass")
            return false;
    }

    // 2. 私有派生：基类对外部不可访问，应返回空向量
    {
        auto bases = bases_of(^^DerivedPriv, access_context::current());
        if (!bases.empty())
            return false; // 期望 size() == 0
    }

    // 3. 使用 unchecked 上下文，私有基类变为“可访问”，应出现
    {
        auto bases = bases_of(^^DerivedPriv, access_context::unchecked());
        if (bases.size() != 1)
            return false;
        info base = *bases.begin();
        if (!has_identifier(base) || identifier_of(base) != "SimpleClass")
            return false;
    }

    return true;
}
static_assert(test_bases_of());

// ============================================================
// 测试 3：static_data_members_of 和 nonstatic_data_members_of
// ============================================================
consteval bool test_data_member_subsets()
{
    // 获取所有可见成员，然后检查子集是否匹配
    auto members = members_of(^^SimpleClass, access_context::unchecked()); // 全量

    auto statics = static_data_members_of(^^SimpleClass, access_context::unchecked());
    auto nonstatics =
        nonstatic_data_members_of(^^SimpleClass, access_context::unchecked());

    // 验证 pub_static 和 priv_static 在 statics 中
    bool has_pub_static = false, has_priv_static = false;
    for (info s : statics)
    {
        if (has_identifier(s))
        {
            auto name = identifier_of(s);
            if (name == "pub_static")
                has_pub_static = true;
            if (name == "priv_static")
                has_priv_static = true;
        }
    }
    if (!has_pub_static || !has_priv_static)
        return false;

    // 验证 pub_var 和 priv_var 在 nonstatics 中
    bool has_pub_var = false, has_priv_var = false;
    for (info ns : nonstatics)
    {
        if (has_identifier(ns))
        {
            auto name = identifier_of(ns);
            if (name == "pub_var")
                has_pub_var = true;
            if (name == "priv_var")
                has_priv_var = true;
        }
    }
    if (!has_pub_var || !has_priv_var)
        return false;

    // 并且它们不应该出现在对方的列表中
    for (info s : statics)
    {
        if (has_identifier(s))
        {
            auto name = identifier_of(s);
            if (name == "pub_var" || name == "priv_var")
                return false;
        }
    }
    for (info ns : nonstatics)
    {
        if (has_identifier(ns))
        {
            auto name = identifier_of(ns);
            if (name == "pub_static" || name == "priv_static")
                return false;
        }
    }

    // 同时保证按原顺序（顺序应与 members_of 一致），但无需严格测试
    return true;
}
static_assert(test_data_member_subsets());

// ============================================================
// 测试 4：enumerators_of
// ============================================================
consteval bool test_enumerators_of()
{
    auto enumerators = enumerators_of(^^Color);
    if (enumerators.size() != 3)
        return false;

    // 检查名称和顺序：Red, Green, Blue
    auto it = enumerators.begin();
    if (has_identifier(*it) && identifier_of(*it) != "Red")
        return false;
    ++it;
    if (has_identifier(*it) && identifier_of(*it) != "Green")
        return false;
    ++it;
    if (has_identifier(*it) && identifier_of(*it) != "Blue")
        return false;

    return true;
}
static_assert(test_enumerators_of());

// ============================================================
// 测试 5：subobjects_of（简单验证）
// ============================================================
consteval bool test_subobjects_of()
{
    // 对于 SimpleClass，subobjects_of 应包含所有非静态数据成员和基类（此处无基类）
    auto subobjects = subobjects_of(^^SimpleClass, access_context::unchecked());
    // 至少应包含 pub_var 和 priv_var
    bool has_pub_var = false, has_priv_var = false;
    for (info sub : subobjects)
    {
        if (has_identifier(sub))
        {
            auto name = identifier_of(sub);
            if (name == "pub_var")
                has_pub_var = true;
            if (name == "priv_var")
                has_priv_var = true;
        }
    }
    if (!has_pub_var || !has_priv_var)
        return false;

    // 对于派生类，应包含基类子对象和非静态数据成员
    auto sub_derived = subobjects_of(^^DerivedPub, access_context::unchecked());
    // 应能找到基类 SimpleClass 的子对象（可能是一个 base 反射）
    bool has_base = false;
    for (info sub : sub_derived)
    {
        if (is_base(sub) && type_of(sub) == ^^SimpleClass)
            has_base = true;
    }
    if (!has_base)
        return false;

    return true;
}
static_assert(test_subobjects_of());

// ============================================================
int main()
{
    std::cout << "All member queries tests passed.\n";
    return 0;
}