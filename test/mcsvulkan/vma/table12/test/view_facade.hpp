#pragma once
// ============================================================================
// table12/test/view_facade.hpp — test-only 门面（让 171 条 oracle 原样保留）
//
// ability11 的断言/测试体用 view 一个对象：v.sel / v.checks / v.ui.hover /
// v.ui.last_click_t ...；ability12 把 ui_state 拆成了 intent::Interaction。
//
// 这里用“引用代理”把两部分重新拼成一个 v：
//   - 代理成员是同一对象的别名，读写语义完全一致（写 v.ui.last_click_t 就是写
//     Interaction.last_click_t，不是副本）；
//   - 隐式转换 operator State&() 让 36 处 command::Context 与 2 处 intent::wheel
//     的实参保持原样（它们要的是 intent::State&）。
//
// 约束（写死）：
//   * 只用于 table12/test/，公共头与核心签名永远不认识 ViewFacade；
//   * Session 在 facade 生命周期内不得移动/拷贝（selftest 里 Session 在栈上不动，
//     Session 本身已 delete 复制）；
//   * 只转引用，不转值（避免把 facade 切片成 State）。
// ============================================================================

#include "../frame.hpp"
#include "../intent.hpp"
#include "../model.hpp"

namespace table::test
{
// ── v.ui 的替身：ability11 的 comp::ui_state ──────────────────────────
struct UiFacade
{
    intent::Interaction &ix;

    Target &hover = ix.hover;
    int &band_row = ix.band_row;
    int &band_col = ix.band_col;
    glm::vec2 &pointer = ix.pointer;
    bool &left = ix.left;
    double &now = ix.now;
    intent::DragMode &drag = ix.drag;
    Target &drag_target = ix.drag_target;
    double &last_click_t = ix.last_click_t;
    glm::vec2 &last_click_pos = ix.last_click_pos;
    int &click_count = ix.click_count;
    bool &header_moved = ix.header_moved;
};

// ── v：State 的组件 + Interaction 的 ui ───────────────────────────────
struct ViewFacade
{
    intent::State &state;
    intent::Interaction &interaction;

    intent::Selection &sel = state.sel;
    intent::Checks &checks = state.checks;
    intent::Edit &edit = state.edit;
    intent::Filter &filter = state.filter;
    intent::Columns &cols = state.cols;
    intent::Scroll &scroll = state.scroll;
    intent::Sort &sort = state.sort;
    intent::History &history = state.history;
    intent::Find &find = state.find;
    intent::Menu &menu = state.menu;
    intent::Tooltip &tip = state.tip;

    UiFacade ui{interaction};

    // ability11 的 view::reset 同时清 UI（hover/拖拽）；这里等价委托。
    // 不重置 version/revision：ability11 的 ui.reset() 本身也不重置 version。
    void reset(const model::Table &m)
    {
        state.reset(m);
        interaction.reset();
    }

    // ── 为什么需要隐式转换（方案 A） ────────────────────────────────────
    //   selftest 里有 36 处  command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
    //   第二参要 intent::State&；2 处 intent::wheel(v, ix, p, ...) 同理。
    //   36 处是主体不是边角，所以用转换运算符，36+2 处一行不改。
    //   若某处同时有 State&/const State& 两个重载而出现歧义，只保留非 const 版本
    //   （const 参数可由非 const 引用经限定转换绑定），并在该调用点记一笔。
    operator intent::State &() noexcept
    {
        return state;
    }
    operator const intent::State &() const noexcept
    {
        return state;
    }
};
} // namespace table::test
