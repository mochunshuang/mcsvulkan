#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// 来源层 + 命中路由 + 控件树
//
//   硬件(glfw_input) ──► input_sample ──┐
//                                       ├─► 命中链(hitTest) ──► 识别器 ──► 裁决层 ──► 订阅层
//   控件树(widget) ─────────────────────┘
//
// 一次样本的固定顺序（顺序定死，才不依赖注册顺序、不产生竞争类 bug）：
//   ① 时间先推进：上一批等待窗口到期的候选先胜出、先派发
//   ② 收尾：命中链上所有人 on_close（撤回/定案上一段手势）
//   ③ 开启：命中链上所有人 on_open（提出本样本的新候选）
//   ④ 直通：本样本的原始事件（无歧义，不参与竞争）
//   ⑤ 结算本批：hold=0 的候选（快捷键/缩放/拖动/已无升格可能的单击）此刻胜出
//
// 命中链在“按下”那一刻固定下来（原生 UI 的指针捕获）：指针之后划出控件，事件也还归它，
// 直到松手。悬停（没有按键按下时）用当前位置的链，并据此选择该区域的裁决层。
// ═══════════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <format>
#include <memory>
#include <print>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// 依赖：使用前调用方必须先 include mcsvulkan 的 event / input 头（本工程里是 ../head.hpp），
//       因为下面用到了 event::* 与 input::glfw_forward。
//       把这三个头提升到 include/detail/input/ 时，把这一行换成 #include "glfw_input.hpp" 即可。
#include "ui_arena.hpp"
#include "../head.hpp"

namespace mcs::vulkan::ui
{
    struct rect
    {
        double x{};
        double y{};
        double w{};
        double h{};
    };

    // ═══ 控件：命中测试的来源，也是订阅层（虚函数）的宿主 ═══
    class widget : public gesture_target
    {
      public:
        widget(std::string name, rect area) : name_{std::move(name)}, area_{area} {}

        widget(const widget &) = delete;
        widget(widget &&) = delete;
        widget &operator=(const widget &) = delete;
        widget &operator=(widget &&) = delete;

        [[nodiscard]] const std::string &name() const noexcept
        {
            return name_;
        }
        [[nodiscard]] const rect &area() const noexcept
        {
            return area_;
        }
        [[nodiscard]] int depth() const noexcept
        {
            return depth_;
        }
        [[nodiscard]] widget *parent() const noexcept
        {
            return parent_;
        }
        [[nodiscard]] const std::vector<widget *> &children() const noexcept
        {
            return children_;
        }

        void add(widget &child) noexcept
        {
            child.parent_ = this;
            child.depth_ = depth_ + 1;
            children_.push_back(&child);
        }

        [[nodiscard]] bool hit(const event::position2d_event &p) const noexcept
        {
            return p.xpos >= area_.x && p.xpos < area_.x + area_.w && p.ypos >= area_.y &&
                   p.ypos < area_.y + area_.h;
        }

        // 命中链：最内层 → 根。后加的子控件先测（等于“画在上面”的先命中）。
        void hit_chain(const event::position2d_event &p, std::vector<widget *> &out)
        {
            if (!hit(p))
                return;
            for (auto it = children_.rbegin(); it != children_.rend(); ++it)
            {
                (*it)->hit_chain(p, out);
                if (!out.empty())
                {
                    out.push_back(this);
                    return;
                }
            }
            out.push_back(this);
        }

        // 事件在这里冒泡：自己处理不了就交给父控件
        [[nodiscard]] bool dispatch(const gesture &g)
        {
            if (deliver_to(*this, g))
                return true;
            return parent_ != nullptr ? parent_->dispatch(g) : false;
        }

        // 本控件注册的识别器（子类构造里 add_recognizer<T>(...)）
        [[nodiscard]] std::vector<gesture_recognizer *> &recognizers() noexcept
        {
            return recognizers_;
        }
        [[nodiscard]] const std::vector<gesture_recognizer *> &recognizers()
            const noexcept
        {
            return recognizers_;
        }

