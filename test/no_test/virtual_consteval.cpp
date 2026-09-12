#include <iostream>
#include <exception>
#include <utility>

struct base
{
    virtual ~base() = default;
    [[nodiscard]] consteval virtual bool fun() const = 0;
};

struct A : base
{
    [[nodiscard]] consteval bool fun() const override
    {
        return true;
    }
};
struct B : base
{
    [[nodiscard]] consteval bool fun() const override
    {
        return false;
    }
};

// 编译时使用
consteval bool test(const base &b)
{
    return b.fun(); // 编译时求值，OK
}

int main()
try
{
    static_assert(test(A{}));
    static_assert(not test(B{}));

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