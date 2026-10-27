// test_multicast_v2.cpp
//
// 编译： g++ -std=c++20 -O2 -Wall test_multicast_v2.cpp -o test_multicast_v2
//
// 相对上一版改动：
//   1. 删 Guard、删 length_error —— callback_t 无异常，dispatch 无需收拾状态
//   2. 删 pending_free_head_ —— 分发中 attach 强制 append，detach 直接进 free_head_
//   3. 全部标 constexpr
//   4. handle 新增 callback(h) / context(h) / invoke(h, v)
//
// 不变式：
//   M1 dispatch 只遍历 [0, n_snapshot_)；分发中 attach 一律 append，本轮不可见
//   M2 detach 只翻 occupied，槽位进 free_head_；index 恒定
//   M3 activate/deactivate 立即生效
//   M4 (slot, gen) 唯一绑定一个 attach 周期；gen 单调递增
//   M5 对象生命周期由调用方保证
//   M6 detach/activate/deactivate/dispatch 全部 noexcept 且零分配
//   M7 attach 可能抛 bad_alloc；用 reserve() 预分配可规避
// -----------------------------------------------------------------------------

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <functional>

// NOLINTBEGIN
class multicast_sink
{
  public:
    using callback_t = void (*)(void *, int);
    using index_t = std::uint32_t;
    static constexpr index_t npos = ~index_t{0};

    struct handle
    {
        index_t slot = npos;
        std::uint64_t gen = 0;
        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return slot != npos;
        }
    };

    constexpr multicast_sink() = default;
    constexpr multicast_sink(const multicast_sink &) = delete;
    constexpr multicast_sink &operator=(const multicast_sink &) = delete;

    // 预分配：让"回调里 attach"不触发 vector realloc
    constexpr void reserve(std::size_t n)
    {
        slots_.reserve(n);
    }

    // 唯一可能抛 bad_alloc 的入口
    template <class Obj>
    constexpr handle attach(Obj &obj)
    {
        return attach_erased(
            &obj, [](void *p, int v) noexcept { static_cast<Obj *>(p)->on_event(v); });
    }

    // O(1)、零分配、noexcept
    constexpr void detach(handle h) noexcept
    {
        if (h.slot >= slots_.size())
            return;
        auto &s = slots_[h.slot];
        if (!s.occupied || s.gen != h.gen)
            return;
        s.occupied = false;
        s.obj = nullptr;
        s.fn = nullptr;
        s.free_next = free_head_;
        free_head_ = h.slot;
    }

    constexpr void activate(handle h) noexcept
    {
        set_active(h, true);
    }
    constexpr void deactivate(handle h) noexcept
    {
        set_active(h, false);
    }

    // 无异常（callback_t 保证不抛），零分配
    constexpr void dispatch(int v) noexcept
    {
        const bool outer = !distributing_;
        if (outer)
        {
            distributing_ = true;
            n_snapshot_ = static_cast<index_t>(slots_.size());
        }
        for (index_t i = 0; i < n_snapshot_; ++i)
        {
            auto &s = slots_[i];
            if (!s.occupied || !s.active)
                continue;
            s.fn(s.obj, v); // noexcept
        }
        if (outer)
            distributing_ = false;
    }

    // ---- handle → callback_t 访问 ----
    [[nodiscard]] constexpr callback_t callback(handle h) const noexcept
    {
        if (h.slot >= slots_.size())
            return nullptr;
        const auto &s = slots_[h.slot];
        if (!s.occupied || s.gen != h.gen)
            return nullptr;
        return s.fn;
    }

    [[nodiscard]] constexpr void *context(handle h) const noexcept
    {
        if (h.slot >= slots_.size())
            return nullptr;
        const auto &s = slots_[h.slot];
        if (!s.occupied || s.gen != h.gen)
            return nullptr;
        return s.obj;
    }

    // 用 handle 直接调用
    constexpr void invoke(handle h, int v) const noexcept
    {
        if (h.slot >= slots_.size())
            return;
        const auto &s = slots_[h.slot];
        if (!s.occupied || s.gen != h.gen)
            return;
        s.fn(s.obj, v);
    }

    [[nodiscard]] constexpr std::size_t slot_count() const noexcept
    {
        return slots_.size();
    }
    [[nodiscard]] constexpr std::size_t live_count() const noexcept
    {
        std::size_t n = 0;
        for (auto &s : slots_)
            if (s.occupied)
                ++n;
        return n;
    }

  private:
    struct slot
    {
        void *obj = nullptr;
        callback_t fn = nullptr;
        index_t free_next = npos;
        std::uint64_t gen = 0;
        bool occupied = false;
        bool active = true;
    };

    std::vector<slot> slots_;
    index_t free_head_ = npos; // 常规空闲链（含分发中 detach 的）
    index_t n_snapshot_ = 0;   // 分发上界
    bool distributing_ = false;

    constexpr handle attach_erased(void *obj, callback_t fn)
    {
        index_t idx;
        if (distributing_ || free_head_ == npos)
        {
            // append：绝不复用本轮正在遍历范围内的槽位
            idx = static_cast<index_t>(slots_.size());
            slot s;
            s.obj = obj;
            s.fn = fn;
            s.gen = 1;
            s.occupied = true;
            s.active = true;
            slots_.push_back(s); // 可能抛
        }
        else
        {
            idx = free_head_;
            auto &s = slots_[idx];
            free_head_ = s.free_next;
            s.free_next = npos;
            s.obj = obj;
            s.fn = fn;
            s.gen += 1;
            s.occupied = true;
            s.active = true;
        }
        return {idx, slots_[idx].gen};
    }

    constexpr void set_active(handle h, bool v) noexcept
    {
        if (h.slot >= slots_.size())
            return;
        auto &s = slots_[h.slot];
        if (s.occupied && s.gen == h.gen)
            s.active = v;
    }
};

