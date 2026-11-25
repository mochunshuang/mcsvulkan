#pragma once
// ============================================================================
// test_ui_ability10_table.hpp
//
//   table 不是 widget，是一组数据：
//
//     table_data  列主序的数据（列 = 名字 + cells + 显示策略）
//     view        意图（行序/选择/焦点/滚动/编辑/拖拽）
//     geometry    派生：列盒 / 可见行 / 滚动条（纯函数 sync）
//     frame_data  派生：quads / texts / zones（纯函数 build_frame）
//     prefs       样式与开关
//
//   操作由数据唯一确定：
//     sync(data, view, prefs)      -> geometry
//     hit_test(geometry, x, y)     -> zone/hit
//     apply(data, view, prefs, g, command)   // 唯一写数据
//     build_frame(data, view, prefs, g)      -> frame_data
//
//   本文件只依赖 std + glm；ui_new / glmfw / dao / shader 都不认识。
//   make_table / asRenderObject / encode 在 cpp 的宿主层。
// ============================================================================

#include <algorithm>
#include <array>
#include <cassert>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <numeric>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

namespace ui_table
{
    // ── 什么是 table ──────────────────────────────────────────────────────
    //   table = 数据 + 组件 + 算法；不是 widget。
    //     data     table_data（列/行/主键 row_id）
    //     comp     组件状态（见下）
    //     derive   data + comp -> geometry（列盒、可见行、滚动区）
    //     present  data + comp + geometry -> frame_data（DL + zones）
    //     sys      算法；input 只做“事件/手势 -> 系统调用”
    //
    // ── table 的组件（comp::）─────────────────────────────────────────────
    //   view = scroll + sort + filter + selection + checks + columns
    //        + history + find + menu + tooltip + ui + edit
    //   每个组件只声明自己的数据与 reset；算法在 sys:: 对应系统里。
    //
    // ── API 分类 ─────────────────────────────────────────────────────────
    //   自定义 API（本文件）：
    //     derive::sync / derive::solve_columns
    //     present::build_frame
    //     sys::systems（handle / rows / paint）
    //     input::hit_test / system_hover / system_wheel / system_text
    //           / system_event / system_gesture
    //   系统 API（外部依赖）：
    //     仅 std + glm；font 由宿主提供 metrics；effect/DL 由宿主消费
    //   宿主/平台 API（不在本文件）：
    //     cpp 适配层使用 GLFW / Vulkan / render / clipboard；
    //     header 不认识平台，保证 table 核心可单独测试
    //
    // ── 数据流 ───────────────────────────────────────────────────────────
    //   input system_event -> sys::systems -> comp
    //   derive::sync(data, comp, prefs) -> geometry
    //   present::build_frame(data, comp, geometry) -> frame_data
    // ─────────────────────────────────────────────────────────────────────

    // ── 基础 ──────────────────────────────────────────────────────────────
    struct rect
    {
        float x{0}, y{0}, w{0}, h{0};
        [[nodiscard]] constexpr bool empty() const noexcept
        {
            return w <= 0.0F || h <= 0.0F;
        }
        [[nodiscard]] constexpr float x1() const noexcept
        {
            return x + w;
        }
        [[nodiscard]] constexpr float y1() const noexcept
        {
            return y + h;
        }
    };

    [[nodiscard]] inline rect intersect(const rect &a, const rect &b) noexcept
    {
        const float l = std::max(a.x, b.x), t = std::max(a.y, b.y);
        const float r = std::min(a.x1(), b.x1()), d = std::min(a.y1(), b.y1());
        if (r <= l || d <= t)
            return {};
        return {l, t, r - l, d - t};
    }
    [[nodiscard]] inline rect clip_x(const rect &r, float x0, float x1) noexcept
    {
        const float l = std::max(r.x, x0), q = std::min(r.x1(), x1);
        if (q <= l)
            return {};
        return {l, r.y, q - l, r.h};
    }

