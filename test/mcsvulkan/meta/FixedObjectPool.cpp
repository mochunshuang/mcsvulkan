#include <array>
#include <cstddef>
#include <iostream>
#include <new>
#include <type_traits>
#include <utility>

// ============================================================================
// 通用固定大小对象池（修正版）
// ============================================================================

template <typename T, std::size_t N>
class FixedObjectPool
{
    static_assert(!std::is_abstract_v<T>, "T cannot be abstract");
    static_assert(std::is_destructible_v<T>, "T must be destructible");

    // 每个槽位独立对齐，避免未定义行为
    using Storage = std::aligned_storage_t<sizeof(T), alignof(T)>;

  public:
    FixedObjectPool() = default;

    ~FixedObjectPool()
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            if (used_[i])
            {
                reinterpret_cast<T *>(&storage_[i])->~T();
                used_[i] = false;
            }
        }
    }

    // 禁用拷贝和移动
    FixedObjectPool(const FixedObjectPool &) = delete;
    FixedObjectPool &operator=(const FixedObjectPool &) = delete;
    FixedObjectPool(FixedObjectPool &&) = delete;
    FixedObjectPool &operator=(FixedObjectPool &&) = delete;

    template <typename... Args>
    T *acquire(Args &&...args)
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            if (!used_[i])
            {
                // placement new 在正确对齐的内存上构造对象
                T *obj = new (&storage_[i]) T(std::forward<Args>(args)...);
                used_[i] = true;
                return obj;
            }
        }
        return nullptr; // 池满
    }

    void release(T *ptr)
    {
        if (!ptr)
            return;

        // 检查指针是否属于本池
        auto *base = reinterpret_cast<const unsigned char *>(&storage_[0]);
        auto *p = reinterpret_cast<const unsigned char *>(ptr);
        if (p < base || p >= base + sizeof(storage_))
        {
            return; // 不属于本池
        }

        std::size_t index = (p - base) / sizeof(Storage);
        if (index >= N || !used_[index])
        {
            return; // 无效指针或未使用
        }

        // 显式调用析构（虚析构会正确派发）
        ptr->~T();
        used_[index] = false;
    }

    std::size_t capacity() const noexcept
    {
        return N;
    }

    std::size_t available() const noexcept
    {
        std::size_t count = 0;
        for (bool used : used_)
            if (!used)
                ++count;
        return count;
    }

    // 用于测试：返回第 i 个槽位的地址
    void *slotAddress(std::size_t i) const
    {
        return const_cast<void *>(static_cast<const void *>(&storage_[i]));
    }

  private:
    std::array<Storage, N> storage_{};
    std::array<bool, N> used_{};
};

// ============================================================================
// 测试用的多态类层次结构
// ============================================================================

struct Base
{
    int baseValue = 0;

    Base(int v = 0) : baseValue(v)
    {
        ++baseConstructorCount;
    }

    virtual ~Base()
    {
        ++baseDestructorCount;
    }

    virtual int getType() const
    {
        return 0;
    }

    virtual int compute(int x) const
    {
        return baseValue + x;
    }

    static int baseConstructorCount;
    static int baseDestructorCount;
};

int Base::baseConstructorCount = 0;
int Base::baseDestructorCount = 0;

struct DerivedA : public Base
{
    int aValue = 10;

    DerivedA(int base = 0, int a = 10) : Base(base), aValue(a)
    {
        ++derivedAConstructorCount;
    }

    ~DerivedA() override
    {
        ++derivedADestructorCount;
    }

    int getType() const override
    {
        return 1;
    }

    int compute(int x) const override
    {
        return baseValue + aValue + x;
    }

    static int derivedAConstructorCount;
    static int derivedADestructorCount;
};

int DerivedA::derivedAConstructorCount = 0;
int DerivedA::derivedADestructorCount = 0;

struct DerivedB : public Base
{
    int bValue = 20;

    DerivedB(int base = 0, int b = 20) : Base(base), bValue(b)
    {
        ++derivedBConstructorCount;
    }

    ~DerivedB() override
    {
        ++derivedBDestructorCount;
    }

    int getType() const override
    {
        return 2;
    }

    int compute(int x) const override
    {
        return baseValue + bValue + x;
    }

