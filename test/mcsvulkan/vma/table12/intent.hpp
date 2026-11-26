#pragma once
// ============================================================================
// table12/intent.hpp — 意图：State（持久）+ Interaction（临时）
//
//   ability11 用一个 view 同时装“跨帧意图”和“临时交互”，读者无法判断字段寿命。
//   ability12 拆成两个类型：
//     State        表的一生：滚动/排序/筛选/选择/勾选/列/历史/查找/菜单/编辑
//     Interaction  窗口/帧级：hover、拖拽、指针、连击计时（掉帧可丢）
//
//   窄门：hover / wheel 只写 Interaction（+ 滚动量），不碰 Model。
//
//   依赖：contract/model。不 include frame/command/session/window/platform。
// ============================================================================

#include "contract.hpp"
#include "model.hpp"

namespace table::frame
{
struct Layout; // 定义在 frame.hpp；wheel 只需要它的指针做悬停判定
} // namespace table::frame

namespace table::intent
{
using model::Table;

    struct Theme
    {
        float header_h{40.0F}, row_h{30.0F}, row_gap{2.0F}, cell_pad{10.0F};
        float bar{8.0F}, radius{10.0F}, cell_radius{4.0F}, sep_hit{3.0F};
        float edge_hit{6.0F}, min_w{240.0F}, min_h{120.0F};
        float max_w{1.0e30F}, max_h{1.0e30F};
        int frozen{0};
        float select_w{36.0F};
        bool row_select{true};
        bool wrap{false};
        float group_h{26.0F};

        glm::vec4 bg{0.10F, 0.11F, 0.14F, 0.97F};
        glm::vec4 header_bg{0.17F, 0.19F, 0.25F, 1.0F};
        glm::vec4 row_bg[2]{{0.13F, 0.14F, 0.18F, 1.0F}, {0.15F, 0.16F, 0.21F, 1.0F}};
        glm::vec4 row_hover{0.19F, 0.22F, 0.30F, 1.0F};
        glm::vec4 col_hover{1.0F, 1.0F, 1.0F, 0.05F};
        glm::vec4 sel{0.19F, 0.31F, 0.44F, 0.98F};
        glm::vec4 text{0.86F, 0.88F, 0.93F, 1.0F};
        glm::vec4 header_text{0.85F, 0.87F, 0.92F, 1.0F};
        glm::vec4 sep{1.0F, 1.0F, 1.0F, 0.08F};
        glm::vec4 accent{0.45F, 0.72F, 1.0F, 0.95F};
        glm::vec4 bar_track{1.0F, 1.0F, 1.0F, 0.06F};
        glm::vec4 bar_thumb{0.55F, 0.72F, 0.95F, 0.55F};
        glm::vec4 bar_thumb_hot{0.62F, 0.80F, 1.0F, 0.85F};
    };
    // ── State：意图 ────────────────────────────────────────────────────────
    enum class DragMode : std::uint8_t
    {
        none,
        col_w,
        vthumb,
        hthumb,
        table_size,
        select,
        header,
    };
        struct Edit
        {
            int row{-1}, col{-1};
            CellRef ref{};
            std::string buf, orig;
            std::string compose;
            std::vector<std::string> undo, redo;
            int caret{0}, anchor{0};
            double last_push_t{-1.0e9};
            bool last_push_typing{false};
            [[nodiscard]] bool active() const noexcept
            {
                return row >= 0 && col >= 0;
            }
            [[nodiscard]] bool has_sel() const noexcept
            {
                return anchor != caret;
            }
            [[nodiscard]] int sel_lo() const noexcept
            {
                return std::min(anchor, caret);
            }
            [[nodiscard]] int sel_hi() const noexcept
            {
                return std::max(anchor, caret);
            }
        };