    struct row_id
    {
        std::uint64_t v{0};
        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return v != 0;
        }
        friend constexpr bool operator==(const row_id &, const row_id &) = default;
        friend constexpr auto operator<=>(const row_id &, const row_id &) = default;
    };
    using row_idx = int;
    using col_idx = int;
    // ── 身份：table / column / row / cell ────────────────────────────────
    struct table_id
    {
        std::uint32_t value{};
        friend constexpr bool operator==(const table_id &, const table_id &) = default;
        friend constexpr auto operator<=>(const table_id &, const table_id &) = default;
    };
    struct column_id
    {
        std::uint32_t value{};
        friend constexpr bool operator==(const column_id &, const column_id &) = default;
        friend constexpr auto operator<=>(const column_id &, const column_id &) = default;
    };
    [[nodiscard]] inline table_id next_table_id() noexcept
    {
        static std::atomic<std::uint32_t> n{1};
        return {n.fetch_add(1)};
    }
    struct cell_ref
    {
        row_id row{};
        column_id col{};
        friend constexpr bool operator==(const cell_ref &, const cell_ref &) = default;
    };

    // ── zone ──────────────────────────────────────────────────────────────
    enum class part : std::uint32_t
    {
        none = 0,
        frame,
        header,
        cell,
        col_sep,
        check,
        check_head,
        filter_btn,
        header_group,
        vbar_track,
        vbar_thumb,
        hbar_track,
        hbar_thumb,
        edge_n,
        edge_w,
        edge_ne,
        edge_nw,
        edge_se,
        edge_sw,
        menu_item,
        Count
    };

    struct zone
    {
        part kind{part::none};
        int row{-1};
        int col{-1};
        table_id table{};
        [[nodiscard]] constexpr bool inside() const noexcept
        {
            return kind != part::none;
        }
        friend constexpr bool operator==(const zone &, const zone &) = default;
    };

    struct owner_key
    {
        table_id table{};
        part kind{part::none};
        row_id row{};
        column_id col{};
        friend constexpr bool operator==(const owner_key &, const owner_key &) = default;
    };
    static_assert(std::is_trivially_copyable_v<owner_key>);
    static_assert(sizeof(owner_key) <= 32);
    // target：输入/picking 的强类型目标；zone 是它的几何投影
    struct target
    {
        table_id table{};
        part kind{part::none};
        row_id row{};
        column_id col{};
        int row_index{-1};
        int col_index{-1};
        [[nodiscard]] zone to_zone() const noexcept
        {
            return {kind, row_index, col_index, table};
        }
        friend constexpr bool operator==(const target &, const target &) = default;
    };

    namespace bits
    {
        inline constexpr std::uint32_t kPartShift = 27;
        inline constexpr std::uint32_t kTableShift = 22;
        inline constexpr std::uint32_t kRowShift = 9;
        inline constexpr std::uint32_t kTableMask = 0x1Fu;
        inline constexpr std::uint32_t kRowNone = 0x1FFFu;
        inline constexpr std::uint32_t kColNone = 0x1FFu;
        static_assert(static_cast<std::uint32_t>(part::Count) <= 32, "part > 5bit");
    } // namespace bits

    [[nodiscard]] constexpr std::uint32_t zone_key(const zone &z) noexcept
    {
        const auto r = z.row < 0 ? bits::kRowNone : static_cast<std::uint32_t>(z.row);
        const auto c = z.col < 0 ? bits::kColNone : static_cast<std::uint32_t>(z.col);
        return (static_cast<std::uint32_t>(z.kind) << bits::kPartShift) |
               ((z.table.value & bits::kTableMask) << bits::kTableShift) |
               (r << bits::kRowShift) | c;
    }
    [[nodiscard]] constexpr zone zone_from(std::uint32_t key) noexcept
    {
        zone z{};
        z.kind = static_cast<part>(key >> bits::kPartShift);
        z.table.value = (key >> bits::kTableShift) & bits::kTableMask;
        const auto r = (key >> bits::kRowShift) & bits::kRowNone;
        const auto c = key & bits::kColNone;
        z.row = r == bits::kRowNone ? -1 : static_cast<int>(r);
        z.col = c == bits::kColNone ? -1 : static_cast<int>(c);
        return z;
    }
    [[nodiscard]] inline const char *part_name(part p) noexcept
    {
        switch (p)
        {
        case part::frame:
            return "frame";
        case part::header:
            return "header";
        case part::cell:
            return "cell";
        case part::col_sep:
            return "col_sep";
        case part::check:
            return "check";
        case part::check_head:
            return "check_head";
        case part::filter_btn:
            return "filter_btn";
        case part::header_group:
            return "header_group";
        case part::vbar_track:
            return "vbar_track";
        case part::vbar_thumb:
            return "vbar_thumb";
        case part::hbar_track:
            return "hbar_track";
        case part::hbar_thumb:
            return "hbar_thumb";
        case part::edge_n:
            return "edge_n";
        case part::edge_w:
            return "edge_w";
        case part::edge_ne:
            return "edge_ne";
        case part::edge_nw:
            return "edge_nw";
        case part::edge_se:
            return "edge_se";
        case part::edge_sw:
            return "edge_sw";
        default:
            return "none";
        }
    }

    inline std::uint32_t &hover_fn_ref() noexcept
    {
        static std::uint32_t v = ~0U;
        return v;
    }
    inline bool trace_on() noexcept
    {
        static const bool v = std::getenv("TABLE_TRACE") != nullptr;
        return v;
    }
    template <class... A>
    inline void trace(std::format_string<A...> fmt, A &&...a)
    {
        if (trace_on())
            std::println(fmt, std::forward<A>(a)...);
    }

    enum class cursor : std::uint8_t
    {
        arrow = 0,
        ibeam,
        resize_ew,
        resize_ns,
        resize_nwse,
        resize_nesw,
        hand,
    };
    [[nodiscard]] inline cursor cursor_for(const zone &z) noexcept
    {
        switch (z.kind)
        {
        case part::header:
        case part::header_group:
        case part::check:
        case part::check_head:
        case part::vbar_thumb:
        case part::hbar_thumb:
            return cursor::hand;
        case part::col_sep:
            return cursor::resize_ew;
        case part::edge_n:
            return cursor::resize_ns;
        case part::edge_w:
            return cursor::resize_ew;
        case part::edge_ne:
        case part::edge_sw:
            return cursor::resize_nesw;
        case part::edge_nw:
        case part::edge_se:
            return cursor::resize_nwse;
        default:
            return cursor::arrow;
        }
    }

    // ── 颜色 / 字体 / 文本 / arena ────────────────────────────────────────
    [[nodiscard]] inline glm::vec4 hex_color(std::string_view s, const glm::vec4 &fb)
    {
        if (s.size() != 7 || s[0] != '#')
            return fb;
        const auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        };
        int v[6]{};
        for (int i = 0; i < 6; ++i)
            if ((v[i] = nib(s[static_cast<std::size_t>(i + 1)])) < 0)
                return fb;
        return {static_cast<float>(v[0] * 16 + v[1]) / 255.0F,
                static_cast<float>(v[2] * 16 + v[3]) / 255.0F,
                static_cast<float>(v[4] * 16 + v[5]) / 255.0F, 1.0F};
    }

    struct glyph_metrics
    {
        glm::vec4 uv{0, 0, 1, 1};
        glm::vec4 plane{0};
        float advance{0};
        std::uint32_t tex{0}, samp{0}, font{4};
        float px_range{0};
        bool y_down{false};
    };

    struct font
    {
        float px{15.0F};
        float line_h{1.25F};
        std::array<glyph_metrics, 128> ascii{};
        std::array<std::uint8_t, 128> has{};
        std::unordered_map<char32_t, glyph_metrics> other;
        void put(char32_t cp, const glyph_metrics &m)
        {
            if (cp < 128)
            {
                ascii[static_cast<std::size_t>(cp)] = m;
                has[static_cast<std::size_t>(cp)] = 1;
            }
            else
            {
                other.emplace(cp, m);
            }
        }
        [[nodiscard]] const glyph_metrics *find(char32_t cp) const noexcept
        {
            if (cp < 128)
                return has[static_cast<std::size_t>(cp)]
                           ? &ascii[static_cast<std::size_t>(cp)]
                           : nullptr;
            const auto it = other.find(cp);
            return it == other.end() ? nullptr : &it->second;
        }
    };

    [[nodiscard]] inline std::size_t utf8_decode(std::string_view s,
                                                 char32_t &cp) noexcept
    {
        if (s.empty())
        {
            cp = 0;
            return 0;
        }
        const auto u = static_cast<unsigned char>(s[0]);
        if (u < 0x80)
        {
            cp = u;
            return 1;
        }
        if ((u & 0xE0) == 0xC0 && s.size() >= 2)
        {
            cp = static_cast<char32_t>((u & 0x1F) << 6) |
                 (static_cast<unsigned char>(s[1]) & 0x3F);
            return 2;
        }
        if ((u & 0xF0) == 0xE0 && s.size() >= 3)
        {
            cp = static_cast<char32_t>((u & 0x0F) << 12) |
                 (static_cast<char32_t>(static_cast<unsigned char>(s[1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(s[2]) & 0x3F);
            return 3;
        }
        if ((u & 0xF8) == 0xF0 && s.size() >= 4)
        {
            cp = static_cast<char32_t>((u & 0x07) << 18) |
                 (static_cast<char32_t>(static_cast<unsigned char>(s[1]) & 0x3F) << 12) |
                 (static_cast<char32_t>(static_cast<unsigned char>(s[2]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(s[3]) & 0x3F);
            return 4;
        }
        cp = u;
        return 1;
    }
    [[nodiscard]] inline std::size_t utf8_prev(std::string_view s, std::size_t i) noexcept
    {
        if (i == 0 || s.empty())
            return 0;
        --i;
        while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
            --i;
        return i;
    }
    [[nodiscard]] inline std::size_t utf8_next(std::string_view s, std::size_t i) noexcept
    {
        if (i >= s.size())
            return s.size();
        ++i;
        while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
            ++i;
        return i;
    }
    // 词边界（编辑框用）：字母/数字/下划线/非 ASCII 算词内
    [[nodiscard]] inline bool is_word_byte(char c) noexcept
    {
        const auto u = static_cast<unsigned char>(c);
        return (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') ||
               (u >= 'A' && u <= 'Z') || u == '_' || u >= 0x80;
    }
    [[nodiscard]] inline std::size_t utf8_word_left(std::string_view s,
                                                    std::size_t i) noexcept
    {
        if (i == 0)
            return 0;
        i = utf8_prev(s, i);
        while (i > 0 && !is_word_byte(s[i]))
            i = utf8_prev(s, i);
        while (i > 0)
        {
            const auto prev = utf8_prev(s, i);
            if (!is_word_byte(s[prev]))
                break;
            i = prev;
        }
        return i;
    }
    [[nodiscard]] inline std::size_t utf8_word_right(std::string_view s,
                                                     std::size_t i) noexcept
    {
        while (i < s.size() && !is_word_byte(s[i]))
            i = utf8_next(s, i);
        while (i < s.size() && is_word_byte(s[i]))
            i = utf8_next(s, i);
        return i;
    }

    [[nodiscard]] inline float text_width(const font &f, std::string_view s) noexcept
    {
        float w = 0;
        std::size_t i = 0;
        while (i < s.size())
        {
            char32_t cp = 0;
            i += utf8_decode(s.substr(i), cp);
            if (const auto *g = f.find(cp))
                w += g->advance * f.px;
        }
        return w;
    }

    // 稳定块 arena：reset 后从 0 号块重新用（cur 是"当前块下标"）
    struct arena
    {
        static constexpr std::size_t kBlock = 64 * 1024;
        std::vector<std::unique_ptr<char[]>> blocks;
        std::vector<std::unique_ptr<char[]>> big;
        std::size_t cur{0};
        std::size_t used{0};

        void reset() noexcept
        {
            cur = 0;
            used = 0;
            big.clear();
        }
        [[nodiscard]] std::string_view put(std::string_view s)
        {
            if (s.empty())
                return {};
            if (s.size() > kBlock)
            {
                big.push_back(std::make_unique<char[]>(s.size()));
                std::memcpy(big.back().get(), s.data(), s.size());
                return {big.back().get(), s.size()};
            }
            if (blocks.empty())
                blocks.push_back(std::make_unique<char[]>(kBlock));
            if (used + s.size() > kBlock)
            {
                ++cur;
                if (cur >= blocks.size())
                    blocks.push_back(std::make_unique<char[]>(kBlock));
                used = 0;
            }
            char *dst = blocks[cur].get() + used;
            std::memcpy(dst, s.data(), s.size());
            used += s.size();
            return {dst, s.size()};
        }
    };

    [[nodiscard]] inline std::string_view fit_text(const font &f, std::string_view s,
                                                   float max_w, arena &ar,
                                                   std::string &scratch)
    {
        if (text_width(f, s) <= max_w)
            return s;
        const float ell = text_width(f, "...");
        if (max_w <= ell)
            return ar.put("...");
        std::size_t cut = 0, i = 0;
        float w = 0;
        while (i < s.size())
        {
            char32_t cp = 0;
            const std::size_t n = utf8_decode(s.substr(i), cp);
            const auto *g = f.find(cp);
            const float a = g ? g->advance * f.px : 0;
            if (w + a + ell > max_w)
                break;
            w += a;
            i += n;
            cut = i;
        }
        scratch.assign(s.substr(0, cut));
        scratch += "...";
        return ar.put(scratch);
    }

    inline void wrap_lines(const font &f, std::string_view s, float inner,
                           std::vector<std::string_view> &out)
    {
        out.clear();
        std::size_t start = 0, last_space = std::string_view::npos, i = 0;
        float w = 0;
        while (i < s.size())
        {
            char32_t cp = 0;
            const std::size_t n = utf8_decode(s.substr(i), cp);
            if (cp == U'\n')
            {
                out.push_back(s.substr(start, i - start));
                i += n;
                start = i;
                last_space = std::string_view::npos;
                w = 0;
                continue;
            }
            const auto *g = f.find(cp);
            const float a = g ? g->advance * f.px : 0;
            if (w + a > inner && i > start)
            {
                const std::size_t cut =
                    (last_space != std::string_view::npos && last_space > start)
                        ? last_space
                        : i;
                out.push_back(s.substr(start, cut - start));
                start = cut;
                while (start < s.size() && s[start] == ' ')
                    ++start;
                i = start;
                last_space = std::string_view::npos;
                w = 0;
                continue;
            }
            if (cp == U' ')
                last_space = i;
            w += a;
            i += n;
        }
        out.push_back(s.substr(start));
    }

    // ── 数据 ──────────────────────────────────────────────────────────────
    enum class render_kind : std::uint8_t
    {
        text = 0,
        tag,
        bar,
        dot,
        Count
    };
    enum class align : std::uint8_t
    {
        left = 0,
        center,
        right,
    };

    struct cell
    {
        std::string text;
        std::string note;
    };

    struct column
    {
        column_id id{}; // 稳定列身份（finalize 后 = 1..N）
        std::string key;
        std::string title;
        std::string group;
        float w{140.0F}, min_w{48.0F}, max_w{640.0F}, weight{0.0F};
        align a{align::left};
        bool filterable{true};
        bool wrap{false};
        render_kind render{render_kind::text};
        std::vector<std::pair<std::string, glm::vec4>> tags;
        [[nodiscard]] glm::vec4 tag_color(std::string_view v, const glm::vec4 &fb) const
        {
            for (const auto &[k, c] : tags)
                if (k == v)
                    return c;
            return fb;
        }
    };

    // 列主序：cells[col][row]；ids 是稳定行身份
    struct table_data
    {
        std::vector<column> cols;
        std::vector<std::vector<cell>> cells;
        std::vector<row_id> ids;

        [[nodiscard]] int rows() const noexcept
        {
            return static_cast<int>(ids.size());
        }
        [[nodiscard]] int cols_count() const noexcept
        {
            return static_cast<int>(cols.size());
        }
        void finalize()
        {
            // 只给还没有身份的列补 id：已有 id 永不改（cell_ref / target_key 依赖稳定）
            std::uint32_t max_id = 0;
            for (const auto &c : cols)
                max_id = std::max(max_id, c.id.value);
            for (auto &c : cols)
                if (c.id.value == 0)
                    c.id = column_id{++max_id};
        }
        [[nodiscard]] row_id new_row_id() const noexcept
        {
            std::uint64_t max_id = 0;
            for (const auto &id : ids)
                max_id = std::max(max_id, id.v);
            return row_id{max_id + 1};
        }
        // ── 模型增删：行身份稳定，列身份只补新列 ─────────────────────────
        row_id insert_row(int at = -1)
        {
            if (ids.empty() || at < 0 || at > rows())
                at = rows();
            for (auto &col : cells)
                col.insert(col.begin() + at, cell{});
            const row_id id = new_row_id();
            ids.insert(ids.begin() + at, id);
            return id;
        }
        bool remove_row(int at)
        {
            if (at < 0 || at >= rows())
                return false;
            for (auto &col : cells)
                if (static_cast<std::size_t>(at) < col.size())
                    col.erase(col.begin() + at);
            ids.erase(ids.begin() + at);
            return true;
        }
        column_id insert_column(int at, column c)
        {
            if (at < 0 || at > cols_count())
                at = cols_count();
            std::uint32_t max_id = 0;
            for (const auto &x : cols)
                max_id = std::max(max_id, x.id.value);
            if (c.id.value == 0)
                c.id = column_id{max_id + 1};
            const column_id id = c.id;
            cols.insert(cols.begin() + at, std::move(c));
            cells.insert(cells.begin() + at,
                         std::vector<cell>(static_cast<std::size_t>(rows())));
            return id;
        }
        bool remove_column(int at)
        {
            if (at < 0 || at >= cols_count())
                return false;
            cols.erase(cols.begin() + at);
            cells.erase(cells.begin() + at);
            return true;
        }
        [[nodiscard]] column_id column_of(int c) const noexcept
        {
            return c >= 0 && static_cast<std::size_t>(c) < cols.size()
                       ? cols[static_cast<std::size_t>(c)].id
                       : column_id{};
        }
        [[nodiscard]] cell_ref cell_id(int r, int c) const noexcept
        {
            return {static_cast<std::size_t>(r) < ids.size()
                        ? ids[static_cast<std::size_t>(r)]
                        : row_id{},
                    column_of(c)};
        }
        [[nodiscard]] std::optional<std::pair<int, int>> cell_index(
            cell_ref ref) const noexcept
        {
            const int r = index_of(ref.row);
            if (r < 0)
                return std::nullopt;
            for (std::size_t i = 0; i < cols.size(); ++i)
                if (cols[i].id == ref.col)
                    return std::pair<int, int>{r, static_cast<int>(i)};
            return std::nullopt;
        }
        [[nodiscard]] owner_key owner_of(table_id t, const zone &z) const noexcept
        {
            owner_key k{};
            k.table = t;
            k.kind = z.kind;
            if (z.row >= 0 && static_cast<std::size_t>(z.row) < ids.size())
                k.row = ids[static_cast<std::size_t>(z.row)];
            k.col = column_of(z.col);
            return k;
        }
        [[nodiscard]] std::string_view text(int r, int c) const noexcept
        {
            if (c < 0 || r < 0 || static_cast<std::size_t>(c) >= cells.size() ||
                static_cast<std::size_t>(r) >= cells[static_cast<std::size_t>(c)].size())
                return {};
            return cells[static_cast<std::size_t>(c)][static_cast<std::size_t>(r)].text;
        }
        [[nodiscard]] const cell &at(int r, int c) const noexcept
        {
            return cells[static_cast<std::size_t>(c)][static_cast<std::size_t>(r)];
        }
        void set_cell(int r, int c, std::string s)
        {
            if (c < 0 || r < 0 || static_cast<std::size_t>(c) >= cells.size())
                return;
            auto &col = cells[static_cast<std::size_t>(c)];
            if (static_cast<std::size_t>(r) >= col.size())
                return;
            col[static_cast<std::size_t>(r)].text = std::move(s);
        }
        [[nodiscard]] int index_of(row_id id) const noexcept
        {
            for (std::size_t i = 0; i < ids.size(); ++i)
                if (ids[i] == id)
                    return static_cast<int>(i);
            return -1;
        }
        [[nodiscard]] bool any_group() const noexcept
        {
            for (const auto &c : cols)
                if (!c.group.empty())
                    return true;
            return false;
        }
    };

    [[nodiscard]] inline target make_target(part kind, int row, int col,
                                            table_id table = {}) noexcept
    {
        return {table, kind, {}, {}, row, col};
    }
    [[nodiscard]] inline target target_of(const table_data &d, table_id t,
                                          const zone &z) noexcept
    {
        const owner_key k = d.owner_of(t, z);
        return {k.table, k.kind, k.row, k.col, z.row, z.col};
    }

    // picking 协议：entity_index 就是 target_key；resolve 得到完整 target
    using target_key = std::uint32_t;
    [[nodiscard]] inline target_key key_of(const target &t) noexcept
    {
        return zone_key(t.to_zone());
    }
    [[nodiscard]] inline target resolve(const table_data &d, target_key key) noexcept
    {
        const zone z = zone_from(key);
        return target_of(d, z.table, z);
    }

    // ── owner：每类 part 一个类型，visit_owner 是唯一分派点 ───────────────
    struct none_owner
    {
        table_id table{};
    };
    struct frame_owner
    {
        table_id table{};
    };
    struct header_owner
    {
        table_id table{};
        column_id col{};
        int col_index{-1};
    };
    struct sep_owner
    {
        table_id table{};
        column_id col{};
        int col_index{-1};
    };
    struct filter_owner
    {
        table_id table{};
        column_id col{};
        int col_index{-1};
    };
    struct group_owner
    {
        table_id table{};
        column_id col{};
        int col_index{-1};
    };
    struct cell_owner
    {
        table_id table{};
        row_id row{};
        column_id col{};
        int row_index{-1};
        int col_index{-1};
    };
    struct check_owner
    {
        table_id table{};
        row_id row{};
        int row_index{-1};
    };
    struct check_all_owner
    {
        table_id table{};
    };
    struct bar_owner
    {
        table_id table{};
        part kind{part::none};
    };
    struct edge_owner
    {
        table_id table{};
        part kind{part::none};
    };
    struct menu_owner
    {
        table_id table{};
        int index{-1};
    };
    using owner = std::variant<none_owner, frame_owner, header_owner, sep_owner,
                               filter_owner, group_owner, cell_owner, check_owner,
                               check_all_owner, bar_owner, edge_owner, menu_owner>;

    [[nodiscard]] inline owner to_owner(const target &t) noexcept
    {
        switch (t.kind)
        {
        case part::cell:
            return cell_owner{t.table, t.row, t.col, t.row_index, t.col_index};
        case part::check:
            return check_owner{t.table, t.row, t.row_index};
        case part::check_head:
            return check_all_owner{t.table};
        case part::header:
            return header_owner{t.table, t.col, t.col_index};
        case part::col_sep:
            return sep_owner{t.table, t.col, t.col_index};
        case part::filter_btn:
            return filter_owner{t.table, t.col, t.col_index};
        case part::header_group:
            return group_owner{t.table, t.col, t.col_index};
        case part::menu_item:
            return menu_owner{t.table, t.col_index};
        case part::frame:
            return frame_owner{t.table};
        case part::vbar_track:
        case part::vbar_thumb:
        case part::hbar_track:
        case part::hbar_thumb:
            return bar_owner{t.table, t.kind};
        case part::edge_n:
        case part::edge_w:
        case part::edge_ne:
        case part::edge_nw:
        case part::edge_se:
        case part::edge_sw:
            return edge_owner{t.table, t.kind};
        default:
            return none_owner{t.table};
        }
    }

    template <class... Fs>
    struct overloaded : Fs...
    {
        using Fs::operator()...;
    };
    template <class... Fs>
    overloaded(Fs...) -> overloaded<Fs...>;

    template <class... Fs>
    decltype(auto) visit_owner(const owner &o, Fs &&...fs)
    {
        return std::visit(overloaded{std::forward<Fs>(fs)...}, o);
    }

    struct prefs
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

    // ── view：意图 ────────────────────────────────────────────────────────
    enum class drag_mode : std::uint8_t
    {
        none,
        col_w,
        vthumb,
        hthumb,
        table_size,
        select,
        header,
    };

    // ═ 组件层 comp：数据 + 不变量 + reset ════════════════════════════════
    namespace comp
    {
        struct edit_state
        {
            int row{-1}, col{-1};
            cell_ref cell{};
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

        // ── 组件：每份数据只属于一个组件 ─────────────────────────────────────
        struct scroll_state
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
        struct sort_state
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
        struct filter_state
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
        struct selection_state
        {
            int cur_r{-1}, cur_c{-1};
            int r0{-1}, c0{-1}, r1{-1}, c1{-1};
            cell_ref cur_cell{}, r0_cell{}, r1_cell{};
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
        struct checks_state
        {
            std::vector<std::uint8_t> on;
            std::vector<row_id> ids;
            int last{-1};
            void reset(int rows)
            {
                on.assign(static_cast<std::size_t>(rows), 0);
                ids.assign(static_cast<std::size_t>(rows), {});
                last = -1;
            }
        };
        struct columns_state
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

            void reset(const table_data &d)
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
        struct history_state
        {
            struct undo_change
            {
                row_id id{};
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
        struct find_state
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
        struct menu_state
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
        struct tooltip_state
        {
            double hover_t{-1};
            void reset() noexcept
            {
                hover_t = -1;
            }
        };
        struct ui_state
        {
            target hover{};
            int band_row{-1}, band_col{-1};
            glm::vec2 pointer{0, 0};
            bool left{false};
            double now{0};
            drag_mode drag{drag_mode::none};

            float drag_base{0}, drag_base2{0}, drag_start_x{0}, drag_start_y{0};
            target drag_target{};
            float trs_x0{0}, trs_y0{0}, trs_w{0}, trs_h{0};
            part trs_edge{part::none}; // 哪个把手在缩放整表
            double last_click_t{-1.0e9};
            glm::vec2 last_click_pos{0, 0};
            int click_count{0};
            bool header_moved{false};
            std::uint32_t version{1};

            void touch() noexcept
            {
                ++version;
            }
            void reset() noexcept
            {
                hover = {};
                band_row = band_col = -1;
                pointer = {0, 0};
                left = false;
                drag = drag_mode::none;
                drag_target = {};

                drag_base = drag_base2 = drag_start_x = drag_start_y = 0;
                trs_edge = part::none;
                last_click_t = -1.0e9;
                last_click_pos = {0, 0};
                click_count = 0;
                header_moved = false;
            }
        };

        // view 是组件的组合根：数据归属各组件，算法由各 system 负责
        struct view
        {
            scroll_state scroll;
            sort_state sort;
            filter_state filter;
            selection_state sel;
            checks_state checks;
            columns_state cols;
            history_state history;
            find_state find;
            menu_state menu;
            tooltip_state tip;
            ui_state ui;
            edit_state edit;
            using undo_change = history_state::undo_change;

            void reset(const table_data &d)
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
                ui.reset();
                tip.reset();
                edit = {};
            }
            // 模型增删后对齐：保留已有 order/宽度/勾选/选择，只补新列新行
            void reconcile(const table_data &d)
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
                // 勾选按 row_id 重新对齐：中间插入/删除行时，勾选跟着行走
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
                const auto row_pos = [&](row_id id) {
                    if (!id.valid())
                        return -1;
                    for (std::size_t r = 0; r < d.ids.size(); ++r)
                        if (d.ids[r] == id)
                            return static_cast<int>(r);
                    return -1;
                };
                const auto col_pos = [&](column_id id) {
                    if (id.value == 0)
                        return -1;
                    for (std::size_t c = 0; c < d.cols.size(); ++c)
                        if (d.cols[c].id == id)
                            return static_cast<int>(c);
                    return -1;
                };
                const auto remap = [&](const cell_ref &ref, int &r, int &c) {
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
                ui.touch();
            }
        };

        // ── 事件直调系统：hover ──────────────────────────────────────────────
    } // namespace comp

    using view = comp::view;

    // ── drag：与 owner 同构的单一分派点 ──────────────────────────────────
    struct drag_none
    {
    };
    struct drag_select
    {
    };
    struct drag_col_w
    {
    };
    struct drag_header
    {
    };
    struct drag_vthumb
    {
    };
    struct drag_hthumb
    {
    };
    struct drag_table_size
    {
    };
    using drag_ref = std::variant<drag_none, drag_select, drag_col_w, drag_header,
                                  drag_vthumb, drag_hthumb, drag_table_size>;

    [[nodiscard]] inline drag_ref to_drag_ref(drag_mode m) noexcept
    {
        switch (m)
        {
        case drag_mode::select:
            return drag_select{};
        case drag_mode::col_w:
            return drag_col_w{};
        case drag_mode::header:
            return drag_header{};
        case drag_mode::vthumb:
            return drag_vthumb{};
        case drag_mode::hthumb:
            return drag_hthumb{};
        case drag_mode::table_size:
            return drag_table_size{};
        default:
            return drag_none{};
        }
    }

    template <class... Fs>
    decltype(auto) visit_drag(drag_mode m, Fs &&...fs)
    {
        return std::visit(overloaded{std::forward<Fs>(fs)...}, to_drag_ref(m));
    }

    // ═ 输入层 input：事件 → 系统 ═══════════════════════════════════════
    struct geometry; // 定义在后面；wheel 只需要它的指针做悬停判定
    namespace input
    {
        inline void system_hover(view &v, const target &t)
        {
            const zone z = t.to_zone();
            if (!(z == v.ui.hover.to_zone()))
                v.tip.hover_t = v.ui.now;
            v.ui.hover = t;
            v.ui.band_row = v.ui.band_col = -1;
            if (z.kind == part::cell || z.kind == part::check)
            {
                v.ui.band_row = z.row;
                v.ui.band_col = z.col;
            }
            if (v.menu.open)
                v.menu.hover = z.kind == part::menu_item ? z.col : -1;
            // hover 不推进版本：picking 回读依赖版本稳定
        }

        // ── 事件直调系统：wheel ───────────────────────────────────────────────
        inline void system_wheel(view &v, const prefs &p, float dx, float dy, bool shift,
                                 const geometry *g = nullptr)
        {
            if (g != nullptr && (v.ui.hover.kind == part::hbar_track ||
                                 v.ui.hover.kind == part::hbar_thumb))
                shift = true;
            if (shift)
                v.scroll.x -= dy * std::max(p.row_h * 2.0F, 10.0F);
            else
                v.scroll.y -= dy * (p.row_h + p.row_gap);
            v.ui.touch();
        }

        // ── command / effect ──────────────────────────────────────────────────
    } // namespace input

    using input::system_hover;
    using input::system_wheel;

    enum class key : std::uint8_t
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

    struct cmd_hover
    {
        zone z{};
    };
    struct cmd_press
    {
        float x{0}, y{0};
        zone z{};
        bool dbl{false}, shift{false}, ctrl{false};
        target t{};
    };
    struct cmd_release
    {
        float x{0}, y{0};
        target t{};
    };
    struct cmd_drag
    {
        float x{0}, y{0};
        target t{};
    };
    struct cmd_wheel
    {
        float dy{0}, dx{0};
        bool shift{false};
    };
    struct cmd_key
    {
        ui_table::key k{key::none};
        bool shift{false}, ctrl{false};
        target t{};
    };
    // 语义命令：由输入路由（apply）识别手势后发出，由 feature 消费
    struct cmd_clear_filter
    {
        int col{-1};
    };
    // 选择：手势/键盘只发这些语义命令，selection_feature 是唯一写 sel 的地方
    struct cmd_scroll_begin
    {
        int axis{0}; // 0=v 1=h
        float base{0}, pos{0};
    };

    struct fx_set_cell
    {
        row_id id{};
        int col{0};
        std::string text;
    };
    struct fx_clipboard
    {
        std::string text;
    };
    using effect = std::variant<fx_set_cell, fx_clipboard>;

    // ── 行变换（filter / sort）───────────────────────────────────────────
    struct row_view
    {
        std::span<const row_idx> rows;
        std::uint32_t version{0};
    };

    struct row_pipeline
    {
        std::vector<row_idx> buf;
        row_view view_{};
        row_view base(const table_data &d)
        {
            buf.resize(static_cast<std::size_t>(d.rows()));
            std::iota(buf.begin(), buf.end(), 0);
            view_.rows = buf;
            view_.version = static_cast<std::uint32_t>(buf.size());
            return view_;
        }
    };

    // ── geometry：派生 ────────────────────────────────────────────────────
    struct zone_rect
    {
        rect r{};
        zone z{};
        std::uint16_t order{0};
    };

    // 表头分组带：一段连续的可见列（first/last 是几何位置，first_col/last_col 是数据列）
    struct group_span
    {
        int first_pos{0}, last_pos{0};
        int first_col{0}, last_col{0};
    };

    struct geometry
    {
        rect box, header, group, col_header, body, frozen_view, scrollv;
        rect gutter_head, gutter_body;
        float group_h{0}, gutter_w{0}, frozen_w{0}, content_w{0}, content_h{0};
        float sx{0}, sy{0}, step{0};
        int frozen{0};

        std::vector<int> col_index;
        std::vector<float> col_x, col_w;
        std::vector<int> view_rows;
        std::vector<int> row_index;
        std::vector<float> row_y, row_h, row_off;
        std::vector<float> row_h_data;
        std::vector<group_span> groups;
        float row_h_key{-1};
        std::uint32_t src_ver{0};

        bool vbar{false}, hbar{false};
        rect vtrack, vthumb, htrack, hthumb, corner;
        rect edge_n, edge_w, edge_ne, edge_nw, edge_se, edge_sw;
        std::uint32_t version{0};

        [[nodiscard]] int col_count() const noexcept
        {
            return static_cast<int>(col_index.size());
        }
        [[nodiscard]] int row_count() const noexcept
        {
            return static_cast<int>(row_index.size());
        }
        [[nodiscard]] float col_x_at(int p) const noexcept
        {
            return body.x + gutter_w + (p < frozen ? 0.0F : -sx) +
                   col_x[static_cast<std::size_t>(p)];
        }
        [[nodiscard]] float clip_x0(int p) const noexcept
        {
            return p < frozen ? frozen_view.x : scrollv.x;
        }
        [[nodiscard]] float clip_x1(int p) const noexcept
        {
            return p < frozen ? frozen_view.x1() : scrollv.x1();
        }
        [[nodiscard]] int row_at(float y) const noexcept
        {
            for (int i = 0; i < row_count(); ++i)
            {
                const float y0 = row_y[static_cast<std::size_t>(i)];
                if (y >= y0 && y < y0 + row_h[static_cast<std::size_t>(i)])
                    return row_index[static_cast<std::size_t>(i)];
            }
            return -1;
        }
        [[nodiscard]] int col_at(float x) const noexcept
        {
            if (x < body.x + gutter_w)
                return -1;
            for (int i = 0; i < col_count(); ++i)
            {
                const float x0 = std::max(col_x_at(i), clip_x0(i));
                const float x1 = std::min(
                    col_x_at(i) + col_w[static_cast<std::size_t>(i)], clip_x1(i));
                if (x >= x0 && x < x1)
                    return col_index[static_cast<std::size_t>(i)];
            }
            return -1;
        }
        [[nodiscard]] int pos_of_col(int c) const noexcept
        {
            for (int i = 0; i < col_count(); ++i)
                if (col_index[static_cast<std::size_t>(i)] == c)
                    return i;
            return -1;
        }
        [[nodiscard]] int pos_at(float x) const noexcept
        {
            for (int i = 0; i < col_count(); ++i)
            {
                const float x0 = std::max(col_x_at(i), clip_x0(i));
                const float x1 = std::min(
                    col_x_at(i) + col_w[static_cast<std::size_t>(i)], clip_x1(i));
                if (x >= x0 && x < x1)
                    return i;
            }
            return -1;
        }
        // ── 全量行序（view_rows/row_off）：键盘可见性用 ──
        [[nodiscard]] int order_pos(int data_row) const noexcept
        {
            for (int i = 0; i < static_cast<int>(view_rows.size()); ++i)
                if (view_rows[static_cast<std::size_t>(i)] == data_row)
                    return i;
            return -1;
        }
        [[nodiscard]] float order_y(int data_row) const noexcept
        {
            const int p = order_pos(data_row);
            return p < 0 ? -1.0F : row_off[static_cast<std::size_t>(p)];
        }
        [[nodiscard]] float order_h(int data_row) const noexcept
        {
            return static_cast<std::size_t>(data_row) < row_h_data.size()
                       ? row_h_data[static_cast<std::size_t>(data_row)]
                       : 0.0F;
        }
    };

    // 键盘/查找把活动格滚进视口：只改 scroll，下一次 sync 自然生效
    inline void ensure_visible(comp::view &v, const geometry &g, const prefs &, int row,
                               int col) noexcept
    {
        if (row >= 0 && g.body.h > 0)
        {
            const int pos = g.order_pos(row);
            if (pos >= 0)
            {
                const float y0 = g.row_off[static_cast<std::size_t>(pos)];
                const float y1 = y0 + g.order_h(row);
                const float max_sy = std::max(0.0F, g.content_h - g.body.h);
                float ny = v.scroll.y;
                if (y0 < ny + 0.5F)
                    ny = y0;
                else if (y1 > ny + g.body.h - 0.5F)
                    ny = y1 - g.body.h;
                ny = std::clamp(ny, 0.0F, max_sy);
                if (ny != v.scroll.y)
                {
                    v.scroll.y = ny;
                    v.ui.touch();
                }
            }
        }
        if (col >= 0)
        {
            const int cp = g.pos_of_col(col);
            if (cp >= 0 && cp >= g.frozen && g.scrollv.w > 0)
            {
                const float x0 = g.col_x[static_cast<std::size_t>(cp)];
                const float x1 = x0 + g.col_w[static_cast<std::size_t>(cp)];
                const float span = g.scrollv.w;
                const float max_sx = std::max(0.0F, g.content_w - g.frozen_w - span);
                float nx = v.scroll.x;
                if (x0 < nx)
                    nx = x0;
                else if (x1 > nx + span)
                    nx = x1 - span;
                nx = std::clamp(nx, 0.0F, max_sx);
                if (nx != v.scroll.x)
                {
                    v.scroll.x = nx;
                    v.ui.touch();
                }
            }
        }
    }

    // ── frame_data：派生 ──────────────────────────────────────────────────
    enum class stream : std::uint8_t
    {
        chrome = 0,
        content = 1,
        overlay = 2,
        proxy = 3,
        Count
    };

    struct shape_op
    {
        rect r{};
        glm::vec4 color{0};
        glm::vec4 border{0};
        float radius{0}, border_w{0};
        std::uint32_t zone{0};
        owner who{};
    };
    struct text_op
    {
        rect r{};
        glm::vec4 color{1};
        float baseline{0};
        const char *p{nullptr};
        std::uint32_t len{0};
        std::uint32_t zone{0};
        owner who{};
    };

    struct dl
    {
        std::array<std::vector<shape_op>, 4> shapes;
        std::array<std::vector<text_op>, 4> texts;
        arena ar;
        void clear()
        {
            for (auto &s : shapes)
                s.clear();
            for (auto &t : texts)
                t.clear();
            ar.reset();
        }
        void add_shape(stream st, const rect &r, const glm::vec4 &c, float radius = 0,
                       const glm::vec4 &border = {}, float bw = 0, std::uint32_t zk = 0)
        {
            if (r.empty())
                return;
            shapes[static_cast<std::size_t>(st)].push_back(
                {r, c, border, radius, bw, zk});
        }
        void add_text(stream st, const rect &r, const glm::vec4 &c, float baseline,
                      std::string_view s, std::uint32_t zk = 0)
        {
            if (s.empty() || r.empty())
                return;
            const auto k = ar.put(s);
            texts[static_cast<std::size_t>(st)].push_back(
                {r, c, baseline, k.data(), static_cast<std::uint32_t>(k.size()), zk});
        }
        [[nodiscard]] std::size_t op_count() const noexcept
        {
            std::size_t n = 0;
            for (const auto &s : shapes)
                n += s.size();
            for (const auto &t : texts)
                n += t.size();
            return n;
        }
    };

    struct frame_data
    {
        dl draw;
        std::vector<zone_rect> zones;
        std::vector<std::uint8_t> visual;
        std::uint32_t version{0};
        table_id table{};
        void clear()
        {
            draw.clear();
            zones.clear();
            visual.clear();
        }
    };

    enum : std::uint8_t
    {
        vis_in_range = 1u << 0,
        vis_active = 1u << 1,
        vis_editing = 1u << 2,
    };

    // ═════════════════════════════════════════════════════════════════════
    // Feature：行为的四个缝（state / command / rows / paint）
    // ═════════════════════════════════════════════════════════════════════
    struct table_ctx
    {
        const table_data &d;
        view &v;
        const prefs &p;
        const geometry &g;
        const font &ft;
        std::vector<effect> *fx{nullptr};
        void *out{nullptr};
        table_id id{};
    };
    struct frame_ctx
    {
        const table_data &d;
        const view &v;
        const prefs &p;
        const geometry &g;
        const font &ft;
    };

    // 排序：语义命令 + 行 stage + 列头标记（状态在 view.sort_*）
    // ═ 系统层 sys：输入/行列/绘制算法 ═══════════════════════════════════
    namespace sys
    {
        // 列宽自适应：标题与所有单元格里最宽的一行 + 内边距（菜单 Fit 与双击分隔条共用）
        inline void autofit_column(table_ctx &c, int col) noexcept
        {
            auto &v = c.v;
            if (col < 0 || col >= c.d.cols_count() ||
                static_cast<std::size_t>(col) >= v.cols.w.size())
                return;
            float w = text_width(c.ft, c.d.cols[static_cast<std::size_t>(col)].title);
            for (int r = 0; r < c.d.rows(); ++r)
                w = std::max(w, text_width(c.ft, c.d.text(r, col)));
            const auto &def = c.d.cols[static_cast<std::size_t>(col)];
            v.cols.w[static_cast<std::size_t>(col)] =
                std::clamp(w + 2.0F * c.p.cell_pad + 24.0F, def.min_w, def.max_w);
            v.cols.user[static_cast<std::size_t>(col)] = 1;
            v.ui.touch();
        }

        // 列操作：菜单项与（将来的）右键命令共用同一批实现
        inline void reset_sort(view &v) noexcept
        {
            v.sort.col = -1;
            v.sort.dir = 0;
        }
        inline void hide_column(table_ctx &c, int col) noexcept
        {
            auto &v = c.v;
            int visible = 0;
            for (const auto h : v.cols.hidden)
                visible += h == 0 ? 1 : 0;
            if (visible <= 1 || col < 0 ||
                static_cast<std::size_t>(col) >= v.cols.hidden.size())
                return;
            v.cols.hidden[static_cast<std::size_t>(col)] = 1;
            if (static_cast<std::size_t>(col) < v.filter.text.size())
                v.filter.text[static_cast<std::size_t>(col)].clear();
            v.ui.touch();
        }
        inline void show_all_columns(table_ctx &c) noexcept
        {
            std::fill(c.v.cols.hidden.begin(), c.v.cols.hidden.end(), 0);
            c.v.ui.touch();
        }

        struct sort_feature
        {
            bool toggle(table_ctx &c, int col)
            {
                c.v.sort.toggle(col);
                c.v.ui.touch();
                return true;
            }
            template <class E>
            bool on_command(table_ctx &, const E &)
            {
                return false;
            }
            void rows(table_ctx &c, std::vector<row_idx> &r)
            {
                if (c.v.sort.dir == 0 || c.v.sort.col < 0)
                    return;
                const int cc = c.v.sort.col;
                const bool desc = c.v.sort.dir == 2;
                std::stable_sort(r.begin(), r.end(), [&](int a, int b) {
                    const auto x = c.d.text(a, cc);
                    const auto y = c.d.text(b, cc);
                    return desc ? x > y : x < y;
                });
            }

            void paint(const frame_ctx &c, frame_data &fr) const
            {
                if (c.v.sort.dir == 0 || c.v.sort.col < 0)
                    return;
                const int pos = c.g.pos_of_col(c.v.sort.col);
                if (pos < 0)
                    return;
                const rect hr =
                    clip_x({c.g.col_x_at(pos), c.g.col_header.y,
                            c.g.col_w[static_cast<std::size_t>(pos)], c.g.col_header.h},
                           c.g.clip_x0(pos), c.g.clip_x1(pos));
                if (hr.empty())
                    return;
                fr.draw.add_shape(stream::chrome,
                                  {hr.x1() - 12, hr.y + hr.h * 0.5F - 3, 8, 6},
                                  c.p.header_text, 1, {}, 0,
                                  zone_key({part::header, -1, c.v.sort.col}));
            }
        };

        // 筛选：语义命令 + 行 stage + 列头漏斗（状态在 view.filter）
        struct filter_feature
        {
            bool begin(table_ctx &c, const target &t)
            {
                return begin(c, t.col_index);
            }
            bool begin(table_ctx &c, int col)
            {
                auto &v = c.v;
                v.filter.edit = col;
                v.filter.compose.clear();
                v.filter.input = static_cast<std::size_t>(col) < v.filter.text.size()
                                     ? v.filter.text[static_cast<std::size_t>(col)]
                                     : std::string{};
                v.filter.orig = v.filter.input;
                v.ui.touch();
                return true;
            }
            bool set(table_ctx &c, int col, std::string text)
            {
                auto &f = c.v.filter.text;
                if (static_cast<std::size_t>(c.d.cols_count()) > f.size())
                    f.resize(static_cast<std::size_t>(c.d.cols_count()));
                if (col >= 0 && static_cast<std::size_t>(col) < f.size())
                    f[static_cast<std::size_t>(col)] = std::move(text);
                c.v.ui.touch();
                return true;
            }
            bool clear(table_ctx &c, int col)
            {
                auto &f = c.v.filter.text;
                if (col < 0)
                    f.assign(static_cast<std::size_t>(c.d.cols_count()), {});
                else if (static_cast<std::size_t>(col) < f.size())
                    f[static_cast<std::size_t>(col)].clear();
                c.v.ui.touch();
                return true;
            }
            static void sync_live(view &v) noexcept
            {
                if (v.filter.edit < 0)
                    return;
                const auto col = static_cast<std::size_t>(v.filter.edit);
                if (col < v.filter.text.size())
                    v.filter.text[col] = v.filter.input;
            }
            bool text(table_ctx &c, std::string_view s)
            {
                if (c.v.filter.edit < 0)
                    return false;
                c.v.filter.input.append(s);
                c.v.filter.compose.clear();
                sync_live(c.v); // 边打边过滤，而不是等 Enter
                c.v.ui.touch();
                return true;
            }
            [[nodiscard]] static bool active(const view &v, int col) noexcept
            {
                return col >= 0 && static_cast<std::size_t>(col) < v.filter.text.size() &&
                       !v.filter.text[static_cast<std::size_t>(col)].empty();
            }

            template <class E>
            bool on_command(table_ctx &c, const E &x)
            {
                if constexpr (std::is_same_v<E, cmd_press>)
                    return press(c, x);
                else if constexpr (std::is_same_v<E, cmd_key>)
                    return key(c, x);
                return false;
            }
            bool press(table_ctx &c, const cmd_press &pr)
            {
                auto &v = c.v;
                if (v.filter.edit < 0)
                    return false;
                if (pr.z.kind != part::filter_btn)
                {
                    if (static_cast<std::size_t>(v.filter.edit) < v.filter.text.size())
                        v.filter.text[static_cast<std::size_t>(v.filter.edit)] =
                            v.filter.input;
                    v.filter.edit = -1;
                    v.filter.orig.clear();
                    v.ui.touch();
                }
                return false;
            }
            bool key(table_ctx &c, const cmd_key &k)
            {
                auto &v = c.v;
                if (v.filter.edit < 0)
                    return false;
                v.filter.compose.clear();
                if (k.k == key::esc)
                {
                    // 取消：还原进入编辑前的筛选串
                    if (static_cast<std::size_t>(v.filter.edit) < v.filter.text.size())
                        v.filter.text[static_cast<std::size_t>(v.filter.edit)] =
                            v.filter.orig;
                    v.filter.input = v.filter.orig;
                    v.filter.orig.clear();
                    v.filter.edit = -1;
                }
                else if (k.k == key::enter || k.k == key::tab)
                {
                    if (static_cast<std::size_t>(v.filter.edit) < v.filter.text.size())
                        v.filter.text[static_cast<std::size_t>(v.filter.edit)] =
                            v.filter.input;
                    v.filter.orig.clear();
                    v.filter.edit = -1;
                }
                else if (k.k == key::backspace && !v.filter.input.empty())
                {
                    const std::size_t q = utf8_prev(std::string_view(v.filter.input),
                                                    v.filter.input.size());
                    v.filter.input.erase(q);
                    sync_live(v);
                }
                else if (k.k == key::del)
                {
                    v.filter.input.clear();
                    sync_live(v);
                }
                v.ui.touch();
                return true;
            }

            void rows(table_ctx &c, std::vector<row_idx> &r)
            {
                const auto &f = c.v.filter.text;
                if (f.empty())
                    return;
                std::vector<row_idx> keep;
                keep.reserve(r.size());
                for (const int row : r)
                {
                    bool ok = true;
                    for (std::size_t col = 0; col < f.size() && ok; ++col)
                        if (!f[col].empty())
                            ok = contains(c.d.text(row, static_cast<int>(col)), f[col]);
                    if (ok)
                        keep.push_back(row);
                }
                r.swap(keep);
            }

            void paint(const frame_ctx &c, frame_data &fr) const
            {
                if (c.v.filter.edit >= 0)
                {
                    const int pos = c.g.pos_of_col(c.v.filter.edit);
                    if (pos >= 0)
                    {
                        rect hr = clip_x({c.g.col_x_at(pos), c.g.col_header.y,
                                          c.g.col_w[static_cast<std::size_t>(pos)],
                                          c.g.col_header.h},
                                         c.g.clip_x0(pos), c.g.clip_x1(pos));
                        if (!hr.empty())
                        {
                            const std::string shown =
                                c.v.filter.input + c.v.filter.compose;
                            fr.draw.add_shape(
                                stream::overlay, hr, {0.06F, 0.08F, 0.12F, 1.0F},
                                c.p.cell_radius, c.p.accent, 1.2F,
                                zone_key({part::filter_btn, -1, c.v.filter.edit}));
                            fr.draw.add_text(
                                stream::overlay,
                                {hr.x + 6, hr.y, std::max(hr.w - 12, 1.0F), hr.h},
                                c.p.text, hr.y + hr.h * 0.5F + c.ft.px * 0.30F, shown, 0);
                            const float tw = text_width(c.ft, shown);
                            fr.draw.add_shape(stream::overlay,
                                              {hr.x + 6 + tw, hr.y + 6, 1.5F,
                                               std::max(hr.h - 12, 1.0F)},
                                              c.p.accent, 0, {}, 0, 0);
                        }
                    }
                    return;
                }
                for (int i = 0; i < c.g.col_count(); ++i)
                {
                    const int dc = c.g.col_index[static_cast<std::size_t>(i)];
                    if (dc < 0 || dc >= c.d.cols_count() ||
                        !c.d.cols[static_cast<std::size_t>(dc)].filterable)
                        continue;
                    const rect hr =
                        clip_x({c.g.col_x_at(i), c.g.col_header.y,
                                c.g.col_w[static_cast<std::size_t>(i)], c.g.col_header.h},
                               c.g.clip_x0(i), c.g.clip_x1(i));
                    if (hr.w < 30)
                        continue;
                    const float x = hr.x1() - 16;
                    const float y = hr.y + hr.h * 0.5F - 4;
                    const auto color =
                        active(c.v, dc) ? c.p.accent
                                        : glm::vec4(c.p.header_text.r, c.p.header_text.g,
                                                    c.p.header_text.b, 0.5F);
                    fr.draw.add_shape(stream::chrome, {x, y, 10, 2}, color, 1, {}, 0,
                                      zone_key({part::header, -1, dc}));
                    fr.draw.add_shape(stream::chrome, {x + 2, y + 3, 6, 2}, color, 1, {},
                                      0, zone_key({part::header, -1, dc}));
                    fr.draw.add_shape(stream::chrome, {x + 4, y + 6, 2, 2}, color, 1, {},
                                      0, zone_key({part::header, -1, dc}));
                }
            }

            static bool contains(std::string_view h, std::string_view n) noexcept
            {
                if (n.empty())
                    return true;
                if (h.size() < n.size())
                    return false;
                const auto eq = [](char a, char b) {
                    if (a >= 'A' && a <= 'Z')
                        a = static_cast<char>(a - 'A' + 'a');
                    if (b >= 'A' && b <= 'Z')
                        b = static_cast<char>(b - 'A' + 'a');
                    return a == b;
                };
                for (std::size_t i = 0; i + n.size() <= h.size(); ++i)
                {
                    std::size_t k = 0;
                    while (k < n.size() && eq(h[i + k], n[k]))
                        ++k;
                    if (k == n.size())
                        return true;
                }
                return false;
            }
        };

        // 选择：语义命令 + 绘制（状态在 view 的 sel_*/cur_*）
        struct selection_feature
        {
            [[nodiscard]] static bool in_range(const view &v, int r, int c) noexcept
            {
                return v.sel.r0 >= 0 && v.sel.c0 >= 0 &&
                       r >= std::min(v.sel.r0, v.sel.r1) &&
                       r <= std::max(v.sel.r0, v.sel.r1) &&
                       c >= std::min(v.sel.c0, v.sel.c1) &&
                       c <= std::max(v.sel.c0, v.sel.c1);
            }

            static void sync_cells(const table_data &d, view &v)
            {
                v.sel.cur_cell = d.cell_id(v.sel.cur_r, v.sel.cur_c);
                v.sel.r0_cell = d.cell_id(v.sel.r0, v.sel.c0);
                v.sel.r1_cell = d.cell_id(v.sel.r1, v.sel.c1);
            }

            static void anchor(view &v, int r, int c)
            {
                v.sel.r0 = v.sel.r1 = v.sel.cur_r = r;
                v.sel.c0 = v.sel.c1 = v.sel.cur_c = c;
                v.ui.touch();
            }
            void set(table_ctx &c, const target &t)
            {
                set(c, t.row_index, t.col_index);
            }
            void set(table_ctx &c, int r, int col)
            {
                anchor(c.v, r, col);
                sync_cells(c.d, c.v);
            }

            // 单击 = anchor；Shift+单击 = 从 anchor 扩到目标（Excel/Finder 语义）
            void set_or_extend(table_ctx &c, const target &t, bool shift)
            {
                if (shift && c.v.sel.r0 >= 0)
                    extend(c, t.row_index, t.col_index);
                else
                    set(c, t);
            }

            // 拖拽框选：指针出体区时按一行步长自动滚，并把目标夹到首/末可见行列
            void drag_extend(table_ctx &c, float x, float y)
            {
                auto &v = c.v;
                const auto &g = c.g;
                const float max_sy = std::max(0.0F, g.content_h - g.body.h);
                if (y < g.body.y && v.scroll.y > 0.0F)
                    v.scroll.y = std::max(0.0F, v.scroll.y - g.step);
                else if (y > g.body.y1())
                    v.scroll.y = std::min(max_sy, v.scroll.y + g.step);
                const float max_sx =
                    std::max(0.0F, g.content_w - g.frozen_w - g.scrollv.w);
                if (x < g.scrollv.x && v.scroll.x > 0.0F)
                    v.scroll.x = std::max(0.0F, v.scroll.x - 40.0F);
                else if (x > g.scrollv.x1())
                    v.scroll.x = std::min(max_sx, v.scroll.x + 40.0F);
                int r = g.row_at(y);
                int cc = g.col_at(x);
                if (r < 0 && !g.view_rows.empty())
                    r = y < g.body.y ? g.view_rows.front() : g.view_rows.back();
                if (cc < 0 && g.col_count() > 0)
                    cc = x < g.scrollv.x ? g.col_index.front() : g.col_index.back();
                if (r >= 0 && cc >= 0)
                    extend(c, r, cc);
                v.ui.touch();
            }

            // 点分组头 = 选中这一组的全部行
            void all_of_group(table_ctx &c, int first_col)
            {
                auto &v = c.v;
                for (const auto &sp : c.g.groups)
                {
                    if (sp.first_col != first_col)
                        continue;
                    if (c.g.view_rows.empty())
                        return;
                    v.sel.r0 = c.g.view_rows.front();
                    v.sel.r1 = c.g.view_rows.back();
                    v.sel.c0 = sp.first_col;
                    v.sel.c1 = sp.last_col;
                    v.sel.cur_r = v.sel.r1;
                    v.sel.cur_c = v.sel.c1;
                    sync_cells(c.d, v);
                    v.ui.touch();
                    return;
                }
            }

            void extend(table_ctx &c, int r, int cc)
            {
                auto &v = c.v;
                if (v.sel.r0 < 0)
                    anchor(v, r, cc);
                else
                {
                    v.sel.r1 = r;
                    v.sel.c1 = cc;
                    v.sel.cur_r = r;
                    v.sel.cur_c = cc;
                    v.ui.touch();
                }
                sync_cells(c.d, v);
            }

            void move(table_ctx &c, int dr, int dc, bool extend_sel)
            {
                auto &v = c.v;
                if (v.sel.cur_r < 0)
                {
                    if (!c.g.view_rows.empty())
                        anchor(v, c.g.view_rows.front(), 0);
                    return;
                }
                int nr = v.sel.cur_r;
                int nc = v.sel.cur_c;
                if (dc != 0)
                {
                    // 横向只走可见列（隐藏列不参与键盘导航）
                    int cp = c.g.pos_of_col(nc);
                    if (cp < 0)
                        cp = 0;
                    const int np =
                        std::clamp(cp + dc, 0, std::max(c.g.col_count() - 1, 0));
                    if (c.g.col_count() > 0)
                        nc = c.g.col_index[static_cast<std::size_t>(np)];
                }
                else
                {
                    int pos = 0;
                    for (int i = 0; i < static_cast<int>(c.g.view_rows.size()); ++i)
                        if (c.g.view_rows[static_cast<std::size_t>(i)] == nr)
                        {
                            pos = i;
                            break;
                        }
                    const int np = std::clamp(pos + dr, 0,
                                              static_cast<int>(c.g.view_rows.size()) - 1);
                    nr = c.g.view_rows[static_cast<std::size_t>(np)];
                }
                if (!extend_sel)
                    anchor(v, nr, nc);
                else
                {
                    v.sel.r1 = nr;
                    v.sel.c1 = nc;
                    v.sel.cur_r = nr;
                    v.sel.cur_c = nc;
                    v.ui.touch();
                }
                sync_cells(c.d, v);
                ensure_visible(v, c.g, c.p, nr, nc);
            }

            void to(table_ctx &c, int r, int cc, bool extend_sel)
            {
                auto &v = c.v;
                // Home/End 落在可见列的首/末列，而不是可能被隐藏的数据列
                if (c.g.col_count() > 0)
                {
                    if (cc <= 0)
                        cc = c.g.col_index.front();
                    else if (cc >= c.d.cols_count() - 1)
                        cc = c.g.col_index.back();
                }
                if (!extend_sel)
                    anchor(v, r, cc);
                else
                {
                    v.sel.r1 = r;
                    v.sel.c1 = cc;
                    v.sel.cur_r = r;
                    v.sel.cur_c = cc;
                    v.ui.touch();
                }
                sync_cells(c.d, v);
                ensure_visible(v, c.g, c.p, r, cc);
            }

            void all(table_ctx &c)
            {
                auto &v = c.v;
                if (c.g.view_rows.empty())
                    return;
                v.sel.r0 = c.g.view_rows.front();
                v.sel.r1 = c.g.view_rows.back();
                v.sel.c0 = c.g.col_count() > 0 ? c.g.col_index.front() : 0;
                v.sel.c1 =
                    c.g.col_count() > 0 ? c.g.col_index.back() : c.d.cols_count() - 1;
                v.sel.cur_r = v.sel.r1;
                v.sel.cur_c = v.sel.c1;
                sync_cells(c.d, v);
                v.ui.touch();
            }

            void clear(table_ctx &c)
            {
                auto &v = c.v;
                v.sel.r0 = v.sel.c0 = v.sel.r1 = v.sel.c1 = -1;
                v.sel.cur_r = v.sel.cur_c = -1;
                v.sel.cur_cell = v.sel.r0_cell = v.sel.r1_cell = {};
                v.ui.touch();
            }

            template <class E>
            bool on_command(table_ctx &, const E &)
            {
                return false;
            }
            void rows(table_ctx &, std::vector<row_idx> &) {}

            void paint(const frame_ctx &c, frame_data &fr) const
            {
                if (c.v.sel.r0 < 0)
                    return;
                const int r0 = std::min(c.v.sel.r0, c.v.sel.r1);
                const int r1 = std::max(c.v.sel.r0, c.v.sel.r1);
                const int c0 = std::min(c.v.sel.c0, c.v.sel.c1);
                const int c1 = std::max(c.v.sel.c0, c.v.sel.c1);

                // 列头高亮
                for (int i = 0; i < c.g.col_count(); ++i)
                {
                    const int dc = c.g.col_index[static_cast<std::size_t>(i)];
                    if (dc < c0 || dc > c1)
                        continue;
                    const rect hr =
                        clip_x({c.g.col_x_at(i), c.g.col_header.y,
                                c.g.col_w[static_cast<std::size_t>(i)], c.g.col_header.h},
                               c.g.clip_x0(i), c.g.clip_x1(i));
                    if (!hr.empty())
                        fr.draw.add_shape(stream::content, hr, {1, 1, 1, 0.05F}, 0, {}, 0,
                                          0);
                }

                for (int ri = 0; ri < c.g.row_count(); ++ri)
                {
                    const int dr = c.g.row_index[static_cast<std::size_t>(ri)];
                    if (dr < r0 || dr > r1)
                        continue;
                    for (int ci = 0; ci < c.g.col_count(); ++ci)
                    {
                        const int dc = c.g.col_index[static_cast<std::size_t>(ci)];
                        if (dc < c0 || dc > c1)
                            continue;
                        if (c.v.edit.active() && c.v.edit.row == dr && c.v.edit.col == dc)
                            continue;
                        rect cr = clip_x({c.g.col_x_at(ci),
                                          c.g.row_y[static_cast<std::size_t>(ri)],
                                          c.g.col_w[static_cast<std::size_t>(ci)],
                                          c.g.row_h[static_cast<std::size_t>(ri)]},
                                         c.g.clip_x0(ci), c.g.clip_x1(ci));
                        cr = intersect(cr, c.g.body);
                        if (cr.empty())
                            continue;
                        fr.draw.add_shape(stream::content, cr, c.p.sel, c.p.cell_radius,
                                          {}, 0, 0);
                        if (dr == c.v.sel.cur_r && dc == c.v.sel.cur_c)
                            fr.draw.add_shape(stream::content, cr, {0, 0, 0, 0}, 0,
                                              c.p.accent, 1.5F, 0);
                    }
                }

                // 选区外框
                float y0 = 1e30F, y1 = -1e30F, x0 = 1e30F, x1 = -1e30F;
                for (int ri = 0; ri < c.g.row_count(); ++ri)
                {
                    const int dr = c.g.row_index[static_cast<std::size_t>(ri)];
                    if (dr >= r0 && dr <= r1)
                    {
                        y0 = std::min(y0, c.g.row_y[static_cast<std::size_t>(ri)]);
                        y1 = std::max(y1, c.g.row_y[static_cast<std::size_t>(ri)] +
                                              c.g.row_h[static_cast<std::size_t>(ri)]);
                    }
                }
                for (int i = 0; i < c.g.col_count(); ++i)
                {
                    const int dc = c.g.col_index[static_cast<std::size_t>(i)];
                    if (dc < c0 || dc > c1)
                        continue;
                    const float l = std::max(c.g.col_x_at(i), c.g.clip_x0(i));
                    const float r =
                        std::min(c.g.col_x_at(i) + c.g.col_w[static_cast<std::size_t>(i)],
                                 c.g.clip_x1(i));
                    if (r > l)
                    {
                        x0 = std::min(x0, l);
                        x1 = std::max(x1, r);
                    }
                }
                if (y1 > y0 && x1 > x0)
                {
                    x0 = std::max(x0, c.g.body.x);
                    x1 = std::min(x1, c.g.body.x1());
                    y0 = std::max(y0, c.g.body.y);
                    y1 = std::min(y1, c.g.body.y1());
                    fr.draw.add_shape(stream::content, {x0, y0, x1 - x0, 2}, c.p.accent,
                                      0, {}, 0, 0);
                    fr.draw.add_shape(stream::content, {x0, y1 - 2, x1 - x0, 2},
                                      c.p.accent, 0, {}, 0, 0);
                    fr.draw.add_shape(stream::content, {x0, y0, 2, y1 - y0}, c.p.accent,
                                      0, {}, 0, 0);
                    fr.draw.add_shape(stream::content, {x1 - 2, y0, 2, y1 - y0},
                                      c.p.accent, 0, {}, 0, 0);
                }
            }
        };

        // 编辑：语义命令 + 改写 data 的 effect（状态在 view.edit）
        struct edit_feature
        {
            static int pos_of(const geometry &g, int row)
            {
                for (int i = 0; i < static_cast<int>(g.view_rows.size()); ++i)
                    if (g.view_rows[static_cast<std::size_t>(i)] == row)
                        return i;
                return -1;
            }

            static void start(const table_data &d, view &v, int r, int c)
            {
                if (r < 0 || c < 0)
                    return;
                v.edit.row = r;
                v.edit.col = c;
                v.edit.cell = d.cell_id(r, c);
                v.edit.buf = std::string(d.text(r, c));
                v.edit.orig = v.edit.buf;
                v.edit.caret = static_cast<int>(v.edit.buf.size());
                v.edit.anchor = v.edit.caret;
                v.edit.undo.clear();
                v.edit.redo.clear();
                v.edit.compose.clear();
                v.ui.touch();
            }

            static void commit(table_ctx &c)
            {
                auto &e = c.v.edit;
                if (!e.active())
                    return;
                if (e.buf != e.orig)
                {
                    const auto id = e.cell.row;
                    trace("[commit] row={} col={} id={} text={}", e.row, e.col, id.v,
                          e.buf);
                    if (c.fx)
                        c.fx->push_back(fx_set_cell{id, e.col, e.buf});
                    c.v.history.undo.push_back({{id, e.col, e.orig, e.buf}});
                    c.v.history.redo.clear();
                }
                e = {};
                c.v.ui.touch();
            }

            static void replace_sel(view &v, std::string_view s)
            {
                auto &e = v.edit;
                const int lo = e.sel_lo(), hi = e.sel_hi();
                e.buf.replace(static_cast<std::size_t>(lo),
                              static_cast<std::size_t>(hi - lo), s);
                e.caret = lo + static_cast<int>(s.size());
                e.anchor = e.caret;
            }

            static int caret_at(const table_ctx &c, float x)
            {
                const auto &e = c.v.edit;
                const int pos = c.g.pos_of_col(e.col);
                if (pos < 0)
                    return 0;
                const float left = c.g.col_x_at(pos) + c.p.cell_pad;
                int best = 0;
                float bestd = 1.0e30F;
                std::size_t i = 0;
                while (true)
                {
                    const float w =
                        text_width(c.ft, std::string_view(e.buf).substr(0, i));
                    const float d = std::abs((left + w) - x);
                    if (d < bestd)
                    {
                        bestd = d;
                        best = static_cast<int>(i);
                    }
                    if (i >= e.buf.size())
                        break;
                    char32_t cp = 0;
                    i += std::max<std::size_t>(
                        utf8_decode(std::string_view(e.buf).substr(i), cp), 1);
                }
                return best;
            }

            bool paste(table_ctx &c, std::string text)
            {
                auto &v = c.v;
                if (v.edit.active())
                {
                    snapshot(v, false);
                    for (char &ch : text)
                        if (ch == '\n' || ch == '\r' || ch == '\t')
                            ch = ' ';
                    replace_sel(v, text);
                }
                else if (v.sel.cur_r >= 0 && v.sel.cur_c >= 0 && c.fx)
                {
                    const int r = v.sel.cur_r;
                    const int col = v.sel.cur_c;
                    std::vector<view::undo_change> batch;
                    std::size_t line = 0;
                    int dr = 0;
                    while (line <= text.size())
                    {
                        const std::size_t nl = text.find('\n', line);
                        const std::size_t end =
                            nl == std::string::npos ? text.size() : nl;
                        const std::string_view ln{text.data() + line, end - line};
                        std::size_t p = 0;
                        int dc = 0;
                        while (p <= ln.size())
                        {
                            const std::size_t tab = ln.find('\t', p);
                            const std::size_t e2 =
                                tab == std::string_view::npos ? ln.size() : tab;
                            const int rr = r + dr;
                            const int cc = col + dc;
                            if (rr >= 0 && rr < c.d.rows() && cc >= 0 &&
                                cc < c.d.cols_count())
                            {
                                const auto old = c.d.text(rr, cc);
                                const auto id = c.d.ids[static_cast<std::size_t>(rr)];
                                std::string val{ln.substr(p, e2 - p)};
                                c.fx->push_back(fx_set_cell{id, cc, val});
                                batch.push_back({id, cc, std::string(old), val});
                            }
                            ++dc;
                            if (tab == std::string_view::npos)
                                break;
                            p = tab + 1;
                        }
                        ++dr;
                        if (nl == std::string::npos)
                            break;
                        line = nl + 1;
                    }
                    if (!batch.empty())
                    {
                        v.history.undo.push_back(std::move(batch));
                        v.history.redo.clear();
                    }
                }
                v.ui.touch();
                return true;
            }

            bool begin(table_ctx &c, const target &t)
            {
                return begin(c, t.row_index, t.col_index);
            }
            bool begin(table_ctx &c, int r, int col)
            {
                start(c.d, c.v, r, col);
                return true;
            }

            bool compose(table_ctx &c, std::string text)
            {
                if (c.v.edit.active())
                {
                    c.v.edit.compose = std::move(text);
                    c.v.ui.touch();
                }
                return true;
            }

            bool text(table_ctx &c, std::string_view s)
            {
                auto &v = c.v;
                if (v.edit.active())
                {
                    snapshot(v, true);
                    replace_sel(v, s);
                }
                else if (v.sel.cur_r >= 0 && v.sel.cur_c >= 0)
                {
                    start(c.d, v, v.sel.cur_r, v.sel.cur_c);
                    v.edit.buf.clear();
                    v.edit.caret = 0;
                    v.edit.anchor = 0;
                    replace_sel(v, s);
                }
                v.ui.touch();
                return true;
            }

            static void snapshot(view &v, bool typing = false)
            {
                auto &e = v.edit;
                // 连续输入（0.8s 内）合并成一步 undo，和浏览器的习惯一致
                if (typing && e.last_push_typing && v.ui.now - e.last_push_t < 0.8)
                {
                    e.redo.clear();
                    return;
                }
                e.undo.push_back(e.buf);
                if (e.undo.size() > 64)
                    e.undo.erase(e.undo.begin());
                e.redo.clear();
                e.last_push_t = v.ui.now;
                e.last_push_typing = typing;
            }
            static void select_all(view &v) noexcept
            {
                v.edit.anchor = 0;
                v.edit.caret = static_cast<int>(v.edit.buf.size());
                v.ui.touch();
            }
            void select_word(table_ctx &c, float x)
            {
                auto &e = c.v.edit;
                e.caret = caret_at(c, x);
                const std::string_view sv{e.buf};
                const auto caret = static_cast<std::size_t>(
                    std::clamp(e.caret, 0, static_cast<int>(e.buf.size())));
                std::size_t probe = caret;
                if (probe >= sv.size() || !is_word_byte(sv[probe]))
                    probe = probe > 0 ? utf8_prev(sv, probe) : 0;
                std::size_t lo = probe;
                while (lo > 0 && is_word_byte(sv[utf8_prev(sv, lo)]))
                    lo = utf8_prev(sv, lo);
                std::size_t hi = probe;
                while (hi < sv.size() && is_word_byte(sv[hi]))
                    hi = utf8_next(sv, hi);
                if (hi <= lo)
                    hi = std::min(sv.size(), utf8_next(sv, lo));
                e.anchor = static_cast<int>(lo);
                e.caret = static_cast<int>(hi);
                c.v.ui.touch();
            }

            template <class E>
            bool on_command(table_ctx &c, const E &x)
            {
                auto &v = c.v;
                using T = E;
                if constexpr (std::is_same_v<T, cmd_press>)
                {
                    if (v.edit.active())
                    {
                        if (x.z.kind == part::cell && x.z.row == v.edit.row &&
                            x.z.col == v.edit.col)
                        {
                            if (v.ui.click_count >= 2)
                            {
                                if (v.ui.click_count >= 3)
                                    select_all(v);
                                else
                                    select_word(c, x.x);
                            }
                            else
                            {
                                const int caret = caret_at(c, x.x);
                                v.edit.caret = caret;
                                if (!x.shift)
                                    v.edit.anchor = caret;
                                v.ui.touch();
                            }
                        }
                        else
                            commit(c);
                    }
                    return false; // 让 core 继续处理按下
                }

                else if constexpr (std::is_same_v<T, cmd_drag>)
                {
                    if (!v.edit.active())
                        return false;
                    const target tgt =
                        x.t.kind != part::none ? x.t : target_of(c.d, {}, x.t.to_zone());
                    bool in_row = false;
                    if (tgt.kind == part::cell)
                        in_row = tgt.row_index == v.edit.row;
                    else
                        for (int i = 0; i < c.g.row_count(); ++i)
                        {
                            if (c.g.row_index[static_cast<std::size_t>(i)] != v.edit.row)
                                continue;
                            const float y0 = c.g.row_y[static_cast<std::size_t>(i)];
                            const float y1 = y0 + c.g.row_h[static_cast<std::size_t>(i)];
                            in_row = x.y >= y0 && x.y < y1;
                            break;
                        }
                    if (in_row)
                    {
                        v.edit.caret = caret_at(c, x.x);
                        v.ui.touch();
                        return true; // 编辑框内拖动 = 选文本，不是选单元格
                    }
                    return false;
                }
                else if constexpr (std::is_same_v<T, cmd_key>)
                {
                    if (!v.edit.active())
                        return false;
                    auto &e = v.edit;
                    switch (x.k)
                    {
                    case key::esc:
                        e = {};
                        break;
                    case key::enter: {
                        const int row = e.row;
                        const int col = e.col;
                        commit(c);
                        const int pos = pos_of(c.g, row);
                        if (pos + 1 < static_cast<int>(c.g.view_rows.size()))
                        {
                            const int nr =
                                c.g.view_rows[static_cast<std::size_t>(pos + 1)];
                            v.sel.r0 = v.sel.r1 = v.sel.cur_r = nr;
                            v.sel.c0 = v.sel.c1 = v.sel.cur_c = col;
                            v.ui.touch();
                            ensure_visible(v, c.g, c.p, nr, col);
                        }
                        break;
                    }
                    case key::tab: {
                        commit(c);
                        const int nc = std::clamp(v.sel.cur_c + (x.shift ? -1 : 1), 0,
                                                  c.d.cols_count() - 1);
                        v.sel.cur_c = v.sel.c0 = v.sel.c1 = nc;
                        v.ui.touch();
                        ensure_visible(v, c.g, c.p, v.sel.cur_r, nc);
                        break;
                    }
                    case key::backspace:
                        snapshot(v, true);
                        if (e.has_sel())
                            replace_sel(v, {});
                        else if (e.caret > 0)
                        {
                            const auto sv = std::string_view(e.buf);
                            const std::size_t q =
                                x.ctrl ? utf8_word_left(sv,
                                                        static_cast<std::size_t>(e.caret))
                                       : utf8_prev(sv, static_cast<std::size_t>(e.caret));
                            e.buf.erase(q, static_cast<std::size_t>(e.caret) - q);
                            e.caret = static_cast<int>(q);
                            e.anchor = e.caret;
                        }
                        break;
                    case key::del:
                        snapshot(v, true);
                        if (e.has_sel())
                            replace_sel(v, {});
                        else if (e.caret < static_cast<int>(e.buf.size()))
                        {
                            const auto sv = std::string_view(e.buf);
                            const std::size_t q =
                                x.ctrl ? utf8_word_right(
                                             sv, static_cast<std::size_t>(e.caret))
                                       : utf8_next(sv, static_cast<std::size_t>(e.caret));
                            e.buf.erase(static_cast<std::size_t>(e.caret),
                                        q - static_cast<std::size_t>(e.caret));
                        }
                        break;
                    case key::left:
                        e.caret =
                            e.has_sel()
                                ? e.sel_lo()
                                : static_cast<int>(
                                      x.ctrl
                                          ? utf8_word_left(
                                                std::string_view(e.buf),
                                                static_cast<std::size_t>(e.caret))
                                          : utf8_prev(std::string_view(e.buf),
                                                      static_cast<std::size_t>(e.caret)));
                        if (!x.shift)
                            e.anchor = e.caret;
                        break;
                    case key::right:
                        e.caret =
                            e.has_sel()
                                ? e.sel_hi()
                                : static_cast<int>(
                                      x.ctrl
                                          ? utf8_word_right(
                                                std::string_view(e.buf),
                                                static_cast<std::size_t>(e.caret))
                                          : utf8_next(std::string_view(e.buf),
                                                      static_cast<std::size_t>(e.caret)));
                        if (!x.shift)
                            e.anchor = e.caret;
                        break;
                    case key::home:
                        e.caret = 0;
                        if (!x.shift)
                            e.anchor = 0;
                        break;
                    case key::end:
                        e.caret = static_cast<int>(e.buf.size());
                        if (!x.shift)
                            e.anchor = e.caret;
                        break;
                    case key::a:
                        if (x.ctrl)
                        {
                            e.anchor = 0;
                            e.caret = static_cast<int>(e.buf.size());
                        }
                        break;
                    case key::c:
                        if (x.ctrl && c.fx)
                            c.fx->push_back(fx_clipboard{
                                e.has_sel()
                                    ? e.buf.substr(static_cast<std::size_t>(e.sel_lo()),
                                                   static_cast<std::size_t>(e.sel_hi() -
                                                                            e.sel_lo()))
                                    : e.buf});
                        break;
                    case key::x:
                        if (x.ctrl)
                        {
                            if (e.has_sel() && c.fx)
                                c.fx->push_back(fx_clipboard{e.buf.substr(
                                    static_cast<std::size_t>(e.sel_lo()),
                                    static_cast<std::size_t>(e.sel_hi() - e.sel_lo()))});
                            snapshot(v, true);
                            if (e.has_sel())
                                replace_sel(v, {});
                        }
                        break;
                    case key::z:
                        if (x.ctrl && !e.undo.empty())
                        {
                            e.redo.push_back(e.buf);
                            e.buf = e.undo.back();
                            e.undo.pop_back();
                            e.caret = static_cast<int>(e.buf.size());
                            e.anchor = e.caret;
                            e.last_push_typing = false;
                        }
                        break;
                    case key::y:
                        if (x.ctrl && !e.redo.empty())
                        {
                            e.undo.push_back(e.buf);
                            e.buf = e.redo.back();
                            e.redo.pop_back();
                            e.caret = static_cast<int>(e.buf.size());
                            e.anchor = e.caret;
                            e.last_push_typing = false;
                        }
                        break;
                    default:
                        break;
                    }
                    v.ui.touch();
                    return true;
                }
                return false;
            }

            void rows(table_ctx &, std::vector<row_idx> &) {}

            void paint(const frame_ctx &c, frame_data &fr) const
            {
                const auto &e = c.v.edit;
                if (!e.active())
                    return;
                int ri = -1, ci = -1;
                for (int i = 0; i < c.g.row_count(); ++i)
                    if (c.g.row_index[static_cast<std::size_t>(i)] == e.row)
                        ri = i;
                for (int i = 0; i < c.g.col_count(); ++i)
                    if (c.g.col_index[static_cast<std::size_t>(i)] == e.col)
                        ci = i;
                if (ri < 0 || ci < 0)
                    return;
                rect cr =
                    clip_x({c.g.col_x_at(ci), c.g.row_y[static_cast<std::size_t>(ri)],
                            c.g.col_w[static_cast<std::size_t>(ci)],
                            c.g.row_h[static_cast<std::size_t>(ri)]},
                           c.g.clip_x0(ci), c.g.clip_x1(ci));
                cr = intersect(cr, c.g.body);
                if (cr.empty())
                    return;
                fr.draw.add_shape(stream::overlay, cr, {0.06F, 0.08F, 0.12F, 1.0F},
                                  c.p.cell_radius, {}, 0, 0);
                const float inner = std::max(cr.w - 2 * c.p.cell_pad, 1.0F);
                const float pre = text_width(
                    c.ft,
                    std::string_view(e.buf).substr(0, static_cast<std::size_t>(e.caret)));
                const float off = pre > inner ? pre - inner + 2.0F : 0.0F;
                if (e.has_sel())
                {
                    const float lo =
                        text_width(c.ft, std::string_view(e.buf).substr(0, e.sel_lo()));
                    const float hi =
                        text_width(c.ft, std::string_view(e.buf).substr(0, e.sel_hi()));
                    if (hi > lo)
                        fr.draw.add_shape(
                            stream::overlay,
                            {cr.x + c.p.cell_pad + lo - off, cr.y + 4, hi - lo,
                             std::max(cr.h - 8, 1.0F)},
                            {c.p.accent.r, c.p.accent.g, c.p.accent.b, 0.35F}, 0, {}, 0,
                            0);
                }
                const rect tx = intersect(
                    {cr.x + c.p.cell_pad - off, cr.y, inner + off, cr.h}, c.g.body);
                fr.draw.add_text(stream::overlay, tx, c.p.text,
                                 cr.y + cr.h * 0.5F + c.ft.px * 0.30F, e.buf, 0);
                const float cx = std::clamp(cr.x + c.p.cell_pad + pre - off, cr.x + 1,
                                            cr.x + cr.w - 2);
                fr.draw.add_shape(stream::overlay,
                                  {cx, cr.y + 4, 1.5F, std::max(cr.h - 8, 1.0F)},
                                  c.p.accent, 0, {}, 0, 0);
                if (!e.compose.empty())
                {
                    const float cw = text_width(c.ft, e.compose);
                    fr.draw.add_text(stream::overlay,
                                     {cx + 2, cr.y, std::max(cw + 4, 1.0F), cr.h},
                                     {c.p.accent.r, c.p.accent.g, c.p.accent.b, 0.9F},
                                     cr.y + cr.h * 0.5F + c.ft.px * 0.30F, e.compose, 0);
                    fr.draw.add_shape(stream::overlay,
                                      {cx + 2, cr.y + cr.h - 4, std::max(cw, 1.0F), 1},
                                      c.p.accent, 0, {}, 0, 0);
                }
                fr.draw.add_shape(stream::overlay, cr, {0, 0, 0, 0}, 0, c.p.accent, 1.5F,
                                  0);
            }
        };

        // 列宽：拖分隔条只发 begin/move/end，宽度写 view.col_w
        struct resize_feature
        {
            bool begin(table_ctx &c, const target &t, float x)
            {
                const int col = t.col_index;
                const float base =
                    col >= 0 && static_cast<std::size_t>(col) < c.v.cols.w.size()
                        ? c.v.cols.w[static_cast<std::size_t>(col)]
                        : 0.0F;
                return begin(c, col, x, base);
            }
            bool begin(table_ctx &c, int col, float x, float base)
            {
                c.v.cols.resize_col = col;
                c.v.cols.resize_x0 = x;
                c.v.cols.resize_base = base;
                c.v.ui.touch();
                return true;
            }
            bool move(table_ctx &c, float x)
            {
                auto &v = c.v;
                if (v.cols.resize_col >= 0 &&
                    static_cast<std::size_t>(v.cols.resize_col) < v.cols.w.size())
                {
                    const auto &def =
                        c.d.cols[static_cast<std::size_t>(v.cols.resize_col)];
                    v.cols.w[static_cast<std::size_t>(v.cols.resize_col)] =
                        std::clamp(v.cols.resize_base + (x - v.cols.resize_x0), def.min_w,
                                   def.max_w);
                    v.cols.user[static_cast<std::size_t>(v.cols.resize_col)] = 1;
                    v.ui.touch();
                }
                return true;
            }
            bool end(table_ctx &c)
            {
                c.v.cols.resize_col = -1;
                c.v.ui.touch();
                return true;
            }
            template <class E>
            bool on_command(table_ctx &, const E &)
            {
                return false;
            }
            void rows(table_ctx &, std::vector<row_idx> &) {}
            void paint(const frame_ctx &, frame_data &) const {}
        };

        // 列序：拖列头只发 begin/move/end，顺序写 view.order
        struct reorder_feature
        {
            bool begin(table_ctx &c, const target &t, float x)
            {
                return begin(c, t.col_index, x);
            }
            bool begin(table_ctx &c, int col, float x)
            {
                auto &v = c.v;
                v.cols.reorder_from = c.g.pos_of_col(col);
                v.cols.reorder_to = v.cols.reorder_from;
                v.cols.reorder_col = col;
                v.cols.reorder_x0 = x;
                v.ui.header_moved = false;
                v.ui.touch();
                return true;
            }
            bool move(table_ctx &c, float x)
            {
                auto &v = c.v;
                if (v.cols.reorder_from >= 0)
                {
                    if (std::abs(x - v.cols.reorder_x0) > 4.0F)
                        v.ui.header_moved = true;
                    const int to = c.g.pos_at(x);
                    if (to >= 0)
                        v.cols.reorder_to = to;
                    v.ui.touch();
                }
                return true;
            }
            int end(table_ctx &c)
            {
                auto &v = c.v;
                int clicked = -1;
                if (v.ui.header_moved)
                    v.cols.move(v.cols.reorder_from, v.cols.reorder_to);
                else
                    clicked = v.cols.reorder_col;
                v.cols.reorder_from = v.cols.reorder_to = -1;
                v.cols.reorder_col = -1;
                v.ui.touch();
                return clicked;
            }
            template <class E>
            bool on_command(table_ctx &, const E &)
            {
                return false;
            }
            void rows(table_ctx &, std::vector<row_idx> &) {}

            void paint(const frame_ctx &c, frame_data &fr) const
            {
                if (c.v.ui.drag != drag_mode::header || c.v.cols.reorder_from < 0)
                    return;
                const int from = c.v.cols.reorder_from;
                if (from >= static_cast<int>(c.v.cols.order.size()))
                    return;
                const int col = c.v.cols.order[static_cast<std::size_t>(from)];
                const int pos = c.g.pos_of_col(col);
                if (pos < 0)
                    return;
                const float gw = c.g.col_w[static_cast<std::size_t>(pos)];
                const float gx = c.v.ui.pointer.x - gw * 0.5F;
                const rect ghost{gx, c.g.col_header.y, gw, c.g.col_header.h};
                fr.draw.add_shape(
                    stream::overlay, ghost,
                    {c.p.header_bg.r, c.p.header_bg.g, c.p.header_bg.b, 0.85F},
                    c.p.cell_radius, c.p.accent, 1.5F, 0);
                if (col < c.d.cols_count())
                    fr.draw.add_text(
                        stream::overlay,
                        {ghost.x + c.p.cell_pad, ghost.y, gw - 2 * c.p.cell_pad, ghost.h},
                        c.p.header_text, ghost.y + ghost.h * 0.5F + c.ft.px * 0.30F,
                        c.d.cols[static_cast<std::size_t>(col)].title, 0);
                if (c.v.cols.reorder_to >= 0)
                {
                    const int to = c.v.cols.reorder_to;
                    const float xt =
                        to < c.g.col_count()
                            ? c.g.col_x_at(to)
                            : c.g.col_x_at(c.g.col_count() - 1) + c.g.col_w.back();
                    fr.draw.add_shape(
                        stream::overlay,
                        {xt - 1, c.g.col_header.y, 2, c.g.body.y1() - c.g.col_header.y},
                        c.p.accent, 0, {}, 0, 0);
                }
            }
        };

        // 冻结列：只写 view.frozen；几何在 sync 里消费
        struct freeze_feature
        {
            bool set(table_ctx &c, int n)
            {
                c.v.cols.frozen = n;
                c.v.ui.touch();
                return true;
            }
            template <class E>
            bool on_command(table_ctx &, const E &)
            {
                return false;
            }
            void rows(table_ctx &, std::vector<row_idx> &) {}
            void paint(const frame_ctx &, frame_data &) const {}
        };

        // 行勾选：命令 + 沟槽绘制
        struct checks_feature
        {
            bool toggle(table_ctx &c, const target &t, bool shift)
            {
                return toggle(c, t.row_index, shift);
            }
            bool toggle(table_ctx &c, int row, bool shift)
            {
                auto &v = c.v;
                if (row < 0 || static_cast<std::size_t>(row) >= v.checks.on.size())
                    return true;
                const bool on = v.checks.on[static_cast<std::size_t>(row)] == 0;
                if (shift && v.checks.last >= 0)
                {
                    // 区间按“可见顺序”走：排序/筛选后也只勾选当前看得到的行
                    const int pl = c.g.order_pos(v.checks.last);
                    const int pr = c.g.order_pos(row);
                    if (pl >= 0 && pr >= 0)
                    {
                        const int lo = std::min(pl, pr);
                        const int hi = std::max(pl, pr);
                        for (int i = lo; i <= hi; ++i)
                        {
                            const int r = c.g.view_rows[static_cast<std::size_t>(i)];
                            v.checks.on[static_cast<std::size_t>(r)] = on ? 1 : 0;
                            v.checks.ids[static_cast<std::size_t>(r)] =
                                c.d.ids[static_cast<std::size_t>(r)];
                        }
                    }
                    else
                    {
                        v.checks.on[static_cast<std::size_t>(row)] = on ? 1 : 0;
                        v.checks.ids[static_cast<std::size_t>(row)] =
                            c.d.ids[static_cast<std::size_t>(row)];
                    }
                }
                else
                {
                    v.checks.on[static_cast<std::size_t>(row)] = on ? 1 : 0;
                    v.checks.ids[static_cast<std::size_t>(row)] =
                        c.d.ids[static_cast<std::size_t>(row)];
                }
                v.checks.last = row;
                v.ui.touch();
                return true;
            }
            bool toggle_all(table_ctx &c)
            {
                auto &v = c.v;
                int on = 0;
                for (const int r : c.g.view_rows)
                    on += v.checks.on[static_cast<std::size_t>(r)] != 0;
                const bool all = !c.g.view_rows.empty() &&
                                 on == static_cast<int>(c.g.view_rows.size());
                for (const int r : c.g.view_rows)
                {
                    v.checks.on[static_cast<std::size_t>(r)] = all ? 0 : 1;
                    v.checks.ids[static_cast<std::size_t>(r)] =
                        c.d.ids[static_cast<std::size_t>(r)];
                }
                v.ui.touch();
                return true;
            }
            template <class E>
            bool on_command(table_ctx &, const E &)
            {
                return false;
            }
            void rows(table_ctx &, std::vector<row_idx> &) {}
            void paint(const frame_ctx &c, frame_data &fr) const
            {
                if (c.g.gutter_w <= 0)
                    return;
                fr.draw.add_shape(stream::chrome, c.g.gutter_head, c.p.header_bg, {},
                                  {1, 1, 1, 0.08F}, 1.0F, zone_key({part::check_head}));
                int total = 0, on = 0;
                for (const int r : c.g.view_rows)
                {
                    ++total;
                    on += c.v.checks.on[static_cast<std::size_t>(r)] != 0;
                }
                const bool all = total > 0 && on == total;
                const bool part = on > 0 && !all;
                const rect hb{c.g.gutter_head.x + (c.g.gutter_head.w - 14) * 0.5F,
                              c.g.gutter_head.y + (c.g.gutter_head.h - 14) * 0.5F, 14,
                              14};
                if (all)
                    fr.draw.add_shape(stream::chrome, hb, c.p.accent, 3, {}, 0,
                                      zone_key({part::check_head}));
                else
                    fr.draw.add_shape(stream::chrome, hb, {0, 0, 0, 0}, 3,
                                      {glm::vec3(c.p.accent), 0.55F}, 1.2F,
                                      zone_key({part::check_head}));
                if (part)
                    fr.draw.add_shape(stream::chrome,
                                      {hb.x + 3, hb.y + hb.h * 0.5F - 1, hb.w - 6, 2},
                                      c.p.accent, 1, {}, 0, zone_key({part::check_head}));

                for (int ri = 0; ri < c.g.row_count(); ++ri)
                {
                    const int dr = c.g.row_index[static_cast<std::size_t>(ri)];
                    const rect gr{c.g.gutter_body.x,
                                  c.g.row_y[static_cast<std::size_t>(ri)], c.g.gutter_w,
                                  c.g.row_h[static_cast<std::size_t>(ri)]};
                    const rect bx{gr.x + (gr.w - 14) * 0.5F, gr.y + (gr.h - 14) * 0.5F,
                                  14, 14};
                    const auto zk = zone_key({part::check, dr, -1});
                    if (c.v.checks.on[static_cast<std::size_t>(dr)] != 0)
                        fr.draw.add_shape(stream::chrome, bx, c.p.accent, 3, {}, 0, zk);
                    else
                        fr.draw.add_shape(stream::chrome, bx, {0, 0, 0, 0}, 3,
                                          {glm::vec3(c.p.accent), 0.45F}, 1.2F, zk);
                }
            }
        };

        // 滚动：命令 + 双滚动条绘制
        struct scroll_feature
        {
            bool begin(table_ctx &c, int axis, float base, float pos)
            {
                c.v.scroll.axis = axis;
                c.v.scroll.base = base;
                c.v.scroll.pos0 = pos;
                c.v.ui.touch();
                return true;
            }
            bool move(table_ctx &c, float pos)
            {
                auto &v = c.v;
                if (v.scroll.axis == 0)
                {
                    const float max_sy = std::max(0.0F, c.g.content_h - c.g.body.h);
                    const float travel = std::max(c.g.vtrack.h - c.g.vthumb.h, 1.0F);
                    v.scroll.y = std::clamp(v.scroll.base +
                                                (pos - v.scroll.pos0) * (max_sy / travel),
                                            0.0F, max_sy);
                }
                else
                {
                    const float max_sx =
                        std::max(0.0F, c.g.content_w - c.g.frozen_w - c.g.scrollv.w);
                    const float travel = std::max(c.g.htrack.w - c.g.hthumb.w, 1.0F);
                    v.scroll.x = std::clamp(v.scroll.base +
                                                (pos - v.scroll.pos0) * (max_sx / travel),
                                            0.0F, max_sx);
                }
                v.ui.touch();
                return true;
            }
            bool end(table_ctx &c)
            {
                c.v.scroll.axis = 0;
                c.v.ui.touch();
                return true;
            }
            template <class E>
            bool on_command(table_ctx &, const E &)
            {
                return false;
            }
            void rows(table_ctx &, std::vector<row_idx> &) {}
            void paint(const frame_ctx &c, frame_data &fr) const
            {
                if (c.g.vbar)
                {
                    fr.draw.add_shape(stream::chrome, c.g.vtrack, c.p.bar_track,
                                      c.p.bar * 0.5F, {}, 0,
                                      zone_key({part::vbar_track}));
                    const bool hot = c.v.ui.hover.kind == part::vbar_thumb ||
                                     c.v.ui.drag == drag_mode::vthumb;
                    fr.draw.add_shape(stream::chrome, c.g.vthumb,
                                      hot ? c.p.bar_thumb_hot : c.p.bar_thumb,
                                      c.p.bar * 0.5F, {}, 0,
                                      zone_key({part::vbar_thumb}));
                }
                if (c.g.hbar)
                {
                    fr.draw.add_shape(stream::chrome, c.g.htrack, c.p.bar_track,
                                      c.p.bar * 0.5F, {}, 0,
                                      zone_key({part::hbar_track}));
                    const bool hot = c.v.ui.hover.kind == part::hbar_thumb ||
                                     c.v.ui.drag == drag_mode::hthumb;
                    fr.draw.add_shape(stream::chrome, c.g.hthumb,
                                      hot ? c.p.bar_thumb_hot : c.p.bar_thumb,
                                      c.p.bar * 0.5F, {}, 0,
                                      zone_key({part::hbar_thumb}));
                }
            }
        };

        // 整表缩放：命令（无绘制）
        struct table_resize_feature
        {
            bool begin(table_ctx &c, float x, float y, float w, float h)
            {
                c.v.ui.trs_x0 = x;
                c.v.ui.trs_y0 = y;
                c.v.ui.trs_w = w;
                c.v.ui.trs_h = h;
                c.v.ui.touch();
                return true;
            }
            bool move(table_ctx &c, float x, float y)
            {
                auto &v = c.v;
                // 左/上把手：往左/上拖是放大，所以位移取反
                const part e = v.ui.trs_edge;
                const bool left =
                    e == part::edge_w || e == part::edge_nw || e == part::edge_sw;
                const bool top =
                    e == part::edge_n || e == part::edge_ne || e == part::edge_nw;
                const bool use_x = e != part::edge_n;
                const bool use_y = e != part::edge_w;
                float dx = x - v.ui.trs_x0;
                float dy = y - v.ui.trs_y0;
                if (left)
                    dx = -dx;
                if (top)
                    dy = -dy;
                v.cols.user_w = std::max(v.ui.trs_w + (use_x ? dx : 0.0F), c.p.min_w);
                v.cols.user_h = std::max(v.ui.trs_h + (use_y ? dy : 0.0F), c.p.min_h);
                v.ui.touch();
                return true;
            }
            bool end(table_ctx &c)
            {
                c.v.ui.touch();
                return true;
            }
            template <class E>
            bool on_command(table_ctx &, const E &)
            {
                return false;
            }
            void rows(table_ctx &, std::vector<row_idx> &) {}
            void paint(const frame_ctx &, frame_data &) const {}
        };

        // 剪贴板 / 删除：读 selection 切片，写 effect + undo
        struct clipboard_feature
        {
            static void clear_sel(table_ctx &c)
            {
                auto &v = c.v;
                if (v.sel.r0 < 0 || !c.fx)
                    return;
                std::vector<view::undo_change> batch;
                for (int r = std::min(v.sel.r0, v.sel.r1);
                     r <= std::max(v.sel.r0, v.sel.r1); ++r)
                    for (int cc = std::min(v.sel.c0, v.sel.c1);
                         cc <= std::max(v.sel.c0, v.sel.c1); ++cc)
                    {
                        const auto old = c.d.text(r, cc);
                        if (old.empty())
                            continue;
                        const auto id = c.d.ids[static_cast<std::size_t>(r)];
                        c.fx->push_back(fx_set_cell{id, cc, {}});
                        batch.push_back({id, cc, std::string(old), {}});
                    }
                if (!batch.empty())
                {
                    v.history.undo.push_back(std::move(batch));
                    v.history.redo.clear();
                    v.ui.touch();
                }
            }

            bool copy(table_ctx &c, bool cut)
            {
                auto &v = c.v;
                if (v.sel.r0 < 0 || !c.fx)
                    return true;
                std::string tsv;
                for (int r = std::min(v.sel.r0, v.sel.r1);
                     r <= std::max(v.sel.r0, v.sel.r1); ++r)
                {
                    for (int cc = std::min(v.sel.c0, v.sel.c1);
                         cc <= std::max(v.sel.c0, v.sel.c1); ++cc)
                    {
                        if (cc > std::min(v.sel.c0, v.sel.c1))
                            tsv += '\t';
                        tsv += c.d.text(r, cc);
                    }
                    tsv += '\n';
                }
                c.fx->push_back(fx_clipboard{std::move(tsv)});
                if (cut)
                    clear_sel(c);
                return true;
            }
            bool remove(table_ctx &c)
            {
                clear_sel(c);
                return true;
            }
            template <class E>
            bool on_command(table_ctx &, const E &)
            {
                return false;
            }
            void rows(table_ctx &, std::vector<row_idx> &) {}
            void paint(const frame_ctx &, frame_data &) const {}
        };

        // 撤销 / 重做：只消费 view.undo / view.redo
        struct undo_feature
        {
            bool run(table_ctx &c, bool redo)
            {
                auto &v = c.v;
                if (!c.fx)
                    return true;
                auto &from = redo ? v.history.redo : v.history.undo;
                auto &to = redo ? v.history.undo : v.history.redo;
                if (from.empty())
                    return true;
                auto batch = std::move(from.back());
                from.pop_back();
                for (const auto &ch : batch)
                    c.fx->push_back(
                        fx_set_cell{ch.id, ch.col, redo ? ch.after : ch.before});
                to.push_back(std::move(batch));
                v.ui.touch();
                return true;
            }
            template <class E>
            bool on_command(table_ctx &, const E &)
            {
                return false;
            }
            void rows(table_ctx &, std::vector<row_idx> &) {}
            void paint(const frame_ctx &, frame_data &) const {}
        };

        // 查找：Ctrl+F，输入，Enter 下一个，Esc 关闭
        struct find_feature
        {
            bool toggle(table_ctx &c)
            {
                auto &v = c.v;
                v.find.mode = !v.find.mode;
                v.find.buf.clear();
                v.find.hit = -1;
                v.ui.touch();
                return true;
            }
            bool close(table_ctx &c)
            {
                c.v.find.mode = false;
                c.v.ui.touch();
                return true;
            }
            bool next(table_ctx &c)
            {
                c.v.find.hit = next_hit(c.d, c.g, c.v);
                c.v.ui.touch();
                return true;
            }
            bool text(table_ctx &c, std::string_view s)
            {
                if (!c.v.find.mode)
                    return false;
                c.v.find.buf.append(s);
                c.v.find.compose.clear();
                c.v.find.hit = first_hit(c.d, c.g, c.v);
                if (c.v.find.hit >= 0)
                    ensure_visible(c.v, c.g, c.p, c.v.find.hit, 0);
                c.v.ui.touch();
                return true;
            }
            static bool match(const table_data &d, int r, int c,
                              const std::string &needle)
            {
                return !needle.empty() && filter_feature::contains(d.text(r, c), needle);
            }
            static int first_hit(const table_data &d, const geometry &g, const view &v)
            {
                for (const int r : g.view_rows)
                    for (int c = 0; c < d.cols_count(); ++c)
                        if (match(d, r, c, v.find.buf))
                            return r;
                return -1;
            }
            static int next_hit(const table_data &d, const geometry &g, const view &v)
            {
                if (v.find.buf.empty())
                    return -1;
                bool after = v.find.hit < 0;
                for (const int r : g.view_rows)
                {
                    if (!after)
                    {
                        if (r == v.find.hit)
                            after = true;
                        continue;
                    }
                    for (int c = 0; c < d.cols_count(); ++c)
                        if (match(d, r, c, v.find.buf))
                            return r;
                }
                for (const int r : g.view_rows)
                {
                    for (int c = 0; c < d.cols_count(); ++c)
                        if (match(d, r, c, v.find.buf))
                            return r;
                }
                return -1;
            }

            template <class E>
            bool on_command(table_ctx &c, const E &x)
            {
                if constexpr (std::is_same_v<E, cmd_key>)
                    return key(c, x);
                else if constexpr (std::is_same_v<E, cmd_press>)
                {
                    // 点别处 = 退出查找（让这次点击继续走正常的表内分派）
                    if (c.v.find.mode)
                    {
                        c.v.find.mode = false;
                        c.v.ui.touch();
                    }
                }
                return false;
            }
            bool key(table_ctx &c, const cmd_key &k)
            {
                auto &v = c.v;
                if (!v.find.mode)
                    return false;
                v.find.compose.clear();
                if (k.k == key::esc)
                    v.find.mode = false;
                else if (k.k == key::backspace && !v.find.buf.empty())
                {
                    const std::size_t q =
                        utf8_prev(std::string_view(v.find.buf), v.find.buf.size());
                    v.find.buf.erase(q);
                    v.find.hit = first_hit(c.d, c.g, v);
                    if (v.find.hit >= 0)
                        ensure_visible(v, c.g, c.p, v.find.hit, 0);
                }
                else if (k.k == key::enter)
                {
                    v.find.hit = next_hit(c.d, c.g, v);
                    if (v.find.hit >= 0)
                    {
                        v.sel.r0 = v.sel.r1 = v.sel.cur_r = v.find.hit;
                        v.sel.c0 = v.sel.c1 = v.sel.cur_c = 0;
                        ensure_visible(v, c.g, c.p, v.find.hit, 0);
                        v.ui.touch();
                    }
                }
                v.ui.touch();
                return true;
            }
            void rows(table_ctx &, std::vector<row_idx> &) {}
            void paint(const frame_ctx &c, frame_data &fr) const
            {
                if (c.v.find.mode)
                {
                    const float fy =
                        c.g.box.y >= 32.0F ? c.g.box.y - 28.0F : c.g.box.y + 4.0F;
                    const rect bar{c.g.box.x + 8, fy, c.g.box.w - 16, 22};
                    fr.draw.add_shape(stream::overlay, bar, {0.06F, 0.08F, 0.12F, 0.95F},
                                      4, c.p.accent, 1.0F, 0);
                    const std::string shown = c.v.find.buf + c.v.find.compose;
                    fr.draw.add_text(stream::overlay,
                                     {bar.x + 8, bar.y, bar.w - 16, bar.h}, c.p.text,
                                     bar.y + bar.h * 0.5F + c.ft.px * 0.30F,
                                     shown.empty() ? std::string_view{"Find..."}
                                                   : std::string_view{shown},
                                     0);
                }
                if (c.v.find.buf.empty())
                    return;
                for (int ri = 0; ri < c.g.row_count(); ++ri)
                {
                    const int dr = c.g.row_index[static_cast<std::size_t>(ri)];
                    for (int ci = 0; ci < c.g.col_count(); ++ci)
                    {
                        const int dc = c.g.col_index[static_cast<std::size_t>(ci)];
                        if (!match(c.d, dr, dc, c.v.find.buf))
                            continue;
                        rect cr = clip_x({c.g.col_x_at(ci),
                                          c.g.row_y[static_cast<std::size_t>(ri)],
                                          c.g.col_w[static_cast<std::size_t>(ci)],
                                          c.g.row_h[static_cast<std::size_t>(ri)]},
                                         c.g.clip_x0(ci), c.g.clip_x1(ci));
                        cr = intersect(cr, c.g.body);
                        if (cr.empty())
                            continue;
                        const float a = dr == c.v.find.hit ? 0.32F : 0.16F;
                        fr.draw.add_shape(stream::content, cr,
                                          {c.p.accent.r, c.p.accent.g, c.p.accent.b, a},
                                          0, {}, 0, 0);
                    }
                }
            }
        };

        // tooltip：悬停 0.6s 后显示 note（无 note 用文本）
        struct tooltip_feature
        {
            template <class E>
            bool on_command(table_ctx &c, const E &)
            {
                return false;
            }
            void rows(table_ctx &, std::vector<row_idx> &) {}
            void paint(const frame_ctx &c, frame_data &fr) const
            {
                if (c.v.ui.hover.kind != part::cell || c.v.ui.now - c.v.tip.hover_t < 0.6)
                    return;
                if (c.v.ui.left || c.v.ui.drag != drag_mode::none)
                    return;
                const int r = c.v.ui.hover.row_index;
                const int col = c.v.ui.hover.col_index;
                if (r < 0 || col < 0 || r >= c.d.rows() || col >= c.d.cols_count())
                    return;
                const auto &cell = c.d.at(r, col);
                std::string_view text = cell.note.empty() ? std::string_view{cell.text}
                                                          : std::string_view{cell.note};
                if (text.empty())
                    return;
                const float tw = text_width(c.ft, text);
                const float bw = std::min(tw + 16.0F, 360.0F);
                const float bh = c.ft.px + 12.0F;
                const float bx =
                    std::min(std::max(c.v.ui.pointer.x + 14.0F, c.g.box.x + 4),
                             c.g.box.x1() - bw - 4);
                const float by =
                    std::min(std::max(c.v.ui.pointer.y + 14.0F, c.g.box.y + 4),
                             c.g.box.y1() - bh - 4);
                const rect box{bx, by, bw, bh};
                fr.draw.add_shape(stream::overlay, box, {0.05F, 0.06F, 0.09F, 0.97F}, 4,
                                  {1, 1, 1, 0.15F}, 1.0F, 0);
                fr.draw.add_text(stream::overlay, {box.x + 8, box.y, box.w - 16, box.h},
                                 c.p.text, box.y + box.h * 0.5F + c.ft.px * 0.30F, text,
                                 0);
            }
        };

        // 菜单布局：paint 与 hit_test 共用，避免两套几何
        inline constexpr float menu_w = 150.0F;
        inline constexpr float menu_item_h = 24.0F;
        inline constexpr int menu_item_count = 4;
        [[nodiscard]] inline rect menu_box_rect(const view &v) noexcept
        {
            return {v.menu.x, v.menu.y, menu_w,
                    menu_item_h * static_cast<float>(menu_item_count) + 8.0F};
        }
        [[nodiscard]] inline rect menu_item_rect(const view &v, int i) noexcept
        {
            return {v.menu.x + 4.0F,
                    v.menu.y + 4.0F + menu_item_h * static_cast<float>(i), menu_w - 8.0F,
                    menu_item_h};
        }
        // 列菜单：Fit / Reset sort / Hide / Show all
        struct menu_feature
        {
            static constexpr const char *kItems[4] = {"Fit column", "Reset sort",
                                                      "Hide column", "Show all"};

            bool open(table_ctx &c, int col, float x, float y)
            {
                if (col < 0)
                    return false;
                auto &v = c.v;
                v.menu.open = true;
                v.menu.col = col;
                v.menu.x =
                    std::min(std::max(x, c.g.box.x + 4.0F), c.g.box.x1() - menu_w - 4.0F);
                v.menu.y = std::min(
                    std::max(y, c.g.box.y + 4.0F),
                    c.g.box.y1() - menu_item_h * static_cast<float>(menu_item_count) -
                        12.0F);
                v.menu.hover = -1;
                v.ui.touch();
                return true;
            }

            bool item(table_ctx &c, const target &t)
            {
                return item(c, t.col_index);
            }
            bool item(table_ctx &c, int index)
            {
                auto &v = c.v;
                if (v.menu.open && index >= 0)
                {
                    switch (index)
                    {
                    case 0: {
                        autofit_column(c, v.menu.col);
                        break;
                    }
                    case 1:
                        reset_sort(v);
                        break;
                    case 2: {
                        hide_column(c, v.menu.col);
                        break;
                    }
                    case 3:
                        show_all_columns(c);
                        break;
                    default:
                        break;
                    }
                }
                v.menu.open = false;
                v.ui.touch();
                return true;
            }

            template <class E>
            bool on_command(table_ctx &c, const E &x)
            {
                auto &v = c.v;
                if constexpr (std::is_same_v<E, cmd_press>)
                {
                    if (!v.menu.open)
                        return false;
                    if (x.z.kind != part::menu_item)
                    {
                        v.menu.open = false;
                        v.ui.touch();
                    }
                    return false;
                }
                else if constexpr (std::is_same_v<E, cmd_key>)
                {
                    if (!v.menu.open)
                        return false;
                    if (x.k == key::esc)
                    {
                        v.menu.open = false;
                        v.ui.touch();
                    }
                    return true;
                }
                return false;
            }
            void rows(table_ctx &, std::vector<row_idx> &) {}
            void paint(const frame_ctx &c, frame_data &fr) const
            {
                if (!c.v.menu.open)
                    return;
                const rect box = menu_box_rect(c.v);
                fr.draw.add_shape(stream::overlay, box, {0.07F, 0.08F, 0.12F, 0.98F}, 6,
                                  {1, 1, 1, 0.18F}, 1.0F, 0);
                for (int i = 0; i < menu_item_count; ++i)
                {
                    const rect it = menu_item_rect(c.v, i);
                    if (c.v.menu.hover == i)
                        fr.draw.add_shape(stream::overlay, it, {1, 1, 1, 0.06F}, 4, {}, 0,
                                          0);
                    fr.draw.add_text(stream::overlay, {it.x + 10, it.y, it.w - 20, it.h},
                                     c.p.text, it.y + it.h * 0.5F + c.ft.px * 0.30F,
                                     kItems[i], 0);
                    fr.zones.push_back({it, zone{part::menu_item, -1, i},
                                        static_cast<std::uint16_t>(20 + i)});
                }
            }
        };

        struct systems
        {
            filter_feature filter;
            find_feature find;
            sort_feature sort;
            resize_feature resize;
            reorder_feature reorder;
            freeze_feature freeze;
            checks_feature checks;
            scroll_feature scroll;
            table_resize_feature table_resize;
            selection_feature selection;
            edit_feature edit;
            clipboard_feature clipboard;
            undo_feature undo;
            menu_feature menu;
            tooltip_feature tooltip;

            template <class E>
            bool handle(table_ctx &c, const E &cmd)
            {
                if (filter.on_command(c, cmd))
                    return true;
                if (find.on_command(c, cmd))
                    return true;
                if (sort.on_command(c, cmd))
                    return true;
                if (resize.on_command(c, cmd))
                    return true;
                if (reorder.on_command(c, cmd))
                    return true;
                if (freeze.on_command(c, cmd))
                    return true;
                if (checks.on_command(c, cmd))
                    return true;
                if (scroll.on_command(c, cmd))
                    return true;
                if (table_resize.on_command(c, cmd))
                    return true;
                if (selection.on_command(c, cmd))
                    return true;
                if (edit.on_command(c, cmd))
                    return true;
                if (clipboard.on_command(c, cmd))
                    return true;
                if (undo.on_command(c, cmd))
                    return true;
                if (menu.on_command(c, cmd))
                    return true;
                if (tooltip.on_command(c, cmd))
                    return true;
                return false;
            }
            void rows(table_ctx &c, std::vector<row_idx> &r)
            {
                filter.rows(c, r);
                sort.rows(c, r);
            }
            void paint(const frame_ctx &c, frame_data &fr) const
            {
                filter.paint(c, fr);
                find.paint(c, fr);
                sort.paint(c, fr);
                resize.paint(c, fr);
                reorder.paint(c, fr);
                freeze.paint(c, fr);
                checks.paint(c, fr);
                scroll.paint(c, fr);
                table_resize.paint(c, fr);
                selection.paint(c, fr);
                edit.paint(c, fr);
                clipboard.paint(c, fr);
                undo.paint(c, fr);
                menu.paint(c, fr);
                tooltip.paint(c, fr);
            }
        };

        // 文本事件：按当前焦点分发给 filter / find / edit
    } // namespace sys

    using systems = sys::systems;
    using sys::filter_feature;
    using sys::menu_box_rect;
    using sys::menu_item_rect;
    using sys::menu_item_count;

    namespace input
    {
        inline bool system_text(table_ctx &c, systems &sy, std::string_view s)
        {
            if (c.v.filter.edit >= 0)
                return sy.filter.text(c, s);
            if (c.v.find.mode)
                return sy.find.text(c, s);
            return sy.edit.text(c, s);
        }

        // ═════════════════════════════════════════════════════════════════════
        // sync：data + view + prefs -> geometry（纯）
        // ═════════════════════════════════════════════════════════════════════
    } // namespace input

    using input::system_text;

    // ═ 派生层 derive：geometry + rows ═══════════════════════════════════
    namespace derive
    {
        inline void solve_columns(const table_data &d, const view &v, const prefs &p,
                                  float avail, geometry &g)
        {
            std::vector<int> vis;
            vis.reserve(v.cols.order.size());
            for (const int dc : v.cols.order)
            {
                if (dc < 0 || dc >= d.cols_count())
                    continue;
                if (static_cast<std::size_t>(dc) < v.cols.hidden.size() &&
                    v.cols.hidden[static_cast<std::size_t>(dc)] != 0)
                    continue;
                vis.push_back(dc);
            }
            const int n = static_cast<int>(vis.size());
            g.col_index.resize(static_cast<std::size_t>(n));
            g.col_x.resize(static_cast<std::size_t>(n));
            g.col_w.resize(static_cast<std::size_t>(n));
            float fixed = 0, weight = 0;
            for (int i = 0; i < n; ++i)
            {
                const int dc = vis[static_cast<std::size_t>(i)];
                g.col_index[static_cast<std::size_t>(i)] = dc;
                const auto &def = d.cols[static_cast<std::size_t>(dc)];
                const bool user = v.cols.user[static_cast<std::size_t>(dc)] != 0;
                const float cw =
                    std::clamp(user ? v.cols.w[static_cast<std::size_t>(dc)] : def.w,
                               def.min_w, def.max_w);
                g.col_w[static_cast<std::size_t>(i)] = cw;
                if (!user && def.weight > 0)
                    weight += def.weight;
                else
                    fixed += cw;
            }
            const float rest = avail - fixed;
            if (weight > 0 && rest > 0)
                for (int i = 0; i < n; ++i)
                {
                    const int dc = g.col_index[static_cast<std::size_t>(i)];
                    const auto &def = d.cols[static_cast<std::size_t>(dc)];
                    if (v.cols.user[static_cast<std::size_t>(dc)] != 0 || def.weight <= 0)
                        continue;
                    g.col_w[static_cast<std::size_t>(i)] =
                        std::clamp(rest * (def.weight / weight), def.min_w, def.max_w);
                }
            float x = 0;
            for (int i = 0; i < n; ++i)
            {
                g.col_x[static_cast<std::size_t>(i)] = x;
                x += g.col_w[static_cast<std::size_t>(i)];
            }
        }

        inline void ensure_metrics(const table_data &d, const view &v, const prefs &p,
                                   geometry &g, const font &ft)
        {
            float key = static_cast<float>(d.rows()) * 7.0F +
                        static_cast<float>(d.cols_count()) * 131.0F + p.row_h * 17.0F +
                        (p.wrap ? 977.0F : 0.0F);
            for (const float w : v.cols.w)
                key += w * 0.013F;
            if (key == g.row_h_key &&
                g.row_h_data.size() == static_cast<std::size_t>(d.rows()))
                return;
            g.row_h_key = key;
            g.row_h_data.assign(static_cast<std::size_t>(d.rows()), p.row_h);
            bool any_wrap = p.wrap;
            for (const auto &c : d.cols)
                any_wrap = any_wrap || c.wrap;
            if (!any_wrap)
                return;
            std::vector<std::string_view> lines;
            for (int r = 0; r < d.rows(); ++r)
            {
                float h = p.row_h;
                for (int c = 0; c < d.cols_count(); ++c)
                {
                    if (!d.cols[static_cast<std::size_t>(c)].wrap && !p.wrap)
                        continue;
                    const int pos = g.pos_of_col(c);
                    const float cw =
                        pos >= 0 ? g.col_w[static_cast<std::size_t>(pos)] : 100.0F;
                    wrap_lines(ft, d.text(r, c), std::max(cw - 2 * p.cell_pad, 4.0F),
                               lines);
                    h = std::max(
                        h, static_cast<float>(lines.size()) * (ft.px * ft.line_h) + 8.0F);
                }
                g.row_h_data[static_cast<std::size_t>(r)] = h;
            }
        }

        inline void sync(const table_data &d, view &v, const prefs &p, geometry &g,
                         const rect &viewport, const font &ft, systems &sy,
                         row_pipeline &pipe)
        {
            if (v.cols.order.size() != static_cast<std::size_t>(d.cols_count()) ||
                v.checks.on.size() != static_cast<std::size_t>(d.rows()))
                v.reconcile(d);
            g = geometry{};
            g.version = v.ui.version;

            float w = v.cols.user_w > 0 ? v.cols.user_w : viewport.w;
            float h = v.cols.user_h > 0 ? v.cols.user_h : viewport.h;
            const float maxw = std::max(p.min_w, std::min(p.max_w, viewport.w));
            const float maxh = std::max(p.min_h, std::min(p.max_h, viewport.h));
            w = std::clamp(w, p.min_w, maxw);
            h = std::clamp(h, p.min_h, maxh);
            g.box = {viewport.x, viewport.y, w, h};
            g.step = p.row_h + p.row_gap;

            const float bar = std::max(p.bar, 0.0F);
            g.group_h = d.any_group() ? std::max(p.group_h, 0.0F) : 0.0F;
            const float header_h = g.group_h + p.header_h;
            g.header = {g.box.x + 1, g.box.y + 1, g.box.w - 2 - bar, header_h};
            g.body = {g.header.x, g.header.y + header_h, g.header.w,
                      g.box.h - 2 - header_h - bar};
            if (g.body.h < 0)
                g.body.h = 0;
            g.group = {g.header.x, g.header.y, g.header.w, g.group_h};
            g.col_header = {g.header.x, g.header.y + g.group_h, g.header.w, p.header_h};

            g.gutter_w = p.row_select ? std::max(p.select_w, 0.0F) : 0.0F;
            if (g.gutter_w > g.body.w * 0.5F)
                g.gutter_w = 0;
            g.gutter_head = {g.col_header.x, g.col_header.y, g.gutter_w, g.col_header.h};
            g.gutter_body = {g.body.x, g.body.y, g.gutter_w, g.body.h};

            solve_columns(d, v, p, std::max(g.body.w - g.gutter_w, 1.0F), g);
            // 分组带：连续、同名 group 的可见列算一段；命中/整组选择都读这张表
            g.groups.clear();
            {
                int start = -1;
                std::string_view cur;
                const auto flush = [&](int end) {
                    if (start >= 0 && end >= start)
                        g.groups.push_back({start, end,
                                            g.col_index[static_cast<std::size_t>(start)],
                                            g.col_index[static_cast<std::size_t>(end)]});
                };
                for (int i = 0; i < g.col_count(); ++i)
                {
                    const auto &name =
                        d.cols[static_cast<std::size_t>(
                                   g.col_index[static_cast<std::size_t>(i)])]
                            .group;
                    if (name.empty())
                    {
                        flush(i - 1);
                        start = -1;
                        cur = {};
                        continue;
                    }
                    if (start < 0 || name != cur)
                    {
                        flush(i - 1);
                        start = i;
                        cur = name;
                    }
                }
                flush(g.col_count() - 1);
            }
            g.content_w = g.col_count() > 0
                              ? g.col_x.back() + g.col_w.back() - g.col_x.front()
                              : 0.0F;

            ensure_metrics(d, v, p, g, ft);

            pipe.base(d);
            {
                table_ctx ctx{d, v, p, g, ft, nullptr};
                sy.rows(ctx, pipe.buf);
            }
            g.view_rows.assign(pipe.buf.begin(), pipe.buf.end());
            // 当前格被筛掉：清选区（ability7 语义），避免编辑/勾选落到不可见行
            if (v.sel.cur_r >= 0 && std::find(g.view_rows.begin(), g.view_rows.end(),
                                              v.sel.cur_r) == g.view_rows.end())
            {
                v.sel = {};
                v.ui.touch();
            }
            g.row_off.assign(g.view_rows.size() + 1, 0.0F);
            float acc = 0;
            for (std::size_t i = 0; i < g.view_rows.size(); ++i)
            {
                g.row_off[i] = acc;
                acc += g.row_h_data[static_cast<std::size_t>(g.view_rows[i])] + p.row_gap;
            }
            g.row_off.back() = acc;
            g.content_h = g.view_rows.empty() ? 0.0F : acc - p.row_gap;

            const float bx = g.body.x + g.gutter_w;
            const float bw = std::max(g.body.w - g.gutter_w, 0.0F);
            g.frozen = std::clamp(v.cols.frozen >= 0 ? v.cols.frozen : p.frozen, 0,
                                  g.col_count());
            if (g.frozen > 0)
            {
                g.frozen_w = std::min(
                    g.col_x[static_cast<std::size_t>(g.frozen - 1)] +
                        g.col_w[static_cast<std::size_t>(g.frozen - 1)] - g.col_x.front(),
                    bw * 0.8F);
                g.frozen_view = {bx, g.body.y, g.frozen_w, g.body.h};
                g.scrollv = {bx + g.frozen_w, g.body.y, std::max(bw - g.frozen_w, 0.0F),
                             g.body.h};
            }
            else
            {
                g.frozen_w = 0;
                g.frozen_view = {bx, g.body.y, 0, g.body.h};
                g.scrollv = {bx, g.body.y, bw, g.body.h};
            }

            g.vbar = bar > 0 && g.content_h > g.body.h + 0.5F;
            g.hbar = bar > 0 && g.content_w - g.frozen_w > g.scrollv.w + 0.5F;
            const float max_sy = std::max(0.0F, g.content_h - g.body.h);
            const float max_sx = std::max(0.0F, g.content_w - g.frozen_w - g.scrollv.w);
            v.scroll.y = std::clamp(v.scroll.y, 0.0F, max_sy);
            v.scroll.x = std::clamp(v.scroll.x, 0.0F, max_sx);
            g.sx = v.scroll.x;
            g.sy = v.scroll.y;

            if (!g.view_rows.empty() && g.body.h > 0)
            {
                const auto it =
                    std::upper_bound(g.row_off.begin(), g.row_off.end(), g.sy);
                int first = std::clamp(static_cast<int>(it - g.row_off.begin()) - 1, 0,
                                       static_cast<int>(g.view_rows.size()) - 1);
                float y = g.body.y + g.row_off[static_cast<std::size_t>(first)] - g.sy;
                for (int i = first; i < static_cast<int>(g.view_rows.size()); ++i)
                {
                    const int dr = g.view_rows[static_cast<std::size_t>(i)];
                    const float rh = g.row_h_data[static_cast<std::size_t>(dr)];
                    if (y + rh > g.body.y && y < g.body.y1())
                    {
                        const float y0 = std::max(y, g.body.y);
                        const float y1 = std::min(y + rh, g.body.y1());
                        if (y1 - y0 > 0.5F)
                        {
                            g.row_index.push_back(dr);
                            g.row_y.push_back(y0);
                            g.row_h.push_back(y1 - y0);
                        }
                    }
                    y += rh + p.row_gap;
                    if (y >= g.body.y1())
                        break;
                }
            }

            if (bar > 0)
            {
                const float tx = g.box.x1() - bar - 1;
                const float ty = g.body.y + 2;
                const float th = std::max(g.body.h - 4, 1.0F);
                g.vtrack = {tx, ty, bar, th};
                if (g.vbar)
                {
                    const float ratio =
                        std::clamp(g.body.h / std::max(g.content_h, 1.0F), 0.06F, 1.0F);
                    const float hgt = std::max(th * ratio, 18.0F);
                    const float t = max_sy > 0 ? g.sy / max_sy : 0.0F;
                    g.vthumb = {tx, ty + (th - hgt) * t, bar, hgt};
                }
                const float hx = g.body.x + 2;
                const float hy = g.box.y1() - bar - 1;
                const float hw = std::max(g.body.w - 4, 1.0F);
                g.htrack = {hx, hy, hw, bar};
                if (g.hbar)
                {
                    const float ratio =
                        std::clamp(g.scrollv.w / std::max(g.content_w - g.frozen_w, 1.0F),
                                   0.06F, 1.0F);
                    const float wid = std::max(hw * ratio, 18.0F);
                    const float t = max_sx > 0 ? g.sx / max_sx : 0.0F;
                    g.hthumb = {hx + (hw - wid) * t, hy, wid, bar};
                }
                g.corner = {g.htrack.x1(), g.vtrack.y, bar, bar};
            }

            const float e = p.edge_hit;
            const float C = std::max(bar + 2.0F, e + 4.0F);
            const auto &fr = g.box;
            g.edge_n = {fr.x + C, fr.y - e, std::max(fr.w - 2 * C, 1.0F), 2 * e};
            float wh = fr.h - 2 * C;
            if (g.hbar)
                wh = std::min(wh, g.htrack.y - (fr.y + C));
            g.edge_w = {fr.x - e, fr.y + C, 2 * e, std::max(wh, 1.0F)};
            g.edge_nw = {fr.x - e, fr.y - e, C, C};
            g.edge_ne = {fr.x1() - C, fr.y - e, C, C};
            g.edge_sw = {fr.x - e, fr.y1() - C, C, C};
            g.edge_se = {fr.x1() - C, fr.y1() - C, C, C};

            assert(g.header.x == g.body.x && g.header.w == g.body.w);
            assert(g.col_header.h == p.header_h);
            assert(g.header.h == g.group_h + p.header_h);
        }

        // ═════════════════════════════════════════════════════════════════════
        // apply：唯一写 data / view 的地方
        // ═════════════════════════════════════════════════════════════════════
        inline bool in_sel(const view &v, int r, int c) noexcept
        {
            return v.sel.r0 >= 0 && v.sel.c0 >= 0 && r >= std::min(v.sel.r0, v.sel.r1) &&
                   r <= std::max(v.sel.r0, v.sel.r1) &&
                   c >= std::min(v.sel.c0, v.sel.c1) && c <= std::max(v.sel.c0, v.sel.c1);
        }

        inline void select_anchor(view &v, int r, int c)
        {
            v.sel.r0 = v.sel.r1 = v.sel.cur_r = r;
            v.sel.c0 = v.sel.c1 = v.sel.cur_c = c;
            v.ui.touch();
        }

        inline int view_pos_of(const geometry &g, int data_row)
        {
            for (int i = 0; i < static_cast<int>(g.view_rows.size()); ++i)
                if (g.view_rows[static_cast<std::size_t>(i)] == data_row)
                    return i;
            return 0;
        }

        inline void goto_cell(view &v, int r, int c, bool extend)
        {
            if (r < 0 || c < 0)
                return;
            if (!extend)
                select_anchor(v, r, c);
            else
            {
                v.sel.r1 = r;
                v.sel.c1 = c;
                v.sel.cur_r = r;
                v.sel.cur_c = c;
                v.ui.touch();
            }
        }

        inline void move_sel(view &v, const geometry &g, int dr, int dc, bool extend)
        {
            if (v.sel.cur_r < 0)
            {
                if (!g.view_rows.empty())
                    select_anchor(v, g.view_rows.front(), 0);
                return;
            }
            if (dc != 0)
            {
                goto_cell(v, v.sel.cur_r, std::max(0, v.sel.cur_c + dc), extend);
                return;
            }
            const int pos = view_pos_of(g, v.sel.cur_r);
            const int np =
                std::clamp(pos + dr, 0, static_cast<int>(g.view_rows.size()) - 1);
            goto_cell(v, g.view_rows[static_cast<std::size_t>(np)], v.sel.cur_c, extend);
        }

        inline void begin_edit(const table_data &d, view &v, int r, int c)
        {
            if (r < 0 || c < 0)
                return;
            v.edit.row = r;
            v.edit.col = c;
            v.edit.cell = d.cell_id(r, c);
            v.edit.buf = std::string(d.text(r, c));
            v.edit.orig = v.edit.buf;
            v.edit.caret = static_cast<int>(v.edit.buf.size());
            v.edit.anchor = v.edit.caret;
        }

    } // namespace derive

    using derive::sync;
    using derive::solve_columns;

    namespace input
    {
        // 连击计数：press 的分派顺序要求它在 systems 之前更新（edit 会先读它）
        inline void note_click(view &v, const cmd_press &c) noexcept
        {
            const bool near_last =
                !c.shift && v.ui.now - v.ui.last_click_t < 0.40 &&
                (c.x - v.ui.last_click_pos.x) * (c.x - v.ui.last_click_pos.x) +
                        (c.y - v.ui.last_click_pos.y) * (c.y - v.ui.last_click_pos.y) <
                    25.0F;
            v.ui.click_count =
                near_last ? std::min(std::max(v.ui.click_count, 1) + 1, 3) : 1;
            v.ui.last_click_t = v.ui.now;
            v.ui.last_click_pos = {c.x, c.y};
        }

        template <class E>
        inline void system_gesture(table_data &d, view &v, const prefs &p,
                                   const geometry &g, systems &sy, const font &ft,
                                   const E &c, std::vector<effect> &fx)
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            {
                using T = E;
                if constexpr (std::is_same_v<T, cmd_press>)
                {
                    const zone cz = c.z;
                    const target tgt =
                        c.t.kind != part::none ? c.t : target_of(d, {}, cz);
                    const zone z = tgt.to_zone();
                    v.ui.pointer = {c.x, c.y};
                    v.ui.left = true;
                    v.ui.drag_start_x = c.x;
                    v.ui.drag_start_y = c.y;
                    v.ui.header_moved = false;
                    const bool dbl = c.dbl || v.ui.click_count >= 2;
                    if (v.edit.active() && z.kind == part::cell && v.edit.row == z.row &&
                        v.edit.col == z.col)
                        return;
                    v.ui.drag = drag_mode::none;
                    v.ui.drag_target = tgt;
                    visit_owner(
                        to_owner(tgt),
                        overloaded{
                            [&](const cell_owner &) {
                                sy.selection.set_or_extend(tc, tgt, c.shift);
                                v.ui.drag = drag_mode::select;
                                if (dbl)
                                {
                                    sy.edit.begin(tc, tgt);
                                    if (v.ui.click_count >= 3)
                                        sys::edit_feature::select_all(v);
                                    else
                                        sy.edit.select_word(tc, c.x);
                                }
                            },
                            [&](const check_owner &) {
                                sy.checks.toggle(tc, tgt, c.shift);
                            },
                            [&](const check_all_owner &) { sy.checks.toggle_all(tc); },
                            [&](const header_owner &h) {
                                if (h.col_index < 0)
                                    return;
                                v.ui.drag = drag_mode::header;
                                sy.reorder.begin(tc, tgt, c.x);
                            },
                            [&](const sep_owner &h) {
                                if (h.col_index < 0)
                                    return;
                                if (v.ui.click_count >= 2)
                                {
                                    sys::autofit_column(tc, h.col_index);
                                    return;
                                }
                                v.ui.drag = drag_mode::col_w;
                                sy.resize.begin(tc, tgt, c.x);
                            },
                            [&](const filter_owner &h) {
                                if (h.col_index >= 0)
                                    sy.filter.begin(tc, tgt);
                            },
                            [&](const bar_owner &b) {
                                if (b.kind == part::vbar_thumb)
                                {
                                    v.ui.drag = drag_mode::vthumb;
                                    sy.scroll.begin(tc, 0, v.scroll.y, c.y);
                                }
                                else if (b.kind == part::hbar_thumb)
                                {
                                    v.ui.drag = drag_mode::hthumb;
                                    sy.scroll.begin(tc, 1, v.scroll.x, c.x);
                                }
                                else if (b.kind == part::vbar_track)
                                {
                                    // 点轨道 = 翻一页（点在滑块上方往上，下方往下）
                                    const float page = std::max(g.body.h * 0.9F, g.step);
                                    const float max_sy =
                                        std::max(0.0F, g.content_h - g.body.h);
                                    v.scroll.y = std::clamp(
                                        v.scroll.y + (c.y < g.vthumb.y ? -page : page),
                                        0.0F, max_sy);
                                    v.ui.touch();
                                }
                                else if (b.kind == part::hbar_track)
                                {
                                    const float page =
                                        std::max(g.scrollv.w * 0.9F, 10.0F);
                                    const float max_sx = std::max(
                                        0.0F, g.content_w - g.frozen_w - g.scrollv.w);
                                    v.scroll.x = std::clamp(
                                        v.scroll.x + (c.x < g.hthumb.x ? -page : page),
                                        0.0F, max_sx);
                                    v.ui.touch();
                                }
                            },
                            [&](const edge_owner &eo) {
                                v.ui.drag = drag_mode::table_size;
                                v.ui.trs_edge = eo.kind;
                                sy.table_resize.begin(
                                    tc, c.x, c.y,
                                    v.cols.user_w > 0 ? v.cols.user_w : g.box.w,
                                    v.cols.user_h > 0 ? v.cols.user_h : g.box.h);
                            },
                            [&](const menu_owner &) { sy.menu.item(tc, tgt); },
                            [&](const frame_owner &) { sy.selection.clear(tc); },
                            [&](const group_owner &o) {
                                sy.selection.all_of_group(tc, o.col_index);
                                v.ui.drag = drag_mode::none;
                            },
                            [&](const none_owner &) { sy.selection.clear(tc); }});
                    v.ui.touch();
                }
                else if constexpr (std::is_same_v<T, cmd_drag>)
                {
                    v.ui.pointer = {c.x, c.y};
                    v.ui.pointer = {c.x, c.y};
                    visit_drag(
                        v.ui.drag,
                        overloaded{[&](const drag_select &) {
                                       sy.selection.drag_extend(tc, c.x, c.y);
                                   },
                                   [&](const drag_col_w &) { sy.resize.move(tc, c.x); },
                                   [&](const drag_vthumb &) { sy.scroll.move(tc, c.y); },
                                   [&](const drag_hthumb &) { sy.scroll.move(tc, c.x); },
                                   [&](const drag_table_size &) {
                                       sy.table_resize.move(tc, c.x, c.y);
                                   },
                                   [&](const drag_header &) { sy.reorder.move(tc, c.x); },
                                   [&](const drag_none &) {}});
                    v.ui.touch();
                }
                else if constexpr (std::is_same_v<T, cmd_release>)
                {
                    v.ui.left = false;
                    if (v.ui.drag == drag_mode::header)
                    {
                        const int sc = sy.reorder.end(tc);
                        if (sc >= 0)
                        {
                            v.sort.toggle(sc);
                            sy.selection.clear(tc); // 排序后行位置变了，选区语义失效
                            v.ui.touch();
                        }
                    }
                    if (v.ui.drag == drag_mode::col_w)
                        sy.resize.end(tc);
                    if (v.ui.drag == drag_mode::vthumb || v.ui.drag == drag_mode::hthumb)
                        sy.scroll.end(tc);
                    if (v.ui.drag == drag_mode::table_size)
                        sy.table_resize.end(tc);
                    v.ui.drag = drag_mode::none;
                    v.ui.touch();
                }
                else if constexpr (std::is_same_v<T, cmd_key>)
                {
                    switch (c.k)
                    {
                    case key::up:
                        sy.selection.move(tc, -1, 0, c.shift);
                        break;
                    case key::down:
                        sy.selection.move(tc, 1, 0, c.shift);
                        break;
                    case key::left:
                        sy.selection.move(tc, 0, -1, c.shift);
                        break;
                    case key::right:
                        sy.selection.move(tc, 0, 1, c.shift);
                        break;
                    case key::home:
                        if (c.ctrl)
                            sy.selection.to(tc,
                                            g.view_rows.empty() ? 0 : g.view_rows.front(),
                                            0, c.shift);
                        else
                            sy.selection.to(tc, v.sel.cur_r, 0, c.shift);
                        break;
                    case key::end:
                        if (c.ctrl)
                            sy.selection.to(tc,
                                            g.view_rows.empty() ? d.rows() - 1
                                                                : g.view_rows.back(),
                                            d.cols_count() - 1, c.shift);
                        else
                            sy.selection.to(tc, v.sel.cur_r, d.cols_count() - 1, c.shift);
                        break;
                    case key::pgup:
                    case key::pgdn: {
                        const int page = std::max(
                            1, static_cast<int>(g.body.h / std::max(g.step, 1.0F)) - 1);
                        sy.selection.move(tc, c.k == key::pgup ? -page : page, 0,
                                          c.shift);
                        break;
                    }
                    case key::enter:
                        sy.selection.move(tc, c.shift ? -1 : 1, 0, false);
                        break;
                    case key::f2:
                        sy.edit.begin(tc, v.sel.cur_r, v.sel.cur_c);
                        break;
                    case key::tab: {
                        // 浏览器式 tab 顺序：行末换到下一行行首，行首 Shift+Tab 回上一行行末
                        const int ncol = g.col_count();
                        if (ncol <= 0)
                            break;
                        const int pos = std::max(0, g.order_pos(v.sel.cur_r));
                        int cp = g.pos_of_col(v.sel.cur_c);
                        if (cp < 0)
                            cp = 0;
                        cp += c.shift ? -1 : 1;
                        int rp = pos;
                        if (cp >= ncol)
                        {
                            cp = 0;
                            rp = std::min(rp + 1,
                                          static_cast<int>(g.view_rows.size()) - 1);
                        }
                        else if (cp < 0)
                        {
                            cp = ncol - 1;
                            rp = std::max(rp - 1, 0);
                        }
                        if (!g.view_rows.empty())
                            sy.selection.to(tc, g.view_rows[static_cast<std::size_t>(rp)],
                                            g.col_index[static_cast<std::size_t>(cp)],
                                            false);
                        break;
                    }
                    case key::del:
                    case key::backspace:
                        sy.clipboard.remove(tc);
                        break;
                    case key::space:
                        if (v.sel.cur_r >= 0)
                            sy.checks.toggle(tc, v.sel.cur_r, c.shift);
                        break;
                    case key::a:
                        if (c.ctrl)
                            sy.selection.all(tc);
                        break;
                    case key::c:
                    case key::x:
                        if (c.ctrl)
                            sy.clipboard.copy(tc, c.k == key::x);
                        break;
                    case key::z:
                    case key::y:
                        if (c.ctrl)
                            sy.undo.run(tc, c.k == key::y);
                        break;
                    case key::f:
                        if (c.ctrl)
                            sy.find.toggle(tc);
                        break;
                    default:
                        break;
                    }
                    v.ui.touch();
                }
            }
        }

        // ═════════════════════════════════════════════════════════════════════
        // hit_test
        // ═════════════════════════════════════════════════════════════════════

        inline zone hit_test(const geometry &g, float x, float y)
        {
            zone best{};
            const auto test = [&](const rect &r, const zone &z) {
                if (x >= r.x && x < r.x1() && y >= r.y && y < r.y1())
                    best = z;
            };
            test(g.box, {part::frame});
            if (g.group_h > 0)
            {
                for (const auto &sp : g.groups)
                {
                    const float x0 =
                        std::max(g.col_x_at(sp.first_pos), g.clip_x0(sp.first_pos));
                    const float x1 =
                        std::min(g.col_x_at(sp.last_pos) +
                                     g.col_w[static_cast<std::size_t>(sp.last_pos)],
                                 g.clip_x1(sp.last_pos));
                    if (x >= x0 && x < x1 && y >= g.group.y && y < g.group.y1())
                        best = {part::header_group, -1, sp.first_col};
                }
            }
            for (int i = 0; i < g.col_count(); ++i)
            {
                const int dc = g.col_index[static_cast<std::size_t>(i)];
                test(clip_x({g.col_x_at(i), g.col_header.y,
                             g.col_w[static_cast<std::size_t>(i)], g.col_header.h},
                            g.clip_x0(i), g.clip_x1(i)),
                     {part::header, -1, dc});
            }
            test(g.gutter_head, {part::check_head});
            for (int ri = 0; ri < g.row_count(); ++ri)
            {
                const int dr = g.row_index[static_cast<std::size_t>(ri)];
                const float yy = g.row_y[static_cast<std::size_t>(ri)];
                const float hh = g.row_h[static_cast<std::size_t>(ri)];
                for (int ci = 0; ci < g.col_count(); ++ci)
                {
                    const int dc = g.col_index[static_cast<std::size_t>(ci)];
                    rect cr = clip_x(
                        {g.col_x_at(ci), yy, g.col_w[static_cast<std::size_t>(ci)], hh},
                        g.clip_x0(ci), g.clip_x1(ci));
                    cr = intersect(cr, g.body);
                    test(cr, {part::cell, dr, dc});
                }
                if (g.gutter_w > 0)
                    test({g.gutter_body.x, yy, g.gutter_w, hh}, {part::check, dr, -1});
            }
            for (int i = 1; i < g.col_count(); ++i)
            {
                const int dc = g.col_index[static_cast<std::size_t>(i - 1)];
                const float xx = g.col_x_at(i);
                test(clip_x({xx - 3, g.col_header.y, 6, g.col_header.h}, g.clip_x0(i),
                            g.clip_x1(i)),
                     {part::col_sep, -1, dc});
            }
            if (g.vbar)
            {
                test(g.vtrack, {part::vbar_track});
                test(g.vthumb, {part::vbar_thumb});
            }
            if (g.hbar)
            {
                test(g.htrack, {part::hbar_track});
                test(g.hthumb, {part::hbar_thumb});
            }
            test(g.edge_n, {part::edge_n});
            test(g.edge_w, {part::edge_w});
            test(g.edge_ne, {part::edge_ne});
            test(g.edge_nw, {part::edge_nw});
            test(g.edge_se, {part::edge_se});
            test(g.edge_sw, {part::edge_sw});
            return best;
        }

        // 唯一拾取入口：菜单覆盖层 → 几何 → 过滤按钮；不再用 frame.zones 二次覆盖
        [[nodiscard]] inline zone hit_zone(const table_data &d, const geometry &g,
                                           const view &v, float x, float y,
                                           table_id tid = {})
        {
            if (v.menu.open)
            {
                for (int i = 0; i < menu_item_count; ++i)
                {
                    const rect r = menu_item_rect(v, i);
                    if (x >= r.x && x < r.x1() && y >= r.y && y < r.y1())
                        return {part::menu_item, -1, i, tid};
                }
                const rect mb = menu_box_rect(v);
                if (x >= mb.x && x < mb.x1() && y >= mb.y && y < mb.y1())
                    return {};
            }
            zone z = hit_test(g, x, y);
            z.table = tid;
            for (int i = 0; i < g.col_count(); ++i)
            {
                const int dc = g.col_index[static_cast<std::size_t>(i)];
                if (dc < 0 || dc >= d.cols_count() ||
                    !d.cols[static_cast<std::size_t>(dc)].filterable)
                    continue;
                const rect hr =
                    clip_x({g.col_x_at(i), g.col_header.y,
                            g.col_w[static_cast<std::size_t>(i)], g.col_header.h},
                           g.clip_x0(i), g.clip_x1(i));
                if (hr.w <= 24)
                    continue;
                // 漏斗的可点区域比图标大一圈，但停在分隔条热区之前（否则列宽拖不动）
                const rect btn{hr.x1() - 24, hr.y, 19, hr.h};
                if (x >= btn.x && x < btn.x1() && y >= btn.y && y < btn.y1())
                    return {part::filter_btn, -1, dc, tid};
            }
            return z;
        }

        template <class E>
        inline void system_event(table_data &d, view &v, const prefs &p,
                                 const geometry &g, systems &sy, const font &ft,
                                 const E &ev, std::vector<effect> &fx)
        {
            if constexpr (std::is_same_v<E, cmd_press>)
                note_click(v, ev);
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            if (!sy.handle(tc, ev))
                system_gesture(d, v, p, g, sy, ft, ev, fx);
        }

        // ═════════════════════════════════════════════════════════════════════
        // build_frame：data + view + geometry -> frame_data（纯）
        // ═════════════════════════════════════════════════════════════════════
    } // namespace input

    using input::system_gesture;
    using input::hit_test;
    using input::system_event;

    // ═ 呈现层 present：frame_data（DL + zones） ══════════════════════════
    namespace present
    {
        [[nodiscard]] inline target hit_target(const table_data &d, const geometry &g,
                                               const view &v, float x, float y,
                                               table_id tid = {})
        {
            return target_of(d, tid, input::hit_zone(d, g, v, x, y, tid));
        }
        inline void build_frame(const table_data &d, const view &v, const prefs &p,
                                const geometry &g, const font &ft, const systems &sy,
                                frame_data &out, table_id tid = {})
        {
            out.clear();
            out.version = v.ui.version;
            if (g.box.empty())
                return;
            auto &dl = out.draw;
            const auto &box = g.box;
            std::string scratch;
            const auto zk = [](const zone &z) {
                return zone_key(z);
            };

            out.visual.assign(static_cast<std::size_t>(g.row_count()) *
                                  static_cast<std::size_t>(g.col_count()),
                              0);
            for (int ri = 0; ri < g.row_count(); ++ri)
            {
                const int dr = g.row_index[static_cast<std::size_t>(ri)];
                for (int ci = 0; ci < g.col_count(); ++ci)
                {
                    const int dc = g.col_index[static_cast<std::size_t>(ci)];
                    std::uint8_t vis = 0;
                    if (v.edit.active() && v.edit.row == dr && v.edit.col == dc)
                        vis |= vis_editing;
                    out.visual[static_cast<std::size_t>(ri) *
                                   static_cast<std::size_t>(g.col_count()) +
                               static_cast<std::size_t>(ci)] = vis;
                }
            }

            // 外框
            dl.add_shape(stream::chrome, box, p.bg, p.radius, {1, 1, 1, 0.10F}, 1.0F,
                         zk({part::frame}));

            // 分组行
            if (g.group_h > 0)
            {
                int i = 0;
                while (i < g.col_count())
                {
                    const auto &def = d.cols[static_cast<std::size_t>(
                        g.col_index[static_cast<std::size_t>(i)])];
                    if (def.group.empty())
                    {
                        ++i;
                        continue;
                    }
                    int j = i;
                    while (j + 1 < g.col_count() &&
                           d.cols[static_cast<std::size_t>(
                                      g.col_index[static_cast<std::size_t>(j + 1)])]
                                   .group == def.group)
                        ++j;
                    for (int k = i; k <= j; ++k)
                    {
                        const rect gr =
                            clip_x({g.col_x_at(k), g.group.y,
                                    g.col_w[static_cast<std::size_t>(k)], g.group.h},
                                   g.clip_x0(k), g.clip_x1(k));
                        if (gr.empty())
                            continue;
                        dl.add_shape(stream::chrome, gr,
                                     {glm::vec3(p.header_bg) * 0.84F, p.header_bg.a}, 0,
                                     {}, 0,
                                     zk({part::header_group, -1,
                                         g.col_index[static_cast<std::size_t>(i)]}));
                    }
                    const bool froz = i < g.frozen;
                    const float base = g.body.x + g.gutter_w - (froz ? 0.0F : g.sx);
                    const float x0 = base + g.col_x[static_cast<std::size_t>(i)];
                    const float x1 = base + g.col_x[static_cast<std::size_t>(j)] +
                                     g.col_w[static_cast<std::size_t>(j)];
                    const rect band =
                        froz
                            ? rect{g.frozen_view.x, g.group.y, g.frozen_view.w, g.group.h}
                            : rect{g.scrollv.x, g.group.y, g.scrollv.w, g.group.h};
                    const rect tr = intersect({x0, g.group.y, x1 - x0, g.group.h}, band);
                    if (tr.w > 2)
                    {
                        const auto s = fit_text(ft, def.group, std::max(tr.w - 12, 1.0F),
                                                dl.ar, scratch);
                        const float tw = text_width(ft, s);
                        dl.add_text(stream::chrome,
                                    {tr.x + std::max((tr.w - tw) * 0.5F, 0.0F), tr.y,
                                     std::max(tw, 1.0F), tr.h},
                                    {0.80F, 0.84F, 0.92F, 1.0F},
                                    tr.y + tr.h * 0.5F + ft.px * 0.30F, s,
                                    zk({part::header_group, -1,
                                        g.col_index[static_cast<std::size_t>(i)]}));
                    }
                    i = j + 1;
                }
            }

            // 列头
            for (int i = 0; i < g.col_count(); ++i)
            {
                const int dc = g.col_index[static_cast<std::size_t>(i)];
                const rect hr =
                    clip_x({g.col_x_at(i), g.col_header.y,
                            g.col_w[static_cast<std::size_t>(i)], g.col_header.h},
                           g.clip_x0(i), g.clip_x1(i));
                if (hr.empty())
                    continue;
                glm::vec4 bg = p.header_bg;
                if (v.ui.band_col == dc)
                    bg = glm::vec4(glm::vec3(bg) + glm::vec3(0.06F), bg.a);
                dl.add_shape(stream::chrome, hr, bg, p.cell_radius, {}, 0,
                             zone_key({part::header, -1, dc}));
                const auto &def = d.cols[static_cast<std::size_t>(dc)];
                const float inner = std::max(
                    hr.w - 2 * p.cell_pad - (def.filterable ? 18.0F : 0.0F), 1.0F);
                const float tw = text_width(ft, def.title);
                float dx = 0;
                if (def.a == align::center)
                    dx = std::max((inner - tw) * 0.5F, 0.0F);
                else if (def.a == align::right)
                    dx = std::max(inner - tw, 0.0F);
                dl.add_text(stream::chrome,
                            {hr.x + p.cell_pad + dx, hr.y, inner - dx, hr.h},
                            p.header_text, hr.y + hr.h * 0.5F + ft.px * 0.30F, def.title,
                            zone_key({part::header, -1, dc}));
            }

            // 沟槽
            if (g.gutter_w > 0)
            {
            }

            // 行带 + 空表
            for (int ri = 0; ri < g.row_count(); ++ri)
            {
                const int dr = g.row_index[static_cast<std::size_t>(ri)];
                const rect rr =
                    intersect({g.body.x, g.row_y[static_cast<std::size_t>(ri)], g.body.w,
                               g.row_h[static_cast<std::size_t>(ri)]},
                              g.body);
                if (rr.empty())
                    continue;
                glm::vec4 bg = p.row_bg[static_cast<std::size_t>(dr) & 1U];
                if (v.ui.band_row == dr)
                    bg = p.row_hover;
                dl.add_shape(stream::chrome, rr, bg, 0, {}, 0, zk({part::cell, dr, -1}));
            }
            if (g.view_rows.empty())
                dl.add_text(stream::chrome, {g.body.x, g.body.y, g.body.w, g.body.h},
                            {0.55F, 0.58F, 0.65F, 1.0F},
                            g.body.y + g.body.h * 0.5F + ft.px * 0.30F, "No data",
                            zk({part::frame}));

            // 单元格
            for (int ri = 0; ri < g.row_count(); ++ri)
            {
                const int dr = g.row_index[static_cast<std::size_t>(ri)];
                for (int ci = 0; ci < g.col_count(); ++ci)
                {
                    const int dc = g.col_index[static_cast<std::size_t>(ci)];
                    rect cr =
                        clip_x({g.col_x_at(ci), g.row_y[static_cast<std::size_t>(ri)],
                                g.col_w[static_cast<std::size_t>(ci)],
                                g.row_h[static_cast<std::size_t>(ri)]},
                               g.clip_x0(ci), g.clip_x1(ci));
                    cr = intersect(cr, g.body);
                    if (cr.empty())
                        continue;
                    const auto z = zk({part::cell, dr, dc});
                    if (v.ui.band_col == dc)
                        dl.add_shape(stream::content, cr, p.col_hover, p.cell_radius, {},
                                     0, z);
                    const auto &def = d.cols[static_cast<std::size_t>(dc)];
                    const auto text = d.text(dr, dc);
                    if (text.empty())
                        continue;
                    const float inner = std::max(cr.w - 2 * p.cell_pad, 1.0F);
                    const float baseline = cr.y + cr.h * 0.5F + ft.px * 0.30F;
                    if (def.render == render_kind::tag)
                    {
                        const auto s = fit_text(ft, text, std::max(inner - 16, 1.0F),
                                                dl.ar, scratch);
                        const float tw = text_width(ft, s);
                        const float pw = std::min(tw + 16, inner);
                        const auto tc = def.tag_color(s, {0.42F, 0.45F, 0.50F, 1.0F});
                        const rect pill{cr.x + p.cell_pad, cr.y + (cr.h - 20) * 0.5F, pw,
                                        20};
                        dl.add_shape(stream::content, pill, {tc.r, tc.g, tc.b, 0.30F}, 10,
                                     {}, 0, z);
                        dl.add_text(stream::content,
                                    {pill.x + 8, pill.y, pw - 16, pill.h},
                                    {tc.r * 0.5F + 0.5F, tc.g * 0.5F + 0.5F,
                                     tc.b * 0.5F + 0.5F, 1},
                                    pill.y + pill.h * 0.5F + ft.px * 0.30F, s, z);
                    }
                    else if (def.render == render_kind::dot)
                    {
                        const auto dc2 = def.tag_color(text, {0.42F, 0.45F, 0.50F, 1.0F});
                        dl.add_shape(stream::content,
                                     {cr.x + p.cell_pad, cr.y + (cr.h - 8) * 0.5F, 8, 8},
                                     dc2, 4, {}, 0, z);
                        const auto s = fit_text(ft, text, std::max(inner - 16, 1.0F),
                                                dl.ar, scratch);
                        dl.add_text(stream::content,
                                    {cr.x + p.cell_pad + 16, cr.y, inner - 16, cr.h},
                                    p.text, baseline, s, z);
                    }
                    else if (def.render == render_kind::bar)
                    {
                        float frac = 0;
                        std::size_t q = 0;
                        double val = 0;
                        bool any = false, pct = false;
                        while (q < text.size() && (text[q] == ' ' || text[q] == '\t'))
                            ++q;
                        while (q < text.size() && text[q] >= '0' && text[q] <= '9')
                        {
                            val = val * 10 + (text[q] - '0');
                            any = true;
                            ++q;
                        }
                        while (q < text.size() && (text[q] == ' ' || text[q] == '%'))
                        {
                            pct = pct || text[q] == '%';
                            ++q;
                        }
                        if (any)
                            frac = static_cast<float>(pct || val > 1 ? val / 100.0 : val);
                        frac = std::clamp(frac, 0.0F, 1.0F);
                        const float by = cr.y + (cr.h - 6) * 0.5F + 3;
                        dl.add_shape(stream::content, {cr.x + p.cell_pad, by, inner, 6},
                                     {1, 1, 1, 0.10F}, 3, {}, 0, z);
                        dl.add_shape(
                            stream::content,
                            {cr.x + p.cell_pad, by, std::max(inner * frac, 2.0F), 6},
                            p.accent, 3, {}, 0, z);
                    }
                    else
                    {
                        if (def.wrap || p.wrap)
                        {
                            // 折行：行高在 sync 里按 wrap_lines 算过，这里逐行画
                            std::vector<std::string_view> lines;
                            wrap_lines(ft, text, inner, lines);
                            const float lh = ft.px * ft.line_h;
                            float ty = cr.y + 4.0F;
                            for (const auto ln : lines)
                            {
                                if (ty >= cr.y1())
                                    break;
                                const float tw = text_width(ft, ln);
                                float dx = 0;
                                if (def.a == align::center)
                                    dx = std::max((inner - tw) * 0.5F, 0.0F);
                                else if (def.a == align::right)
                                    dx = std::max(inner - tw, 0.0F);
                                dl.add_text(stream::content,
                                            {cr.x + p.cell_pad + dx, ty,
                                             std::max(inner - dx, 1.0F),
                                             std::min(lh, cr.y1() - ty)},
                                            p.text, ty + ft.px * 0.85F, ln, z);
                                ty += lh;
                            }
                        }
                        else
                        {
                            const auto s = fit_text(ft, text, inner, dl.ar, scratch);
                            const float tw = text_width(ft, s);
                            float dx = 0;
                            if (def.a == align::center)
                                dx = std::max((inner - tw) * 0.5F, 0.0F);
                            else if (def.a == align::right)
                                dx = std::max(inner - tw, 0.0F);
                            dl.add_text(stream::content,
                                        {cr.x + p.cell_pad + dx, cr.y, inner - dx, cr.h},
                                        p.text, baseline, s, z);
                        }
                    }
                }
            }

            // 列分隔
            for (int i = 1; i < g.col_count(); ++i)
            {
                const int dc = g.col_index[static_cast<std::size_t>(i - 1)];
                const float xx = g.col_x_at(i);
                const auto z = zk({part::col_sep, -1, dc});
                dl.add_shape(stream::chrome,
                             clip_x({xx - 0.5F, g.col_header.y, 1, g.col_header.h},
                                    g.clip_x0(i), g.clip_x1(i)),
                             p.sep, 0, {}, 0, z);
                dl.add_shape(stream::chrome,
                             clip_x({xx - 0.5F, g.body.y, 1, g.body.h}, g.clip_x0(i),
                                    g.clip_x1(i)),
                             p.sep, 0, {}, 0, z);
            }

            // 滚动条
            if (g.vbar)
            {
            }

            // 代理（最后画 = 拾取赢）：直接由 hit_test 的同一套几何生成
            const auto add_zone = [&](const rect &r, const zone &z) {
                if (!r.empty())
                    out.zones.push_back({r, z, 0});
            };
            add_zone(g.box, {part::frame});
            if (g.group_h > 0)
                for (const auto &sp : g.groups)
                {
                    const float x0 =
                        std::max(g.col_x_at(sp.first_pos), g.clip_x0(sp.first_pos));
                    const float x1 =
                        std::min(g.col_x_at(sp.last_pos) +
                                     g.col_w[static_cast<std::size_t>(sp.last_pos)],
                                 g.clip_x1(sp.last_pos));
                    add_zone({x0, g.group.y, x1 - x0, g.group.h},
                             {part::header_group, -1, sp.first_col});
                }
            for (int i = 0; i < g.col_count(); ++i)
            {
                const rect hz =
                    clip_x({g.col_x_at(i), g.col_header.y,
                            g.col_w[static_cast<std::size_t>(i)], g.col_header.h},
                           g.clip_x0(i), g.clip_x1(i));
                const int dc = g.col_index[static_cast<std::size_t>(i)];
                add_zone(hz, {part::header, -1, dc});
                if (dc >= 0 && dc < d.cols_count() &&
                    d.cols[static_cast<std::size_t>(dc)].filterable && hz.w > 24)
                    add_zone({hz.x1() - 24, hz.y, 19, hz.h}, {part::filter_btn, -1, dc});
            }
            add_zone(g.gutter_head, {part::check_head});
            for (int ri = 0; ri < g.row_count(); ++ri)
            {
                const int dr = g.row_index[static_cast<std::size_t>(ri)];
                for (int ci = 0; ci < g.col_count(); ++ci)
                    add_zone(intersect(clip_x({g.col_x_at(ci),
                                               g.row_y[static_cast<std::size_t>(ri)],
                                               g.col_w[static_cast<std::size_t>(ci)],
                                               g.row_h[static_cast<std::size_t>(ri)]},
                                              g.clip_x0(ci), g.clip_x1(ci)),
                                       g.body),
                             {part::cell, dr, g.col_index[static_cast<std::size_t>(ci)]});
                if (g.gutter_w > 0)
                    add_zone({g.gutter_body.x, g.row_y[static_cast<std::size_t>(ri)],
                              g.gutter_w, g.row_h[static_cast<std::size_t>(ri)]},
                             {part::check, dr, -1});
            }
            for (int i = 1; i < g.col_count(); ++i)
            {
                const int dc = g.col_index[static_cast<std::size_t>(i - 1)];
                add_zone(clip_x({g.col_x_at(i) - 3, g.col_header.y, 6, g.col_header.h},
                                g.clip_x0(i), g.clip_x1(i)),
                         {part::col_sep, -1, dc});
            }
            if (g.vbar)
            {
                add_zone(g.vtrack, {part::vbar_track});
                add_zone(g.vthumb, {part::vbar_thumb});
            }
            if (g.hbar)
            {
                add_zone(g.htrack, {part::hbar_track});
                add_zone(g.hthumb, {part::hbar_thumb});
            }
            add_zone(g.edge_n, {part::edge_n});
            add_zone(g.edge_w, {part::edge_w});
            add_zone(g.edge_ne, {part::edge_ne});
            add_zone(g.edge_nw, {part::edge_nw});
            add_zone(g.edge_se, {part::edge_se});
            add_zone(g.edge_sw, {part::edge_sw});
            // feature 的绘制贡献；放在基础 zone 之后，菜单等 zone 才能赢拾取
            {
                frame_ctx fc{d, v, p, g, ft};
                sy.paint(fc, out);
            }
            // 装饰性 op（背景/外框/tooltip 底）也必须有主：默认归 table frame。
            // 更具体的 zone 在 emit 时已经给了，不会被覆盖。
            const std::uint32_t frame_zone = zk({part::frame});
            for (auto &ss : out.draw.shapes)
                for (auto &o : ss)
                    if (o.zone == 0)
                        o.zone = frame_zone;
            for (auto &tt : out.draw.texts)
                for (auto &o : tt)
                    if (o.zone == 0)
                        o.zone = frame_zone;
            const std::uint32_t tbits = (tid.value & bits::kTableMask)
                                        << bits::kTableShift;
            if (tbits != 0)
            {
                for (auto &ss : out.draw.shapes)
                    for (auto &o : ss)
                        if (o.zone != 0)
                            o.zone |= tbits;
                for (auto &tt : out.draw.texts)
                    for (auto &o : tt)
                        if (o.zone != 0)
                            o.zone |= tbits;
            }
            for (auto &zr : out.zones)
                zr.z.table = tid;
            for (const auto &z : out.zones)
                dl.add_shape(stream::proxy, z.r, {0, 0, 0, 0}, 0, {}, 0, zk(z.z));
            // every op（含 proxy）都必须带 owner：新增 part 时 to_owner 会先返回
            // none_owner，selftest 的 “no owner” 断言会当场失败。
            if (tid.value != 0)
            {
                const auto assign_who = [&](auto &ops) {
                    for (auto &o : ops)
                        if (o.zone != 0)
                            o.who = to_owner(target_of(d, tid, zone_from(o.zone)));
                };
                for (auto &ss : out.draw.shapes)
                    assign_who(ss);
                for (auto &tt : out.draw.texts)
                    assign_who(tt);
            }
        }

        // ── 自测 ──────────────────────────────────────────────────────────────
    } // namespace present

    using present::build_frame;
    using present::hit_target;

    [[nodiscard]] inline target hit_test(const table_data &d, const geometry &g,
                                         const view &v, float x, float y,
                                         table_id tid = {})
    {
        return hit_target(d, g, v, x, y, tid);
    }

    inline int selftest()
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

        table_data d;
        {
            column c;
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
            c.render = render_kind::tag;
            d.cols.push_back(c);
        }
        d.finalize();
        check(d.column_of(0) == column_id{1} && d.column_of(2) == column_id{3},
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
            d.ids.push_back(row_id{static_cast<std::uint64_t>(i + 1)});
        }

        view v;
        v.reset(d);
        prefs p;
        geometry g;
        row_pipeline pipe;
        systems sy;
        font ft;
        ft.px = 15.0F;
        glyph_metrics gm;
        gm.advance = 0.5F;
        for (int c = 32; c < 127; ++c)
            ft.put(static_cast<char32_t>(c), gm);

        std::vector<effect> fx;
        // 和 submit 完全相同的分发：语义命令 → feature，没人认领 → apply
        auto send = [&]<class E>(const E &c) {
            system_event(d, v, p, g, sy, ft, c, fx);
        };
        auto send_text = [&](std::string_view s) {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            system_text(tc, sy, s);
        };
        auto send_begin = [&](int r, int col) {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.edit.begin(tc, r, col);
        };

        const rect vp{0, 0, 800, 600};
        sync(d, v, p, g, vp, ft, sy, pipe);
        check(g.col_count() == 3 && g.row_count() == 3, "sync geometry");
        check(g.header.w == g.body.w, "header/body width");

        send(cmd_press{10, 10, {part::cell, 1, 0}, false, false, false});
        check(v.sel.cur_r == 1 && v.sel.cur_c == 0, "press selects");
        check(v.sel.cur_cell.row == d.ids[1] && v.sel.cur_cell.col == d.column_of(0),
              "selection carries cell_ref");
        sync(d, v, p, g, vp, ft, sy, pipe);

        // G1：Shift+单击 = 从 anchor 扩选；点非格区域 = 清选区
        v.ui.last_click_t = -1.0e9;
        send(cmd_press{10, 10, {part::cell, 2, 1}, false, true, false});
        check(v.sel.r0 == 1 && v.sel.c0 == 0 && v.sel.r1 == 2 && v.sel.c1 == 1,
              "shift+click extends from anchor");
        send(cmd_press{10, 10, {part::none}, false, false, false});
        check(v.sel.r0 < 0, "click on non-cell area clears selection");
        send(cmd_release{10, 10});
        v.ui.last_click_t = -1.0e9;

        // G3：点分组带 = 选中整组（用一张带 group 的独立小表）
        {
            table_data dg;
            column gc;
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
            dg.ids.push_back(row_id{1});
            dg.ids.push_back(row_id{2});
            view vg;
            vg.reset(dg);
            geometry gg;
            row_pipeline pg;
            sync(dg, vg, p, gg, vp, ft, sy, pg);
            const auto gz =
                hit_test(dg, gg, vg, gg.col_x_at(1) + 5.0F, gg.group.y + 5.0F);
            check(gz.to_zone().kind == part::header_group && gz.to_zone().col >= 0,
                  "group hit test carries first column");
            {
                const cmd_press press{gg.col_x_at(1) + 5.0F,
                                      gg.group.y + 5.0F,
                                      {part::header_group, -1, gz.to_zone().col},
                                      false,
                                      false,
                                      false};
                system_event(dg, vg, p, gg, sy, ft, press, fx);
            }
            check(vg.sel.c0 == 0 && vg.sel.c1 == 1 && vg.sel.r0 == 0 &&
                      vg.sel.r1 == dg.rows() - 1,
                  "group header click selects whole group");
        }

        // G5：键盘导航把活动格滚进视口（短视口）
        {
            sync(d, v, p, g, {0, 0, 800, 120}, ft, sy, pipe);
            v.scroll.y = 0;
            v.ui.last_click_t = -1.0e9;
            send(cmd_press{g.scrollv.x + 20,
                           g.row_y.empty() ? g.body.y + 5 : g.row_y.front() + 5,
                           {part::cell, g.view_rows.front(), 0},
                           false,
                           false,
                           false});
            for (int i = 0; i < 4; ++i)
                send(cmd_key{key::down, false, false});
            check(v.sel.cur_r == g.view_rows.back(), "keyboard down reaches last row");
            check(v.scroll.y > 0.0F, "keyboard navigation auto-scrolls");
            sync(d, v, p, g, {0, 0, 800, 120}, ft, sy, pipe);
            check(g.row_index.back() == v.sel.cur_r,
                  "active row is inside the viewport after scroll");
            send(cmd_release{g.scrollv.x + 20, g.body.y + 5});
            sync(d, v, p, g, vp, ft, sy, pipe);
            v.scroll.y = 0;
        }

        frame_data f;
        build_frame(d, v, p, g, ft, sy, f, table_id{7});
        check(f.draw.op_count() > 0, "frame has ops");
        check(!f.zones.empty(), "zones");
        {
            frame_data fo;
            build_frame(d, v, p, g, ft, sy, fo, table_id{7});
            bool owned = false;
            for (const auto &ss : fo.draw.shapes)
                for (const auto &o : ss)
                    if (o.zone != 0 && std::holds_alternative<cell_owner>(o.who))
                    {
                        const auto &co = std::get<cell_owner>(o.who);
                        owned = co.table == table_id{7};
                    }
            check(owned, "DL op carries typed owner");
        }
        build_frame(d, v, p, g, ft, sy, f, table_id{7});
        build_frame(d, v, p, g, ft, sy, f, table_id{7});
        check(f.draw.op_count() > 0, "frame reusable (arena reset)");

        fx.clear();
        v.ui.last_click_t = -1.0e9;
        send(cmd_press{10, 10, {part::cell, 1, 0}, false, false, false});
        sync(d, v, p, g, vp, ft, sy, pipe);
        send(cmd_key{key::f2, false, false});
        check(v.edit.active(), "F2 begins edit");
        send_text("X");
        send(cmd_key{key::enter, false, false});
        check(!v.edit.active(), "enter commits");
        bool has = false;
        for (const auto &e : fx)
            if (const auto *q = std::get_if<fx_set_cell>(&e))
            {
                has = true;
                check(q->id == row_id{2}, "effect id");
            }
        check(has, "commit effect");

        fx.clear();
        send(cmd_press{10, 70, {part::header, -1, 0}, false, false, false});
        send(cmd_release{10, 70});
        sync(d, v, p, g, vp, ft, sy, pipe);
        check(!g.view_rows.empty() && d.text(g.view_rows.front(), 0) == "alpha.dat",
              "sort ascending");
        check(d.ids[static_cast<std::size_t>(g.view_rows.front())] == row_id{2},
              "row_id stable under sort");
        // 三态：再点两次 = 降序、取消
        send(cmd_press{10, 70, {part::header, -1, 0}, false, false, false});
        send(cmd_release{10, 70});
        sync(d, v, p, g, vp, ft, sy, pipe);
        check(v.sort.dir == 2 && !g.view_rows.empty() &&
                  d.text(g.view_rows.front(), 0) == "gamma.dat",
              "sort: second click descending");
        send(cmd_press{10, 70, {part::header, -1, 0}, false, false, false});
        send(cmd_release{10, 70});
        sync(d, v, p, g, vp, ft, sy, pipe);
        check(v.sort.dir == 0 && !g.view_rows.empty() &&
                  d.text(g.view_rows.front(), 0) == "beta.dat",
              "sort: third click restores original order");

        // G6：模型增删 + 身份稳定 + view 增量对齐（不是整份 reset）
        {
            const auto keep_col_id = d.cols[0].id;
            d.finalize();
            check(d.cols[0].id == keep_col_id, "finalize keeps existing column ids");
            const auto id_new = d.insert_row(1);
            check(d.rows() == 4 && d.ids[1].v == id_new.v,
                  "insert row gives a fresh stable id");
            check(d.ids[2] == row_id{2} && d.ids[3] == row_id{3},
                  "insert row keeps neighbour ids");
            sync(d, v, p, g, vp, ft, sy, pipe);
            check(g.row_count() == 4 && v.checks.on.size() == 4,
                  "sync reconciles a new row without reset");
            check(v.sel.cur_r >= 0 && v.sel.cur_r < 4,
                  "selection survives a model change");
            check(d.remove_row(1) && d.rows() == 3 && d.ids[1] == row_id{2},
                  "remove row keeps neighbour ids");
            sync(d, v, p, g, vp, ft, sy, pipe);
            check(g.row_count() == 3, "sync reconciles a removed row");
            ui_table::column extra;
            extra.key = "extra";
            extra.title = "Extra";
            extra.w = 80;
            const auto cid = d.insert_column(1, extra);
            sync(d, v, p, g, vp, ft, sy, pipe);
            check(d.cols_count() == 4 && g.col_count() == 4, "insert column reconciled");
            check(d.column_of(1) == cid && d.column_of(0) == keep_col_id &&
                      d.column_of(2) == column_id{2},
                  "insert column keeps existing column ids");
            check(d.remove_column(1) && d.cols_count() == 3, "remove column");
            sync(d, v, p, g, vp, ft, sy, pipe);
            check(g.col_count() == 3, "sync reconciles a removed column");
        }

        sync(d, v, p, g, {0, 0, 800, 120}, ft, sy, pipe);
        ui_table::system_wheel(v, p, 0.0F, -3.0F, false);
        sync(d, v, p, g, {0, 0, 800, 120}, ft, sy, pipe);
        check(v.scroll.y > 0.0F, "wheel scrolls");

        // 9) 列布局三件套
        sync(d, v, p, g, vp, ft, sy, pipe);
        const float w0 = v.cols.w[0];
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.resize.begin(tc, 0, 0.0F, w0);
        }
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.resize.move(tc, 300.0F);
        }
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.resize.end(tc);
        }
        check(v.cols.w[0] > w0, "resize feature writes col_w");
        sync(d, v, p, g, vp, ft, sy, pipe);
        {
            const int pos = g.pos_of_col(0);
            check(pos >= 0 && std::abs(g.col_w[static_cast<std::size_t>(pos)] -
                                       v.cols.w[0]) < 0.5F,
                  "resize reaches geometry");
        }

        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.reorder.begin(tc, 0, 0.0F);
        }
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.reorder.move(tc, 450.0F);
        }
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.reorder.end(tc);
        }
        check(v.cols.order[0] != 0, "reorder feature writes order");

        {
            ui_table::table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.freeze.set(tc, 2);
        }
        check(v.cols.frozen == 2, "freeze feature writes frozen");

        // 10) checks / clipboard / undo / table resize
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.checks.toggle(tc, 0, false);
        }
        check(v.checks.on[0] == 1 && v.checks.ids[0] == d.ids[0],
              "checks carries row_id");
        v.checks.reset(d.rows());
        v.ui.last_click_t = -1.0e9;
        send(cmd_press{10,
                       10,
                       {part::check, 0, -1},
                       false,
                       false,
                       false,
                       make_target(part::check, 0, -1)});
        send(cmd_press{10,
                       10,
                       {part::check, 2, -1},
                       false,
                       true,
                       false,
                       make_target(part::check, 2, -1)});
        {
            int on = 0;
            for (const auto x : v.checks.on)
                on += x != 0;
            check(on == d.rows(), "checks: shift range selects visible range");
        }
        send(cmd_press{10,
                       10,
                       {part::check_head, -1, -1},
                       false,
                       false,
                       false,
                       make_target(part::check_head, -1, -1)});
        {
            int on = 0;
            for (const auto x : v.checks.on)
                on += x != 0;
            check(on == 0, "checks: header clears all");
        }
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.selection.set(tc, 1, 0);
        }
        fx.clear();
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.clipboard.copy(tc, false);
        }
        bool clip = false;
        for (const auto &e : fx)
            clip = clip || std::holds_alternative<fx_clipboard>(e);
        check(clip, "clipboard feature");
        fx.clear();
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.clipboard.remove(tc);
        }
        bool del = false;
        for (const auto &e : fx)
            del = del || std::holds_alternative<fx_set_cell>(e);
        check(del, "delete feature");
        fx.clear();
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.undo.run(tc, false);
        }
        check(!fx.empty(), "undo feature");
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.table_resize.begin(tc, 0, 0, 800, 600);
        }
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.table_resize.move(tc, 120, 120);
        }
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.table_resize.end(tc);
        }
        check(v.cols.user_w > 0, "table resize feature");

        // 11) 恢复项：筛选 UI / 查找 / 列菜单 / 粘贴 / 编辑撤销
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.filter.begin(tc, 0);
        }
        send_text("alpha");
        send(cmd_key{key::enter, false, false});
        sync(d, v, p, g, vp, ft, sy, pipe);
        {
            bool all = true;
            for (const int r : g.view_rows)
                all = all && filter_feature::contains(d.text(r, 0), "alpha");
            check(!g.view_rows.empty() && all, "filter UI applies");
        }
        {
            // 大小写不敏感 + 两列 AND
            v.filter.text[0] = "ALPHA";
            sync(d, v, p, g, vp, ft, sy, pipe);
            check(!g.view_rows.empty() &&
                      filter_feature::contains(d.text(g.view_rows.front(), 0), "ALPHA"),
                  "filter: case-insensitive");
            v.filter.text[1] = "10 KB";
            sync(d, v, p, g, vp, ft, sy, pipe);
            bool both = !g.view_rows.empty();
            for (const int r : g.view_rows)
                both = both && filter_feature::contains(d.text(r, 0), "alpha") &&
                       filter_feature::contains(d.text(r, 1), "10 KB");
            check(both, "filter: two columns AND");
            v.filter.text[0].clear();
            v.filter.text[1].clear();
            sync(d, v, p, g, vp, ft, sy, pipe);
        }
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.filter.clear(tc, 0);
        }
        sync(d, v, p, g, vp, ft, sy, pipe);

        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.find.toggle(tc);
        }
        send_text("alpha");
        check(v.find.mode && v.find.hit >= 0, "find locates rows");
        {
            // 命中在视口外：Enter 必须把命中行滚进来
            sync(d, v, p, g, {0, 0, 800, 120}, ft, sy, pipe);
            v.scroll.y = 0;
            {
                table_ctx tc{d, v, p, g, ft, &fx, nullptr};
                sy.find.close(tc);
                sy.find.toggle(tc);
            }
            send_text("gamma");
            send(cmd_key{key::enter, false, false});
            check(v.find.hit == 2, "find: hit is the off-screen row");
            check(v.scroll.y > 0.0F, "find: enter scrolls to the hit");
            sync(d, v, p, g, {0, 0, 800, 120}, ft, sy, pipe);
            check(std::find(g.row_index.begin(), g.row_index.end(), v.find.hit) !=
                      g.row_index.end(),
                  "find: hit row is visible after scroll");
            sync(d, v, p, g, vp, ft, sy, pipe);
            v.scroll.y = 0;
        }
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.find.close(tc);
        }

        {
            ui_table::table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.menu.open(tc, 0, 10.0F, 10.0F);
        }
        check(v.menu.open, "context menu opens");
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.menu.item(tc, 2);
        }
        check(v.cols.hidden[0] == 1, "hide column");
        sync(d, v, p, g, vp, ft, sy, pipe);
        check(g.col_count() == 2, "hidden column excluded from geometry");
        {
            // 键盘左右只走可见列：隐藏 col0 后从 col1 往右到 col2，再往右停住
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.selection.set(tc, g.view_rows.front(), g.col_index.front());
        }
        send(cmd_key{key::right, false, false});
        sync(d, v, p, g, vp, ft, sy, pipe);
        check(v.sel.cur_c == g.col_index.back(),
              "hidden: keyboard moves over visible columns");
        send(cmd_key{key::right, false, false});
        sync(d, v, p, g, vp, ft, sy, pipe);
        check(v.sel.cur_c == g.col_index.back(),
              "hidden: keyboard stops at last visible");
        check(v.sel.cur_c != 0, "hidden: never lands on hidden column");
        {
            ui_table::table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.menu.open(tc, 1, 10.0F, 10.0F);
        }
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.menu.item(tc, 3);
        }
        check(v.cols.hidden[0] == 0, "show all restores columns");
        sync(d, v, p, g, vp, ft, sy, pipe);
        {
            ui_table::table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.menu.open(tc, 0, 10.0F, 10.0F);
        }
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.menu.item(tc, 0);
        }
        check(v.cols.w[0] > 0.0F, "autofit writes width");
        {
            ui_table::table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.menu.open(tc, 1, 10.0F, 10.0F);
        }
        send(cmd_press{10, 10, {part::menu_item, -1, 3}, false, false, false});
        check(!v.menu.open && v.cols.hidden[0] == 0, "menu item click path");

        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.selection.set(tc, 0, 0);
        }
        fx.clear();
        {
            ui_table::table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.edit.paste(tc, "P1\tP2");
        }
        check(!fx.empty(), "paste emits cells");

        send_begin(0, 0);
        send_text("X");
        const std::string before_undo = v.edit.buf;
        send(cmd_key{key::z, false, true});
        check(v.edit.buf != before_undo, "edit text undo");
        send(cmd_key{key::esc, false, false});

        // 13) 编辑选文本 / 提交后撤销 / 滚动条拖 / 整表缩放 / 光标
        send_begin(0, 0);
        send(cmd_key{key::end, false, false});
        send(cmd_key{key::left, true, false});
        send(cmd_key{key::left, true, false});
        check(v.edit.has_sel(), "edit shift selection");
        send(cmd_key{key::esc, false, false});

        send_begin(0, 0);
        send_text("Q");
        send(cmd_key{key::enter, false, false});
        check(!v.edit.active(), "commit before undo");
        fx.clear();
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.undo.run(tc, false);
        }
        check(!fx.empty(), "ctrl+z after commit");

        ui_table::system_wheel(v, p, 0.0F, -5.0F, false);
        sync(d, v, p, g, {0, 0, 800, 120}, ft, sy, pipe);
        const float sy0 = v.scroll.y;
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.scroll.begin(tc, 0, sy0, 0.0F);
        }
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.scroll.move(tc, -50.0F);
        }
        {
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.scroll.end(tc);
        }
        check(sy0 > 0.0F && v.scroll.y < sy0, "scrollbar thumb drag");

        const float uw0 = v.cols.user_w;
        send(cmd_press{799, 119, {part::edge_se}, false, false, false});
        send(cmd_drag{849, 169});
        send(cmd_release{849, 169});
        check(v.cols.user_w > uw0, "table resize drag");
        check(cursor_for({part::edge_se}) == cursor::resize_nwse,
              "resize cursor mapping");
        {
            build_frame(d, v, p, g, ft, sy, f, table_id{7});
            bool thumb = false, sep = false, edge = false;
            for (const auto &zr : f.zones)
            {
                thumb = thumb || zr.z.kind == part::vbar_thumb;
                sep = sep || zr.z.kind == part::col_sep;
                edge = edge || zr.z.kind == part::edge_se;
            }
            check(thumb && sep && edge, "frame zones carry bars/edges");
        }
        {
            const auto ez =
                ui_table::hit_test(d, g, v, g.edge_se.x + 5.0F, g.edge_se.y + 5.0F)
                    .to_zone();
            check(ez.kind == part::edge_se, "edge hit test");
            v.menu.open = true;
            v.menu.x = 100.0F;
            v.menu.y = 100.0F;
            const auto mz = ui_table::hit_test(d, g, v, 175.0F, 140.0F).to_zone();
            check(mz.kind == part::menu_item && mz.col == 1, "menu hit test");
            v.menu.open = false;
            const int dc0 = g.col_index[0];
            const auto fz = ui_table::hit_test(d, g, v, g.col_x_at(0) + g.col_w[0] - 9.0F,
                                               g.col_header.y + g.col_header.h * 0.5F)
                                .to_zone();
            check(fz.kind == part::filter_btn && fz.col == dc0, "filter button hit test");
        }

        // G2：拖拽框选出体区 = 自动滚动 + 目标夹到末行
        {
            sync(d, v, p, g, {0, 0, 800, 120}, ft, sy, pipe);
            v.scroll.y = 0;
            v.ui.last_click_t = -1.0e9;
            send(cmd_press{g.scrollv.x + 20,
                           g.row_y.front() + 5,
                           {part::cell, g.view_rows.front(), 0},
                           false,
                           false,
                           false});
            send(cmd_drag{g.scrollv.x + 20, g.body.y1() + 20});
            check(v.scroll.y > 0.0F, "drag past bottom edge auto-scrolls");
            check(v.sel.r1 == g.view_rows.back(), "drag extends to last row");
            send(cmd_release{g.scrollv.x + 20, g.body.y1() + 20});
            sync(d, v, p, g, vp, ft, sy, pipe);
            v.scroll.y = 0;
        }

        // G4：编辑框词级操作（双击选词 / 三击全选 / Ctrl+← / Ctrl+Backspace）
        {
            const std::string keep = d.cells[0][0].text;
            d.cells[0][0].text = "alpha beta gamma";
            sync(d, v, p, g, vp, ft, sy, pipe);
            send_begin(0, 0);
            send(cmd_key{key::end, false, false});
            send(cmd_key{key::left, false, true});
            check(v.edit.caret == 11, "edit: Ctrl+Left jumps to word start");
            send(cmd_key{key::backspace, false, true});
            check(v.edit.buf == "alpha gamma", "edit: Ctrl+Backspace deletes a word");
            send(cmd_key{key::esc, false, false});
            {
                const float origin =
                    g.col_x_at(g.pos_of_col(0)) + p.cell_pad + text_width(ft, "alpha be");
                v.ui.last_click_t = -1.0e9;
                send(cmd_press{origin,
                               g.row_y.front() + 5,
                               {part::cell, 0, 0},
                               false,
                               false,
                               false});
                send(cmd_press{origin,
                               g.row_y.front() + 5,
                               {part::cell, 0, 0},
                               false,
                               false,
                               false});
                check(v.edit.active() && v.edit.sel_lo() == 6 && v.edit.sel_hi() == 10,
                      "edit: double-click selects the word");
                send(cmd_press{origin,
                               g.row_y.front() + 5,
                               {part::cell, 0, 0},
                               false,
                               false,
                               false});
                check(v.edit.sel_lo() == 0 &&
                          v.edit.sel_hi() == static_cast<int>(v.edit.buf.size()),
                      "edit: triple-click selects all");
            }
            send(cmd_key{key::esc, false, false});
            send(cmd_release{0, 0});
            d.cells[0][0].text = keep;
            sync(d, v, p, g, vp, ft, sy, pipe);
        }

        // 13) 筛选实时生效 / 查找点别处关闭 / 双击分隔条自适应
        {
            const std::string keep = d.cells[0][0].text;
            d.cells[0][0].text = "livefilter";
            v.ui.last_click_t = -1.0e9;
            send(cmd_press{10,
                           10,
                           {part::filter_btn, -1, 0},
                           false,
                           false,
                           false,
                           make_target(part::filter_btn, -1, 0)});
            send_text("livef");
            sync(d, v, p, g, vp, ft, sy, pipe);
            check(v.filter.edit == 0 && g.view_rows.size() == 1 &&
                      d.text(g.view_rows.front(), 0) == "livefilter",
                  "filter: live while typing");
            send(cmd_key{key::enter, false, false});
            check(v.filter.edit < 0 && v.filter.text[0] == "livef",
                  "filter: enter keeps filter");
            v.ui.last_click_t = -1.0e9;
            send(cmd_press{10,
                           10,
                           {part::filter_btn, -1, 0},
                           false,
                           false,
                           false,
                           make_target(part::filter_btn, -1, 0)});
            send(cmd_key{key::del, false, false});
            sync(d, v, p, g, vp, ft, sy, pipe);
            check(v.filter.text[0].empty() && g.view_rows.size() == 3,
                  "filter: Delete clears live");
            send(cmd_key{key::enter, false, false});
            d.cells[0][0].text = keep;
            sync(d, v, p, g, vp, ft, sy, pipe);
        }
        {
            send(cmd_key{key::f, false, true});
            check(v.find.mode, "find: Ctrl+F opens");
            v.ui.last_click_t = -1.0e9;
            send(cmd_press{10,
                           10,
                           {part::cell, 1, 0},
                           false,
                           false,
                           false,
                           make_target(part::cell, 1, 0)});
            check(!v.find.mode, "find: click elsewhere closes");
        }
        {
            v.ui.last_click_t = -1.0e9;
            v.cols.user[0] = 0;
            v.cols.w[0] = 60; // 明显偏离自适应宽度，双击后应被改回来
            const float before = v.cols.w[0];
            send(cmd_press{10,
                           10,
                           {part::col_sep, -1, 0},
                           false,
                           false,
                           false,
                           make_target(part::col_sep, -1, 0)});
            send(cmd_release{10, 10});
            send(cmd_press{10,
                           10,
                           {part::col_sep, -1, 0},
                           false,
                           false,
                           false,
                           make_target(part::col_sep, -1, 0)});
            send(cmd_release{10, 10});
            check(v.cols.user[0] == 1 && v.cols.w[0] != before,
                  "resize: double-click separator autofits");
            sync(d, v, p, g, vp, ft, sy, pipe);
        }

        // 14) wrap：行高按行数增长，且 build_frame 真的画出多行
        {
            const std::string keep = d.cells[0][1].text;
            d.cells[0][1].text = "alpha beta gamma delta epsilon zeta";
            d.cols[0].wrap = true;
            sync(d, v, p, g, {0, 0, 800, 600}, ft, sy, pipe);
            check(g.row_h_data[1] > p.row_h, "wrap: row height grows");
            frame_data fw;
            build_frame(d, v, p, g, ft, sy, fw, table_id{});
            const auto wrap_zone = zone_key({part::cell, 1, 0});
            int lines = 0;
            for (const auto &t : fw.draw.texts[static_cast<std::size_t>(stream::content)])
                lines += t.zone == wrap_zone ? 1 : 0;
            check(lines > 1, "wrap: multiple text lines emitted");
            d.cols[0].wrap = false;
            d.cells[0][1].text = keep;
            sync(d, v, p, g, vp, ft, sy, pipe);
        }

        // 15) 滚动条轨道翻页 / 上边把手缩放（方向要正确）
        {
            sync(d, v, p, g, {0, 0, 800, 120}, ft, sy, pipe);
            v.scroll.y = 0;
            v.ui.last_click_t = -1.0e9;
            send(cmd_press{g.vtrack.x + 2,
                           g.vtrack.y1() - 2,
                           {part::vbar_track, -1, -1},
                           false,
                           false,
                           false,
                           make_target(part::vbar_track, -1, -1)});
            check(v.scroll.y > 0.0F, "scroll: track click pages down");
            send(cmd_release{g.vtrack.x + 2, g.vtrack.y1() - 2});

            sync(d, v, p, g, vp, ft, sy, pipe);
            v.cols.user_w = 500;
            v.cols.user_h = 300;
            sync(d, v, p, g, vp, ft, sy, pipe);
            const float h0 = g.box.h;
            v.ui.last_click_t = -1.0e9;
            const ui_table::rect en = g.edge_n;
            send(cmd_press{en.x + en.w * 0.5F,
                           en.y + en.h * 0.5F,
                           {part::edge_n},
                           false,
                           false,
                           false,
                           make_target(part::edge_n, -1, -1)});
            send(cmd_drag{en.x + en.w * 0.5F, en.y + en.h * 0.5F - 40.0F});
            send(cmd_release{en.x + en.w * 0.5F, en.y + en.h * 0.5F - 40.0F});
            sync(d, v, p, g, vp, ft, sy, pipe);
            check(v.cols.user_h > h0, "table resize: top handle grows height");
            v.cols.user_h = -1;
            v.cols.user_w = -1;
            sync(d, v, p, g, vp, ft, sy, pipe);
        }

        // 16) undo 合并：同一段连续输入只占一步
        {
            send_begin(0, 0);
            const std::string keep = v.edit.buf;
            send_text("abc");
            check(v.edit.undo.size() == 1, "undo: typing burst coalesced");
            send(cmd_key{key::z, false, true});
            check(v.edit.buf == keep, "undo: coalesced burst undone in one step");
            send(cmd_key{key::esc, false, false});
        }

        // 12) tooltip：悬停 0.6s 后才多出图元
        sync(d, v, p, g, vp, ft, sy, pipe);
        v.ui.hover = make_target(part::cell, 0, 0);
        v.tip.hover_t = 0.0;
        v.ui.now = 0.0;
        frame_data t0, t1;
        build_frame(d, v, p, g, ft, sy, t0);
        v.ui.now = 1.0;
        build_frame(d, v, p, g, ft, sy, t1);
        check(t1.draw.op_count() > t0.draw.op_count(), "tooltip shows after dwell");

        // 13) zone/owner/cursor 全 part 往返，几何不变量
        {
            const std::array<std::tuple<part, int, int>, 19> parts{{
                {part::cell, 1, 0},          {part::check, 1, -1},
                {part::check_head, -1, -1},  {part::header, -1, 0},
                {part::col_sep, -1, 0},      {part::filter_btn, -1, 0},
                {part::header_group, -1, 0}, {part::menu_item, -1, 1},
                {part::frame, -1, -1},       {part::vbar_track, -1, -1},
                {part::vbar_thumb, -1, -1},  {part::hbar_track, -1, -1},
                {part::hbar_thumb, -1, -1},  {part::edge_n, -1, -1},
                {part::edge_w, -1, -1},      {part::edge_ne, -1, -1},
                {part::edge_nw, -1, -1},     {part::edge_se, -1, -1},
                {part::edge_sw, -1, -1},
            }};
            for (const auto &[k, r, c] : parts)
            {
                const zone z{k, r, c, table_id{3}};
                const zone back = zone_from(zone_key(z));
                check(back.kind == k && back.row == r && back.col == c &&
                          back.table == z.table,
                      std::format("zone roundtrip {}", part_name(k)).c_str());
            }
            const std::array<std::pair<part, cursor>, 12> cursors{{
                {part::header, cursor::hand},
                {part::header_group, cursor::hand},
                {part::check, cursor::hand},
                {part::check_head, cursor::hand},
                {part::vbar_thumb, cursor::hand},
                {part::hbar_thumb, cursor::hand},
                {part::col_sep, cursor::resize_ew},
                {part::edge_n, cursor::resize_ns},
                {part::edge_w, cursor::resize_ew},
                {part::edge_ne, cursor::resize_nesw},
                {part::edge_sw, cursor::resize_nesw},
                {part::edge_nw, cursor::resize_nwse},
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
            sync(d, v, p, g, vp, ft, sy, pipe);
            v.sel.reset();
            v.ui.last_click_t = -1.0e9;
            send(cmd_press{
                10,
                10,
                {part::cell, g.view_rows.front(), g.col_index.front()},
                false,
                false,
                false,
                make_target(part::cell, g.view_rows.front(), g.col_index.front())});
            const int col0 = v.sel.cur_c;
            send(cmd_key{key::right, false, false});
            check(v.sel.cur_c != col0, "keyboard: right moves column");
            send(cmd_key{key::left, false, false});
            check(v.sel.cur_c == col0, "keyboard: left moves column");
            const float hx = g.col_x_at(0) + g.col_w[0] * 0.5F;
            const float hy = g.col_header.y + g.col_header.h * 0.5F;
            const auto ht = make_target(part::header, -1, 0);
            send(cmd_press{hx, hy, {part::header, -1, 0}, false, false, false, ht});
            send(cmd_drag{g.col_x_at(1) + 20.0F, hy});
            send(cmd_release{g.col_x_at(1) + 20.0F, hy});
            sync(d, v, p, g, vp, ft, sy, pipe);
            check(v.cols.order[0] != 0, "reorder drag: order changed");
            check(v.cols.order[0] == 1 && v.cols.order[1] == 0,
                  "reorder drag: col0 moved to col1");
            check(d.cols[0].id.value != 0, "reorder drag: column id stable");
            v.cols.order = {0, 1, 2};
            sync(d, v, p, g, vp, ft, sy, pipe);

            v.cols.user_w = 500;
            v.cols.user_h = 300;
            sync(d, v, p, g, vp, ft, sy, pipe);
            const auto ne = g.edge_ne;
            send(cmd_press{ne.x + ne.w * 0.5F,
                           ne.y + ne.h * 0.5F,
                           {part::edge_ne},
                           false,
                           false,
                           false,
                           make_target(part::edge_ne, -1, -1)});
            send(cmd_drag{ne.x + ne.w * 0.5F - 40.0F, ne.y + ne.h * 0.5F - 30.0F});
            send(cmd_release{ne.x + ne.w * 0.5F - 40.0F, ne.y + ne.h * 0.5F - 30.0F});
            sync(d, v, p, g, vp, ft, sy, pipe);
            check(v.cols.user_w < 500, "table resize ne: width shrinks from left");
            check(v.cols.user_h > 300, "table resize ne: height grows upward");
            v.cols.user_w = -1;
            v.cols.user_h = -1;
            sync(d, v, p, g, vp, ft, sy, pipe);

            const auto save_w = v.cols.w;
            v.cols.w = {400, 400, 400};
            sync(d, v, p, g, vp, ft, sy, pipe);
            check(g.hbar, "hbar appears when columns overflow");
            const float sx0 = v.scroll.x;
            const auto hb = g.hthumb;
            send(cmd_press{hb.x + hb.w * 0.5F,
                           hb.y + hb.h * 0.5F,
                           {part::hbar_thumb},
                           false,
                           false,
                           false,
                           make_target(part::hbar_thumb, -1, -1)});
            send(cmd_drag{hb.x + hb.w * 0.5F + 60.0F, hb.y + hb.h * 0.5F});
            send(cmd_release{hb.x + hb.w * 0.5F + 60.0F, hb.y + hb.h * 0.5F});
            sync(d, v, p, g, vp, ft, sy, pipe);
            check(v.scroll.x > sx0, "hbar thumb drag scrolls x");
            v.cols.w = save_w;
            v.scroll.x = 0;
            sync(d, v, p, g, vp, ft, sy, pipe);
        }

        // 15) 编辑写回、undo/redo、勾选三态、wrap 换行、动态行
        {
            fx.clear();
            send_begin(0, 0);
            const auto before_edit = d.text(0, 0);
            send_text("Q");
            send(cmd_key{key::enter, false, false});
            check(!v.edit.active(), "edit: Enter closes editor");
            bool wrote = false;
            for (auto &e : fx)
                if (auto *q = std::get_if<fx_set_cell>(&e))
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
            send(cmd_key{key::z, false, true});
            check(v.edit.buf == keep2, "undo: single step restores");
            send(cmd_key{key::y, false, true});
            check(v.edit.buf.ends_with("xy"), "redo: restores edit text");
            send(cmd_key{key::esc, false, false});
            check(!v.edit.active(), "edit: Esc cancels");

            v.checks.reset(d.rows());
            check(v.checks.on.size() == static_cast<std::size_t>(d.rows()),
                  "checks: array grows with rows");
            v.checks.on[0] = 1;
            table_ctx tc{d, v, p, g, ft, &fx, nullptr};
            sy.checks.toggle_all(tc);
            int on = 0;
            for (auto x : v.checks.on)
                on += x != 0;
            check(on == d.rows(), "checks: toggle_all from partial -> all");
            sy.checks.toggle_all(tc);
            on = 0;
            for (auto x : v.checks.on)
                on += x != 0;
            check(on == 0, "checks: toggle_all from all -> none");

            d.set_cell(0, 0, "a\nb");
            p.wrap = true;
            v.cols.user[0] = 0;
            sync(d, v, p, g, vp, ft, sy, pipe);
            check(g.row_h_data[0] > p.row_h, "wrap: explicit newline grows row");
            p.wrap = false;
            sync(d, v, p, g, vp, ft, sy, pipe);
            check(g.row_h_data[0] <= p.row_h, "wrap: toggle off restores row height");

            const auto n0 = d.rows();
            const auto id0 = d.insert_row(1);
            const auto id1 = d.insert_row(2);
            d.finalize();
            v.reset(d);
            sync(d, v, p, g, vp, ft, sy, pipe);
            check(d.rows() == n0 + 2, "dynamic: insert two rows");
            check(id0 != id1 && d.ids[1] != d.ids[2], "dynamic: fresh row ids");
            check(v.checks.on.size() == static_cast<std::size_t>(d.rows()),
                  "dynamic: checks size follows insert");
            check(d.remove_row(2) && d.remove_row(1), "dynamic: remove two rows");
            d.finalize();
            v.reset(d);
            sync(d, v, p, g, vp, ft, sy, pipe);

            bool cells_hit = true;
            for (int ri = 0; ri < g.row_count(); ++ri)
                for (int ci = 0; ci < g.col_count(); ++ci)
                {
                    const auto t =
                        ui_table::hit_test(d, g, v, g.col_x_at(ci) + 4.0F,
                                           g.row_y[static_cast<std::size_t>(ri)] + 4.0F);
                    cells_hit =
                        cells_hit && t.kind == part::cell &&
                        t.row_index == g.row_index[static_cast<std::size_t>(ri)] &&
                        t.col_index == g.col_index[static_cast<std::size_t>(ci)];
                }
            check(cells_hit, "hit_test: every visible cell center resolves");
            frame_data f;
            build_frame(d, v, p, g, ft, sy, f, table_id{7});
            bool owner_ok = true, zone_ok = true;
            for (const auto &ss : f.draw.shapes)
                for (const auto &o : ss)
                {
                    zone_ok = zone_ok && o.zone != 0;
                    owner_ok = owner_ok && !std::holds_alternative<none_owner>(o.who);
                }
            for (const auto &tt : f.draw.texts)
                for (const auto &o : tt)
                {
                    zone_ok = zone_ok && o.zone != 0;
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
            send(cmd_press{10,
                           10,
                           {part::cell, rr, cc},
                           false,
                           false,
                           false,
                           make_target(part::cell, rr, cc)});
            send(cmd_key{key::space, false, false});
            check(v.checks.on[static_cast<std::size_t>(rr)] != 0,
                  "space toggles current row check");
            send(cmd_key{key::space, false, false});
            check(v.checks.on[static_cast<std::size_t>(rr)] == 0,
                  "space toggles current row check off");

            send(cmd_key{key::pgdn, false, false});
            check(v.sel.cur_r != rr, "keyboard: PageDown moves selection");
            send(cmd_key{key::pgup, false, false});
            check(v.sel.cur_r == rr, "keyboard: PageUp returns");

            const int last = g.view_rows.back();
            send(cmd_press{10,
                           10,
                           {part::cell, last, cc},
                           false,
                           true,
                           false,
                           make_target(part::cell, last, cc)});
            check(v.sel.r1 == last && v.sel.r0 == rr,
                  "shift+click extends selection to last row");
            send(cmd_press{1,
                           1,
                           {part::none, -1, -1},
                           false,
                           false,
                           false,
                           make_target(part::none, -1, -1)});
            check(!v.sel.has_sel(), "click non-cell clears selection");

            check(d.cell_id(0, 0).row == d.ids[0] &&
                      d.cell_id(0, 0).col == d.column_of(0),
                  "cell_id roundtrip row/col");
            check(d.index_of(d.ids[0]) == 0, "index_of row_id");
            check(d.cols[0].id == d.column_of(0), "column id stable");
            check(d.cols[1].id == d.column_of(1), "column id stable 2");

            const zone zt{part::cell, 1, 2, table_id{7}};
            const auto backt = zone_from(zone_key(zt));
            check(backt.kind == part::cell && backt.row == 1 && backt.col == 2 &&
                      backt.table == table_id{7},
                  "zone roundtrip with table bits");
            check(zone_key(zt) != zone_key({part::cell, 1, 2, table_id{8}}),
                  "zone table bits participate in key");

            check(cursor_for({part::cell, 0, 0}) == cursor::arrow, "cursor cell arrow");
            check(cursor_for({part::menu_item}) == cursor::arrow, "cursor menu arrow");
        }

        std::println("[table-selftest] done, {} failed", fails);
        return fails;
    }

} // namespace ui_table