    static int derivedBConstructorCount;
    static int derivedBDestructorCount;
};

int DerivedB::derivedBConstructorCount = 0;
int DerivedB::derivedBDestructorCount = 0;

// ============================================================================
// 辅助函数
// ============================================================================

void resetCounters()
{
    Base::baseConstructorCount = 0;
    Base::baseDestructorCount = 0;
    DerivedA::derivedAConstructorCount = 0;
    DerivedA::derivedADestructorCount = 0;
    DerivedB::derivedBConstructorCount = 0;
    DerivedB::derivedBDestructorCount = 0;
}

// ============================================================================
// 测试用例（每个测试独立，重置所需计数器）
// ============================================================================

// 测试1：验证每个槽位地址对齐到 alignof(T)
bool test_slot_alignment()
{
    FixedObjectPool<DerivedA, 5> pool;

    for (std::size_t i = 0; i < pool.capacity(); ++i)
    {
        void *addr = pool.slotAddress(i);
        auto addrValue = reinterpret_cast<std::uintptr_t>(addr);
        if (addrValue % alignof(DerivedA) != 0)
        {
            std::cerr << "FAIL [alignment]: slot " << i << " is not aligned, address=0x"
                      << std::hex << addrValue << std::dec << "\n";
            return false;
        }
    }

    std::cout << "PASS [alignment]: all slot addresses are properly aligned\n";
    return true;
}

// 测试2：虚函数在对象池中正常工作，且构造/析构计数正确
bool test_virtual_functions_work()
{
    resetCounters();
    {
        FixedObjectPool<DerivedA, 3> pool;

        DerivedA *a = pool.acquire(5, 15);
        DerivedA *b = pool.acquire(7, 25);

        if (!a || !b)
        {
            std::cerr << "FAIL [virtual_functions]: acquire returned nullptr\n";
            return false;
        }

        // 通过基类指针调用虚函数
        Base *baseA = a;
        Base *baseB = b;

        if (baseA->getType() != 1 || baseB->getType() != 1)
        {
            std::cerr << "FAIL [virtual_functions]: getType() incorrect\n";
            return false;
        }

        if (baseA->compute(10) != 5 + 15 + 10 || baseB->compute(20) != 7 + 25 + 20)
        {
            std::cerr << "FAIL [virtual_functions]: compute() incorrect\n";
            return false;
        }

        // 构造次数正确
        if (DerivedA::derivedAConstructorCount != 2 || Base::baseConstructorCount != 2)
        {
            std::cerr << "FAIL [virtual_functions]: constructor count incorrect\n";
            return false;
        }

        // 手动释放一个对象
        pool.release(a);
        if (DerivedA::derivedADestructorCount != 1 || Base::baseDestructorCount != 1)
        {
            std::cerr
                << "FAIL [virtual_functions]: destructor count after release incorrect\n";
            return false;
        }

        // 释放另一个
        pool.release(b);
        if (DerivedA::derivedADestructorCount != 2 || Base::baseDestructorCount != 2)
        {
            std::cerr << "FAIL [virtual_functions]: destructor count after second "
                         "release incorrect\n";
            return false;
        }
    } // 池析构时，所有对象都已释放，不应再调用析构

    if (Base::baseDestructorCount != 2 || DerivedA::derivedADestructorCount != 2)
    {
        std::cerr << "FAIL [virtual_functions]: destructor count after pool destruction "
                     "incorrect\n";
        return false;
    }

    return true;
}

