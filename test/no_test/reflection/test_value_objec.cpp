#include <meta>
#include <iostream>
using namespace std::meta;

// 测试实体
int globalVar = 0;
constexpr int constVar = 42;
enum E
{
    e0
};

// ---- is_value 测试 ----
// 值反射源：reflect_constant、constant_of、枚举器
constexpr info val_literal = reflect_constant(10);
constexpr info val_bool = reflect_constant(true);
constexpr info val_constexpr = constant_of(^^constVar);
constexpr info val_enumerator = ^^e0;

// 其他实体
constexpr info obj_global = reflect_object(globalVar);
constexpr info var_global = ^^globalVar;
constexpr info type_int = ^^int;

// 断言 is_value
static_assert(is_value(val_literal));     // 字面量值反射
static_assert(is_value(val_bool));        // 布尔值反射
static_assert(is_value(val_constexpr));   // constexpr 变量的值
static_assert(!is_value(val_enumerator)); // 枚举器不为值
static_assert(!is_value(type_int));       // 类型不是值
static_assert(!is_value(obj_global));     // 对象反射不是值
static_assert(!is_value(var_global));     // 变量反射不是值

// ---- is_object 测试 ----
constexpr info obj_constval = reflect_object(constVar);

static_assert(is_object(obj_global));   // 全局变量对象反射
static_assert(is_object(obj_constval)); // constexpr 变量对象反射
static_assert(!is_object(type_int));    // 类型不是对象
static_assert(!is_object(val_literal)); // 值反射不是对象
static_assert(!is_object(var_global));  // 变量反射代表声明，不是对象

int main()
{
    std::cout << "main done\n";
    return 0;
}