            struct Scroll
        {
            float x{0}, y{0};
            int axis{0};
            float base{0}, pos0{0};
            void reset() noexcept
            {
                x = y = 0;
                axis = 0;
                base = pos0 = 0;
            }
        };
        struct Sort
        {
            int col{-1}, dir{0};
            void toggle(int c) noexcept
            {
                if (col != c)
                {
                    col = c;
                    dir = 1;
                }
                else
                    dir = (dir + 1) % 3;
                if (dir == 0)
                    col = -1;
            }
            void reset() noexcept
            {
                col = -1;
                dir = 0;
            }
        };
        struct Filter
        {
            std::vector<std::string> text;
            int edit{-1};
            std::string input;
            std::string orig;    // 进入编辑时的筛选串：Esc 取消时还原
            std::string compose; // IME 预编辑（不参与过滤，只用来看）
            void reset(int cols)
            {
                text.assign(static_cast<std::size_t>(cols), {});
                edit = -1;
                input.clear();
                orig.clear();
                compose.clear();
            }
        };
        struct Selection
        {
            int cur_r{-1}, cur_c{-1};
            int r0{-1}, c0{-1}, r1{-1}, c1{-1};
            CellRef cur_cell{}, r0_cell{}, r1_cell{};
            [[nodiscard]] bool has_sel() const noexcept
            {
                return r0 >= 0;
            }
            void reset() noexcept
            {
                cur_r = cur_c = -1;
                r0 = c0 = r1 = c1 = -1;
                cur_cell = r0_cell = r1_cell = {};
            }
        };
        struct Checks
        {
            std::vector<std::uint8_t> on;
            std::vector<RowId> ids;
            int last{-1};
            void reset(int rows)
            {
                on.assign(static_cast<std::size_t>(rows), 0);
                ids.assign(static_cast<std::size_t>(rows), {});
                last = -1;
            }
        };
        struct Columns
        {
            std::vector<int> order;
            std::vector<float> w;
            std::vector<std::uint8_t> user;
            std::vector<std::uint8_t> hidden;
            int frozen{-1};
            float user_w{-1}, user_h{-1};
            int resize_col{-1};
            float resize_x0{0}, resize_base{0};
            int reorder_from{-1}, reorder_to{-1}, reorder_col{-1};
            float reorder_x0{0};

            void move(int from, int to) noexcept
            {
                if (from < 0 || to < 0 || from == to ||
                    from >= static_cast<int>(order.size()) ||
                    to >= static_cast<int>(order.size()))
                    return;
                const int val = order[static_cast<std::size_t>(from)];
                order.erase(order.begin() + from);
                order.insert(order.begin() + to, val);
            }

            void reset(const Table &d)
            {
                order.resize(static_cast<std::size_t>(d.cols_count()));
                std::iota(order.begin(), order.end(), 0);
                hidden.assign(static_cast<std::size_t>(d.cols_count()), 0);
                w.resize(static_cast<std::size_t>(d.cols_count()));
                user.assign(static_cast<std::size_t>(d.cols_count()), 0);
                for (int c = 0; c < d.cols_count(); ++c)
                {
                    w[static_cast<std::size_t>(c)] =
                        d.cols[static_cast<std::size_t>(c)].w;
                    user[static_cast<std::size_t>(c)] =
                        d.cols[static_cast<std::size_t>(c)].weight > 0 ? 0 : 1;
                }
                frozen = -1;
                user_w = user_h = -1;
                resize_col = -1;
                reorder_from = reorder_to = reorder_col = -1;
            }
        };
        struct History
        {
            struct undo_change
            {
                RowId id{};
                int col{0};
                std::string before, after;
            };
            std::vector<std::vector<undo_change>> undo, redo;
            void reset()
            {
                undo.clear();
                redo.clear();
            }
        };
        struct Find
        {
            bool mode{false};
            std::string buf;
            std::string compose; // IME 预编辑
            int hit{-1};
            void reset() noexcept
            {
                mode = false;
                buf.clear();
                compose.clear();
                hit = -1;
            }
        };
        struct Menu
        {
            bool open{false};
            int col{-1};
            float x{0}, y{0};
            int hover{-1};
            void reset() noexcept
            {
                open = false;
                col = -1;
                hover = -1;
            }
        };
        struct Tooltip
        {
            double hover_t{-1};
            void reset() noexcept
            {
                hover_t = -1;
            }
        };

    // ── 临时交互：窗口/帧级，掉帧可丢、不可序列化 ────────────────────────
    struct Interaction
    {
        Target hover{};
        int band_row{-1}, band_col{-1};
        glm::vec2 pointer{0, 0};
        bool left{false};
        double now{0};
        DragMode drag{DragMode::none};

