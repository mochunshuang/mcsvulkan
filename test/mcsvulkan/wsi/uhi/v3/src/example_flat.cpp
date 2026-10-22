// ═══════════════════════════════════════════════════════════════════════
//  example_flat.cpp —— 把 uhi 的三个 kind **完全展开**，没有模板、没有反射
//
//  这是一份"等价物"，不是库的一部分。它的唯一目的：让五脏六腑看得见。
//  与模板版的对应关系见文件末尾的对照表；两者的记录逐字相同（同一个脚本）。
//
//     树 = one_of< refines<click, dbl>, drag >        // 三个 kind 一格
//
//  编译：g++ -std=c++26 -O0 example_flat.cpp -o example_flat   （不需要 -freflection）
// ═══════════════════════════════════════════════════════════════════════
#include <array>
#include <cassert>
#include <cstdint>
#include <print>
#include <string_view>

// ═══════════════════════════════════════════════════════════════════════
// ① 平台输入：帧（你那边是 arena_event::input_frame）
// ═══════════════════════════════════════════════════════════════════════
enum class Kind : int { click = 0, dbl = 1, drag = 2, count = 3 };

enum class Change : std::uint8_t { none, press, release, move };
struct frame
{
    Change  change = Change::none;
    double  dx = 0.0, dy = 0.0;
    int64_t now = 0;
};

// ═══════════════════════════════════════════════════════════════════════
// ② "结构"展开成三张表（等价于 plan / awaits_t / holds_ns）
//    —— 这就是模板版在编译期算出来的东西，这里手写死。
// ═══════════════════════════════════════════════════════════════════════
constexpr int NK = 3;
constexpr std::string_view NAME[NK] = {"click", "dbl", "drag"};

// i 必须等 j 结束（refines 的传递闭包）：click 等 dbl
constexpr bool AWAITS[NK][NK] = {
    /*click*/ {false, true, false},
    /*dbl  */ {false, false, false},
    /*drag */ {false, false, false},
};
// 互斥（one_of）：click~drag、dbl~drag
constexpr bool EXCLUDES[NK][NK] = {
    /*click*/ {false, false, true},
    /*dbl  */ {false, false, true},
    /*drag */ {true, true, false},
};
// 兜底时限（毫秒，0 = 不设）
constexpr int64_t HOLD_MS[NK] = {0, 0, 5000};

// 槽位顺序 = 拓扑序（最具体先）：dbl 不 await 谁，click await dbl
constexpr int SLOT[NK] = {1 /*dbl*/, 2 /*drag*/, 0 /*click*/};

// ═══════════════════════════════════════════════════════════════════════
// ③ 载荷类型 + FSM（你写的那三个）
//    契约：FSM::payload 的类型必须与此处"这个 kind 的载荷"完全一致。
//         模板版由 kind_t<"click", click_event> 声明，由 leaf::payload()
//         里的 static_assert 钉死；这里只能靠人守（这正是模板的价值）。
// ═══════════════════════════════════════════════════════════════════════
enum class Step : std::uint8_t { idle, active, done, aborted, hold };

struct click_event
{
    double x = 0.0;
};
struct drag_event
{
    double dist = 0.0;
};

struct click_fsm
{
    bool        s = false;
    click_event payload{}; // ← kind click 的载荷
    Step        step(const frame &f, int64_t) noexcept
    {
        if (!s)
        {
            if (f.change == Change::press) { s = true; payload.x = 10.0; return Step::active; }
            return Step::idle;
        }
        if (f.change == Change::release) { s = false; return Step::done; }
        return Step::active;
    }
    void reset() noexcept { s = false; }
};

struct dbl_fsm
{
    bool    down = false;    // 第一下按着
    bool    waiting = false; // 第一下已抬，等第二下
    int64_t deadline = 0;
    Step    step(const frame &f, int64_t now) noexcept
    {
        if (f.change == Change::press) { down = true; return Step::active; }
        if (f.change == Change::release && down)
        {
            down = false;
            if (waiting) { waiting = false; return Step::done; } // 第二下抬起 ⇒ 双击成立
            waiting  = true;                                     // 第一下抬起 ⇒ 等第二下
            deadline = now + 300;
            return Step::active;
        }
        return (down || waiting) ? Step::active : Step::idle;
    }
    Step tick(int64_t now) noexcept
    {
        if (waiting && now >= deadline) { waiting = false; return Step::aborted; }
        return Step::hold;
    }
    void reset() noexcept { down = false; waiting = false; }
};

struct drag_fsm
{
    bool       down = false;
    drag_event payload{};
    Step       step(const frame &f, int64_t) noexcept
    {
        if (f.change == Change::press) { down = true; payload = {}; return Step::active; }
        if (f.change == Change::release) { if (down) { down = false; return Step::aborted; } return Step::idle; }
        if (!down) return Step::idle;
        if (f.change == Change::move)
        {
            payload.dist += (f.dx < 0 ? -f.dx : f.dx) + (f.dy < 0 ? -f.dy : f.dy);
            if (payload.dist >= 30.0) { down = false; return Step::done; }
        }
        return Step::active;
    }
    void reset() noexcept { down = false; }
};

