#include <iostream>

template <typename T>
void fun(T)
{
    std::cout << "default\n";
}

template <>
void fun<int *>(int *)
{
    std::cout << "int *\n";
}

int main()
{
    int *p = nullptr;
    fun<>(p);
    fun<int *>(p);
    std::cout << "main done\n";
    return 0;
}