        // 绑定识别器：通常在控件构造函数里调用（见 demo）。
        // 也可以从外部装配（测试、脚本化 UI）：生命周期由控件持有，调用方只管创建。
        template <class R, class... Args>
        R &add_recognizer(Args &&...args)
        {
            auto owned = std::make_unique<R>(this, depth_, std::forward<Args>(args)...);
            R &ref = *owned;
            recognizers_.push_back(&ref);
            owned_.push_back(std::move(owned));
            return ref;
        }

        // 可选的裁决层替换：不同区域用不同裁决规则（nullptr = 用管线当前的裁决层）
        // 非 const：它交出去的是可以继续被写入的裁决层对象
        [[nodiscard]] virtual gesture_arena *arena_override() noexcept
        {
            return nullptr;
        }

        // hover 状态：演示“对象状态真的发生改变”
        [[nodiscard]] bool hovered() const noexcept
        {
            return hovered_;
        }
        virtual void on_hover_enter()
        {
            hovered_ = true;
        }
        virtual void on_hover_leave()
        {
            hovered_ = false;
        }

        [[nodiscard]] widget *as_widget() noexcept override
        {
            return this;
        }

      protected:
        void set_area(rect r) noexcept
        {
            area_ = r;
        }

      private:
        std::string name_{};
        rect area_{};
        widget *parent_{nullptr};
        int depth_{0};
        bool hovered_{false};
        std::vector<widget *> children_{};
        std::vector<std::unique_ptr<gesture_recognizer>> owned_{};
        std::vector<gesture_recognizer *> recognizers_{};
    };

    // ═══ 管线：硬件 → 命中路由 → 识别 → 裁决 → 订阅 ═══
    class ui_input : public gesture_sink
    {
      public:
        ui_input(input::glfw_forward &hardware, widget &root)
            : hardware_{hardware}, root_{root}, default_arena_{this}
        {
            arena_ = &default_arena_;
            focus_ = &root_;
            rebuild_focus_chain();

            hardware_.subscribe<input::glfw_forward::keyboard_change_fn>(
                this, &ui_input::on_hardware_keyboard);
            hardware_.subscribe<input::glfw_forward::mousebutton_change_fn>(
                this, &ui_input::on_hardware_mousebutton);
            hardware_.subscribe<input::glfw_forward::scroll_event_change_fn>(
                this, &ui_input::on_hardware_scroll);
        }

        ~ui_input() noexcept
        {
            hardware_.unsubscribe<input::glfw_forward::keyboard_change_fn>(
                this, &ui_input::on_hardware_keyboard);
            hardware_.unsubscribe<input::glfw_forward::mousebutton_change_fn>(
                this, &ui_input::on_hardware_mousebutton);
            hardware_.unsubscribe<input::glfw_forward::scroll_event_change_fn>(
                this, &ui_input::on_hardware_scroll);
        }

        ui_input(const ui_input &) = delete;
        ui_input(ui_input &&) = delete;
        ui_input &operator=(const ui_input &) = delete;
        ui_input &operator=(ui_input &&) = delete;

        // 每帧调用：没有新输入时也要裁决（长按到点 / 单击等待窗口过期）
        void update()
        {
            update(event_clock::now());
        }

        // 指定时间推进：自测与确定性重放用（配合 push_sample 注入样本）
        void update(event_time now)
        {
            apply_pending_arena();
            arena_->commit_due(now);
        }

        // 运行时替换裁决层（只在空闲时真正切换，避免把等待中的候选丢掉）
        void request_arena(gesture_arena *want) noexcept
        {
            pending_arena_ = want;
        }

        // 运行时开关：忽略控件自带的裁决层，所有区域都走默认层。
        // 用途就是对比：同一个右键拖动，在不同裁决规则下会交给不同的虚函数。
        void set_force_default_arena(bool on) noexcept
        {
            force_default_arena_ = on;
        }
        [[nodiscard]] bool force_default_arena() const noexcept
        {
            return force_default_arena_;
        }

