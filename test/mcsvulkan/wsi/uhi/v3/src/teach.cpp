// ═══════════════════════════════════════════════════════════════════════
//  teach.cpp —— 四个闸门：把"生成 UI 事件"变成"有限单调系统"
//
//  闸门 1  思想：为什么要变、变成什么、凭什么一定成立（伪代码 + 前提）
//  闸门 2  难点：六个难点，每个一个独立小环境，只演示一件事
//  闸门 3  骨架：2 个 kind 的完整系统（手写全部表），麻雀虽小五脏俱全
//  闸门 4  模板：把表交给编译器从**类型**算出来，闸门 3 的代码一行不改
//
//  编译（不需要反射）：
//    g++ -std=c++26 -O0 teach.cpp -o teach -lstdc++exp
// ═══════════════════════════════════════════════════════════════════════
#include <array>
#include <cstdint>
#include <cstdio>
#include <print>
#include <string>
#include <string_view>

// ═══════════════════════════════════════════════════════════════════════
//  词典（kit）—— 四关共用。先给一次，后面每关都直接用它。
//
//  一个"格子"（cell）= 同一台设备上、彼此有关系的若干个候选。
//  它需要且只需要四样数据：
//
//    waits[i][j]  i 等 j：j 还活着时 i 不许赢
//    excl[i][j]   i 与 j 互斥：谁先赢谁清场
//    order[i]     同一帧都完成时，处理次序（语义，不是随意）
//    hold[i]      兜底：活过这么久没结论 ⇒ 静默判负（0 = 不设）
//
//  外加状态：alive/defered/begin/want。就这些，没有别的。
// ═══════════════════════════════════════════════════════════════════════
namespace kit
{
    template <int N>
    struct Tables
    {
        bool waits[N][N]{};
        bool excl[N][N]{};
        int order[N]{};
        int64_t hold[N]{};
    };

    // 识别器唯一能对仲裁层说的话（2 bit + 一个"只过了时间"）
    enum Step : int
    {
        idle = 0,
        active = 1,
        done = 2,
        abort = 3,
        hold = 4
    };

    template <int N>
    struct Cell
    {
        const Tables<N> *T = nullptr;
        explicit Cell(const Tables<N> *t) : T(t) {}

        // ── 状态：只有这几个布尔和时间 ──
        std::array<bool, N> alive{};    // 有没有一个"进行中的假设"
        std::array<bool, N> pending{};  // 已完成、但在等别人 ⇒ 跨帧保留
        std::array<int64_t, N> begin{}; // 这次假设从哪一帧开始
        std::array<uint8_t, N> want{};  // 本帧的临时票：0 无 / 1 想赢 / 2 放弃

        std::string log; // 给教学看的文字记录
        int64_t now = 0;

        // 判决出口：库里就是 record() 那个 lambda。这里用"函数指针 + ctx"表达同一件事。
        const char *const *names = nullptr; // 打印用
        void (*on_verdict)(void *, int, bool) = nullptr;
        void *ctx = nullptr;

        const char *name(int i) const
        {
            return names ? names[i] : "?";
        }
        void emit(int i, bool accepted)
        {
            if (on_verdict)
                on_verdict(ctx, i, accepted); // ← 唯一的判决出口
            log += name(i);
            log += accepted ? ":acc " : ":sup ";
        }

        // 识别器说一句话（仲裁层的全部输入）
        void say(int i, int step)
        {
            switch (step)
            {
            case active:
                if (!alive[i])
                {
                    alive[i] = true;
                    begin[i] = now;
                }
                break;
            case done:
                if (!alive[i])
                {
                    alive[i] = true;
                    begin[i] = now;
                }
                want[i] = 1;
                break;
            case abort:
                if (alive[i])
                    want[i] = 2;
                break;
            case idle:
                if (alive[i] && !pending[i])
                    want[i] = 2;
                break;
            case hold:
                break;
            }
        }

        // 问题一：我现在能不能赢？
        bool blocked(int i) const
        {
            for (int j = 0; j < N; ++j)
                if (j != i && alive[j] && T->waits[i][j])
                    return true;
            return false;
        }

