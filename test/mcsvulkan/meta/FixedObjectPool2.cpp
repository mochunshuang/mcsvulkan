#include <array>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#include <bitset>

// ============================================================================
// 自定义对齐存储
// ============================================================================
template <typename T>
struct AlignedStorage
{
    alignas(T) unsigned char data[sizeof(T)];
};

// ============================================================================
// 块式对象池（线程不安全，若需线程安全请使用外部锁包装）
// ============================================================================
template <typename T, std::size_t BlockSize = 64>
class ChunkedObjectPool
{
    static_assert(!std::is_abstract_v<T>, "T cannot be abstract");
    static_assert(std::is_destructible_v<T>, "T must be destructible");

    struct Block
    {
        std::array<AlignedStorage<T>, BlockSize> slots;
        std::bitset<BlockSize> used; // 标记槽位是否占用
    };

  public:
    ChunkedObjectPool()
    {
        addBlock();
    }

    ~ChunkedObjectPool()
    {
        // 析构所有仍存活的对象并释放块内存
        for (Block *block : blocks_)
        {
            for (std::size_t i = 0; i < BlockSize; ++i)
            {
                if (block->used.test(i))
                {
                    T *obj = reinterpret_cast<T *>(&block->slots[i]);
                    obj->~T();
                }
            }
            delete block;
        }
    }

    // 禁用拷贝和移动（池本身不可复制/移动）
    ChunkedObjectPool(const ChunkedObjectPool &) = delete;
    ChunkedObjectPool &operator=(const ChunkedObjectPool &) = delete;
    ChunkedObjectPool(ChunkedObjectPool &&) = delete;
    ChunkedObjectPool &operator=(ChunkedObjectPool &&) = delete;

    // 从池中分配对象，返回裸指针。若 T 构造抛出异常，槽位自动回滚。
    template <typename... Args>
    T *acquire(Args &&...args)
    {
        if (freeSlots_.empty())
        {
            addBlock();
        }

        std::size_t globalIndex = freeSlots_.back();
        freeSlots_.pop_back();

        Block *block = blocks_[globalIndex / BlockSize];
        std::size_t localIndex = globalIndex % BlockSize;

        T *obj = nullptr;
        try
        {
            // placement new 构造对象
            obj = new (&block->slots[localIndex]) T(std::forward<Args>(args)...);
            block->used.set(localIndex);
            return obj;
        }
        catch (...)
        {
            // 构造失败，槽位重新标记为空闲
            block->used.reset(localIndex);
            freeSlots_.push_back(globalIndex);
            throw;
        }
    }

    // 释放对象，返回是否成功。无效指针（不属于本池或未使用）返回 false。
    bool release(T *ptr) noexcept
    {
        if (!ptr)
            return false;

        // 查找所属块
        for (Block *block : blocks_)
        {
            auto *base = reinterpret_cast<unsigned char *>(&block->slots[0]);
            auto *p = reinterpret_cast<unsigned char *>(ptr);
            if (p >= base && p < base + sizeof(block->slots))
            {
                std::size_t localIndex = (p - base) / sizeof(AlignedStorage<T>);
                if (localIndex < BlockSize && block->used.test(localIndex))
                {
                    ptr->~T();
                    block->used.reset(localIndex);
                    std::size_t blockIndex = block - blocks_.front();
                    std::size_t globalIndex = blockIndex * BlockSize + localIndex;
                    freeSlots_.push_back(globalIndex);
                    return true;
                }
                break; // 指针在块内但未使用或越界，视为无效
            }
        }
        return false;
    }

    std::size_t capacity() const noexcept
    {
        return blocks_.size() * BlockSize;
    }

    std::size_t available() const noexcept
    {
        return freeSlots_.size();
    }

  private:
    void addBlock()
    {
        Block *block = new Block();
        std::size_t blockIndex = blocks_.size();
        blocks_.push_back(block);
        for (std::size_t i = 0; i < BlockSize; ++i)
        {
            freeSlots_.push_back(blockIndex * BlockSize + i);
        }
    }

    std::vector<Block *> blocks_;        // 所有块，地址稳定
    std::vector<std::size_t> freeSlots_; // 空闲槽位全局索引
};

// ============================================================================
// PooledUniquePtr：与 std::unique_ptr 无缝集成的智能指针
// ============================================================================
template <typename T, typename Pool>
class PooledUniquePtr
{
    struct PoolDeleter
    {
        Pool *pool;

        void operator()(T *ptr) const noexcept
        {
            pool->release(ptr); // 归还池，忽略返回值（因为池保证有效）
        }
    };

    std::unique_ptr<T, PoolDeleter> ptr_;

