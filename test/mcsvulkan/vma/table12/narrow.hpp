#pragma once
// ============================================================================
// table12/narrow.hpp — 输入窄门（app 层用；不是核心公共面）
//
//   这些入口只写 State/Interaction（不碰 Model），由 window/scene 层在
//   平台 I/O 之后调用：
//     feed_text   文本输入（filter/find/edit 按焦点分发）
//     paste_text  剪贴板粘贴（Host 先读平台剪贴板）
//     ime_compose IME 预编辑串
//     open_menu   右键菜单
//     modal_open  是否有模态（读）
//     cancel_modal Esc 取消模态（写 State）
//     wheel       滚轮（写 State/Interaction）
//
//   核心公共头（session.hpp）只留：schema + dispatch/apply_effects/compose/hit
//   + observe_pointer/update_hover/clear_hover。
// ============================================================================

#include "session.hpp"

namespace table::session
{
using command::Outcome;

Outcome feed_text(Session &s, std::string_view text);
Outcome paste_text(Session &s, std::string text);
void ime_compose(Session &s, std::string text);
void open_menu(Session &s, int col, float x, float y);
[[nodiscard]] bool modal_open(const Session &s) noexcept;
bool cancel_modal(Session &s) noexcept;
void wheel(Session &s, float dx, float dy, bool shift);
} // namespace table::session