        // 问题二：我赢了，谁必须让位？（同一张表，读另一个方向）
        void win(int i)
        {
            for (int n = 0; n < N; ++n)
            {
                const int j = T->order[n];
                if (j != i && alive[j] && (T->excl[i][j] || T->waits[j][i]))
                {
                    alive[j] = false;
                    pending[j] = false;
                    emit(j, false);
                }
            }
            alive[i] = false;
            pending[i] = false;
            emit(i, true);
        }

        // 一帧的判决：四步，顺序本身就是语义。返回唤醒用了几轮。
        int settle()
        {
            // ⓪ 兜底：删除一个活得太久的假设（仍然是"删除"，保持单调）
            for (int i = 0; i < N; ++i)
                if (alive[i] && T->hold[i] > 0 && now - begin[i] > T->hold[i])
                {
                    alive[i] = false;
                    pending[i] = false;
                    want[i] = 0;
                }
            // ① 放弃：它是"从定义域里删掉"，不是判决 ⇒ 静默
            for (int i = 0; i < N; ++i)
                if (want[i] == 2)
                {
                    alive[i] = false;
                    pending[i] = false;
                    want[i] = 0;
                }
            // ② 争胜：按 order（最具体先）在**剩下的集合**上选择
            for (int n = 0; n < N; ++n)
            {
                const int i = T->order[n];
                if (want[i] != 1 || !alive[i])
                    continue;
                if (blocked(i))
                {
                    pending[i] = true;
                    continue;
                }
                win(i);
            }
            want.fill(0);
            // ③ 唤醒：杀一个人可能解锁另一个人 ⇒ 迭代到不动点
            int rounds = 0;
            for (int again = 0; again < N; ++again)
            {
                bool moved = false;
                for (int i = 0; i < N; ++i)
                    if (alive[i] && pending[i] && !blocked(i))
                    {
                        win(i);
                        moved = true;
                    }
                if (!moved)
                    break;
                ++rounds;
            }
            return rounds;
        }
    };

    // 胜利回调：{对象指针, 一个不捕获的函数指针}（两字，可延迟绑定、可拷贝、零堆）
    struct Sink
    {
        void *self = nullptr;
        void (*fn)(void *, const void *) noexcept = nullptr;
        void operator()(const void *p) const noexcept
        {
            if (fn)
                fn(self, p);
        }
    };
} // namespace kit

// ═══════════════════════════════════════════════════════════════════════
//  闸门 1：思想
//
//  原始问题（你的需求）：
//    一堆并行的小状态机各自跑；每一帧它们各自可能说"我成了"；
//    必须挑出**恰好一个**解释，并让其他人退场；不能有内存分配。
//
//  关键观察：这句话里没有"算法"，只有"约束"。
//    ① 每个识别器只知道自己 —— 所以"谁赢"它算不出来
//    ② 关系只有三种 —— 互不相干 / 互斥 / 一个更具体
//    ③ 每个识别器对外的全部输出只有 5 个值 —— 内部再复杂也被压平
//
//  于是它变成一个有限单调系统（least fixed point，最小不动点）：
//
//    定义域 D = { i | alive[i] }                     有限
//    每帧输入 = 每个 i 说一句 ∈ {idle,active,done,abort}
//    算子 F(D) = D \ { 被某个已选中的 i 清掉的人 }     单调（只会变小）
//    判决     = lfp(F) 在"选中 i"上的投影
//
//  伪代码（就是闸门 2/3/4 里那个 settle）：
//
//      D = {i | alive[i]}
//      删掉 hold 到期的                     // 仍是删除
//      删掉说 abort/idle 的                 // 仍不是判决
//      for i in order:                      // order 是语义：最具体先
//          if i ∈ D and i says done：
//              if 没有任何 j∈D 满足 waits[i][j]:  选中 i；D -= （i 以及与 i 冲突的人）
//              else: 记下 i 在等（跨帧保留）
//      repeat 上面第 3 步，直到这一轮没有任何变化（最多 N 轮）
//
//  凭什么**一定成立**：
//    ① 终止：每轮至少删除一个元素，D 有限 ⇒ ≤N 轮
//    ② 唯一：算子是单调的，最小不动点唯一 ⇒ 结果与"谁先被扫到"无关
//    ③ 确定：唯一需要顺序的地方只有 order，而它是语义
//  三个前提（缺一个就化不了，这是难点）：
//    P1 识别器输出必须归一成 5 个值        （否则输入域无限）
//    P2 关系必须只有两两、且种类有限        （否则不是矩阵）
//    P3 同帧并列必须有语义次序              （否则结果不确定）
//
//  下面三关分别验证这三个前提 + 四个难点。
// ═══════════════════════════════════════════════════════════════════════
namespace g1
{
    void run()
    {
        std::println("╔══════════════════════════════════════════════════════════════╗");
        std::println("║ 闸门 1  思想：判决 = 有限单调系统的最小不动点                ║");
        std::println("╚══════════════════════════════════════════════════════════════╝");
        std::println("  原始需求：N 个并行识别器，每帧各自可能说 done，");
        std::println("            挑出恰好一个解释、其余退场、零分配。");
        std::println("  观察①  识别器只知道自己 ⇒ 裁决权必须外置到格子");
        std::println("  观察②  关系只有 3 种     ⇒ 可以编码成 2 bit × N²");
        std::println("  观察③  对外输出只有 5 值 ⇒ 输入域有限");
        std::println("  ⇒ 判决退化成：在一张 N×N 布尔表上求最小不动点，≤N 轮收敛。");
        std::println("  三个前提：P1 输出归一  P2 关系两两有限  P3 并列有语义次序\n");
    }
} // namespace g1

