#include <iostream>

#include "../head.hpp"

#include <chrono>
#include <print>

using mcs::vulkan::conn::connect_object;

// NOLINTBEGIN
void test_slot_ptr()
{
    struct my_signal : connect_object
    {
        using signal_click = void(int key);
        using signal_empty = void();

        void submit_key_event(int v = 0)
        {
            this->emit<signal_click>(v);
        }
        void onSuccessEmit(this my_signal &self) noexcept
        {
            std::cout << "successful emit\n";
            self.has_callback = true;
        }
        bool has_callback{};
    };
    struct my_slot : connect_object
    {

        using signal_click_callback = void();

        my_slot() noexcept {}
        void onClick(this my_slot &self, int key) noexcept
        {
            self.value = key;
            self.emit<signal_click_callback>();
        }
        void onClick2(int key) noexcept
        {
            value = key;
            this->emit<signal_click_callback>();
        }
        int value{-1};
    };
    // c0: lambda

    {
        my_signal signaler;
        my_slot sloter;
        auto ret = connect_object::connect<my_signal::signal_click>(
            &signaler, &sloter, [&sloter](my_slot *self, int key) noexcept {
                self->value = key;
                assert(self == &sloter); // NOTE: 允许捕获
            });

        assert(ret);

        signaler.submit_key_event();
        assert(sloter.value == 0);
    }
    {
        my_signal signaler;
        my_slot sloter;
        auto ret = connect_object::connect<my_signal::signal_click>(
            &signaler, &sloter,
            [](my_slot *self, int key) noexcept { self->value = key; });

        assert(ret);

        signaler.submit_key_event(1);
        assert(sloter.value == 1);
    }
    // c0: member function
    {
        my_signal signaler;
        my_slot sloter;
        auto ret = connect_object::connect<my_signal::signal_click>(&signaler, &sloter,
                                                                    &my_slot::onClick);

        assert(ret);

        ret = connect_object::connect<my_slot::signal_click_callback>(
            &sloter, &signaler, &my_signal::onSuccessEmit);

        assert(ret);

        int listen_key;
        ret = connect_object::connect<my_signal::signal_click>(
            &signaler, &sloter,
            [&](const my_slot &, int key) noexcept { listen_key = key; });

        assert(not signaler.has_callback);
        signaler.submit_key_event(2); // c0: 一次发送
        assert(sloter.value == 2);
        assert(signaler.has_callback);

        assert(listen_key == 2); // c0: 所有slot 都受到信息
    }
    {
        // NOTE: 普通成员
        my_signal signaler;
        my_slot sloter;
        auto ret = connect_object::connect<my_signal::signal_click>(&signaler, &sloter,
                                                                    &my_slot::onClick2);

        assert(ret);

        ret = connect_object::connect<my_slot::signal_click_callback>(
            &sloter, &signaler, &my_signal::onSuccessEmit);

        assert(ret);
        std::println("{}", connect_object::slot_impl_string<my_signal::signal_click>(
                               &signaler, &sloter, &my_slot::onClick2));
        signaler.submit_key_event(2);
        assert(sloter.value == 2);
        assert(signaler.has_callback);
    }
    {
        my_signal signaler;
        my_slot sloter;
        auto *ret = connect_object::connect<my_signal::signal_click>(&signaler, &sloter,
                                                                     &my_slot::onClick);

        assert(ret);

        ret = connect_object::connect<my_slot::signal_click_callback>(
            &sloter, &signaler, &my_signal::onSuccessEmit);

        assert(ret);

        int listen_key{-1}; // c0: 不需要 my_slot *,
        ret = connect_object::connect<my_signal::signal_click>(
            &signaler, &sloter, [&](int key) noexcept { listen_key = key; });

        assert(ret);
        assert(ret->rcvr_hold());

        std::cout << ">>> connect_object::disconnect start\n";
        connect_object::disconnect<my_signal::signal_click>(&signaler, &sloter, ret);
        std::cout << ">>> connect_object::disconnect end\n";
        // NOTE: dont`t use ret after disconnect

        assert(not signaler.has_callback);
        signaler.submit_key_event(2); // c0: 一次发送
        assert(sloter.value == 2);
        assert(signaler.has_callback);

        assert(listen_key == -1); // c0: 已经断开连接则不再得到更新
    }
}