        // 光标位置的轮询来源：每帧调用一次，位置变了就当作一个 pointer_move 样本。
        // （回调式来源是 glfw_input 的变更通道；两种来源可以并存，识别层不关心差别）
        void push_cursor(const event::position2d_event &p)
        {
            const bool moved =
                !has_cursor_ || p.xpos != cursor_.xpos || p.ypos != cursor_.ypos;
            if (!moved)
                return;
            const double dx = has_cursor_ ? p.xpos - cursor_.xpos : 0.0;
            const double dy = has_cursor_ ? p.ypos - cursor_.ypos : 0.0;
            has_cursor_ = true;
            cursor_ = p;
            push(input_sample{event_clock::now(), p, mods_.value(),
                              raw::pointer_move{dx, dy}});
        }

        // 样本入口：来源层（回调式 / 轮询式）与自测都从这里进
        void push_sample(const input_sample &s)
        {
            push(s);
        }

        [[nodiscard]] gesture_arena *arena() const noexcept
        {
            return arena_;
        }
        [[nodiscard]] widget *focus() const noexcept
        {
            return focus_;
        }
        [[nodiscard]] int arena_swaps() const noexcept
        {
            return swaps_;
        }
        [[nodiscard]] int unhandled() const noexcept
        {
            return unhandled_;
        }

        void set_focus(widget *w)
        {
            if (w == nullptr || w == focus_)
                return;
            focus_ = w;
            rebuild_focus_chain();
        }

        // ── 裁决层的出口：胜出者由这里交付 ──
        void on_winner(const candidate &won) override
        {
            widget *node = won.target != nullptr ? won.target->as_widget() : nullptr;

            if (won.captures_pointer && won.owner != nullptr &&
                won.slot.where == arena_slot::area::ePOINTER)
            {
                auto &track = pointers_[static_cast<std::size_t>(won.slot.id)];
                track.owner = won.owner;
                track.capture_node = node;
                if (trace_input)
                    std::println("[route] 指针捕获 → {}（{}）", slot_name(won.slot),
                                 node != nullptr ? node->name() : "?");
            }

            bool handled = false;
            if (node != nullptr)
                handled = node->dispatch(won.payload);
            else if (won.target != nullptr)
                handled = deliver_to(*won.target, won.payload);
            if (!handled)
                ++unhandled_;

            if (trace_input)
                std::println("[win  ] {}{}", describe(won.payload),
                             handled
                                 ? std::format("   → {}",
                                               node != nullptr ? node->name() : "target")
                                 : "   → 没人处理");

            // 点/拖之后把键盘焦点给到控件（键盘事件的去处）
            if (node != nullptr && (std::holds_alternative<tap_gesture>(won.payload) ||
                                    std::holds_alternative<drag_gesture>(won.payload)))
                set_focus(node);
        }

      private:
        // ── 来源层：四条硬件通道 → 统一样本 ──
        static void on_hardware_keyboard(void *self, event::position2d_event pos,
                                         event_time t, event::keyboard_event e) noexcept
        {
            auto &p = *static_cast<ui_input *>(self);
            p.mods_.on_key(e); // 先更新修饰键状态，再解释这个按键
            p.push(input_sample{t, pos, p.mods_.value(), to_raw(e)});
        }

        static void on_hardware_mousebutton(void *self, event::position2d_event pos,
                                            event_time t,
                                            event::mousebutton_event e) noexcept
        {
            auto &p = *static_cast<ui_input *>(self);
            p.push(input_sample{t, pos, p.mods_.value(), to_raw(e)});
        }

        static void on_hardware_scroll(void *self, event::position2d_event pos,
                                       event_time t, event::scroll_event e) noexcept
        {
            auto &p = *static_cast<ui_input *>(self);
            p.push(
                input_sample{t, pos, p.mods_.value(), raw::wheel{e.xoffset, e.yoffset}});
        }

        [[nodiscard]] static raw::payload to_raw(const event::keyboard_event &e) noexcept
        {
            if (e.press())
                return raw::key_down{e.key, e.scancode};
            if (e.repeat())
                return raw::key_repeat{e.key, e.scancode};
            return raw::key_up{e.key, e.scancode};
        }

