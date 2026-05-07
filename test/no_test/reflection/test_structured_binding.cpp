#include <iostream>
#include <meta>
#include <utility>
using namespace std::meta;

// 全局 constexpr 结构化绑定，引入的变量具有静态存储期，可用 ^^ 反射
static constexpr auto [x, y] = std::pair{1, 2.0};

// 普通全局 constexpr 变量
static constexpr int plain = 0;

// 类成员和普通变量
int globalVar = 0;

// 测试断言
static_assert(is_structured_binding(^^x));
static_assert(is_structured_binding(^^y));
static_assert(!is_structured_binding(^^plain));
static_assert(!is_structured_binding(^^globalVar));
static_assert(!is_structured_binding(^^int));

int main()
{
    std::cout << "main done\n";
    return 0;
}