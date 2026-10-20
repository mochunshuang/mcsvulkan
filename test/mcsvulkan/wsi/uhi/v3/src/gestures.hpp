#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  gestures.hpp —— 应用层：四个手势的状态机 + 四个 leaf
//
//  这里没有任何裁决、没有任何关系——关系在测试里用组合子声明。
//  载荷只挂在 click 上（click_event），其余没有载荷。
// ═══════════════════════════════════════════════════════════════════════
#include <chrono>
#include <cstdint>

#include "arena_event.hpp"
#include "uhi.hpp"

namespace gesture
{
    using uhi::step_t;
    using uhi::time_point;

    struct click_event
    {
        double x = 0.0;
    };

    struct click_fsm
    {
        bool        s = false;
        click_event payload{};
        step_t      step(const ae::input_frame &f, time_point) noexcept
        {
            const auto &c = f.change;
            if (c.change != ae::ChangeType::PointerButton || !c.button.is_left())
                return s ? step_t::active : step_t::idle;
            if (!s && c.button.is_press())
            {
                s         = true;
                payload.x = f.context.pointer_x; // 按下时的位置
                return step_t::active;
            }
            if (s && c.button.is_pelease()) { s = false; return step_t::done; }
            return s ? step_t::active : step_t::idle;
        }
        void reset() noexcept { s = false; }
    };

    struct dbl_fsm
    {
        enum class st : std::uint8_t { idle, first_press, wait_second, second_press };
        st         s = st::idle;
        time_point deadline{};
        static constexpr auto window = std::chrono::milliseconds{300};

        step_t step(const ae::input_frame &f, time_point now) noexcept
        {
            if (s == st::wait_second && now >= deadline) { s = st::idle; return step_t::aborted; }
            const auto &c = f.change;
            if (c.change != ae::ChangeType::PointerButton || !c.button.is_left())
                return s == st::idle ? step_t::idle : step_t::active;
            switch (s)
            {
            case st::idle:
                if (c.button.is_press()) { s = st::first_press; return step_t::active; }
                return step_t::idle;
            case st::first_press:
                if (c.button.is_pelease()) { s = st::wait_second; deadline = now + window; }
                return step_t::active;
            case st::wait_second:
                if (c.button.is_press()) s = st::second_press;
                return step_t::active;
            case st::second_press:
                if (c.button.is_pelease()) { s = st::idle; return step_t::done; }
                return step_t::active;
            }
            return step_t::idle;
        }
        // 只有时间前进时被调用：窗口一过就自己放弃（于是不需要 none 帧）
        step_t tick(time_point now) noexcept
        {
            if (s == st::wait_second && now >= deadline) { s = st::idle; return step_t::aborted; }
            return step_t::hold;
        }
        void reset() noexcept { s = st::idle; }
    };

    struct drag_fsm
    {
        bool   armed = false;
        double acc = 0.0;
        step_t step(const ae::input_frame &f, time_point) noexcept
        {
            const auto &c = f.change;
            if (c.change == ae::ChangeType::PointerButton && c.button.is_left())
            {
                if (c.button.is_press()) { armed = true; acc = 0.0; return step_t::active; }
                if (c.button.is_pelease()) { armed = false; return step_t::aborted; }
            }
            if (!armed) return step_t::idle;
            if (c.change == ae::ChangeType::PointerDownMove)
            {
                acc += (c.move.dx < 0 ? -c.move.dx : c.move.dx) +
                       (c.move.dy < 0 ? -c.move.dy : c.move.dy);
                return acc > 12.0 ? step_t::done : step_t::active;
            }
            return step_t::active;
        }
        void reset() noexcept { armed = false; acc = 0.0; }
    };

    struct key_fsm
    {
        step_t step(const ae::input_frame &f, time_point) noexcept
        {
            const auto &c = f.change;
            if (c.change == ae::ChangeType::Key && c.key.key == ae::Key::eA && c.key.is_press())
                return step_t::done;
            return step_t::idle;
        }
        void reset() noexcept {}
    };

    using mouse_src = uhi::source_t<"mouse.left">;
    using key_src   = uhi::source_t<"keyboard">;

    using g_click = uhi::leaf<uhi::kind_t<"click", click_event>, mouse_src, click_fsm>;
    using g_dbl   = uhi::leaf<uhi::kind_t<"dbl">, mouse_src, dbl_fsm>;
    using g_drag  = uhi::leaf<uhi::kind_t<"drag">, mouse_src, drag_fsm>;
    using g_key   = uhi::leaf<uhi::kind_t<"key_A">, key_src, key_fsm>;
} // namespace gesture