        float drag_base{0}, drag_base2{0}, drag_start_x{0}, drag_start_y{0};
        Target drag_target{};
        float trs_x0{0}, trs_y0{0}, trs_w{0}, trs_h{0};
        Part trs_edge{Part::none};
        double last_click_t{-1.0e9};
        glm::vec2 last_click_pos{0, 0};
        int click_count{0};
        bool header_moved{false};

        void reset() noexcept
        {
            hover = {};
            band_row = band_col = -1;
            pointer = {0, 0};
            left = false;
            drag = DragMode::none;
            drag_target = {};

            drag_base = drag_base2 = drag_start_x = drag_start_y = 0;
            trs_edge = Part::none;
            last_click_t = -1.0e9;
            last_click_pos = {0, 0};
            click_count = 0;
            header_moved = false;
        }
    };

    // ── 持久意图：跨帧的用户意图，可 reset / reconcile / 序列化 / 撤销 ────
    struct State
    {
        Scroll scroll;
        Sort sort;
        Filter filter;
        Selection sel;
        Checks checks;
        Columns cols;
        History history;
        Find find;
        Menu menu;
        Tooltip tip;
        Edit edit;
        // 唯一“已变化”计数器：compose 把它写进 Frame::revision
        std::uint32_t revision{1};

        using undo_change = History::undo_change;

        void reset(const model::Table &d)
        {
            scroll.reset();
            sort.reset();
            filter.reset(d.cols_count());
            sel.reset();
            checks.reset(d.rows());
            cols.reset(d);
            history.reset();
            find.reset();
            menu.reset();
            tip.reset();
            edit = {};
        }
            void reconcile(const Table &d)
            {
                const auto nc = static_cast<std::size_t>(d.cols_count());
                const auto nr = static_cast<std::size_t>(d.rows());
                const auto ncols = static_cast<std::size_t>(d.cols_count());
                std::vector<int> old_order = cols.order;
                cols.order.clear();
                cols.order.reserve(ncols);
                for (const int c : old_order)
                    if (c >= 0 && static_cast<std::size_t>(c) < ncols &&
                        std::find(cols.order.begin(), cols.order.end(), c) ==
                            cols.order.end())
                        cols.order.push_back(c);
                for (std::size_t c = 0; c < ncols; ++c)
                    if (std::find(cols.order.begin(), cols.order.end(),
                                  static_cast<int>(c)) == cols.order.end())
                        cols.order.push_back(static_cast<int>(c));
                cols.w.resize(ncols);
                cols.user.resize(ncols);
                cols.hidden.resize(ncols);
                for (std::size_t c = 0; c < ncols; ++c)
                {
                    if (c >= d.cols.size())
                        continue;
                    const auto &def = d.cols[c];
                    if (cols.w[c] <= 0.0F)
                    {
                        cols.w[c] = def.w;
                        cols.user[c] = static_cast<std::uint8_t>(def.weight > 0 ? 0 : 1);
                    }
                }
                filter.text.resize(ncols);
                // 勾选按 RowId 重新对齐：中间插入/删除行时，勾选跟着行走
                {
                    const auto old_on = checks.on;
                    const auto old_ids = checks.ids;
                    checks.on.assign(nr, 0);
                    checks.ids.assign(nr, {});
                    for (std::size_t r = 0; r < nr; ++r)
                    {
                        if (r >= d.ids.size())
                            continue;
                        checks.ids[r] = d.ids[r];
                        for (std::size_t k = 0; k < old_ids.size(); ++k)
                            if (old_ids[k] == d.ids[r])
                            {
                                checks.on[r] = k < old_on.size() ? old_on[k] : 0;
                                break;
                            }
                    }
                }
                const auto old_cur = sel.cur_cell;
                const auto old_r0 = sel.r0_cell;
                const auto old_r1 = sel.r1_cell;
                const auto row_pos = [&](RowId id) {
                    if (!id.valid())
                        return -1;
                    for (std::size_t r = 0; r < d.ids.size(); ++r)
                        if (d.ids[r] == id)
                            return static_cast<int>(r);
                    return -1;
                };
                const auto col_pos = [&](ColumnId id) {
                    if (id.value == 0)
                        return -1;
                    for (std::size_t c = 0; c < d.cols.size(); ++c)
                        if (d.cols[c].id == id)
                            return static_cast<int>(c);
                    return -1;
                };
                const auto remap = [&](const CellRef &ref, int &r, int &c) {
                    const int rp = row_pos(ref.row);
                    const int cp = col_pos(ref.col);
                    if (rp >= 0 && cp >= 0)
                    {
                        r = rp;
                        c = cp;
                    }
                };
                remap(old_cur, sel.cur_r, sel.cur_c);
                remap(old_r0, sel.r0, sel.c0);
                remap(old_r1, sel.r1, sel.c1);
                const auto clamp_rc = [](int &r, int &c, int rows, int cols) {
                    r = rows <= 0 ? -1 : std::clamp(r < 0 ? 0 : r, 0, rows - 1);
                    c = cols <= 0 ? -1 : std::clamp(c < 0 ? 0 : c, 0, cols - 1);
                };
                const int rows_n = d.rows();
                const int cols_n = d.cols_count();
                if (rows_n > 0 && cols_n > 0)
                {
                    clamp_rc(sel.cur_r, sel.cur_c, rows_n, cols_n);
                    if (sel.r0 >= 0)
                    {
                        clamp_rc(sel.r0, sel.c0, rows_n, cols_n);
                        clamp_rc(sel.r1, sel.c1, rows_n, cols_n);
                    }
                    sel.cur_cell = d.cell_id(sel.cur_r, sel.cur_c);
                    sel.r0_cell = d.cell_id(sel.r0, sel.c0);
                    sel.r1_cell = d.cell_id(sel.r1, sel.c1);
                }
                else
                    sel.reset();
                if (edit.active() && (edit.row >= rows_n || edit.col >= cols_n))
                    edit = {};
            }
        void touch() noexcept
        {
            ++revision;
        }
    };
    // ── drag：与 Owner 同构的单一分派点 ──────────────────────────────────
    struct DragNone
    {
    };
    struct DragSelect
    {
    };
    struct DragColW
    {
    };
    struct DragHeader
    {
    };
    struct DragVThumb
    {
    };
    struct DragHThumb
    {
    };
    struct DragTableSize
    {
    };
    using DragRef = std::variant<DragNone, DragSelect, DragColW, DragHeader,
                                  DragVThumb, DragHThumb, DragTableSize>;

