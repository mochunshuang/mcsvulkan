#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// test_intent5_table.hpp —— 数据生成表格（事件 → 数据 → 派生 → 渲染数据）
//
//   test_intent4 的 table_demo 原样保留作参考；这里是另一套实现。
//
//   一条流水线，每一段只做一件事：
//
//     事件 event
//       │  resolve：只做“路由 + 组装命令”，不改任何数据
//       ▼
//     命令 command（POD，可记录 / 可回放 / 可单测）
//       │  apply：唯一写数据的地方（纯数据变换，不碰渲染）
//       ▼
//     数据 data
//       ├─ table：列 = 名字 + cells + 显示策略（声明宽 / 权重 / 覆盖）
//       └─ view ：连线数据（行序、选择、焦点、滚动、编辑、拖拽、菜单、悬停）
//       │  derive：纯函数，按需算，不保存“上一帧”
//       ├─ geometry：列宽 / 可见范围 / 滚动条 / 内容尺寸
//       └─ frame_data：渲染数据上下文（带 scissor 的显示列表：quad + 文本）
//       ▼
//     渲染 emit：只把 frame_data 变成实例，零判断、零查询
//
//   规则：
//     · 事件只产生命令；命令只改数据；渲染只读 frame_data。
//     · 算法全部无状态（列宽 / 命中 / 排序 / 选择 / 帧构建都是纯函数）。
//     · 列数运行时决定：没有 <N>、没有 schema 模板参数。
//     · 文本按“内容”缓存（数据 → 字形是纯函数 + 记忆），不按 (行,列) 存第二份。
//     · 几何 / 文本 / 帧都不是“状态”，每帧从数据重算（带容量复用，不分配）。
//
//   这个头在 test_intent5.cpp 中、UI / 输入 / 字体类型定义之后 include 一次。
// ═══════════════════════════════════════════════════════════════════════════

namespace table_demo
{
    using namespace ui_new;

    // ═══════════════════════════════════════════════════════════════════════════
    // 1. 数据
    // ═══════════════════════════════════════════════════════════════════════════

    struct cell
    {
        std::string text; // 单元格文本（唯一真相）
        std::string note; // 可选：该格 tooltip（空 = 无）
    };

    struct column
    {
        std::string name;        // 表头文本
        std::vector<cell> cells; // 该列全部行（列主序）

        // 显示策略是列自己的数据：重排列时宽度/权重/覆盖一起移动。
        float width = 0.0f;  // 声明宽度：>0 固定
        float weight = 0.0f; // >0 参与剩余宽度分配
        float over = 0.0f;   // 用户覆盖：>0 拖宽；<0 隐藏；0 未覆盖
    };

    struct table
    {
        std::vector<column> cols;
        std::vector<std::string> notes; // 行级 note（优先级低于格 note）

        [[nodiscard]] std::size_t rows() const noexcept
        {
            return cols.empty() ? 0u : cols.front().cells.size();
        }
        [[nodiscard]] std::string_view text(std::uint32_t row, int col) const noexcept
        {
            return cols[static_cast<std::size_t>(col)].cells[row].text;
        }
        [[nodiscard]] const cell &at(std::uint32_t row, int col) const noexcept
        {
            return cols[static_cast<std::size_t>(col)].cells[row];
        }
    };

    // ── 命中：二维地址（行/列都是视图坐标）────────────────────────────────────
    enum class region : std::uint8_t
    {
        outside,
        header,
        cell,
        separator,
        vbar,
        hbar,
    };

    struct hit
    {
        region r = region::outside;
        int row = -1;
        int col = -1;
        bool thumb = false;

        [[nodiscard]] constexpr bool inside() const noexcept
        {
            return r != region::outside;
        }
        friend constexpr bool operator==(const hit &, const hit &) = default;
    };

    // ── 连线数据（事件只改这里）──────────────────────────────────────────────
    struct view
    {
        // 行：视图行 → 数据行。排序 = 重排它；数据本身不动。
        std::vector<std::uint32_t> rows;

        // 选择：数据行 id（升序去重）。
        std::vector<std::uint32_t> selected;
        std::uint32_t anchor = 0;

        // 指针 / 悬停（也是数据：工具提示、光标都从它派生）
        glm::dvec2 pointer{-1.0, -1.0};
        glm::dvec2 press_pos{-1.0, -1.0};
        hit press{};
        hit hover{};
        hit hover_prev{};

        // 焦点（视图坐标）
        int focus_row = 0;
        int focus_col = 0;

        float scroll_x = 0.0f, scroll_y = 0.0f;
        int sort_col = -1, sort_dir = 0;

        // 单元格矩形选区（视图坐标）
        bool rect_sel = false;
        int rect_r0 = -1, rect_c0 = -1, rect_r1 = -1, rect_c1 = -1;

        // 编辑
        bool editing = false;
        int edit_row = -1, edit_col = -1;
        std::string buf;
        std::size_t caret = 0, caret_anchor = 0;

        // 拖拽
        enum class drag_kind : std::uint8_t
        {
            none,
            width,
            column,
            vbar,
            hbar,
        };
        drag_kind drag = drag_kind::none;
        int drag_col = -1;
        int drag_pos = -1;
        float drag_w0 = 0.0f, drag_grab = 0.0f;

        // 右键菜单
        bool menu_open = false;
        int menu_col = -1;
        float menu_x = 0.0f, menu_y = 0.0f;

        // tooltip 计时（毫秒）
        std::int64_t tt_t0 = 0;
        int tt = 0; // 0 无 / 1 计时 / 2 显示

        // 双击
        std::int64_t click_t = -100000;
        int click_row = -1, click_col = -1;
    };

    struct prefs
    {
        float pad = 12.0f;
        float row_h = 30.0f;
        float row_gap = 2.0f;
        float header_h = 40.0f;
        float cell_pad = 10.0f;
        float min_col_w = 56.0f;
        float max_col_w = 640.0f;
        float bar = 7.0f;
        float default_col_w = 120.0f;
        float font_px = 15.0f;
        float margin = 40.0f;
        float top = 100.0f;
        int frozen = 1;
    };

    // ── 选择：一份数据，几个纯查询 ───────────────────────────────────────────
    inline bool is_selected(const view &v, std::uint32_t row_id) noexcept
    {
        return std::binary_search(v.selected.begin(), v.selected.end(), row_id);
    }

    inline int pos_of_row(const view &v, std::uint32_t row_id) noexcept
    {
        const auto it = std::find(v.rows.begin(), v.rows.end(), row_id);
        return it == v.rows.end() ? -1 : static_cast<int>(it - v.rows.begin());
    }

