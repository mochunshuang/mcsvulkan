#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  test_util.hpp —— 测试自己的小工具（不属于库）
//
//    at(ms)            把毫秒换成 time_point
//    glfw_post(a,ms,e) 模拟 GLFW 回调：写 arena.now，再交给真分发器
//    expect(...)       比较记录，失败时打印
//    kb/mb/p2/ws/L/R/PRESS/RELEASE  惯用缩写
// ═══════════════════════════════════════════════════════════════════════
#include <cassert>
#include <chrono>
#include <print>
#include <string_view>

#include "uhi.hpp"
#include "uhi_integration.hpp"

namespace test_util
{
    namespace event = mcs::vulkan::event;

    inline uhi::time_point at(int ms)
    {
        return uhi::time_point{} + std::chrono::milliseconds{ms};
    }

    inline void expect(std::string_view got, std::string_view want, const char *where)
    {
        if (got == want) return;
        std::print(stderr, "{}:\n  got  [{}]\n  want [{}]\n", where, got, want);
        assert(false && "unexpected trace");
    }

    // 模拟 GLFW 回调：时间打在 arena 上（生产里由 poll 循环写 Clock::now()），
    // 然后交给真的 event_dispatcher。
    template <class Arena, class Event>
    void glfw_post(Arena &a, int ms, const Event &e)
    {
        a.now = at(ms);
        if constexpr (std::is_same_v<Event, event::keyboard_event>)
            event::keyboard_event_dispatcher::instance().distribute(e);
        else if constexpr (std::is_same_v<Event, event::mousebutton_event>)
            event::mousebutton_event_dispatcher::instance().distribute(e);
        else if constexpr (std::is_same_v<Event, event::position2d_event>)
            event::cursor_pos_event_dispatcher::instance().distribute(e);
        else if constexpr (std::is_same_v<Event, event::cursor_enter_event>)
            event::cursor_enter_event_dispatcher::instance().distribute(e);
        else
            event::window_size_event_dispatcher::instance().distribute(e);
    }

    using kb = event::keyboard_event;
    using mb = event::mousebutton_event;
    using p2 = event::position2d_event;
    using ws = event::window_size_event;

    inline constexpr auto L       = event::MouseButtons::eMOUSE_BUTTON_LEFT;
    inline constexpr auto R       = event::MouseButtons::eMOUSE_BUTTON_RIGHT;
    inline constexpr auto PRESS   = event::Action::ePRESS;
    inline constexpr auto RELEASE = event::Action::eRELEASE;
} // namespace test_util

using test_util::at;
using test_util::expect;
using test_util::glfw_post;
using test_util::kb;
using test_util::mb;
using test_util::p2;
using test_util::ws;
using test_util::L;
using test_util::R;
using test_util::PRESS;
using test_util::RELEASE;
