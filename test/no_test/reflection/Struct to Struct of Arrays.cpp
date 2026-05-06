#include <iostream>
#include <exception>

#include <meta>
#include <array>
#include <type_traits>

using namespace std;
using namespace std::meta;

template <typename T, size_t N>
struct struct_of_arrays_impl // NOLINT
{
    struct impl;

    consteval
    {
        auto ctx = std::meta::access_context::current();

        std::vector<std::meta::info> old_members = nonstatic_data_members_of(^^T, ctx);
        std::vector<std::meta::info> new_members = {};
        for (std::meta::info member : old_members)
        {
            //NOTE: substitute 模板实例化？
            auto array_type = substitute(^^std::array, {
                                                           type_of(member),
                                                           std::meta::reflect_constant(N),
                                                       });
            // identifier_of 获得字符串名词？
            auto mem_descr =
                data_member_spec(array_type, {.name = identifier_of(member)});
            new_members.push_back(mem_descr);
        }
        define_aggregate(^^impl, new_members);
    }
};

template <typename T, size_t N>
using struct_of_arrays = struct_of_arrays_impl<T, N>::impl;

template <typename T, size_t N>
using struct_of_arrays2 = [:[] {
    // ... same logic ..
    auto ctx = std::meta::access_context::current();
    std::vector<std::meta::info> old_members = nonstatic_data_members_of(^^T, ctx);
    std::vector<std::meta::info> new_members = {};
    for (std::meta::info member : old_members)
    {
        auto array_type = substitute(^^std::array, {
                                                       type_of(member),
                                                       std::meta::reflect_constant(N),
                                                   });
        auto mem_descr = data_member_spec(array_type, {.name = identifier_of(member)});
        new_members.push_back(mem_descr);
    }

    // OK, same instantiation
    struct impl;
    define_aggregate(^^impl, new_members);
}():];

int main()
try
{
    struct point
    {
        float x;
        float y;
        float z;
    };

    using points = struct_of_arrays<point, 30>;
    // equivalent to:
    // struct points {
    //   std::array<float, 30> x;
    //   std::array<float, 30> y;
    //   std::array<float, 30> z;
    // };
    using x_type = decltype(points{}.x);
    static_assert(std::is_same_v<x_type, std::array<float, 30>>);

    // NOTE: 下面是失败的
    // using points2 = struct_of_arrays2<point, 30>;

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