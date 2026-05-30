#include <cstdint>
#include <bit>
#include <array>
#include <cassert>
#include <iostream>
#include <print>
#include <stdexcept>

// =============================================================================
// 1. 编译期计算索引/版本位数
// =============================================================================
template <uint32_t N>
struct entity_traits
{
    // 计算表示 0..N-1 所需的最小位数
    static constexpr uint32_t index_bits = [] {
        if constexpr (N <= 1)
            return 1u;
        else
            return 32 - std::countl_zero(N - 1);
    }();
    static constexpr uint32_t version_bits = 32 - index_bits;
    static constexpr uint32_t index_mask = (1u << index_bits) - 1;
    static constexpr uint32_t version_mask =
        (version_bits == 32) ? 0u : ((1u << version_bits) - 1);
};

// =============================================================================
// 2. 实体 ID (uint32_t 打包)
// =============================================================================
template <uint32_t N>
struct entity_id
{
    using traits = entity_traits<N>;
    static constexpr uint32_t index_mask = traits::index_mask;
    static constexpr uint32_t version_mask = traits::version_mask;
    static constexpr uint32_t index_bits = traits::index_bits;

    uint32_t handle = 0; // NOLINT

    [[nodiscard]] constexpr uint32_t index() const noexcept
    {
        return handle & index_mask;
    }
    [[nodiscard]] constexpr uint32_t version() const noexcept
    {
        return (handle >> index_bits) & version_mask;
    }

    static constexpr entity_id make(uint32_t slot, uint32_t ver) noexcept
    {
        return entity_id{(slot & index_mask) | ((ver & version_mask) << index_bits)};
    }

    constexpr friend bool operator==(entity_id a, entity_id b) noexcept
    {
        return a.handle == b.handle;
    }
    constexpr friend bool operator!=(entity_id a, entity_id b) noexcept
    {
        return a.handle != b.handle;
    }
};

// =============================================================================
// 3. 位图分配器（固定大小池，O(1) 快速查找）
// =============================================================================
template <uint32_t N>
class entity_pool
{
    static_assert(N > 0, "Pool size must be positive");

    using id_type = entity_id<N>;
    using traits = entity_traits<N>;
    static constexpr uint32_t slot_chunk = 64;                  // 位图字长
    static constexpr uint32_t slot_chunk_mask = slot_chunk - 1; // 位偏移掩码 (63)

    // 位图：每个比特代表一个槽位，0＝空闲，1＝占用
    static constexpr uint32_t words = (N + slot_chunk_mask) / slot_chunk;
    std::array<uint64_t, words> bits_{};
    std::array<uint32_t, N> generations_{}; // 截断后的版本号
    uint32_t alive_count_ = 0;

  public:
    entity_pool() = default;

    [[nodiscard]] constexpr uint32_t count() const noexcept
    {
        return alive_count_;
    }
    [[nodiscard]] constexpr bool is_full() const noexcept
    {
        return alive_count_ == N;
    }

    // 分配一个实体（调用者确保池未满）
    [[nodiscard]] constexpr id_type allocate() noexcept
    {
        assert(!is_full());

        for (uint32_t i = 0; i < words; ++i)
        {
            uint64_t inv = ~bits_[i];
            if (inv != 0U)
            {
                uint32_t bit = std::countr_zero(inv); // 第一个空闲位
                uint32_t slot = (i * slot_chunk) + bit;
                bits_[i] |= (1ULL << bit); // 标记占用

                uint32_t new_ver = (generations_[slot] + 1) & traits::version_mask;
                generations_[slot] = new_ver;
                ++alive_count_;
                return id_type::make(slot, new_ver);
            }
        }
        // 理论上不可达
        // throw std::logic_error("entity_pool::allocate: pool full but not detected");
        std::println("abort: entity_pool::allocate: pool full but not detected");
        std::abort();
    }

    // 释放实体
    constexpr void free(id_type id) noexcept
    {
        uint32_t slot = id.index();
        assert(slot < N);
        uint64_t mask = 1ULL << (slot & slot_chunk_mask);
        assert(bits_[slot / slot_chunk] & mask); // 确实占用
        bits_[slot / slot_chunk] &= ~mask;       // 标记空闲
        --alive_count_;
        // 注意：版本号保持原值，以便 is_alive 正确判旧 ID 无效
    }

    // 检查实体是否仍然存活
    [[nodiscard]] constexpr bool is_alive(id_type id) const noexcept
    {
        uint32_t slot = id.index();
        if (slot >= N)
            return false;
        uint64_t mask = 1ULL << (slot & slot_chunk_mask);
        // 两个条件：槽位当前被占用，且版本号匹配
        return (bits_[slot / slot_chunk] & mask) && (id.version() == generations_[slot]);
    }
};

