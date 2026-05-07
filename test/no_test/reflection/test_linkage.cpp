#include <iostream>
#include <meta>
using namespace std::meta;

// 外部链接
int globalVar;
void globalFunc() {}
extern "C" void cFunc() {}

// 内部链接
static int internalVar;
static void internalFunc() {}
namespace
{
    int anonVar;
} // namespace

// 类成员
struct S
{
    static int staticMem; // 外部链接（若 S 有外部链接）
    int nonStaticMem;     // 无链接
    static void staticFunc();
};
int S::staticMem = 0;

// -------- has_external_linkage --------
static_assert(has_external_linkage(^^globalVar));
static_assert(has_external_linkage(^^globalFunc));
static_assert(has_external_linkage(^^cFunc)); // C 链接也是外部链接
static_assert(!has_external_linkage(^^internalVar));
static_assert(!has_external_linkage(^^anonVar));

// -------- has_internal_linkage --------
static_assert(has_internal_linkage(^^internalVar));
static_assert(has_internal_linkage(^^internalFunc));
static_assert(has_internal_linkage(^^anonVar));
static_assert(!has_internal_linkage(^^globalVar));

// -------- has_c_language_linkage --------
static_assert(has_c_language_linkage(^^cFunc));
static_assert(!has_c_language_linkage(^^globalFunc));
static_assert(!has_c_language_linkage(^^globalVar));

// -------- has_linkage --------
static_assert(has_linkage(^^globalVar));
static_assert(has_linkage(^^internalVar));
static_assert(has_linkage(^^cFunc));
static_assert(has_linkage(^^S::staticMem));     // 静态成员有链接
static_assert(!has_linkage(^^int));             // 基本类型无链接
static_assert(!has_linkage(^^S::nonStaticMem)); // 非静态成员无链接
static_assert(has_linkage(^^S));                // 自定义类信息有连接

// -------- has_module_linkage --------
// 未使用模块，所有实体不应具有模块链接
static_assert(!has_module_linkage(^^globalVar));
static_assert(!has_module_linkage(^^internalVar));
static_assert(!has_module_linkage(^^cFunc));

int main()
{
    std::cout << "main done\n";
    return 0;
}