#include <iostream>
#include <meta>

using namespace std::meta;

// ===== is_complete_type =====
struct Complete
{
};
static_assert(is_complete_type(^^Complete));
static_assert(is_complete_type(^^int));
static_assert(is_complete_type(^^int[5]));
struct Incomplete; // 前向声明
static_assert(!is_complete_type(^^Incomplete));
static_assert(!is_complete_type(^^void));
static_assert(!is_complete_type(^^int[])); // 不完整数组
struct Incomplete
{
}; // 变完整
static_assert(is_complete_type(^^Incomplete));

// ===== is_enumerable_type =====
// 1. 前向声明不可枚举
struct Fwd;
static_assert(!is_enumerable_type(^^Fwd));

// 2. 类定义内部：静态断言应失败（类未完成）
struct S
{
    static_assert(!is_enumerable_type(^^S)); // 规范示例，此处 S 未完成
    int a;
    double b;

    // 成员函数内部可枚举
    static consteval bool test_in_member()
    {
        return is_enumerable_type(^^S);
    }
};
// 3. 类定义完成后外部可枚举
static_assert(is_enumerable_type(^^S));
// 4. 成员函数内部断言（通过 consteval 函数）
static_assert(S::test_in_member());

// 5. 枚举类型内部不可枚举，外部可枚举
enum class E
{
    A = is_enumerable_type(^^E) ? 1 : 2 // 规范示例，枚举器定义内返回 false
};
static_assert(is_enumerable_type(^^E));     // 外部可枚举
static_assert(static_cast<int>(E::A) == 2); // 内部为 false -> A = 2

// 6. 别名类型（dealias 处理）
using SE = E;
static_assert(is_enumerable_type(^^SE)); // 别名应视同底层类型

// 7. 非可枚举类型：基本类型、数组、void 等
static_assert(!is_enumerable_type(^^int));
static_assert(!is_enumerable_type(^^int[3]));
static_assert(!is_enumerable_type(^^void));

// 8. 有私有成员的类仍然可枚举
struct Private
{
  private:
    int x;

  public:
    int y;
};
static_assert(is_enumerable_type(^^Private));

// 9. 聚合类、元组类可枚举
struct Aggregate
{
    int i;
    double d;
};
static_assert(is_enumerable_type(^^Aggregate));
static_assert(is_enumerable_type(^^std::pair<int, double>));
static_assert(is_enumerable_type(^^std::tuple<int, double, char>));

int main()
{
    std::cout << "main done\n";
    return 0;
}