    inline std::vector<std::uint32_t> selected_rows(const view &v)
    {
        if (!v.rect_sel)
            return v.selected;
        std::vector<std::uint32_t> ids;
        const int r0 = std::min(v.rect_r0, v.rect_r1);
        const int r1 = std::max(v.rect_r0, v.rect_r1);
        for (int r = std::max(0, r0); r <= r1 && r < static_cast<int>(v.rows.size()); ++r)
            ids.push_back(v.rows[static_cast<std::size_t>(r)]);
        return ids;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // 2. 几何：派生量 + 纯函数
    // ═══════════════════════════════════════════════════════════════════════════

    struct col_box
    {
        float x = 0.0f, w = 0.0f;
    };

    struct bar_box
    {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        float thumb_off = 0.0f, thumb_len = 0.0f;
        bool on = false;
    };

    struct geometry
    {
        std::vector<col_box> cols;
        float x0 = 0, y0 = 0, w = 0, h = 0;
        float header_h = 0, body_h = 0, row_h = 0, row_gap = 0, pad = 0, cell_pad = 0;
        float scroll_x = 0, scroll_y = 0;
        float content_w = 0, content_h = 0, max_x = 0, max_y = 0;
        bar_box vb{}, hb{};
        bool vb_on = false, hb_on = false;
        int col0 = 0, col1 = 0;
        int row0 = 0, row1 = 0;
        int frozen = 0;
        bool valid = false;

        [[nodiscard]] int count() const noexcept
        {
            return static_cast<int>(cols.size());
        }
        [[nodiscard]] float frozen_w() const noexcept
        {
            if (frozen <= 0)
                return 0.0f;
            if (frozen < count())
                return cols[static_cast<std::size_t>(frozen)].x;
            return w;
        }
    };

    inline int column_at(const geometry &g, float px) noexcept
    {
        int best = -1;
        for (int c = 0; c < g.count(); ++c)
            if (g.cols[static_cast<std::size_t>(c)].w > 0.0f &&
                px >= g.cols[static_cast<std::size_t>(c)].x)
                best = c;
        return best;
    }

    inline void visible_cols(const geometry &g, float sx, float w, int &c0,
                             int &c1) noexcept
    {
        const int n = g.count();
        c0 = std::clamp(column_at(g, sx), 0, n - 1);
        c1 = c0;
        while (c1 < n && g.cols[static_cast<std::size_t>(c1)].x < sx + w)
            ++c1;
    }

    inline void visible_rows(float sy, float body_h, std::size_t n, float step, int &r0,
                             int &r1) noexcept
    {
        if (n == 0 || step <= 0.0f)
        {
            r0 = r1 = 0;
            return;
        }
        const int total = static_cast<int>(n);
        r0 = std::clamp(static_cast<int>(sy / step), 0, total);
        r1 = std::min(total, r0 + static_cast<int>(body_h / step) + 2);
    }

    inline bar_box make_bar(float x, float y, float w, float h, float track_len,
                            float content, float visible, float scroll) noexcept
    {
        bar_box b{};
        b.on = content > visible + 0.5f;
        if (!b.on || track_len <= 0.0f)
            return b;
        b.x = x;
        b.y = y;
        b.w = w;
        b.h = h;
        const float lo = std::min(28.0f, track_len);
        b.thumb_len = std::clamp(track_len * visible / content, lo, track_len);
        const float maxs = content - visible;
        b.thumb_off = maxs > 0.0f ? (scroll / maxs) * (track_len - b.thumb_len) : 0.0f;
        return b;
    }

    // 列宽：声明宽度 / 权重 / 用户覆盖，只有一个优先级顺序。
    inline void solve(geometry &g, const table &t, const view &v, const prefs &p,
                      float x0, float y0, float w, float h)
    {
        const int n = static_cast<int>(t.cols.size());
        if (n <= 0 || w <= 0.0f || h <= 0.0f)
        {
            g.valid = false;
            return;
        }

        g.cols.resize(static_cast<std::size_t>(n));
        g.x0 = x0;
        g.y0 = y0;
        g.w = w;
        g.h = h;
        g.pad = p.pad;
        g.cell_pad = p.cell_pad;
        g.row_h = p.row_h;
        g.row_gap = p.row_gap;
        g.header_h = p.header_h;
        g.frozen = p.frozen;

        const auto target_of = [&p](const column &col) -> float {
            if (col.over < 0.0f)
                return 0.0f;
            if (col.over > 0.0f)
                return col.over;
            if (col.width > 0.0f)
                return col.width;
            if (col.weight > 0.0f)
                return -1.0f;
            return p.default_col_w;
        };

        float fixed = 0.0f, weight = 0.0f;
        for (const auto &col : t.cols)
        {
            const float target = target_of(col);
            if (target > 0.0f)
                fixed += target;
            else if (target < 0.0f)
                weight += col.weight;
        }

        const float inner = std::max(0.0f, w - p.pad * static_cast<float>(n + 1));
        constexpr float flex_min = 140.0f;
        const float flex = std::max(inner - fixed, weight > 0.0f ? flex_min : 0.0f);

        float x = p.pad;
        for (int c = 0; c < n; ++c)
        {
            const auto &col = t.cols[static_cast<std::size_t>(c)];
            const float target = target_of(col);
            const float wc = target >= 0.0f
                                 ? target
                                 : (weight > 0.0f ? flex * col.weight / weight : 0.0f);
            auto &box = g.cols[static_cast<std::size_t>(c)];
            box.x = x;
            box.w = static_cast<float>(static_cast<int>(wc));
            x += box.w + p.pad;
        }
        if (const float rest = w - x; rest > 0.5f)
            for (int c = n - 1; c >= 0; --c)
                if (g.cols[static_cast<std::size_t>(c)].w > 0.0f)
                {
                    g.cols[static_cast<std::size_t>(c)].w += rest;
                    break;
                }

        const auto &last = g.cols[static_cast<std::size_t>(n - 1)];
        g.content_w = last.x + last.w + p.pad;
        g.max_x = std::max(0.0f, g.content_w - w);
        g.hb_on = g.max_x > 0.5f;
        const float hbar_h = g.hb_on ? p.bar + 4.0f : 0.0f;
        g.body_h = h - p.header_h - hbar_h;
        g.content_h = static_cast<float>(t.rows()) * (p.row_h + p.row_gap);
        g.max_y = std::max(0.0f, g.content_h - g.body_h);
        g.vb_on = g.max_y > 0.5f;

        g.scroll_x = std::clamp(v.scroll_x, 0.0f, g.max_x);
        g.scroll_y = std::clamp(v.scroll_y, 0.0f, g.max_y);

        const float vb_w = g.vb_on ? p.bar : 0.0f;
        g.vb = make_bar(w - p.bar - 4.0f, p.header_h + 4.0f, p.bar, g.body_h - 8.0f,
                        g.body_h - 8.0f, g.content_h, g.body_h, g.scroll_y);
        g.vb_on = g.vb.on;
        g.hb = make_bar(4.0f, h - p.bar - 4.0f, w - vb_w - p.bar - 8.0f, p.bar,
                        w - vb_w - p.bar - 8.0f, g.content_w, w, g.scroll_x);
        g.hb_on = g.hb.on;

        visible_cols(g, g.scroll_x, w, g.col0, g.col1);
        visible_rows(g.scroll_y, g.body_h, t.rows(), p.row_h + p.row_gap, g.row0, g.row1);
        g.valid = true;
    }

    inline bool in_box(const geometry &g, glm::dvec2 p) noexcept
    {
        return g.valid && p.x >= g.x0 && p.x <= g.x0 + g.w && p.y >= g.y0 &&
               p.y <= g.y0 + g.h;
    }

    inline hit hit_test(const geometry &g, std::size_t rows, double px,
                        double py) noexcept
    {
        if (!g.valid || g.w <= 0.0f || g.h <= 0.0f)
            return {};

        const double lx = px - g.x0;
        const double ly = py - g.y0;
        if (lx < 0.0 || ly < 0.0 || lx > g.w || ly > g.h)
            return {};

        if (g.vb_on && lx >= g.vb.x - 2.0 && ly >= g.header_h)
        {
            const bool thumb = ly >= g.vb.y + g.vb.thumb_off &&
                               ly <= g.vb.y + g.vb.thumb_off + g.vb.thumb_len;
            return {region::vbar, -1, -1, thumb};
        }
        if (g.hb_on && ly >= g.hb.y - 2.0)
        {
            const bool thumb = lx >= g.hb.x + g.hb.thumb_off &&
                               lx <= g.hb.x + g.hb.thumb_off + g.hb.thumb_len;
            return {region::hbar, -1, -1, thumb};
        }

        const auto row_at = [&](double body_y) -> int {
            const double step = static_cast<double>(g.row_h + g.row_gap);
            const double y = body_y + g.scroll_y;
            if (y < 0.0)
                return -1;
            const auto r = static_cast<long long>(y / step);
            if (r < 0 || static_cast<std::size_t>(r) >= rows)
                return -1;
            const double in_row = y - static_cast<double>(r) * step;
            return in_row > static_cast<double>(g.row_h) ? -1 : static_cast<int>(r);
        };

        const int n = g.count();
        const int frozen = g.frozen;
        const float frozen_w = g.frozen_w();

        if (frozen > 0 && lx < frozen_w)
        {
            if (frozen < n)
            {
                const double sep =
                    static_cast<double>(frozen_w) - static_cast<double>(g.pad) * 0.5;
                if (lx >= sep - 4.0 && lx <= sep + 4.0)
                    return {region::separator, -1, frozen - 1};
            }
            const int c = column_at(g, static_cast<float>(lx));
            if (ly < static_cast<double>(g.header_h))
                return {region::header, -1, c};
            const int row = row_at(ly - static_cast<double>(g.header_h));
            if (row < 0)
                return {};
            return {region::cell, row, c};
        }

        for (int c = frozen + 1; c < n; ++c)
        {
            const double sx = static_cast<double>(g.cols[static_cast<std::size_t>(c)].x -
                                                  g.pad * 0.5f) -
                              g.scroll_x;
            const double d = lx - sx;
            if (d >= -4.0 && d <= 4.0)
                return {region::separator, -1, c - 1};
        }

        if (ly < static_cast<double>(g.header_h))
            return {region::header, -1,
                    column_at(g, static_cast<float>(lx + g.scroll_x))};

        const int row = row_at(ly - static_cast<double>(g.header_h));
        if (row < 0)
            return {};
        return {region::cell, row, column_at(g, static_cast<float>(lx + g.scroll_x))};
    }

    // 浮层 / 菜单几何：只有一个来源，点击、绘制、hover 都问它
    struct rect
    {
        float x = 0, y = 0, w = 0, h = 0;
    };

    inline rect overlay_at(float ax, float ay, float w, float h, float x0, float y0,
                           float tw, float th, float off_x = 0.0f,
                           float off_y = 6.0f) noexcept
    {
        rect r{};
        r.w = w;
        r.h = h;
        r.x = std::clamp(ax + off_x, x0 + 4.0f, x0 + tw - w - 4.0f);
        r.y = ay + off_y;
        if (r.y + h > y0 + th)
            r.y = ay - h - off_y;
        return r;
    }

    inline constexpr float kMenuRow = 26.0f;
    inline constexpr int kMenuItemCount = 4;
    inline constexpr std::string_view kMenuLabels[kMenuItemCount] = {
        "Fit width", "Reset sort", "Hide column", "Show all"};

    inline rect menu_rect(const geometry &g, float ax, float ay) noexcept
    {
        return overlay_at(ax, ay, 150.0f, kMenuRow * kMenuItemCount + 8.0f, g.x0, g.y0,
                          g.w, g.h);
    }

    inline int menu_item_at(const rect &r, float px, float py) noexcept
    {
        if (px < r.x || px > r.x + r.w || py < r.y + 4.0f || py > r.y + r.h - 4.0f)
            return -1;
        const int item = static_cast<int>((py - r.y - 4.0f) / kMenuRow);
        return item >= 0 && item < kMenuItemCount ? item : -1;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // 3. 文本：数据 → 字形（纯函数 + 按内容记忆）
    // ═══════════════════════════════════════════════════════════════════════════

    struct cell_glyphs
    {
        std::vector<shader_data::Glyph> glyphs;
        float advance = 0.0f;
    };

    template <class Font>
    cell_glyphs shape_cell(Font &font, std::string_view text, float font_px, float vw,
                           float vh)
    {
        namespace font_ns = mcs::vulkan::font;
        cell_glyphs out;
        if (text.empty())
            return out;

        std::string owned{text};
        auto norm = font_ns::utf8proc::normalize(owned.c_str());
        auto analyze = font_ns::bidi::analyze(norm.codepoints, 0);
        auto runs = font_ns::assign_fonts(analyze, font);
        auto shaped = font_ns::harfbuzz::shape(analyze.mirrored_codepoints, runs,
                                               font.notdefFont());

        float cursor = 0.0f;
        for (const auto &run : shaped)
            for (const auto &g : run)
            {
                if (!(g.plane_bounds == decltype(g.plane_bounds){}))
                    out.glyphs.push_back(make_glyph(g, vw, vh, font_px, cursor, 0.0f, 0,
                                                    0, ~0u, glm::vec4(1.0f), 1));
                cursor += g.advance_x * font_px;
            }
        out.advance = cursor;
        return out;
    }

    struct shaped_text
    {
        cell_glyphs g;
        glm::vec2 viewport{-1.0f, -1.0f};
        float px = 0.0f;
    };

    // 内容 → 字形。同一段文字在表里出现多少次都只 shape 一次。
    struct text_cache
    {
        std::unordered_map<std::string, shaped_text> map;

        const cell_glyphs &get(std::string_view text, FontSelect &font, float px,
                               float vw, float vh)
        {
            const glm::vec2 vp{vw, vh};
            auto [it, inserted] = map.try_emplace(std::string(text));
            auto &e = it->second;
            if (inserted || e.px != px || e.viewport != vp)
            {
                e.px = px;
                e.viewport = vp;
                e.g = shape_cell(font, text, px, vw, vh);
            }
            return e.g;
        }
    };

    inline std::size_t utf8_prev(std::string_view s, std::size_t i) noexcept
    {
        if (i == 0)
            return 0;
        --i;
        while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
            --i;
        return i;
    }

    inline std::size_t utf8_next(std::string_view s, std::size_t i) noexcept
    {
        if (i >= s.size())
            return s.size();
        ++i;
        while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
            ++i;
        return i;
    }

    inline void append_utf8(std::string &s, std::uint32_t cp) noexcept
    {
        if (cp < 0x80)
            s.push_back(static_cast<char>(cp));
        else if (cp < 0x800)
        {
            s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp < 0x10000)
        {
            s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    // tooltip 文本 = 数据的投影（格 note > 行 note > "列名: 值"），不是状态。
    inline std::string tooltip_text(const table &t, const view &v)
    {
        if (v.hover.row < 0 || v.hover.col < 0 ||
            v.hover.row >= static_cast<int>(v.rows.size()) ||
            v.hover.col >= static_cast<int>(t.cols.size()))
            return {};
        const int col = v.hover.col;
        const std::uint32_t row = v.rows[static_cast<std::size_t>(v.hover.row)];
        const auto &c = t.at(row, col);
        std::string out = t.cols[static_cast<std::size_t>(col)].name + ": " + c.text;
        if (!c.note.empty())
            out = c.note;
        else if (row < t.notes.size() && !t.notes[row].empty())
            out = t.notes[row];
        return out;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // 4. 命令：事件能对数据说的全部话（POD，可记录 / 可回放）
    // ═══════════════════════════════════════════════════════════════════════════

    enum class cmd : std::uint8_t
    {
        none,
        pointer_move,
        pointer_leave,
        press,
        context_menu,
        click,
        drag_begin,
        drag_move,
        drag_end,
        wheel,
        key,
        text,
        tick, // 时间也是输入：tick 只改数据（tooltip 计时）
    };

    struct command
    {
        cmd kind = cmd::none;
        glm::dvec2 pos{-1.0, -1.0};
        double total_dx = 0.0, total_dy = 0.0;
        std::int64_t time = 0;
        int wheel_delta = 0;
        Key key{};
        std::uint32_t codepoint = 0;
    };

    // 数据之外、算法需要的东西（配置 + 服务）
    struct env
    {
        const prefs &p;
        FontSelect &font;
        text_cache &text;
        glm::vec2 viewport;
    };

    // ═══════════════════════════════════════════════════════════════════════════
    // 5. 数据操作：全是无状态函数，只改数据（不渲染、不失效）
    // ═══════════════════════════════════════════════════════════════════════════

    inline bool op_scroll_to(view &v, const geometry &g, float sx, float sy)
    {
        const float nx = std::clamp(sx, 0.0f, g.max_x);
        const float ny = std::clamp(sy, 0.0f, g.max_y);
        if (nx == v.scroll_x && ny == v.scroll_y)
            return false;
        v.scroll_x = nx;
        v.scroll_y = ny;
        std::println("[table] scroll x={:.0f} y={:.0f}", nx, ny);
        return true;
    }

    inline bool op_ensure_visible(view &v, const geometry &g, const prefs &p, int row)
    {
        if (row < 0 || !g.valid)
            return false;
        const float step = p.row_h + p.row_gap;
        const float top = static_cast<float>(row) * step;
        const float bottom = top + step;
        float sy = v.scroll_y;
        if (top < sy)
            sy = top;
        else if (bottom > sy + g.body_h)
            sy = bottom - g.body_h;
        return op_scroll_to(v, g, v.scroll_x, sy);
    }

    inline void op_select_row(view &v, int row, int mode)
    {
        const int n = static_cast<int>(v.rows.size());
        if (n <= 0)
            return;
        row = std::clamp(row, 0, n - 1);
        const std::uint32_t id = v.rows[static_cast<std::size_t>(row)];

        if (mode == 2)
        {
            auto it = std::lower_bound(v.selected.begin(), v.selected.end(), id);
            if (it != v.selected.end() && *it == id)
                v.selected.erase(it);
            else
                v.selected.insert(it, id);
            v.anchor = id;
        }
        else if (mode == 1)
        {
            int a = pos_of_row(v, v.anchor);
            if (a < 0)
                a = row;
            const int lo = std::min(a, row), hi = std::max(a, row);
            v.selected.clear();
            v.selected.reserve(static_cast<std::size_t>(hi - lo + 1));
            for (int r = lo; r <= hi; ++r)
                v.selected.push_back(v.rows[static_cast<std::size_t>(r)]);
            std::sort(v.selected.begin(), v.selected.end());
        }
        else
        {
            v.selected.assign(1, id);
            v.anchor = id;
        }

        v.focus_row = row;
        int cnt = 0;
        for (int r = 0; r < n; ++r)
            cnt += is_selected(v, v.rows[static_cast<std::size_t>(r)]) ? 1 : 0;
        std::println("[table] select row={} mode={} count={}", row, mode, cnt);
    }

    inline void op_select_all(view &v)
    {
        const int n = static_cast<int>(v.rows.size());
        if (n <= 0)
            return;
        v.selected = v.rows;
        std::sort(v.selected.begin(), v.selected.end());
        v.anchor = v.rows.front();
        v.focus_row = 0;
    }

    inline void op_sort(table &t, view &v)
    {
        const std::size_t n = v.rows.size();
        v.rows.resize(n);
        for (std::size_t i = 0; i < n; ++i)
            v.rows[i] = static_cast<std::uint32_t>(i);
        if (v.sort_col < 0 || v.sort_dir == 0 ||
            v.sort_col >= static_cast<int>(t.cols.size()))
            return;
        const int col = v.sort_col;
        const int dir = v.sort_dir;
        std::stable_sort(v.rows.begin(), v.rows.end(),
                         [&t, col, dir](std::uint32_t a, std::uint32_t b) {
                             const auto sa = t.text(a, col);
                             const auto sb = t.text(b, col);
                             return dir > 0 ? sa < sb : sa > sb;
                         });
    }

    inline void op_move_column(table &t, view &v, int from, int to)
    {
        auto &cols = t.cols;
        const int n = static_cast<int>(cols.size());
        if (from == to || from < 0 || to < 0 || from >= n || to >= n)
            return;

        column moved = std::move(cols[static_cast<std::size_t>(from)]);
        cols.erase(cols.begin() + from);
        cols.insert(cols.begin() + to, std::move(moved));

        const auto remap = [from, to](int i) -> int {
            if (i < 0)
                return i;
            if (i == from)
                return to;
            if (from < to && i > from && i <= to)
                return i - 1;
            if (to < from && i >= to && i < from)
                return i + 1;
            return i;
        };
        v.sort_col = remap(v.sort_col);
        v.focus_col = remap(v.focus_col);
        v.edit_col = remap(v.edit_col);
        v.menu_col = remap(v.menu_col);
        v.drag_col = remap(v.drag_col);
    }

    inline void op_start_edit(view &v, const table &t, int row, int col, bool clear)
    {
        if (row < 0 || col < 0 || row >= static_cast<int>(v.rows.size()) ||
            col >= static_cast<int>(t.cols.size()))
            return;
        v.editing = true;
        v.edit_row = row;
        v.edit_col = col;
        v.buf = clear ? std::string{}
                      : std::string(t.text(v.rows[static_cast<std::size_t>(row)], col));
        v.caret = v.buf.size();
        v.caret_anchor = v.caret;
    }

    inline bool op_commit_edit(table &t, view &v)
    {
        if (!v.editing)
            return false;
        const int row = v.edit_row;
        const int col = v.edit_col;
        if (row >= 0 && row < static_cast<int>(v.rows.size()) && col >= 0 &&
            col < static_cast<int>(t.cols.size()))
        {
            t.cols[static_cast<std::size_t>(col)]
                .cells[v.rows[static_cast<std::size_t>(row)]]
                .text = v.buf;
            std::println("[table] commit ({},{}) = \"{}\"", row, col, v.buf);
        }
        v.editing = false;
        v.edit_row = -1;
        v.edit_col = -1;
        if (v.sort_col == col)
            op_sort(t, v);
        return true;
    }

    inline void op_cancel_edit(view &v)
    {
        v.editing = false;
        v.edit_row = -1;
        v.edit_col = -1;
    }

    inline void op_auto_fit(table &t, const env &e, int col)
    {
        if (col < 0 || col >= static_cast<int>(t.cols.size()) || t.rows() == 0)
            return;
        if (e.viewport.x <= 0.0f || e.viewport.y <= 0.0f)
            return;
        const auto &p = e.p;
        const auto &c = t.cols[static_cast<std::size_t>(col)];
        float w = 40.0f;
        for (const auto &cell : c.cells)
            w = std::max(
                w, e.text.get(cell.text, e.font, p.font_px, e.viewport.x, e.viewport.y)
                           .advance +
                       p.cell_pad * 2.0f + 8.0f);
        w = std::max(
            w, e.text.get(c.name, e.font, p.font_px, e.viewport.x, e.viewport.y).advance +
                   p.cell_pad * 2.0f + 26.0f);
        t.cols[static_cast<std::size_t>(col)].over =
            std::clamp(w, p.min_col_w, p.max_col_w);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // 6. 归约：命令 → 数据（唯一写数据的地方）
    // ═══════════════════════════════════════════════════════════════════════════

    inline bool apply_click(table &t, view &v, const env &e, const geometry &g,
                            ModifierKey mods, const command &c);

    inline bool apply_drag_begin(table &t, view &v, const geometry &g, const command &c)
    {
        const auto h = hit_test(g, t.rows(), c.pos.x, c.pos.y);
        if (h.r == region::cell && h.row >= 0 && h.col >= 0)
        {
            v.rect_sel = true;
            v.rect_r0 = v.rect_r1 = h.row;
            v.rect_c0 = v.rect_c1 = h.col;
        }
        if (v.press.r == region::separator && v.press.col >= 0)
        {
            v.drag = view::drag_kind::width;
            v.drag_col = v.press.col;
            v.drag_w0 = g.cols[static_cast<std::size_t>(v.drag_col)].w;
        }
        else if (v.press.r == region::header && v.press.col >= 0)
        {
            v.drag = view::drag_kind::column;
            v.drag_pos = v.press.col;
        }
        else if (v.press.r == region::vbar && v.press.thumb)
        {
            v.drag = view::drag_kind::vbar;
            v.drag_grab = static_cast<float>(v.press_pos.y - (g.y0 + g.vb.y));
        }
        else if (v.press.r == region::hbar && v.press.thumb)
        {
            v.drag = view::drag_kind::hbar;
            v.drag_grab = static_cast<float>(v.press_pos.x - (g.x0 + g.hb.x));
        }
        return true;
    }

    inline bool apply_drag_move(table &t, view &v, const env &env, const geometry &g,
                                const command &c)
    {
        bool changed = false;

        if (v.rect_sel)
        {
            const auto h = hit_test(g, t.rows(), c.pos.x, c.pos.y);
            if (h.col >= 0)
                v.rect_c1 = h.col;
            const int last_row = static_cast<int>(v.rows.size()) - 1;
            if (h.row >= 0)
                v.rect_r1 = h.row;
            else if (last_row >= 0)
                v.rect_r1 = std::clamp(v.rect_r1, 0, last_row);
            changed = true;
        }

        switch (v.drag)
        {
        case view::drag_kind::column: {
            const int p = column_at(g, static_cast<float>(c.pos.x - g.x0 + g.scroll_x));
            if (p >= 0 && p != v.drag_pos)
            {
                const int from = v.drag_pos;
                op_move_column(t, v, from, p);
                v.drag_pos = p;
                changed = true;
            }
            break;
        }
        case view::drag_kind::width: {
            if (v.drag_col >= 0 && v.drag_col < static_cast<int>(t.cols.size()))
            {
                t.cols[static_cast<std::size_t>(v.drag_col)].over =
                    std::clamp(v.drag_w0 + static_cast<float>(c.total_dx),
                               env.p.min_col_w, env.p.max_col_w);
                changed = true;
            }
            break;
        }
        case view::drag_kind::vbar: {
            const float span = g.vb.h - g.vb.thumb_len;
            if (span > 1.0f)
            {
                const float k = std::clamp(
                    static_cast<float>(c.pos.y - g.y0 - g.vb.y - v.drag_grab) / span,
                    0.0f, 1.0f);
                changed |= op_scroll_to(v, g, v.scroll_x, k * g.max_y);
            }
            break;
        }
        case view::drag_kind::hbar: {
            const float span = g.hb.w - g.hb.thumb_len;
            if (span > 1.0f)
            {
                const float k = std::clamp(
                    static_cast<float>(c.pos.x - g.x0 - g.hb.x - v.drag_grab) / span,
                    0.0f, 1.0f);
                changed |= op_scroll_to(v, g, k * g.max_x, v.scroll_y);
            }
            break;
        }
        default:
            break;
        }
        return changed;
    }

    inline bool apply_drag_end(table &t, view &v, const command &)
    {
        if (v.rect_sel)
            std::println("[table] rect ({},{})-({},{})", v.rect_r0, v.rect_c0, v.rect_r1,
                         v.rect_c1);
        if (v.drag == view::drag_kind::width && v.drag_col >= 0)
            std::println("[table] column {} width -> {:.0f}", v.drag_col,
                         t.cols[static_cast<std::size_t>(v.drag_col)].over);
        else if (v.drag == view::drag_kind::column)
            std::println("[table] move column to pos {}", v.drag_pos);
        v.drag = view::drag_kind::none;
        v.drag_pos = -1;
        return true;
    }

    inline bool apply_key(table &t, view &v, const env &env, const geometry &g,
                          ModifierKey mods, const command &c)
    {
        const bool shift = mods.hasAll(ModifierKey::Shift());
        const bool ctrl = mods.hasAll(ModifierKey::Control());

        const auto erase_selection = [&]() -> bool {
            const auto a = std::min(v.caret, v.caret_anchor);
            const auto b = std::max(v.caret, v.caret_anchor);
            if (a == b)
                return false;
            v.buf.erase(a, b - a);
            v.caret = a;
            v.caret_anchor = a;
            return true;
        };

        if (v.editing)
        {
            switch (c.key)
            {
            case Key::eENTER:
                return op_commit_edit(t, v);
            case Key::eESCAPE:
                op_cancel_edit(v);
                return true;
            case Key::eTAB: {
                const int er = v.edit_row;
                const int ec = v.edit_col;
                op_commit_edit(t, v);
                op_select_row(v, er, 0);
                v.focus_col = std::clamp(ec + 1, 0, static_cast<int>(t.cols.size()) - 1);
                op_ensure_visible(v, g, env.p,
                                  std::clamp(er, 0, static_cast<int>(v.rows.size()) - 1));
                return true;
            }
            case Key::eLEFT:
                v.caret = utf8_prev(v.buf, v.caret);
                if (!shift)
                    v.caret_anchor = v.caret;
                return true;
            case Key::eRIGHT:
                v.caret = utf8_next(v.buf, v.caret);
                if (!shift)
                    v.caret_anchor = v.caret;
                return true;
            case Key::eHOME:
                v.caret = 0;
                if (!shift)
                    v.caret_anchor = 0;
                return true;
            case Key::eEND:
                v.caret = v.buf.size();
                if (!shift)
                    v.caret_anchor = v.caret;
                return true;
            case Key::eBACKSPACE:
                if (v.caret != v.caret_anchor)
                    return erase_selection();
                if (v.caret > 0)
                {
                    const auto p0 = utf8_prev(v.buf, v.caret);
                    v.buf.erase(p0, v.caret - p0);
                    v.caret = p0;
                    v.caret_anchor = p0;
                    return true;
                }
                return false;
            case Key::eDELETE:
                if (v.caret != v.caret_anchor)
                    return erase_selection();
                if (v.caret < v.buf.size())
                {
                    const auto n2 = utf8_next(v.buf, v.caret);
                    v.buf.erase(v.caret, n2 - v.caret);
                    v.caret_anchor = v.caret;
                    return true;
                }
                return false;
            default:
                return false;
            }
        }

        const int n = static_cast<int>(v.rows.size());
        if (n <= 0)
            return false;
        const int ncol = static_cast<int>(t.cols.size());
        const int page =
            std::max(1, static_cast<int>(g.body_h / (env.p.row_h + env.p.row_gap)));

        const auto move = [&](int row, int col, bool extend) -> bool {
            const int rr = std::clamp(row, 0, n - 1);
            op_select_row(v, rr, extend ? 1 : 0);
            v.focus_col = std::clamp(col, 0, ncol - 1);
            op_ensure_visible(v, g, env.p, rr);
            return true;
        };

        switch (c.key)
        {
        case Key::eUP:
            return move(v.focus_row - 1, v.focus_col, shift);
        case Key::eDOWN:
            return move(v.focus_row + 1, v.focus_col, shift);
        case Key::eLEFT:
            return move(v.focus_row, v.focus_col - 1, shift);
        case Key::eRIGHT:
            return move(v.focus_row, v.focus_col + 1, shift);
        case Key::eTAB:
            if (v.focus_col + 1 < ncol)
                return move(v.focus_row, v.focus_col + 1, shift);
            return move(v.focus_row + 1, 0, false);
        case Key::ePAGE_UP:
            return move(v.focus_row - page, v.focus_col, shift);
        case Key::ePAGE_DOWN:
            return move(v.focus_row + page, v.focus_col, shift);
        case Key::eHOME:
            return move(ctrl ? 0 : v.focus_row, 0, shift);
        case Key::eEND:
            return move(ctrl ? n - 1 : v.focus_row, ncol - 1, shift);
        case Key::eENTER:
            op_start_edit(v, t, v.focus_row, v.focus_col, false);
            return true;
        case Key::eA:
            if (!ctrl)
                return false;
            op_select_all(v);
            return true;
        case Key::eF10:
            if (std::getenv("TABLE_SELFTEST") != nullptr)
            {
                v.rect_sel = true;
                v.rect_r0 = 2;
                v.rect_c0 = 1;
                v.rect_r1 = 4;
                v.rect_c1 = 3;
                return true;
            }
            return false;
        case Key::eC: {
            if (!ctrl)
                return false;
            const auto ids = selected_rows(v);
            if (v.rect_sel)
            {
                const int c0x = std::min(v.rect_c0, v.rect_c1);
                const int c1x = std::max(v.rect_c0, v.rect_c1);
                const int r0 = std::min(v.rect_r0, v.rect_r1);
                const int r1 = std::max(v.rect_r0, v.rect_r1);
                std::println("[table] copy rect ({},{})-({},{})", r0, c0x, r1, c1x);
                int li = 0;
                for (int rr = r0; rr <= r1 && li < static_cast<int>(ids.size());
                     ++rr, ++li)
                {
                    std::print("[table] tsv:");
                    for (int q = c0x; q <= c1x; ++q)
                        std::print("{}{}", q == c0x ? " " : "\t",
                                   t.text(ids[static_cast<std::size_t>(li)], q));
                    std::println("");
                }
                return true;
            }
            std::print("[table] copy {} rows:", ids.size());
            for (auto id : ids)
                std::print(" {}", id);
            std::println("");
            std::print("[table] names:");
            for (auto id : ids)
                std::print(" '{}'", t.text(id, 0));
            std::println("");
            return true;
        }
        case Key::eDELETE:
            if (v.focus_row >= 0 && v.focus_row < n && v.focus_col >= 0 &&
                v.focus_col < ncol)
            {
                t.cols[static_cast<std::size_t>(v.focus_col)]
                    .cells[v.rows[static_cast<std::size_t>(v.focus_row)]]
                    .text.clear();
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    inline bool apply_click(table &t, view &v, const env &e, const geometry &g,
                            ModifierKey mods, const command &c)
    {
        if (v.menu_open)
        {
            const auto mr = menu_rect(g, v.menu_x, v.menu_y);
            const int item = menu_item_at(mr, static_cast<float>(c.pos.x),
                                          static_cast<float>(c.pos.y));
            v.menu_open = false;
            switch (item)
            {
            case 0:
                op_auto_fit(t, e, v.menu_col);
                std::println("[table] menu fit pos={}", v.menu_col);
                break;
            case 1:
                v.sort_col = -1;
                v.sort_dir = 0;
                op_sort(t, v);
                std::println("[table] menu reset-sort");
                break;
            case 2:
                if (v.menu_col >= 0 && v.menu_col < static_cast<int>(t.cols.size()))
                    t.cols[static_cast<std::size_t>(v.menu_col)].over = -1.0f;
                {
                    int visible = 0;
                    for (const auto &col : t.cols)
                        visible += col.over < 0.0f ? 0 : 1;
                    std::println("[table] menu hide pos={} columns={}", v.menu_col,
                                 visible);
                }
                break;
            case 3:
                for (auto &col : t.cols)
                    col.over = 0.0f;
                std::println("[table] menu show-all columns={}",
                             static_cast<int>(t.cols.size()));
                break;
            default:
                break;
            }
            return true;
        }

        const auto h = hit_test(g, t.rows(), c.pos.x, c.pos.y);

        if (v.editing &&
            !(h.r == region::cell && h.row == v.edit_row && h.col == v.edit_col))
            op_commit_edit(t, v);

        const bool dbl = (h.r == region::cell || h.r == region::separator) &&
                         c.time - v.click_t < 320 && h.row == v.click_row &&
                         h.col == v.click_col;
        v.click_t = c.time;
        v.click_row = h.row;
        v.click_col = h.col;

        switch (h.r)
        {
        case region::cell: {
            const bool shift = mods.hasAll(ModifierKey::Shift());
            const bool ctrl = mods.hasAll(ModifierKey::Control());
            op_select_row(v, h.row, ctrl ? 2 : (shift ? 1 : 0));
            v.focus_col = h.col;
            op_ensure_visible(v, g, e.p, h.row);
            if (dbl)
            {
                op_start_edit(v, t, h.row, h.col, false);
                std::println("[table] edit ({},{})", h.row, h.col);
            }
            else
                std::println("[table] select ({},{})", h.row, h.col);
            return true;
        }
        case region::header:
            if (h.col < 0)
                return true;
            if (v.sort_col != h.col)
            {
                v.sort_col = h.col;
                v.sort_dir = 1;
            }
            else if (v.sort_dir > 0)
                v.sort_dir = -1;
            else
            {
                v.sort_col = -1;
                v.sort_dir = 0;
            }
            op_sort(t, v);
            std::println("[table] sort col={} dir={}", v.sort_col, v.sort_dir);
            return true;
        case region::separator:
            if (dbl && h.col >= 0)
            {
                op_auto_fit(t, e, h.col);
                std::println("[table] auto-fit pos={} width={:.0f}", h.col,
                             t.cols[static_cast<std::size_t>(h.col)].over);
            }
            return true;
        case region::vbar:
        case region::hbar:
            if (!h.thumb)
            {
                if (h.r == region::vbar)
                    op_scroll_to(v, g, v.scroll_x,
                                 v.scroll_y + (c.pos.y < g.y0 + g.vb.y + g.vb.thumb_off
                                                   ? -g.body_h * 0.9f
                                                   : g.body_h * 0.9f));
                else
                    op_scroll_to(v, g,
                                 v.scroll_x + (c.pos.x < g.x0 + g.hb.x + g.hb.thumb_off
                                                   ? -g.w * 0.9f
                                                   : g.w * 0.9f),
                                 v.scroll_y);
            }
            return true;
        default:
            return false;
        }
    }

    inline bool apply_wheel(table &t, view &v, const env &env, const geometry &g,
                            ModifierKey mods, const command &c)
    {
        const auto h = hit_test(g, t.rows(), v.pointer.x, v.pointer.y);
        if (!h.inside())
            return false;
        const bool shift = mods.hasAll(ModifierKey::Shift());
        if (shift && g.max_x > 0.0f)
            return op_scroll_to(
                v, g, v.scroll_x - static_cast<float>(c.wheel_delta) * 48.0f, v.scroll_y);
        const float step = env.p.row_h + env.p.row_gap;
        return op_scroll_to(v, g, v.scroll_x,
                            v.scroll_y - static_cast<float>(c.wheel_delta) * step * 3.0f);
    }

    inline bool apply_text(table &t, view &v, std::uint32_t cp)
    {
        if (cp < 32 || cp == 127)
            return false;
        if (!v.editing)
        {
            if (v.focus_row < 0)
                return false;
            op_start_edit(v, t, v.focus_row, v.focus_col, true);
        }
        if (v.caret != v.caret_anchor)
        {
            const auto a = std::min(v.caret, v.caret_anchor);
            const auto b = std::max(v.caret, v.caret_anchor);
            v.buf.erase(a, b - a);
            v.caret = a;
            v.caret_anchor = a;
        }
        std::string piece;
        append_utf8(piece, cp);
        v.buf.insert(v.caret, piece);
        v.caret += piece.size();
        return true;
    }

    // 唯一的写数据入口：命令 → 数据。返回值 = 数据是否变了。
    inline bool apply(table &t, view &v, const env &e, const geometry &g,
                      ModifierKey mods, const command &c)
    {
        switch (c.kind)
        {
        case cmd::none:
            return false;

        case cmd::pointer_move: {
            bool changed = v.pointer != c.pos;
            v.pointer = c.pos;
            if (v.menu_open && !in_box(g, c.pos))
            {
                v.menu_open = false;
                std::println("[table] menu close (outside)");
                changed = true;
            }
            const auto h = hit_test(g, t.rows(), c.pos.x, c.pos.y);
            if (!(h == v.hover))
            {
                v.hover = h;
                changed = true;
            }
            return changed;
        }

        case cmd::pointer_leave:
            if (!v.hover.inside())
                return false;
            v.hover = {};
            return true;

        case cmd::press:
            v.press = hit_test(g, t.rows(), c.pos.x, c.pos.y);
            v.press_pos = c.pos;
            return false;

        case cmd::context_menu: {
            const auto h = hit_test(g, t.rows(), c.pos.x, c.pos.y);
            if (h.r != region::header || h.col < 0)
                return false;
            v.menu_open = true;
            v.menu_col = h.col;
            v.menu_x = static_cast<float>(c.pos.x);
            v.menu_y = static_cast<float>(c.pos.y);
            std::println("[table] menu open pos={}", h.col);
            return true;
        }

        case cmd::click:
            return apply_click(t, v, e, g, mods, c);
        case cmd::drag_begin:
            return apply_drag_begin(t, v, g, c);
        case cmd::drag_move:
            return apply_drag_move(t, v, e, g, c);
        case cmd::drag_end:
            return apply_drag_end(t, v, c);
        case cmd::wheel:
            return apply_wheel(t, v, e, g, mods, c);
        case cmd::key:
            return apply_key(t, v, e, g, mods, c);
        case cmd::text:
            return apply_text(t, v, c.codepoint);
        case cmd::tick: {
            bool changed = false;
            if (!(v.hover == v.hover_prev))
            {
                if (v.tt == 2)
                    std::println("[table] tooltip hide");
                v.hover_prev = v.hover;
                v.tt_t0 = c.time;
                v.tt = v.hover.inside() ? 1 : 0;
                changed = true;
            }
            else if (v.tt == 1 && c.time - v.tt_t0 >= 500)
            {
                v.tt = 2;
                std::println("[table] tooltip show row={} col={} text=\"{}\"",
                             v.hover.row, v.hover.col, tooltip_text(t, v));
                changed = true;
            }
            return changed;
        }
        }
        return false;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // 7. 渲染数据上下文：数据 → 显示列表（纯函数，可单测，无 GPU）
    // ═══════════════════════════════════════════════════════════════════════════

    struct draw_quad
    {
        float x = 0, y = 0, w = 0, h = 0;
        glm::vec4 fill{0.0f};
        glm::vec4 border{0.0f};
        float border_w = 0.0f;
        float radius = 0.0f;
    };

    struct draw_text
    {
        const cell_glyphs *run = nullptr;
        float x = 0.0f;
        float baseline = 0.0f;
        glm::vec4 color{1.0f};
    };

    struct draw_band
    {
        int x = 0, y = 0, w = 0, h = 0;
        std::vector<draw_quad> quads;
        std::vector<draw_text> texts;
    };

    struct frame_data
    {
        std::vector<draw_band> bands; // 按绘制顺序
        std::size_t used = 0;         // 本次用到的 band 数（容量复用）
        bool valid = false;

        void reset() noexcept
        {
            used = 0;
            valid = false;
        }

        draw_band &begin(int bx, int by, int bw, int bh)
        {
            if (used == bands.size())
                bands.emplace_back();
            auto &b = bands[used++];
            b.x = bx;
            b.y = by;
            b.w = bw;
            b.h = bh;
            b.quads.clear();
            b.texts.clear();
            return b;
        }

        [[nodiscard]] std::size_t quad_count() const noexcept
        {
            std::size_t n = 0;
            for (std::size_t i = 0; i < used; ++i)
                n += bands[i].quads.size();
            return n;
        }
        [[nodiscard]] std::size_t text_count() const noexcept
        {
            std::size_t n = 0;
            for (std::size_t i = 0; i < used; ++i)
                n += bands[i].texts.size();
            return n;
        }
    };

    // 数据（table + view + geometry）→ 显示列表。颜色/位置/裁剪全部在这里定下来，
    // 渲染端只剩“上传”一件事；这份显示列表可以脱离 GPU 断言。
    inline void build_frame(frame_data &out, const table &t, const view &v,
                            const prefs &p, const geometry &g, FontSelect &font,
                            text_cache &cache, float vw, float vh)
    {
        out.reset();
        out.valid = g.valid && t.rows() > 0 && v.rows.size() == t.rows();
        if (!out.valid)
            return;

        const int n = g.count();
        const int frozen = g.frozen;
        const float frozen_w = g.frozen_w();
        const float step = p.row_h + p.row_gap;
        const float x0 = g.x0, y0 = g.y0, w = g.w, h = g.h;
        const float vb_w = g.vb_on ? p.bar : 0.0f;
        const float hw = std::max(0.0f, w - frozen_w - vb_w);
        const int p_first = std::max(frozen, g.col0);
        const glm::vec4 accent{0.45f, 0.72f, 1.0f, 0.95f};

        const auto shape = [&](std::string_view s) -> const cell_glyphs & {
            return cache.get(s, font, p.font_px, vw, vh);
        };
        const auto put_quad = [](draw_band &b, float qx, float qy, float qw, float qh,
                                 glm::vec4 fill, float radius, glm::vec4 border = {},
                                 float border_w = 0.0f) {
            if (qw <= 0.5f || qh <= 0.5f)
                return;
            b.quads.push_back(draw_quad{.x = qx,
                                        .y = qy,
                                        .w = qw,
                                        .h = qh,
                                        .fill = fill,
                                        .border = border,
                                        .border_w = border_w,
                                        .radius = radius});
        };
        const auto put_text = [](draw_band &b, const cell_glyphs &cg, float tx,
                                 float baseline, glm::vec4 color) {
            b.texts.push_back(
                draw_text{.run = &cg, .x = tx, .baseline = baseline, .color = color});
        };

        const auto row_bg = [&](int r, bool sel) {
            glm::vec4 bg = (r % 2) ? glm::vec4{0.13f, 0.14f, 0.18f, 0.95f}
                                   : glm::vec4{0.15f, 0.16f, 0.21f, 0.95f};
            if (sel)
                bg = {0.19f, 0.31f, 0.44f, 0.98f};
            else if (v.hover.r == region::cell && v.hover.row == r)
                bg = {0.19f, 0.22f, 0.30f, 0.98f};
            return bg;
        };
        const auto cell_fg = [&](const column &col, std::string_view text, bool sel) {
            glm::vec4 fg{0.86f, 0.88f, 0.93f, 1.0f};
            if (col.name == "Status")
            {
                if (text == "ready")
                    fg = {0.45f, 0.85f, 0.55f, 1.0f};
                else if (text == "busy")
                    fg = {0.95f, 0.75f, 0.35f, 1.0f};
                else
                    fg = {0.95f, 0.45f, 0.45f, 1.0f};
            }
            else if (col.name == "Action")
                fg = {0.55f, 0.72f, 0.95f, 1.0f};
            if (sel)
                fg = glm::min(fg + glm::vec4(0.10f), glm::vec4(1.0f));
            return fg;
        };
        const auto header_fg = [&](int c) {
            return (v.sort_col == c && v.sort_dir != 0)
                       ? glm::vec4{0.72f, 0.86f, 1.0f, 1.0f}
                       : glm::vec4{0.85f, 0.87f, 0.92f, 1.0f};
        };
        const auto sort_arrow = [&](draw_band &b, int c, float cx) {
            if (v.sort_col != c || v.sort_dir == 0)
                return;
            const float ax = cx + p.cell_pad +
                             shape(t.cols[static_cast<std::size_t>(c)].name).advance +
                             8.0f;
            const float ay = y0 + 26.0f - 9.0f;
            for (int k = 0; k < 3; ++k)
            {
                const float bw = 8.0f - static_cast<float>(k) * 2.0f;
                const float by = v.sort_dir > 0 ? ay + static_cast<float>(k) * 3.0f
                                                : ay + static_cast<float>(2 - k) * 3.0f;
                put_quad(b, ax + (8.0f - bw) * 0.5f, by, bw, 2.0f, accent, 1.0f);
            }
        };

        // ── 面板底 ─────────────────────────────────────────────────
        {
            auto &b = out.begin(0, 0, static_cast<int>(vw), static_cast<int>(vh));
            put_quad(b, x0, y0, w, h, {0.10f, 0.11f, 0.14f, 0.97f}, 10.0f);
        }

        // ── 表头：一条代码画两个带 ─────────────────────────────────
        const auto emit_header = [&](int bx, int bw, int c0, int c1, float sx) {
            auto &b =
                out.begin(bx, static_cast<int>(y0), bw, static_cast<int>(p.header_h));
            put_quad(b, static_cast<float>(bx), y0, static_cast<float>(bw), p.header_h,
                     {0.17f, 0.19f, 0.25f, 1.0f}, 10.0f);
            for (int c = c0; c < c1; ++c)
            {
                const auto &box = g.cols[static_cast<std::size_t>(c)];
                if (box.w <= 0.5f)
                    continue;
                const float cx = x0 + box.x - sx;
                if (v.sort_col == c)
                    put_quad(b, cx, y0, box.w - p.pad, p.header_h,
                             {0.45f, 0.72f, 1.0f, 0.10f}, 6.0f);
                if ((v.hover.r == region::header || v.hover.r == region::cell) &&
                    v.hover.col == c)
                    put_quad(b, cx, y0, box.w - p.pad, p.header_h,
                             {1.0f, 1.0f, 1.0f, 0.05f}, 6.0f);
                put_text(b, shape(t.cols[static_cast<std::size_t>(c)].name),
                         cx + p.cell_pad, y0 + 26.0f, header_fg(c));
                sort_arrow(b, c, cx);
            }
            for (int c = std::max(c0, 1); c < std::min(c1 + 1, n); ++c)
            {
                const bool hot =
                    (v.hover.r == region::separator && v.hover.col == c - 1) ||
                    (v.drag == view::drag_kind::width && v.drag_col == c - 1);
                const float sx2 =
                    x0 + g.cols[static_cast<std::size_t>(c)].x - p.pad * 0.5f - sx;
                put_quad(b, hot ? sx2 - 1.0f : sx2, y0 + 6.0f, hot ? 3.0f : 1.0f,
                         p.header_h - 12.0f,
                         hot ? accent : glm::vec4{1.0f, 1.0f, 1.0f, 0.08f}, 0.5f);
            }
        };

        // ── 表体：一条代码画两个带 ─────────────────────────────────
        float caret_x = 0.0f, anchor_x = 0.0f;
        const cell_glyphs *edit_run = nullptr;
        if (v.editing)
        {
            const std::string_view sv{v.buf};
            edit_run = &shape(sv);
            caret_x = v.caret == 0 ? 0.0f : shape(sv.substr(0, v.caret)).advance;
            anchor_x =
                v.caret_anchor == 0 ? 0.0f : shape(sv.substr(0, v.caret_anchor)).advance;
        }

        const auto emit_body = [&](int bx, int bw, int c0, int c1, float sx) {
            auto &b = out.begin(bx, static_cast<int>(y0 + p.header_h), bw,
                                static_cast<int>(std::max(0.0f, g.body_h)));
            for (int r = g.row0; r < g.row1; ++r)
            {
                const float ry =
                    y0 + p.header_h + static_cast<float>(r) * step - g.scroll_y;
                const bool sel = is_selected(v, v.rows[static_cast<std::size_t>(r)]);
                put_quad(b, static_cast<float>(bx), ry, static_cast<float>(bw), p.row_h,
                         row_bg(r, sel), 4.0f);
                for (int c = c0; c < c1; ++c)
                {
                    const auto &box = g.cols[static_cast<std::size_t>(c)];
                    if (box.w <= 0.5f)
                        continue;
                    const float cx = x0 + box.x - sx;
                    const float cw = box.w - p.pad;
                    const bool editing_here =
                        v.editing && v.edit_row == r && v.edit_col == c;
                    const auto &data = t.at(v.rows[static_cast<std::size_t>(r)], c);

                    if (editing_here)
                        put_quad(b, cx, ry, cw, p.row_h, {0.08f, 0.10f, 0.14f, 1.0f},
                                 4.0f);
                    else
                    {
                        if (v.hover.r == region::cell && v.hover.row == r &&
                            v.hover.col == c)
                            put_quad(b, cx, ry, cw, p.row_h, {1.0f, 1.0f, 1.0f, 0.05f},
                                     4.0f);
                        put_text(
                            b, shape(data.text), cx + p.cell_pad, ry + 21.0f,
                            cell_fg(t.cols[static_cast<std::size_t>(c)], data.text, sel));
                    }
                    if (!v.editing && v.focus_row == r && v.focus_col == c)
                        put_quad(b, cx, ry, cw, p.row_h, glm::vec4{0.0f}, 4.0f, accent,
                                 2.0f);
                    if (editing_here && edit_run)
                    {
                        const float a0 = std::min(anchor_x, caret_x);
                        const float b0 = std::max(anchor_x, caret_x);
                        if (b0 - a0 > 0.5f)
                            put_quad(b, cx + p.cell_pad + a0, ry + 3.0f, b0 - a0,
                                     p.row_h - 6.0f, {0.45f, 0.72f, 1.0f, 0.28f}, 2.0f);
                        put_text(b, *edit_run, cx + p.cell_pad, ry + 21.0f,
                                 {1.0f, 1.0f, 1.0f, 1.0f});
                        put_quad(b, cx + p.cell_pad + caret_x, ry + 6.0f, 1.5f,
                                 p.row_h - 12.0f, accent, 0.75f);
                        put_quad(b, cx, ry, cw, p.row_h, glm::vec4{0.0f}, 4.0f, accent,
                                 2.0f);
                    }
                }
            }
        };

        if (frozen > 0)
            emit_header(static_cast<int>(x0), static_cast<int>(frozen_w), 0,
                        std::min(frozen, n), 0.0f);
        emit_header(static_cast<int>(x0 + frozen_w), static_cast<int>(hw), p_first,
                    g.col1, g.scroll_x);
        if (frozen > 0)
            emit_body(static_cast<int>(x0), static_cast<int>(frozen_w), 0,
                      std::min(frozen, n), 0.0f);
        emit_body(static_cast<int>(x0 + frozen_w), static_cast<int>(hw), p_first, g.col1,
                  g.scroll_x);

        // ── 滚动条 / 外框 / 浮层 ───────────────────────────────────
        {
            auto &b = out.begin(0, 0, static_cast<int>(vw), static_cast<int>(vh));
            if (g.vb_on)
            {
                const bool hot = (v.hover.r == region::vbar && v.hover.thumb) ||
                                 v.drag == view::drag_kind::vbar;
                put_quad(b, x0 + g.vb.x, y0 + g.vb.y, g.vb.w, g.vb.h, {1, 1, 1, 0.06f},
                         g.vb.w * 0.5f);
                put_quad(b, x0 + g.vb.x, y0 + g.vb.y + g.vb.thumb_off, g.vb.w,
                         g.vb.thumb_len,
                         hot ? glm::vec4{0.62f, 0.80f, 1.0f, 0.85f}
                             : glm::vec4{0.55f, 0.72f, 0.95f, 0.55f},
                         g.vb.w * 0.5f);
            }
            if (g.hb_on)
            {
                const bool hot = (v.hover.r == region::hbar && v.hover.thumb) ||
                                 v.drag == view::drag_kind::hbar;
                put_quad(b, x0 + g.hb.x, y0 + g.hb.y, g.hb.w, g.hb.h, {1, 1, 1, 0.06f},
                         g.hb.h * 0.5f);
                put_quad(b, x0 + g.hb.x + g.hb.thumb_off, y0 + g.hb.y, g.hb.thumb_len,
                         g.hb.h,
                         hot ? glm::vec4{0.62f, 0.80f, 1.0f, 0.85f}
                             : glm::vec4{0.55f, 0.72f, 0.95f, 0.55f},
                         g.hb.h * 0.5f);
            }
            const glm::vec4 line{1, 1, 1, 0.14f};
            put_quad(b, x0, y0, w, 1.0f, line, 0.0f);
            put_quad(b, x0, y0 + h - 1.0f, w, 1.0f, line, 0.0f);
            put_quad(b, x0, y0, 1.0f, h, line, 0.0f);
            put_quad(b, x0 + w - 1.0f, y0, 1.0f, h, line, 0.0f);

            if (v.tt == 2 && v.hover.inside() && !v.menu_open)
            {
                const std::string tip = tooltip_text(t, v);
                const auto &tg = shape(tip);
                const float pad = 8.0f;
                const float tw = tg.advance + pad * 2.0f;
                const float th = 24.0f;
                const auto to = overlay_at(static_cast<float>(v.pointer.x),
                                           static_cast<float>(v.pointer.y), tw, th, x0,
                                           y0, w, h, 12.0f, 14.0f);
                put_quad(b, to.x, to.y, tw, th, {0.08f, 0.09f, 0.12f, 0.97f}, 4.0f,
                         {1.0f, 1.0f, 1.0f, 0.15f}, 1.0f);
                put_text(b, tg, to.x + pad, to.y + 17.0f, {0.92f, 0.94f, 0.98f, 1.0f});
            }

            if (v.menu_open)
            {
                const auto mr = menu_rect(g, v.menu_x, v.menu_y);
                put_quad(b, mr.x, mr.y, mr.w, mr.h, {0.12f, 0.13f, 0.17f, 0.99f}, 6.0f,
                         {1.0f, 1.0f, 1.0f, 0.12f}, 1.0f);
                const int hot = menu_item_at(mr, static_cast<float>(v.pointer.x),
                                             static_cast<float>(v.pointer.y));
                if (hot >= 0)
                    put_quad(b, mr.x + 4.0f,
                             mr.y + 4.0f + static_cast<float>(hot) * kMenuRow,
                             mr.w - 8.0f, kMenuRow, {0.45f, 0.72f, 1.0f, 0.22f}, 4.0f);
                for (int i = 0; i < kMenuItemCount; ++i)
                    put_text(b, shape(kMenuLabels[i]), mr.x + 12.0f,
                             mr.y + 18.0f + static_cast<float>(i) * kMenuRow,
                             {0.90f, 0.92f, 0.96f, 1.0f});
            }
        }
    }

    // 显示列表 → 实例。没有任何判断，参数只有“画什么”。
    inline void emit_frame(render_context &ctx, const frame_data &f, float vw, float vh)
    {
        std::vector<shader_data::Cell> cells;
        std::vector<shader_data::Glyph> glyphs;
        cells.reserve(256);
        glyphs.reserve(512);

        for (std::size_t i = 0; i < f.used; ++i)
        {
            const auto &band = f.bands[i];
            ctx.drawRecorder.setScissor(
                VkRect2D{.offset = {band.x, band.y},
                         .extent = {static_cast<uint32_t>(std::max(0, band.w)),
                                    static_cast<uint32_t>(std::max(0, band.h))}});
            cells.clear();
            glyphs.clear();
            for (const auto &q : band.quads)
            {
                if (q.w <= 0.5f || q.h <= 0.5f)
                    continue;
                shader_data::Cell c{};
                c.quad = glm::vec4(((q.x + q.w * 0.5f) / vw) * 2.0f - 1.0f,
                                   ((q.y + q.h * 0.5f) / vh) * 2.0f - 1.0f,
                                   (q.w / vw) * 2.0f, (q.h / vh) * 2.0f);
                c.px = glm::vec2(q.w, q.h);
                c.fill = q.fill;
                c.border = q.border;
                c.border_w = q.border_w;
                c.radius = q.radius;
                cells.push_back(c);
            }
            for (const auto &t : band.texts)
            {
                if (!t.run)
                    continue;
                const float dx = t.x / vw * 2.0f;
                const float dy = t.baseline / vh * 2.0f;
                for (auto gl : t.run->glyphs)
                {
                    gl.model[3][0] += dx;
                    gl.model[3][1] += dy;
                    gl.color = t.color;
                    gl.modulateFlag = 1;
                    gl.hover_fn = ~0u;
                    glyphs.push_back(gl);
                }
            }
            if (!cells.empty())
                ctx.drawRecorder.addInstances(std::span<const shader_data::Cell>{cells});
            if (!glyphs.empty())
                ctx.drawRecorder.addInstances(
                    std::span<const shader_data::Glyph>{glyphs});
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // 8. 数据源：JSON → table（数据进来只有这一个口）
    // ═══════════════════════════════════════════════════════════════════════════

    struct column_spec
    {
        std::string_view name;
        float width = 0.0f;
        float weight = 0.0f;
    };

    inline table load(const nlohmann::json &doc, std::span<const column_spec> specs)
    {
        table t;
        t.cols.resize(specs.size());
        t.notes.resize(doc.size());

        std::vector<std::string> keys;
        keys.reserve(specs.size());
        for (const auto &s : specs)
            keys.emplace_back(s.name);

        for (std::size_t c = 0; c < specs.size(); ++c)
        {
            auto &col = t.cols[c];
            col.name = keys[c];
            col.width = specs[c].width;
            col.weight = specs[c].weight;
            col.cells.resize(doc.size());
        }

        for (std::size_t r = 0; r < doc.size(); ++r)
        {
            const auto &row = doc[r];
            const auto cells_it = row.find("cells");
            for (std::size_t c = 0; c < specs.size(); ++c)
            {
                auto &dst = t.cols[c].cells[r];
                dst.text = row.at(keys[c]).get<std::string>();
                if (cells_it != row.end() && cells_it->contains(keys[c]))
                    dst.note = cells_it->at(keys[c]).get<std::string>();
            }
            if (const auto note_it = row.find("tooltip"); note_it != row.end())
                t.notes[r] = note_it->get<std::string>();
        }
        return t;
    }

    inline nlohmann::json demo_rows()
    {
        static const char *kTableJson = R"json([
  {"Name":"atlas.msdf#0","Status":"ready","Size":"12.5 KB","Modified":"2026-10-10","Action":"edit"},
  {"Name":"ui.frag#1","Status":"busy","Size":"19.5 KB","Modified":"2026-10-11","Action":"open"},
  {"Name":"table.vert#2","Status":"ready","Size":"26.5 KB","Modified":"2026-10-12","Action":"open"},
  {"Name":"scene.gltf#3","Status":"error","Size":"33.5 KB","Modified":"2026-10-13","Action":"edit"},
  {"Name":"font.json#4","Status":"ready","Size":"40.5 KB","Modified":"2026-10-14","Action":"open"},
  {"Name":"atlas.msdf#5","Status":"ready","Size":"47.5 KB","Modified":"2026-10-15","Action":"open","tooltip":"pinned by CI","cells":{"Size":"cache: 3 blocks"}},
  {"Name":"ui.frag#6","Status":"busy","Size":"54.5 KB","Modified":"2026-10-16","Action":"edit"},
  {"Name":"table.vert#7","Status":"ready","Size":"61.5 KB","Modified":"2026-10-17","Action":"open"},
  {"Name":"scene.gltf#8","Status":"error","Size":"68.5 KB","Modified":"2026-10-18","Action":"open"},
  {"Name":"font.json#9","Status":"ready","Size":"75.5 KB","Modified":"2026-10-19","Action":"edit"},
  {"Name":"atlas.msdf#10","Status":"ready","Size":"82.5 KB","Modified":"2026-10-20","Action":"open"},
  {"Name":"ui.frag#11","Status":"busy","Size":"89.5 KB","Modified":"2026-10-21","Action":"open"},
  {"Name":"table.vert#12","Status":"ready","Size":"96.5 KB","Modified":"2026-10-22","Action":"edit"},
  {"Name":"scene.gltf#13","Status":"error","Size":"103.5 KB","Modified":"2026-10-23","Action":"open"},
  {"Name":"font.json#14","Status":"ready","Size":"110.5 KB","Modified":"2026-10-24","Action":"open"},
  {"Name":"atlas.msdf#15","Status":"ready","Size":"117.5 KB","Modified":"2026-10-25","Action":"edit"},
  {"Name":"ui.frag#16","Status":"busy","Size":"124.5 KB","Modified":"2026-10-26","Action":"open"},
  {"Name":"table.vert#17","Status":"ready","Size":"131.5 KB","Modified":"2026-10-27","Action":"open"}
])json";
        return nlohmann::json::parse(kTableJson);
    }

    inline constexpr column_spec demo_columns[] = {
        {"Name", 220.0f, 0.0f},     {"Status", 0.0f, 1.0f},  {"Size", 80.0f, 0.0f},
        {"Modified", 130.0f, 0.0f}, {"Action", 70.0f, 0.0f},
    };

    inline table demo_table()
    {
        return load(demo_rows(), demo_columns);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // 9. 事件 Trait（输入契约）
    // ═══════════════════════════════════════════════════════════════════════════

    struct table_click_payload
    {
        double x{}, y{};
        intent::time_point at{};
    };

    struct TableClickTrait
        : gesture::trait_base<table_click_payload, static_string{"click"},
                              static_string{"on_click"}, app::policy::click>
    {
        static void collect(intent::time_point, payload_t &d,
                            const gesture::click_event &e) noexcept
        {
            d = {e.x, e.y, e.at};
        }
    };

    struct table_drag_payload
    {
        gesture::drag_phase phase{};
        double x{}, y{}, dx{}, dy{}, total_dx{}, total_dy{};
    };

    struct TableDragTrait
        : gesture::trait_base<table_drag_payload, static_string{"drag"},
                              static_string{"on_drag"}, app::policy::drag>
    {
        static void collect(intent::time_point, payload_t &d,
                            const gesture::drag_event &e) noexcept
        {
            d = {e.phase, e.x, e.y, e.dx, e.dy, e.total_dx, e.total_dy};
        }
    };

    struct table_wheel_payload
    {
        int delta{};
    };

    struct TableWheelTrait
        : gesture::trait_base<table_wheel_payload, static_string{"wheel"},
                              static_string{"on_wheel"}, app::policy::wheel>
    {
        static void collect(intent::time_point, payload_t &d,
                            const gesture::wheel_event &e) noexcept
        {
            d = {e.delta};
        }
    };

    struct table_key_payload
    {
        Key key{};
        input_frame::Transition transition{};
    };

    struct TableKeyTrait
        : gesture::trait_base<table_key_payload, static_string{"table_key"},
                              static_string{"on_key"}, app::policy::hover>
    {
        static void collect(intent::time_point, payload_t &d,
                            const gesture::key_event &e) noexcept
        {
            d = {e.key, e.transition};
        }
    };

    using table_event_tree =
        intent::all_of<intent::leaf<gesture::click_template<TableClickTrait>>,
                       intent::leaf<gesture::drag_template<TableDragTrait>>,
                       intent::leaf<gesture::wheel_template<TableWheelTrait>>,
                       intent::leaf<gesture::key_observer_template<TableKeyTrait>>>;

    // ═══════════════════════════════════════════════════════════════════════════
    // 10. 对象：数据 + 三个入口（输入 / 时间 / 渲染）
    // ═══════════════════════════════════════════════════════════════════════════

    struct host
    {
        ui_new::Widget *owner = nullptr;
        ui_new::ScreenWidget *screen = nullptr;
        glm::vec2 viewport{-1.0f, -1.0f};
    };

    // 自测用的“只打印一次”标记（不是交互数据）
    struct trace_flags
    {
        bool outside = false;
        bool frozen = false;
    };

    inline auto make_table(table data, FontSelect *font)
    {
        view v;
        v.rows.resize(data.rows());
        for (std::size_t i = 0; i < v.rows.size(); ++i)
            v.rows[i] = static_cast<std::uint32_t>(i);

        return make_aggregate<"Table">(
            // ── 数据 / 派生缓存 ────────────────────────────────────
            field<"table">(std::move(data)), field<"view">(std::move(v)),
            field<"geom">(geometry{}), field<"frame">(frame_data{}),
            field<"prefs">(prefs{}), field<"font">(font), field<"host">(host{}),
            field<"text">(text_cache{}), field<"trace">(trace_flags{}),
            field<"ticks">(std::uint32_t{0}),

            // ── 输入 ────────────────────────────────────────────────
            field<"input_state">(input_frame::event_context{}),
            field<"arbiter">(intent::debug_arbiter<table_event_tree>{}),
            field<"input_binding">(glfw_input_translation::scoped_binding{}),

            method<"invalidate">([](auto &self) noexcept {
                if (auto *s = self.host.screen)
                    for (std::uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; ++f)
                        s->makeRenderDirty(f);
            }),

            // ── 派生一步：数据 → 几何（render / apply 共用）──────────
            method<"sync">([](auto &self) -> bool {
                if (self.view.rows.size() != self.table.rows())
                {
                    self.view.rows.resize(self.table.rows());
                    for (std::size_t i = 0; i < self.view.rows.size(); ++i)
                        self.view.rows[i] = static_cast<std::uint32_t>(i);
                    self.view.selected.clear();
                }
                const auto &h = self.host;
                const float vw = h.viewport.x, vh = h.viewport.y;
                float px0 = 0.0f, py0 = 0.0f, pw = vw, ph = vh;
                if (h.owner)
                {
                    const float w = static_cast<float>(h.owner->size.width);
                    const float ht = static_cast<float>(h.owner->size.height);
                    if (w > 0.0f && ht > 0.0f)
                    {
                        px0 = static_cast<float>(h.owner->offset.x);
                        py0 = static_cast<float>(h.owner->offset.y);
                        pw = w;
                        ph = ht;
                    }
                }
                if (pw <= 0.0f || ph <= 0.0f)
                {
                    self.geom.valid = false;
                    return false;
                }
                const auto &p = self.prefs;
                const float x0 = px0 + p.margin;
                const float y0 = py0 + p.top;
                const float w = std::min(pw - p.margin * 2.0f, vw - x0 - p.margin);
                const float ht = std::min(ph - p.top - p.margin, vh - y0 - p.margin);
                if (w < 260.0f || ht < 160.0f)
                {
                    self.geom.valid = false;
                    return false;
                }
                solve(self.geom, self.table, self.view, p, x0, y0, w, ht);
                return self.geom.valid;
            }),

            // ── 事件 → 命令 → 数据。只有一个写入口。────────────────
            method<"submit">([](auto &self, command c) -> bool {
                if (!self.font || !self.template invoke<"sync">())
                    return false;
                const env e{self.prefs, *self.font, self.text, self.host.viewport};
                if (!apply(self.table, self.view, e, self.geom, self.input_state.modifier,
                           c))
                    return false;
                self.template invoke<"invalidate">();
                return true;
            }),

            // ── 时间：也是一条命令（tick），只改数据 ────────────────
            method<"tick">([](auto &self, double now) {
                ++self.ticks;
                if (self.ticks == 100)
                    std::println("[table] tick 100");
                self.template invoke<"submit">(
                    command{.kind = cmd::tick, .time = static_cast<std::int64_t>(now)});
            }),

            // ── 语义事件（arbiter 发出来的）→ 命令 ─────────────────
            method<"on_click">([](auto &self, const table_click_payload &e) {
                self.template invoke<"submit">(
                    command{.kind = cmd::click, .pos = {e.x, e.y}, .time = e.at.count()});
            }),
            method<"on_drag">([](auto &self, const table_drag_payload &e) {
                command c{};
                c.pos = {e.x, e.y};
                c.total_dx = e.total_dx;
                c.total_dy = e.total_dy;
                switch (e.phase)
                {
                case gesture::drag_phase::begin:
                    c.kind = cmd::drag_begin;
                    break;
                case gesture::drag_phase::move:
                    c.kind = cmd::drag_move;
                    break;
                case gesture::drag_phase::end:
                    c.kind = cmd::drag_end;
                    break;
                }
                self.template invoke<"submit">(c);
            }),
            method<"on_wheel">([](auto &self, const table_wheel_payload &e) {
                self.template invoke<"submit">(
                    command{.kind = cmd::wheel, .wheel_delta = e.delta});
            }),
            method<"on_key">([](auto &self, const table_key_payload &e) {
                if (e.transition != input_frame::Transition::Press)
                    return;
                self.template invoke<"submit">(command{.kind = cmd::key, .key = e.key});
            }),

            // ── 原始事件：路由（只组装命令，不改数据）──────────────
            method<"input_event">([](auto &self, auto e) noexcept {
                using T = std::remove_cvref_t<decltype(e)>;
                using namespace mcs::vulkan::event;

                if constexpr (std::same_as<T, position2d_event>)
                {
                    // 指针坐标先变成数据（pointer_move 会写 view.pointer），
                    // 下面的闸门用到的就是刚更新过的坐标。
                    self.template invoke<"submit">(
                        command{.kind = cmd::pointer_move, .pos = {e.xpos, e.ypos}});
                }

                // 指针事件只处理落在表内的；拖拽/编辑中放行，否则会丢 end。
                if constexpr (std::same_as<T, position2d_event> ||
                              std::same_as<T, mousebutton_event> ||
                              std::same_as<T, scroll_event>)
                {
                    if (!self.template invoke<"sync">())
                        return;
                    if (!in_box(self.geom, self.view.pointer) &&
                        self.view.drag == view::drag_kind::none && !self.view.editing)
                    {
                        if (!self.trace.outside)
                        {
                            self.trace.outside = true;
                            std::println("[table] ignore outside ({:.0f},{:.0f})",
                                         self.view.pointer.x, self.view.pointer.y);
                        }
                        return;
                    }
                }

                if constexpr (std::same_as<T, cursor_enter_event>)
                {
                    if (!e.value)
                        self.template invoke<"submit">(
                            command{.kind = cmd::pointer_leave});
                }
                else if constexpr (std::same_as<T, mousebutton_event>)
                {
                    if (e.press() && !e.repeat())
                    {
                        if (e.button == MouseButtons::eMOUSE_BUTTON_LEFT)
                            self.template invoke<"submit">(
                                command{.kind = cmd::press, .pos = self.view.pointer});
                        else if (e.button == MouseButtons::eMOUSE_BUTTON_RIGHT)
                            self.template invoke<"submit">(command{
                                .kind = cmd::context_menu, .pos = self.view.pointer});
                    }
                }
                else if constexpr (std::same_as<T, mcs::vulkan::event::char_event>)
                {
                    self.template invoke<"submit">(
                        command{.kind = cmd::text, .codepoint = e.codepoint});
                }

                using frame_t = decltype(input_frame::build_change(self.input_state, e));
                if constexpr (!std::is_void_v<frame_t>)
                {
                    auto f = input_frame::build_change(self.input_state, e);
                    self.arbiter.feed(self, f, app::now_ms());
                    input_frame::apply_change(self.input_state, f);
                }
            }),

            // ── 渲染：派生 + 上传（没有任何业务判断）────────────────
            method<"render">([](auto &self, ui_new::ScreenWidget *screen,
                                ui_new::Widget *owner, render_context &ctx) {
                self.host.screen = screen;
                self.host.owner = owner;
                if (!ctx.drawRecorder.hasViewport())
                    return;
                const auto vp = ctx.drawRecorder.currentViewport();
                const float vw = vp.width;
                const float vh = vp.height;
                self.host.viewport = glm::vec2{vw, vh};
                if (!self.font || self.table.rows() == 0)
                {
                    self.frame.valid = false;
                    return;
                }
                if (!self.template invoke<"sync">())
                {
                    self.frame.valid = false;
                    return;
                }

                // 自测断言点：冻结列不随 scroll_x 移动
                if (self.geom.scroll_x > 0.0f && !self.trace.frozen)
                {
                    self.trace.frozen = true;
                    std::println("[table] frozen x={:.0f} w={:.0f} scroll={:.0f}",
                                 self.geom.x0 + self.geom.cols[0].x, self.geom.frozen_w(),
                                 self.geom.scroll_x);
                }

                build_frame(self.frame, self.table, self.view, self.prefs, self.geom,
                            *self.font, self.text, vw, vh);
                if (!self.frame.valid)
                    return;
                emit_frame(ctx, self.frame, vw, vh);
            }));
    }

} // namespace table_demo
