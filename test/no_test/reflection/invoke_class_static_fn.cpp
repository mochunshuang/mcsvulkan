#include <meta>
#include <iostream>
#include <string>
#include <stdexcept>
#include <span>
#include <any>

using namespace std::meta;

struct MyMath
{
    static int add(int a, int b)
    {
        return a + b;
    }
    static void print_hello()
    {
        std::cout << "Hello!\n";
    }
    static double pi()
    {
        return 3.14159;
    }

    template <typename T>
    static T multiply(T a, T b)
    {
        return a * b;
    }
    template <typename T>
    static T identity(T x)
    {
        return x;
    }
};

template <typename Class, typename... Args>
std::any invoke_class_static_fn_any(const std::string &name, Args &&...args)
{
    static constexpr auto members =
        define_static_array(members_of(^^Class, access_context::current()));

    template for (constexpr info m : members)
    {
        if constexpr (is_static_member(m))
        {
            // 普通静态函数
            if constexpr (is_function(m) &&
                          requires { [:m:](std::forward<Args>(args)...); })
            {
                if (name == identifier_of(m))
                {
                    if constexpr (std::is_void_v<decltype([:m:](
                                      std::forward<Args>(args)...))>)
                    {
                        [:m:](std::forward<Args>(args)...);
                        return std::any{};
                    }
                    else
                    {
                        auto &&value = [:m:](std::forward<Args>(args)...);
                        return std::any(std::forward<decltype(value)>(value));
                    }
                }
            }
            // 静态函数模板
            else if constexpr (is_function_template(m) &&
                               requires { template[:m:](std::forward<Args>(args)...); })
            {
                if (name == identifier_of(m))
                {
                    if constexpr (std::is_void_v<decltype(template[:m:](
                                      std::forward<Args>(args)...))>)
                    {
                        template[:m:](std::forward<Args>(args)...);
                        return std::any{};
                    }
                    else
                    {
                        auto &&value = template[:m:](std::forward<Args>(args)...);
                        return std::any(std::forward<decltype(value)>(value));
                    }
                }
            }
        }
    }
    throw std::runtime_error("Function not found: " + name);
}

template <typename From, typename To>
concept return_convertible_to =
    bool(std::is_void_v<To> ? std::same_as<From, void> : std::convertible_to<From, To>);

template <typename Class, typename Ret, typename... Args>
Ret invoke_class_static_fn(const std::string &name, Args &&...args)
{
    static constexpr auto members =
        define_static_array(members_of(^^Class, access_context::current()));

    template for (constexpr info m : members)
    {
        if constexpr (is_static_member(m))
        {
            if constexpr (is_function(m) && requires {
                              {
                                  [:m:](std::forward<Args>(args)...)
                              } -> return_convertible_to<Ret>;
                          })
            {
                if (name == identifier_of(m))
                    return [:m:](std::forward<Args>(args)...);
            }
            else if constexpr (is_function_template(m) && requires {
                                   {
                                       template[:m:](std::forward<Args>(args)...)
                                   } -> return_convertible_to<Ret>;
                               })
            {
                if (name == identifier_of(m))
                    return template[:m:](std::forward<Args>(args)...);
            }
        }
    }
    throw std::runtime_error("Function not found or return type mismatch: " + name);
}

int main()
try
{
    // 普通静态函数
    int sum = std::any_cast<int>(invoke_class_static_fn_any<MyMath>("add", 3, 4));
    std::cout << "add(3,4) = " << sum << '\n';

    invoke_class_static_fn_any<MyMath>("print_hello"); // void 函数

    auto pi_val = std::any_cast<double>(invoke_class_static_fn_any<MyMath>("pi"));
    std::cout << "pi = " << pi_val << '\n';

    // 函数模板
    auto prod =
        std::any_cast<double>(invoke_class_static_fn_any<MyMath>("multiply", 2.5, 4.0));
    std::cout << "multiply(2.5, 4.0) = " << prod << '\n';

    int prod_int =
        std::any_cast<int>(invoke_class_static_fn_any<MyMath>("multiply", 3, 7));
    std::cout << "multiply(3, 7) = " << prod_int << '\n';

    auto id_d =
        std::any_cast<double>(invoke_class_static_fn_any<MyMath>("identity", 3.14));
    std::cout << "identity(3.14) = " << id_d << '\n';

    const char *id_str = std::any_cast<const char *>(
        invoke_class_static_fn_any<MyMath>("identity", "hello"));
    std::cout << "identity(\"hello\") = " << id_str << '\n';

    {
        // ---- 模板指定返回值版本测试 ----
        std::cout << "\n--- Ret specified version ---\n";
        int sum2 = invoke_class_static_fn<MyMath, int>("add", 3, 4);
        std::cout << "add(3,4) = " << sum2 << '\n';

        invoke_class_static_fn<MyMath, void>("print_hello");

        double pi2 = invoke_class_static_fn<MyMath, double>("pi");
        std::cout << "pi = " << pi2 << '\n';

        double prod2 = invoke_class_static_fn<MyMath, double>("multiply", 2.5, 4.0);
        std::cout << "multiply(2.5, 4.0) = " << prod2 << '\n';

        int prod_int = invoke_class_static_fn<MyMath, int>("multiply", 3, 7);
        std::cout << "multiply(3, 7) = " << prod_int << '\n';

        double id_d = invoke_class_static_fn<MyMath, double>("identity", 3.14);
        std::cout << "identity(3.14) = " << id_d << '\n';

        const char *id_str =
            invoke_class_static_fn<MyMath, const char *>("identity", "hello");
        std::cout << "identity(\"hello\") = " << id_str << '\n';
    }

    std::cout << "All tests passed.\n";
    return 0;
}
catch (const std::exception &e)
{
    std::cerr << "Exception: " << e.what() << '\n';
    return 1;
}