// ═══════════════════════════════════════════════════════════════════════
//  闸门 2：六个难点，每个一个独立环境
//
//  这一关的每个 demo 都**不是**完整系统：没有 FSM、没有事件、没有回调。
//  它只回答一个问题："这件事到底能不能做到，怎么做到。"
//  统一用 N=4 的格子，四位是 a,b,c,d。
// ═══════════════════════════════════════════════════════════════════════
namespace g2
{
    using kit::Cell;
    using kit::Step;
    using kit::Tables;
    inline constexpr const char *NAMES[4] = {"a", "b", "c", "d"};
    using T4 = Tables<4>;
    using C4 = Cell<4>;

    void reset(C4 &c)
    {
        c.alive.fill(false);
        c.pending.fill(false);
        c.begin.fill(0);
        c.want.fill(0);
        c.log.clear();
        c.now = 0;
    }

    // ── 难点 1：关系必须外置（识别器之间不能互相认识）──────────────
    //   证据形态：同一份 FSM 代码，换一张表就换一套语义，FSM 一个字不改。
    void d1_relations_live_in_the_table()
    {
        std::println("── 难点 1：关系外置 ──");
        T4 t{};
        t.order[0] = 0;
        t.order[1] = 1;
        t.order[2] = 2;
        t.order[3] = 3;
        t.excl[0][3] = t.excl[3][0] = true; // a ~ d
        t.excl[1][3] = t.excl[3][1] = true; // b ~ d

        C4 c{&t};
        c.names = NAMES;
        reset(c);
        for (int i = 0; i < 4; ++i)
            c.say(i, Step::active);
        c.settle();
        c.say(3, Step::done); // d 完成
        c.settle();
        std::println("  表说 a~d、b~d ⇒ d 赢后清场：记录 [{}{}]", c.log, "");
        std::println("  关键：a 和 b 的\"代码\"完全不知道 d 存在。关系只住在表里。\n");
    }