// =============================================================================
// 测试基础设施
// =============================================================================
#define CHECK(cond)                                                              \
    do                                                                           \
    {                                                                            \
        if (!(cond))                                                             \
        {                                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

#define RUN(fn)                           \
    do                                    \
    {                                     \
        std::printf("[ RUN ] %s\n", #fn); \
        fn();                             \
        std::printf("[ OK  ] %s\n", #fn); \
    } while (0)

struct Counter
{
    int id;
    int calls = 0;
    int last_v = -1;
    explicit Counter(int i) : id(i) {}
    void on_event(int v) noexcept
    {
        ++calls;
        last_v = v;
    }
};

// 可以带副作用的探针（detach / attach / activate / detach-self...）
struct Probe
{
    int calls = 0;
    int last_v = -1;
    void (*side_effect)(Probe *, int) = nullptr;
    void on_event(int v) noexcept
    {
        ++calls;
        last_v = v;
        if (side_effect)
            side_effect(this, v);
    }
};

// literal 类型，用于 constexpr 编译期测试
struct LiteralCounter
{
    int calls = 0;
    constexpr void on_event(int) noexcept
    {
        ++calls;
    }
};

// =============================================================================
// 编译期测试（constexpr）
// =============================================================================
constexpr bool constexpr_lifecycle()
{
    multicast_sink s;
    LiteralCounter a, b;
    auto ha = s.attach(a);
    auto hb = s.attach(b);

    s.dispatch(1);    // a=1, b=1
    s.deactivate(ha); // a off
    s.dispatch(2);    // a off, b=2
    s.activate(ha);   // a on
    s.dispatch(3);    // a=2, b=3
    s.detach(hb);     // b off
    s.dispatch(4);    // a=3, b off
    s.detach(ha);     // a off
    s.dispatch(5);    // both off

    return a.calls == 3 && b.calls == 3;
}
static_assert(constexpr_lifecycle());

constexpr bool constexpr_handle_access()
{
    multicast_sink s;
    LiteralCounter c;
    auto h = s.attach(c);

    if (!h.valid())
        return false;
    if (s.callback(h) == nullptr)
        return false;
    if (s.context(h) != static_cast<void *>(&c))
        return false;

    s.invoke(h, 42); // 用 handle 直调
    if (c.calls != 1)
        return false;

    s.detach(h);
    return s.callback(h) == nullptr && s.context(h) == nullptr;
}
static_assert(constexpr_handle_access());

// =============================================================================
// 运行时测试
// =============================================================================

void test_attach_dispatch()
{
    multicast_sink sink;
    Counter a(1), b(2);
    (void)sink.attach(a);
    (void)sink.attach(b);

    sink.dispatch(10);
    CHECK(a.calls == 1 && a.last_v == 10);
    CHECK(b.calls == 1 && b.last_v == 10);
    sink.dispatch(20);
    CHECK(a.calls == 2 && a.last_v == 20);
    CHECK(b.calls == 2 && b.last_v == 20);
}

void test_detach()
{
    multicast_sink sink;
    Counter a(1), b(2), c(3);
    (void)sink.attach(a);
    auto hb = sink.attach(b);
    (void)sink.attach(c);

    sink.detach(hb);
    sink.dispatch(1);
    CHECK(a.calls == 1 && b.calls == 0 && c.calls == 1);

    sink.detach(hb); // 幂等
    sink.dispatch(2);
    CHECK(a.calls == 2 && b.calls == 0 && c.calls == 2);
}

void test_activate_deactivate()
{
    multicast_sink sink;
    Counter a(1);
    auto ha = sink.attach(a);

    sink.dispatch(1);
    CHECK(a.calls == 1);
    sink.deactivate(ha);
    sink.dispatch(2);
    CHECK(a.calls == 1);
    sink.activate(ha);
    sink.dispatch(3);
    CHECK(a.calls == 2 && a.last_v == 3);

    sink.deactivate(ha);
    sink.detach(ha);
    sink.activate(ha); // 死 handle：no-op
    sink.dispatch(4);
    CHECK(a.calls == 2);
}

void test_many_attach_growth()
{
    multicast_sink sink;
    std::vector<Counter> counters;
    counters.reserve(200);
    for (int i = 0; i < 200; ++i)
        counters.emplace_back(i);

    multicast_sink::handle first;
    for (int i = 0; i < 200; ++i)
    {
        auto h = sink.attach(counters[i]);
        if (i == 0)
            first = h;
    }
    CHECK(sink.slot_count() >= 200);

    sink.dispatch(99);
    for (auto &c : counters)
        CHECK(c.calls == 1 && c.last_v == 99);

    sink.detach(first); // realloc 后最早 handle 仍精确命中
    sink.dispatch(100);
    CHECK(counters[0].calls == 1);
    for (std::size_t i = 1; i < counters.size(); ++i)
        CHECK(counters[i].calls == 2);
}

void test_slot_recycle_invalidates_old_handle()
{
    multicast_sink sink;
    Counter a(1), b(2);

    auto ha = sink.attach(a);
    sink.detach(ha);
    auto hb = sink.attach(b); // 复用 slot

    CHECK(hb.slot == ha.slot);
    CHECK(hb.gen > ha.gen);

    sink.detach(ha); // 旧 handle：no-op
    sink.dispatch(7);
    CHECK(b.calls == 1 && a.calls == 0);
}

// ---------- handle → callback_t 访问能力 ----------
void test_handle_callback_access()
{
    multicast_sink sink;
    Counter a(1);
    auto ha = sink.attach(a);

    // 查询 callback 与 context
    auto cb = sink.callback(ha);
    CHECK(cb != nullptr);
    CHECK(sink.context(ha) == static_cast<void *>(&a));

    // 用 handle 直接调用
    sink.invoke(ha, 77);
    CHECK(a.calls == 1 && a.last_v == 77);

    // detach 后 handle 失效
    sink.detach(ha);
    CHECK(sink.callback(ha) == nullptr);
    CHECK(sink.context(ha) == nullptr);
    sink.invoke(ha, 88);
    CHECK(a.calls == 1); // 不再被调

    // 无效 handle（初始值 / 越界）都返回 nullptr
    multicast_sink::handle empty{};
    CHECK(sink.callback(empty) == nullptr);
    CHECK(sink.context(empty) == nullptr);

    multicast_sink::handle out_of_range{999, 0};
    CHECK(sink.callback(out_of_range) == nullptr);
}

// ---------- 用户核心诉求：多轮激活失活不破坏批量 invoke 语义 ----------
void test_multi_round_activate_does_not_corrupt_dispatch()
{
    multicast_sink sink;
    Counter a(1), b(2), c(3);
    auto ha = sink.attach(a);
    auto hb = sink.attach(b);
    auto hc = sink.attach(c);

    // 100 轮随机（固定模式）激活/失活，然后批量 invoke 必须准确反映最终态
    for (int i = 0; i < 100; ++i)
    {
        sink.deactivate(ha);
        sink.deactivate(hb);
        sink.deactivate(hc);
        if (i % 3 == 0)
            sink.activate(ha);
        if (i % 3 == 1)
            sink.activate(hb);
        if (i % 3 == 2)
            sink.activate(hc);
    }
    // 最终态：最后一次 i=99，99%3==0 → activate(ha)
    //       而 i=98 时 deactivate 全部 → 最终只有 a 活着
    const int a_before = a.calls, b_before = b.calls, c_before = c.calls;

    sink.dispatch(42);
    CHECK(a.calls == a_before + 1);
    CHECK(b.calls == b_before);
    CHECK(c.calls == c_before);

    // 再走一轮：只在 b 上反复切换 50 次，最终 activate
    for (int i = 0; i < 50; ++i)
    {
        sink.deactivate(hb);
        sink.activate(hb);
    }
    sink.deactivate(ha);
    sink.deactivate(hc);
    sink.dispatch(43);
    CHECK(b.calls == b_before + 1);
    CHECK(b.last_v == 43);
    CHECK(a.calls == a_before + 1); // a 仍是上一轮的值
}

// ---------- 分发中操作 ----------
void test_detach_peer_during_dispatch()
{
    multicast_sink sink;
    Probe a, b, c;
    (void)sink.attach(a);
    (void)sink.attach(b);
    auto hc = sink.attach(c);

    a.side_effect = [](Probe *, int) {
    };
    auto detach_c = [](Probe *self, int) {
        auto *h = static_cast<multicast_sink::handle *>(
            self->side_effect == nullptr ? nullptr : nullptr);
        (void)h;
    };
    (void)detach_c;

    // 用 lambda 捕获 sink + hc 来 detach
    a.side_effect = [](Probe *, int) {
    }; // placeholder
    // 改为直接捕获式（简单）：
    struct Ctx
    {
        multicast_sink *s;
        multicast_sink::handle hc;
        bool done = false;
        static void hook(Probe *p, int)
        {
            auto *c = static_cast<Ctx *>(p->side_effect ? nullptr : nullptr);
            (void)c;
        }
    };
    (void)sizeof(Ctx);

    // 最简：改用外部变量 + 静态函数
    static multicast_sink *g_sink = nullptr;
    static multicast_sink::handle g_hc{};
    static bool g_fired = false;
    g_sink = &sink;
    g_hc = hc;
    g_fired = false;
    a.side_effect = [](Probe *, int) {
        if (!g_fired)
        {
            g_sink->detach(g_hc);
            g_fired = true;
        }
    };
    sink.dispatch(0);
    CHECK(a.calls == 1 && b.calls == 1 && c.calls == 0);

    a.side_effect = nullptr;
    sink.dispatch(0);
    CHECK(c.calls == 0);
}

void test_attach_during_dispatch()
{
    multicast_sink sink;
    sink.reserve(10); // 避免 realloc
    Probe a, late;
    (void)sink.attach(a);

    bool done = false;
    a.side_effect = [](Probe *p, int) {
        auto *ctx = static_cast<std::pair<multicast_sink *, bool *> *>(nullptr);
        (void)ctx;
        (void)p;
    };
    // 同上，用静态简化
    static multicast_sink *g_sink = nullptr;
    static Probe *g_late = nullptr;
    static bool *g_done = nullptr;
    g_sink = &sink;
    g_late = &late;
    g_done = &done;
    a.side_effect = [](Probe *, int) {
        if (!*g_done)
        {
            (void)g_sink->attach(*g_late);
            *g_done = true;
        }
    };
    sink.dispatch(0);
    CHECK(a.calls == 1 && late.calls == 0); // 本轮 snapshot 外

    a.side_effect = nullptr;
    sink.dispatch(0);
    CHECK(late.calls == 1);
}

void test_activate_peer_during_dispatch()
{
    multicast_sink sink;
    Probe a, c;
    (void)sink.attach(a);
    auto hc = sink.attach(c);
    sink.deactivate(hc);

    static multicast_sink *g_sink = nullptr;
    static multicast_sink::handle g_hc{};
    g_sink = &sink;
    g_hc = hc;
    a.side_effect = [](Probe *, int) {
        g_sink->activate(g_hc);
    };

    sink.dispatch(0);
    CHECK(a.calls == 1 && c.calls == 1); // 立即生效
    a.side_effect = nullptr;
}

void test_self_detach_during_dispatch()
{
    multicast_sink sink;
    Probe a, b;
    auto ha = sink.attach(a);
    (void)sink.attach(b);

    static multicast_sink *g_sink = nullptr;
    static multicast_sink::handle g_ha{};
    static bool g_fired = false;
    g_sink = &sink;
    g_ha = ha;
    g_fired = false;
    a.side_effect = [](Probe *, int) {
        if (!g_fired)
        {
            g_sink->detach(g_ha);
            g_fired = true;
        }
    };
    sink.dispatch(0);
    CHECK(a.calls == 1 && b.calls == 1);

    a.side_effect = nullptr;
    sink.dispatch(0);
    CHECK(a.calls == 1 && b.calls == 2);
}

void test_self_deactivate_during_dispatch()
{
    multicast_sink sink;
    Probe a, b;
    auto ha = sink.attach(a);
    (void)sink.attach(b);

    static multicast_sink *g_sink = nullptr;
    static multicast_sink::handle g_ha{};
    static bool g_fired = false;
    g_sink = &sink;
    g_ha = ha;
    g_fired = false;
    a.side_effect = [](Probe *, int) {
        if (!g_fired)
        {
            g_sink->deactivate(g_ha);
            g_fired = true;
        }
    };
    sink.dispatch(0);
    CHECK(a.calls == 1 && b.calls == 1);

    a.side_effect = nullptr;
    sink.dispatch(0);
    CHECK(a.calls == 1 && b.calls == 2);
}

void test_nested_dispatch()
{
    // 内层沿用外层 n_snapshot_，不重设 distributing_
    multicast_sink sink;
    Probe a;
    (void)sink.attach(a);

    static multicast_sink *g_sink = nullptr;
    static int g_depth = 0;
    g_sink = &sink;
    g_depth = 0;
    a.side_effect = [](Probe *, int v) {
        if (g_depth == 0 && v == 0)
        {
            ++g_depth;
            g_sink->dispatch(99); // 内层
        }
    };
    sink.dispatch(0);
    CHECK(a.calls == 2 && a.last_v == 99);
}

void test_detach_then_attach_in_dispatch_uses_append()
{
    // 关键：分发中 detach 后 attach 不会复用该槽位（append 到末尾）
    multicast_sink sink;
    sink.reserve(10);
    Probe a, b, c;
    (void)sink.attach(a);
    auto hb = sink.attach(b);

    const auto before = sink.slot_count();

    static multicast_sink *g_sink = nullptr;
    static multicast_sink::handle g_hb{};
    static Probe *g_c = nullptr;
    static bool g_done = false;
    g_sink = &sink;
    g_hb = hb;
    g_c = &c;
    g_done = false;
    a.side_effect = [](Probe *, int) {
        if (!g_done)
        {
            g_sink->detach(g_hb);
            (void)g_sink->attach(*g_c);
            g_done = true;
        }
    };
    sink.dispatch(0);

    CHECK(sink.slot_count() == before + 1); // append，不复用
    CHECK(a.calls == 1 && b.calls == 0 && c.calls == 0);

    a.side_effect = nullptr;
    sink.dispatch(0);
    CHECK(c.calls == 1);
}

void test_gen_monotonic()
{
    multicast_sink sink;
    Counter a(1);
    std::uint64_t prev = 0;
    for (int i = 0; i < 20; ++i)
    {
        auto h = sink.attach(a);
        CHECK(h.gen > prev);
        prev = h.gen;
        sink.detach(h);
    }
    CHECK(sink.slot_count() == 1);
}

void test_empty_sink_dispatch()
{
    multicast_sink sink;
    sink.dispatch(0);
    CHECK(sink.slot_count() == 0);
    CHECK(sink.live_count() == 0);
}

void test_detach_cycle_reuses_slot()
{
    multicast_sink sink;
    Counter a(1);
    for (int i = 0; i < 1000; ++i)
    {
        auto h = sink.attach(a);
        sink.detach(h);
    }
    CHECK(sink.slot_count() == 1);
    CHECK(sink.live_count() == 0);
}

// =============================================================================
int main()
{
    RUN(test_attach_dispatch);
    RUN(test_detach);
    RUN(test_activate_deactivate);
    RUN(test_many_attach_growth);
    RUN(test_slot_recycle_invalidates_old_handle);
    RUN(test_handle_callback_access);
    RUN(test_multi_round_activate_does_not_corrupt_dispatch);
    RUN(test_detach_peer_during_dispatch);
    RUN(test_attach_during_dispatch);
    RUN(test_activate_peer_during_dispatch);
    RUN(test_self_detach_during_dispatch);
    RUN(test_self_deactivate_during_dispatch);
    RUN(test_nested_dispatch);
    RUN(test_detach_then_attach_in_dispatch_uses_append);
    RUN(test_gen_monotonic);
    RUN(test_empty_sink_dispatch);
    RUN(test_detach_cycle_reuses_slot);

    std::printf("\nAll tests passed.\n");
    return 0;
} // NOLINTEND