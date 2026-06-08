#include <bit>
#include <array>
#include <cassert>
#include <functional>
#include <iostream>
#include <cstring>
#include <string_view>
#include <vector>

// NOLINTBEGIN

// ========== 测试1：普通结构体，包含成员变量和成员函数 ==========
struct Data
{
    int x;
    double y; // 在 64 位下，由于对齐，总大小通常为 16 字节

    // 普通成员函数（非虚）
    void print() const
    {
        std::cout << "Data::print() -> x=" << x << ", y=" << y << '\n';
    }

    int compute() const
    {
        return static_cast<int>(x + y);
    }
};

static_assert(sizeof(Data) == 16, "Data must be 16 bytes");
static_assert(std::is_trivially_copyable_v<Data>);

struct runtime_object
{
    std::vector<std::array<std::byte, 16>> data;
    std::vector<std::function_ref<void(runtime_object *slef)> noexcept> functions;
    std::function_ref<bool(std::string_view) noexcept> functuon_infos;
};

void test_plain_struct()
{
    std::cout << "=== Test 1: Plain struct with members ===\n";

    // 1. 创建原始对象
    Data original{42, 3.14};
    original.print();

    // 2. 对象 -> 字节数组 (std::array<std::byte, 16>)
    auto bytes = std::bit_cast<std::array<std::byte, 16>>(original);

    // 3. 字节数组 -> 对象（全新对象，状态完全相同）
    Data restored = std::bit_cast<Data>(bytes);

    // 4. 检查成员变量
    std::cout << "Restored: ";
    restored.print();
    assert(original.x == restored.x);
    assert(original.y == restored.y);
    std::cout << "compute(): " << restored.compute() << '\n';

    // 5. 修改原始对象不影响已独立的字节数组
    original.x = 999;
    auto bytes2 = std::bit_cast<std::array<std::byte, 16>>(original);
    Data another = std::bit_cast<Data>(bytes2);
    assert(another.x == 999);
    std::cout << "Round-trip successful, no aliasing issues.\n\n";
}

// ========== 测试2：包含函数指针和数据指针的结构（模拟闭包） ==========
// 16 字节：一个函数指针 + 一个数据指针
struct Callable
{
    void (*func)(int, void *); // 函数指针 8 字节
    void *context;             // 上下文数据指针 8 字节

    // 便利调用接口
    void run(int val) const
    {
        if (func)
            func(val, context);
    }
};

static_assert(sizeof(Callable) == 16);
static_assert(std::is_trivially_copyable_v<Callable>);

// 一个具体的回调函数
void my_callback(int val, void *ctx)
{
    int *p = static_cast<int *>(ctx);
    std::cout << "Callback called with val=" << val << ", *context=" << *p << '\n';
}

void test_function_pointer_struct()
{
    std::cout << "=== Test 2: Function pointer + data pointer ===\n";

    int context_data = 100;
    Callable original{&my_callback, &context_data};

    // 调用原始对象
    original.run(7);

    // 对象 -> 字节数组 -> 对象
    auto bytes = std::bit_cast<std::array<std::byte, 16>>(original);
    Callable restored = std::bit_cast<Callable>(bytes);

    // 验证成员变量（指针值）
    assert(restored.func == original.func);
    assert(restored.context == original.context);

    // 通过恢复后的对象调用成员函数（非虚，直接通过指针调用）
    std::cout << "Restored callable: ";
    restored.run(77); // 应正确调用 my_callback
    std::cout << '\n';
}

// ========== 测试3：无捕获 lambda 转为函数指针并嵌入结构 ==========
void test_lambda_like()
{
    std::cout << "=== Test 3: Lambda-like through function pointer ===\n";

    // 无捕获 lambda 可以转为函数指针
    auto lambda = [](int v, void *ctx) {
        double *d = static_cast<double *>(ctx);
        std::cout << "Lambda body: v=" << v << ", *d=" << *d << '\n';
    };

    double data = 3.1415;
    Callable obj{lambda, &data}; // lambda 转为函数指针

    // bit_cast 往返
    auto bytes = std::bit_cast<std::array<std::byte, 16>>(obj);
    Callable obj2 = std::bit_cast<Callable>(bytes);

    obj2.run(2024); // 完全正常
    std::cout << '\n';
}

// ========== 测试4：直接将字节数组“绑定”为对象（反序列化场景） ==========
void test_direct_binding()
{
    std::cout << "=== Test 4: Direct binding of byte array to object ===\n";

    // 模拟从网络/文件读取的 16 字节数据
    std::array<std::byte, 16> raw_bytes;

    // 制造一个有效的 Data 对象表示
    Data sample{-1, 2.71828};
    raw_bytes = std::bit_cast<decltype(raw_bytes)>(sample);

    // 直接将这段字节解释为 Data 并调用成员函数
    Data rebound = std::bit_cast<Data>(raw_bytes);
    rebound.print(); // 输出: -1, 2.71828
    std::cout << "Rebound compute: " << rebound.compute() << '\n';
    std::cout << '\n';
}

// ========== 测试5：极端情况——全零字节仍是有效状态 ==========
void test_zero_initialized()
{
    std::cout << "=== Test 5: Zero-initialized byte array ===\n";

    std::array<std::byte, 16> zeros{}; // 全部为零

    // 对于平凡类型，全零位模式是合法值（例如 int=0, double=0.0）
    Data zero_obj = std::bit_cast<Data>(zeros);
    zero_obj.print(); // x=0, y=0
    assert(zero_obj.x == 0 && zero_obj.y == 0.0);
    std::cout << "Zero state is valid.\n\n";
}

int main()
{
    test_plain_struct();
    test_function_pointer_struct();
    test_lambda_like();
    test_direct_binding();
    test_zero_initialized();
    std::cout << "All tests passed. std::bit_cast round-trip is safe.\n";
    return 0;
} // NOLINTEND