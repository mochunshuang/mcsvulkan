#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

#include "head.hpp"

using mcs::vulkan::conn::connect_object;

// ============================================================
// 测试对象
// ============================================================
struct W : connect_object
{
    using sig = void(int);
    using sig_b = void(int, int); // 不同签名 = 不同 signal_key

    int hits = 0;
};

// 同签名的两个函数指针 → 同一个 slot_type
void cb_a(W *s, int v) noexcept
{
    s->hits += v;
}
void cb_b(W *s, int v) noexcept
{
    s->hits += v;
}

// ============================================================
// 1. connect：默认追加
// ============================================================
void t_default_append()
{
    W w;
    connect_object::connect<W::sig>(&w, &w, &w, &cb_a);
    connect_object::connect<W::sig>(&w, &w, &w, &cb_a);
    connect_object::connect<W::sig>(&w, &w, &w, &cb_a);

    w.hits = 0;
    w.emit<W::sig>(1);
    assert(w.hits == 3);
    std::cout << "[PASS] default connect appends (3 slots)\n";
}

// ============================================================
// 2. connect_unique：同键去重
// ============================================================
void t_unique()
{
    W w;
    auto *h1 = connect_object::connect_unique<W::sig>(&w, &w, &w, &cb_a);
    auto *h2 = connect_object::connect_unique<W::sig>(&w, &w, &w, &cb_a);
    assert(h1 && h1 == h2);

    w.hits = 0;
    w.emit<W::sig>(1);
    assert(w.hits == 1);
    std::cout << "[PASS] connect_unique dedupes on same key\n";
}

// ============================================================
// 3. connect_unique：返回的句柄依然可用
// ============================================================
void t_unique_handle_is_live()
{
    W w;
    auto *h1 = connect_object::connect_unique<W::sig>(&w, &w, &w, &cb_a);
    auto *h2 = connect_object::connect_unique<W::sig>(&w, &w, &w, &cb_a);
    assert(h1 == h2 && h1->rcvr_hold());

    connect_object::disconnect<W::sig>(&w, &w, h1);
    w.hits = 0;
    w.emit<W::sig>(1);
    assert(w.hits == 0);
    std::cout << "[PASS] connect_unique returns live handle\n";
}

// ============================================================
// 4. connect_replace：同键下 N 个 → 全部清掉，只剩 1 个
// ============================================================
void t_replace_all()
{
    W w;
    for (int i = 0; i < 5; ++i)
        connect_object::connect<W::sig>(&w, &w, &w, &cb_a);

    connect_object::connect_replace<W::sig>(&w, &w, &w, &cb_a);

    w.hits = 0;
    w.emit<W::sig>(1);
    assert(w.hits == 1);
    std::cout << "[PASS] connect_replace collapses all to 1\n";
}

// ============================================================
// 5. connect_replace：重复调用，无泄漏
//    循环里同一个 lambda 表达式 → 同一个闭包类型 → 同一个键
// ============================================================
void t_replace_repeat()
{
    auto counter = std::make_shared<int>(0);
    {
        W w;
        for (int i = 0; i < 20; ++i)
        {
            connect_object::connect_replace<W::sig>(
                &w, &w, &w, [counter](W *s, int v) noexcept { s->hits += v; });
        }
        assert(counter.use_count() == 2); // 外层 1 + 最后一个 lambda 1
        w.hits = 0;
        w.emit<W::sig>(1);
        assert(w.hits == 1);
    }
    assert(counter.use_count() == 1);
    std::cout << "[PASS] connect_replace x20 no leak\n";
}

// ============================================================
// 6. 键的四维各自验证
// ============================================================

// 6a. slot_type 是键的一部分：同签名函数指针 → 同类型
void t_key_slot_type()
{
    W w;
    auto *h1 = connect_object::connect_unique<W::sig>(&w, &w, &w, &cb_a);
    auto *h2 = connect_object::connect_unique<W::sig>(&w, &w, &w, &cb_b);
    assert(h1 == h2);

    w.hits = 0;
    w.emit<W::sig>(1);
    assert(w.hits == 1);
    std::cout << "[PASS] slot_type is part of key\n";
}

// 6b. 不同 lambda 类型 → 不同 slot_type
void t_key_lambda_type()
{
    W w;
    auto *h1 = connect_object::connect_unique<W::sig>(
        &w, &w, &w, [](W *s, int v) noexcept { s->hits += v; });
    auto *h2 = connect_object::connect_unique<W::sig>(
        &w, &w, &w, [](W *s, int v) noexcept { s->hits += 100 * v; });
    assert(h1 != h2);

    w.hits = 0;
    w.emit<W::sig>(1);
    assert(w.hits == 101);
    std::cout << "[PASS] different lambda types = different keys\n";
}