        [[nodiscard]] static raw::payload to_raw(
            const event::mousebutton_event &e) noexcept
        {
            return e.press() ? raw::payload{raw::pointer_down{e.button}}
                             : raw::payload{raw::pointer_up{e.button}};
        }

        // ── 修饰键状态：以“状态”为准，而不是每个事件里的快照字段
        //    （滚轮和移动事件根本不带修饰键，而 Ctrl+滚轮必须知道 Ctrl 是否按下） ──
        class modifier_state
        {
          public:
            void on_key(const event::keyboard_event &e) noexcept
            {
                const auto flag = flag_of(e.key);
                if (e.key == event::Key::eCAPS_LOCK || e.key == event::Key::eNUM_LOCK)
                {
                    if (e.action == event::Action::ePRESS)
                        toggle(flag);
                    return;
                }
                if (e.action == event::Action::ePRESS)
                    value_ |= flag;
                else if (e.action == event::Action::eRELEASE)
                    value_.remove(flag);
            }

            [[nodiscard]] event::ModifierKey value() const noexcept
            {
                return value_;
            }

          private:
            static event::ModifierKey::Value flag_of(event::Key k) noexcept
            {
                switch (k)
                {
                case event::Key::eLEFT_SHIFT:
                case event::Key::eRIGHT_SHIFT:
                    return event::ModifierKey::eSHIFT;
                case event::Key::eLEFT_CONTROL:
                case event::Key::eRIGHT_CONTROL:
                    return event::ModifierKey::eCONTROL;
                case event::Key::eLEFT_ALT:
                case event::Key::eRIGHT_ALT:
                    return event::ModifierKey::eALT;
                case event::Key::eLEFT_SUPER:
                case event::Key::eRIGHT_SUPER:
                    return event::ModifierKey::eSUPER;
                case event::Key::eCAPS_LOCK:
                    return event::ModifierKey::eCAPS_LOCK;
                case event::Key::eNUM_LOCK:
                    return event::ModifierKey::eNUM_LOCK;
                default:
                    return event::ModifierKey::eNONE;
                }
            }

            void toggle(event::ModifierKey::Value flag) noexcept
            {
                if (value_.has(flag))
                    value_.remove(flag);
                else
                    value_ |= flag;
            }

            event::ModifierKey value_{event::ModifierKey::None()};
        };

        // 一条指针流的记录：按下时固定命中链，捕获后由捕获者独占
        struct pointer_track
        {
            std::vector<widget *> nodes{};
            std::vector<gesture_recognizer *> chain{};
            gesture_recognizer *owner{nullptr};
            widget *capture_node{nullptr};
        };

        void push(const input_sample &s)
        {
            arena_->commit_due(s.time); // ① 时间先推进
            route(s);                   // ②③④ 命中 / 收尾 / 开启 / 直通
            arena_->commit_due(s.time); // ⑤ 结算本批
        }

        void route(const input_sample &s)
        {
            if (as<raw::pointer_down>(s.payload) != nullptr)
            {
                on_pointer_down(s);
                return;
            }
            if (as<raw::pointer_up>(s.payload) != nullptr)
            {
                on_pointer_up(s);
                return;
            }
            if (as<raw::pointer_move>(s.payload) != nullptr)
            {
                on_pointer_move(s);
                return;
            }
            on_keyboard_or_wheel(s);
        }

        void on_pointer_down(const input_sample &s)
        {
            const auto *down = as<raw::pointer_down>(s.payload);
            auto &track = pointers_[static_cast<std::size_t>(down->button)];

            // 按下这一刻把命中链固定下来（指针捕获的常规做法）
            track.nodes.clear();
            root_.hit_chain(s.pos, track.nodes);
            track.chain.clear();
            for (auto *node : track.nodes)
                for (auto *r : node->recognizers())
                    track.chain.push_back(r);
            track.owner = nullptr;
            track.capture_node = nullptr;

            update_hover(s.pos); // hover 状态与按下同步（裁决层切换只在空闲时发生）
            run_close_open(track.chain, s);
            deliver_raw(s, deepest(track.nodes));
        }

