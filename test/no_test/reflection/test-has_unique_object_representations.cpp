#include <meta>

namespace meta = std::meta;

// NOTE: 该 API 考察的是类型的对象表示是否与其值一一对应（即每个位组合都对应一个合法值，没有陷阱表示、没有填充位、也没有因符号零/NaN 等导致的多对一映射）。
// 辅助类型：无填充位/无额外位表示
struct UniqueRep
{
    unsigned char a;
    unsigned char b;
    // 无填充
};

static_assert(sizeof(UniqueRep) == 2); // 保证紧凑
// 注意：该结构体是否有唯一对象表示？成员是 unsigned char，没有填充字节，所有值位参与表示
static_assert(meta::has_unique_object_representations(^^unsigned char));

static_assert(meta::has_unique_object_representations(^^UniqueRep));

// 反例
static_assert(meta::has_unique_object_representations(^^bool));
static_assert(meta::has_unique_object_representations(^^int)); // 可能有填充位或符号陷阱
static_assert(!meta::has_unique_object_representations(^^double));
static_assert(!meta::has_unique_object_representations(^^void));
static_assert(!meta::has_unique_object_representations(^^int &));
#include <iostream>
#include <exception>

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