// 6c. recvr 是键的一部分
void t_key_recvr()
{
    W s, r1, r2;
    connect_object::connect_unique<W::sig>(&s, &r1, &r1, &cb_a);
    connect_object::connect_unique<W::sig>(&s, &r2, &r2, &cb_a);
    r1.hits = r2.hits = 0;
    s.emit<W::sig>(1);
    assert(r1.hits == 1 && r2.hits == 1);
    std::cout << "[PASS] recvr is part of key\n";
}

// 6d. signal_key 是键的一部分 —— 用不同签名区分
void t_key_signal()
{
    W w;
    auto *ha = connect_object::connect_unique<W::sig>(&w, &w, &w, &cb_a);

    auto cb2 = [](W *s, int a, int b) noexcept {
        s->hits += a + b;
    };
    auto *hb = connect_object::connect_unique<W::sig_b>(&w, &w, &w, cb2);

    assert(ha != hb);

    w.hits = 0;
    w.emit<W::sig>(1); // cb_a → +1
    assert(w.hits == 1);
    w.emit<W::sig_b>(1, 2); // cb2  → +3
    assert(w.hits == 4);
    std::cout << "[PASS] signal_key is part of key\n";
}

// 6e. 不同键的 replace 不越界
void t_replace_does_not_touch_other_keys()
{
    W s, r1, r2;
    connect_object::connect<W::sig>(&s, &r1, &r1, &cb_a);
    connect_object::connect<W::sig>(&s, &r1, &r1, &cb_a); // r1 键下 2 个
    connect_object::connect<W::sig>(&s, &r2, &r2, &cb_a); // r2 键下 1 个

    connect_object::connect_replace<W::sig>(&s, &r1, &r1, &cb_a); // 只动 r1

    r1.hits = r2.hits = 0;
    s.emit<W::sig>(1);
    assert(r1.hits == 1);
    assert(r2.hits == 1);
    std::cout << "[PASS] replace scoped to its own key\n";
}

// ============================================================
// 7. 非自连接
// ============================================================
void t_non_self()
{
    W s, r1, r2;
    connect_object::connect_unique<W::sig>(&s, &r1, &r1, &cb_a);
    connect_object::connect_unique<W::sig>(&s, &r2, &r2, &cb_a);

    r1.hits = r2.hits = 0;
    s.emit<W::sig>(3);
    assert(r1.hits == 3 && r2.hits == 3);
    std::cout << "[PASS] non-self connect works\n";
}

// ============================================================
// 8. 同一 sndr 上不同 signal_key 各自独立
//    ★ 修正：用同签名的函数指针，才能保证 replace 命中的是同一键
// ============================================================
struct Multi : connect_object
{
    using sig_a = void(int);
    using sig_b = void(int, int);
    int a_hits = 0;
    int b_hits = 0;
};

// 同签名 → 同一个 slot_type，键相同
void multi_a1(Multi *s, int v) noexcept
{
    s->a_hits += v;
}
void multi_a2(Multi *s, int v) noexcept
{
    s->a_hits += 10 * v;
}
void multi_b(Multi *s, int a, int b) noexcept
{
    s->b_hits += a + b;
}

void t_multi_signal_independent()
{
    Multi m;
    connect_object::connect_unique<Multi::sig_a>(&m, &m, &m, &multi_a1);
    connect_object::connect_unique<Multi::sig_b>(&m, &m, &m, &multi_b);

    // multi_a1 和 multi_a2 同签名 → 同一 slot_type → 键相同 → 真替换
    connect_object::connect_replace<Multi::sig_a>(&m, &m, &m, &multi_a2);

    m.emit<Multi::sig_a>(1);    // multi_a2 → +10
    m.emit<Multi::sig_b>(1, 2); // multi_b  → +3

    assert(m.a_hits == 10);
    assert(m.b_hits == 3);
    std::cout << "[PASS] different signals independent\n";
}

// ============================================================
// 9. 孤儿：connect_unique 不返回死句柄
// ============================================================
void t_orphan_skipped()
{
    W s;
    {
        W r;
        connect_object::connect<W::sig>(&s, &r, &r, &cb_a);
    } // r 析构 → s 的 signal_slot_map 里留下一个孤儿

    W r2;
    auto *h = connect_object::connect_unique<W::sig>(&s, &r2, &r2, &cb_a);
    assert(h);
    r2.hits = 0;
    s.emit<W::sig>(1);
    assert(r2.hits == 1);
    std::cout << "[PASS] orphan skipped by find_existing\n";
}

