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

using parameter_list = std::variant<int, double>;

struct dynamic_pointer
{
    using Fn = void (*)(void *ptr, parameter_list) noexcept;

    template <class Agg, static_string hover_fn_name>
        requires(requires(Agg &agg, parameter_list p) {
            agg.template invoke<hover_fn_name>(p);
        })
    static constexpr dynamic_pointer make() noexcept
    {
        return dynamic_pointer{[](void *ptr, parameter_list p) noexcept {
            auto *agg = static_cast<std::decay_t<Agg> *>(ptr);
            agg->template invoke<hover_fn_name>(p);
        }};
    }
    constexpr void dispach(void *ptr, parameter_list p) noexcept
    {
        fn(ptr, std::move(p));
    }

    dynamic_pointer() = delete;

  private:
    Fn fn;
    explicit constexpr dynamic_pointer(Fn f) noexcept : fn(f) {}
};

void test_dynamic_pointer()
{
    auto obj = make_aggregate<"TextBox">(
        field<"text">(std::string("CD")),
        method<"arena">([](auto &&self, parameter_list p) noexcept {
            p.visit([&](auto &&value) noexcept {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, int>)
                {
                    self.text = std::format("{}", value);
                    std::println("int: text: {}", self.text);
                }
                else if constexpr (std::is_same_v<T, double>)
                {
                    self.text = std::format("{}", value);
                    std::println("double: text: {}", self.text);
                }
                else
                {
                    // 断言失败，或？继续事件冒泡？
                }
            });
        }));
    auto dp = dynamic_pointer::make<std::decay_t<decltype(obj)>, "arena">();
    dp.dispach(&obj, {0});
    dp.dispach(&obj, {0.0});

    struct arena_export
    {
        constexpr void invoke(parameter_list p) noexcept
        {
            fn.dispach(obj, p);
        }
        constexpr arena_export(void *obj, dynamic_pointer fn) : obj{obj}, fn{fn} {}

      private:
        void *obj;
        dynamic_pointer fn;
    };
    arena_export c{&obj, dynamic_pointer::make<std::decay_t<decltype(obj)>, "arena">()};
    c.invoke({1});
    c.invoke({1.3});
}

int main()
try
{

    test_dynamic_pointer();

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