    // ── 难点 2：一张表被读成两个方向 ────────────────────────────────
    void d2_one_table_two_directions()
    {
        std::println("── 难点 2：一张 waits 表，两个读法 ──");
        T4 t{};
        t.order[0] = 0;
        t.order[1] = 1;
        t.waits[0][1] = true; // a 等 b

        { // b 自己放弃 ⇒ a 解锁，且 b 不记 sup
            C4 c{&t};
            c.names = NAMES;
            reset(c);
            c.say(0, Step::active);
            c.say(1, Step::active);
            c.settle();
            c.say(0, Step::done);
            c.settle();
            const std::string mid = c.log;
            c.now = 100;
            c.say(1, Step::abort);
            c.settle();
            std::println("  b 放弃 ⇒ a 解锁：中段[{}] 末段[{}]", mid.empty() ? "空" : mid,
                         c.log);
        }
        { // b 先完成 ⇒ b 赢，a 让位（因为 a 在等 b）
            C4 c{&t};
            c.names = NAMES;
            reset(c);
            c.say(0, Step::active);
            c.say(1, Step::active);
            c.settle();
            c.say(0, Step::done);
            c.settle(); //NOTE: 因为 a 等 b,那么a先完成也没用
            c.say(1, Step::done);
            c.settle();
            std::println("  b 完成 ⇒ a 让位：记录 [{}]", c.log);
        }
        std::println("  同一格 waits[0][1]：blocked() 读它决定\"我能不能赢\"，");
        std::println("  win(1) 读它决定\"要不要杀 a\"。所以一张表够。\n");
    }

    // ── 难点 3：同帧并列 ⇒ 次序就是语义 ─────────────────────────────
    void d3_tie_order_is_semantics()
    {
        std::println("── 难点 3：同帧并列 ──");
        T4 base{};
        base.excl[0][1] = base.excl[1][0] = true; // a~b
        for (int v = 0; v < 2; ++v)
        {
            T4 t = base;
            t.order[0] = v ? 1 : 0;
            t.order[1] = v ? 0 : 1;
            t.order[2] = 2;
            t.order[3] = 3;
            C4 c{&t};
            c.names = NAMES;
            reset(c);
            c.say(0, Step::active);
            c.say(1, Step::active);
            c.settle();
            c.say(0, Step::done);
            c.say(1, Step::done); // 同一帧
            c.settle();
            std::println("  order 前两位=[{},{}] ⇒ 记录 [{}]", t.order[0], t.order[1],
                         c.log);
        }
        std::println(
            "  同一输入换次序 ⇒ 换赢家。所以 order 必须由结构决定（最具体先）。\n");
    }

    // ── 难点 4：兜底——"删除"也保持单调 ─────────────────────────────
    void d4_hold_is_still_a_deletion()
    {
        std::println("── 难点 4：兜底 ──");
        T4 t{};
        t.order[0] = 0;
        t.order[1] = 1;
        t.waits[0][1] = true; // a 等 b
        t.hold[1] = 200;      // b 超 200ms 没结论 ⇒ 静默判负

        C4 c{&t};
        c.names = NAMES;
        reset(c);
        c.now = 0;
        c.say(0, Step::active);
        c.say(1, Step::active);
        c.settle();
        c.now = 50;
        c.say(0, Step::done);
        c.settle();
        std::println("  t=50  a 完成被 b 挡住   记录 [{}]", c.log.empty() ? "空" : c.log);
        c.now = 150;
        c.settle();
        std::println("  t=150 兜底未到期        记录 [{}]", c.log.empty() ? "空" : c.log);
        c.now = 250;
        c.settle();
        std::println("  t=250 b 超时 ⇒ a 解锁   记录 [{}]", c.log);
        std::println("  b 离场没有记录：自己也说不清的事，不该通知任何人。\n");
    }

    // ── 难点 5：槽位才是状态，FSM 只是摘要 ──────────────────────────
    void d5_slot_owns_the_state()
    {
        std::println("── 难点 5：槽位是状态 ──");
        struct Stub // 一个"有内部状态"的识别器
        {
            bool holding = false;
            void reset() noexcept
            {
                holding = false;
            }
        };
        T4 t{};
        t.order[0] = 0;
        C4 c{&t};
        c.names = NAMES;
        reset(c);

        Stub s;
        s.holding = true; // 它以为自己按着
        c.say(0, Step::active);
        c.settle();
        std::println("  识别器说 active ⇒ 槽位 alive={} （它开始了一次假设）",
                     c.alive[0]);

        // 槽位被清（比如超时），识别器必须被通知
        c.now = 1000;
        t.hold[0] = 100;
        c.settle();
        const bool was_alive = c.alive[0];
        if (!was_alive && s.holding)
            s.reset(); // ← 这就是 kill 回调要干的事
        std::println("  兜底清掉槽位 alive={} ⇒ 必须同时 reset 识别器（holding={}）",
                     c.alive[0], s.holding);
        std::println("  否则下次按下不会重开假设，会用上一次的载荷。这条真出过 bug。\n");
    }

