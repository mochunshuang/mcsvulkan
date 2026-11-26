#pragma once
// ============================================================================
// table12/command.hpp — 命令词汇、结果、feature 组合机制
//
//   Command    输入链唯一的语义词汇（press/release/drag/wheel/key/...）。
//   Effect     命令的副作用值（改模型 / 写剪贴板），不是回调。
//   Outcome    唯一的命令结果；feature/host 之间只传它。
//   Context    feature 能看到的全部上下文；没有 void*、没有窗口、没有 GPU。
//   Router     feature 的有序组合（短路折叠），顺序即行为（F7）。
//
//   依赖：contract/model/intent/frame。不 include session/window/platform。
// ============================================================================

#include <cstdlib>
#include <inplace_vector>
#include <variant>

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

    // ── Effect：值，不是回调；SetCell 由 Host 在 dispatch 之后调 session::apply_effects 应用 ──
    using Effect = std::variant<SetCell, WriteClipboard>;

    // ── Command：唯一命令类型（host 构造；feature 按类型处理） ─────────────
    using Command = std::variant<Hover, Press, Release, Drag, Wheel, KeyPress,
                                 ClearFilter, ScrollBegin>;

    // ── EffectList：定长、无堆分配；溢出 = 程序错误（不静默丢 SetCell/剪贴板） ──
    using EffectList = std::inplace_vector<Effect, 8>;

    inline void push_effect(EffectList &out, Effect e) noexcept
    {
        if (out.size() == out.max_size())
        {
            std::fputs("table::command::EffectList overflow (8 effects)\n", stderr);
            std::abort();
        }
        out.push_back(std::move(e));
    }

    // ── Outcome：唯一“命令改变了什么”的返回值 ─────────────────────────────
    struct Outcome
    {
        bool handled{false};
        bool model_changed{false}; // dispatch 阶段恒 false；apply_effects 之后才可能为 true
        bool state_changed{false};
        EffectList effects{};

        static Outcome handled_now() noexcept
        {
            Outcome o;
            o.handled = true;
            return o;
        }
        static Outcome changed_now() noexcept
        {
            Outcome o;
            o.handled = true;
            o.state_changed = true;
            return o;
        }
        // 合并“已执行 feature 的全部字段”（不按 handled 过滤：未认领也可能已写 State）
        void merge(const Outcome &o) noexcept
        {
            handled = handled || o.handled;
            model_changed = model_changed || o.model_changed;
            state_changed = state_changed || o.state_changed;
            for (const auto &e : o.effects)
                push_effect(effects, e);
        }
    };

    // 合并两个结果（保持左值字段，右值只贡献 handled/effects）
    inline Outcome merged(Outcome a, Outcome b) noexcept
    {
        a.merge(b);
        return a;
    }

    // bool 风格的 feature helper 返回值 → Outcome（行为不变：handled 即原 bool）
    inline Outcome handled(bool ok) noexcept
    {
        return ok ? Outcome::handled_now() : Outcome{};
    }

    // ── Context：feature 的唯一入口参数（没有 fx：Outcome 是唯一结果） ──────
    struct Context
    {
        const model::Table &model;
        intent::State &state;
        intent::Interaction &interaction;
        const intent::Theme &theme;
        const frame::Layout &layout;
        const Font &font;
        bool trace{false};
        TableId id{};
    };

    // ── Router：feature 的有序组合（顺序 = 行为，F7 不动点） ───────────────
    //   按顺序执行；合并每个已执行 feature 的全部字段；handled 只决定是否继续。
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

        Outcome dispatch(Context &c, const Command &cmd)
        {
            Outcome total;
            std::visit(
                [&](const auto &one) {
                    std::apply(
                        [&](auto &...f) {
                            auto run_one = [&](auto &feat) {
                                if (total.handled)
                                    return;
                                Outcome sub = feat.handle(c, one);
                                total.merge(sub);
                            };
                            (run_one(f), ...);
                        },
                        features);
                },
                cmd);
            return total;
        }
    };
} // namespace table::command
