// ═══════════════════════════════════════════════════════════════════════
//  lesson.cpp —— 为什么"裁决"可以变成几张表：五段最小实验
//
//  L1  两个候选之间只有三种关系（穷举）
//  L2  三个候选 ⇒ 必须是一张矩阵，而且必须外置
//  L3  同一张矩阵被读成两个方向（能不能赢 / 赢了杀谁）
//  L4  同一帧两个都想赢 ⇒ 次序决定胜负（所以次序必须是语义）
//  L5  合体：表 + 三个函数 + 兜底（不动点迭代）
//
//  编译：g++ -std=c++26 -O0 lesson.cpp -o lesson
// ═══════════════════════════════════════════════════════════════════════
#include <array>
#include <cstdint>
#include <cstdio>
#include <print>
#include <string>
#include <string_view>

constexpr int NK = 3;
constexpr const char *NAME[NK] = {"a", "b", "c"};

// ═══════════════════════════════════════════════════════════════════════
// L1：两个候选之间只有三种关系
//
//   把"我想让 click 和 dbl 的关系是……"这种自然语言穷举一遍：
//     · 它们互不相干          → 各自成立
//     · 它们互斥              → 先到先得（后到的让位）
//     · 其中一个更具体        → 更具体的没说话之前，别发
//   没有第四种。因为两个候选之间能发生的只有：
//     谁先、谁包含谁、谁都不管谁。
// ═══════════════════════════════════════════════════════════════════════
enum class Rel
{
    independent,
    exclusive,
    refines // b 是 a 的更具体版本
};

std::string_view decide_two(Rel r, bool a_done, bool b_done)
{
    switch (r)
    {
    case Rel::independent: return (a_done && b_done) ? "A 和 B 都成立" : a_done ? "A" : b_done ? "B" : "无";
    case Rel::exclusive: return a_done ? "A（先到的赢，B 让位）" : b_done ? "B（先到的赢，A 让位）" : "无";
    case Rel::refines: return b_done ? "B（更具体的赢，A 让位）" : a_done ? "A（但要先等 B 一段窗口）" : "无";
    }
    return "无";
}

void l1()
{
    std::println("── L1  两个候选之间只有三种关系 ──");
    const char *rn[3] = {"互不相干", "互斥", "B 细化 A"};
    const Rel   rs[3] = {Rel::independent, Rel::exclusive, Rel::refines};
    for (int i = 0; i < 3; ++i)
        std::println("  {:<8} A成={:<5} B成={:<5} ⇒ {}", rn[i], false, true, decide_two(rs[i], false, true));
    std::println("  这说明：关系的**种类**是有限的（3 种），所以关系可以被编码成 2 bit。\n");
}

// ═══════════════════════════════════════════════════════════════════════
// 一台"cell"：一个格子里所有候选共用的存活表
//   四个数据 + 三个函数，就是全部
// ═══════════════════════════════════════════════════════════════════════
struct Tables
{
    bool    waits[NK][NK]{}; // i 等 j：j 活着时 i 不能赢
    bool    excl[NK][NK]{};  // i 与 j 互斥：谁先赢谁清场
    int     order[NK] = {0, 1, 2}; // 同帧都完成时，谁先被处理
    int64_t hold[NK]{};            // 兜底：活过这么久没结论 ⇒ 静默判负（0 = 不设）
};

struct Cell
{
    const Tables *T = nullptr;
    std::array<bool, NK>    alive{};
    std::array<bool, NK>    defered{};
    std::array<int64_t, NK> begin{};
    std::array<uint8_t, NK> want{}; // 本帧：0 无 / 1 想赢 / 2 放弃
    std::string             log{};
    int64_t                 now = 0;

    void reset_state() { alive.fill(false); defered.fill(false); begin.fill(0); want.fill(0); log.clear(); }

    // 识别器说一句话（这是 FSM 唯一能对仲裁层做的表达）
    void say(int i, int step) // 0 idle / 1 active / 2 done / 3 abort
    {
        switch (step)
        {
        case 1: if (!alive[i]) { alive[i] = true; begin[i] = now; } break;
        case 2: if (!alive[i]) { alive[i] = true; begin[i] = now; } want[i] = 1; break;
        case 3: if (alive[i]) want[i] = 2; break;
        case 0: if (alive[i] && !defered[i]) want[i] = 2; break;
        }
    }

