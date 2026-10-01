#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// 识别层 + 裁决层
//
//   识别层  把 input_sample 翻译成“候选”: gesture + 特异性 + 等待窗口
//   裁决层  抽象类 gesture_arena —— 可以被整体替换（运行时），因为“谁说了算”
//           本来就该随命中链变化：VSCode 里右键属于编辑器，Win10 桌面上属于桌面。
//
// 为什么裁决层是抽象类而不是模板参数：
//   1) 识别器要能被容器/控件树以非模板的形式持有，也要能在运行时换规则；
//   2) 裁决规则是策略：同一个物理动作，在不同命中链上由不同规则裁决才是对的。
//      抽象类 + 指针注入是最直接的表达，也方便以后接 hitTest 链条做“按区域裁决”。
//
// 候选只有四条路能到达订阅层：offer(提出) / retract(撤回) / accept(定案) /
// commit_due(时间到)。同一个 slot 同时只允许一个候选在场。
// ═══════════════════════════════════════════════════════════════════════════

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <print>
#include <string>
#include <utility>
#include <vector>

#include "ui_event.hpp"

namespace mcs::vulkan::ui
{
    // ═══ 手势参数（识别层的一组阈值；每个控件可以带自己的一份） ═══
    struct gesture_config
    {
        milliseconds click_max_time{250};       // 单击最长时长
        milliseconds multi_click_interval{500}; // 多击最大间隔
        milliseconds long_press_time{500};      // 长按阈值

        double click_max_move = 6.0;       // 单击最大位移
        double multi_click_max_dist = 6.0; // 多击位置漂移容忍
        double drag_min_dist = 8.0;        // 触发拖动的位移阈值

        int max_click_count = 3; // 1 = 单击没有“更强的可能”，松手立即胜出
    };
    inline constexpr gesture_config kGesture{};

    // 语义特异性：谁更“具体”。写成一张显式表，而不是散落在一堆 if 里。
    namespace priority
    {
        constexpr int kPerTap = 10; // 连击 = kPerTap * count → 单击10 双击20 三击30
        constexpr int kLongPress = 15;
        constexpr int kDrag = 20;
        constexpr int kShortcut = 15;
        constexpr int kZoom = 15;
    } // namespace priority

    // 两种真实世界的裁决策略，差别只有一处：弱候选要不要多等一个窗口
    enum class arbitration
    {
        eARENA,      // 竞技场：弱候选等窗口过期才胜出；被更强候选抢先则落败消失(Flutter/Android)
        eOPTIMISTIC, // 乐观：弱候选立即胜出，之后更强候选再补一个(Web DOM: click 之后还有 dblclick)
    };
    inline constexpr arbitration kArbitration = arbitration::eARENA;

    // 弱候选还要等多久？—— 这是两种策略唯一的差别点
    [[nodiscard]] constexpr milliseconds upgrade_hold(const gesture_config &cfg,
                                                      int count) noexcept
    {
        if (kArbitration == arbitration::eOPTIMISTIC)
            return milliseconds{0};
        return count < cfg.max_click_count ? cfg.multi_click_interval : milliseconds{0};
    }

    // ═══ 槽位：竞争资源。一个按钮一个槽、一个键一个槽、滚轮一个槽 ═══
    struct arena_slot
    {
        enum class area : std::uint8_t
        {
            ePOINTER,
            eKEY,
            eWHEEL,
        };

        area where{area::ePOINTER};
        std::uint16_t id{0}; // 按钮码 / 键码

        friend constexpr bool operator==(const arena_slot &,
                                         const arena_slot &) noexcept = default;
    };

    [[nodiscard]] constexpr arena_slot pointer_slot(event::MouseButtons button) noexcept
    {
        return {arena_slot::area::ePOINTER, static_cast<std::uint16_t>(button)};
    }
    [[nodiscard]] constexpr arena_slot key_slot(event::Key key) noexcept
    {
        return {arena_slot::area::eKEY, static_cast<std::uint16_t>(key)};
    }
    [[nodiscard]] constexpr arena_slot wheel_slot() noexcept
    {
        return {arena_slot::area::eWHEEL, 0};
    }

