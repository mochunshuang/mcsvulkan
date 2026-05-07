#include <iostream>
#include <meta>
#include <span>
using namespace std::meta;

// 基类：提供各种虚函数形态
struct Base
{
    void non_virtual() {}                         // 普通函数
    virtual void virt() {}                        // 虚函数
    virtual void pure_virt() = 0;                 // 纯虚函数
    virtual void will_be_overridden() {}          // 将被覆盖（派生类隐式覆盖）
    virtual void will_be_overridden_explicit() {} // 将被覆盖（派生类显式 override）
    virtual void base_final() final {}            // final 虚函数
};

// 派生类：覆盖与新定义
struct Derived : public Base
{
    void non_virtual() {}        // 隐藏，非虚
    void virt() override {}      // 显式覆盖
    void pure_virt() override {} // 覆盖纯虚，不再是纯虚

    // 覆盖但未写 override 关键字
    void will_be_overridden() {}

    // 覆盖且显式写 override
    void will_be_overridden_explicit() override {}

    // 新引入的虚函数
    virtual void new_virtual() {}     // 新虚函数
    virtual void new_final() final {} // 新引入的 final 虚函数
};

// 测试虚基类（is_virtual 对直接基类关系适用）
struct VirtualBase
{
};
struct DerivedVirt : virtual VirtualBase
{
};

// 辅助函数：在派生类内部获取基类反射（需要访问上下文）
struct TestVirtBaseHelper : DerivedVirt
{
    static consteval bool check_virtual_base()
    {
        auto bases = bases_of(^^DerivedVirt, access_context::current());
        std::span<const info> sp(bases);
        if (sp.size() != 1)
            return false;
        return is_virtual(sp[0]); // 虚基类
    }
};

// ==================== 静态断言测试 ====================

// ----- is_virtual -----
// 基类
static_assert(!is_virtual(^^Base::non_virtual));
static_assert(is_virtual(^^Base::virt));
static_assert(is_virtual(^^Base::pure_virt));
static_assert(is_virtual(^^Base::will_be_overridden));
static_assert(is_virtual(^^Base::will_be_overridden_explicit));
static_assert(is_virtual(^^Base::base_final));

// 派生类
static_assert(!is_virtual(^^Derived::non_virtual)); // 隐藏后的非虚函数
static_assert(is_virtual(^^Derived::virt));
static_assert(is_virtual(^^Derived::pure_virt));
static_assert(is_virtual(^^Derived::will_be_overridden));
static_assert(is_virtual(^^Derived::will_be_overridden_explicit));
static_assert(is_virtual(^^Derived::new_virtual));
static_assert(is_virtual(^^Derived::new_final));

// 虚基类关系
static_assert(TestVirtBaseHelper::check_virtual_base());

// ----- is_pure_virtual -----
static_assert(!is_pure_virtual(^^Base::virt));
static_assert(is_pure_virtual(^^Base::pure_virt));
static_assert(!is_pure_virtual(^^Base::base_final));

static_assert(!is_pure_virtual(^^Derived::pure_virt)); // 已实现纯虚函数
static_assert(!is_pure_virtual(^^Derived::new_virtual));

// ----- is_override (覆盖另一个成员函数即为真，不要求 override 关键字) -----
static_assert(!is_override(^^Base::virt));
static_assert(!is_override(^^Base::pure_virt));
static_assert(!is_override(^^Base::will_be_overridden));

static_assert(is_override(^^Derived::virt));
static_assert(is_override(^^Derived::pure_virt));
static_assert(is_override(^^Derived::will_be_overridden)); // 覆盖但无关键字
static_assert(is_override(^^Derived::will_be_overridden_explicit));
static_assert(!is_override(^^Derived::new_virtual)); // 新引入，非覆盖
static_assert(!is_override(^^Derived::new_final));   // 新引入

// ----- is_final (虚函数且声明中有 final 说明符) -----
static_assert(!is_final(^^Base::virt));
static_assert(!is_final(^^Base::pure_virt));
static_assert(is_final(^^Base::base_final));

static_assert(!is_final(^^Derived::virt));
static_assert(!is_final(^^Derived::pure_virt));
static_assert(!is_final(^^Derived::will_be_overridden));
static_assert(!is_final(^^Derived::will_be_overridden_explicit));
static_assert(!is_final(^^Derived::new_virtual));
static_assert(is_final(^^Derived::new_final));

int main()
{
    std::cout << "main done\n";
    return 0;
}