    // ── 难点 6：唤醒要迭代到不动点，轮数 ≤ N ────────────────────────
    void d6_wake_is_a_fixed_point()
    {
        std::println("── 难点 6：唤醒 = 不动点迭代 ──");
        // 链：a 等 b，b 等 c。c 完成 ⇒ c 赢杀 b ⇒ b 死解锁 a ⇒ a 赢
        T4 t{};
        t.order[0] = 2;
        t.order[1] = 1;
        t.order[2] = 0; // 最具体先：c,b,a
        t.order[3] = 3;
        t.waits[0][1] = true; // a 等 b
        t.waits[1][2] = true; // b 等 c

        C4 c{&t};
        c.names = NAMES;
        reset(c);
        c.say(0, Step::active);
        c.say(1, Step::active);
        c.say(2, Step::active);
        c.settle();
        c.say(0, Step::done);
        c.say(1, Step::done);
        c.say(2, Step::done);
        const int rounds = c.settle();
        std::println("  三级链一次判决 ⇒ 记录 [{}]（唤醒用了 {} 轮）", c.log, rounds);
        std::println("  注意 0 轮：因为 order 是**拓扑序**（最具体先），级联在 ② "
                     "一遍里就走完了。");
        std::println("  ③ 真正负责的是**跨帧**的 pending —— 见难点 4：t=250 那一帧 a "
                     "并没有说 done，");
        std::println(
            "  它是上一帧 pending 下来的，靠 ③ 被唤醒。这才是\"不动点\"存在的理由。\n");
    }

    void run()
    {
        std::println("╔══════════════════════════════════════════════════════════════╗");
        std::println("║ 闸门 2  六个难点，每个一个独立环境（没有 FSM、没有事件）     ║");
        std::println("╚══════════════════════════════════════════════════════════════╝");
        d1_relations_live_in_the_table();
        d2_one_table_two_directions();
        d3_tie_order_is_semantics();
        d4_hold_is_still_a_deletion();
        d5_slot_owns_the_state();
        d6_wake_is_a_fixed_point();
    }
} // namespace g2

// ═══════════════════════════════════════════════════════════════════════
//  闸门 3：最小骨架 —— 2 个 kind 的完整系统，全部手写
//
//    click（有载荷）与 dbl（无载荷），关系：dbl 细化 click。
//    五脏：事件、FSM、表、格子、sink、arena、feed、记录
// ═══════════════════════════════════════════════════════════════════════
namespace g3
{
    using kit::Cell;
    using kit::Sink;
    using kit::Step;
    using kit::Tables;

    enum : int
    {
        CLICK = 0,
        DBL = 1,
        N = 2
    };
    constexpr const char *NAME[N] = {"click", "dbl"};

    // ① 四张表（手写；闸门 4 会让编译器生成同样的东西）
    constexpr Tables<N> make_tables()
    {
        Tables<N> t{};
        t.waits[CLICK][DBL] = true; // click 等 dbl
        t.order[0] = DBL;           // 最具体先
        t.order[1] = CLICK;
        return t;
    }
    inline constexpr Tables<N> TABLES = make_tables();

    // ② 平台帧（真实项目里是 arena_event::input_frame）
    enum class Change : std::uint8_t
    {
        none,
        press,
        release
    };
    struct Frame
    {
        Change change = Change::none;
        int64_t x = 0;
        int64_t now = 0;
    };

    // ③ 识别器：只说 5 个值，内部随便复杂
    struct ClickFsm
    {
        bool down = false;
        int64_t payload_x = 0; // 载荷存在识别器里
        int step(const Frame &f) noexcept
        {
            if (!down)
            {
                if (f.change == Change::press)
                {
                    down = true;
                    payload_x = f.x;
                    return Step::active;
                }
                return Step::idle;
            }
            if (f.change == Change::release)
            {
                down = false;
                return Step::done;
            }
            return Step::active;
        }
        void reset() noexcept
        {
            down = false;
        }
    };

