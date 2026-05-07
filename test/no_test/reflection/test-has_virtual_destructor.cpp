#include <meta>

namespace meta = std::meta;

// 辅助类型
struct WithVirtualDtor
{
    virtual ~WithVirtualDtor() = default;
};

struct PolymorphicNoVirtualDtor
{
    virtual void f() {}
};

struct Plain
{
};

// ─── has_virtual_destructor ───
static_assert(meta::has_virtual_destructor(^^WithVirtualDtor));
static_assert(!meta::has_virtual_destructor(^^PolymorphicNoVirtualDtor));
static_assert(!meta::has_virtual_destructor(^^Plain));
static_assert(!meta::has_virtual_destructor(^^int));
static_assert(!meta::has_virtual_destructor(^^int *));

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