// ═══════════════════════════════════════════════════════════════════════
// ④ arena：make_aggregate 展开成一个普通 struct
//    self 就是这个 struct 自己。
// ═══════════════════════════════════════════════════════════════════════
struct arena
{
    int    clicks = 0, dbls = 0, drags = 0;
    double last_x = 0.0, last_dist = 0.0;

    void on_click(const click_event &e) noexcept { ++clicks; last_x = e.x; }
    void on_dbl() noexcept { ++dbls; }
    void on_drag(const drag_event &e) noexcept { ++drags; last_dist = e.dist; }
};

// ═══════════════════════════════════════════════════════════════════════
// ⑤ sink：bind 展开成三个 thunk
//    —— 这就是"胜利回调"的全部：{对象指针, 一个不捕获的函数指针}
//    —— 载荷用 const void* 擦除，thunk 里 static_cast 回真类型
// ═══════════════════════════════════════════════════════════════════════
struct sink
{
    void *self = nullptr;
    void (*fn)(void *, const void *) noexcept = nullptr;
    void operator()(const void *p) const noexcept { if (fn) fn(self, p); }
};

inline std::array<sink, NK> bind_all(arena &a)
{
    return {{
        // click：有载荷
        {&a, +[](void *s, const void *p) noexcept {
             static_cast<arena *>(s)->on_click(*static_cast<const click_event *>(p));
         }},
        // dbl：无载荷
        {&a, +[](void *s, const void *) noexcept { static_cast<arena *>(s)->on_dbl(); }},
        // drag：有载荷
        {&a, +[](void *s, const void *p) noexcept {
             static_cast<arena *>(s)->on_drag(*static_cast<const drag_event *>(p));
         }},
    }};
}

// ═══════════════════════════════════════════════════════════════════════
// ⑥ 记录（= ui::trace）
// ═══════════════════════════════════════════════════════════════════════
struct Event
{
    int      kind = 0;
    bool     accepted = false;
    int64_t  at = 0, begin = 0, end = 0;
};
struct Recorder
{
    std::array<Event, 32> trace{};
    int                   n = 0;

    std::string text() const
    {
        std::string s;
        for (int i = 0; i < n; ++i)
        {
            if (i) s += ", ";
            s += NAME[trace[i].kind];
            s += trace[i].accepted ? ":acc@" : ":sup@";
            s += std::to_string(trace[i].at);
        }
        return s;
    }
};

// ═══════════════════════════════════════════════════════════════════════
// ⑦ stage：一个 cell 的存活表（= uhi::stage 展开，NK 个槽全部列出）
// ═══════════════════════════════════════════════════════════════════════
struct Stage
{
    struct Slot
    {
        bool    alive = false;
        bool    defered = false;
        int64_t begin = 0, end = 0;
    };
    std::array<Slot, NK>       s{};
    std::array<std::uint8_t, NK> want{}; // 0 none / 1 done / 2 abort

    // ── 阶段①：只写 want，不判决（= stage::on<g>）──
    void on(int k, Step r, int64_t now) noexcept
    {
        switch (r)
        {
        case Step::active:
            if (!s[k].alive) { s[k].alive = true; s[k].begin = now; }
            break;
        case Step::done:
            if (!s[k].alive) { s[k].alive = true; s[k].begin = now; }
            s[k].end = now;
            want[k] = 1;
            break;
        case Step::aborted: if (s[k].alive) want[k] = 2; break;
        case Step::hold: break;
        case Step::idle: if (s[k].alive && !s[k].defered) want[k] = 2; break;
        }
    }

    bool blocked(int i) const noexcept
    {
        for (int j = 0; j < NK; ++j)
            if (j != i && s[j].alive && AWAITS[i][j]) return true;
        return false;
    }

    // ── 阶段②③：判决（= stage::settle）──
    template <class Log, class Kill>
    void settle(Log &log, Kill &kill, int64_t now) noexcept
    {
        for (int i = 0; i < NK; ++i)
            if (s[i].alive && HOLD_MS[i] > 0 && now - s[i].begin > HOLD_MS[i])
            {
                s[i].alive = false; s[i].defered = false; want[i] = 0;
                kill(i);
            }
        // ① 放弃：静默，且在"争胜"之前处理（否则会被邻居记成 superseded）
        for (int i = 0; i < NK; ++i)
            if (want[i] == 2) { s[i].alive = false; s[i].defered = false; want[i] = 0; kill(i); }
        // ② 争胜：按槽位序（最具体先）
        for (int n = 0; n < NK; ++n)
        {
            const int i = SLOT[n];
            if (want[i] != 1) continue;
            if (!s[i].alive) continue;             // 本帧已被邻居判负：这个 done 属于下一次
            if (blocked(i)) { s[i].defered = true; continue; }
            win(i, log);
        }
        want.fill(0);
        // ③ 唤醒：被挡住的那几个，阻塞者可能刚刚死了
        for (int again = 0; again < NK; ++again)
            for (int i = 0; i < NK; ++i)
                if (s[i].alive && s[i].defered && !blocked(i)) win(i, log);
    }