// 编译时开销测试
void test_compile_time_overhead()
{
    std::cout << "\n=== 编译时开销测试 ===\n";

    // 测试编译时类型检查的开销
    struct TestSignal : connect_object
    {
        using signal_one = void(int);
        using signal_two = void(int, double);
        using signal_three = void(int, double, const char *);
    };

    struct TestSlot : connect_object
    {
        void slot_one(this TestSlot &slef, int) noexcept {}
        void slot_two(this TestSlot &slef, int, double) noexcept {}
        void slot_three(this TestSlot &slef, int, double, const char *) noexcept {}
    };

    TestSignal signal;
    TestSlot slot;

    std::cout << "编译时类型检查测试（如果编译通过即表示正常）:\n";

    // 这些应该能编译通过
    auto conn1 = connect_object::connect<TestSignal::signal_one>(&signal, &slot,
                                                                 &TestSlot::slot_one);
    std::cout << "1. 单参数连接: " << (conn1 ? "成功" : "失败") << "\n";

    auto conn2 = connect_object::connect<TestSignal::signal_two>(&signal, &slot,
                                                                 &TestSlot::slot_two);
    std::cout << "2. 双参数连接: " << (conn2 ? "成功" : "失败") << "\n";

    auto conn3 = connect_object::connect<TestSignal::signal_three>(&signal, &slot,
                                                                   &TestSlot::slot_three);
    std::cout << "3. 三参数连接: " << (conn3 ? "成功" : "失败") << "\n";

    // 尝试错误连接（应该编译失败或返回nullptr）
    // 注：取消下面代码的注释会触发编译错误，这是正确的

    // 参数类型不匹配
    // auto conn_err1 = connect_object::connect<TestSignal::signal_one>(
    //     &signal, &slot, &TestSlot::slot_two); // 应该编译失败

    // 参数数量不匹配
    // auto conn_err2 = connect_object::connect<TestSignal::signal_two>(
    //     &signal, &slot, &TestSlot::slot_one); // 应该编译失败

    if (conn1)
        connect_object::disconnect<TestSignal::signal_one>(&signal, &slot, conn1);
    if (conn2)
        connect_object::disconnect<TestSignal::signal_two>(&signal, &slot, conn2);
    if (conn3)
        connect_object::disconnect<TestSignal::signal_three>(&signal, &slot, conn3);

    std::cout << "\n编译时检查工作正常！\n";
}
// 新增性能测试函数
void test_signal_slot_performance()
{
    std::cout << "\n=== 信号槽性能测试 ===\n";

    struct TestSignal : connect_object
    {
        using signal_test = void(int);
        using signal_empty = void();
    };

    struct TestSlot : connect_object
    {
        int value = 0;
        int empty_count = 0;

        // void onTest(this TestSlot &slef, int v) noexcept
        // {
        //     slef.value = v;
        // }
        void onTest(int v) noexcept
        {
            value = v;
        }
        void onTest2(int v) noexcept
        {
            value = v; // C0: class member
        }
        void onEmpty(this TestSlot &slef) noexcept
        {
            slef.empty_count++;
        }
    };
    {
        TestSlot s;
        auto v = &TestSlot::onTest2;
        TestSlot *ps = &s;
        // ps->(*v)(0);
        (ps->*v)(0);
        (s.*v)(0);
        std::mem_fn(v)(s, 2);
        std::mem_fn(v)(ps, 2);
        std::mem_fn (&TestSlot::onTest2)(s, 2);
        std::mem_fn (&TestSlot::onTest2)(ps, 2);
    }

    TestSignal signal;
    TestSlot slot;

    // 1. 测量直接函数调用开销（基准）
    {
        constexpr int ITERATIONS = 1000000;
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < ITERATIONS; ++i)
        {
            slot.onTest(i);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        std::cout << "1. 直接函数调用: " << duration.count() / double(ITERATIONS)
                  << " ns/次\n";
    }

    // 2. 测量带参数信号槽调用开销
    {
        constexpr int ITERATIONS = 1000000;
        auto conn = connect_object::connect<TestSignal::signal_test>(&signal, &slot,
                                                                     &TestSlot::onTest);

        assert(conn);

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < ITERATIONS; ++i)
        {
            signal.emit<TestSignal::signal_test>(i);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        std::cout << "2. 带参数信号槽: " << duration.count() / double(ITERATIONS)
                  << " ns/次\n";

        connect_object::disconnect<TestSignal::signal_test>(&signal, &slot, conn);
    }

    // 3. 测量无参数信号槽调用开销
    {
        constexpr int ITERATIONS = 1000000;
        auto conn = connect_object::connect<TestSignal::signal_empty>(&signal, &slot,
                                                                      &TestSlot::onEmpty);

        assert(conn);

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < ITERATIONS; ++i)
        {
            signal.emit<TestSignal::signal_empty>();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        std::cout << "3. 无参数信号槽: " << duration.count() / double(ITERATIONS)
                  << " ns/次\n";
        std::cout << "   槽调用次数: " << slot.empty_count << "\n";

        connect_object::disconnect<TestSignal::signal_empty>(&signal, &slot, conn);
    }

    // 4. 测量lambda连接开销
    {
        constexpr int ITERATIONS = 1000000;
        int lambda_counter = 0;

        auto conn = connect_object::connect<TestSignal::signal_test>(
            &signal, &slot,
            [&lambda_counter](TestSlot *, int) noexcept { lambda_counter++; });

        assert(conn);

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < ITERATIONS; ++i)
        {
            signal.emit<TestSignal::signal_test>(i);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        std::cout << "4. Lambda连接: " << duration.count() / double(ITERATIONS)
                  << " ns/次\n";
        std::cout << "   Lambda计数: " << lambda_counter << "\n";

        connect_object::disconnect<TestSignal::signal_test>(&signal, &slot, conn);
    }

    // 5. 测量多连接开销（1个信号，10个槽）
    {
        constexpr int ITERATIONS = 100000;
        constexpr int SLOT_COUNT = 10;

        std::vector<std::unique_ptr<TestSlot>> slots;
        std::vector<connect_object::connect_ptr *> connections;

        // 创建10个槽
        for (int i = 0; i < SLOT_COUNT; ++i)
        {
            auto slot_ptr = std::make_unique<TestSlot>();
            auto conn = connect_object::connect<TestSignal::signal_test>(
                &signal, slot_ptr.get(), &TestSlot::onTest);

            assert(conn);
            slots.push_back(std::move(slot_ptr));
            connections.push_back(conn);
        }

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < ITERATIONS; ++i)
        {
            signal.emit<TestSignal::signal_test>(i);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

        std::cout << "5. 多连接(" << SLOT_COUNT
                  << "槽): " << duration.count() / double(ITERATIONS) << " ns/次发射\n";
        std::cout << "   每个槽平均: "
                  << duration.count() / double(ITERATIONS * SLOT_COUNT) << " ns/槽\n";

        // 断开连接
        for (size_t i = 0; i < connections.size(); ++i)
        {
            connect_object::disconnect<TestSignal::signal_test>(&signal, slots[i].get(),
                                                                connections[i]);
        }
    }

    // 6. 测量连接/断开开销
    {
        constexpr int ITERATIONS = 10000;

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < ITERATIONS; ++i)
        {
            TestSlot temp_slot;
            auto conn = connect_object::connect<TestSignal::signal_test>(
                &signal, &temp_slot, &TestSlot::onTest);

            assert(conn);

            // 立即断开
            connect_object::disconnect<TestSignal::signal_test>(&signal, &temp_slot,
                                                                conn);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

        std::cout << "6. 连接+断开开销: " << duration.count() / double(ITERATIONS)
                  << " ns/次\n";
    }

    std::cout << "\n=== 性能测试完成 ===\n\n";
}

