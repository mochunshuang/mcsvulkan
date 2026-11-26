#pragma once
// ============================================================================
// table12/test/selftest.hpp — 行为 oracle（171 项断言）
//
//   逐条保留 ability11 的 171 项断言，只把调用方式换成新的门：
//     sync(...)        -> detail::compose_layout（等价于 session::compose 的 layout 段）
//     build_frame(...) -> detail::compose_draw
//     system_event(...) -> session::dispatch（同一条 feature 短路链）
//     system_text(...)  -> session::feed_text
//     hit_test(...)     -> detail::hit_target
//
//   目的：证明“结构重排不改行为”。任何一条变红，都说明重构改了语义。
// ============================================================================

#include "../session.hpp"
#include "view_facade.hpp"

namespace table::test
{
using command::ClearFilter;
using command::Command;
using command::Drag;
using command::Effect;
using command::Hover;
using command::Key;
using command::KeyPress;
using command::Outcome;
using command::Press;
using command::Release;
using command::ScrollBegin;
using command::SetCell;
using command::Wheel;
using command::WriteClipboard;
using detail::AppRouter;
using detail::ChecksFeature;
using detail::ClipboardFeature;
using detail::compose_draw;
using detail::compose_layout;
using detail::EditFeature;
using detail::FilterFeature;
using detail::FindFeature;
using detail::FreezeFeature;
using detail::MenuFeature;
using detail::ReorderFeature;
using detail::ResizeFeature;
using detail::ScrollFeature;
using detail::SelectionFeature;
using detail::SortFeature;
using detail::TableResizeFeature;
using detail::TooltipFeature;
using detail::UndoFeature;
using frame::DrawList;
using frame::Layout;
using frame::PaintContext;
using frame::RowPipeline;
using frame::Stream;
using intent::Interaction;
using intent::State;
using intent::Theme;
using model::Cell;
using model::Column;
using model::make_target;
using model::Table;
    inline int core_selftest()
    {
        int fails = 0;
        const bool verbose = std::getenv("TABLE_SELFTEST_VERBOSE") != nullptr;
        const auto check = [&](bool ok, const char *what) {
            if (!ok)
            {
                ++fails;
                std::println("[table-selftest] FAIL: {}", what);
            }
            else if (verbose)
                std::println("[table-selftest] ok: {}", what);
        };

        // 一个 Session = 一张表：真值 + 持久意图 + 临时交互 + 派生缓存
        session::Session S;
        model::Table &d = S.model;
        test::ViewFacade v{S.state, S.interaction};
        intent::Interaction &ix = S.interaction;
        intent::Theme &p = S.theme;
        Font &ft = S.font;
        detail::AppRouter &sy = S.router;
        frame::RowPipeline &pipe = S.pipe;
        frame::Layout &g = S.layout;
        {
            Column c;
            c.key = "name";
            c.title = "Name";
            c.w = 200;
            c.min_w = 100;
            c.max_w = 400;
            d.cols.push_back(c);
            c = {};
            c.key = "size";
            c.title = "Size";
            c.w = 120;
            d.cols.push_back(c);
            c = {};
            c.key = "kind";
            c.title = "Kind";
            c.w = 120;
            c.render = RenderKind::tag;
            d.cols.push_back(c);
        }
        d.finalize();
        check(d.column_of(0) == ColumnId{1} && d.column_of(2) == ColumnId{3},
              "column ids assigned");
        d.cells.resize(3);
        const char *names[] = {"beta.dat", "alpha.dat", "gamma.dat"};
        const char *sizes[] = {"20 KB", "10 KB", "30 KB"};
        const char *kinds[] = {"png", "gltf", "bin"};
        for (int i = 0; i < 3; ++i)
        {
            d.cells[0].push_back({names[i], {}});
            d.cells[1].push_back({sizes[i], {}});
            d.cells[2].push_back({kinds[i], {}});
            d.ids.push_back(RowId{static_cast<std::uint64_t>(i + 1)});
        }

        S.reset();
        ft.px = 15.0F;
        GlyphMetrics gm;
        gm.advance = 0.5F;
        for (int c = 32; c < 127; ++c)
            ft.put(static_cast<char32_t>(c), gm);

                std::vector<Effect> fx;
        // 和 host 完全相同的分发：命令 → dispatch（feature 短路链 → gesture）
        auto send = [&]<class E>(const E &c) {
            const auto out = session::dispatch(S, command::Command{c});
            fx = out.effects;
        };
        auto send_text = [&](std::string_view s) {
            const auto out = session::feed_text(S, s);
            fx = out.effects;
        };
        auto send_begin = [&](int r, int col) {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<EditFeature>().begin(tc, r, col);
        };
        // 公共门：一次 compose = layout passes + paint passes
        auto sync = [&](const Rect &vp_) {
            session::compose(S, vp_);
        };

const Rect vp{0, 0, 800, 600};
        sync(vp);
        check(g.col_count() == 3 && g.row_count() == 3, "sync geometry");
        check(g.header.w == g.body.w, "header/body width");

        send(Press{10, 10, {Part::cell, 1, 0}, false, false, false});
        check(v.sel.cur_r == 1 && v.sel.cur_c == 0, "press selects");
        check(v.sel.cur_cell.row == d.ids[1] && v.sel.cur_cell.col == d.column_of(0),
              "selection carries cell_ref");
        sync(vp);

        // G1：Shift+单击 = 从 anchor 扩选；点非格区域 = 清选区
        v.ui.last_click_t = -1.0e9;
        send(Press{10, 10, {Part::cell, 2, 1}, false, true, false});
        check(v.sel.r0 == 1 && v.sel.c0 == 0 && v.sel.r1 == 2 && v.sel.c1 == 1,
              "shift+click extends from anchor");
        send(Press{10, 10, {Part::none}, false, false, false});
        check(v.sel.r0 < 0, "click on non-cell area clears selection");
        send(Release{10, 10});
        v.ui.last_click_t = -1.0e9;

        // G3：点分组带 = 选中整组（用一张带 group 的独立小表）
        {
            // 第二张表 = 第二个 Session：同一套门（compose / hit / dispatch）
            session::Session S2;
            Table &dg = S2.model;
            Column gc;
            for (int i = 0; i < 4; ++i)
            {
                gc = {};
                gc.key = std::format("c{}", i);
                gc.title = gc.key;
                gc.w = 120;
                gc.group = i < 2 ? "G1" : "G2";
                dg.cols.push_back(gc);
            }
            dg.finalize();
            dg.cells.resize(4);
            for (int i = 0; i < 4; ++i)
            {
                dg.cells[static_cast<std::size_t>(i)].push_back({"v", {}});
                dg.cells[static_cast<std::size_t>(i)].push_back({"w", {}});
            }
            dg.ids.push_back(RowId{1});
            dg.ids.push_back(RowId{2});
            S2.theme = p;
            S2.font = ft;
            S2.reset();
            session::compose(S2, vp);
            State &vg = S2.state;
            const Layout &gg = S2.layout;
            const auto gz = session::hit(S2, gg.col_x_at(1) + 5.0F, gg.group.y + 5.0F);
            check(gz.to_zone().kind == Part::header_group && gz.to_zone().col >= 0,
                  "group hit test carries first column");
            {
                const Press press{gg.col_x_at(1) + 5.0F, gg.group.y + 5.0F,
                                  {Part::header_group, -1, gz.to_zone().col}, false,
                                  false, false};
                session::dispatch(S2, Command{press});
            }
            check(vg.sel.c0 == 0 && vg.sel.c1 == 1 && vg.sel.r0 == 0 &&
                      vg.sel.r1 == dg.rows() - 1,
                  "group header click selects whole group");
        }

        // G5：键盘导航把活动格滚进视口（短视口）
        {
            sync({0, 0, 800, 120});
            v.scroll.y = 0;
            v.ui.last_click_t = -1.0e9;
            send(Press{g.scrollv.x + 20,
                           g.row_y.empty() ? g.body.y + 5 : g.row_y.front() + 5,
                           {Part::cell, g.view_rows.front(), 0},
                           false,
                           false,
                           false});
            for (int i = 0; i < 4; ++i)
                send(KeyPress{Key::down, false, false});
            check(v.sel.cur_r == g.view_rows.back(), "keyboard down reaches last row");
            check(v.scroll.y > 0.0F, "keyboard navigation auto-scrolls");
            sync({0, 0, 800, 120});
            check(g.row_index.back() == v.sel.cur_r,
                  "active row is inside the viewport after scroll");
            send(Release{g.scrollv.x + 20, g.body.y + 5});
            sync(vp);
            v.scroll.y = 0;
        }

        DrawList f;
        detail::compose_draw(d, v, ix, p, g, ft, sy, f, TableId{7});
        check(f.draw.op_count() > 0, "frame has ops");
        check(!f.zones.empty(), "zones");
        {
            DrawList fo;
            detail::compose_draw(d, v, ix, p, g, ft, sy, fo, TableId{7});
            bool owned = false;
            for (const auto &ss : fo.draw.shapes)
                for (const auto &o : ss)
                    if (o.key != 0 && std::holds_alternative<cell_owner>(o.who))
                    {
                        const auto &co = std::get<cell_owner>(o.who);
                        owned = co.table == TableId{7};
                    }
            check(owned, "DL op carries typed owner");
        }
        detail::compose_draw(d, v, ix, p, g, ft, sy, f, TableId{7});
        detail::compose_draw(d, v, ix, p, g, ft, sy, f, TableId{7});
        check(f.draw.op_count() > 0, "frame reusable (arena reset)");

        fx.clear();
        v.ui.last_click_t = -1.0e9;
        send(Press{10, 10, {Part::cell, 1, 0}, false, false, false});
        sync(vp);
        send(KeyPress{Key::f2, false, false});
        check(v.edit.active(), "F2 begins edit");
        send_text("X");
        send(KeyPress{Key::enter, false, false});
        check(!v.edit.active(), "enter commits");
        bool has = false;
        for (const auto &e : fx)
            if (const auto *q = std::get_if<SetCell>(&e))
            {
                has = true;
                check(q->id == RowId{2}, "effect id");
            }
        check(has, "commit effect");

        fx.clear();
        send(Press{10, 70, {Part::header, -1, 0}, false, false, false});
        send(Release{10, 70});
        sync(vp);
        check(!g.view_rows.empty() && d.text(g.view_rows.front(), 0) == "alpha.dat",
              "sort ascending");
        check(d.ids[static_cast<std::size_t>(g.view_rows.front())] == RowId{2},
              "row_id stable under sort");
        // 三态：再点两次 = 降序、取消
        send(Press{10, 70, {Part::header, -1, 0}, false, false, false});
        send(Release{10, 70});
        sync(vp);
        check(v.sort.dir == 2 && !g.view_rows.empty() &&
                  d.text(g.view_rows.front(), 0) == "gamma.dat",
              "sort: second click descending");
        send(Press{10, 70, {Part::header, -1, 0}, false, false, false});
        send(Release{10, 70});
        sync(vp);
        check(v.sort.dir == 0 && !g.view_rows.empty() &&
                  d.text(g.view_rows.front(), 0) == "beta.dat",
              "sort: third click restores original order");

        // G6：模型增删 + 身份稳定 + State 增量对齐（不是整份 reset）
        {
            const auto keep_col_id = d.cols[0].id;
            d.finalize();
            check(d.cols[0].id == keep_col_id, "finalize keeps existing column ids");
            const auto id_new = d.insert_row(1);
            check(d.rows() == 4 && d.ids[1].v == id_new.v,
                  "insert row gives a fresh stable id");
            check(d.ids[2] == RowId{2} && d.ids[3] == RowId{3},
                  "insert row keeps neighbour ids");
            sync(vp);
            check(g.row_count() == 4 && v.checks.on.size() == 4,
                  "sync reconciles a new row without reset");
            check(v.sel.cur_r >= 0 && v.sel.cur_r < 4,
                  "selection survives a model change");
            check(d.remove_row(1) && d.rows() == 3 && d.ids[1] == RowId{2},
                  "remove row keeps neighbour ids");
            sync(vp);
            check(g.row_count() == 3, "sync reconciles a removed row");
            Column extra;
            extra.key = "extra";
            extra.title = "Extra";
            extra.w = 80;
            const auto cid = d.insert_column(1, extra);
            sync(vp);
            check(d.cols_count() == 4 && g.col_count() == 4, "insert column reconciled");
            check(d.column_of(1) == cid && d.column_of(0) == keep_col_id &&
                      d.column_of(2) == ColumnId{2},
                  "insert column keeps existing column ids");
            check(d.remove_column(1) && d.cols_count() == 3, "remove column");
            sync(vp);
            check(g.col_count() == 3, "sync reconciles a removed column");
        }

        sync({0, 0, 800, 120});
        intent::wheel(v, ix, p, 0.0F, -3.0F, false);
        sync({0, 0, 800, 120});
        check(v.scroll.y > 0.0F, "wheel scrolls");

        // 9) 列布局三件套
        sync(vp);
        const float w0 = v.cols.w[0];
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<ResizeFeature>().begin(tc, 0, 0.0F, w0);
        }
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<ResizeFeature>().move(tc, 300.0F);
        }
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<ResizeFeature>().end(tc);
        }
        check(v.cols.w[0] > w0, "resize feature writes col_w");
        sync(vp);
        {
            const int pos = g.pos_of_col(0);
            check(pos >= 0 && std::abs(g.col_w[static_cast<std::size_t>(pos)] -
                                       v.cols.w[0]) < 0.5F,
                  "resize reaches geometry");
        }

        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<ReorderFeature>().begin(tc, 0, 0.0F);
        }
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<ReorderFeature>().move(tc, 450.0F);
        }
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<ReorderFeature>().end(tc);
        }
        check(v.cols.order[0] != 0, "reorder feature writes order");

        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<FreezeFeature>().set(tc, 2);
        }
        check(v.cols.frozen == 2, "freeze feature writes frozen");

        // 10) checks / clipboard / undo / table resize
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<ChecksFeature>().toggle(tc, 0, false);
        }
        check(v.checks.on[0] == 1 && v.checks.ids[0] == d.ids[0],
              "checks carries row_id");
        v.checks.reset(d.rows());
        v.ui.last_click_t = -1.0e9;
        send(Press{10,
                       10,
                       {Part::check, 0, -1},
                       false,
                       false,
                       false,
                       make_target(Part::check, 0, -1)});
        send(Press{10,
                       10,
                       {Part::check, 2, -1},
                       false,
                       true,
                       false,
                       make_target(Part::check, 2, -1)});
        {
            int on = 0;
            for (const auto x : v.checks.on)
                on += x != 0;
            check(on == d.rows(), "checks: shift range selects visible range");
        }
        send(Press{10,
                       10,
                       {Part::check_head, -1, -1},
                       false,
                       false,
                       false,
                       make_target(Part::check_head, -1, -1)});
        {
            int on = 0;
            for (const auto x : v.checks.on)
                on += x != 0;
            check(on == 0, "checks: header clears all");
        }
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<SelectionFeature>().set(tc, 1, 0);
        }
        fx.clear();
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<ClipboardFeature>().copy(tc, false);
        }
        bool clip = false;
        for (const auto &e : fx)
            clip = clip || std::holds_alternative<WriteClipboard>(e);
        check(clip, "clipboard feature");
        fx.clear();
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<ClipboardFeature>().remove(tc);
        }
        bool del = false;
        for (const auto &e : fx)
            del = del || std::holds_alternative<SetCell>(e);
        check(del, "delete feature");
        fx.clear();
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<UndoFeature>().run(tc, false);
        }
        check(!fx.empty(), "undo feature");
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<TableResizeFeature>().begin(tc, 0, 0, 800, 600);
        }
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<TableResizeFeature>().move(tc, 120, 120);
        }
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<TableResizeFeature>().end(tc);
        }
        check(v.cols.user_w > 0, "table resize feature");

        // 11) 恢复项：筛选 UI / 查找 / 列菜单 / 粘贴 / 编辑撤销
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<FilterFeature>().begin(tc, 0);
        }
        send_text("alpha");
        send(KeyPress{Key::enter, false, false});
        sync(vp);
        {
            bool all = true;
            for (const int r : g.view_rows)
                all = all && FilterFeature::contains(d.text(r, 0), "alpha");
            check(!g.view_rows.empty() && all, "filter UI applies");
        }
        {
            // 大小写不敏感 + 两列 AND
            v.filter.text[0] = "ALPHA";
            sync(vp);
            check(!g.view_rows.empty() &&
                      FilterFeature::contains(d.text(g.view_rows.front(), 0), "ALPHA"),
                  "filter: case-insensitive");
            v.filter.text[1] = "10 KB";
            sync(vp);
            bool both = !g.view_rows.empty();
            for (const int r : g.view_rows)
                both = both && FilterFeature::contains(d.text(r, 0), "alpha") &&
                       FilterFeature::contains(d.text(r, 1), "10 KB");
            check(both, "filter: two columns AND");
            v.filter.text[0].clear();
            v.filter.text[1].clear();
            sync(vp);
        }
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<FilterFeature>().clear(tc, 0);
        }
        sync(vp);

        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<FindFeature>().toggle(tc);
        }
        send_text("alpha");
        check(v.find.mode && v.find.hit >= 0, "find locates rows");
        {
            // 命中在视口外：Enter 必须把命中行滚进来
            sync({0, 0, 800, 120});
            v.scroll.y = 0;
            {
                command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
                sy.get<FindFeature>().close(tc);
                sy.get<FindFeature>().toggle(tc);
            }
            send_text("gamma");
            send(KeyPress{Key::enter, false, false});
            check(v.find.hit == 2, "find: hit is the off-screen row");
            check(v.scroll.y > 0.0F, "find: enter scrolls to the hit");
            sync({0, 0, 800, 120});
            check(std::find(g.row_index.begin(), g.row_index.end(), v.find.hit) !=
                      g.row_index.end(),
                  "find: hit row is visible after scroll");
            sync(vp);
            v.scroll.y = 0;
        }
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<FindFeature>().close(tc);
        }

        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<MenuFeature>().open(tc, 0, 10.0F, 10.0F);
        }
        check(v.menu.open, "context menu opens");
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<MenuFeature>().item(tc, 2);
        }
        check(v.cols.hidden[0] == 1, "hide column");
        sync(vp);
        check(g.col_count() == 2, "hidden column excluded from geometry");
        {
            // 键盘左右只走可见列：隐藏 col0 后从 col1 往右到 col2，再往右停住
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<SelectionFeature>().set(tc, g.view_rows.front(), g.col_index.front());
        }
        send(KeyPress{Key::right, false, false});
        sync(vp);
        check(v.sel.cur_c == g.col_index.back(),
              "hidden: keyboard moves over visible columns");
        send(KeyPress{Key::right, false, false});
        sync(vp);
        check(v.sel.cur_c == g.col_index.back(),
              "hidden: keyboard stops at last visible");
        check(v.sel.cur_c != 0, "hidden: never lands on hidden column");
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<MenuFeature>().open(tc, 1, 10.0F, 10.0F);
        }
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<MenuFeature>().item(tc, 3);
        }
        check(v.cols.hidden[0] == 0, "show all restores columns");
        sync(vp);
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<MenuFeature>().open(tc, 0, 10.0F, 10.0F);
        }
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<MenuFeature>().item(tc, 0);
        }
        check(v.cols.w[0] > 0.0F, "autofit writes width");
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<MenuFeature>().open(tc, 1, 10.0F, 10.0F);
        }
        send(Press{10, 10, {Part::menu_item, -1, 3}, false, false, false});
        check(!v.menu.open && v.cols.hidden[0] == 0, "menu item click path");

        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<SelectionFeature>().set(tc, 0, 0);
        }
        fx.clear();
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<EditFeature>().paste(tc, "P1\tP2");
        }
        check(!fx.empty(), "paste emits cells");

        send_begin(0, 0);
        send_text("X");
        const std::string before_undo = v.edit.buf;
        send(KeyPress{Key::z, false, true});
        check(v.edit.buf != before_undo, "edit text undo");
        send(KeyPress{Key::esc, false, false});

        // 13) 编辑选文本 / 提交后撤销 / 滚动条拖 / 整表缩放 / 光标
        send_begin(0, 0);
        send(KeyPress{Key::end, false, false});
        send(KeyPress{Key::left, true, false});
        send(KeyPress{Key::left, true, false});
        check(v.edit.has_sel(), "edit shift selection");
        send(KeyPress{Key::esc, false, false});

        send_begin(0, 0);
        send_text("Q");
        send(KeyPress{Key::enter, false, false});
        check(!v.edit.active(), "commit before undo");
        fx.clear();
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<UndoFeature>().run(tc, false);
        }
        check(!fx.empty(), "ctrl+z after commit");

        intent::wheel(v, ix, p, 0.0F, -5.0F, false);
        sync({0, 0, 800, 120});
        const float sy0 = v.scroll.y;
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<ScrollFeature>().begin(tc, 0, sy0, 0.0F);
        }
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<ScrollFeature>().move(tc, -50.0F);
        }
        {
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<ScrollFeature>().end(tc);
        }
        check(sy0 > 0.0F && v.scroll.y < sy0, "scrollbar thumb drag");

        const float uw0 = v.cols.user_w;
        send(Press{799, 119, {Part::edge_se}, false, false, false});
        send(Drag{849, 169});
        send(Release{849, 169});
        check(v.cols.user_w > uw0, "table resize drag");
        check(cursor_for({Part::edge_se}) == Cursor::resize_nwse,
              "resize cursor mapping");
        {
            detail::compose_draw(d, v, ix, p, g, ft, sy, f, TableId{7});
            bool thumb = false, sep = false, edge = false;
            for (const auto &zr : f.zones)
            {
                thumb = thumb || zr.z.kind == Part::vbar_thumb;
                sep = sep || zr.z.kind == Part::col_sep;
                edge = edge || zr.z.kind == Part::edge_se;
            }
            check(thumb && sep && edge, "frame zones carry bars/edges");
        }
        {
            const auto ez =
                detail::hit_target(d, g, v, g.edge_se.x + 5.0F, g.edge_se.y + 5.0F)
                    .to_zone();
            check(ez.kind == Part::edge_se, "edge hit test");
            v.menu.open = true;
            v.menu.x = 100.0F;
            v.menu.y = 100.0F;
            const auto mz = detail::hit_target(d, g, v, 175.0F, 140.0F).to_zone();
            check(mz.kind == Part::menu_item && mz.col == 1, "menu hit test");
            v.menu.open = false;
            const int dc0 = g.col_index[0];
            const auto fz = detail::hit_target(d, g, v, g.col_x_at(0) + g.col_w[0] - 9.0F, g.col_header.y + g.col_header.h * 0.5F)
                                .to_zone();
            check(fz.kind == Part::filter_btn && fz.col == dc0, "filter button hit test");
        }

        // G2：拖拽框选出体区 = 自动滚动 + 目标夹到末行
        {
            sync({0, 0, 800, 120});
            v.scroll.y = 0;
            v.ui.last_click_t = -1.0e9;
            send(Press{g.scrollv.x + 20,
                           g.row_y.front() + 5,
                           {Part::cell, g.view_rows.front(), 0},
                           false,
                           false,
                           false});
            send(Drag{g.scrollv.x + 20, g.body.y1() + 20});
            check(v.scroll.y > 0.0F, "drag past bottom edge auto-scrolls");
            check(v.sel.r1 == g.view_rows.back(), "drag extends to last row");
            send(Release{g.scrollv.x + 20, g.body.y1() + 20});
            sync(vp);
            v.scroll.y = 0;
        }

        // G4：编辑框词级操作（双击选词 / 三击全选 / Ctrl+← / Ctrl+Backspace）
        {
            const std::string keep = d.cells[0][0].text;
            d.cells[0][0].text = "alpha beta gamma";
            sync(vp);
            send_begin(0, 0);
            send(KeyPress{Key::end, false, false});
            send(KeyPress{Key::left, false, true});
            check(v.edit.caret == 11, "edit: Ctrl+Left jumps to word start");
            send(KeyPress{Key::backspace, false, true});
            check(v.edit.buf == "alpha gamma", "edit: Ctrl+Backspace deletes a word");
            send(KeyPress{Key::esc, false, false});
            {
                const float origin =
                    g.col_x_at(g.pos_of_col(0)) + p.cell_pad + text_width(ft, "alpha be");
                v.ui.last_click_t = -1.0e9;
                send(Press{origin,
                               g.row_y.front() + 5,
                               {Part::cell, 0, 0},
                               false,
                               false,
                               false});
                send(Press{origin,
                               g.row_y.front() + 5,
                               {Part::cell, 0, 0},
                               false,
                               false,
                               false});
                check(v.edit.active() && v.edit.sel_lo() == 6 && v.edit.sel_hi() == 10,
                      "edit: double-click selects the word");
                send(Press{origin,
                               g.row_y.front() + 5,
                               {Part::cell, 0, 0},
                               false,
                               false,
                               false});
                check(v.edit.sel_lo() == 0 &&
                          v.edit.sel_hi() == static_cast<int>(v.edit.buf.size()),
                      "edit: triple-click selects all");
            }
            send(KeyPress{Key::esc, false, false});
            send(Release{0, 0});
            d.cells[0][0].text = keep;
            sync(vp);
        }

        // 13) 筛选实时生效 / 查找点别处关闭 / 双击分隔条自适应
        {
            const std::string keep = d.cells[0][0].text;
            d.cells[0][0].text = "livefilter";
            v.ui.last_click_t = -1.0e9;
            send(Press{10,
                           10,
                           {Part::filter_btn, -1, 0},
                           false,
                           false,
                           false,
                           make_target(Part::filter_btn, -1, 0)});
            send_text("livef");
            sync(vp);
            check(v.filter.edit == 0 && g.view_rows.size() == 1 &&
                      d.text(g.view_rows.front(), 0) == "livefilter",
                  "filter: live while typing");
            send(KeyPress{Key::enter, false, false});
            check(v.filter.edit < 0 && v.filter.text[0] == "livef",
                  "filter: enter keeps filter");
            v.ui.last_click_t = -1.0e9;
            send(Press{10,
                           10,
                           {Part::filter_btn, -1, 0},
                           false,
                           false,
                           false,
                           make_target(Part::filter_btn, -1, 0)});
            send(KeyPress{Key::del, false, false});
            sync(vp);
            check(v.filter.text[0].empty() && g.view_rows.size() == 3,
                  "filter: Delete clears live");
            send(KeyPress{Key::enter, false, false});
            d.cells[0][0].text = keep;
            sync(vp);
        }
        {
            send(KeyPress{Key::f, false, true});
            check(v.find.mode, "find: Ctrl+F opens");
            v.ui.last_click_t = -1.0e9;
            send(Press{10,
                           10,
                           {Part::cell, 1, 0},
                           false,
                           false,
                           false,
                           make_target(Part::cell, 1, 0)});
            check(!v.find.mode, "find: click elsewhere closes");
        }
        {
            v.ui.last_click_t = -1.0e9;
            v.cols.user[0] = 0;
            v.cols.w[0] = 60; // 明显偏离自适应宽度，双击后应被改回来
            const float before = v.cols.w[0];
            send(Press{10,
                           10,
                           {Part::col_sep, -1, 0},
                           false,
                           false,
                           false,
                           make_target(Part::col_sep, -1, 0)});
            send(Release{10, 10});
            send(Press{10,
                           10,
                           {Part::col_sep, -1, 0},
                           false,
                           false,
                           false,
                           make_target(Part::col_sep, -1, 0)});
            send(Release{10, 10});
            check(v.cols.user[0] == 1 && v.cols.w[0] != before,
                  "resize: double-click separator autofits");
            sync(vp);
        }

        // 14) wrap：行高按行数增长，且 build_frame 真的画出多行
        {
            const std::string keep = d.cells[0][1].text;
            d.cells[0][1].text = "alpha beta gamma delta epsilon zeta";
            d.cols[0].wrap = true;
            sync({0, 0, 800, 600});
            check(g.row_h_data[1] > p.row_h, "wrap: row height grows");
            DrawList fw;
            detail::compose_draw(d, v, ix, p, g, ft, sy, fw, TableId{});
            const auto wrap_zone = zone_key({Part::cell, 1, 0});
            int lines = 0;
            for (const auto &t : fw.draw.texts[static_cast<std::size_t>(Stream::content)])
                lines += t.key == wrap_zone ? 1 : 0;
            check(lines > 1, "wrap: multiple text lines emitted");
            d.cols[0].wrap = false;
            d.cells[0][1].text = keep;
            sync(vp);
        }

        // 15) 滚动条轨道翻页 / 上边把手缩放（方向要正确）
        {
            sync({0, 0, 800, 120});
            v.scroll.y = 0;
            v.ui.last_click_t = -1.0e9;
            send(Press{g.vtrack.x + 2,
                           g.vtrack.y1() - 2,
                           {Part::vbar_track, -1, -1},
                           false,
                           false,
                           false,
                           make_target(Part::vbar_track, -1, -1)});
            check(v.scroll.y > 0.0F, "scroll: track click pages down");
            send(Release{g.vtrack.x + 2, g.vtrack.y1() - 2});

            sync(vp);
            v.cols.user_w = 500;
            v.cols.user_h = 300;
            sync(vp);
            const float h0 = g.box.h;
            v.ui.last_click_t = -1.0e9;
            const Rect en = g.edge_n;
            send(Press{en.x + en.w * 0.5F,
                           en.y + en.h * 0.5F,
                           {Part::edge_n},
                           false,
                           false,
                           false,
                           make_target(Part::edge_n, -1, -1)});
            send(Drag{en.x + en.w * 0.5F, en.y + en.h * 0.5F - 40.0F});
            send(Release{en.x + en.w * 0.5F, en.y + en.h * 0.5F - 40.0F});
            sync(vp);
            check(v.cols.user_h > h0, "table resize: top handle grows height");
            v.cols.user_h = -1;
            v.cols.user_w = -1;
            sync(vp);
        }

        // 16) undo 合并：同一段连续输入只占一步
        {
            send_begin(0, 0);
            const std::string keep = v.edit.buf;
            send_text("abc");
            check(v.edit.undo.size() == 1, "undo: typing burst coalesced");
            send(KeyPress{Key::z, false, true});
            check(v.edit.buf == keep, "undo: coalesced burst undone in one step");
            send(KeyPress{Key::esc, false, false});
        }

        // 12) tooltip：悬停 0.6s 后才多出图元
        sync(vp);
        v.ui.hover = make_target(Part::cell, 0, 0);
        v.tip.hover_t = 0.0;
        v.ui.now = 0.0;
        DrawList t0, t1;
        detail::compose_draw(d, v, ix, p, g, ft, sy, t0);
        v.ui.now = 1.0;
        detail::compose_draw(d, v, ix, p, g, ft, sy, t1);
        check(t1.draw.op_count() > t0.draw.op_count(), "tooltip shows after dwell");

        // 13) Zone/Owner/Cursor 全 Part 往返，几何不变量
        {
            const std::array<std::tuple<Part, int, int>, 19> parts{{
                {Part::cell, 1, 0},          {Part::check, 1, -1},
                {Part::check_head, -1, -1},  {Part::header, -1, 0},
                {Part::col_sep, -1, 0},      {Part::filter_btn, -1, 0},
                {Part::header_group, -1, 0}, {Part::menu_item, -1, 1},
                {Part::frame, -1, -1},       {Part::vbar_track, -1, -1},
                {Part::vbar_thumb, -1, -1},  {Part::hbar_track, -1, -1},
                {Part::hbar_thumb, -1, -1},  {Part::edge_n, -1, -1},
                {Part::edge_w, -1, -1},      {Part::edge_ne, -1, -1},
                {Part::edge_nw, -1, -1},     {Part::edge_se, -1, -1},
                {Part::edge_sw, -1, -1},
            }};
            for (const auto &[k, r, c] : parts)
            {
                const Zone z{k, r, c, TableId{3}};
                const Zone back = zone_from(zone_key(z));
                check(back.kind == k && back.row == r && back.col == c &&
                          back.table == z.table,
                      std::format("zone roundtrip {}", part_name(k)).c_str());
            }
            const std::array<std::pair<Part, Cursor>, 12> cursors{{
                {Part::header, Cursor::hand},
                {Part::header_group, Cursor::hand},
                {Part::check, Cursor::hand},
                {Part::check_head, Cursor::hand},
                {Part::vbar_thumb, Cursor::hand},
                {Part::hbar_thumb, Cursor::hand},
                {Part::col_sep, Cursor::resize_ew},
                {Part::edge_n, Cursor::resize_ns},
                {Part::edge_w, Cursor::resize_ew},
                {Part::edge_ne, Cursor::resize_nesw},
                {Part::edge_sw, Cursor::resize_nesw},
                {Part::edge_nw, Cursor::resize_nwse},
            }};
            for (const auto &[k, want] : cursors)
                check(cursor_for({k}) == want,
                      std::format("cursor {}", part_name(k)).c_str());
        }
        {
            bool xs = true, ys = true, ws = true, hs = true;
            for (int i = 1; i < g.col_count(); ++i)
                xs = xs && g.col_x_at(i) >= g.col_x_at(i - 1);
            for (int i = 1; i < g.row_count(); ++i)
                ys = ys && g.row_y[static_cast<std::size_t>(i)] >=
                               g.row_y[static_cast<std::size_t>(i - 1)];
            for (int i = 0; i < g.col_count(); ++i)
                ws = ws && g.col_w[static_cast<std::size_t>(i)] >=
                               d.cols[static_cast<std::size_t>(
                                          g.col_index[static_cast<std::size_t>(i)])]
                                   .min_w;
            for (int i = 0; i < g.row_count(); ++i)
                hs = hs && g.row_h[static_cast<std::size_t>(i)] >= p.row_h;
            check(xs, "geometry: column x monotonic");
            check(ys, "geometry: row y monotonic");
            check(ws, "geometry: column width >= min");
            check(hs, "geometry: row height >= row_h");
            check(g.col_count() == 3 && g.row_count() == 3, "geometry: 3x3 visible");
            check(g.body.w > 0 && g.body.h > 0, "geometry: body non-empty");
        }

        // 14) 键盘 End / Ctrl+Home、reorder 拖拽、ne 缩放、横条拖拽
        {
            v.edit = {};
            v.filter.edit = -1;
            v.find.mode = false;
            v.menu.open = false;
            v.cols.hidden.assign(d.cols_count(), 0);
            v.cols.order = {0, 1, 2};
            sync(vp);
            v.sel.reset();
            v.ui.last_click_t = -1.0e9;
            send(Press{
                10,
                10,
                {Part::cell, g.view_rows.front(), g.col_index.front()},
                false,
                false,
                false,
                make_target(Part::cell, g.view_rows.front(), g.col_index.front())});
            const int col0 = v.sel.cur_c;
            send(KeyPress{Key::right, false, false});
            check(v.sel.cur_c != col0, "keyboard: right moves column");
            send(KeyPress{Key::left, false, false});
            check(v.sel.cur_c == col0, "keyboard: left moves column");
            const float hx = g.col_x_at(0) + g.col_w[0] * 0.5F;
            const float hy = g.col_header.y + g.col_header.h * 0.5F;
            const auto ht = make_target(Part::header, -1, 0);
            send(Press{hx, hy, {Part::header, -1, 0}, false, false, false, ht});
            send(Drag{g.col_x_at(1) + 20.0F, hy});
            send(Release{g.col_x_at(1) + 20.0F, hy});
            sync(vp);
            check(v.cols.order[0] != 0, "reorder drag: order changed");
            check(v.cols.order[0] == 1 && v.cols.order[1] == 0,
                  "reorder drag: col0 moved to col1");
            check(d.cols[0].id.value != 0, "reorder drag: column id stable");
            v.cols.order = {0, 1, 2};
            sync(vp);

            v.cols.user_w = 500;
            v.cols.user_h = 300;
            sync(vp);
            const auto ne = g.edge_ne;
            send(Press{ne.x + ne.w * 0.5F,
                           ne.y + ne.h * 0.5F,
                           {Part::edge_ne},
                           false,
                           false,
                           false,
                           make_target(Part::edge_ne, -1, -1)});
            send(Drag{ne.x + ne.w * 0.5F - 40.0F, ne.y + ne.h * 0.5F - 30.0F});
            send(Release{ne.x + ne.w * 0.5F - 40.0F, ne.y + ne.h * 0.5F - 30.0F});
            sync(vp);
            check(v.cols.user_w < 500, "table resize ne: width shrinks from left");
            check(v.cols.user_h > 300, "table resize ne: height grows upward");
            v.cols.user_w = -1;
            v.cols.user_h = -1;
            sync(vp);

            const auto save_w = v.cols.w;
            v.cols.w = {400, 400, 400};
            sync(vp);
            check(g.hbar, "hbar appears when columns overflow");
            const float sx0 = v.scroll.x;
            const auto hb = g.hthumb;
            send(Press{hb.x + hb.w * 0.5F,
                           hb.y + hb.h * 0.5F,
                           {Part::hbar_thumb},
                           false,
                           false,
                           false,
                           make_target(Part::hbar_thumb, -1, -1)});
            send(Drag{hb.x + hb.w * 0.5F + 60.0F, hb.y + hb.h * 0.5F});
            send(Release{hb.x + hb.w * 0.5F + 60.0F, hb.y + hb.h * 0.5F});
            sync(vp);
            check(v.scroll.x > sx0, "hbar thumb drag scrolls x");
            v.cols.w = save_w;
            v.scroll.x = 0;
            sync(vp);
        }

        // 15) 编辑写回、undo/redo、勾选三态、wrap 换行、动态行
        {
            fx.clear();
            send_begin(0, 0);
            const auto before_edit = d.text(0, 0);
            send_text("Q");
            send(KeyPress{Key::enter, false, false});
            check(!v.edit.active(), "edit: Enter closes editor");
            bool wrote = false;
            for (auto &e : fx)
                if (auto *q = std::get_if<SetCell>(&e))
                {
                    wrote = true;
                    d.set_cell(d.index_of(q->id), q->col, q->text);
                }
            check(wrote && d.text(0, 0).find('Q') != std::string::npos,
                  "edit: commit writes data");
            (void)before_edit;
            fx.clear();

            send_begin(0, 1);
            const auto keep2 = v.edit.buf;
            send_text("xy");
            send(KeyPress{Key::z, false, true});
            check(v.edit.buf == keep2, "undo: single step restores");
            send(KeyPress{Key::y, false, true});
            check(v.edit.buf.ends_with("xy"), "redo: restores edit text");
            send(KeyPress{Key::esc, false, false});
            check(!v.edit.active(), "edit: Esc cancels");

            v.checks.reset(d.rows());
            check(v.checks.on.size() == static_cast<std::size_t>(d.rows()),
                  "checks: array grows with rows");
            v.checks.on[0] = 1;
            command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
            sy.get<ChecksFeature>().toggle_all(tc);
            int on = 0;
            for (auto x : v.checks.on)
                on += x != 0;
            check(on == d.rows(), "checks: toggle_all from partial -> all");
            sy.get<ChecksFeature>().toggle_all(tc);
            on = 0;
            for (auto x : v.checks.on)
                on += x != 0;
            check(on == 0, "checks: toggle_all from all -> none");

            d.set_cell(0, 0, "a\nb");
            p.wrap = true;
            v.cols.user[0] = 0;
            sync(vp);
            check(g.row_h_data[0] > p.row_h, "wrap: explicit newline grows row");
            p.wrap = false;
            sync(vp);
            check(g.row_h_data[0] <= p.row_h, "wrap: toggle off restores row height");

            const auto n0 = d.rows();
            const auto id0 = d.insert_row(1);
            const auto id1 = d.insert_row(2);
            d.finalize();
            v.reset(d);
            sync(vp);
            check(d.rows() == n0 + 2, "dynamic: insert two rows");
            check(id0 != id1 && d.ids[1] != d.ids[2], "dynamic: fresh row ids");
            check(v.checks.on.size() == static_cast<std::size_t>(d.rows()),
                  "dynamic: checks size follows insert");
            check(d.remove_row(2) && d.remove_row(1), "dynamic: remove two rows");
            d.finalize();
            v.reset(d);
            sync(vp);

            bool cells_hit = true;
            for (int ri = 0; ri < g.row_count(); ++ri)
                for (int ci = 0; ci < g.col_count(); ++ci)
                {
                    const auto t =
                        detail::hit_target(d, g, v, g.col_x_at(ci) + 4.0F, g.row_y[static_cast<std::size_t>(ri)] + 4.0F);
                    cells_hit =
                        cells_hit && t.kind == Part::cell &&
                        t.row_index == g.row_index[static_cast<std::size_t>(ri)] &&
                        t.col_index == g.col_index[static_cast<std::size_t>(ci)];
                }
            check(cells_hit, "hit_test: every visible cell center resolves");
            DrawList f;
            detail::compose_draw(d, v, ix, p, g, ft, sy, f, TableId{7});
            bool owner_ok = true, zone_ok = true;
            for (const auto &ss : f.draw.shapes)
                for (const auto &o : ss)
                {
                    zone_ok = zone_ok && o.key != 0;
                    owner_ok = owner_ok && !std::holds_alternative<none_owner>(o.who);
                }
            for (const auto &tt : f.draw.texts)
                for (const auto &o : tt)
                {
                    zone_ok = zone_ok && o.key != 0;
                    owner_ok = owner_ok && !std::holds_alternative<none_owner>(o.who);
                }
            check(zone_ok, "selftest: every op has zone");
            check(owner_ok, "selftest: every op has owner");
        }

        // 17) 导航/勾选/选中扩展/身份往返的补充断言
        {
            v.edit = {};
            v.sel.reset();
            v.ui.last_click_t = -1.0e9;
            const int rr = g.view_rows.front();
            const int cc = g.col_index.front();
            send(Press{10,
                           10,
                           {Part::cell, rr, cc},
                           false,
                           false,
                           false,
                           make_target(Part::cell, rr, cc)});
            send(KeyPress{Key::space, false, false});
            check(v.checks.on[static_cast<std::size_t>(rr)] != 0,
                  "space toggles current row check");
            send(KeyPress{Key::space, false, false});
            check(v.checks.on[static_cast<std::size_t>(rr)] == 0,
                  "space toggles current row check off");

            send(KeyPress{Key::pgdn, false, false});
            check(v.sel.cur_r != rr, "keyboard: PageDown moves selection");
            send(KeyPress{Key::pgup, false, false});
            check(v.sel.cur_r == rr, "keyboard: PageUp returns");

            const int last = g.view_rows.back();
            send(Press{10,
                           10,
                           {Part::cell, last, cc},
                           false,
                           true,
                           false,
                           make_target(Part::cell, last, cc)});
            check(v.sel.r1 == last && v.sel.r0 == rr,
                  "shift+click extends selection to last row");
            send(Press{1,
                           1,
                           {Part::none, -1, -1},
                           false,
                           false,
                           false,
                           make_target(Part::none, -1, -1)});
            check(!v.sel.has_sel(), "click non-cell clears selection");

            check(d.cell_id(0, 0).row == d.ids[0] &&
                      d.cell_id(0, 0).col == d.column_of(0),
                  "cell_id roundtrip row/col");
            check(d.index_of(d.ids[0]) == 0, "index_of row_id");
            check(d.cols[0].id == d.column_of(0), "column id stable");
            check(d.cols[1].id == d.column_of(1), "column id stable 2");

            const Zone zt{Part::cell, 1, 2, TableId{7}};
            const auto backt = zone_from(zone_key(zt));
            check(backt.kind == Part::cell && backt.row == 1 && backt.col == 2 &&
                      backt.table == TableId{7},
                  "zone roundtrip with table bits");
            check(zone_key(zt) != zone_key({Part::cell, 1, 2, TableId{8}}),
                  "zone table bits participate in key");

            check(cursor_for({Part::cell, 0, 0}) == Cursor::arrow, "cursor cell arrow");
            check(cursor_for({Part::menu_item}) == Cursor::arrow, "cursor menu arrow");
        }

        std::println("[table-selftest] done, {} failed", fails);
        return fails;
    }
} // namespace table::test
