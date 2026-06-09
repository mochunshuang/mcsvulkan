#include <iostream>

// NOLINTBEGIN
struct VBase
{
    ~VBase()
    {
        std::cout << "~VBase\n";
    }
};
struct Base1
{
    ~Base1()
    {
        std::cout << "~Base1\n";
    }
};
struct Base2
{
    ~Base2()
    {
        std::cout << "~Base2\n";
    }
};

struct MemberA
{
    ~MemberA()
    {
        std::cout << "~MemberA\n";
    }
};
struct MemberB
{
    ~MemberB()
    {
        std::cout << "~MemberB\n";
    }
};

struct Derived : virtual VBase, Base1, Base2
{
    MemberA a;
    MemberB b;
    ~Derived()
    {
        std::cout << "~Derived body\n";
    }
};

// 构造顺序（由标准规则决定）：
// 1. VBase (虚基类)
// 2. Base1 (从左到右)
// 3. Base2
// 4. a (按声明顺序)
// 5. b
// 6. Derived 构造函数体

// NOTE: 构造 和 析构是相反的顺序，你知道构造 就知道析构
int main()
{
    Derived d;
    return 0; // d 离开作用域，析构开始
} // NOLINTEND