    template <class Log>
    void win(int i, Log &log) noexcept
    {
        for (int j = 0; j < NK; ++j)
            if (j != i && s[j].alive && (EXCLUDES[i][j] || AWAITS[j][i]))
            {
                s[j].alive = false;
                log(j, false, s[j].begin, s[j].end); // 先发让位
            }
        s[i].alive = false; s[i].defered = false;
        log(i, true, s[i].begin, s[i].end);          // 最后发赢家
    }
};

// ═══════════════════════════════════════════════════════════════════════
// ⑧ ui：把上面全部装起来（= uhi::ui 的成员）
// ═══════════════════════════════════════════════════════════════════════
struct Ui
{
    click_fsm click{};
    dbl_fsm   dbl{};
    drag_fsm  drag{};
    Stage     st{};
    std::array<sink, NK> *sinks = nullptr;
    Recorder  rec{};
    std::array<const void *, NK> payloads{};

    // feed(frame) 展开：三个 FSM 各走一步 → 结算
    void feed(const frame &f)
    {
        const int64_t now = f.now;

        st.on(static_cast<int>(Kind::click), click.step(f, now), now);
        payloads[static_cast<int>(Kind::click)] = &click.payload;

        st.on(static_cast<int>(Kind::dbl), dbl.step(f, now), now);
        payloads[static_cast<int>(Kind::dbl)] = nullptr; // 无载荷

        st.on(static_cast<int>(Kind::drag), drag.step(f, now), now);
        payloads[static_cast<int>(Kind::drag)] = &drag.payload;

        auto kill = [&](int k) {
            switch (static_cast<Kind>(k))
            {
            case Kind::click: click.reset(); break;
            case Kind::dbl: dbl.reset(); break;
            case Kind::drag: drag.reset(); break;
            default: break;
            }
        };
        auto record = [&](int k, bool accepted, int64_t b, int64_t e) {
            if (accepted) (*sinks)[k](payloads[k]);             // ← 唯一的回调点
            if (rec.n < 32) rec.trace[rec.n++] = {k, accepted, now, b, e};
            kill(k);                                            // 判决后复位识别器
        };
        st.settle(record, kill, now);
    }

    // tick(now)：只有时间推进
    void tick(int64_t now)
    {
        st.on(static_cast<int>(Kind::click), Step::hold, now);
        st.on(static_cast<int>(Kind::dbl), dbl.tick(now), now);
        st.on(static_cast<int>(Kind::drag), Step::hold, now);

        auto kill = [&](int k) {
            switch (static_cast<Kind>(k))
            {
            case Kind::click: click.reset(); break;
            case Kind::dbl: dbl.reset(); break;
            case Kind::drag: drag.reset(); break;
            default: break;
            }
        };
        auto record = [&](int k, bool accepted, int64_t b, int64_t e) {
            if (accepted) (*sinks)[k](payloads[k]);
            if (rec.n < 32) rec.trace[rec.n++] = {k, accepted, now, b, e};
            kill(k);
        };
        st.settle(record, kill, now);
    }
};

// ═══════════════════════════════════════════════════════════════════════
int main()
{
    arena a;
    Ui    ui{};
    auto  sinks = bind_all(a);
    ui.sinks    = &sinks;

    const auto show = [&](const char *where) {
        std::print(stderr, "{:<10} [{}]\n", where, ui.rec.text());
        ui.rec.n = 0;
    };

    // ① 单击：click 完成但被 dbl 挡住 → 等 dbl 的超时
    ui.feed({Change::press, 0, 0, 0});
    ui.feed({Change::release, 0, 0, 50});
    show("after rel");
    ui.tick(350);                 // dbl 的 300ms 到期 ⇒ 让位 ⇒ click 落地
    show("tick 350");
    assert(a.clicks == 1 && a.last_x == 10.0);

    // ② 双击：dbl 赢，click 让位
    ui.feed({Change::press, 0, 0, 400});
    ui.feed({Change::release, 0, 0, 440});
    ui.feed({Change::press, 0, 0, 500});
    ui.feed({Change::release, 0, 0, 540});
    show("dbl rel");
    assert(a.dbls == 1);

    // ③ 拖拽：drag 赢，click 与 dbl 一起让位
    ui.feed({Change::press, 0, 0, 900});
    ui.feed({Change::move, 100, 0, 960});
    show("drag");
    assert(a.drags == 1 && a.last_dist == 100.0);

    std::println("--- arena: clicks={} dbls={} drags={} last_dist={} ---", a.clicks, a.dbls,
                 a.drags, a.last_dist);
    return 0;
}
