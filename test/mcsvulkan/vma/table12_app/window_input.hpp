#pragma once
// ============================================================================
// table12_app/window_input.hpp — window 层的输入链（只放定义）
//
//   依赖：host.hpp（Binding/Host）。ui_dao / InputCtx / hoverPool 由 demo 在本头
//   之前提供（DAG：shader_abi/glue/platform/encode -> host -> window_input）。
//   迁移分块：alpha = trace + on_hover；beta = Binding::on_* 定义；
//   gamma = 其余自由函数 + 模型 API + cancel_modal。
// ============================================================================

#include "host.hpp"

namespace table::window
{
// ── 诊断开关（环境变量只活在这一层；核心不读 env、不做 I/O） ──────────
template <class... A>
inline void trace(std::format_string<A...> fmt, A &&...a)
{
    static const bool on = std::getenv("TABLE_TRACE") != nullptr;
    if (on)
        std::println(fmt, std::forward<A>(a)...);
}

// ── GPU hover 回调（hoverPool().bind(&on_hover)；诊断用） ─────────────
inline void on_hover(picking_result r, bool enter, InputCtx &,
                     ui_new::ScreenWidget *screen) noexcept
{
    if (screen == nullptr || screen->table_host_ctx == nullptr)
    {
        trace("[hover] dropped: no screen/host");
        return;
    }
    auto &h = *screen->table_host_ctx;
    if (!h.accept_pick(r))
        return;   // the only frame-level renderVersion comparison
    h.on_pick(r); // diagnostics: update pick/last_pick only
    trace("[hover] {} key={:#x}", enter ? "ENTER" : "LEAVE",
                    r.key.entity_index);
}

} // namespace table::window