// ============================================================
// 10. connect_replace 在存在孤儿时也安全
// ============================================================
void t_replace_with_orphan()
{
    W s;
    for (int i = 0; i < 3; ++i)
    {
        W r;
        connect_object::connect<W::sig>(&s, &r, &r, &cb_a); // 三个孤儿
    }

    W live;
    connect_object::connect<W::sig>(&s, &live, &live, &cb_a);
    live.hits = 0;

    connect_object::connect_replace<W::sig>(&s, &live, &live, &cb_a);

    s.emit<W::sig>(1);
    assert(live.hits == 1);
    std::cout << "[PASS] replace with orphans present is safe\n";
}

// ============================================================
// 11. 自连接
// ============================================================
void t_self_connect_all_apis()
{
    {
        W w;
        connect_object::connect<W::sig>(&w, &w, &w, &cb_a);
        connect_object::connect<W::sig>(&w, &w, &w, &cb_a);
        w.hits = 0;
        w.emit<W::sig>(1);
        assert(w.hits == 2);
    }
    {
        W w;
        auto *h1 = connect_object::connect_unique<W::sig>(&w, &w, &w, &cb_a);
        auto *h2 = connect_object::connect_unique<W::sig>(&w, &w, &w, &cb_a);
        assert(h1 == h2);
        w.hits = 0;
        w.emit<W::sig>(1);
        assert(w.hits == 1);
    }
    {
        W w;
        connect_object::connect<W::sig>(&w, &w, &w, &cb_a);
        connect_object::connect<W::sig>(&w, &w, &w, &cb_a);
        connect_object::connect<W::sig>(&w, &w, &w, &cb_a);
        connect_object::connect_replace<W::sig>(&w, &w, &w, &cb_a);
        w.hits = 0;
        w.emit<W::sig>(1);
        assert(w.hits == 1);
    }
    std::cout << "[PASS] self-connect supports all three APIs\n";
}

// ============================================================
// 12. RAII：不手动 disconnect，全部走析构
//     三个不同 lambda 字面量 → 三个不同键 → 三份独立捕获
// ============================================================
void t_raii_no_leak()
{
    auto counter = std::make_shared<int>(0);
    {
        W w;
        connect_object::connect<W::sig>(
            &w, &w, &w, [counter](W *s, int v) noexcept { s->hits += v; });
        connect_object::connect_unique<W::sig>(
            &w, &w, &w, [counter](W *s, int v) noexcept { s->hits += v; });
        connect_object::connect_replace<W::sig>(
            &w, &w, &w, [counter](W *s, int v) noexcept { s->hits += v; });

        assert(counter.use_count() == 1 + 3);
    }
    assert(counter.use_count() == 1);
    std::cout << "[PASS] RAII: all policies released at dtor\n";
}

// ============================================================
// 13. 多参数信号端到端 —— 3 参 emit，unique + replace 都跑
// ============================================================
struct S3 : connect_object
{
    using sig3 = void(int, double, const char *);
    int n = 0;
    double last_d = 0.0;
    const char *last_s = nullptr;
};

void s3_v1(S3 *s, int /*i*/, double d, const char *c) noexcept
{
    ++s->n;
    s->last_d = d;
    s->last_s = c;
}
void s3_v2(S3 *s, int /*i*/, double d, const char * /*c*/) noexcept
{
    s->n += 100;
    s->last_d = d;
}

void t_multi_param_e2e()
{
    S3 w;

    // unique：同键只连一次
    auto *h1 = connect_object::connect_unique<S3::sig3>(&w, &w, &w, &s3_v1);
    auto *h2 = connect_object::connect_unique<S3::sig3>(&w, &w, &w, &s3_v1);
    assert(h1 == h2);

    w.emit<S3::sig3>(1, 1.5, "one");
    assert(w.n == 1);
    assert(w.last_d == 1.5);
    assert(std::string_view(w.last_s) == "one");

    // replace：s3_v1 和 s3_v2 同签名 → 同 slot_type → 同键 → 真替换
    connect_object::connect_replace<S3::sig3>(&w, &w, &w, &s3_v2);

    w.emit<S3::sig3>(2, 2.5, "two");
    assert(w.n == 1 + 100); // v2 生效 → +100，不是 +1
    assert(w.last_d == 2.5);

    // 再 replace 一次，同类型
    connect_object::connect_replace<S3::sig3>(&w, &w, &w, &s3_v2);
    w.emit<S3::sig3>(3, 3.5, "three");
    assert(w.n == 1 + 100 + 100); // 仍然只是 v2

    std::cout << "[PASS] multi-param (3 args) e2e: unique + replace + emit\n";
}

