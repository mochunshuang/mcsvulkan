#include <iostream>
#include <exception>

#include <meta>

template <typename T>
consteval std::meta::info make_integer_seq_refl(T N)
{
    std::vector args{^^T}; //NOTE: 新玩法. 类型初始化。。。然后后面赋新类型
    for (T k = 0; k < N; ++k)
    {
        args.push_back(std::meta::reflect_constant(k)); //NOTE: reflect_constant 包装？
    }
    return substitute(^^std::integer_sequence, args); //NOTE: 集合替换？ substitute
}

template <typename T, T N>
using make_integer_sequence = [:make_integer_seq_refl<T>(N):];

int main()
try
{
    //与使用常规模板元编程的手动方法相比，我们可以提供更好的make_integer_sequence实现（尽管今天的标准库依赖于内部函数）：

    constexpr auto v = make_integer_sequence<int, 3>{};
    static_assert(
        std::is_same_v<std::decay_t<decltype(v)>, std::integer_sequence<int, 0, 1, 2>>);

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