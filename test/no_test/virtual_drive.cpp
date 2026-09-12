#include <cassert>
#include <iostream>
#include <exception>
#include <memory>
#include <utility>

class Base
{
  public:
    virtual Base *clone() const
    {
        return new Base(*this);
    }
    virtual Base &get()
    {
        return *this;
    }
};

class Derived : public Base
{
  public:
    // 协变：返回 Derived* 而不是 Base*
    Derived *clone() const override
    {
        return new Derived(*this);
    }
    // 协变：返回 Derived& 而不是 Base&
    Derived &get() override
    {
        return *this;
    }
};

// NOTE: 变成智能指针。但是明显无法得到静态信息，需要强转？
class BaseClass
{
  public:
    [[nodiscard]] virtual std::unique_ptr<BaseClass> clone() const = 0;
    virtual ~BaseClass() = default;
};

class DerivedClass : public BaseClass
{
  public:
    [[nodiscard]] std::unique_ptr<BaseClass> clone() const override
    {
        return std::make_unique<DerivedClass>(*this); // 隐式转换为 unique_ptr<Base>
    }
    int a = 1;

    // void test(sturct A{} a); //NOTE: 不能匿名定义类型在参数列表中
};

int main()
try
{
    // NOTE: 如果
    Base pBase = Derived();
    [[maybe_unused]] Base *p1 = pBase.clone(); // 静态类型：Base*，因为 pBase 是 Base*

    Derived pDerived = Derived();
    [[maybe_unused]] Derived *p2 =
        pDerived.clone(); // 静态类型：Derived*，因为 pDerived 是 Derived*

    {
        std::unique_ptr<BaseClass> basePtr = DerivedClass{}.clone();
        bool pass{};
        if (auto derivedPtr = dynamic_cast<DerivedClass *>(basePtr.get()))
        {
            pass = true;
            assert(derivedPtr->a == 1);
        }
        assert(pass);
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