  public:
    // 从池中获取对象，若池分配失败或构造抛异常则抛出对应异常
    template <typename... Args>
    PooledUniquePtr(Pool &pool, Args &&...args)
        : ptr_(pool.acquire(std::forward<Args>(args)...), PoolDeleter{&pool})
    {
        if (!ptr_)
            throw std::bad_alloc();
    }

    // 移动构造和移动赋值
    PooledUniquePtr(PooledUniquePtr &&) noexcept = default;
    PooledUniquePtr &operator=(PooledUniquePtr &&) noexcept = default;

    // 禁用拷贝
    PooledUniquePtr(const PooledUniquePtr &) = delete;
    PooledUniquePtr &operator=(const PooledUniquePtr &) = delete;

    // 标准接口
    T *get() const noexcept
    {
        return ptr_.get();
    }
    T *operator->() const noexcept
    {
        return get();
    }
    T &operator*() const noexcept
    {
        return *get();
    }
    explicit operator bool() const noexcept
    {
        return static_cast<bool>(ptr_);
    }

    // 释放所有权，返回裸指针（不再由池管理，需调用者手动 delete 或忽略）
    T *release() noexcept
    {
        return ptr_.release();
    }

    // 重置为池中新建的对象（成功则替换，失败则保持原状或释放原对象？本实现失败时仅释放旧对象）
    template <typename... Args>
    void reset(Args &&...args)
    {
        auto *newPtr = ptr_.get_deleter().pool->acquire(std::forward<Args>(args)...);
        if (newPtr)
        {
            ptr_.reset(newPtr); // 释放旧对象（通过删除器归还池），接管新指针
        }
        else
        {
            ptr_.reset(); // 仅释放旧对象
        }
    }

    // 与 std::unique_ptr 转换
    std::unique_ptr<T, PoolDeleter> &get_unique_ptr() noexcept
    {
        return ptr_;
    }
    const std::unique_ptr<T, PoolDeleter> &get_unique_ptr() const noexcept
    {
        return ptr_;
    }
};

// ============================================================================
// 测试用多态类
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

// 用于异常测试的类
struct ThrowingData : public Base
{
    ThrowingData(bool shouldThrow) : Base(0)
    {
        if (shouldThrow)
        {
            throw std::runtime_error("constructor failed");
        }
        ++throwingConstructorCount;
    }

    static int throwingConstructorCount;
};

int ThrowingData::throwingConstructorCount = 0;

// ============================================================================
// 辅助函数
// ============================================================================
void resetCounters()
{
    Base::baseConstructorCount = 0;
    Base::baseDestructorCount = 0;
    DerivedA::derivedAConstructorCount = 0;
    DerivedA::derivedADestructorCount = 0;
    ThrowingData::throwingConstructorCount = 0;
}

// ============================================================================
// 测试用例
// ============================================================================
bool test_basic_allocation_and_virtual_calls()
{
    resetCounters();
    ChunkedObjectPool<DerivedA, 2> pool;

    auto p1 = PooledUniquePtr<DerivedA, decltype(pool)>(pool, 5, 15);
    auto p2 = PooledUniquePtr<DerivedA, decltype(pool)>(pool, 7, 25);

    if (!p1 || !p2)
    {
        std::cerr << "FAIL [basic]: acquire failed\n";
        return false;
    }

    Base *b1 = p1.get();
    Base *b2 = p2.get();

    if (b1->getType() != 1 || b2->getType() != 1)
    {
        std::cerr << "FAIL [basic]: getType() incorrect\n";
        return false;
    }
    if (b1->compute(10) != 5 + 15 + 10 || b2->compute(20) != 7 + 25 + 20)
    {
        std::cerr << "FAIL [basic]: compute() incorrect\n";
        return false;
    }

    if (DerivedA::derivedAConstructorCount != 2)
    {
        std::cerr << "FAIL [basic]: constructor count incorrect\n";
        return false;
    }

    // 离开作用域时 unique_ptr 自动归还，析构函数会被调用
    return true;
}

bool test_exception_safety()
{
    resetCounters();
    ChunkedObjectPool<ThrowingData, 2> pool;

    // 分配一个正常对象
    auto p1 = PooledUniquePtr<ThrowingData, decltype(pool)>(pool, false);
    if (!p1)
    {
        std::cerr << "FAIL [exception]: first allocation failed\n";
        return false;
    }
    std::size_t availAfter1 = pool.available();
    if (availAfter1 != 1)
    {
        std::cerr << "FAIL [exception]: available after first allocation incorrect\n";
        return false;
    }

    // 尝试分配一个会抛出异常的对象
    bool caught = false;
    try
    {
        auto p2 = PooledUniquePtr<ThrowingData, decltype(pool)>(pool, true);
    }
    catch (const std::runtime_error &)
    {
        caught = true;
    }

    if (!caught)
    {
        std::cerr << "FAIL [exception]: exception not caught\n";
        return false;
    }

    // 异常后池应保持可用，槽位被回滚
    if (pool.available() != 1)
    {
        std::cerr << "FAIL [exception]: available after failed constructor incorrect\n";
        return false;
    }

    // 再次分配正常对象应该成功
    auto p3 = PooledUniquePtr<ThrowingData, decltype(pool)>(pool, false);
    if (!p3)
    {
        std::cerr << "FAIL [exception]: re-allocation after exception failed\n";
        return false;
    }
    if (ThrowingData::throwingConstructorCount !=
        2) // 第一次成功 + 第三次成功（中间失败）
    {
        std::cerr << "FAIL [exception]: constructor count incorrect\n";
        return false;
    }

    return true;
}