        void on_pointer_up(const input_sample &s)
        {
            const auto *up = as<raw::pointer_up>(s.payload);
            auto &track = pointers_[static_cast<std::size_t>(up->button)];

            if (track.owner != nullptr)
            {
                // 先收尾、后开启：其它识别器的 retract 不能把捕获者的 drag_end 抹掉
                for (auto *r : track.chain)
                    if (r != track.owner)
                        r->on_close(s, *arena_);
                track.owner->on_move(s, *arena_); // 捕获者收尾：drag_end
            }
            else
            {
                run_close_open(track.chain, s);
            }

            deliver_raw(s, track.capture_node != nullptr ? track.capture_node
                                                         : deepest(track.nodes));

            track.owner = nullptr;
            track.capture_node = nullptr;
            track.chain.clear();
            track.nodes.clear();
            update_hover(s.pos);
        }

        void on_pointer_move(const input_sample &s)
        {
            if (auto *track = active_track(); track != nullptr)
            {
                if (track->owner != nullptr)
                {
                    // 捕获流：先让其它识别器收尾（它们的 retract 不能抹掉捕获者刚提出的东西），
                    // 再让捕获者处理这一棒 —— 仍然是“先收尾、后开启”的同一条规矩。
                    for (auto *r : track->chain)
                        if (r != track->owner)
                            r->on_close(s, *arena_);
                    track->owner->on_move(s, *arena_);
                }
                else
                {
                    run_close_open(track->chain, s);
                }
                deliver_raw(s, track->capture_node != nullptr ? track->capture_node
                                                              : deepest(track->nodes));
                return;
            }

            // 没有按键：悬停链上的识别器也能看到移动（拖动起手就靠它）
            update_hover(s.pos);
            run_close_open(hover_nodes_, s);
            deliver_raw(s, deepest(hover_nodes_));
        }

        void on_keyboard_or_wheel(const input_sample &s)
        {
            const bool is_key = as<raw::key_down>(s.payload) != nullptr ||
                                as<raw::key_repeat>(s.payload) != nullptr ||
                                as<raw::key_up>(s.payload) != nullptr;
            if (is_key)
            {
                update_arena_for(focus_nodes_);
                run_close_open(focus_nodes_, s);
                deliver_raw(s, deepest(focus_nodes_));
            }
            else
            {
                update_arena_for(hover_nodes_);
                run_close_open(hover_nodes_, s);
                deliver_raw(s, deepest(hover_nodes_));
            }
        }

        // 按下时固定的识别器链（指针捕获语义：这一串手势只属于按下那一刻命中的那些人）
        void run_close_open(const std::vector<gesture_recognizer *> &chain,
                            const input_sample &s)
        {
            for (auto *r : chain)
                r->on_close(s, *arena_);
            for (auto *r : chain)
                r->on_open(s, *arena_);
        }

        // 悬停链 / 焦点链：每次都从控件现取识别器，不缓存
        // （控件可能在管线构造之后才绑定识别器；缓存会变成过期快照）
        void run_close_open(const std::vector<widget *> &nodes, const input_sample &s)
        {
            for (auto *w : nodes)
                for (auto *r : w->recognizers())
                    r->on_close(s, *arena_);
            for (auto *w : nodes)
                for (auto *r : w->recognizers())
                    r->on_open(s, *arena_);
        }

        // 直通层：原始事件（无歧义）给到最内层，处理不了就冒泡
        void deliver_raw(const input_sample &s, widget *node)
        {
            gesture g{};
            if (const auto *down = as<raw::pointer_down>(s.payload))
                g = pointer_event{ctx_of(s), down->button, event::Action::ePRESS};
            else if (const auto *up = as<raw::pointer_up>(s.payload))
                g = pointer_event{ctx_of(s), up->button, event::Action::eRELEASE};
            else if (const auto *move = as<raw::pointer_move>(s.payload))
                g = pointer_move_event{ctx_of(s), move->dx, move->dy};
            else if (const auto *wheel = as<raw::wheel>(s.payload))
                g = wheel_event{ctx_of(s), wheel->dx, wheel->dy};
            else if (const auto *kd = as<raw::key_down>(s.payload))
                g = key_event{ctx_of(s), kd->key, event::Action::ePRESS, kd->scancode};
            else if (const auto *kr = as<raw::key_repeat>(s.payload))
                g = key_event{ctx_of(s), kr->key, event::Action::eREPEAT, kr->scancode};
            else if (const auto *ku = as<raw::key_up>(s.payload))
                g = key_event{ctx_of(s), ku->key, event::Action::eRELEASE, ku->scancode};
            else
                return;

            if (node == nullptr)
                return;
            if (!node->dispatch(g))
                ++unhandled_;
        }

