#pragma once
// ============================================================================
// table12_app/scene.hpp — 场景/宿主接线（app 层，不是 demo 框架）
//
//   table_spec / make_table / make_table_screen / make_screen
//   window_tables / input_ime / modal_open / hover_index
//
//   依赖：host.hpp / window_input.hpp / window_draw.hpp，且必须放在 demo 框架
//   （ui_new / named_resource / renderer / imm32）之后 include。
//   DAG：host -> window_draw -> window_input -> scene -> test/probes -> main
// ============================================================================

#include "host.hpp"
#include "window_draw.hpp"
#include "window_input.hpp"

namespace table_ui
{
namespace ev = mcs::vulkan::event;
using table::window::Binding;
using table::window::Host;
using table::window::on_hover;
using table::window::trace;
using table::window::insert_row;
using table::window::remove_row;
using table::window::insert_column;
using table::window::remove_column;
using table::window::set_cell;
using table::window::apply_effects;
using table::window::click_dispatch;
using table::window::input_dispatch;
using table::window::set_pointer;
using table::window::notify_change;
using table::window::notify_model;
using table::window::cancel_modal;
using table::window::draw_sync;
using table::window::draw_build;
using table::window::draw_encode;
using table::window::draw;
using table::window::pointer_target;
using table::window::on_pick;
using table::window::Binding;
using table::window::Host;
using table::window::on_hover;
using table::window::trace;
using table::window::insert_row;
using table::window::remove_row;
using table::window::insert_column;
using table::window::remove_column;
using table::window::set_cell;
using table::window::apply_effects;
using table::window::click_dispatch;
using table::window::input_dispatch;
using table::window::set_pointer;
using table::window::notify_change;
using table::window::notify_model;
using table::window::cancel_modal;
    struct table_spec
    {
        table::model::Table data;
        table::intent::Theme prefs;
        table::Font font;
        float flex{1.0F}; // share of the Row
    };

    Host &window_tables() noexcept;
std::uint32_t hover_index();

inline auto make_table(table::model::Table data, table::intent::Theme p, table::Font f)
    {
        data.finalize();
        Binding st;
        st.table_binding.session.model = std::move(data);
        st.table_binding.session.theme = p;
        st.table_binding.session.font = std::move(f);
        st.table_binding.session.id = table::TableId{window_tables().allocate_table_id()};
        st.table_binding.session.state.reset(st.table_binding.session.model);
        return make_aggregate<"Table">(
            field<"arena">(std::move(st)),

            // frame time: arena tick + tooltip / double-click detection
            method<"tick">([](auto &self, double t) { self.arena.advance(t); }),

            // draw entry: single, straight into the draw chain
            method<"render">([](auto &self, ui_new::ScreenWidget *screen,
                                ui_new::Widget *owner, render_context &ctx) {
                auto &s = self.arena;
                s.window_slot.screen = screen;
                s.window_slot.owner = owner;
                // Record this frame's draw order: table_at walks it in reverse
                // (not the attach order).
                if (screen != nullptr && screen->table_host_ctx != nullptr)
                    screen->table_host_ctx->mark_drawn(&s);
                s.window_slot.win = named_resource::frameState().window->data();
                if (s.window_slot.win != nullptr && owner != nullptr)
                {
                    int ww = 1, wh = 1;
                    glfwGetWindowSize(s.window_slot.win, &ww, &wh);
                    if (ww > 0 && wh > 0)
                    {
                        // Window logical -> framebuffer pixels. This must use the
                        // screen size (DPI scale), not the owner width: with N
                        // tables the owner is only a sub-rect of the screen.
                        const auto screenSize = screen->size();
                        s.window_slot.sx =
                            static_cast<float>(screenSize.width) / static_cast<float>(ww);
                        s.window_slot.sy = static_cast<float>(screenSize.height) /
                               static_cast<float>(wh);
                    }
                    if (std::getenv("TABLE_GEOMLOG") != nullptr)
                    {
                        static double lw = -1, lh = -1, lx = -1, ly = -1;
                        if (owner->size.width != lw || owner->size.height != lh ||
                            owner->offset.x != lx || owner->offset.y != ly)
                        {
                            lw = owner->size.width;
                            lh = owner->size.height;
                            lx = owner->offset.x;
                            ly = owner->offset.y;
                            std::println("[host] owner=({:.0f},{:.0f} @ {:.0f},{:.0f}) "
                                         "win={}x{} scale=({:.3f},{:.3f})",
                                         owner->size.width, owner->size.height,
                                         owner->offset.x, owner->offset.y, ww, wh, s.window_slot.sx,
                                         s.window_slot.sy);
                        }
                    }
                }
                const table::Rect box{
                    owner ? static_cast<float>(owner->offset.x) : 0.0F,
                    owner ? static_cast<float>(owner->offset.y) : 0.0F,
                    owner ? static_cast<float>(owner->size.width) : 1.0F,
                    owner ? static_cast<float>(owner->size.height) : 1.0F};
                table::window::draw(s, box, ctx.view, ctx.builder);
                // Cursor is written once by the host after screen.render
            }));
    }