    [[nodiscard]] inline DragRef to_drag_ref(DragMode m) noexcept
    {
        switch (m)
        {
        case DragMode::select:
            return DragSelect{};
        case DragMode::col_w:
            return DragColW{};
        case DragMode::header:
            return DragHeader{};
        case DragMode::vthumb:
            return DragVThumb{};
        case DragMode::hthumb:
            return DragHThumb{};
        case DragMode::table_size:
            return DragTableSize{};
        default:
            return DragNone{};
        }
    }

    template <class... Fs>
    decltype(auto) visit_drag(DragMode m, Fs &&...fs)
    {
        return std::visit(overloaded{std::forward<Fs>(fs)...}, to_drag_ref(m));
    }
            inline void hover(State &v, Interaction &ix, const Target &t)
        {
            const Zone z = t.to_zone();
            if (!(z == ix.hover.to_zone()))
                v.tip.hover_t = ix.now;
            ix.hover = t;
            ix.band_row = ix.band_col = -1;
            if (z.kind == Part::cell || z.kind == Part::check)
            {
                ix.band_row = z.row;
                ix.band_col = z.col;
            }
            if (v.menu.open)
                v.menu.hover = z.kind == Part::menu_item ? z.col : -1;
            // hover 不推进版本：picking 回读依赖版本稳定
        }

        // ── 事件直调系统：wheel ───────────────────────────────────────────────
        inline void wheel(State &v, Interaction &ix, const Theme &p, float dx, float dy,
                  bool shift, const frame::Layout *g = nullptr)
        {
            if (g != nullptr && (ix.hover.kind == Part::hbar_track ||
                                 ix.hover.kind == Part::hbar_thumb))
                shift = true;
            if (shift)
                v.scroll.x -= dy * std::max(p.row_h * 2.0F, 10.0F);
            else
                v.scroll.y -= dy * (p.row_h + p.row_gap);
            v.touch();
        }
} // namespace table::intent