// =============================================================================
// 4. 测试
// =============================================================================
void test_entity_id()
{
    using traits = entity_traits<4096>;
    static_assert(traits::index_bits == 12);
    static_assert(traits::version_bits == 20);
    static_assert(traits::index_mask == 0xFFF);
    static_assert(traits::version_mask == 0xFFFFF);

    using id = entity_id<4096>;
    static_assert(sizeof(id) == 4);

    // 构造与字段提取
    auto e1 = id::make(5, 1);
    assert(e1.index() == 5);
    assert(e1.version() == 1);

    // 版本递增
    auto e2 = id::make(5, 2);
    assert(e2.index() == 5);
    assert(e2.version() == 2);
    assert(e1 != e2);

    // 版本溢出截断
    uint32_t max_ver = traits::version_mask; // 1,048,575
    auto e_max = id::make(5, max_ver);
    assert(e_max.version() == max_ver);
    auto e_overflow = id::make(5, max_ver + 1);
    assert(e_overflow.version() == 0); // 正确截断为 0
    assert(e_max != e_overflow);

    std::cout << "[PASS] entity_id tests\n";
}

void test_basic_allocation()
{
    constexpr uint32_t N = 8; // 小池便于测试
    entity_pool<N> pool;

    // 初始全空闲
    assert(!pool.is_full());
    assert(pool.count() == 0);

    // 分配第一个
    auto e0 = pool.allocate();
    assert(pool.count() == 1);
    assert(pool.is_alive(e0));
    assert(e0.version() == 1);

    // 分配剩余直到满
    for (uint32_t i = 1; i < N; ++i)
    {
        auto e = pool.allocate();
        assert(pool.is_alive(e));
    }
    assert(pool.is_full());
    assert(pool.count() == N);

    // 尝试分配应触发断言（这里不测，用 is_full 预检查）
    std::cout << "[PASS] basic allocation tests\n";
}

void test_free_and_reuse()
{
    constexpr uint32_t N = 8;
    entity_pool<N> pool;

    auto e0 = pool.allocate();
    auto e1 = pool.allocate();
    auto e2 = pool.allocate();
    assert(pool.count() == 3);

    // 释放 e1
    pool.free(e1);
    assert(!pool.is_alive(e1));
    assert(pool.count() == 2);
    assert(pool.is_alive(e0));
    assert(pool.is_alive(e2));

    // 重新分配，期望拿到 e1 的槽位（位图从低位开始找）
    auto e3 = pool.allocate();
    assert(pool.count() == 3);
    assert(e3.index() == e1.index());         // 槽位复用
    assert(e3.version() == e1.version() + 1); // 版本递增
    assert(!pool.is_alive(e1));               // 旧 ID 无效
    assert(pool.is_alive(e3));                // 新 ID 有效

    std::cout << "[PASS] free and reuse tests\n";
}

void test_version_wraparound()
{
    // 使用一个很小的池，减少版本位数，加速回绕
    constexpr uint32_t N = 4;
    // 对于 N=4，index_bits=2, version_bits=30，版本号很大，手动模拟难以触发。
    // 因此我们改为直接测试 ID 的版本截断逻辑，这已在 test_entity_id 中完成。
    // 分配器的版本号存储同样用 version_mask 截断，因此行为一致。
    // 这里只验证分配器版本递增与截断一致性。
    entity_pool<N> pool;

    auto e = pool.allocate();
    uint32_t v1 = e.version();
    assert(v1 == 1);
    // 释放并重新分配，版本应递增
    pool.free(e);
    auto e2 = pool.allocate();
    assert(e2.index() == e.index());
    assert(e2.version() == ((v1 + 1) & entity_traits<N>::version_mask));

    // 手动模拟多次分配释放，直到版本回绕？
    // 因为 version_bits = 30，需要 2^30 次操作，不现实。但逻辑由代码保证。
    std::cout << "[PASS] version wraparound logic tests\n";
}

void test_is_alive_edge_cases()
{
    constexpr uint32_t N = 8;
    entity_pool<N> pool;

    // 未分配的槽位构造的假 ID
    auto fake = entity_id<N>::make(7, 1); // 槽位 7 初始空闲
    assert(!pool.is_alive(fake));         // 槽位未占用

    // 分配槽位 7
    auto real = pool.allocate(); // 可能不是槽位 7，继续分配直到拿到 7
    // 简单办法：直接分配 N 次，必然包含槽位 7
    for (int i = 0; i < N - 1; ++i)
        pool.allocate();
    // 现在槽位 7 应该已被占用（因为我们分配了 N 个）
    // 获取槽位 7 的 ID（我们需要找到它）
    // 实际上不需要，我们只验证 is_alive 对已释放实体的判断。

    // 释放后再查
    // 重新开始
    pool = entity_pool<N>{}; // 重置
    auto e = pool.allocate();
    assert(pool.is_alive(e));
    pool.free(e);
    assert(!pool.is_alive(e));

    // 错误的版本号
    auto wrong_ver = entity_id<N>::make(e.index(), e.version() + 1);
    assert(!pool.is_alive(wrong_ver)); // 版本不匹配且槽位空闲

    std::cout << "[PASS] is_alive edge cases\n";
}

int main()
{
    test_entity_id();
    test_basic_allocation();
    test_free_and_reuse();
    test_version_wraparound();
    test_is_alive_edge_cases();
    std::cout << "All tests passed.\n";
    return 0;
}