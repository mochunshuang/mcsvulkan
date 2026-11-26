#pragma once
// ============================================================================
// table12/test/probes.hpp — ability12 的测试驱动（test-only）
//
//   event_probe / multi_probe：headless 回归（TABLE_EVTEST / TABLE_MULTITEST）。
//   只被 demo main 调用；**不被任何 app 头反向 include**。
//   依赖：scene.hpp / window_* / host.hpp 之后 include。
// ============================================================================

#include "../../table12_app/scene.hpp"
#include "alloc_count.hpp"

namespace table_ui
{
        inline void init_multi_table(Binding &s, std::uint32_t id, int rows)
    {
        table::model::Column c;
        c.key = "v";
        c.title = "V";
        c.w = 200.0F;
        s.session.model.cols.push_back(c);
        s.session.model.finalize();
        s.session.model.cells.resize(1);
        for (int i = 0; i < rows; ++i)
        {
            s.session.model.cells[0].push_back({std::format("r{}", i), {}});
            s.session.model.ids.push_back(table::RowId{static_cast<std::uint64_t>(i + 1)});
        }
        s.session.id = table::TableId{id};
        s.session.state.reset(s.session.model);
        s.session.theme.row_h = 30.0F;
        s.session.font.px = 15.0F;
        table::GlyphMetrics gm;
        gm.advance = 0.5F;
        for (int ch = 32; ch < 127; ++ch)
            s.session.font.put(static_cast<char32_t>(ch), gm);
        // headless must initialize the coordinate transform explicitly:
        // owner == nullptr and sx/sy == 1.
        s.window_slot.owner = nullptr;
        s.window_slot.sx = s.window_slot.sy = 1.0F;
    }

inline int event_probe()
    {
        int fails = 0;
        const auto check = [&](bool ok, const char *what) {
            if (!ok)
            {
                ++fails;
                std::println("[chain] FAIL: {}", what);
            }
            else
                std::println("[chain] ok: {}", what);
        };

        Binding s;
        {
            table::model::Column c;
            c.key = "v";
            c.title = "V";
            c.w = 200;
            s.session.model.cols.push_back(c);
        }
        s.session.model.finalize();
        s.session.model.cells.resize(1);
        for (int i = 0; i < 50; ++i)
        {
            s.session.model.cells[0].push_back({std::format("r{}", i), {}});
            s.session.model.ids.push_back(table::RowId{static_cast<std::uint64_t>(i + 1)});
        }
        s.session.id = table::TableId{window_tables().allocate_table_id()};
        s.session.state.reset(s.session.model);
        s.session.theme.row_h = 30.0F;
        s.session.font.px = 15.0F;
        table::GlyphMetrics gm;
        gm.advance = 0.5F;
        for (int c = 32; c < 127; ++c)
            s.session.font.put(static_cast<char32_t>(c), gm);

        const table::Rect box{0, 0, 500, 220};
        const ui_dao::view view{500.0F, 220.0F};

        // ---- draw chain: sync -> build -> encode
        table_ui::draw_sync(s, box);
        check(s.session.layout.col_count() == 1 && s.session.layout.row_count() > 0,
              "draw.sync: data+view -> geometry");
        table_ui::draw_build(s);
        check(s.session.draw.draw.op_count() > 0, "draw.build: geometry -> frame_data");
        {
            std::size_t zero_ops = 0, no_owner = 0;
            for (const auto &stream_shapes : s.session.draw.draw.shapes)
                for (const auto &o : stream_shapes)
                {
                    if (o.key == 0)
                        ++zero_ops;
                    else if (std::holds_alternative<table::none_owner>(o.who))
                        ++no_owner;
                }
            for (const auto &stream_texts : s.session.draw.draw.texts)
                for (const auto &o : stream_texts)
                {
                    if (o.key == 0)
                        ++zero_ops;
                    else if (std::holds_alternative<table::none_owner>(o.who))
                        ++no_owner;
                }
            check(zero_ops == 0, "dl: every op carries target_key");
            check(no_owner == 0, "dl: every zoned op carries owner");
        }
        std::uint32_t instances = 0, zoned = 0, owners = 0;
        table_ui::draw_encode(s, view, [&](auto &&inst) {
            ++instances;
            if (inst.entity_index != 0)
            {
                ++zoned;
                const auto t = table::platform::resolve_key(s.session.model, inst.entity_index);
                owners += t.kind != table::Part::none ? 1U : 0U;
            }
        });
        check(instances > 0, "draw.encode: frame_data -> shader instances");
        check(zoned > 0 && zoned == owners,
              "draw.encode: every entity_index resolves to a target");
        {
            // CPU side of the pixel loop: draw zone and input hit share target_key
            const int r = s.session.layout.view_rows.front();
            const float x = s.session.layout.col_x_at(0) + 10.0F;
            const float y = s.session.layout.row_index.empty() ? s.session.layout.body.y + 5.0F
                                                     : s.session.layout.row_y.front() + 5.0F;
            const auto hit = table::session::hit(s.session, x, y);
            const auto key = table::zone_key(hit.to_zone());
            bool found = false;
            for (const auto &ops : s.session.draw.draw.shapes)
                for (const auto &o : ops)
                    if (o.key == key)
                        found = true;
            check(found, "merge: frame op.zone == input target_key");
            check(hit.row_index == r && hit.col_index == 0,
                  "merge: hit_target gives the same target");
        }

        // ---- input chain: cmd -> systems/visit_owner -> view/data
        const std::uint32_t v0 = s.session.state.revision;
        const int r0 = s.session.layout.view_rows.front();
        input_dispatch(s, table::command::Press{
                              s.session.layout.col_x_at(0) + 10.0F,
                              s.session.layout.row_y.front() + 5.0F,
                              {table::Part::cell, r0, 0, s.session.id},
                              false,
                              false,
                              false,
                              table::model::make_target(table::Part::cell, r0, 0, s.session.id)});
        check(s.session.state.sel.cur_r == r0 && s.session.state.sel.cur_c == 0,
              "input: press -> selection");
        check(s.session.state.revision > v0, "input: press bumps draw version");
        check(s.changes.selection, "input: selection change is reported");
        s.changes = {};
        input_dispatch(s, table::command::KeyPress{table::command::Key::space, false, false});
        check(s.changes.checks, "input: row-check change is reported");
        input_dispatch(s, table::command::KeyPress{table::command::Key::down, false, false});
        check(s.session.state.sel.cur_r > r0, "input: key down -> selection moves");

        // ---- arena 入口：event -> input_frame -> arbiter -> gesture -> on_*
        {
            int sel_cb = 0, chk_cb = 0, model_cb = 0;
            s.cb.on_selection_changed = [&] {
                ++sel_cb;
            };
            s.cb.on_checks_changed = [&] {
                ++chk_cb;
            };
            s.cb.on_model_changed = [&] {
                ++model_cb;
            };
            const float px = s.session.layout.col_x_at(0) + 10.0F;
            const float py = s.session.layout.row_y.front() + 5.0F;
            const auto press = [&](ev::MouseButtons b) {
                s.handle_event(ev::mousebutton_event{.button = b,
                                                     .action = ev::Action::ePRESS,
                                                     .modifier_key = ev::ModifierKey{}});
            };
            const auto release = [&](ev::MouseButtons b) {
                s.handle_event(ev::mousebutton_event{.button = b,
                                                     .action = ev::Action::eRELEASE,
                                                     .modifier_key = ev::ModifierKey{}});
            };

            s.session.interaction.last_click_t = -1.0e9;
            s.session.state.edit = {};
            s.handle_event(ev::position2d_event{.xpos = px, .ypos = py});
            press(ev::MouseButtons::eMOUSE_BUTTON_LEFT);
            release(ev::MouseButtons::eMOUSE_BUTTON_LEFT);
            check(s.session.state.sel.cur_r == r0, "arena: click event -> selection");
            check(sel_cb > 0, "callback: selection_changed fires");
            {
                const int chk0 = chk_cb;
                input_dispatch(s, table::command::KeyPress{table::command::Key::space, false, false});
                check(chk_cb > chk0, "callback: checks_changed fires");
            }

            s.session.interaction.last_click_t = -1.0e9;
            s.session.state.edit = {};
            s.handle_event(ev::keyboard_event{.key = ev::Key::eDOWN,
                                              .action = ev::Action::ePRESS,
                                              .modifier_key = ev::ModifierKey{},
                                              .scancode = 0});
            check(s.session.state.sel.cur_r > r0, "arena: key event -> selection");
            const int after_press = s.session.state.sel.cur_r;
            s.handle_event(ev::keyboard_event{.key = ev::Key::eDOWN,
                                              .action = ev::Action::eREPEAT,
                                              .modifier_key = ev::ModifierKey{},
                                              .scancode = 0});
            check(s.session.state.sel.cur_r > after_press,
                  "arena: key repeat -> selection moves again");
            s.handle_event(ev::keyboard_event{.key = ev::Key::eDOWN,
                                              .action = ev::Action::eRELEASE,
                                              .modifier_key = ev::ModifierKey{},
                                              .scancode = 0});

            const auto scroll_before = s.session.state.scroll.y;
            s.handle_event(ev::scroll_event{.xoffset = 0.0, .yoffset = -1.0});
            check(s.session.state.scroll.y > scroll_before, "arena: wheel event -> scroll");

            // 拖拽：press -> move 超过 slop -> release
            s.session.interaction.last_click_t = -1.0e9;
            s.session.state.edit = {};
            s.session.state.scroll.y = 0.0F;
            s.handle_event(ev::position2d_event{.xpos = px, .ypos = py});
            press(ev::MouseButtons::eMOUSE_BUTTON_LEFT);
            s.handle_event(ev::position2d_event{.xpos = px, .ypos = py + 40.0F});
            s.handle_event(ev::position2d_event{.xpos = px, .ypos = py + 70.0F});
            check(s.session.state.sel.r1 > s.session.state.sel.r0, "arena: drag event -> extend");
            release(ev::MouseButtons::eMOUSE_BUTTON_LEFT);

            // 双击：两次 click 落在同一格 -> core 进编辑并选词
            s.session.interaction.last_click_t = -1.0e9;
            s.session.state.edit = {};
            s.handle_event(ev::position2d_event{.xpos = px, .ypos = py});
            press(ev::MouseButtons::eMOUSE_BUTTON_LEFT);
            release(ev::MouseButtons::eMOUSE_BUTTON_LEFT);
            press(ev::MouseButtons::eMOUSE_BUTTON_LEFT);
            release(ev::MouseButtons::eMOUSE_BUTTON_LEFT);
            check(s.session.state.edit.active(), "arena: double click -> edit");
            s.handle_event(ev::keyboard_event{.key = ev::Key::eESCAPE,
                                              .action = ev::Action::ePRESS,
                                              .modifier_key = ev::ModifierKey{},
                                              .scancode = 0});
            s.handle_event(ev::keyboard_event{.key = ev::Key::eESCAPE,
                                              .action = ev::Action::eRELEASE,
                                              .modifier_key = ev::ModifierKey{},
                                              .scancode = 0});

            // 字符：F2 进编辑，char 进 system_text
            s.session.interaction.last_click_t = -1.0e9;
            s.session.state.edit = {};
            s.handle_event(ev::keyboard_event{.key = ev::Key::eF2,
                                              .action = ev::Action::ePRESS,
                                              .modifier_key = ev::ModifierKey{},
                                              .scancode = 0});
            s.handle_event(ev::keyboard_event{.key = ev::Key::eF2,
                                              .action = ev::Action::eRELEASE,
                                              .modifier_key = ev::ModifierKey{},
                                              .scancode = 0});
            s.handle_event(ev::char_event{.codepoint = U'Z'});
            check(s.session.state.edit.active() && !s.session.state.edit.buf.empty() &&
                      s.session.state.edit.buf.back() == 'Z',
                  "arena: char event -> edit buffer");
            s.handle_event(ev::keyboard_event{.key = ev::Key::eESCAPE,
                                              .action = ev::Action::ePRESS,
                                              .modifier_key = ev::ModifierKey{},
                                              .scancode = 0});
            s.handle_event(ev::keyboard_event{.key = ev::Key::eESCAPE,
                                              .action = ev::Action::eRELEASE,
                                              .modifier_key = ev::ModifierKey{},
                                              .scancode = 0});

            // 覆盖 overlay 状态：菜单 + tooltip 底色也必须带 owner/zone
            s.session.state.edit = {};
            s.handle_event(
                ev::position2d_event{.xpos = px, .ypos = s.session.layout.col_header.y + 10.0F});
            press(ev::MouseButtons::eMOUSE_BUTTON_RIGHT);
            release(ev::MouseButtons::eMOUSE_BUTTON_RIGHT);
            s.session.state.tip.hover_t = s.session.interaction.now - 1.0;
            s.session.interaction.hover = table::model::make_target(table::Part::cell, r0, 0, s.session.id);
            table_ui::draw_build(s);
            {
                std::size_t zero_ops = 0, no_owner = 0;
                for (const auto &ss : s.session.draw.draw.shapes)
                    for (const auto &o : ss)
                    {
                        zero_ops += o.key == 0;
                        no_owner += o.key != 0 &&
                                    std::holds_alternative<table::none_owner>(o.who);
                    }
                for (const auto &tt : s.session.draw.draw.texts)
                    for (const auto &o : tt)
                    {
                        zero_ops += o.key == 0;
                        no_owner += o.key != 0 &&
                                    std::holds_alternative<table::none_owner>(o.who);
                    }
                check(zero_ops == 0 && no_owner == 0,
                      "dl: overlay ops also carry target_key + owner");
            }
            s.session.state.menu.open = false;

            // 模型回调 + view 对齐（中间插入：勾选和选区按 row_id 跟着走）
            {
                s.session.state.checks.on.assign(s.session.model.rows(), 0);
                s.session.state.checks.ids = s.session.model.ids;
                s.session.state.checks.on[0] = 1;
                const auto keep = s.session.model.ids[0];
                const auto sel_before = s.session.state.sel.cur_r;
                (void)table_ui::insert_row(s, 0);
                check(s.session.state.checks.on[1] == 1 && s.session.state.checks.ids[1] == keep,
                      "model: insert at 0 keeps checks by row_id");
                check(s.session.state.sel.cur_r == sel_before + 1,
                      "model: insert at 0 keeps selection by row_id");
                (void)table_ui::remove_row(s, 0);
                check(s.session.state.checks.on[0] == 1 && s.session.state.checks.ids[0] == keep,
                      "model: remove at 0 keeps checks by row_id");
                check(s.session.state.sel.cur_r == sel_before,
                      "model: remove at 0 keeps selection by row_id");
            }
            const auto rows_before = s.session.model.rows();
            (void)table_ui::insert_row(s, 1);
            check(s.session.model.rows() == rows_before + 1 &&
                      s.session.state.checks.on.size() == s.session.model.rows(),
                  "callback: insert_row reconciles view");
            check(model_cb > 0, "callback: model_changed fires");
            (void)table_ui::remove_row(s, 1);
            check(s.session.model.rows() == rows_before, "callback: remove_row restores");
        }

        // redraw after input: the same draw chain sees the new view
        const std::uint32_t v1 = s.session.state.revision;
        table_ui::draw_sync(s, box);
        table_ui::draw_build(s);
        check(s.session.layout.version == v1, "loop: input version -> geometry version");
        check(s.session.draw.draw.op_count() > 0, "loop: input -> next frame redraw");

        std::println("[chain] done, {} failed", fails);
        return fails;
    }