bool test_move_semantics()
{
    resetCounters();
    ChunkedObjectPool<DerivedA, 2> pool;

    auto p1 = PooledUniquePtr<DerivedA, decltype(pool)>(pool, 1, 2);
    DerivedA *raw = p1.get();

    // 移动构造
    auto p2 = std::move(p1);
    if (p1.get() != nullptr)
    {
        std::cerr << "FAIL [move]: source pointer not null after move\n";
        return false;
    }
    if (p2.get() != raw)
    {
        std::cerr << "FAIL [move]: moved pointer incorrect\n";
        return false;
    }

    // 移动赋值
    auto p3 = PooledUniquePtr<DerivedA, decltype(pool)>(pool, 3, 4);
    p2 = std::move(p3);
    if (p3.get() != nullptr || p2.get() == raw)
    {
        std::cerr << "FAIL [move]: move assignment incorrect\n";
        return false;
    }

    // 释放 p2，确保旧对象 raw 被归还
    p2.reset();
    if (DerivedA::derivedADestructorCount != 1) // raw 对应的对象析构
    {
        std::cerr << "FAIL [move]: destructor count after reset incorrect\n";
        return false;
    }

    // 释放原始 raw（现在未被任何智能指针管理，需手动归还）
    pool.release(raw);
    if (DerivedA::derivedADestructorCount != 2)
    {
        std::cerr << "FAIL [move]: destructor count after manual release incorrect\n";
        return false;
    }

    return true;
}

bool test_expansion_preserves_old_pointers()
{
    resetCounters();
    ChunkedObjectPool<DerivedA, 2> pool;

    auto p1 = PooledUniquePtr<DerivedA, decltype(pool)>(pool, 1, 2);
    auto p2 = PooledUniquePtr<DerivedA, decltype(pool)>(pool, 3, 4);
    DerivedA *raw1 = p1.get();
    DerivedA *raw2 = p2.get();

    // 第三次分配触发扩容
    auto p3 = PooledUniquePtr<DerivedA, decltype(pool)>(pool, 5, 6);
    if (!p3)
    {
        std::cerr << "FAIL [expansion]: third allocation failed\n";
        return false;
    }

    if (p1.get() != raw1 || p2.get() != raw2)
    {
        std::cerr << "FAIL [expansion]: old pointer changed after expansion\n";
        return false;
    }

    if (raw1->compute(0) != 1 + 2 + 0 || raw2->compute(0) != 3 + 4 + 0)
    {
        std::cerr << "FAIL [expansion]: virtual call on old pointer incorrect\n";
        return false;
    }

    return true;
}

bool test_release_and_reuse()
{
    resetCounters();
    ChunkedObjectPool<DerivedA, 4> pool;

    auto p1 = PooledUniquePtr<DerivedA, decltype(pool)>(pool, 10, 20);
    DerivedA *addr1 = p1.get();
    p1.reset(); // 归还池

    auto p2 = PooledUniquePtr<DerivedA, decltype(pool)>(pool, 30, 40);
    if (!p2)
    {
        std::cerr << "FAIL [reuse]: re-acquire failed\n";
        return false;
    }

    // 地址可能相同（后进先出，但取决于实现，不做强制）
    if (reinterpret_cast<void *>(addr1) != reinterpret_cast<void *>(p2.get()))
    {
        std::cerr << "WARN [reuse]: address not reused immediately\n";
    }

    if (p2->compute(5) != 30 + 40 + 5)
    {
        std::cerr << "FAIL [reuse]: virtual call incorrect\n";
        return false;
    }

    return true;
}