// 测试3：对象池重用内存，且多态行为正确
bool test_pool_reuse()
{
    resetCounters();
    FixedObjectPool<DerivedB, 2> pool;

    DerivedB *b1 = pool.acquire(100, 200);
    if (!b1)
    {
        std::cerr << "FAIL [reuse]: first acquire failed\n";
        return false;
    }

    Base *base1 = b1;
    if (base1->getType() != 2 || base1->compute(1) != 100 + 200 + 1)
    {
        std::cerr << "FAIL [reuse]: virtual call on first object incorrect\n";
        return false;
    }

    // 释放对象
    pool.release(b1);
    if (DerivedB::derivedBDestructorCount != 1)
    {
        std::cerr << "FAIL [reuse]: destructor not called on release\n";
        return false;
    }

    // 再次获取，应重用同一块内存（地址相同）
    DerivedB *b2 = pool.acquire(300, 400);
    if (!b2)
    {
        std::cerr << "FAIL [reuse]: second acquire failed\n";
        return false;
    }

    Base *base2 = b2;
    if (base2->getType() != 2 || base2->compute(2) != 300 + 400 + 2)
    {
        std::cerr << "FAIL [reuse]: virtual call on reused object incorrect\n";
        return false;
    }

    if (reinterpret_cast<void *>(b1) != reinterpret_cast<void *>(b2))
    {
        std::cerr
            << "WARN [reuse]: reused object address differs (expected same address)\n";
    }

    // 清理
    pool.release(b2);
    return true;
}

// 测试4：池析构时自动销毁仍然存活的对象
bool test_pool_destructor_cleans_up()
{
    resetCounters();
    {
        FixedObjectPool<DerivedA, 3> pool;
        auto *a1 = pool.acquire(1, 2);
        auto *a2 = pool.acquire(3, 4);
        auto *a3 = pool.acquire(5, 6);

        if (!a1 || !a2 || !a3)
        {
            std::cerr << "FAIL [pool_destructor]: acquire failed\n";
            return false;
        }

        if (DerivedA::derivedAConstructorCount != 3)
        {
            std::cerr << "FAIL [pool_destructor]: constructor count incorrect\n";
            return false;
        }
    } // 池离开作用域，自动析构所有存活对象

    if (DerivedA::derivedADestructorCount != 3)
    {
        std::cerr << "FAIL [pool_destructor]: pool destructor did not call destructors "
                     "correctly\n";
        return false;
    }
    if (Base::baseDestructorCount != 3)
    {
        std::cerr << "FAIL [pool_destructor]: base destructor count incorrect\n";
        return false;
    }
    return true;
}

// 测试5：池满时 acquire 返回 nullptr，释放后可再次获取
bool test_pool_full()
{
    FixedObjectPool<Base, 2> pool;

    auto *a = pool.acquire(10);
    auto *b = pool.acquire(20);
    auto *c = pool.acquire(30); // 应失败

    if (!a || !b)
    {
        std::cerr << "FAIL [pool_full]: first acquisitions should succeed\n";
        return false;
    }
    if (c != nullptr)
    {
        std::cerr
            << "FAIL [pool_full]: acquire should return nullptr when pool is full\n";
        return false;
    }

    pool.release(a);
    c = pool.acquire(40); // 应成功
    if (!c)
    {
        std::cerr << "FAIL [pool_full]: acquire after release should succeed\n";
        return false;
    }

    pool.release(b);
    pool.release(c);
    return true;
}

// 测试6：验证 release 对不属于本池的指针不做任何操作（安全性）
bool test_release_invalid_pointer()
{
    resetCounters(); // 重要：独立测试，重置计数器
    FixedObjectPool<DerivedA, 2> pool;
    auto *a = pool.acquire(1, 2);
    if (!a)
    {
        std::cerr << "FAIL [invalid_pointer]: acquire failed\n";
        return false;
    }

    // 创建一个栈上对象，试图释放到池中
    DerivedA stackObj(10, 20);
    pool.release(&stackObj); // 应无操作

    // 释放有效指针，验证计数
    pool.release(a);
    if (DerivedA::derivedADestructorCount != 1 || Base::baseDestructorCount != 1)
    {
        std::cerr << "FAIL [invalid_pointer]: destructor count incorrect after release\n";
        return false;
    }

    // 验证池状态
    if (pool.available() != 2)
    {
        std::cerr << "FAIL [invalid_pointer]: available count incorrect\n";
        return false;
    }
    return true;
}

// ============================================================================
// 运行所有测试
// ============================================================================

bool runTests()
{
    return test_slot_alignment() && test_virtual_functions_work() && test_pool_reuse() &&
           test_pool_destructor_cleans_up() && test_pool_full() &&
           test_release_invalid_pointer();
}

int main()
{
    if (runTests())
    {
        std::cout << "All object pool with virtual functions tests passed!\n";
        return 0;
    }
    return 1;
}