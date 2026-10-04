#include <algorithm>
#include <cassert>
#include <cstdint>
#include <format>
#include <iostream>
#include <exception>
#include <print>
#include <type_traits>
#include <variant>

#include "head.hpp"

using mcs::vulkan::meta::make_aggregate;
using mcs::vulkan::meta::field;
using mcs::vulkan::meta::method;
using mcs::vulkan::meta::static_string;
// NOLINTBEGIN

template <typename T>
struct runtime_type
{
    static constexpr char value = 0; // NOLINT
};

template <typename T>
inline const void *runtime_type_v = &runtime_type<T>::value; // NOLINT

struct dynamic_pointer
{
    using Fn = void (*)(void *ptr, const void *type, void *p) noexcept;

    template <class Agg>
        requires(
            requires(Agg &agg, const void *type, void *p) {
                agg.template invoke<"runtime_invoke">(type, p);
            } || //
            requires(Agg &agg, const void *type, void *p) {
                agg.runtime_invoke(type, p);
            })
    static constexpr dynamic_pointer make() noexcept
    {
        return dynamic_pointer{[](void *ptr, const void *type, void *p) noexcept {
            auto *agg = static_cast<std::decay_t<Agg> *>(ptr);
            if constexpr (requires(Agg &agg, const void *type, void *p) {
                              agg.template invoke<"runtime_invoke">(type, p);
                          })
                agg->template invoke<"runtime_invoke">(type, p);
            else
                agg->runtime_invoke(type, p);
        }};
    }
    template <typename P>
        requires(!std::is_reference_v<P> && std::is_same_v<P, std::remove_cv_t<P>>)
    constexpr void dispach(void *ptr, P p) noexcept
    {
        // volatile 指针，阻止编译器追踪其指向对象的类型
        void *volatile vp = &p;
        fn(ptr, runtime_type_v<P>, vp);
    }

    dynamic_pointer() = delete;

  private:
    Fn fn;
    explicit constexpr dynamic_pointer(Fn f) noexcept : fn(f) {}
};

struct test_class
{
    void runtime_invoke(const void *type, void *p) noexcept
    {
        if (type == runtime_type_v<int>)
        {
            call_int(*static_cast<int *>(p));
        }
    }
    void call_int(int a)
    {
        std::println("call_int: {}", a);
    }
};

void test_dynamic_pointer()
{
    test_class a;
    test_class *objPtr = &a;
    void *void_ptr = objPtr;
    {
        int b = 0;
        objPtr->runtime_invoke(runtime_type_v<int>, &b);
    }
    auto dp = dynamic_pointer::make<test_class>();
    dp.dispach(objPtr, int{1});

    struct test
    {
        void runtime_invoke(const void *type, void *p)
        {
            if (type == runtime_type_v<double>)
            {
                call_int(*static_cast<double *>(p));
                return;
            }
            else if (type == runtime_type_v<int>)
            {
                call_int(*static_cast<int *>(p));
                return;
            }

            // NOTE: case 需要编译期常量。因此下面的表达是错误的
            // switch (reinterpret_cast<uint64_t>(type))
            // {
            // case reinterpret_cast<uint64_t>(runtime_type_v<double>):
            //     call_int(*static_cast<double *>(p));
            // case reinterpret_cast<uint64_t>(runtime_type_v<int>):
            //     call_int(*static_cast<int *>(p));
            // }
        }
        void call_int(double a)
        {
            std::println("call_int: {}", a);
        }
        void call_int(int a)
        {
            std::println("call_int: {}", a);
        }
    };

    dp = dynamic_pointer::make<test>();
    dp.dispach(void_ptr, int{2}); //NOTE: 没反应
    dp.dispach(void_ptr, double{1.5});

    // NOTE: 返回值只有一个
    // auto return_type = [](int a) {
    //     if (a == 0)
    //         return 1;
    //     else if (a == 1)
    //         return 1.0;
    // };
}

template <typename T>
struct type_pair
{
    static_string name;
    T fn;
};
template <auto... pir>
struct invoke_runtime
{
};

void test_invoke_runtime()
{
    invoke_runtime<type_pair{"int", []() {}}>{};
}

int main()
try
{

    test_dynamic_pointer();
    test_invoke_runtime();

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
} // NOLINTEND