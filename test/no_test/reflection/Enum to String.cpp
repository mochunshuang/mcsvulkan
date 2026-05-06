#include <iostream>
#include <exception>
#include <algorithm>
#include <meta>
#include <map>
#include <ranges>

#include <print>
#include <string_view>

// NOLINTBEGIN
template <typename E, bool Enumerable = std::meta::is_enumerable_type(^^E)>
    requires std::is_enum_v<E>
constexpr std::string_view enum_to_string(E value)
{
    // define_static_array
    // identifier_of
    if constexpr (Enumerable)
        template for (constexpr auto e :
                      std::define_static_array(std::meta::enumerators_of(^^E)))
        {
            if (value == [:e:]) // e 是什么？值，是的 enumerators 迭代的
                return std::meta::identifier_of(e);
        }

    return "<unnamed>";
}

// 取反
template <typename E, bool Enumerable = std::meta::is_enumerable_type(^^E)>
    requires std::is_enum_v<E>
constexpr std::optional<E> string_to_enum(std::string_view name)
{
    if constexpr (Enumerable)
        template for (constexpr auto e :
                      std::define_static_array(std::meta::enumerators_of(
                          ^^E))) if (name == std::meta::identifier_of(e)) return [:e:];

    return std::nullopt;
}

// 不使用扩展语法
template <typename E>
    requires std::is_enum_v<E>
constexpr std::string enum_to_string2(E value)
{

    constexpr auto get_name = [](E value) -> std::optional<std::string> {
        constexpr auto get_pairs = [] {
            return std::meta::enumerators_of(^^E) |
                   std::views::transform([](std::meta::info e) {
                       return std::pair<E, std::string>(std::meta::extract<E>(e),
                                                        std::meta::identifier_of(e));
                   });
        };

        if constexpr (enumerators_of(^^E).size() <= 7)
        {
            // if there aren't many enumerators, use a vector with find_if()
            auto enumerators = get_pairs() | std::ranges::to<std::vector>();
            auto it = std::ranges::find_if(
                enumerators, [value](auto const &pr) { return pr.first == value; });
            if (it == enumerators.end())
                return std::nullopt;
            else
                return it->second;
        }
        else
        {
            // if there are lots of enumerators, use a map with find()
            auto enumerators = get_pairs() | std::ranges::to<std::map>();
            auto it = enumerators.find(value);
            if (it == enumerators.end())
                return std::nullopt;
            else
                return it->second;
        }
    };

    return get_name(value).value_or("<unnamed>");
}

int main()
try
{

    enum Color : int;
    static_assert(enum_to_string(Color(0)) == "<unnamed>");
    std::println("Color 0: {}", enum_to_string(Color(0))); // prints '<unnamed>'

    enum Color : int
    {
        red,
        green,
        blue
    };
    static_assert(enum_to_string(Color::red) == "red");
    static_assert(enum_to_string(Color(42)) == "<unnamed>");
    std::println("Color 0: {}", enum_to_string(Color(0))); // prints 'red'

    static_assert(string_to_enum<Color>("red").value() == Color::red);
    static_assert(not string_to_enum<Color>("unnamed").has_value());

    // NOTE: 不使用扩展语法
    static_assert(enum_to_string2(Color::red) == std::string_view{"red"});

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