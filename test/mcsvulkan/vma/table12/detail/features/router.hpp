#pragma once
// ============================================================================
// table12/detail/features/router.hpp — AppRouter（CommandRouter）
//
//   tuple 顺序 = 行为（F7 不动点，勿改）：
//     Filter, Find, Sort, Resize, Reorder, Freeze, Checks, Scroll,
//     TableResize, Selection, Edit, Clipboard, Undo, Menu, Tooltip
// ============================================================================

#include "filter_find.hpp"
#include "layout_ops.hpp"
#include "selection.hpp"
#include "edit.hpp"
#include "overlay.hpp"

namespace table::detail
{
        struct AppRouter
        {
            std::tuple<FilterFeature, FindFeature, SortFeature, ResizeFeature,
                       ReorderFeature, FreezeFeature, ChecksFeature, ScrollFeature,
                       TableResizeFeature, SelectionFeature, EditFeature,
                       ClipboardFeature, UndoFeature, MenuFeature, TooltipFeature>
                features;

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
            // 短路：第一个认领的 feature 生效
            template <class E>
            Outcome handle(Context &c, const E &cmd)
            {
                Outcome total;
                std::apply(
                    [&](auto &...f) {
                        auto run = [&](auto &feat) {
                            if (total.handled)
                                return;
                            total.merge(feat.handle(c, cmd));
                        };
                        (run(f), ...);
                    },
                    features);
                return total;
            }
            void rows(Context &c, std::vector<row_idx> &r)
            {
                get<FilterFeature>().rows(c, r);
                get<SortFeature>().rows(c, r);
            }
            void paint(const PaintContext &c, DrawList &fr) const
            {
                std::apply([&](const auto &...f) { (f.paint(c, fr), ...); }, features);
            }
            // char 路径：按当前焦点分发给 filter / find / edit
            bool text(Context &c, std::string_view s)
            {
                if (c.v.filter.edit >= 0)
                    return get<FilterFeature>().text(c, s);
                if (c.v.find.mode)
                    return get<FindFeature>().text(c, s);
                return get<EditFeature>().text(c, s);
            }
        };
} // namespace table::detail