    struct DblFsm
    {
        bool down = false, waiting = false;
        int64_t deadline = 0;
        int step(const Frame &f) noexcept
        {
            if (f.change == Change::press)
            {
                down = true;
                return Step::active;
            }
            if (f.change == Change::release && down)
            {
                down = false;
                if (waiting)
                {
                    waiting = false;
                    return Step::done;
                }
                waiting = true;
                deadline = f.now + 300;
                return Step::active;
            }
            return (down || waiting) ? Step::active : Step::idle;
        }
        int tick(int64_t now) noexcept // 只有时间前进时被调用
        {
            if (waiting && now >= deadline)
            {
                waiting = false;
                return Step::abort;
            }
            return Step::hold;
        }
        void reset() noexcept
        {
            down = false;
            waiting = false;
        }
    };

    // ④ arena：一个普通 struct，方法就是 on_<kind>
    struct Arena
    {
        int clicks = 0, dbls = 0;
        int64_t last_x = 0;
        void on_click(const void *p) noexcept
        {
            ++clicks;
            last_x = *static_cast<const int64_t *>(p);
        }
        void on_dbl(const void *) noexcept
        {
            ++dbls;
        }
    };

    // ⑤ 胜利回调：每个 kind 一个 thunk（= bind）
    inline std::array<Sink, N> bind_all(Arena &a)
    {
        return {{
            {&a, +[](void *s,
                     const void *p) noexcept { static_cast<Arena *>(s)->on_click(p); }},
            {&a, +[](void *s,
                     const void *p) noexcept { static_cast<Arena *>(s)->on_dbl(p); }},
        }};
    }

    // ⑥ 舞台：把 FSM、格子、回调、记录装起来
    struct Ui
    {
        ClickFsm click{};
        DblFsm dbl{};
        Cell<N> cell{&TABLES};
        std::array<Sink, N> *sink = nullptr;
        std::string trace;

        Ui()
        {
            cell.names = NAME;
            cell.on_verdict = &Ui::verdict; // 判决出口接到 record()
            cell.ctx = this;
        }

        void reset_kind(int k)
        {
            if (k == CLICK)
                click.reset();
            else
                dbl.reset();
        }
        void record(int k, bool accepted)
        {
            if (accepted)
            {
                const void *p =
                    (k == CLICK) ? static_cast<const void *>(&click.payload_x) : nullptr;
                (*sink)[k](p); // ← 全系统唯一的回调点
            }
            trace += NAME[k];
            trace += accepted ? ":acc " : ":sup ";
            reset_kind(k);
        }

        // ⑦ 一帧：所有 FSM 走一步（只写票）→ 判决 → 记录/回调/复位
        void feed(const Frame &f)
        {
            cell.now = f.now;
            cell.say(CLICK, click.step(f));
            cell.say(DBL, dbl.step(f));
            settle_and_emit();
        }
        void tick(int64_t now)
        {
            cell.now = now;
            cell.say(CLICK, Step::hold);
            cell.say(DBL, dbl.tick(now));
            settle_and_emit();
        }
        static void verdict(void *self, int k, bool acc) noexcept
        {
            static_cast<Ui *>(self)->record(k, acc);
        }
        void settle_and_emit()
        {
            cell.log.clear();
            cell.settle(); // 判决会通过 cell.on_verdict 回到 record()
        }
    };

    void run()
    {
        std::println("╔══════════════════════════════════════════════════════════════╗");
        std::println("║ 闸门 3  最小骨架：2 个 kind，手写全部表                       ║");
        std::println("╚══════════════════════════════════════════════════════════════╝");
        Arena a;
        Ui ui{};
        auto sinks = bind_all(a);
        ui.sink = &sinks;

        // 单击：click 完成但被 dbl 挡住 → dbl 窗口过期后 click 落地
        ui.feed({Change::press, 10, 0});
        ui.feed({Change::release, 10, 50});
        std::println("  t=50  松手后            trace = [{}]", ui.trace);
        ui.tick(350);
        std::println("  t=350 tick 让 dbl 过期  trace = [{}]", ui.trace);

        // 双击
        ui.trace.clear();
        ui.feed({Change::press, 0, 400});
        ui.feed({Change::release, 0, 440});
        ui.feed({Change::press, 0, 500});
        ui.feed({Change::release, 0, 540});
        std::println("  双击                    trace = [{}]", ui.trace);
        std::println("  arena: clicks={} dbls={} last_x={}\n", a.clicks, a.dbls,
                     a.last_x);
    }
} // namespace g3

