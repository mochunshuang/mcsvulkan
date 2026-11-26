#pragma once
// ============================================================================
// table12/session.hpp — 组合根（公共头：只有 schema + 五门声明）
//
//   门（每层只有一个入口）：
//     session::dispatch        唯一命令入口（只写 State/Interaction）
//     session::apply_effects   唯一 Model 写点（Host 在 dispatch 之后调用）
//     session::compose         唯一派生入口（layout passes -> paint passes）
//     session::hit             唯一命中入口（几何 + 语义覆盖，只读）
//
//   实现细节（AppRouter / RowPipeline / passes / gestures）全部藏在
//   Session::Impl（实现见 session.cpp；测试见 test/access.hpp）。
//   本头只依赖 schema 头（G5 验收）。
//
//   数据流：
//     Command -> dispatch -> Router(feature 短路链) -> Outcome{effects}
//             -> Host -> apply_effects（SetCell 写 Model）
//     State.revision 变化 -> compose -> geom + frame（派生缓存）
// ============================================================================

#include <memory>

#include "command.hpp"
#include "contract.hpp"
#include "frame.hpp"
#include "intent.hpp"
#include "model.hpp"

namespace table::test
{
struct SessionAccess;
}

namespace table::session
{
using command::Command;
using command::Effect;
using command::Outcome;
using command::SetCell;
using frame::Frame;

// ── 会话：真值 + 意图 + 派生缓存；实现细节在 Impl（pimpl） ──────────────
struct Session
{
    // 真值：唯一写入口是 session::apply_effects（Host 在 dispatch 之后调用）
    model::Table model{};
    // 意图：持久（State）+ 临时（Interaction）
    intent::State state{};
    intent::Interaction interaction{};
    // 呈现配置：由宿主提供，核心只读
    intent::Theme theme{};
    Font font{};
    // 派生缓存：compose 写，hit 读；随时可丢
    frame::Layout layout{};
    frame::DrawList draw{};
    TableId id{};
    bool trace{false};

    Session();
    ~Session();
    Session(Session &&) noexcept;
    Session &operator=(Session &&) noexcept;
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    [[nodiscard]] std::uint32_t revision() const noexcept
    {
        return state.revision;
    }
    void reset();
    void reconcile();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    friend struct Internals;
    friend struct table::test::SessionAccess;
};

// ── 五门（定义在 session.cpp；本头只声明） ──────────────────────────────
Outcome dispatch(Session &s, const command::Command &cmd);
bool apply_effects(Session &s, const Outcome &out);
Outcome feed_text(Session &s, std::string_view text);
void ime_compose(Session &s, std::string text);
void open_menu(Session &s, int col, float x, float y);
Outcome paste_text(Session &s, std::string text);
[[nodiscard]] bool modal_open(const Session &s) noexcept;
bool cancel_modal(Session &s) noexcept;
void observe_pointer(Session &s, Point p, double now) noexcept;
void update_hover(Session &s, const Target &t);
void clear_hover(Session &s);
void wheel(Session &s, float dx, float dy, bool shift);
Frame compose(Session &s, const Rect &viewport);
[[nodiscard]] Target hit(const Session &s, float x, float y);
} // namespace table::session