    inline ui_new::ScreenWidget make_table_screen(double w, double h,
                                                  std::vector<table_spec> specs)
    {
        table_ui::window_tables().hover_fn = hover_index();
        auto &hs = window_tables();
        if (specs.empty())
            specs.push_back(table_spec{});

        auto row = ui_new::Row("tables");
        row.mainAxisSize(ui_new::MainAxisSize::max);
        row.crossAxisAlignment(ui_new::CrossAxisAlignment::stretch);
        for (std::size_t i = 0; i < specs.size(); ++i)
        {
            auto &spec = specs[i];
            auto t = make_table(std::move(spec.data), spec.prefs, std::move(spec.font));
            auto ro = render::asRenderObject(std::move(t));
            // attach MUST follow asRenderObject(std::move(agg)): only then is
            // ro->agg.arena's address stable. Later unique_ptr moves do not
            // move the aggregate itself.
            auto &arena = ro->agg.arena;
            arena.host = &hs;
            hs.attach(arena);
            const int flex = std::max(1, static_cast<int>(spec.flex + 0.5F));
            row.addChild(ui_new::Expanded(std::format("table{}", i))
                             .flex(flex)
                             .child(ui_new::Container(std::format("panel{}", i))
                                        .renderObject(std::move(ro))));
        }

        ui_new::ScreenWidget screen{ui_new::Size{w, h},
                                    static_cast<std::unique_ptr<ui_new::Widget>>(row)};
        // raw pointer only; the host is owned by frameState's shared_ptr
        screen.table_host_ctx = &hs;
        return screen;
    }

    inline ui_new::ScreenWidget make_screen(double w, double h)
    {
        // Real on-screen demo: two independent tables side by side. This is the
        // visible proof that the single-table singleton is gone.
        std::vector<table_spec> specs(2);
        for (auto &spec : specs)
        {
            table_glue::init_font(spec.font);
            table_glue::init_model(spec.data, spec.prefs);
        }
        // B is a different KIND of table: 3 columns, taller rows, 24 rows and a
        // warm accent. Independent schema + model + view proves that the two
        // tables are real instances, not the same state drawn twice.
        table_spec &b = specs[1];
        b.data.cols.resize(3);
        b.data.cells.resize(3);
        b.data.finalize();
        b.prefs.row_h = 38.0F;
        b.prefs.header_h = 46.0F;
        b.prefs.accent = {0.95F, 0.62F, 0.35F, 0.95F};
        b.prefs.header_bg = {0.24F, 0.19F, 0.15F, 1.0F};
        constexpr int kBRows = 24;
        if (b.data.rows() > kBRows)
        {
            b.data.ids.resize(static_cast<std::size_t>(kBRows));
            for (auto &col : b.data.cells)
                col.resize(static_cast<std::size_t>(kBRows));
        }
        return make_table_screen(w, h, std::move(specs));
    }

    inline Host &window_tables() noexcept
    {
        // Construct the process-wide input system first so it outlives the
        // host: the host binding calls back into the sink when it is destroyed.
        (void)inputTranslation();
        auto &slot = named_resource::frameState().tableHost;
        if (!slot)
            slot = std::make_shared<Host>();
        return *slot;
    }

    inline void input_ime(Binding &s)
    {
        if (s.window_slot.win == nullptr)
            return;
        const bool filter = s.table_binding.session.state.filter.edit >= 0;
        const bool find = s.table_binding.session.state.find.mode;
        const bool edit = s.table_binding.session.state.edit.active();
        if (!filter && !find && !edit)
            return;
        auto pre = table_glue::ime_preedit(s.window_slot.win);
        if (filter)
        {
            if (pre != s.table_binding.session.state.filter.compose)
            {
                s.table_binding.session.state.filter.compose = std::move(pre);
                s.table_binding.session.state.touch();
            }
            return;
        }
        if (find)
        {
            if (pre != s.table_binding.session.state.find.compose)
            {
                s.table_binding.session.state.find.compose = std::move(pre);
                s.table_binding.session.state.touch();
            }
            return;
        }
        if (pre == s.table_binding.session.state.edit.compose)
            return;
        table::session::ime_compose(s.table_binding.session, std::move(pre));
    }

    inline bool modal_open(const Host *h) noexcept
    {
        const Binding *s = (h != nullptr) ? h->focused : nullptr;
        return s != nullptr && (s->table_binding.session.state.filter.edit >= 0 || s->table_binding.session.state.find.mode ||
                                s->table_binding.session.state.edit.active() || s->table_binding.session.state.menu.open);
    }

    inline std::uint32_t hover_index()
    {
        static const std::uint32_t fn = hoverPool().bind(&on_hover);
        return fn;
    }
} // namespace table_ui