    [[nodiscard]] inline std::string slot_name(const arena_slot &s)
    {
        switch (s.where)
        {
        case arena_slot::area::ePOINTER:
            return std::format("pointer/{}", static_cast<event::MouseButtons>(s.id));
        case arena_slot::area::eKEY:
            return std::format("key/{}", static_cast<event::Key>(s.id));
        case arena_slot::area::eWHEEL:
            return std::format("wheel/{}", static_cast<int>(s.id));
        }
        return "?";
    }

    class gesture_recognizer;

    // ═══ 候选：一个“可能胜出”的手势 ═══
    struct candidate
    {
        gesture payload{};   // 胜出后交给订阅层的结构
        int priority{0};     // 语义特异性：越大越具体
        milliseconds hold{}; // 从提出这一刻起，还要等多久才无人能超越
        int depth{0};        // 命中链深度：越深越贴近用户点到的控件
        arena_slot slot{};   // 在哪个槽位竞争（捕获时用它定位是哪条指针流）

        bool captures_pointer{false};       // 胜出后独占这条指针流（拖动用）
        gesture_recognizer *owner{nullptr}; // 谁提出的（捕获后后续 move 归它）
        gesture_target *target{nullptr};    // 胜出后交给谁（虚函数）
    };

    // ═══ 裁决层的出口：胜出者从这里出去，路由器实现它 ═══
    class gesture_sink
    {
      public:
        virtual ~gesture_sink() = default;
        virtual void on_winner(const candidate &won) = 0;
    };

    // ═══ 裁决层接口：可以被替换的“谁说了算” ═══
    class gesture_arena
    {
      public:
        virtual ~gesture_arena() = default;

        [[nodiscard]] virtual const char *name() const noexcept = 0;
        virtual void bind_sink(gesture_sink *sink) noexcept = 0;

        virtual void offer(const arena_slot &slot, event_time now, candidate cand) = 0;
        virtual void retract(const arena_slot &slot) noexcept = 0;
        virtual void accept(const arena_slot &slot) = 0;
        virtual void commit_due(event_time now) = 0;

        [[nodiscard]] virtual bool pending(const arena_slot &slot) const noexcept = 0;
        [[nodiscard]] virtual bool idle() const noexcept = 0;
    };

    // ═══ 识别层接口 ═══
    // 两个入口，缺一不可；管线先跑完所有人的 on_close，再跑 on_open
    // （= Flutter 的 arena close & sweep：旧手势先定案，新候选再入场）。
    class gesture_recognizer
    {
      public:
        virtual ~gesture_recognizer() = default;

        // 收尾：撤回自己的候选 / 给已无可争议的候选定案 / 复位内部状态
        virtual void on_close(const input_sample &s, gesture_arena &arena) = 0;
        // 开启：提出本样本产生的新候选
        virtual void on_open(const input_sample &s, gesture_arena &arena) = 0;
        // 捕获这条指针流之后，后续样本（含松手那一帧）只喂给捕获者
        virtual void on_move(const input_sample &s, gesture_arena &arena)
        {
            (void)s;
            (void)arena;
        }

      protected:
        gesture_recognizer(gesture_target *target, int depth) noexcept
            : target_{target}, depth_{depth}
        {
        }

        [[nodiscard]] gesture_target *target() const noexcept
        {
            return target_;
        }
        [[nodiscard]] int depth() const noexcept
        {
            return depth_;
        }

      private:
        gesture_target *target_{nullptr};
        int depth_{0};
    };

    // ═══ 默认裁决层：特异性高者胜，弱候选要等 hold 窗口 ═══
    class priority_arena : public gesture_arena
    {
      public:
        explicit priority_arena(gesture_sink *sink = nullptr) noexcept : sink_{sink} {}

        [[nodiscard]] const char *name() const noexcept override
        {
            return "priority_arena";
        }
        void bind_sink(gesture_sink *sink) noexcept override
        {
            sink_ = sink;
        }

        void offer(const arena_slot &slot, event_time now, candidate cand) override
        {
            cell *existing = find(slot);
            if (existing != nullptr && !outranks(cand, existing->cand))
            {
                trace("reject ", slot, cand, "被在场候选挡住");
                return;
            }
            if (existing != nullptr)
                trace("preempt", slot, existing->cand, "落败，由更强的候选取代");

            trace("offer  ", slot, cand, "");
            const auto due = now + cand.hold;
            if (existing == nullptr)
                cells_.push_back(cell{slot, std::move(cand), due});
            else
            {
                existing->cand = std::move(cand);
                existing->due = due;
            }
        }

