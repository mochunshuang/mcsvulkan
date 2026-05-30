#include <iostream>
#include <exception>

#include <meta>
#include <array>
#include <type_traits>

#include <print>

using namespace std;
using namespace std::meta;

struct universal_formatter
{
    constexpr auto parse(auto &ctx)
    {
        return ctx.begin();
    }

    template <typename T>
    auto format(const T &t, auto &ctx) const
    {
        auto out =
            std::format_to(ctx.out(), "{}{{",
                           has_identifier(^^T) ? identifier_of(^^T) : "(unnamed-type)");

        auto delim = [&, first = true]() mutable {
            if (!first)
            {
                *out++ = ',';
                *out++ = ' ';
            }
            first = false;
        };

        constexpr auto meta_ctx = std::meta::access_context::unchecked();

        template for (constexpr auto base : define_static_array(bases_of(^^T, meta_ctx)))
        {
            delim();
            out = std::format_to(out, "{}", (typename[:type_of(base):] const &)(t));
        }

        template for (constexpr auto mem :
                      define_static_array(nonstatic_data_members_of(^^T, meta_ctx)))
        {
            delim();
            std::string_view mem_label =
                has_identifier(mem) ? identifier_of(mem) : "(unnamed-member)";
            out = std::format_to(out, ".{}={}", mem_label, t.[:mem:]);
        }

        *out++ = '}';
        return out;
    }
};

struct B
{
    int m0 = 0;
};
struct X : B
{
    int m1 = 1;
};
struct Y
{
    int m2 = 2;
};
class Z : public X, private Y
{
    int m3 = 3;
    int m4 = 4;
};

template <>
struct std::formatter<B> : universal_formatter
{
};
template <>
struct std::formatter<X> : universal_formatter
{
};
template <>
struct std::formatter<Y> : universal_formatter
{
};
template <>
struct std::formatter<Z> : universal_formatter
{
};

int main()
try
{
    std::println("{}", Z());
    // Z{X{B{.m0=0}, .m1 = 1}, Y{{.m0=0}, .m2 = 2}, .m3 = 3, .m4 = 4}
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