    bool blocked(int i) const
    {
        for (int j = 0; j < NK; ++j)
            if (j != i && alive[j] && T->waits[i][j]) return true;
        return false;
    }

    void win(int i)
    {
        for (int n = 0; n < NK; ++n)
        {
            const int j = T->order[n];
            if (j != i && alive[j] && (T->excl[i][j] || T->waits[j][i]))
            {
                alive[j] = false;
                log += std::string(NAME[j]) + ":sup ";
            }
        }
        alive[i] = false;
        defered[i] = false;
        log += std::string(NAME[i]) + ":acc ";
    }

    void settle()
    {
        for (int i = 0; i < NK; ++i) // ⓪ 兜底
            if (alive[i] && T->hold[i] > 0 && now - begin[i] > T->hold[i])
            {
                alive[i] = false; defered[i] = false; want[i] = 0;
            }
        for (int i = 0; i < NK; ++i) // ① 放弃：静默
            if (want[i] == 2) { alive[i] = false; defered[i] = false; want[i] = 0; }
        for (int n = 0; n < NK; ++n) // ② 争胜
        {
            const int i = T->order[n];
            if (want[i] != 1 || !alive[i]) continue;
            if (blocked(i)) { defered[i] = true; continue; }
            win(i);
        }
        want.fill(0);
        for (int again = 0; again < NK; ++again) // ③ 唤醒（不动点迭代）
            for (int i = 0; i < NK; ++i)
                if (alive[i] && defered[i] && !blocked(i)) win(i);
    }
};

// ═══════════════════════════════════════════════════════════════════════
// L2：三个候选 ⇒ 必须是一张矩阵，而且必须外置
//
//   a 和 c 互斥，b 和 c 互斥，a 和 b 无关。
//   问题：这条信息该放在哪？
//     · 放进 a 的 FSM 里 ⇒ a 必须知道 c 存在；加第 4 个候选要改 3 个 FSM
//     · 放进表里        ⇒ 加候选只加一行一列，FSM 一个字不用改
//   所以：关系的归属是**格子**，不是候选。
// ═══════════════════════════════════════════════════════════════════════
void l2()
{
    std::println("── L2  三个候选：关系必须外置成矩阵 ──");
    Tables T{};
    T.excl[0][2] = T.excl[2][0] = true; // a~c
    T.excl[1][2] = T.excl[2][1] = true; // b~c
    Cell c{&T};
    c.now = 0;

    std::println("  场景：a、b、c 都在进行中，这一帧 c 说 done");
    c.now = 0;
    c.say(0, 1); // a active
    c.say(1, 1); // b active
    c.say(2, 1); // c active
    c.settle();  // 只开槽，没人想赢
    c.say(2, 2); // c done
    c.settle();
    std::println("  记录： [{}]", c.log);
    std::println("  解读： c 赢，按 order 扫过去把 a、b 都判让位 —— 一次查表扫完，不需要知道 a/b 内部。\n");
}

// ═══════════════════════════════════════════════════════════════════════
// L3：同一张矩阵，被读成两个方向
//
//   waits[a][b] = true 的意思是"a 在等 b"。
//     · blocked(a) 读 waits[a][b]  → a 能不能赢
//     · win(b)     读 waits[a][b]  → b 赢了之后要不要杀 a
//   同一个格子，两处读，后果相反。这就是为什么只需要一张 waits 表。
// ═══════════════════════════════════════════════════════════════════════
void l3()
{
    std::println("── L3  一张表，两个方向 ──");
    Tables T{};
    T.waits[0][1] = true; // a 等 b

    {   // 情形 A：b 自己放弃了（比如时间窗口过期）⇒ a 顺利赢，而且 b 不记 sup
        Cell c{&T};
        c.now = 0;
        c.say(0, 1); c.say(1, 1);
        c.settle();
        c.say(0, 2); // a 完成
        c.settle();
        std::println("  情形A a完成但b还活着 → a 被挡住，记录 [{}]", c.log.empty() ? "空" : c.log);
        c.now = 100;
        c.say(1, 3); // b 放弃
        c.settle();
        std::println("         b 放弃之后      → a 解锁，记录 [{}]  （b 是静默离开，没有 sup）", c.log);
    }
    {   // 情形 B：b 先完成 ⇒ b 赢，而 a 因为"等着 b"必须让位
        Cell c{&T};
        c.now = 0;
        c.say(0, 1); c.say(1, 1);
        c.settle();
        c.say(0, 2); // a 完成（被挡）
        c.settle();
        c.say(1, 2); // b 完成
        c.settle();
        std::println("  情形B b 先完成        → b 赢，a 让位，记录 [{}]", c.log);
    }
    std::println("  结论：wait' 这张表同时决定了「谁挡谁」和「谁杀谁」，两处读，不需要第二张表。\n");
}

