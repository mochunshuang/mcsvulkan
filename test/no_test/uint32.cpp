#include <assert.h>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <cassert>
#include <climits>
#include <bitset>

// NOLINTBEGIN
struct object_key
{
    uint32_t object_type : 8;     // 假设高 8 位
    uint32_t instance_index : 24; // 低 24 位

    static consteval uint32_t max_type_value(int bit_with) noexcept
    {
        return (1u << bit_with) - 1; // 255
    }

    constexpr object_key(uint32_t type, uint32_t index) noexcept
        : object_type{type}, instance_index{index}
    {
        assert(type <= max_type_value(8));
        assert(type <= max_type_value(24));
    }
    constexpr object_key() : object_key(0, 0) {}

    // 显式组合位，不依赖内存布局
    constexpr operator uint32_t() const noexcept
    {
        return (static_cast<uint32_t>(object_type) << 24) | (instance_index & 0xFFFFFFu);
    }
};

// 辅助打印
void print_key(const char *name, const object_key &k)
{
    uint32_t packed = k;
    std::cout << std::left << std::setw(12) << name
              << "  type=" << static_cast<unsigned>(k.object_type) << "  index=0x"
              << std::hex << std::setw(6) << std::setfill('0') << k.instance_index
              << std::dec << "  packed=0x" << std::hex << std::setw(8) << packed
              << std::dec << std::endl;
}

int main()
{
    std::cout << "=== 1. 正常值测试 ===" << std::endl;
    object_key k1{0x12, 0x345678};
    print_key("k1", k1);
    assert(static_cast<uint32_t>(k1) == 0x12345678u);

    // 边界值
    object_key k_max{0xFF, 0xFFFFFF}; // 全1
    object_key k_min{0x00, 0x000000}; // 全0
    assert(static_cast<uint32_t>(k_max) == 0xFFFFFFFFu);
    assert(static_cast<uint32_t>(k_min) == 0x00000000u);
    assert(k_min == 0x00000000u);
    print_key("k_max", k_max);
    print_key("k_min", k_min);

    // ---- 2. 超出范围赋值 ----
    // 这是你最关心的：是未定义行为吗？
    // C++ 标准说：赋值时若值超出位域可表示范围，行为是 implementation-defined，
    // 但 NOT undefined behavior。绝大多数实现会截断到低位。
    std::cout << "\n=== 2. 超出范围赋值 (实现定义，预期截断) ===" << std::endl;
    object_key k_overflow{};

    // instance_ndex 赋值 0x1ABCDEF (超过 24 位)
    k_overflow.instance_index = 0x1ABCDEF; // 低24位 = 0xABCDEF
    // object_type 赋值 0x123 (超过 8 位)
    k_overflow.object_type = 0x123; // 低8位 = 0x23

    print_key("k_overflow", k_overflow);
    // 检查是否截断
    assert(k_overflow.instance_index == (0x1ABCDEF & 0xFFFFFFu));
    assert(k_overflow.object_type == (0x123 & 0xFFu));
    // 最终打包结果
    assert(static_cast<uint32_t>(k_overflow) ==
           ((0x23u << 24) | (0x1ABCDEFu & 0xFFFFFFu)));

    // ---- 3. 复制构造 & 复制赋值 ----
    std::cout << "\n=== 3. 复制语义 ===" << std::endl;
    object_key k_copy(k1); // 复制构造
    object_key k_assign{};
    k_assign = k1; // 复制赋值
    print_key("k_copy", k_copy);
    print_key("k_assign", k_assign);
    assert(static_cast<uint32_t>(k_copy) == static_cast<uint32_t>(k1));
    assert(static_cast<uint32_t>(k_assign) == static_cast<uint32_t>(k1));

    // ---- 4. 移动构造 & 移动赋值 ----
    std::cout << "\n=== 4. 移动语义 ===" << std::endl;
    object_key k_move_ctor(std::move(k1)); // 移动构造（等价于复制）
    print_key("k1 (after move)", k1);      // 原对象仍然完整
    print_key("k_move_ctor", k_move_ctor);
    assert(static_cast<uint32_t>(k1) == static_cast<uint32_t>(k_move_ctor));

    object_key k_move_assign{};
    k_move_assign = std::move(k_copy); // 移动赋值
    print_key("k_copy (after move)", k_copy);
    print_key("k_move_assign", k_move_assign);
    assert(static_cast<uint32_t>(k_copy) == static_cast<uint32_t>(k_move_assign));

    // 对于这种平凡类型，移动 == 复制，源对象不变，不存在“移动后未定义”的问题。

    // ---- 5. 验证我们显式组合不会意外依赖内存布局 ----
    std::cout << "\n=== 5. 布局独立性验证 ===" << std::endl;
    // 即使编译器把 object_type 放在低位内存（比如小端），
    // 我们的 operator uint32_t() 通过移位强制保证了：高8位是 type，低24位是 index。
    // 你可以用 std::bit_cast 对比（但位域布局是实现定义的，我们不做这种对比）

    object_key k_layout{0xAB, 0x654321};
    uint32_t expected = (0xABu << 24) | 0x654321u;
    uint32_t actual = k_layout;
    assert(actual == expected);
    std::cout << "Manual packing is independent of bit-field memory order.\n";

    std::cout << "\nAll tests passed.\n";
    return 0;
} // NOLINTEND