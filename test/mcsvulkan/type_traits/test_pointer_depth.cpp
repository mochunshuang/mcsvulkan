#include <iostream>
#include <exception>

#include "../head.hpp"

int main()
try
{
    namespace traits = mcs::vulkan::type_traits;

    // 1. 基础类型和 cv 限定符（深度应为 0）
    static_assert(traits::pointer_depth_v<int> == 0, "int");
    static_assert(traits::pointer_depth_v<const int> == 0, "const int");
    static_assert(traits::pointer_depth_v<volatile int> == 0, "volatile int");
    static_assert(traits::pointer_depth_v<const volatile int> == 0, "const volatile int");
    static_assert(traits::pointer_depth_v<void> == 0, "void");

    // 2. 单级指针（深度应为 1）
    static_assert(traits::pointer_depth_v<int *> == 1, "int*");
    static_assert(traits::pointer_depth_v<const int *> == 1, "const int*");
    static_assert(traits::pointer_depth_v<int *const> == 1, "int* const");
    static_assert(traits::pointer_depth_v<const int *const> == 1, "const int* const");
    static_assert(traits::pointer_depth_v<volatile int *> == 1, "volatile int*");
    static_assert(traits::pointer_depth_v<int *volatile> == 1, "int* volatile");
    static_assert(traits::pointer_depth_v<const volatile int *> == 1,
                  "const volatile int*");

    // 3. 多级指针
    static_assert(traits::pointer_depth_v<int **> == 2, "int**");
    static_assert(traits::pointer_depth_v<int ***> == 3, "int***");
    static_assert(traits::pointer_depth_v<const int **> == 2, "const int**");
    static_assert(traits::pointer_depth_v<int **const> == 2, "int** const");
    static_assert(traits::pointer_depth_v<int *const *> == 2, "int* const*");
    static_assert(traits::pointer_depth_v<const int *const *> == 2, "const int* const*");
    static_assert(traits::pointer_depth_v<const int *const *const> == 2,
                  "const int* const* const");
    static_assert(traits::pointer_depth_v<int *volatile *> == 2, "int* volatile*");
    static_assert(traits::pointer_depth_v<int ***const volatile> == 3,
                  "int*** const volatile");

    // 4. 引用类型（不是指针，深度应为 0）
    static_assert(traits::pointer_depth_v<int &> == 0, "int&");
    static_assert(traits::pointer_depth_v<const int &> == 0, "const int&");
    static_assert(traits::pointer_depth_v<int *&> == 0,
                  "int*&"); // 引用到指针，本身不是指针
    static_assert(traits::pointer_depth_v<int &&> == 0, "int&&");

    // 5. 函数指针类型（也是指针，深度应为 1）
    static_assert(traits::pointer_depth_v<void (*)()> == 1, "void(*)()");
    static_assert(traits::pointer_depth_v<void (*)(int)> == 1, "void(*)(int)");
    static_assert(traits::pointer_depth_v<int (*)(double, char)> == 1,
                  "int(*)(double,char)");

    // 6. 成员指针（不是常规指针，深度应为 0）
    struct Dummy
    {
    };
    static_assert(traits::pointer_depth_v<int Dummy::*> == 0, "int Dummy::*");
    static_assert(traits::pointer_depth_v<void (Dummy::*)()> == 0, "void (Dummy::*)()");

    // 7. 复杂嵌套（多级函数指针） 太抽象
    static_assert(traits::pointer_depth_v<void (*(*)(int))()> == 1, "void(*(*)(int))()");

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