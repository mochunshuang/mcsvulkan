#include <meta>
#include <iostream>
using namespace std::meta;

// ========================= 辅助类 =========================

// 普通类 A，带有一个 consteval 构造函数
struct A
{
    int a = 0;
    consteval A(int p) : a(p) {}
};

// B 从 A 公有继承，并利用 using A::A 继承 A 的构造函数
// 同时 B 有自己的 consteval 构造函数，用于测试成员初始化器中的 access_context
struct B : A
{
    using A::A;
    consteval B(int p, int q) : A(p * q) {}
    // s 在非静态数据成员的默认成员初始化器中取得 access_context::current().scope()
    // 根据 eval-point 规则，默认成员初始化器的求值点会被映射到构造函数定义
    // 因此这里得到的 scope 应反映调用构造函数的上下文
    info s = access_context::current().scope();
};

// C 继续从 B 继承，并 using B::B，用来测试继承构造函数中的上下文
struct C : B
{
    using B::B;
};

// Agg 是一个聚合类，其成员 s 同样在默认成员初始化器中保存 current().scope()
// eq 函数有一个默认参数 rhs，该默认参数的 current() 根据规则会在调用点求值
struct Agg
{
    consteval bool eq(info rhs = access_context::current().scope())
    {
        return s == rhs;
    }
    info s = access_context::current().scope();
};

// ========================= 测试 1：基本 current() 行为 =========================
consteval bool test_current_basic()
{
    // 1. 在普通 consteval 函数内部调用 current()
    //    此时 eval-point 就是当前函数，scope 应是本函数的反射，designating_class 为空
    auto ctx = access_context::current();
    if (ctx.designating_class() != info{}) // 普通函数没有指定设计类
        return false;
    if (!is_function(ctx.scope())) // scope 必须是函数
        return false;

    // 2. 默认参数中的 current() 反映调用点
    //    Agg{} 是聚合初始化，成员 s 的默认成员初始化器求值点映射到该聚合初始化出现的位置
    //    Agg{}.eq() 调用的默认参数 rhs 的 current() 同样会在该调用点求值
    //    因此 s 和 rhs 应当相等
    if (!Agg{}.eq())
        return false;

    // 3. 成员初始化器中的 current()
    //    B(1) 调用的是 using A::A 继承来的构造函数 A(int)
    //    该构造函数内部，成员 s 的默认成员初始化器求值点映射到该构造函数调用点（当前为 test_current_basic 内）
    //    因此 scope 应当反映调用发生的函数作用域，这里 s 记录的结果应该是 B(1).s
    //    由于调用发生在 test_current_basic 内，scope 应为 test_current_basic 而非全局，但这里比较 != ^^B 是测试 scope 不是 B
    //    实际上 B(1).s 的 scope 是 B 的构造函数（继承自 A 的构造函数）的反射，所以它的 parent 是 ^^B
    //    标准示例中 B(1).s == ^^B 是验证 s 反射的是构造函数 B（继承而来）本身，而非父类 A
    //    我们这里直接使用标准示例的断言：B(1).s 应该等于 ^^B（构造函数反射）
    if (B(1).s != ^^B)
        return false;

    // 4. 继承构造函数中的成员初始化器
    //    B{1, 2} 调用 B 自己的 consteval 构造函数 B(int, int)
    //    在该构造函数内，成员 s 的默认成员初始化器求值点映射到该构造函数调用点
    //    得到的 s 的 scope 应当是 B(int, int) 这个构造函数的反射
    //    因此 s 是构造函数，且它的父作用域是 ^^B
    if (!is_constructor(B{1, 2}.s) || parent_of(B{1, 2}.s) != ^^B)
        return false;
    //    C{1, 2} 调用 B(int, int) 通过 using B::B 继承来的构造函数
    //    类似地，反射的构造函数父作用域仍然是 ^^B（构造函数定义所在的类）
    if (!is_constructor(C{1, 2}.s) || parent_of(C{1, 2}.s) != ^^B)
        return false;

    return true;
}
static_assert(test_current_basic());

// ========================= 测试 2：模板实体的值依赖行为 =========================
// 验证在模板实体的 requires 子句中 current() 是值依赖的，并且能正确反映模板作用域
template <auto R>
struct TCls
{
    consteval bool fn()
        requires(is_type(access_context::current().scope()))
    {
        // 标准注释：scope 是 'TCls<R>'
        // 这里只要求在 requires 子句中 current() 的 scope 是一个类型（即 TCls<R>）
        // 这证明了在模板中 current() 的值依赖行为——不同的模板实参产生不同的作用域反射
        return true;
    }
};

consteval bool test_template_dependent()
{
    // 调用 TCls<0>.fn()，如果 requires 子句中的 is_type 检查通过，并且最终返回 true，则测试通过
    return TCls<0>{}.fn();
}
static_assert(test_template_dependent());

// ========================= 测试 3：unprivileged() =========================
consteval bool test_unprivileged()
{
    access_context unpriv = access_context::unprivileged();
    // unprivileged() 返回一个 scope 为全局命名空间、designating_class 为空的访问上下文
    if (unpriv.scope() != ^^::) // scope 必须是全局命名空间
        return false;
    if (unpriv.designating_class() != info{}) // 没有指定设计类
        return false;
    return true;
}
static_assert(test_unprivileged());

// ========================= 测试 4：unchecked() =========================
consteval bool test_unchecked()
{
    access_context unchk = access_context::unchecked();
    // unchecked() 返回 scope 和 designating_class 都是空反射的完全无限制上下文
    if (unchk.scope() != info{} || unchk.designating_class() != info{})
        return false;
    return true;
}
static_assert(test_unchecked());

// ========================= 测试 5：via() =========================
consteval bool test_via()
{
    // 获取当前函数内的访问上下文
    access_context cur = access_context::current();
    // 通过 via 指定设计类为 A，scope 应该保持不变
    access_context via_ctx = cur.via(^^A);
    if (via_ctx.scope() != cur.scope())
        return false;
    if (via_ctx.designating_class() != ^^A)
        return false;

    // 也可以通过 null 反射来清除设计类
    access_context via_null = cur.via(info{});
    if (via_null.designating_class() != info{})
        return false;
    return true;
}
static_assert(test_via());

// ========================= 测试 6：lambda 内的 scope =========================
consteval bool test_lambda_scope()
{
    auto lam = []() consteval {
        auto ctx = access_context::current();
        // 在 lambda 内部调用 current()，根据规则其 scope 应该是对应闭包类型的调用运算符
        // 这里我们简单验证：不是全局命名空间，也没有设计类
        return ctx.designating_class() == info{} && !is_namespace(ctx.scope());
    };
    return lam();
}
static_assert(test_lambda_scope());

int main()
{
    std::cout << "all access_context tests passed\n";
    return 0;
}