        void retract(const arena_slot &slot) noexcept override
        {
            const auto it = find_it(slot);
            if (it == cells_.end())
                return;
            trace("retract", slot, it->cand, "识别器自己撤回");
            cells_.erase(it);
        }

        void accept(const arena_slot &slot) override
        {
            const auto it = find_it(slot);
            if (it == cells_.end())
                return;
            declare(*it, "已不可能被超越，立即胜出");
        }

        void commit_due(event_time now) override
        {
            // 先把到期的搬出来再派发：派发会回调进裁决层（比如捕获流的下一棒），
            // 不能一边遍历 cells_ 一边被它改。
            std::vector<cell> due_cells{};
            for (auto it = cells_.begin(); it != cells_.end();)
            {
                if (it->due <= now)
                {
                    due_cells.push_back(std::move(*it));
                    it = cells_.erase(it);
                }
                else
                    ++it;
            }
            for (const auto &c : due_cells)
            {
                trace("win    ", c.slot, c.cand, "等待窗口到期");
                fire(c.cand);
            }
        }

        [[nodiscard]] bool pending(const arena_slot &slot) const noexcept override
        {
            return find(slot) != nullptr;
        }
        [[nodiscard]] bool idle() const noexcept override
        {
            return cells_.empty();
        }

      protected:
        struct cell
        {
            arena_slot slot{};
            candidate cand{};
            event_time due{};
        };

        // 谁有资格取代谁：默认比特异性。子类换掉这一条 = 换了一整套裁决规则。
        // 谁有资格取代谁：先比特异性；平手时比命中深度（越贴近用户点到的控件越优先）。
        // 这样父控件不会在平手时抢走子控件的点击 —— 想赢必须拿出更高的特异性，
        // 这正是 demo 里 desktop(25) vs editor(20) 的对照。
        [[nodiscard]] virtual bool outranks(const candidate &challenger,
                                           const candidate &holder) const noexcept
        {
            if (challenger.priority != holder.priority)
                return challenger.priority > holder.priority;
            return challenger.depth > holder.depth;
        }

        [[nodiscard]] cell *find(const arena_slot &slot) noexcept
        {
            const auto it = find_it(slot);
            return it == cells_.end() ? nullptr : &(*it);
        }
        [[nodiscard]] const cell *find(const arena_slot &slot) const noexcept
        {
            for (const auto &c : cells_)
                if (c.slot == slot)
                    return &c;
            return nullptr;
        }
        [[nodiscard]] std::vector<cell>::iterator find_it(const arena_slot &slot) noexcept
        {
            for (auto it = cells_.begin(); it != cells_.end(); ++it)
                if (it->slot == slot)
                    return it;
            return cells_.end();
        }

        void declare(cell &c, const char *how)
        {
            trace("accept ", c.slot, c.cand, how);
            candidate won = std::move(c.cand);
            const auto it = find_it(c.slot);
            if (it != cells_.end())
                cells_.erase(it);
            fire(won);
        }

        void fire(const candidate &won) const
        {
            if (sink_ != nullptr)
                sink_->on_winner(won);
        }

        void trace(const char *op, const arena_slot &slot, const candidate &c,
                   const char *note) const
        {
            if (!trace_input)
                return;
            std::println("[arena:{}] {:<7} {:<12} prio={:<2} hold={:>4}ms depth={} {}  {}", name(),
                         op, slot_name(slot), c.priority, c.hold.count(), c.depth,
                         describe(c.payload), note);
        }

      private:
        std::vector<cell> cells_{};
        gesture_sink *sink_{nullptr};
    };

    // ═══ 另一套裁决层：命中链越深越优先 ═══
    // 场景：同一个右键拖动，编辑器区域里是“列选择”，桌面背景上是“移动窗口”。
    // 桌面把候选的特异性定得更高，按 priority 裁决就会盖住编辑器（用户明明指着编辑器）；
    // 换成这套规则，用户指着谁就由谁裁决 —— 这就是“hitTest 链条决定裁决层”。
    class deepest_first_arena : public priority_arena
    {
      public:
        using priority_arena::priority_arena;

        [[nodiscard]] const char *name() const noexcept override
        {
            return "deepest_first_arena";
        }

