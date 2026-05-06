#include <iostream>
#include <meta>
#include <tuple>
#include <string>
#include <vector>
#include <utility>

namespace meta = std::meta;

// 1. 索引器：将多个 tuple 展平为单个 tuple
template <std::pair<std::size_t, std::size_t>... indices>
struct Indexer
{
    template <typename Tuples>
    auto operator()(Tuples &&tuples) const
    {
        using ResultType = std::tuple<std::tuple_element_t<
            indices.second, std::remove_cvref_t<std::tuple_element_t<
                                indices.first, std::remove_cvref_t<Tuples>>>>...>;
        return ResultType(std::get<indices.second>(
            std::get<indices.first>(std::forward<Tuples>(tuples)))...);
    }
};

// 2. 将值转换为反射常量并调用 substitute
template <class T>
consteval auto subst_by_value(std::meta::info tmpl, std::vector<T> args)
    -> std::meta::info
{
    std::vector<std::meta::info> a2;
    for (T x : args)
    {
        a2.push_back(std::meta::reflect_constant(x));
    }
    return substitute(tmpl, a2);
}

// 3. 根据所有输入 tuple 的大小生成索引对
consteval auto make_indexer(std::vector<std::size_t> sizes) -> std::meta::info
{
    std::vector<std::pair<int, int>>
        args; // 注意：Indexer 需要 std::size_t，但 reflect_constant 可能需要相同类型
    for (std::size_t tidx = 0; tidx < sizes.size(); ++tidx)
    {
        for (std::size_t eidx = 0; eidx < sizes[tidx]; ++eidx)
        {
            args.push_back({static_cast<int>(tidx), static_cast<int>(eidx)});
        }
    }
    return subst_by_value(^^Indexer, args);
}

// 4. 对外接口 my_tuple_cat
template <typename... Tuples>
auto my_tuple_cat(Tuples &&...tuples)
{
    constexpr
        typename[:make_indexer(
                      {meta::tuple_size(meta::remove_cvref(^^Tuples))...}):] indexer;
    return indexer(std::forward_as_tuple(std::forward<Tuples>(tuples)...));
}

// ---------- 测试用例 ----------
int main()
{
    std::tuple<int, double> t1{1, 2.5};
    std::tuple<char, float> t2{'A', 3.14f};
    std::tuple<std::string> t3{"hello"};

    auto result = my_tuple_cat(t1, t2, t3);

    // 验证类型：最终应该是 tuple<int, double, char, float, std::string>
    static_assert(std::is_same_v<decltype(result),
                                 std::tuple<int, double, char, float, std::string>>);

    // 打印各元素
    std::cout << std::get<0>(result) << '\n'; // 1
    std::cout << std::get<1>(result) << '\n'; // 2.5
    std::cout << std::get<2>(result) << '\n'; // A
    std::cout << std::get<3>(result) << '\n'; // 3.14
    std::cout << std::get<4>(result) << '\n'; // hello

    std::cout << "my_tuple_cat works.\n";
}