#include <iostream>
#include <exception>
#include <type_traits>

#include "../head.hpp"

int main()
try
{
    namespace traits = mcs::vulkan::type_traits;

    // 1. 非引用类型（深度 0）
    static_assert(traits::ref_depth_depth_v<int> == 0);
    static_assert(traits::ref_depth_depth_v<const int> == 0);
    static_assert(traits::ref_depth_depth_v<volatile int> == 0);
    static_assert(traits::ref_depth_depth_v<void> == 0);

    // 2. 单层左值引用（深度 1）
    static_assert(traits::ref_depth_depth_v<int &> == 1);
    static_assert(traits::ref_depth_depth_v<const int &> == 1);
    static_assert(traits::ref_depth_depth_v<int *&> == 1);    // 指针的引用
    static_assert(traits::ref_depth_depth_v<int (&)()> == 1); // 函数引用

    // 3. 引用折叠：int& & 折叠为 int&，所以深度为 1，不是 2
    using IntRef = int &;
    static_assert(traits::ref_depth_depth_v<IntRef &> == 1);  // 折叠后 int&
    static_assert(traits::ref_depth_depth_v<IntRef &&> == 1); // 右值引用无特化
    static_assert(std::is_same_v<IntRef &&, IntRef>, "引用折叠");
    static_assert(std::is_same_v<IntRef &, IntRef>, "引用折叠");

    // 4. 右值引用（当前无特化，深度为 0）
    static_assert(traits::ref_depth_depth_v<int &&> == 0);
    static_assert(traits::ref_depth_depth_v<const int &&> == 0);

    // 5. 混合 cv 限定符（cv 被忽略，不影响深度）
    static_assert(traits::ref_depth_depth_v<const volatile int &> == 1);

    // 6. 指针和引用的组合（仅计算引用层数）
    static_assert(traits::ref_depth_depth_v<int *&> == 1);
    static_assert(traits::ref_depth_depth_v<int **&> == 1);

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