        // ── 命中 / 悬停 / 裁决层选择 ──
        void update_hover(const event::position2d_event &p)
        {
            hover_scratch_.clear();
            root_.hit_chain(p, hover_scratch_);
            update_arena_for(hover_scratch_);

            for (auto *w : hover_nodes_)
                if (std::find(hover_scratch_.begin(), hover_scratch_.end(), w) ==
                    hover_scratch_.end())
                    w->on_hover_leave();
            for (auto *w : hover_scratch_)
                if (std::find(hover_nodes_.begin(), hover_nodes_.end(), w) ==
                    hover_nodes_.end())
                    w->on_hover_enter();

            hover_nodes_ = hover_scratch_;
        }

        // 最内层提供了裁决层就用它 —— 这正是“命中链决定裁决层”
        [[nodiscard]] gesture_arena *arena_of(const std::vector<widget *> &chain) noexcept
        {
            if (force_default_arena_)
                return &default_arena_;
            for (auto *w : chain)
                if (auto *a = w->arena_override(); a != nullptr)
                    return a;
            return &default_arena_;
        }

        // 裁决层随“本次路由用的命中链”走：指针/滚轮看悬停链，键盘看焦点链。
        // 只在空闲时真正换（旧裁决层不能还有候选在等），否则留到下一次空闲。
        void update_arena_for(const std::vector<widget *> &chain)
        {
            if (auto *want = arena_of(chain); want != nullptr && want != arena_)
                request_arena(want);
            apply_pending_arena();
        }

        void apply_pending_arena()
        {
            if (pending_arena_ == nullptr || pending_arena_ == arena_ || !can_swap())
                return;
            arena_ = pending_arena_;
            arena_->bind_sink(this);
            pending_arena_ = nullptr;
            ++swaps_;
            if (trace_input)
                std::println("[route] 裁决层切换 → {}（命中：{}）", arena_->name(),
                             hover_nodes_.empty() ? "root"
                                                  : hover_nodes_.front()->name());
        }

        [[nodiscard]] bool can_swap() const noexcept
        {
            if (!arena_->idle())
                return false;
            for (const auto &t : pointers_)
                if (!t.chain.empty() || t.owner != nullptr)
                    return false;
            return true;
        }

        [[nodiscard]] pointer_track *active_track() noexcept
        {
            for (auto &t : pointers_)
                if (!t.chain.empty())
                    return &t;
            return nullptr;
        }

        [[nodiscard]] static widget *deepest(const std::vector<widget *> &nodes) noexcept
        {
            return nodes.empty() ? nullptr : nodes.front();
        }

        void rebuild_focus_chain()
        {
            focus_nodes_.clear();
            for (widget *w = focus_; w != nullptr; w = w->parent())
                focus_nodes_.push_back(w);
        }

        input::glfw_forward &hardware_;
        widget &root_;
        priority_arena default_arena_;
        gesture_arena *arena_{nullptr};
        gesture_arena *pending_arena_{nullptr};
        bool force_default_arena_{false};
        modifier_state mods_{};
        bool has_cursor_{false};
        event::position2d_event cursor_{};
        std::array<pointer_track, static_cast<std::size_t>(event::MouseButtons::eSIZE)>
            pointers_{};
        std::vector<widget *> hover_nodes_{};
        std::vector<widget *> hover_scratch_{};
        widget *focus_{nullptr};
        std::vector<widget *> focus_nodes_{};
        int swaps_{0};
        int unhandled_{0};
    };

} // namespace mcs::vulkan::ui
