// ============================================================================
// table12/session.cpp — Session 五门与 Impl 的唯一定义点
//
//   这里是核心唯一 include detail/* 的地方：
//     runtime（AppRouter/pipe）/ gestures / layout_passes / paint_passes。
// ============================================================================

#include "session.hpp"

#include "detail/gestures.hpp"
#include "detail/layout_passes.hpp"
#include "detail/paint_passes.hpp"
#include "detail/runtime.hpp"

namespace table::session
{
// 门实现的内部通道（只有 session.cpp 用它；生产代码不直接摸 impl）
struct Internals
{
    static Session::Impl &get(Session &s) noexcept
    {
        return *s.impl;
    }
    static const Session::Impl &get(const Session &s) noexcept
    {
        return *s.impl;
    }
};

Session::Session() : impl(std::make_unique<Impl>()) {}
Session::~Session() = default;
Session::Session(Session &&) noexcept = default;
Session &Session::operator=(Session &&) noexcept = default;

void Session::reset()
{
    state.reset(model);
    interaction.reset();
    layout = {};
    draw = {};
}

void Session::reconcile()
{
    state.reconcile(model);
}

using command::Context;

Outcome dispatch(Session &s, const command::Command &cmd)
{
    Outcome out;
    // 连击计数必须在 feature 之前更新（edit 先读 click_count）
    if (const auto *p = std::get_if<command::Press>(&cmd))
        detail::note_click(s.state, s.interaction, *p);

    Context ctx{s.model, s.state, s.interaction, s.theme, s.layout, s.font, s.trace, s.id};

    Outcome routed;
    std::visit([&](const auto &one) { routed.merge(Internals::get(s).router.handle(ctx, one)); }, cmd);
    if (!routed.handled)
    {
        // 没人认领 → gesture fallback（选择/拖拽/滚动）
        std::visit(
            [&](const auto &one) {
                routed.merge(detail::gesture(s.model, s.state, s.interaction, s.theme,
                                             s.layout, Internals::get(s).router, s.font, one));
            },
            cmd);
    }

    out.merge(routed);
    out.handled = out.handled || !out.effects.empty();
    // Model 不在这里写：由 Host 调 apply_effects（Outcome.model_changed 恒 false）
    out.model_changed = false;

    // blanket touch：等价 ability11 apply_effects 末尾那次 ui.touch()。
    s.state.touch();
    out.state_changed = true;
    return out;
}

bool apply_effects(Session &s, const Outcome &out)
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

Outcome feed_text(Session &s, std::string_view text)
{
    Outcome out;
    Context ctx{s.model, s.state, s.interaction, s.theme, s.layout, s.font, s.trace, s.id};
    out.handled = Internals::get(s).router.text(ctx, text);
    out.model_changed = false;
    s.state.touch();
    out.state_changed = true;
    return out;
}

void ime_compose(Session &s, std::string text)
{
    Context ctx{s.model, s.state, s.interaction, s.theme, s.layout, s.font, s.trace, s.id};
    Internals::get(s).router.get<detail::EditFeature>().compose(ctx, std::move(text));
    s.state.touch();
}

void open_menu(Session &s, int col, float x, float y)
{
    Context ctx{s.model, s.state, s.interaction, s.theme, s.layout, s.font, s.trace, s.id};
    Internals::get(s).router.get<detail::MenuFeature>().open(ctx, col, x, y);
    s.state.touch();
}

Outcome paste_text(Session &s, std::string text)
{
    Outcome out;
    Context ctx{s.model, s.state, s.interaction, s.theme, s.layout, s.font, s.trace, s.id};
    out.merge(Internals::get(s).router.get<detail::EditFeature>().paste(ctx, std::move(text)));
    out.model_changed = false;
    s.state.touch();
    out.state_changed = true;
    return out;
}

bool modal_open(const Session &s) noexcept
{
    return s.state.filter.edit >= 0 || s.state.find.mode || s.state.edit.active() ||
           s.state.menu.open;
}

bool cancel_modal(Session &s) noexcept
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

void observe_pointer(Session &s, Point p, double now) noexcept
{
    s.interaction.pointer = p;
    s.interaction.now = now;
}

void update_hover(Session &s, const Target &t)
{
    intent::hover(s.state, s.interaction, t);
}

void clear_hover(Session &s)
{
    s.interaction.hover = {};
    s.interaction.band_row = s.interaction.band_col = -1;
    s.interaction.left = false;
    s.interaction.drag = intent::DragMode::none;
    s.state.menu.open = false;
    s.state.touch();
}

void wheel(Session &s, float dx, float dy, bool shift)
{
    intent::wheel(s.state, s.interaction, s.theme, dx, dy, shift, &s.layout);
    s.state.touch();
}

Frame compose(Session &s, const Rect &viewport)
{
    detail::compose_layout(s.model, s.state, s.interaction, s.theme, s.layout, viewport, s.font,
                           Internals::get(s).router, Internals::get(s).pipe);
    detail::compose_draw(s.model, s.state, s.interaction, s.theme, s.layout, s.font,
                         Internals::get(s).router, s.draw, s.id);
    s.layout.version = s.state.revision;
    return Frame{s.layout, s.draw, s.state.revision, s.id};
}

Target hit(const Session &s, float x, float y)
{
    const Zone z = detail::hit_zone(s.model, s.layout, s.state, x, y, s.id);
    return model::target_of(s.model, s.id, z);
}
} // namespace table::session