      protected:
        [[nodiscard]] bool outranks(const candidate &challenger,
                                    const candidate &holder) const noexcept override
        {
            if (challenger.depth != holder.depth)
                return challenger.depth > holder.depth;
            return challenger.priority >= holder.priority;
        }
    };

    // ═══════════════════════════════════════════════════════════════════════
    // 内置识别器
    // ═══════════════════════════════════════════════════════════════════════

    inline double dist_sq(const event::position2d_event &a,
                          const event::position2d_event &b) noexcept
    {
        const double dx = a.xpos - b.xpos;
        const double dy = a.ypos - b.ypos;
        return dx * dx + dy * dy;
    }

    // 单击 / 双击 / 三击：一条“连击链”
    class tap_recognizer : public gesture_recognizer
    {
      public:
        tap_recognizer(gesture_target *target, int depth, const gesture_config &cfg) noexcept
            : gesture_recognizer{target, depth}, cfg_{cfg}
        {
        }

        void on_close(const input_sample &s, gesture_arena &arena) override
        {
            if (const auto *down = as<raw::pointer_down>(s.payload))
                seal(arena, *down, s);
            else if (const auto *up = as<raw::pointer_up>(s.payload))
                close_release(*up, s);
        }

        void on_open(const input_sample &s, gesture_arena &arena) override
        {
            if (const auto *up = as<raw::pointer_up>(s.payload))
                offer_click(arena, *up, s);
        }

      private:
        struct track
        {
            bool down{false};
            bool release_ok{false};
            int chain{0};
            event::position2d_event down_pos{};
            event_time down_time{};
            event::position2d_event last_up_pos{};
            event_time last_up_time{};
            milliseconds held{};
        };

        static std::size_t index_of(event::MouseButtons b) noexcept
        {
            return static_cast<std::size_t>(b);
        }

        // 新的一次按下 = 上一段链条必须当场有结论：升格 或 定案
        void seal(gesture_arena &arena, const raw::pointer_down &down, const input_sample &s)
        {
            auto &t = tracks_[index_of(down.button)];
            if (t.down)
                return; // 重复按下（例如在窗口外松开又进来）：忽略，等真正的松开

            t.down = true;
            t.down_pos = s.pos;
            t.down_time = s.time;

            const arena_slot slot = pointer_slot(down.button);
            if (t.chain == 0)
            {
                t.chain = 1; // 全新链条：等松手再决定它是不是单击
                return;
            }

            // 这一击能不能把上一击升格成更多击？
            //   eARENA      : 弱候选必须还在场（还在等窗口）—— 已宣布胜出的单击不再参与升格
            //   eOPTIMISTIC : 弱候选早已派发，只按窗口/漂移/上限判断
            const bool alive =
                kArbitration == arbitration::eOPTIMISTIC || arena.pending(slot);
            const bool chainable =
                alive && t.chain < cfg_.max_click_count &&
                (s.time - t.last_up_time) <= cfg_.multi_click_interval &&
                dist_sq(s.pos, t.last_up_pos) <= cfg_.multi_click_max_dist * cfg_.multi_click_max_dist;

            if (chainable)
            {
                // 更强的候选（N+1 击）要来了 → 当场撤回较弱的那一个（已派发过就无所谓）。
                // 代价：被撤回的单击不会补发（Flutter 同样如此）；要 web 语义请切 eOPTIMISTIC。
                arena.retract(slot);
                t.chain += 1;
            }
            else
            {
                // 漂移/超时/已达上限 → 上一击再也不可能被超越，立即定案胜出
                arena.accept(slot);
                t.chain = 1;
            }
        }

        void close_release(const raw::pointer_up &up, const input_sample &s)
        {
            auto &t = tracks_[index_of(up.button)];
            if (!t.down)
            {
                t.release_ok = false;
                return;
            }
            t.down = false;
            t.held = std::chrono::duration_cast<milliseconds>(s.time - t.down_time);
            const bool in_place =
                dist_sq(s.pos, t.down_pos) <= cfg_.click_max_move * cfg_.click_max_move;
            t.release_ok = t.held <= cfg_.click_max_time && in_place;
            if (!t.release_ok)
                t.chain = 0; // 不是点击：链条结束
        }

