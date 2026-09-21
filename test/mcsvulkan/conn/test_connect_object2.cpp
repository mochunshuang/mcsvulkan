#include <algorithm>
#include <iostream>
#include <exception>
#include <memory>
#include <vector>
#include <cassert>
#include <chrono>

#include "head.hpp"

using mcs::vulkan::conn::connect_object;

namespace
{
    struct my_signal
    {
        connect_object hub;
        using signal_click = void(int);
        void submit(int v)
        {
            hub.emit<signal_click>(v);
        }
    };

    struct my_slot
    {
        connect_object hub;
        int value{-1};
    };

    // 生成一个"可观测"的 lambda：捕获 shared_ptr 计数
    auto make_probe(std::shared_ptr<int> counter)
    {
        return [counter](my_slot *self, int k) noexcept {
            self->value = k;
        };
    }
} // namespace

// ============================================================
// 证明 1：receiver 先析构 → slot_impl 立刻被删除
// ============================================================
void test_raii_receiver_dies_first()
{
    auto counter = std::make_shared<int>(0);
    assert(counter.use_count() == 1);

    {
        my_signal signaler;
        {
            my_slot sloter;
            auto ret = connect_object::connect<my_signal::signal_click>(
                &signaler.hub, &sloter, &sloter.hub, make_probe(counter));
            assert(ret);

            // 此刻：外部 1 份 + slot_impl::slot_ 里 1 份
            assert(counter.use_count() == 2);

            signaler.submit(1);
            assert(sloter.value == 1);
        } // sloter 析构 → hub 析构 → as_rcvr_destroy → release → delete slot_

        // 关键断言：slot_impl 已删除，lambda 里的捕获也析构
        assert(counter.use_count() == 1);

        // sender 侧此时持有的是"孤儿 connect_ptr"，ref_count == 1，rcvr_hold() == false
        // emit 会跳过它并触发 as_sndr_remove_expired_slot
        signaler.submit(2);
        assert(counter.use_count() == 1);
    } // signaler 析构

    assert(counter.use_count() == 1);
    std::cout << "[PASS] receiver dies first: slot_impl released at receiver dtor\n";
}

// ============================================================
// 证明 2：sender 先析构 → slot_impl 立刻被删除
// ============================================================
void test_raii_sender_dies_first()
{
    auto counter = std::make_shared<int>(0);
    assert(counter.use_count() == 1);

    {
        my_slot sloter;
        {
            my_signal signaler;
            auto ret = connect_object::connect<my_signal::signal_click>(
                &signaler.hub, &sloter, &sloter.hub, make_probe(counter));
            assert(ret);
            assert(counter.use_count() == 2);

            signaler.submit(1);
            assert(sloter.value == 1);
        } // signaler 析构 → as_sndr_destroy → release → delete slot_

        assert(counter.use_count() == 1);
    } // sloter 析构 → as_rcvr_destroy → release → delete connect_ptr

    assert(counter.use_count() == 1);
    std::cout << "[PASS] sender dies first: slot_impl released at sender dtor\n";
}

// ============================================================
// 证明 3：显式 disconnect 等价于"提前析构连接"
// ============================================================
void test_raii_explicit_disconnect()
{
    auto counter = std::make_shared<int>(0);
    assert(counter.use_count() == 1);

    {
        my_signal signaler;
        my_slot sloter;

        auto ret = connect_object::connect<my_signal::signal_click>(
            &signaler.hub, &sloter, &sloter.hub, make_probe(counter));
        assert(ret);
        assert(counter.use_count() == 2);

        signaler.submit(7);
        assert(sloter.value == 7);

        connect_object::disconnect<my_signal::signal_click>(&signaler.hub, &sloter.hub,
                                                            ret);

        // 和 hub 析构一样，slot_impl 被立刻删除
        assert(counter.use_count() == 1);

        // 断开后 emit 不再触发
        sloter.value = -1;
        signaler.submit(8);
        assert(sloter.value == -1);
    } // 两个对象析构：slots/map 已空，无二次释放

    assert(counter.use_count() == 1);
    std::cout << "[PASS] explicit disconnect: same effect as dtor, but earlier\n";
}

// ============================================================
// 证明 4：N 个连接 → N 个 slot_impl → 双向对偶释放
// ============================================================
void test_raii_bidirectional_many()
{
    constexpr int N = 8;
    auto counter = std::make_shared<int>(0);

    {
        my_signal signaler;
        std::vector<std::unique_ptr<my_slot>> slots;
        slots.reserve(N);

        for (int i = 0; i < N; ++i)
        {
            auto s = std::make_unique<my_slot>();
            auto ret = connect_object::connect<my_signal::signal_click>(
                &signaler.hub, s.get(), &s->hub, make_probe(counter));
            assert(ret);
            slots.push_back(std::move(s));
        }

        // N 个 lambda 各持一份捕获
        assert(counter.use_count() == 1 + N);

        signaler.submit(99);
        for (auto &s : slots)
            assert(s->value == 99);

        slots.clear(); // 所有 receiver 析构 → N 个 slot_impl 全删
        assert(counter.use_count() == 1);

        signaler.submit(1); // sender 侧清理孤儿 connect_ptr
        assert(counter.use_count() == 1);
    }

    assert(counter.use_count() == 1);
    std::cout << "[PASS] bidirectional: " << N << " connections released automatically\n";
}

void test_emit_without_slot()
{
    struct S : connect_object
    {
        using sig_a = void(int);
        using sig_b = void(int);
    };

    constexpr int N = 10'000'000;

    // 1. map 全空
    {
        S s;
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < N; ++i)
            s.emit<S::sig_a>(i);
        auto t1 = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        std::cout << "empty map, no slot: " << double(ns) / N << " ns/emit\n";
    }

    // 2. map 里有别的信号，但没有 sig_a
    {
        S s;
        S recv;
        connect_object::connect<S::sig_b>(&s, &recv, [](S *, int) noexcept {});
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < N; ++i)
            s.emit<S::sig_a>(i);
        auto t1 = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        std::cout << "non-empty map, miss: " << double(ns) / N << " ns/emit\n";

        auto bench = [&] {
            auto t0 = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < N; ++i)
                s.emit<S::sig_a>(i);
            auto t1 = std::chrono::high_resolution_clock::now();
            return std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() /
                   double(N);
        };

        std::vector<double> runs;
        for (int r = 0; r < 20; ++r)
            runs.push_back(bench());

        std::ranges::sort(runs);
        std::cout << "min:    " << runs.front() << " ns\n";
        std::cout << "median: " << runs[10] << " ns\n";
        std::cout << "max:    " << runs.back() << " ns\n";
    }
}
void test_self_connect()
{
    connect_object obj;
    using sig = void(int);

    // 组合式 connect：sndr_hub / recr / rcvr_hub 全是 &obj
    connect_object::connect<sig>(&obj, &obj, &obj,
                                 [](connect_object *self, int v) noexcept {
                                     std::cout << "self got " << v << '\n';
                                 });

    obj.emit<sig>(42); // 触发自己
    obj.emit<sig>(7);
}
// ============================================================
int main()
try
{
    test_raii_receiver_dies_first();
    test_raii_sender_dies_first();
    test_raii_explicit_disconnect();
    test_raii_bidirectional_many();
    test_emit_without_slot();
    test_self_connect();
    std::cout << "\nall RAII proofs passed\n";
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