#include <iostream>
#include <meta>

namespace meta = std::meta;

// 1. 仅声明、永不定义的模板，用于“打孔”表示已分配的票号
template <int N>
struct Helper; //NOTE: “记忆”靠的是编译器对已定义类型的记录

// 2. 票号管理器
struct TU_Ticket
{
    // 查询当前已分配的最大票号 +1
    static consteval int latest()
    {
        int k = 0;
        //NOTE: 定义了 就 ++
        // 不断检查 Helper<k> 是否完整，完整则说明该票号已分配
        while (meta::is_complete_type(
            meta::substitute(^^Helper, {
                                           std::meta::reflect_constant(k)})))
        {
            ++k;
        }
        return k;
    }

    // 分配最新的票号：将 Helper<latest()> 定义为空聚合体，使其“完整”
    static consteval void increment()
    {
        meta::define_aggregate(
            meta::substitute(^^Helper,
                             {
                                 std::meta::reflect_constant(latest())}),
            {}); // 空成员列表 -> 空结构体
    }
};

// 3. 测试用例：在编译期分配票号并赋值给 constexpr 变量
constexpr int x = TU_Ticket::latest(); // x = 0
consteval
{
    TU_Ticket::increment();
} // 分配票号 0
constexpr int y = TU_Ticket::latest(); // y = 1
consteval
{
    TU_Ticket::increment();
} // 分配票号 1
constexpr int z = TU_Ticket::latest(); // z = 2

// 编译期断言
static_assert(x == 0);
static_assert(y == 1);
static_assert(z == 2);

int main()
{
    std::cout << "Ticket test:\n";
    std::cout << "x = " << x << '\n';
    std::cout << "y = " << y << '\n';
    std::cout << "z = " << z << '\n';
    std::cout << "Compile-time ticket counter works.\n";
    return 0;
}