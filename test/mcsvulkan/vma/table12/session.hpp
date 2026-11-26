#pragma once
// ============================================================================
// table12/session.hpp — 组合根：一个 Session 拥有一张表的全部真值与派生缓存
//
//   门（每层只有一个入口）：
//     session::dispatch        唯一命令入口（只写 State/Interaction）
//     session::apply_effects   唯一 Model 写点（Host 在 dispatch 之后调用）
//     session::compose         唯一派生入口（layout passes → paint passes）
//     session::hit             唯一命中入口（几何 + 语义覆盖，只读）
//     window::route_event / platform::encode / platform::resolve 在 app 层
//
//   dispatch + apply_effects 合起来是唯一写路径；Host 只按序调用，不自己写数据。
//
//   窄门只写 Interaction：observe_pointer / update_hover / clear_hover / wheel
//   窄门只写 State（不碰 Model）：feed_text / open_menu / paste_text / cancel_modal
//
//   数据流：
//     Command → dispatch → CommandRouter(feature 短路链) → Outcome{effects}
//             → Host → apply_effects（SetCell 写 Model）
//     State.revision 变化 → compose → geom + frame（派生缓存）
//
//   每次命令的 touch：feature/gesture 自身的 65 处原样 + dispatch 末尾 1 次 blanket
//   （等价 ability11 apply_effects 末尾那次 ui.touch()）；apply_effects 不 touch。
//
//   member 名字（G0 冻结）：model / state / interaction / theme / font / router /
//   pipe / layout / draw；不再有 data/view/... 之类的短名，也没有同名访问器。
//   注意：成员 model 会遮蔽 table::model 命名空间，Session 内禁止写 model::X
//   （需要时写全限定 table::model::X，或把实现放到类外的门里）。
//
//   依赖：contract/model/intent/frame/command + detail（features/passes）。
//   不 include window/platform/ui_new/GLFW/Vulkan。
// ============================================================================

#include "detail/gestures.hpp"
#include "detail/layout_passes.hpp"
#include "detail/paint_passes.hpp"

namespace table::session
{
// 门签名里只用这些命令/结果名字
using command::Command;
using command::Effect;
using command::Outcome;
using command::SetCell;
using frame::Frame;

// ── 会话：Model / State / Interaction / Theme / 派生缓存 ───────────────
struct Session
{
    // ── 真值：唯一写入口是 session::apply_effects（Host 在 dispatch 之后调用） ──
    model::Table model{}; // Model：列 + 列主序单元格 + 稳定行身份
    // 注：成员 model 遮蔽 table::model，Session 内禁止再写 model::X（用 table::model::X）
    // ── 意图：持久（State）+ 临时（Interaction），ability11 的 view 拆分 ──
    intent::State state{};      // State：滚动/排序/筛选/选择/勾选/列/历史/菜单/编辑
    intent::Interaction interaction{}; // Interaction：hover/拖拽/指针/连击计时
    // ── 呈现配置：由宿主提供，核心只读 ────────────────────────────────
    intent::Theme theme{};   // 样式与开关
    Font font{};             // 字形表
    // ── 组合根：feature 组合 + 行变换缓冲 ─────────────────────────────
    detail::AppRouter router{}; // CommandRouter（15 个 feature 的有序组合）
    frame::RowPipeline pipe{};   // 行变换缓冲（filter/sort 写，compose 读）
    // ── 派生缓存：compose 写，hit 读；随时可丢，不是第二份真值 ────────
    frame::Layout layout{};  // Layout（layout passes 的输出）
    frame::DrawList draw{};  // DrawList（paint passes 的输出）
    TableId id{};
    bool trace{false};

    // G0 冻结：字段名就是规范名（model/state/interaction/theme/font/layout/draw），
    // 不再提供 model()/state()/... 访问器（G2 一次性改字段名时也不加回来）。
    [[nodiscard]] std::uint32_t revision() const noexcept
    {
        return state.revision;
    }

