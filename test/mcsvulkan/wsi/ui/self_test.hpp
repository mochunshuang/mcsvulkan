#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// UI 层自测：不建窗口、不依赖 GLFW 运行时、时间由测试显式注入 —— 完全确定性。
//
//   运行：test_glfw.exe --selftest      （退出码 0 = 全部通过）
//
// 每个用例既是一条断言，也是一条“语义声明”：在 来源层 → 识别层 → 裁决层 → 订阅层
// 这条链上，什么条件下谁胜出、谁收到什么、以及可观察到的顺序。
// ═══════════════════════════════════════════════════════════════════════════

#include <cstddef>
#include <format>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mcs::vulkan::ui::selftest
{
    using hardware = ::mcs::vulkan::input::glfw_forward;

    // ── 记录型控件：把收到的每个结构记成一行（断言用） ──
    //    swallow=false 表示“自己处理不了”，用来验冒泡
    class recorder : public widget
    {
      public:
        recorder(std::string name, rect area) : widget{std::move(name), area} {}

        std::vector<std::string> log{};
        bool swallow{true};

        void clear() noexcept
        {
            log.clear();
        }

        bool on_pointer_down(const pointer_event &e) override
        {
            log.push_back(std::format("raw:down:{}", e.button));
            return swallow;
        }
        bool on_pointer_up(const pointer_event &e) override
        {
            log.push_back(std::format("raw:up:{}", e.button));
            return swallow;
        }
        bool on_wheel(const wheel_event &) override
        {
            log.push_back("raw:wheel");
            return swallow;
        }
        bool on_key(const key_event &e) override
        {
            log.push_back(std::format("raw:key:{}", e.key));
            return swallow;
        }
        bool on_tap(const tap_gesture &e) override
        {
            log.push_back(std::format("tap:{}:{}", tap_name(e.count), e.count));
            return swallow;
        }
        bool on_long_press(const long_press_gesture &) override
        {
            log.push_back("long_press");
            return swallow;
        }
        bool on_drag_begin(const drag_gesture &) override
        {
            log.push_back("drag_begin");
            return swallow;
        }
        bool on_drag_update(const drag_gesture &) override
        {
            log.push_back("drag_update");
            return swallow;
        }
        bool on_drag_end(const drag_gesture &) override
        {
            log.push_back("drag_end");
            return swallow;
        }
        bool on_zoom(const zoom_gesture &) override
        {
            log.push_back("zoom");
            return swallow;
        }
        bool on_shortcut(const shortcut_gesture &e) override
        {
            log.push_back(std::format("shortcut:{}", e.key));
            return swallow;
        }
    };

    // ── 驱动：样本与时间都由测试给 ──
    struct driver
    {
        ui_input &pipe;
        event_time clock{};
        event::position2d_event cursor{0.0, 0.0};
        event::ModifierKey mods{event::ModifierKey::None()};
        bool has_cursor_{false};

        // 时间推进：等待窗口到期的候选在这里胜出
        void at(milliseconds d)
        {
            clock += d;
            pipe.update(clock);
        }

        void move(double x, double y)
        {
            if (has_cursor_ && x == cursor.xpos && y == cursor.ypos)
                return; // 真实输入也不会为“没动”发样本
            const double dx = has_cursor_ ? x - cursor.xpos : 0.0;
            const double dy = has_cursor_ ? y - cursor.ypos : 0.0;
            has_cursor_ = true;
            cursor = event::position2d_event{x, y};
            pipe.push_sample(
                input_sample{clock, cursor, mods, raw::pointer_move{dx, dy}});
        }

        void down(event::MouseButtons b, double x, double y)
        {
            move(x, y);
            pipe.push_sample(input_sample{clock, cursor, mods, raw::pointer_down{b}});
        }

        void up(event::MouseButtons b, double x, double y)
        {
            move(x, y);
            pipe.push_sample(input_sample{clock, cursor, mods, raw::pointer_up{b}});
        }

        void key_down(event::Key k, const event::ModifierKey &m)
        {
            mods = m;
            pipe.push_sample(input_sample{clock, cursor, m, raw::key_down{k, 0}});
        }

        void key_up(event::Key k, const event::ModifierKey &m)
        {
            mods = m;
            pipe.push_sample(input_sample{clock, cursor, m, raw::key_up{k, 0}});
        }

        void wheel(double dy, const event::ModifierKey &m)
        {
            mods = m;
            pipe.push_sample(input_sample{clock, cursor, m, raw::wheel{0.0, dy}});
        }
    };

    // ── 标准测试环境：root(0,0,800,600) + child(100,100,200,100) ──
    //    child 占据 x∈[100,300) y∈[100,200)，其余区域只有 root
    //    两个控件都绑同一套识别器，胜负只由 priority / depth 决定
    struct env
    {
        explicit env(const gesture_config &cfg = kGesture) : pipe{hw, root}
        {
            root.add(child);
            bind(root, cfg);
            bind(child, cfg);
        }

        static void bind(recorder &w, const gesture_config &cfg)
        {
            w.add_recognizer<tap_recognizer>(cfg);
            w.add_recognizer<long_press_recognizer>(cfg);
            w.add_recognizer<drag_recognizer>(cfg);
            w.add_recognizer<zoom_recognizer>();
            w.add_recognizer<shortcut_recognizer>();
        }

        recorder root{"root", rect{0, 0, 800, 600}};
        recorder child{"child", rect{100, 100, 200, 100}};
        hardware hw{};
        ui_input pipe;
        driver drv{pipe};
    };

    [[nodiscard]] inline std::string join(const std::vector<std::string> &v)
    {
        std::string out{};
        for (std::size_t i = 0; i < v.size(); ++i)
        {
            if (i != 0)
                out += " | ";
            out += v[i];
        }
        return out.empty() ? std::string{"(空)"} : out;
    }

    inline bool check(std::string_view name, const std::vector<std::string> &expected,
                      const std::vector<std::string> &actual)
    {
        const bool ok = expected == actual;
        std::println("[{}] {}", ok ? "PASS" : "FAIL", name);
        if (!ok)
        {
            std::println("        期望: {}", join(expected));
            std::println("        实际: {}", join(actual));
        }
        return ok;
    }

    inline bool check_true(std::string_view name, bool value)
    {
        std::println("[{}] {}", value ? "PASS" : "FAIL", name);
        return value;
    }

    using event::Action;
    using event::Key;
    using event::ModifierKey;
    using event::MouseButtons;

    constexpr auto LEFT = MouseButtons::eMOUSE_BUTTON_LEFT;
    constexpr auto CTRL = ModifierKey::eCONTROL;

    // ① 单击必须等窗口：直通层立刻到，语义层要等到“再没有更强候选”
    inline bool case_click_is_deferred()
    {
        env e;
        e.drv.down(LEFT, 150, 150);
        e.drv.at(milliseconds{80});
        e.drv.up(LEFT, 150, 150);

        bool ok = check("①单击：松手瞬间只有直通层，click 还没派发（多击窗口未过）",
                        {"raw:down:MOUSE_BUTTON_LEFT", "raw:up:MOUSE_BUTTON_LEFT"},
                        e.child.log);

        e.drv.at(milliseconds{500}); // 窗口到期
        ok &= check(
            "①单击：窗口到期后 click 胜出，交给命中的子控件（不是父控件）",
            {"raw:down:MOUSE_BUTTON_LEFT", "raw:up:MOUSE_BUTTON_LEFT", "tap:click:1"},
            e.child.log);
        ok &= check_true("①单击：父控件没有被误触发", e.root.log.empty());
        return ok;
    }

    // ② 双击包含单击，但只有双击胜出
    inline bool case_double_click_wins()
    {
        env e;
        e.drv.down(LEFT, 150, 150);
        e.drv.at(milliseconds{60});
        e.drv.up(LEFT, 150, 150);
        e.drv.at(milliseconds{140});
        e.drv.down(LEFT, 150, 150); // 窗口内第二击 → 升格
        e.drv.at(milliseconds{60});
        e.drv.up(LEFT, 150, 150);
        e.drv.at(milliseconds{500});

        return check("②双击：click 被撤回且不补发，只有 double_click 到达订阅层",
                     {"raw:down:MOUSE_BUTTON_LEFT", "raw:up:MOUSE_BUTTON_LEFT",
                      "raw:down:MOUSE_BUTTON_LEFT", "raw:up:MOUSE_BUTTON_LEFT",
                      "tap:double_click:2"},
                     e.child.log);
    }

    // ③ 到连击上限后 hold=0：当场胜出，不再等窗口
    inline bool case_triple_click_immediate()
    {
        env e;
        for (int i = 0; i < 3; ++i)
        {
            e.drv.down(LEFT, 150, 150);
            e.drv.at(milliseconds{60});
            e.drv.up(LEFT, 150, 150);
            if (i != 2)
                e.drv.at(milliseconds{140});
        }
        return check("③三击：到达上限后 hold=0，最后一次松手当场胜出",
                     {"raw:down:MOUSE_BUTTON_LEFT", "raw:up:MOUSE_BUTTON_LEFT",
                      "raw:down:MOUSE_BUTTON_LEFT", "raw:up:MOUSE_BUTTON_LEFT",
                      "raw:down:MOUSE_BUTTON_LEFT", "raw:up:MOUSE_BUTTON_LEFT",
                      "tap:triple_click:3"},
                     e.child.log);
    }

    // ④ 第二次按下位置漂移 → 不能升格 → 上一击立即定案，且发生在这一次按下之前
    inline bool case_drift_breaks_chain()
    {
        env e;
        e.drv.down(LEFT, 150, 150);
        e.drv.at(milliseconds{80});
        e.drv.up(LEFT, 150, 150);
        e.drv.at(milliseconds{100});
        e.drv.down(LEFT, 150, 180); // 漂移 30px > 6px

        bool ok =
            check("④链断：位置漂移让上一击立即定案，且派发在本次按下之前（因果顺序）",
                  {"raw:down:MOUSE_BUTTON_LEFT", "raw:up:MOUSE_BUTTON_LEFT",
                   "tap:click:1", "raw:down:MOUSE_BUTTON_LEFT"},
                  e.child.log);

        e.drv.at(milliseconds{60});
        e.drv.up(LEFT, 150, 180);
        e.drv.at(milliseconds{500});
        ok &= check("④链断后：第二击自成新链条，窗口到期再来一次 click",
                    {"raw:down:MOUSE_BUTTON_LEFT", "raw:up:MOUSE_BUTTON_LEFT",
                     "tap:click:1", "raw:down:MOUSE_BUTTON_LEFT",
                     "raw:up:MOUSE_BUTTON_LEFT", "tap:click:1"},
                    e.child.log);
        return ok;
    }

    // ⑤ 长按：按住到阈值即胜出，松手不再有 click
    inline bool case_long_press()
    {
        env e;
        e.drv.down(LEFT, 150, 150);
        e.drv.at(milliseconds{500});

        bool ok = check("⑤长按：按住到阈值就胜出，不必等松手",
                        {"raw:down:MOUSE_BUTTON_LEFT", "long_press"}, e.child.log);

        e.drv.at(milliseconds{150});
        e.drv.up(LEFT, 150, 150);
        e.drv.at(milliseconds{600});
        ok &= check(
            "⑤长按后松手：不会再冒出 click",
            {"raw:down:MOUSE_BUTTON_LEFT", "long_press", "raw:up:MOUSE_BUTTON_LEFT"},
            e.child.log);
        return ok;
    }

    // ⑥ 拖动是一条流：begin / update / end，且全程没有 click
    inline bool case_drag_stream()
    {
        env e;
        e.drv.down(LEFT, 150, 150);
        e.drv.at(milliseconds{30});
        e.drv.move(170, 150); // 位移 20px > drag_min_dist(8)
        e.drv.at(milliseconds{30});
        e.drv.move(190, 150);
        e.drv.at(milliseconds{30});
        e.drv.up(LEFT, 190, 150);

        return check(
            "⑥拖动：begin/update/end 一条流（raw:up 先于 drag_end），且没有 click",
            {"raw:down:MOUSE_BUTTON_LEFT", "drag_begin", "drag_update",
             "raw:up:MOUSE_BUTTON_LEFT", "drag_end"},
            e.child.log);
    }

    // ⑦ 指针捕获：划出控件后这条流仍归它，父控件不被卷进来
    inline bool case_pointer_capture()
    {
        env e;
        e.drv.down(LEFT, 150, 150);
        e.drv.at(milliseconds{30});
        e.drv.move(170, 150); // 起拖 → 捕获
        e.drv.at(milliseconds{30});
        e.drv.move(400, 150); // 已经划出 child 的 x 范围
        e.drv.at(milliseconds{30});
        e.drv.up(LEFT, 400, 150);

        bool ok = check("⑦捕获：指针划出控件后，拖动流仍然归它",
                        {"raw:down:MOUSE_BUTTON_LEFT", "drag_begin", "drag_update",
                         "raw:up:MOUSE_BUTTON_LEFT", "drag_end"},
                        e.child.log);
        ok &= check_true("⑦捕获：父控件全程没被卷进来", e.root.log.empty());
        return ok;
    }

    // ⑧ 同一帧内的固定顺序：直通层 raw 先到，语义层 shortcut 后到
    inline bool case_shortcut_and_raw_order()
    {
        env e;
        const ModifierKey ctrl{CTRL};
        e.drv.key_down(Key::eLEFT_CONTROL, ctrl);
        e.drv.key_down(Key::eA, ctrl);
        e.drv.key_up(Key::eA, ctrl);

        return check(
            "⑧快捷键：raw 与 shortcut 都在，且 raw 在前（顺序固定，不看注册顺序）",
            {"raw:key:LEFT_CONTROL", "raw:key:A", "shortcut:A", "raw:key:A"}, e.root.log);
    }

    // ⑨ 滚轮：原始事件永远直通；Ctrl+滚轮额外产生语义 zoom
    inline bool case_ctrl_wheel_zoom()
    {
        env e;
        e.drv.move(400, 400); // 进入 root 独享区域，建立悬停链
        const ModifierKey ctrl{CTRL};
        e.drv.wheel(1.0, ctrl);

        bool ok = check("⑨Ctrl+滚轮：原始 wheel 与语义 zoom 并存（两条信息都保留）",
                        {"raw:wheel", "zoom"}, e.root.log);

        e.root.clear();
        e.drv.wheel(1.0, ModifierKey::None());
        ok &= check("⑨普通滚轮：只有原始事件，没有 zoom", {"raw:wheel"}, e.root.log);
        return ok;
    }

    // ⑩ 运行时换裁决层：同一个动作，两套规则给出不同赢家
    class arena_child : public recorder
    {
      public:
        arena_child() : recorder{"child(自带裁决层)", rect{100, 100, 200, 100}} {}

        [[nodiscard]] gesture_arena *arena_override() noexcept override
        {
            return &arena_;
        }

      private:
        deepest_first_arena arena_{};
    };

    inline bool case_arena_swap_changes_winner()
    {
        recorder root{"root(特异性更高)", rect{0, 0, 800, 600}};
        arena_child child{};
        root.add(child);
        root.add_recognizer<drag_recognizer>(kGesture, 25);  // 父：特异性 25
        child.add_recognizer<drag_recognizer>(kGesture, 20); // 子：特异性 20 但更深
        hardware hw{};
        ui_input pipe{hw, root};
        driver drv{pipe};

        // A：悬停到子控件 → 换到它自带的裁决层 → 深度说话，子控件赢
        drv.down(LEFT, 150, 150);
        drv.at(milliseconds{30});
        drv.move(170, 150);
        drv.at(milliseconds{30});
        drv.up(LEFT, 170, 150);
        bool ok = check("⑩A 子控件自带裁决层（深度优先）：子控件赢",
                        {"raw:down:MOUSE_BUTTON_LEFT", "drag_begin",
                         "raw:up:MOUSE_BUTTON_LEFT", "drag_end"},
                        child.log);
        ok &= check("⑩A 父控件没有拿到手势", {}, root.log);

        // B：运行时强制默认裁决层 → 特异性说话，父控件赢（原始层仍按命中链走）
        pipe.set_force_default_arena(true);
        child.clear();
        root.clear();
        drv.at(milliseconds{50});
        drv.move(150, 150); // 空闲时才会真的换层
        drv.down(LEFT, 150, 150);
        drv.at(milliseconds{30});
        drv.move(170, 150);
        drv.at(milliseconds{30});
        drv.up(LEFT, 170, 150);
        ok &= check("⑩B 强制默认裁决层（特异性优先）：父控件赢",
                    {"drag_begin", "raw:up:MOUSE_BUTTON_LEFT", "drag_end"}, root.log);
        ok &= check("⑩B 原始层仍然按命中链走：raw:down 留在子控件",
                    {"raw:down:MOUSE_BUTTON_LEFT"}, child.log);

        // C：放开开关 → 回到子控件自带的裁决层
        pipe.set_force_default_arena(false);
        child.clear();
        root.clear();
        drv.at(milliseconds{50});
        drv.move(150, 150);
        drv.down(LEFT, 150, 150);
        drv.at(milliseconds{30});
        drv.move(170, 150);
        drv.at(milliseconds{30});
        drv.up(LEFT, 170, 150);
        ok &= check("⑩C 放开开关：胜负重新由深度决定，子控件又赢",
                    {"raw:down:MOUSE_BUTTON_LEFT", "drag_begin",
                     "raw:up:MOUSE_BUTTON_LEFT", "drag_end"},
                    child.log);
        return ok;
    }

    // ⑪ 竞技场里没有“更强的候选”时，弱候选不必等窗口
    inline bool case_single_member_arena_immediate()
    {
        gesture_config cfg = kGesture;
        cfg.max_click_count = 1; // 只有单击，没有更强的可能
        env e{cfg};
        e.drv.down(LEFT, 150, 150);
        e.drv.at(milliseconds{60});
        e.drv.up(LEFT, 150, 150);

        return check(
            "⑪单成员：单击松手立即胜出，不需要等多击窗口",
            {"raw:down:MOUSE_BUTTON_LEFT", "raw:up:MOUSE_BUTTON_LEFT", "tap:click:1"},
            e.child.log);
    }

    // ⑫ 已知代价：被升格链条取消的单击不补发
    inline bool case_upgrade_cost()
    {
        env e;
        e.drv.down(LEFT, 150, 150);
        e.drv.at(milliseconds{80});
        e.drv.up(LEFT, 150, 150); // 第一击：click 待定
        e.drv.at(milliseconds{120});
        e.drv.down(LEFT, 150, 150);  // 窗口内 → 升格：撤回第一击
        e.drv.at(milliseconds{500}); // 一直按住 → 长按到点，第二击改判
        e.drv.at(milliseconds{50});
        e.drv.up(LEFT, 150, 150);

        return check("⑫已知代价：第一击的 click 被撤回后不补发，第二击改判成长按",
                     {"raw:down:MOUSE_BUTTON_LEFT", "raw:up:MOUSE_BUTTON_LEFT",
                      "raw:down:MOUSE_BUTTON_LEFT", "long_press",
                      "raw:up:MOUSE_BUTTON_LEFT"},
                     e.child.log);
    }

    // ⑬ 冒泡：命中链深处不处理，就交给父控件
    inline bool case_bubbling()
    {
        env e;
        e.child.swallow = false; // 子控件“处理不了”
        e.drv.down(LEFT, 150, 150);
        e.drv.at(milliseconds{60});
        e.drv.up(LEFT, 150, 150);
        e.drv.at(milliseconds{500});

        bool ok = check(
            "⑬冒泡：子控件收到并声明不处理（自己也记录了）",
            {"raw:down:MOUSE_BUTTON_LEFT", "raw:up:MOUSE_BUTTON_LEFT", "tap:click:1"},
            e.child.log);
        ok &= check(
            "⑬冒泡：同一条继续到父控件，父控件消费掉",
            {"raw:down:MOUSE_BUTTON_LEFT", "raw:up:MOUSE_BUTTON_LEFT", "tap:click:1"},
            e.root.log);
        return ok;
    }

    inline bool run(bool trace = false)
    {
        const bool trace_backup = trace_input;
        trace_input = trace; // 默认关掉，让断言结果看得清；--trace 时打开看每一步裁决

        std::println("");
        std::println(
            "══ UI 层自测（无窗口 / 时间注入 / 确定性） ═══════════════════════");
        bool ok = true;
        ok &= case_click_is_deferred();
        ok &= case_double_click_wins();
        ok &= case_triple_click_immediate();
        ok &= case_drift_breaks_chain();
        ok &= case_long_press();
        ok &= case_drag_stream();
        ok &= case_pointer_capture();
        ok &= case_shortcut_and_raw_order();
        ok &= case_ctrl_wheel_zoom();
        ok &= case_arena_swap_changes_winner();
        ok &= case_single_member_arena_immediate();
        ok &= case_upgrade_cost();
        ok &= case_bubbling();
        std::println(
            "══════════════════════════════════════════════════════════════════");
        std::println("{}", ok ? "全部用例通过" : "有失败用例（上面标 FAIL 的就是）");

        trace_input = trace_backup;
        return ok;
    }

} // namespace mcs::vulkan::ui::selftest