    inline int multi_probe()
    {
        int fails = 0;
        const auto check = [&](bool ok, const char *what) {
            if (!ok)
            {
                ++fails;
                std::println("[multi] FAIL: {}", what);
            }
            else
                std::println("[multi] ok: {}", what);
        };
        namespace ev = mcs::vulkan::event;

        // on_hover needs an InputCtx& (the diagnostic callback ignores it):
        // build a headless InputCtx with the real inputInit; no window / GPU.
        auto inputData = inputInit(16.0F / 9.0F);
        auto inputCtx = InputCtx(inputData);

        Binding a;
        Binding b;
        init_multi_table(a, 1, 10);
        init_multi_table(b, 2, 10);
        draw_sync(a, {0.0F, 0.0F, 400.0F, 300.0F});
        draw_sync(b, {500.0F, 0.0F, 400.0F, 300.0F});

        Host h;
        ui_new::ScreenWidget screen{}; // only used as the host binding
        screen.table_host_ctx = &h;
        h.attach(a);
        h.attach(b);

        const auto make_pick = [](const Binding &t, std::uint32_t version) {
            return picking_result{
                object_key{1U, table::zone_key({table::Part::cell, 0, 0, t.session.id})},
                picking_result::pack(render_version_t{version}, 0), ~0U};
        };

        // 1. initial focus
        check(h.focused == &a, "init: focused == A");
        check(a.session.interaction.hover.kind == table::Part::none &&
                  b.session.interaction.hover.kind == table::Part::none,
              "init: A/B hover empty");

        // 2a. CPU hover: position -> advance -> only B writes ix.hover
        h.route_event(ev::position2d_event{.xpos = 620.0, .ypos = 100.0});
        a.advance(0.0);
        b.advance(0.0);
        check(h.hovered == &b, "cpu-hover: host.hovered == B");
        check(b.session.interaction.hover.kind == table::Part::cell &&
                  b.session.interaction.hover.row_index >= 0,
              "cpu-hover: B ix.hover is a B cell");
        check(a.session.interaction.hover.kind == table::Part::none,
              "cpu-hover: A ix.hover stays empty");

        // 2b. GPU pick: real on_hover routed through ScreenWidget::table_host_ctx
        {
            const auto rb = make_pick(b, static_cast<std::uint32_t>(h.frame_serial));
            table::window::on_hover(rb, true, inputCtx, &screen);
            check(b.pick.key == rb.key, "gpu-hover: B pick updated via on_hover");
            check(a.pick.key.object_type != rb.key.object_type,
                  "gpu-hover: A pick untouched");

            const auto ra = make_pick(a, static_cast<std::uint32_t>(h.frame_serial));
            table::window::on_hover(ra, true, inputCtx, &screen);
            check(a.pick.key == ra.key, "gpu-hover: A pick updated via on_hover");
            check(b.pick.key == rb.key, "gpu-hover: B pick unchanged by A pick");
        }

        // 3. click B: focused=B, B has a selection, A does not
        const int a_sel0 = a.session.state.sel.cur_r;
        h.route_event(
            ev::mousebutton_event{.button = ev::MouseButtons::eMOUSE_BUTTON_LEFT,
                                  .action = ev::Action::ePRESS,
                                  .modifier_key = ev::ModifierKey{}});
        h.route_event(
            ev::mousebutton_event{.button = ev::MouseButtons::eMOUSE_BUTTON_LEFT,
                                  .action = ev::Action::eRELEASE,
                                  .modifier_key = ev::ModifierKey{}});
        check(h.focused == &b, "click: focused == B");
        check(b.session.state.sel.cur_r >= 0, "click: B has a selection");
        check(a.session.state.sel.cur_r == a_sel0, "click: A selection unchanged");

        // 4. keyboard goes to focused (B) only
        {
            const int b0 = b.session.state.sel.cur_r;
            h.route_event(ev::keyboard_event{.key = ev::Key::eDOWN,
                                             .action = ev::Action::ePRESS,
                                             .modifier_key = ev::ModifierKey{},
                                             .scancode = 0});
            check(b.session.state.sel.cur_r > b0, "keyboard: focused B moves");
            check(a.session.state.sel.cur_r == a_sel0, "keyboard: A does not move");
            h.route_event(ev::keyboard_event{.key = ev::Key::eDOWN,
                                             .action = ev::Action::eRELEASE,
                                             .modifier_key = ev::ModifierKey{},
                                             .scancode = 0});
        }

        // 5. chars go to focused (B) only
        {
            h.route_event(ev::keyboard_event{.key = ev::Key::eF2,
                                             .action = ev::Action::ePRESS,
                                             .modifier_key = ev::ModifierKey{},
                                             .scancode = 0});
            h.route_event(ev::keyboard_event{.key = ev::Key::eF2,
                                             .action = ev::Action::eRELEASE,
                                             .modifier_key = ev::ModifierKey{},
                                             .scancode = 0});
            check(b.session.state.edit.active(), "char: F2 opens B edit");
            h.route_event(ev::char_event{.codepoint = U'z'});
            check(b.session.state.edit.active() && !b.session.state.edit.buf.empty() &&
                      b.session.state.edit.buf.back() == 'z',
                  "char: z goes into B edit buffer");
            check(!a.session.state.edit.active(), "char: A edit stays closed");
            h.route_event(ev::keyboard_event{.key = ev::Key::eESCAPE,
                                             .action = ev::Action::ePRESS,
                                             .modifier_key = ev::ModifierKey{},
                                             .scancode = 0});
            h.route_event(ev::keyboard_event{.key = ev::Key::eESCAPE,
                                             .action = ev::Action::eRELEASE,
                                             .modifier_key = ev::ModifierKey{},
                                             .scancode = 0});
        }

        // 6. click A switches focus: later keyboard moves A only
        {
            h.route_event(ev::position2d_event{.xpos = 120.0, .ypos = 100.0});
            h.route_event(
                ev::mousebutton_event{.button = ev::MouseButtons::eMOUSE_BUTTON_LEFT,
                                      .action = ev::Action::ePRESS,
                                      .modifier_key = ev::ModifierKey{}});
            h.route_event(
                ev::mousebutton_event{.button = ev::MouseButtons::eMOUSE_BUTTON_LEFT,
                                      .action = ev::Action::eRELEASE,
                                      .modifier_key = ev::ModifierKey{}});
            check(h.focused == &a, "focus: click A -> focused == A");
            const int a0 = a.session.state.sel.cur_r;
            const int b0 = b.session.state.sel.cur_r;
            h.route_event(ev::keyboard_event{.key = ev::Key::eDOWN,
                                             .action = ev::Action::ePRESS,
                                             .modifier_key = ev::ModifierKey{},
                                             .scancode = 0});
            check(a.session.state.sel.cur_r > a0, "focus: keyboard moves A only");
            check(b.session.state.sel.cur_r == b0, "focus: B does not move");
            h.route_event(ev::keyboard_event{.key = ev::Key::eDOWN,
                                             .action = ev::Action::eRELEASE,
                                             .modifier_key = ev::ModifierKey{},
                                             .scancode = 0});
        }

        // 6b. wheel goes only to the table under the pointer
        {
            h.route_event(ev::position2d_event{.xpos = 120.0, .ypos = 100.0});
            const auto a0 = a.session.state.scroll.y;
            const auto b0 = b.session.state.scroll.y;
            h.route_event(ev::scroll_event{.xoffset = 0.0, .yoffset = -1.0});
            check(a.session.state.scroll.y > a0 && b.session.state.scroll.y == b0,
                  "wheel: pointer over A scrolls only A");

            h.route_event(ev::position2d_event{.xpos = 620.0, .ypos = 100.0});
            const auto a1 = a.session.state.scroll.y;
            const auto b1 = b.session.state.scroll.y;
            h.route_event(ev::scroll_event{.xoffset = 0.0, .yoffset = -1.0});
            check(b.session.state.scroll.y > b1 && a.session.state.scroll.y == a1,
                  "wheel: pointer over B scrolls only B");
        }

        // 6c. right click opens the menu only on the table under the pointer
        {
            h.route_event(ev::position2d_event{.xpos = 620.0, .ypos = 100.0});
            h.route_event(
                ev::mousebutton_event{.button = ev::MouseButtons::eMOUSE_BUTTON_RIGHT,
                                      .action = ev::Action::ePRESS,
                                      .modifier_key = ev::ModifierKey{}});
            h.route_event(
                ev::mousebutton_event{.button = ev::MouseButtons::eMOUSE_BUTTON_RIGHT,
                                      .action = ev::Action::eRELEASE,
                                      .modifier_key = ev::ModifierKey{}});
            check(b.session.state.menu.open && !a.session.state.menu.open,
                  "menu: right click opens only the B menu");
            check(h.focused == &b, "menu: right click focuses B");
            h.route_event(ev::keyboard_event{.key = ev::Key::eESCAPE,
                                             .action = ev::Action::ePRESS,
                                             .modifier_key = ev::ModifierKey{},
                                             .scancode = 0});
            h.route_event(ev::keyboard_event{.key = ev::Key::eESCAPE,
                                             .action = ev::Action::eRELEASE,
                                             .modifier_key = ev::ModifierKey{},
                                             .scancode = 0});
            check(!b.session.state.menu.open, "menu: Esc cancels the B menu");
        }

        // 6d. duplicate table_id is rejected before it corrupts routing
        {
            Host hd;
            Binding d1;
            Binding d2;
            init_multi_table(d1, 7, 3);
            init_multi_table(d2, 7, 3);
            hd.attach(d1);
            bool threw = false;
            try
            {
                hd.attach(d2);
            }
            catch (const std::runtime_error &)
            {
                threw = true;
            }
            check(threw, "attach: duplicate table_id rejected");
            check(hd.tables.size() == 1 && hd.find(table::TableId{7}) == &d1,
                  "attach: duplicate does not corrupt the host");
            check(d2.host == nullptr, "attach: rejected arena keeps host == nullptr");
        }

        // 7. draw order / overlap / hover z-order
        {
            draw_sync(a, {0.0F, 0.0F, 400.0F, 300.0F});
            draw_sync(b, {0.0F, 0.0F, 400.0F, 300.0F});
            // attach order is A, B; drawing B then A -> topmost A wins
            h.begin_frame();
            h.mark_drawn(&b);
            h.mark_drawn(&a);
            h.route_event(ev::position2d_event{.xpos = 120.0, .ypos = 100.0});
            a.advance(0.0);
            b.advance(0.0);
            check(h.table_at(120.0, 100.0) == &a, "zorder: table_at top == A");
            check(a.session.interaction.hover.kind == table::Part::cell,
                  "zorder: only A hovers (B drawn first)");
            check(b.session.interaction.hover.kind == table::Part::none,
                  "zorder: B hover cleared");

            // swap draw order: only B
            h.begin_frame();
            h.mark_drawn(&a);
            h.mark_drawn(&b);
            h.route_event(ev::position2d_event{.xpos = 120.0, .ypos = 100.0});
            a.advance(0.0);
            b.advance(0.0);
            check(h.table_at(120.0, 100.0) == &b, "zorder: table_at top == B");
            check(b.session.interaction.hover.kind == table::Part::cell,
                  "zorder: only B hovers (A drawn first)");
            check(a.session.interaction.hover.kind == table::Part::none,
                  "zorder: A hover cleared");

            // pointer outside every table: both hovers are cleared
            h.begin_frame();
            h.mark_drawn(&a);
            h.mark_drawn(&b);
            h.route_event(ev::position2d_event{.xpos = 2000.0, .ypos = 2000.0});
            a.advance(0.0);
            b.advance(0.0);
            check(h.hovered == nullptr, "zorder: pointer outside -> no hovered");
            check(a.session.interaction.hover.kind == table::Part::none &&
                      b.session.interaction.hover.kind == table::Part::none,
                  "zorder: both hovers cleared outside");
        }

        // 8. frame version consumer (renderVersion compared once in accept_pick)
        {
            h.begin_frame();
            const std::uint32_t serial = static_cast<std::uint32_t>(h.frame_serial);
            const auto good = make_pick(b, serial);
            check(h.accept_pick(good), "version: same-frame picking accepted");
            table::window::on_hover(good, true, inputCtx, &screen);
            check(b.pick.key == good.key, "version: same-frame updates B");

            // differing per-table ix.version must not filter picking
            b.session.state.touch();
            a.session.state.touch();
            table::window::on_hover(good, true, inputCtx, &screen);
            check(b.pick.key == good.key,
                  "version: per-table view version does not filter picking");

            const auto stale = make_pick(b, serial + 1);
            const auto a_before = a.pick.key;
            table::window::on_hover(stale, true, inputCtx, &screen);
            check(!h.accept_pick(stale), "version: stale picking rejected");
            check(b.pick.key == good.key && a.pick.key == a_before,
                  "version: stale picking updates nothing");
        }

        // 9. detach: B is no longer routed, A still works
        {
            h.detach(b);
            check(h.find(b.session.id) == nullptr, "detach: B removed from host");
            check(h.focused == &a, "detach: focused promotes to the surviving A");
            check(a.keyboard_focus, "detach: promoted A has keyboard_focus == true");
            {
                const int a0 = a.session.state.sel.cur_r;
                h.route_event(ev::keyboard_event{.key = ev::Key::eDOWN,
                                                 .action = ev::Action::ePRESS,
                                                 .modifier_key = ev::ModifierKey{},
                                                 .scancode = 0});
                check(a.session.state.sel.cur_r > a0,
                      "detach: A keyboard still works after focused table removed");
                h.route_event(ev::keyboard_event{.key = ev::Key::eDOWN,
                                                 .action = ev::Action::eRELEASE,
                                                 .modifier_key = ev::ModifierKey{},
                                                 .scancode = 0});
            }
            {
                h.route_event(ev::keyboard_event{.key = ev::Key::eF2,
                                                 .action = ev::Action::ePRESS,
                                                 .modifier_key = ev::ModifierKey{},
                                                 .scancode = 0});
                h.route_event(ev::keyboard_event{.key = ev::Key::eF2,
                                                 .action = ev::Action::eRELEASE,
                                                 .modifier_key = ev::ModifierKey{},
                                                 .scancode = 0});
                check(a.session.state.edit.active(), "detach: A can open edit after promotion");
                h.route_event(ev::char_event{.codepoint = U'q'});
                check(a.session.state.edit.active() && !a.session.state.edit.buf.empty() &&
                          a.session.state.edit.buf.back() == 'q',
                      "detach: A still receives char after promotion");
                h.route_event(ev::keyboard_event{.key = ev::Key::eESCAPE,
                                                 .action = ev::Action::ePRESS,
                                                 .modifier_key = ev::ModifierKey{},
                                                 .scancode = 0});
                h.route_event(ev::keyboard_event{.key = ev::Key::eESCAPE,
                                                 .action = ev::Action::eRELEASE,
                                                 .modifier_key = ev::ModifierKey{},
                                                 .scancode = 0});
            }
            h.begin_frame();
            h.mark_drawn(&a);
            check(h.table_at(120.0, 100.0) == &a, "detach: table_at only A");
            const auto b_before = b.pick.key;
            const auto rb = make_pick(b, static_cast<std::uint32_t>(h.frame_serial));
            table::window::on_hover(rb, true, inputCtx, &screen);
            check(b.pick.key == b_before, "detach: on_hover B key not routed");
            const auto ra = make_pick(a, static_cast<std::uint32_t>(h.frame_serial));
            table::window::on_hover(ra, true, inputCtx, &screen);
            check(a.pick.key == ra.key, "detach: A still routed");
        }

        // 10. empty focused: main-loop guards do not crash
        {
            Host empty;
            check(empty.focused == nullptr, "empty: focused == nullptr");
            check(!modal_open(&empty), "empty: modal_open false");
            check(!cancel_modal(empty), "empty: cancel_modal false");
            empty.route_event(ev::keyboard_event{.key = ev::Key::eESCAPE,
                                                 .action = ev::Action::ePRESS,
                                                 .modifier_key = ev::ModifierKey{},
                                                 .scancode = 0});
            check(empty.focused == nullptr, "empty: keyboard ignored, no crash");
        }

        // 11. the real construction entry: make_screen must build TWO Table
        //     render objects in one screen, attached to ONE window host, with
        //     independent models/ids, side-by-side layout, and detach on exit.
        {
            auto scr = make_screen(1536.0, 1000.0);
            check(scr.table_host_ctx == &window_tables(),
                  "screen: bound to the one window host");
            check(window_tables().tables.size() == 2,
                  "screen: window host has two tables");

            scr.updateSize(1536.0, 1000.0);
            scr.layout();
            auto *p0 = scr.findByKey("panel0");
            auto *p1 = scr.findByKey("panel1");
            check(p0 != nullptr && p1 != nullptr, "screen: two Table panel nodes");
            if (p0 != nullptr && p1 != nullptr)
            {
                check(p0->renderObject != nullptr && p1->renderObject != nullptr,
                      "screen: each panel owns a Table render object");
                check(p0->size.width > 0.0 && p1->size.width > 0.0,
                      "screen: both panels laid out");
                check(p1->offset.x >= p0->offset.x + p0->size.width - 1.0,
                      "screen: panels are side by side");
            }

            auto &host2 = window_tables();
            if (host2.tables.size() == 2)
            {
                check(host2.tables[0] != host2.tables[1],
                      "screen: two distinct arena instances");
                check(host2.tables[0]->session.model.rows() == 60 &&
                          host2.tables[1]->session.model.rows() == 24,
                      "screen: independent models (60 rows vs 24 rows)");
                check(host2.tables[0]->session.model.cols_count() == 8 &&
                          host2.tables[1]->session.model.cols_count() == 3,
                      "screen: independent schemas (8 cols vs 3 cols)");
                check(host2.tables[0]->session.id != host2.tables[1]->session.id,
                      "screen: distinct table ids");
            }
            check(host2.focused == (host2.tables.empty() ? nullptr : host2.tables[0]),
                  "screen: first table is focused");
        }
        check(window_tables().tables.empty(), "screen: scope exit detaches both tables");

        // 12. arena destroyed while the host is still alive: ~Binding must
        //     detach, and the host must never access the destroyed arena again.
        {
            Host h3;
            ui_new::ScreenWidget s3{}; // only for on_hover routing
            s3.table_host_ctx = &h3;
            {
                Binding c;
                init_multi_table(c, 3, 5);
                draw_sync(c, {0.0F, 0.0F, 400.0F, 300.0F});
                h3.attach(c);
                h3.begin_frame();
                h3.mark_drawn(&c);
                check(h3.tables.size() == 1 && h3.find(c.session.id) != nullptr,
                      "life: arena attached before destruction");
            }
            check(h3.tables.empty() && h3.find(table::TableId{3}) == nullptr,
                  "life: ~Binding detaches from the host");
            check(h3.focused == nullptr && h3.pressed == nullptr && h3.hovered == nullptr,
                  "life: destruction clears host pointers");
            check(h3.table_at(120.0, 100.0) == nullptr,
                  "life: host does not return the destroyed arena");
            const auto r3 = picking_result{
                object_key{1U, table::zone_key(
                                   {table::Part::cell, 0, 0, table::TableId{3}})},
                picking_result::pack(
                    render_version_t{static_cast<std::uint32_t>(h3.frame_serial)}, 0),
                ~0U};
            table::window::on_hover(r3, true, inputCtx, &s3); // must be a safe no-op
            check(h3.find(table::TableId{3}) == nullptr,
                  "life: on_hover for a destroyed table is a safe no-op");
        }

        std::println("[multi] done, {} failed", fails);
        return fails;
    }
// ── 性能探针（TABLE_PERF）：compose x10k / dispatch x100k + DrawList 指纹 ──
inline int perf_probe()
{
    Binding s;
    init_multi_table(s, 1, 200);
    const table::Rect vp{0.0F, 0.0F, 900.0F, 600.0F};
    table::session::compose(s.session, vp);

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 10000; ++i)
        table::session::compose(s.session, vp);
    const auto t1 = std::chrono::steady_clock::now();

