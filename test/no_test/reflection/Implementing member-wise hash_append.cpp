#include <iostream>
#include <meta>       // 反射核心头文件
#include <functional> // std::hash
#include <string>

using namespace std;
using namespace std::meta;

// NOLINTBEGIN
// ----- 定义注解（P3394R4 规范） -----
struct hash_skip_t
{
};
inline constexpr hash_skip_t hash_skip{}; // 注解值

// ----- 基础类型 hash_append -----
void hash_append(size_t &algo, int val)
{
    algo ^= std::hash<int>{}(val) + 0x9e3779b9 + (algo << 6) + (algo >> 2);
}
void hash_append(size_t &algo, double val)
{
    algo ^= std::hash<double>{}(val) + 0x9e3779b9 + (algo << 6) + (algo >> 2);
}
void hash_append(size_t &algo, const std::string &val)
{
    algo ^= std::hash<std::string>{}(val) + 0x9e3779b9 + (algo << 6) + (algo >> 2);
}

// ----- 编译期过滤：返回不带 hash_skip 注解的成员数组 -----
template <typename T>
consteval auto hashable_members()
{
    constexpr auto ctx = meta::access_context::unchecked();
    std::vector<std::meta::info> all = meta::nonstatic_data_members_of(^^T, ctx);
    std::vector<std::meta::info> filtered;
    for (auto mem : all)
    {
        // 检查是否有 hash_skip_t 类型的注解
        auto skips = meta::annotations_of_with_type(mem, ^^hash_skip_t);
        if (skips.empty()) // 没有 hash_skip 注解 → 保留
            filtered.push_back(mem);
    }
    return filtered; // 之后由 define_static_array 转换为静态 span
}

// ----- 通用成员哈希（注解驱动） -----
template <typename H, typename T>
void hash_append(H &algo, const T &obj)
{
    // 一步拿到过滤后的静态成员列表
    template for (constexpr auto mem : define_static_array(hashable_members<T>()))
    {
        hash_append(algo, obj.[:mem:]);
    }
}

// ----- 用户类型，用 [[=hash_skip]] 标记不需要哈希的成员 -----
struct Product
{
    int id;
    std::string name;
    [[= hash_skip]] double cached_price; // 使用 P3394R4 语法
    std::string description;
};

// ----- 测试 -----
int main()
{
    Product p1{100, "Widget", 19.99, "A nice widget"};
    Product p2{100, "Widget", 999.99, "A nice widget"}; // 仅缓存不同

    size_t h1 = 0, h2 = 0;
    hash_append(h1, p1);
    hash_append(h2, p2);

    std::cout << "Hash of p1: " << h1 << '\n';
    std::cout << "Hash of p2: " << h2 << '\n';
    std::cout << (h1 == h2 ? "Success" : "Failure") << '\n';
} // NOLINTEND