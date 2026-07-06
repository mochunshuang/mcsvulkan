#include <span>
#include <cstddef>
#include <iostream>

// 自定义 structural span（满足 NTTP 要求）
template <typename T>
struct ConstSpan
{
    T *data;
    std::size_t size;

    // 允许从 std::span 构造（constexpr 支持）
    constexpr ConstSpan(const std::span<T> &s) noexcept : data(s.data()), size(s.size())
    {
    }
};

// 模板接收 ConstSpan
template <ConstSpan<const int> sp>
struct Test
{
    void print() const
    {
        for (std::size_t i = 0; i < sp.size; ++i)
            std::cout << sp.data[i] << ' ';
        std::cout << '\n';
    }
};

// 全局数组（静态存储期）
static constexpr int arr[] = {1, 2, 3};

// 先构造 constexpr std::span
constexpr std::span<const int> my_std_span{arr};

// 再构造 constexpr ConstSpan（从 std::span 转换）
constexpr ConstSpan<const int> my_const_span = my_std_span; // 调用构造函数

int main()
{
    Test<my_const_span> t; // ✅ 合法
    t.print();             // 输出 1 2 3
    return 0;
}