// ═══════════════════════════════════════════════════════════════════════
// L4：同一帧两个都想赢 ⇒ 次序决定胜负
//
//   a、b 互斥，两者同一帧都 done。
//   谁赢完全取决于 order —— 所以 order 不能是"随便"的顺序，
//   它必须是**语义**：库里取"细化深度"的拓扑序（最具体先）。
// ═══════════════════════════════════════════════════════════════════════
void l4()
{
    std::println("── L4  同帧双 done：次序就是语义 ──");
    Tables T{};
    T.excl[0][1] = T.excl[1][0] = true;

    for (int variant = 0; variant < 2; ++variant)
    {
        Tables t = T;
        t.order[0] = variant ? 1 : 0;
        t.order[1] = variant ? 0 : 1;
        Cell c{&t};
        c.now = 0;
        c.say(0, 1); c.say(1, 1);
        c.settle();
        c.now = 50;
        c.say(0, 2); c.say(1, 2); // 同一帧两个都 done
        c.settle();
        std::println("  order = [{},{}] ⇒ 记录 [{}]", t.order[0], t.order[1], c.log);
    }
    std::println("  同一份输入、同一张表，只换次序 ⇒ 不同赢家。所以次序必须由结构决定（最具体先），"
                 "不能由声明顺序或内存顺序决定。\n");
}

// ═══════════════════════════════════════════════════════════════════════
// L5：合体 —— 兜底 + 唤醒（不动点迭代）
//
//   a 等 b；b 永远不出结论（卡死）。兜底让 b 静默离场，a 于是落地。
//   "杀一个人可能解锁另一个人" ⇒ 必须反复扫到不动点（N 遍足够，因为 waits 无环）。
// ═══════════════════════════════════════════════════════════════════════
void l5()
{
    std::println("── L5  合体：兜底 + 唤醒 ──");
    Tables T{};
    T.waits[0][1] = true; // a 等 b
    T.hold[1]     = 200;  // b 超过 200ms 没结论 ⇒ 静默判负

    Cell c{&T};
    c.now = 0;
    c.say(0, 1); c.say(1, 1);
    c.settle();
    c.now = 50;
    c.say(0, 2); // a 完成，被 b 挡住
    c.settle();
    std::println("  t=50   a 完成但被 b 挡住          记录 [{}]", c.log.empty() ? "空" : c.log);

    c.now = 150;
    c.settle(); // b 才活了 150 < 200
    std::println("  t=150  兜底未到                   记录 [{}]", c.log.empty() ? "空" : c.log);

    c.now = 250;
    c.settle(); // b 超时 ⇒ 静默离场 ⇒ 同一遍里 a 被唤醒
    std::println("  t=250  b 超时 ⇒ a 解锁            记录 [{}]", c.log);
    std::println("  注意：b 离场没有记录（自己也说不清），a 的赢是它自己早先 done 的结论。\n");
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::println("╔══════════════════════════════════════════════════════════════════╗");
    std::println("║ 从「两个候选」到「一张表」：五段最小实验                          ║");
    std::println("╚══════════════════════════════════════════════════════════════════╝\n");
    l1();
    l2();
    l3();
    l4();
    l5();
    std::println("全部实验结束。");
    return 0;
}
