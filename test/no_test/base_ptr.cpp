#include <any>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <exception>
#include <memory>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <vector>

enum component_status : std::uint8_t
{
    eINVALOID,
    eATTACH,
    eINITIALIZE,
    eENABLE,
    eUPDATE,
    eDISABLE,
    eDETACH,
};

struct component_base
{
    void *data{};
    component_status status{eINVALOID};
};
using component_ptr = std::unique_ptr<component_base, void(void *) noexcept>;

template <typename T>
struct cast_fn
{
    T *operator()(void *ptr) noexcept
    {
        return static_cast<T *>(ptr);
    }
};

template <auto key, typename T>
struct map_type
{
    static constexpr auto id = key; // NOLINT
    using type = T;
};

template <size_t T>
struct register_id : std::false_type
{
};
template <size_t T>
concept unregistered_id = not register_id<T>::value;
template <size_t T>
concept registered_id = register_id<T>::value;

class A
{
};

constexpr auto A_register_id = 0; // NOLINT
template <>
struct register_id<A_register_id> : std::true_type
{
};

static_assert(unregistered_id<1>);
static_assert(registered_id<0>);
// NOTE: 上面的不好，真的 不如 直接使用 Tag

// NOLINTBEGIN
struct Base
{
    virtual ~Base() {}
};

struct B : Base
{
};
struct C : Base
{
};

void test_type_index()
{
    std::unordered_map<std::type_index, std::string> type_names;

    type_names[typeid(int)] = "int";
    type_names[std::type_index(typeid(double))] = "double";
    type_names[std::type_index(typeid(A))] = "A";
    type_names[std::type_index(typeid(B))] = "B";
    type_names[std::type_index(typeid(C))] = "C";

    int i;
    double d;
    A a;

    // 注意我们正在存储指向类型 Base 的指针
    std::unique_ptr<Base> b(new B);
    std::unique_ptr<Base> c(new C);

    std::cout << "i 是 " << type_names[std::type_index(typeid(i))] << '\n';
    std::cout << "d 是 " << type_names[std::type_index(typeid(d))] << '\n';
    std::cout << "a 是 " << type_names[std::type_index(typeid(a))] << '\n';
    //NOTE: 多态的魅力
    std::cout << "*b 是 " << type_names[std::type_index(typeid(*b))] << '\n';
    std::cout << "*c 是 " << type_names[std::type_index(typeid(*c))] << '\n';
} // NOLINTEND

int main()
try
{
    int v = 1;
    void *ptr = &v;
    cast_fn<int> cast_int{};
    auto *ret = cast_int(ptr);
    assert(*ret == 1);

    std::cout << "test_type_index: \n";
    test_type_index();

    // NOLINTBEGIN
    {
        struct A
        {
            int id;
            A(int id) : id(id)
            {
                std::cout << "构造 " << id << '\n';
            }
            ~A()
            {
                std::cout << "析构 " << id << '\n';
            }
        };
        std::tuple<A, A, A, A, A> t{1, 2, 3, 4, 5};
    }
    {
        struct B1
        {
            B1()
            {
                std::cout << "构造 B1\n";
            }
            ~B1()
            {
                std::cout << "析构 B1\n";
            }
        };
        struct B2
        {
            B2()
            {
                std::cout << "构造 B2\n";
            }
            ~B2()
            {
                std::cout << "析构 B2\n";
            }
        };
        struct B3
        {
            B3()
            {
                std::cout << "构造 B3\n";
            }
            ~B3()
            {
                std::cout << "析构 B3\n";
            }
        };

        struct D : B1, B2, B3
        {
            D()
            {
                std::cout << "构造 D\n";
            }
            ~D()
            {
                std::cout << "析构 D\n";
            }
        };
        D d{};

        //多继承不能继承两个一模一样的类（相同类型）。
        // std::tuple 可以包含多个相同类型的元素，这是它与多重继承的一个重要区别。
    }
    // NOLINTEND

    {
        struct any
        {
        };
        // std::any;
    }

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