// 更详细的对比测试：与std::function对比
void test_vs_std_function()
{
    std::cout << "\n=== 与std::function性能对比 ===\n";

    struct TestObj
    {
        int value = 0;
        void method(int v) noexcept
        {
            value = v;
        }
    };

    constexpr int ITERATIONS = 1000000;
    TestObj obj;

    // 1. std::function调用
    {
        std::function<void(int)> func = [&obj](int v) {
            obj.value = v;
        };

        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < ITERATIONS; ++i)
        {
            func(i);
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

        std::cout << "std::function调用: " << duration.count() / double(ITERATIONS)
                  << " ns/次\n";
    }

    // 2. 原始函数指针调用
    {
        void (*func)(TestObj *, int) = [](TestObj *o, int v) {
            o->value = v;
        };

        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < ITERATIONS; ++i)
        {
            func(&obj, i);
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

        std::cout << "原始函数指针: " << duration.count() / double(ITERATIONS)
                  << " ns/次\n";
    }

    // 3. 你的信号槽
    {
        struct TestSignal : connect_object
        {
            using signal_test = void(int);
        };
        struct TestSlot : connect_object
        {
            int value = 0;
            void onTest(this TestSlot &slef, int v) noexcept
            {
                slef.value = v;
            }
        };

        TestSignal signal;
        TestSlot slot;

        auto conn = connect_object::connect<TestSignal::signal_test>(&signal, &slot,
                                                                     &TestSlot::onTest);

        assert(conn);

        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < ITERATIONS; ++i)
        {
            signal.emit<TestSignal::signal_test>(i);
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

        std::cout << "你的信号槽: " << duration.count() / double(ITERATIONS)
                  << " ns/次\n";

        connect_object::disconnect<TestSignal::signal_test>(&signal, &slot, conn);
    }
    std::cout << "obj.value: " << obj.value;
    std::cout << "\n=== 对比测试完成 ===\n\n";
}
// NOLINTEND

int main()
{
    test_slot_ptr();
    test_compile_time_overhead();
    test_signal_slot_performance();
    test_vs_std_function();
    std::cout << "main done\n";
    return 0;
}