bool test_invalid_release()
{
    resetCounters();
    ChunkedObjectPool<DerivedA, 2> pool;
    auto p = PooledUniquePtr<DerivedA, decltype(pool)>(pool, 1, 2);
    if (!p)
    {
        std::cerr << "FAIL [invalid_release]: acquire failed\n";
        return false;
    }

    DerivedA stackObj(10, 20); // 栈上对象，不属于池
    if (pool.release(&stackObj))
    {
        std::cerr
            << "FAIL [invalid_release]: releasing stack object should return false\n";
        return false;
    }

    // 重复释放同一对象
    if (pool.release(p.get()))
    {
        std::cerr << "FAIL [invalid_release]: first release should succeed (but we want "
                     "to test double release)\n";
        return false;
    }

    // 手动释放有效对象
    if (!pool.release(p.get()))
    {
        std::cerr << "FAIL [invalid_release]: releasing valid pointer should succeed\n";
        return false;
    }

    // 再次释放同一指针应该失败
    if (pool.release(p.get()))
    {
        std::cerr << "FAIL [invalid_release]: double release should return false\n";
        return false;
    }

    return true;
}

bool test_multiple_expansions_and_capacity()
{
    resetCounters();
    ChunkedObjectPool<DerivedA, 3> pool;

    std::vector<PooledUniquePtr<DerivedA, decltype(pool)>> ptrs;
    constexpr int numObjects = 10;
    for (int i = 0; i < numObjects; ++i)
    {
        ptrs.emplace_back(pool, i, i * 2);
        if (!ptrs.back())
        {
            std::cerr << "FAIL [multi_expand]: allocation " << i << " failed\n";
            return false;
        }
    }

    // 容量应至少覆盖 numObjects，且 available == capacity - numObjects
    if (pool.capacity() < numObjects)
    {
        std::cerr << "FAIL [multi_expand]: capacity too small\n";
        return false;
    }
    if (pool.available() != pool.capacity() - numObjects)
    {
        std::cerr << "FAIL [multi_expand]: available count inconsistent\n";
        return false;
    }

    // 验证所有对象虚函数
    for (int i = 0; i < numObjects; ++i)
    {
        if (ptrs[i]->compute(0) != i + i * 2 + 0)
        {
            std::cerr << "FAIL [multi_expand]: virtual call incorrect at " << i << "\n";
            return false;
        }
    }

    // 释放所有对象
    ptrs.clear();
    if (pool.available() != pool.capacity())
    {
        std::cerr
            << "FAIL [multi_expand]: available should equal capacity after release\n";
        return false;
    }
    if (DerivedA::derivedADestructorCount != numObjects)
    {
        std::cerr << "FAIL [multi_expand]: destructor count incorrect\n";
        return false;
    }

    return true;
}

bool test_custom_deleter_returns_to_pool()
{
    resetCounters();
    ChunkedObjectPool<DerivedA, 2> pool;
    {
        auto p = PooledUniquePtr<DerivedA, decltype(pool)>(pool, 100, 200);
        if (!p)
        {
            std::cerr << "FAIL [deleter]: allocation failed\n";
            return false;
        }
        // p 离开作用域，unique_ptr 删除器应调用 pool.release
    }
    if (pool.available() != 2)
    {
        std::cerr << "FAIL [deleter]: pool available count incorrect\n";
        return false;
    }
    if (DerivedA::derivedADestructorCount != 1)
    {
        std::cerr << "FAIL [deleter]: destructor not called via deleter\n";
        return false;
    }
    return true;
}

bool test_multiple_pools_independence()
{
    resetCounters();
    ChunkedObjectPool<DerivedA, 2> pool1;
    ChunkedObjectPool<DerivedA, 3> pool2;

    auto p1 = PooledUniquePtr<DerivedA, decltype(pool1)>(pool1, 1, 2);
    auto p2 = PooledUniquePtr<DerivedA, decltype(pool2)>(pool2, 3, 4);

    if (!p1 || !p2)
    {
        std::cerr << "FAIL [multi_pool]: allocation failed\n";
        return false;
    }

    // 两个池独立，容量不同
    if (pool1.capacity() != 2 || pool2.capacity() != 3)
    {
        std::cerr << "FAIL [multi_pool]: capacities differ incorrectly\n";
        return false;
    }

    // 释放 p1 到 pool1，不应影响 pool2
    p1.reset();
    if (pool1.available() != 2 || pool2.available() != 2)
    {
        std::cerr << "FAIL [multi_pool]: pool states not independent\n";
        return false;
    }

    return true;
}

// ============================================================================
// 运行所有测试
// ============================================================================
bool runTests()
{
    return test_basic_allocation_and_virtual_calls() && test_exception_safety() &&
           test_move_semantics() && test_expansion_preserves_old_pointers() &&
           test_release_and_reuse() && test_invalid_release() &&
           test_multiple_expansions_and_capacity() &&
           test_custom_deleter_returns_to_pool() && test_multiple_pools_independence();
}

int main()
{
    if (runTests())
    {
        std::cout << "All production object pool tests passed!\n";
        return 0;
    }
    return 1;
}