// ═══════════════════════════════════════════════════════════════════════
//  闸门 4：把表交给编译器 —— 从**类型**生成闸门 3 那四张表
//
//  闸门 3 里 TABLES 是手写的。这里它变成：
//      one_of< refines< click, dbl >, drag >
//    ↓ 编译期
//      waits / excl / order / hold
//  而 kit::Cell / g3::Ui 这些运行期代码**一行不改**。
// ═══════════════════════════════════════════════════════════════════════
namespace g4
{
    using kit::Cell;
    using kit::Tables;

    constexpr int MAXK = 8;

    struct KindInfo
    {
        const char *name = "";
        int64_t hold = 0;
    };
    struct Rel
    {
        int from = 0, to = 0;
        bool sym = false; // true = 互斥（对称），false = from 等 to（细化）
    };
    struct Plan
    {
        int n = 0;
        KindInfo k[MAXK]{};
        int nr = 0;
        Rel r[MAXK * MAXK]{};
    };

    // 拼接两张表：把右边的关系下标整体平移（这一步忘了就是"关系指错人"）
    consteval Plan shifted(Plan p, int base)
    {
        for (int e = 0; e < p.nr; ++e)
        {
            p.r[e].from += base;
            p.r[e].to += base;
        }
        return p;
    }
    consteval Plan operator+(Plan a, Plan b)
    {
        for (int i = 0; i < b.n; ++i)
            a.k[a.n + i] = b.k[i];
        a.n += b.n;
        for (int e = 0; e < b.nr; ++e)
            a.r[a.nr + e] = b.r[e];
        a.nr += b.nr;
        return a;
    }

    // ── 语法：只有 plan()，运行期不存在 ─────────────────────────────
    template <class K, int64_t Hold = 0>
    struct leaf
    {
        static consteval Plan plan()
        {
            Plan p{};
            p.n = 1;
            p.k[0] = {K::name, Hold};
            return p;
        }
    };

    template <class A, class B>
    struct refines
    {
        static consteval Plan plan()
        {
            const Plan pa = A::plan();
            Plan p = pa + shifted(B::plan(), pa.n);
            for (int i = 0; i < pa.n; ++i)
                for (int j = pa.n; j < p.n; ++j)
                    p.r[p.nr++] = {i, j, false};
            return p;
        }
    };

    template <class... Ts>
    struct all_of
    {
        static consteval Plan plan()
        {
            Plan p{};
            ((p = p + shifted(Ts::plan(), p.n)), ...);
            return p;
        }
    };

    template <class... Ts>
    struct one_of
    {
        static consteval Plan plan()
        {
            Plan p{};
            int off[MAXK]{}; // 每棵子树在表里的起点
            int cnt[MAXK]{};
            int i = 0;
            ((off[i] = p.n, p = p + shifted(Ts::plan(), p.n), cnt[i] = p.n - off[i], ++i),
             ...);
            for (int a = 0; a < i; ++a)
                for (int b = a + 1; b < i; ++b)
                    for (int x = off[a]; x < off[a] + cnt[a]; ++x)
                        for (int y = off[b]; y < off[b] + cnt[b]; ++y)
                            p.r[p.nr++] = {x, y, true};
            return p;
        }
    };

    // ── 四张表 = plan 的纯函数 ──────────────────────────────────────
    template <class Tree>
    inline constexpr Plan plan_v = Tree::plan();

