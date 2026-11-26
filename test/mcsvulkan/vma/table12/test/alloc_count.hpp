#pragma once
// ============================================================================
// table12/test/alloc_count.hpp — 分配计数（test-only，用于 0 分配断言）
//
//   替换全局 operator new/delete：转发到 malloc/free，只额外计数。
//   仅被 probes.hpp include（demo main 那个 TU），不进 production。
// ============================================================================

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace table::test
{
inline std::atomic<std::size_t> g_allocs{0};

[[nodiscard]] inline std::size_t allocs() noexcept
{
    return g_allocs.load(std::memory_order_relaxed);
}
inline void alloc_reset() noexcept
{
    g_allocs.store(0, std::memory_order_relaxed);
}
} // namespace table::test

inline void *operator new(std::size_t n)
{
    table::test::g_allocs.fetch_add(1, std::memory_order_relaxed);
    if (void *p = std::malloc(n == 0 ? 1 : n))
        return p;
    throw std::bad_alloc{};
}
inline void operator delete(void *p) noexcept
{
    std::free(p);
}
inline void operator delete(void *p, std::size_t) noexcept
{
    std::free(p);
}
inline void *operator new[](std::size_t n)
{
    return ::operator new(n);
}
inline void operator delete[](void *p) noexcept
{
    std::free(p);
}
inline void operator delete[](void *p, std::size_t) noexcept
{
    std::free(p);
}