// ============================================================
// 14. emit 时混合"活槽 + 孤儿 + 新加槽"
//     验证：跳过孤儿、触发活槽、顺手清理、再 emit 只留活槽
// ============================================================
void t_emit_mixed_orphan_live()
{
    W sndr;

    // 阶段 1：挂 3 个槽，接收者随后全死
    {
        W r1, r2, r3;
        connect_object::connect<W::sig>(&sndr, &r1, &r1, &cb_a);
        connect_object::connect<W::sig>(&sndr, &r2, &r2, &cb_a);
        connect_object::connect<W::sig>(&sndr, &r3, &r3, &cb_a);
    } // r1/r2/r3 析构 → sndr.signal_slot_map[sig] 里留下 3 个孤儿

    // 阶段 2：挂一个新活槽
    W live;
    connect_object::connect<W::sig>(&sndr, &live, &live, &cb_a);

    // 现在 map 里 = [孤儿, 孤儿, 孤儿, live]
    // 第一次 emit：跳过 3 个孤儿 → 触发 live → 触发 has_expired → 清理
    live.hits = 0;
    sndr.emit<W::sig>(7);
    assert(live.hits == 7);

    // 第二次 emit：孤儿已被清掉，只剩 live
    live.hits = 0;
    sndr.emit<W::sig>(3);
    assert(live.hits == 3);

    // 第三次：再次确认稳定
    live.hits = 0;
    sndr.emit<W::sig>(11);
    assert(live.hits == 11);

    std::cout << "[PASS] emit mixed orphan + live: skip + cleanup + retrigger\n";
}

// ============================================================
// 15. 策略 API + 反向回连
//     同一 hub 双向角色：作为 sender 用策略 API，作为 receiver 保持回连
// ============================================================
struct Node : connect_object
{
    using sig_fwd = void(int);
    using sig_bwd = void(int, int);
    int fwd_hits = 0;
    int bwd_hits = 0;
};

void n_fwd(Node *r, int v) noexcept
{
    r->fwd_hits += v;
}
void n_bwd(Node *s, int a, int b) noexcept
{
    s->bwd_hits += a * b;
}

void t_policy_with_reverse_connect()
{
    // ---- 场景 A：replace + 反向回连 ----
    {
        Node a, b;

        // 正向：a.sig_fwd → b.n_fwd（unique）
        auto *h1 = connect_object::connect_unique<Node::sig_fwd>(&a, &b, &b, &n_fwd);
        assert(h1);

        // 反向：b.sig_bwd → a.n_bwd（普通连接）
        auto *h2 = connect_object::connect<Node::sig_bwd>(&b, &a, &a, &n_bwd);
        assert(h2);

        // 在 a 上用 replace 替换 sig_fwd
        // 不应影响 a 作为接收者的角色（a.slots 里 b.sig_bwd 的槽）
        connect_object::connect_replace<Node::sig_fwd>(&a, &b, &b, &n_fwd);

        // 正向仍然工作
        a.emit<Node::sig_fwd>(3);
        assert(b.fwd_hits == 3);

        // 反向也没被误伤
        b.emit<Node::sig_bwd>(2, 5);
        assert(a.bwd_hits == 10);

        // 再来一轮确认
        a.emit<Node::sig_fwd>(4);
        assert(b.fwd_hits == 7);
        b.emit<Node::sig_bwd>(1, 7);
        assert(a.bwd_hits == 10 + 7);
    }

    // ---- 场景 B：unique + 反向回连 ----
    {
        Node a, b;

        auto *h1 = connect_object::connect_unique<Node::sig_fwd>(&a, &b, &b, &n_fwd);
        auto *h2 = connect_object::connect_unique<Node::sig_fwd>(&a, &b, &b, &n_fwd);
        assert(h1 == h2); // 去重

        auto *h3 = connect_object::connect<Node::sig_bwd>(&b, &a, &a, &n_bwd);
        assert(h3);

        a.emit<Node::sig_fwd>(5);
        assert(b.fwd_hits == 5); // 只触发一次

        b.emit<Node::sig_bwd>(3, 4);
        assert(a.bwd_hits == 12);
    }

    std::cout << "[PASS] policy API + reverse connect coexist\n";
}

// ============================================================
int main()
try
{
    t_default_append();
    t_unique();
    t_unique_handle_is_live();
    t_replace_all();
    t_replace_repeat();

    t_key_slot_type();
    t_key_lambda_type();
    t_key_recvr();
    t_key_signal();
    t_replace_does_not_touch_other_keys();

    t_non_self();
    t_multi_signal_independent();

    t_orphan_skipped();
    t_replace_with_orphan();

    t_self_connect_all_apis();
    t_raii_no_leak();

    // ★ 新增
    t_multi_param_e2e();
    t_emit_mixed_orphan_live();
    t_policy_with_reverse_connect();

    std::cout << "\nall connect policy tests passed\n";
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