    void reset()
    {
        state.reset(model);
        interaction.reset();
        layout = {};
        draw = {};
    }
    void reconcile()
    {
        state.reconcile(model);
    }
};

// ════════════════════════════════════════════════════════════════════════
// 写入口口径（与 ability11 等价，措辞见 HANDOFF §4.2/§5.7 的补充）：
//   dispatch      只写 State / Interaction（feature + gesture），产出 Outcome
//   apply_effects 只写 Model，由 Host 在 dispatch 之后调用
//   两者合起来是唯一写路径；Host 只按序调用，不自己写数据。
// ════════════════════════════════════════════════════════════════════════

// 门 2：唯一命令入口（只写 State/Interaction，不写 Model）
inline Outcome dispatch(Session &s, const command::Command &cmd)
{
    Outcome out;
    // 连击计数必须在 feature 之前更新（edit 先读 click_count）
    if (const auto *p = std::get_if<command::Press>(&cmd))
        detail::note_click(s.state, s.interaction, *p);

    std::vector<command::Effect> fx;
    command::Context ctx{s.model, s.state, s.interaction, s.theme, s.layout, s.font, &fx, s.trace,
                         s.id};

    bool handled = false;
    std::visit([&](const auto &one) { handled = s.router.handle(ctx, one); }, cmd);
    if (!handled)
    {
        // 没人认领 → gesture fallback（选择/拖拽/滚动）
        std::visit(
            [&](const auto &one) {
                detail::gesture(s.model, s.state, s.interaction, s.theme, s.layout, s.router, s.font,
                                one, fx);
            },
            cmd);
    }

    out.handled = handled || !fx.empty();
    // Model 不在这里写：由 Host 调 apply_effects（Outcome.model_changed 恒 false）
    out.model_changed = false;
    for (auto &e : fx)
        out.effects.push_back(std::move(e));

    // blanket touch：等价 ability11 apply_effects 末尾那次 ui.touch()。
    // feature/gesture 自己的 65 处 touch 全部保留，这里只补一次。
    s.state.touch();
    out.state_changed = true;
    return out;
}

// 门：唯一 Model 写点（Host 在 dispatch 之后调用；只写 Model，不 touch）
inline bool apply_effects(Session &s, const Outcome &out)
{
    bool model_changed = false;
    for (const auto &e : out.effects)
    {
        if (const auto *p = std::get_if<command::SetCell>(&e))
        {
            const int r = s.model.index_of(p->id);
            if (r >= 0)
            {
                s.model.set_cell(r, p->col, p->text);
                model_changed = true;
            }
        }
        // WriteClipboard 属于平台 side，由 Host 消费；这里不处理
    }
    return model_changed;
}

// ---- 窄门：char 不走 gesture，按焦点分发给 filter / find / edit ----------
inline Outcome feed_text(Session &s, std::string_view text)
{
    Outcome out;
    std::vector<command::Effect> fx;
    command::Context ctx{s.model, s.state, s.interaction, s.theme, s.layout, s.font, &fx, s.trace,
                         s.id};
    out.handled = s.router.text(ctx, text);
    out.model_changed = false;
    for (auto &e : fx)
        out.effects.push_back(std::move(e));
    s.state.touch();
    out.state_changed = true;
    return out;
}

// ---- 窄门：IME 预编辑串（平台读 IME，核心只存 compose） -----------------
inline void ime_compose(Session &s, std::string text)
{
    std::vector<command::Effect> fx;
    command::Context ctx{s.model, s.state, s.interaction, s.theme, s.layout, s.font, &fx, s.trace,
                         s.id};
    s.router.get<detail::EditFeature>().compose(ctx, std::move(text));
    s.state.touch();
}

// ---- 窄门：右键菜单 / 剪贴板粘贴 / Esc 取消（平台先做 I/O，再交给核心） --
inline void open_menu(Session &s, int col, float x, float y)
{
    command::Context ctx{s.model, s.state, s.interaction, s.theme, s.layout, s.font, nullptr, s.trace,
                         s.id};
    s.router.get<detail::MenuFeature>().open(ctx, col, x, y);
    s.state.touch();
}

inline Outcome paste_text(Session &s, std::string text)
{
    Outcome out;
    std::vector<command::Effect> fx;
    command::Context ctx{s.model, s.state, s.interaction, s.theme, s.layout, s.font, &fx, s.trace,
                         s.id};
    out.handled = s.router.get<detail::EditFeature>().paste(ctx, std::move(text));
    out.model_changed = false;
    for (auto &e : fx)
        out.effects.push_back(std::move(e));
    s.state.touch();
    out.state_changed = true;
    return out;
}

[[nodiscard]] inline bool modal_open(const Session &s) noexcept
{
    return s.state.filter.edit >= 0 || s.state.find.mode || s.state.edit.active() ||
           s.state.menu.open;
}

// Esc 优先取消模态（编辑/筛选/查找/菜单），而不是关窗口
inline bool cancel_modal(Session &s) noexcept
{
    auto &v = s.state;
    if (v.filter.edit >= 0)
    {
        const auto col = static_cast<std::size_t>(v.filter.edit);
        if (col < v.filter.text.size())
            v.filter.text[col] = v.filter.orig;
        v.filter.input = v.filter.orig;
        v.filter.orig.clear();
        v.filter.compose.clear();
        v.filter.edit = -1;
        v.touch();
        return true;
    }
    if (v.find.mode)
    {
        v.find.mode = false;
        v.find.compose.clear();
        v.touch();
        return true;
    }
    if (v.edit.active())
    {
        v.edit = {};
        v.touch();
        return true;
    }
    if (v.menu.open)
    {
        v.menu.open = false;
        v.touch();
        return true;
    }
    return false;
}

// ---- 窄门：只写 Interaction（hover / pointer / wheel），不碰 Model ------
inline void observe_pointer(Session &s, Point p, double now) noexcept
{
    s.interaction.pointer = p;
    s.interaction.now = now;
}

inline void update_hover(Session &s, const Target &t)
{
    intent::hover(s.state, s.interaction, t);
}

inline void clear_hover(Session &s)
{
    s.interaction.hover = {};
    s.interaction.band_row = s.interaction.band_col = -1;
    s.interaction.left = false;
    s.interaction.drag = intent::DragMode::none;
    s.state.menu.open = false;
    s.state.touch();
}

inline void wheel(Session &s, float dx, float dy, bool shift)
{
    intent::wheel(s.state, s.interaction, s.theme, dx, dy, shift, &s.layout);
    s.state.touch();
}

// ════════════════════════════════════════════════════════════════════════
// 门 3：唯一派生入口（layout passes → paint passes）
// ════════════════════════════════════════════════════════════════════════
inline Frame compose(Session &s, const Rect &viewport)
{
    detail::compose_layout(s.model, s.state, s.interaction, s.theme, s.layout, viewport, s.font,
                           s.router, s.pipe);
    detail::compose_draw(s.model, s.state, s.interaction, s.theme, s.layout, s.font, s.router, s.draw,
                         s.id);
    s.layout.version = s.state.revision;
    return Frame{s.layout, s.draw, s.state.revision, s.id};
}

// ════════════════════════════════════════════════════════════════════════
// 门 4：唯一命中入口（几何 + 语义覆盖；只读）
// ════════════════════════════════════════════════════════════════════════
[[nodiscard]] inline Target hit(const Session &s, float x, float y)
{
    const Zone z = detail::hit_zone(s.model, s.layout, s.state, x, y, s.id);
    return model::target_of(s.model, s.id, z);
}
} // namespace table::session
