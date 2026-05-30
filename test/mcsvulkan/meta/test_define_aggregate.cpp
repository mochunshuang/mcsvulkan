#include <iostream>
#include <exception>

#include <meta>

using namespace std::meta;

// NOTE: 不可以。模板传入构造
#if 0
template <std::meta::info R>
consteval bool tfn()
{
   // error: 'define_aggregate' not evaluated from 'consteval' block  
    std::meta::define_aggregate(R, {});
    return true;
}

struct S;
constexpr bool b = tfn<^^S>();
#endif

#if 0
struct S;
template <typename>
struct TCls
{
    static consteval bool sfn() // #1
        requires(
            [] {
                // note: 'consteval' block defined here
                consteval
                {
                    define_aggregate(^^S, {
                                          });
                }
            }(),
            false)
    {
        return false; // never selected
    }

    static consteval bool sfn() // #2
        requires(true)
    {
        return true; // always selected
    }
};

static_assert(TCls<void>::sfn());
static_assert(is_complete_type(^^S));
#endif

#if 0
consteval std::meta::info make_defn(std::meta::info Cls, std::meta::info Mem)
{
    // Synthesizes:
    //   struct Mem {};
    //   struct Cls { Mem m; };
    return /*P1*/ define_aggregate(
        Cls, {data_member_spec(/*P2*/ define_aggregate(Mem, {}), {.name = "m"})});
}

/* P3*/ struct C;
/* P4*/ struct M;
static_assert(/*P5*/ is_type(make_defn(^^C, ^^M)) /*P6*/);

/*P7*/ C obj;
#endif

int main()
try
{
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