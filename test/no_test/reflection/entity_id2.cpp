#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

// NOLINTBEGIN

constexpr uint32_t INVALID = 0xFFFFFFFF; // 无效物理索引标记
constexpr uint32_t INITIAL_VERSION = 0;  // 未使用虚拟槽的初始版本

struct entity_id
{
    uint32_t store_idx; // 虚拟ID
    uint32_t version;   // 版本号
};

// 虚拟槽状态：将虚拟ID映射到物理槽位+版本
struct VirtualSlot
{
    uint32_t physical = INVALID;        // 当前对应的物理槽位，INVALID表示空闲
    uint32_t version = INITIAL_VERSION; // 当前版本（0表示从未分配过）
};

struct point
{
    int x;
    int y;
};

template <typename T, template <typename _Tp> class Alloc = std::allocator>
struct entity_store
{
  public:
    using allocator_type = Alloc<T>;
    using id_allocator_type = typename allocator_type::template rebind<entity_id>::other;

    using value_type = T;

    using grow_strategy_t =
        std::function<size_t(size_t current_capacity, size_t additional)>;
    // 默认扩容策略：翻倍或至少满足额外需求
    static size_t default_grow_strategy(size_t current, size_t additional)
    {
        size_t needed = current + additional;
        size_t doubled = current * 2;
        return (std::max)(needed, doubled);
    }

    struct store_type
    {
        size_t capacity_{0};
        T *data_{};
        entity_id *id_map_{};
    };
    enum State
    {
        undefined,
        nomal,
        growing,
    };
    enum ErrorState
    {
        OK,
        ERR_growing,
        ERR_try_again
    };

    explicit entity_store(size_t capacity = 8,
                          grow_strategy_t grow_strategy = &default_grow_strategy,
                          const allocator_type &alloc = {})
        : alloc_(alloc), id_alloc_(alloc), // 用 alloc 初始化 id_alloc_
          cur_{}, new_{}, count_(0)
    {
        if (capacity > 0)
        {
            cur_.capacity_ = capacity;
            cur_.data_ = alloc_.allocate(capacity);
            cur_.id_map_ = id_alloc_.allocate(capacity);

            status_.store(State::nomal, std::memory_order_release);
        }
    }

    size_t get_new_capacity() const noexcept
    {
        return grow_strategy_(count_, additional_);
    }

    static constexpr ErrorState get_error_status(State s) noexcept
    {
        switch (s)
        {
        case State::growing:
            return ERR_growing;
        case State::nomal:
            return ERR_try_again;
        default:
            std::abort();
        }
    }

    constexpr auto try_allocate() noexcept
    {
        if (auto status = status_.load(std::memory_order_acquire);
            status == State::growing)
            return std::make_pair(entity_id{}, get_error_status(status));

        uint32_t old_val = count_.load(std::memory_order_acquire);
        //NOTE: 如果CAS失败。old_val 会是最新值
        while (old_val < cur_.capacity_)
        {
            if (count_.compare_exchange_weak(old_val, old_val + 1,
                                             std::memory_order_acquire, // 成功时的内存序
                                             std::memory_order_relaxed  // 失败时的内存序
                                             ))
            {
                uint32_t store_idx = old_val;
                uint32_t version = cur_.id_map_[old_val].version + 1;

                //NOTE: 删除的时候 store_idx 可能不再指向 old_val。 但是保证 store_idx 能找到绑定的数据
                cur_.id_map_[store_idx] = {old_val, version};
                return std::make_pair(
                    entity_id{.store_idx = store_idx, .version = version},
                    ErrorState::OK);
            }
            // CAS失败，需要根据当前最新值判断是否继续
        }
        //change status from nomal to growing
        auto expected = State::nomal;
        status_.compare_exchange_weak(expected, State::growing, std::memory_order_release,
                                      std::memory_order_relaxed);
        return std::make_pair(entity_id{}, get_error_status(expected));
    }

    //NOTE: BUG。 有空洞。明显不行。 swap
    constexpr bool try_release(entity_id id)
    {
        // 将 id 映射的存储标记为
        if (status_.load(std::memory_order_acquire) != State::nomal)
            return false;

        uint32_t old_val = count_.load(std::memory_order_acquire);
        std::swap(cur_.data_[id.store_idx], cur_.data_[old_val]);

        --count_;
    }

    //NOTE: 还是得

    void wait_to_nomal() const noexcept
    {
        status_.wait(State::growing);
    }

    void do_grow()
    {
        auto new_capacity = get_new_capacity();
        store_type tmp = cur_;
    }

    // 帧末 统一检查
    auto check_statues()
    {
        if (count_.load(std::memory_order_acquire) == cur_.capacity_)
        {
            status_.store(State::growing, std::memory_order_release);
            do_grow();
            status_.store(State::nomal, std::memory_order_release);
        }
        status_.notify_all();
    }

  private:
    allocator_type alloc_;
    id_allocator_type id_alloc_;

    store_type cur_;
    store_type new_;

    std::atomic<State> status_{};
    std::atomic<uint32_t> count_;

    grow_strategy_t grow_strategy_;
    size_t additional_{};

    // std::mutex mtx;
    // std::condition_variable cv;
};

// NOLINTEND

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