    for (int i = 0; i < 2000; ++i)
        table::session::dispatch(s.session, table::command::Command{table::command::Hover{}});
    const auto t2 = std::chrono::steady_clock::now();
    table::test::alloc_reset();
    for (int i = 0; i < 100000; ++i)
        table::session::dispatch(s.session, table::command::Command{table::command::Hover{}});
    const auto allocs = table::test::allocs();
    const auto t3 = std::chrono::steady_clock::now();
    // Outcome 常见路径（Hover，无 effect）必须 0 分配
    if (allocs != 0)
    {
        std::println("[perf] FAIL: dispatch allocated {} times", allocs);
        return 1;
    }

    // DrawList 指纹：zones/visual/version（顺序敏感）
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&](std::uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    mix(s.session.draw.zones.size());
    mix(s.session.draw.visual.size());
    mix(s.session.draw.version);
    for (const auto &z : s.session.draw.zones)
        mix(static_cast<std::uint64_t>(z.z.kind) * 1000003ull +
            static_cast<std::uint64_t>(z.z.row + 1) * 1009ull +
            static_cast<std::uint64_t>(z.z.col + 1));
    const std::uint64_t ops = s.session.draw.zones.size();
    const auto ms = [](auto a, auto b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    std::println("[perf] compose_10k={:.3f}ms dispatch_100k={:.3f}ms ops={} hash={:016x} allocs=0",
                 ms(t0, t1), ms(t2, t3), ops, h);
    return 0;
}

} // namespace table_ui