        void offer_click(gesture_arena &arena, const raw::pointer_up &up, const input_sample &s)
        {
            auto &t = tracks_[index_of(up.button)];
            if (!t.release_ok)
                return;
            t.release_ok = false;

            const int count = t.chain;
            t.last_up_pos = s.pos;
            t.last_up_time = s.time;

            candidate cand{};
            cand.payload = tap_gesture{ctx_of(s), up.button, count, t.held};
            cand.priority = count * priority::kPerTap;
            cand.hold = upgrade_hold(cfg_, count);
            cand.depth = depth();
            cand.slot = pointer_slot(up.button);
            cand.target = target();
            arena.offer(cand.slot, s.time, std::move(cand));
        }

        std::array<track, static_cast<std::size_t>(event::MouseButtons::eSIZE)> tracks_{};
        gesture_config cfg_{};
    };

    // 长按：按住不动到达阈值。与单击同槽竞争，到点即胜出。
    class long_press_recognizer : public gesture_recognizer
    {
      public:
        long_press_recognizer(gesture_target *target, int depth,
                              const gesture_config &cfg) noexcept
            : gesture_recognizer{target, depth}, cfg_{cfg}
        {
        }

        void on_close(const input_sample &s, gesture_arena &arena) override
        {
            // 收尾必须在“开启”之前跑：松手说明长按不成立，先撤回自己的候选，
            // 后面 tap 才有空槽提出 click（长按特异性 15 高于单击 10，不先撤就会被否掉）。
            if (const auto *up = as<raw::pointer_up>(s.payload))
                arena.retract(pointer_slot(up->button));
        }

        void on_open(const input_sample &s, gesture_arena &arena) override
        {
            const auto *down = as<raw::pointer_down>(s.payload);
            if (down == nullptr)
                return;

            candidate cand{};
            cand.payload = long_press_gesture{ctx_of(s), down->button, cfg_.long_press_time};
            cand.priority = priority::kLongPress;
            cand.hold = cfg_.long_press_time;
            cand.depth = depth();
            cand.slot = pointer_slot(down->button);
            cand.target = target();
            arena.offer(cand.slot, s.time, std::move(cand));
        }

      private:
        gesture_config cfg_{};
    };

    // 拖动：按下臂好 → 移动越过阈值 → drag_begin（要求捕获指针）
    //       捕获后：move → drag_update，松手 → drag_end
    // 为什么必须“捕获”：拖动是一串事件而不是一个点事件。胜出之后必须独占这条指针流，
    // 否则指针划过的其它控件会插进来竞争（原生 UI 里叫指针捕获 / pointer capture）。
    class drag_recognizer : public gesture_recognizer
    {
      public:
        drag_recognizer(gesture_target *target, int depth, const gesture_config &cfg,
                        int prio = priority::kDrag) noexcept
            : gesture_recognizer{target, depth}, cfg_{cfg}, priority_{prio}
        {
        }

        void on_close(const input_sample &s, gesture_arena &arena) override
        {
            (void)arena;
            // 没胜出就在松手时解除武装（候选由 tap/long_press 各自的规则收尾）
            if (const auto *up = as<raw::pointer_up>(s.payload))
                tracks_[index_of(up->button)] = track{};
        }

        void on_open(const input_sample &s, gesture_arena &arena) override
        {
            if (const auto *down = as<raw::pointer_down>(s.payload))
            {
                auto &st = tracks_[index_of(down->button)];
                st = track{};
                st.armed = true;
                st.button = down->button;
                st.start = s.pos;
                st.start_time = s.time;
                return;
            }

            const auto *move = as<raw::pointer_move>(s.payload);
            if (move == nullptr)
                return;

            // 还没被捕获时：越过阈值就提出 drag_begin
            for (auto &st : tracks_)
            {
                if (!st.armed || st.offered)
                    continue;
                if (dist_sq(s.pos, st.start) < cfg_.drag_min_dist * cfg_.drag_min_dist)
                    continue;
                st.offered = true;
                offer_stream(arena, s, st, drag_phase::eBEGIN, true);
            }
        }

