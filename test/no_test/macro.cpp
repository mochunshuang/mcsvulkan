#include <iostream>
#include <exception>

#define TEST_MACRO

// 基础类型
struct Base
{
    virtual ~Base() = default;
};
struct DerivedA : Base
{
    void showA()
    {
        std::cout << "I am A\n";
    }
};
struct DerivedB : Base
{
    void showB()
    {
        std::cout << "I am B\n";
    }
};

// 主模板：用于注册类型信息的“标签”
template <typename T>
struct TypeTag;

// 注册宏：为每个类专门特化，并绑定字符串
#define REGISTER_TYPE(Type, Name)                 \
    template <>                                   \
    struct TypeTag<Type>                          \
    {                                             \
        static constexpr const char *name = Name; \
        using type = Type;                        \
    };

// 手动生成模板实例（这里通过宏展开完成特化定义）
REGISTER_TYPE(DerivedA, "DerivedA")
REGISTER_TYPE(DerivedB, "DerivedB")

template <typename... T>
struct gen_type
{
};

int main()
try
{
    []() {
        // REGISTER_TYPE(gen_type<DerivedA, DerivedB>, "gen_type2");
    }();
    // constexpr auto TEST_MACRO = 3; //NOTE: 不允许 宏是占用变量声明的
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