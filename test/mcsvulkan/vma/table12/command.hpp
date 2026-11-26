#pragma once
// ============================================================================
// table12/command.hpp — 命令词汇、结果、feature 组合机制
//
//   Command   输入链唯一的语义词汇（press/release/drag/wheel/key/...）。
//             Host 把平台事件翻译成 Command，session::dispatch 负责分发。
//   Effect    命令的副作用值（改模型 / 写剪贴板），不是回调。
//   Outcome   唯一的命令结果；host 据此决定剪贴板/回调/重绘。
//   Context   feature 能看到的全部上下文；没有 void*、没有窗口、没有 GPU。
//   Router    feature 的有序组合（tuple + 短路折叠），顺序即行为（F7）。
//
//   依赖：contract/model/intent/frame。不 include session/window/platform。
// ============================================================================

#include "contract.hpp"
#include "frame.hpp"
#include "intent.hpp"
#include "model.hpp"

namespace table::command
{
    enum class Key : std::uint8_t
    {
        none = 0,
        up,
        down,
        left,
        right,
        home,
        end,
        pgup,
        pgdn,
        enter,
        esc,
        tab,
        backspace,
        del,
        space,
        f2,
        a,
        c,
        v,
        x,
        z,
        y,
        f,
    };

    struct Hover
    {
        Zone z{};
    };
    struct Press
    {
        float x{0}, y{0};
        Zone z{};
        bool dbl{false}, shift{false}, ctrl{false};
        Target t{};
    };
    struct Release
    {
        float x{0}, y{0};
        Target t{};
    };
    struct Drag
    {
        float x{0}, y{0};
        Target t{};
    };
    struct Wheel
    {
        float dy{0}, dx{0};
        bool shift{false};
    };
    struct KeyPress
    {
        Key k{Key::none};
        bool shift{false}, ctrl{false};
        Target t{};
    };
    // 语义命令：由输入路由（apply）识别手势后发出，由 feature 消费
    struct ClearFilter
    {
        int col{-1};
    };
    // 选择：手势/键盘只发这些语义命令，SelectionFeature 是唯一写 sel 的地方
    struct ScrollBegin
    {
        int axis{0}; // 0=v 1=h
        float base{0}, pos{0};
    };

    struct SetCell
    {
        RowId id{};
        int col{0};
        std::string text;
    };
    struct WriteClipboard
    {
        std::string text;
    };
    using Effect = std::variant<SetCell, WriteClipboard>;

// ── Command：唯一命令类型（host 构造；feature 按类型处理） ─────────────
using Command = std::variant<Hover, Press, Release, Drag, Wheel, KeyPress,
                             ClearFilter, ScrollBegin>;

// ── Effect：值，不是回调；SetCell 由 Host 在 dispatch 之后调 session::apply_effects 应用 ──
using Effect = std::variant<SetCell, WriteClipboard>;

// ── Outcome：唯一“命令改变了什么”的返回值 ─────────────────────────────
struct Outcome
{
    bool handled{false};
    bool model_changed{false};
    bool state_changed{false};
    std::vector<Effect> effects{};

    static Outcome handled_now() noexcept
    {
        Outcome o;
        o.handled = true;
        return o;
    }
    void merge(const Outcome &o) noexcept
    {
        handled = handled || o.handled;
        model_changed = model_changed || o.model_changed;
        state_changed = state_changed || o.state_changed;
        if (!o.effects.empty())
            effects.insert(effects.end(), o.effects.begin(), o.effects.end());
    }
};

// ── Context：feature 的唯一入口参数（取代 ability11 的 table_ctx） ──────
//   d=model, v=state, ix=interaction, p=theme, g=layout, ft=font, fx=效果出口。
//   ability11 的 void* out 已删除：所有输出都是 Outcome 里的值。
struct Context
{
    const model::Table &d;
    intent::State &v;
    intent::Interaction &ix;
    const intent::Theme &p;
    const frame::Layout &g;
    const Font &ft;
    std::vector<Effect> *fx{nullptr};
    bool trace{false};
    TableId id{};
};

// ── Router：feature 的有序组合（顺序 = 行为，F7 不动点） ───────────────
//   feature 只报告“是否认领”（bool），Router 把它抬成 Outcome：
//   效果经 Context.fx 汇入 Outcome.effects，没有旁路输出参数。
template <class... Fs>
struct Router
{
    std::tuple<Fs...> features;

    template <class F>
    F &get() noexcept
    {
        return std::get<F>(features);
    }
    template <class F>
    const F &get() const noexcept
    {
        return std::get<F>(features);
    }

    // 短路折叠：第一个认领的 feature 生效，其余跳过（与 ability11 的 if 链一致）
    Outcome dispatch(Context &c, const Command &cmd)
    {
        Outcome total;
        c.fx = &total.effects;
        std::visit(
            [&](const auto &one) {
                std::apply(
                    [&](auto &...f) {
                        std::initializer_list<int>{
                            (total.handled
                                 ? 0
                                 : (f.handle(c, one) ? (total.handled = true, 0) : 0))...};
                    },
                    features);
            },
            cmd);
        return total;
    }
};
} // namespace table::command