        void on_move(const input_sample &s, gesture_arena &arena) override
        {
            if (as<raw::pointer_move>(s.payload) != nullptr)
            {
                for (auto &st : tracks_)
                    if (st.offered)
                        offer_stream(arena, s, st, drag_phase::eUPDATE, true);
                return;
            }

            const auto *up = as<raw::pointer_up>(s.payload);
            if (up == nullptr)
                return;

            for (auto &st : tracks_)
            {
                if (!st.offered || st.button != up->button)
                    continue;
                offer_stream(arena, s, st, drag_phase::eEND, false);
                st = track{};
            }
        }

      private:
        struct track
        {
            bool armed{false};
            bool offered{false};
            event::MouseButtons button{event::MouseButtons::eUNDEFINED};
            event::position2d_event start{};
            event_time start_time{};
        };

        static std::size_t index_of(event::MouseButtons b) noexcept
        {
            return static_cast<std::size_t>(b);
        }

        // 捕获流里的每一棒都走同一个出口（commit_due 立即判胜），
        // 所以“捕获后的拖动”不需要另开一条派发通道。
        void offer_stream(gesture_arena &arena, const input_sample &s, const track &st,
                          drag_phase phase, bool keep_capture)
        {
            candidate cand{};
            cand.payload = drag_gesture{ctx_of(s),
                                        st.button,
                                        st.start,
                                        s.pos.xpos - st.start.xpos,
                                        s.pos.ypos - st.start.ypos,
                                        std::chrono::duration_cast<milliseconds>(s.time -
                                                                                 st.start_time),
                                        phase};
            cand.priority = priority_;
            cand.hold = milliseconds{0};
            cand.depth = depth();
            cand.slot = pointer_slot(st.button);
            cand.captures_pointer = keep_capture;
            cand.owner = this;
            cand.target = target();
            arena.offer(cand.slot, s.time, std::move(cand));
        }

        std::array<track, static_cast<std::size_t>(event::MouseButtons::eSIZE)> tracks_{};
        gesture_config cfg_{};
        int priority_{priority::kDrag};
    };

    // 语义快捷键：Ctrl/Alt/Shift/Super + 非修饰键
    class shortcut_recognizer : public gesture_recognizer
    {
      public:
        shortcut_recognizer(gesture_target *target, int depth) noexcept
            : gesture_recognizer{target, depth}
        {
        }

        void on_close(const input_sample &, gesture_arena &) override
        {
        }

        void on_open(const input_sample &s, gesture_arena &arena) override
        {
            const auto *down = as<raw::key_down>(s.payload);
            if (down == nullptr || s.mods.empty() || is_modifier_key(down->key))
                return;

            candidate cand{};
            cand.payload = shortcut_gesture{ctx_of(s), down->key};
            cand.priority = priority::kShortcut;
            cand.hold = milliseconds{0};
            cand.depth = depth();
            cand.slot = key_slot(down->key);
            cand.target = target();
            arena.offer(cand.slot, s.time, std::move(cand));
        }

        [[nodiscard]] static bool is_modifier_key(event::Key k) noexcept
        {
            switch (k)
            {
            case event::Key::eLEFT_SHIFT:
            case event::Key::eRIGHT_SHIFT:
            case event::Key::eLEFT_CONTROL:
            case event::Key::eRIGHT_CONTROL:
            case event::Key::eLEFT_ALT:
            case event::Key::eRIGHT_ALT:
            case event::Key::eLEFT_SUPER:
            case event::Key::eRIGHT_SUPER:
                return true;
            default:
                return false;
            }
        }
    };

    // Ctrl + 滚轮 → 语义缩放（原始 wheel 事件照旧直通派发，两条信息都保留）
    class zoom_recognizer : public gesture_recognizer
    {
      public:
        zoom_recognizer(gesture_target *target, int depth) noexcept
            : gesture_recognizer{target, depth}
        {
        }

        void on_close(const input_sample &, gesture_arena &) override
        {
        }

        void on_open(const input_sample &s, gesture_arena &arena) override
        {
            const auto *wheel = as<raw::wheel>(s.payload);
            if (wheel == nullptr || !s.mods.has(event::ModifierKey::eCONTROL))
                return;

            candidate cand{};
            cand.payload = zoom_gesture{ctx_of(s), wheel->dy};
            cand.priority = priority::kZoom;
            cand.hold = milliseconds{0};
            cand.depth = depth();
            cand.slot = wheel_slot();
            cand.target = target();
            arena.offer(cand.slot, s.time, std::move(cand));
        }
    };

} // namespace mcs::vulkan::ui