    template <class Tree>
    consteval Tables<plan_v<Tree>.n> build_tables()
    {
        constexpr int N = plan_v<Tree>.n;
        const Plan &p = plan_v<Tree>;
        Tables<N> t{};
        for (int e = 0; e < p.nr; ++e)
            if (p.r[e].sym)
                t.excl[p.r[e].from][p.r[e].to] = t.excl[p.r[e].to][p.r[e].from] = true;
            else
                t.waits[p.r[e].from][p.r[e].to] = true;
        for (int i = 0; i < N; ++i)
            t.hold[i] = p.k[i].hold;

        // order = waits 图的反向拓扑序（"什么都不等"的排第一 = 最具体先）
        int indeg[MAXK]{};
        for (int e = 0; e < p.nr; ++e)
            if (!p.r[e].sym)
                ++indeg[p.r[e].from];
        int cnt = 0;
        while (cnt < N)
        {
            bool moved = false;
            for (int i = 0; i < N; ++i)
                if (indeg[i] == 0)
                {
                    indeg[i] = -1;
                    t.order[cnt++] = i;
                    moved = true;
                    for (int e = 0; e < p.nr; ++e)
                        if (!p.r[e].sym && p.r[e].to == i)
                            --indeg[p.r[e].from];
                }
            if (!moved)
                break; // 成环（真实库里 validate 会在编译期拒绝）
        }
        return t;
    }

    template <class Tree>
    inline constexpr auto TABLES = build_tables<Tree>();

    // ── kind：名字 + 载荷类型（这里载荷用 int64_t 简化）─────────────
    struct K_click
    {
        static constexpr const char *name = "click";
        using payload = int64_t;
    };
    struct K_dbl
    {
        static constexpr const char *name = "dbl";
        using payload = void;
    };
    struct K_drag
    {
        static constexpr const char *name = "drag";
        using payload = int64_t;
    };

    // ── 树：一行声明，四张表就出来了 ────────────────────────────────
    using tree = one_of<refines<leaf<K_click>, leaf<K_dbl>>, leaf<K_drag>>;

    // ── 打印生成出来的表（这就是"把声明读成机器码"）─────────────────
    void show_tables()
    {
        constexpr int N = plan_v<tree>.n;
        const auto &p = plan_v<tree>;
        std::println("  编译期生成的 plan（{} 个 kind）:", N);
        for (int i = 0; i < N; ++i)
            std::println("    [{}] {:<8} hold={}ms", i, p.k[i].name, p.k[i].hold);
        std::print("  关系:");
        for (int e = 0; e < p.nr; ++e)
            std::print(" {}{}{}", p.k[p.r[e].from].name, p.r[e].sym ? "~" : "->",
                       p.k[p.r[e].to].name);
        std::println("");
        std::print("  order（最具体先）:");
        for (int i = 0; i < N; ++i)
            std::print(" {}", p.k[TABLES<tree>.order[i]].name);
        std::println("\n");
    }

    void run()
    {
        std::println("╔══════════════════════════════════════════════════════════════╗");
        std::println(
            "║ 闸门 4  模板：四张表 = 类型树的纯函数                          ║");
        std::println("╚══════════════════════════════════════════════════════════════╝");
        std::println("  声明： one_of< refines<click, dbl>, drag >\n");
        show_tables();

        // 同一份 kit::Cell，换上编译器生成的表 —— 运行期代码零改动
        constexpr int N = plan_v<tree>.n;
        const char *names[N];
        for (int i = 0; i < N; ++i)
            names[i] = plan_v<tree>.k[i].name;
        Cell<N> c{&TABLES<tree>};
        c.names = names;
        c.now = 0;
        for (int i = 0; i < N; ++i)
            c.say(i, kit::active);
        c.settle();
        std::println("  三个候选都在进行中，这一帧 drag 完成：");
        c.say(2, kit::done);
        c.settle();
        std::println("  记录 = [{}]（下标 {}={} {}={} {}={}）", c.log, 0,
                     plan_v<tree>.k[0].name, 1, plan_v<tree>.k[1].name, 2,
                     plan_v<tree>.k[2].name);
        std::println("  注意 click 和 dbl 都被清掉：因为 one_of 让 drag 和它俩互斥。");
        std::println("  这些关系不是我写的，是类型树算出来的。\n");
    }
} // namespace g4

// ═══════════════════════════════════════════════════════════════════════
int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    g1::run();
    g2::run();
    g3::run();
    g4::run();
    std::println("四关走完。同一份结算代码（kit::Cell::settle）在每一关都没有改过。");
    return 0;
}
