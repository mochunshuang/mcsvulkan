
// ============================================================================
#pragma once
// test_ui_ability8_table.hpp
//
//   table = RowSource × RowStage × Schema × ViewState
//           经 reduce 管意图、derive 出 Frame、present 出 DL、encode 出实例。
//
//   六个接口：
//     RowSource   数据（只被读）
//     RowStage    变换（filter / sort；编译期组合，运行期可关）
//     Schema      列定义（数据，不是节点）
//     ViewState   状态（意图的累积，按 slice 分版本）
//     Command     意图（variant，visit 不写就编译不过）
//     DL          绘制值（三股流 + 一个 arena）
//
//   两个插槽：
//     数据槽 = RowSource
//     行为槽 = RowStage + Painter
//
//   三条流：chrome（可任意排序）/ content（保序）/ proxy（无色，可任意排序）
//   一个 arena：DL 的文本都指向 arena，帧结束统一 reset。
//
//   ★ 本文件是纯核心：只 include 标准库 + glm。
//     不 include dao / shader ABI / Vulkan / GLFW / JSON。
//     这些东西只允许出现在 encode() 与宿主适配层（cpp）。
// ============================================================================

#include <algorithm>
#include <array>
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

    // ═══════════════════════════════════════════════════════════════════════
    // 0. 基础值
    // ═══════════════════════════════════════════════════════════════════════

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

    // 行身份：跨排序/筛选/撤销都不变
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

    // ═══════════════════════════════════════════════════════════════════════
    // 1. zone：命中了什么（只描述"是谁"，不含像素）
    // ═══════════════════════════════════════════════════════════════════════

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
        Count
    };

    struct zone
    {
        part kind{part::none};
        int row{-1};
        int col{-1};
        [[nodiscard]] constexpr bool inside() const noexcept
        {
            return kind != part::none;
        }
        friend constexpr bool operator==(const zone &, const zone &) = default;
    };

    namespace bits
    {
        inline constexpr std::uint32_t kPartShift = 27;
        inline constexpr std::uint32_t kRowShift = 14;
        inline constexpr std::uint32_t kRowNone = 0x1FFFu;
        inline constexpr std::uint32_t kColNone = 0x3FFFu;
        static_assert(static_cast<std::uint32_t>(part::Count) <= 32,
                      "part 必须塞进 5 bit");
    } // namespace bits

    [[nodiscard]] constexpr std::uint32_t zone_key(const zone &z) noexcept
    {
        const auto r = z.row < 0 ? bits::kRowNone : static_cast<std::uint32_t>(z.row);
        const auto c = z.col < 0 ? bits::kColNone : static_cast<std::uint32_t>(z.col);
        return (static_cast<std::uint32_t>(z.kind) << bits::kPartShift) |
               (r << bits::kRowShift) | c;
    }

    [[nodiscard]] constexpr zone zone_from(std::uint32_t key) noexcept
    {
        zone z{};
        z.kind = static_cast<part>(key >> bits::kPartShift);
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
        case part::none:
            return "none";
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
        case part::Count:
            return "?";
        }
        return "?";
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

    // ═══════════════════════════════════════════════════════════════════════
    // 2. 光标 / 颜色 / 字体 / 文本 / arena
    // ═══════════════════════════════════════════════════════════════════════

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
        case part::filter_btn:
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

    [[nodiscard]] inline glm::vec4 hex_color(std::string_view s,
                                             const glm::vec4 &fallback)
    {
        if (s.size() != 7 || s[0] != '#')
            return fallback;
        const auto nib = [](char ch) -> int {
            if (ch >= '0' && ch <= '9')
                return ch - '0';
            if (ch >= 'a' && ch <= 'f')
                return ch - 'a' + 10;
            if (ch >= 'A' && ch <= 'F')
                return ch - 'A' + 10;
            return -1;
        };
        int v[6]{};
        for (int i = 0; i < 6; ++i)
        {
            v[i] = nib(s[static_cast<std::size_t>(i + 1)]);
            if (v[i] < 0)
                return fallback;
        }
        return {static_cast<float>(v[0] * 16 + v[1]) / 255.0F,
                static_cast<float>(v[2] * 16 + v[3]) / 255.0F,
                static_cast<float>(v[4] * 16 + v[5]) / 255.0F, 1.0F};
    }

    struct glyph_metrics
    {
        glm::vec4 uv{0.0F, 0.0F, 1.0F, 1.0F};
        glm::vec4 plane{0.0F};
        float advance{0.0F};
        std::uint32_t tex{0};
        std::uint32_t samp{0};
        std::uint32_t font{4};
        float px_range{0.0F};
        bool y_down{false};
    };

    // ASCII 平铺（O(1)、无哈希）；其余走 map
    struct font
    {
        float px{15.0F};
        float line_h{1.25F};
        std::array<glyph_metrics, 128> ascii{};
        std::array<std::uint8_t, 128> ascii_has{};
        std::unordered_map<char32_t, glyph_metrics> other;

        void put(char32_t cp, const glyph_metrics &m)
        {
            if (cp < 128)
            {
                ascii[static_cast<std::size_t>(cp)] = m;
                ascii_has[static_cast<std::size_t>(cp)] = 1;
            }
            else
            {
                other.emplace(cp, m);
            }
        }
        [[nodiscard]] const glyph_metrics *find(char32_t cp) const noexcept
        {
            if (cp < 128)
                return ascii_has[static_cast<std::size_t>(cp)] != 0
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

    [[nodiscard]] inline float text_width(const font &f, std::string_view s) noexcept
    {
        float w = 0.0F;
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

    // 稳定块 arena：DL 里的文本全指向这里，帧结束 reset
    struct arena
    {
        static constexpr std::size_t kBlock = 64 * 1024;
        std::vector<std::unique_ptr<char[]>> blocks;
        std::size_t cur{0};
        std::size_t used{0};

        void reset() noexcept
        {
            cur = 0;
            used = 0;
        }
        [[nodiscard]] std::string_view put(std::string_view s)
        {
            if (s.empty())
                return {};
            if (s.size() > kBlock)
            {
                blocks.push_back(std::make_unique<char[]>(s.size()));
                std::memcpy(blocks.back().get(), s.data(), s.size());
                return {blocks.back().get(), s.size()};
            }
            if (blocks.empty() || used + s.size() > kBlock)
            {
                if (cur >= blocks.size())
                    blocks.push_back(std::make_unique<char[]>(kBlock));
                ++cur;
                used = 0;
            }
            char *dst = blocks[cur - 1].get() + used;
            std::memcpy(dst, s.data(), s.size());
            used += s.size();
            return {dst, s.size()};
        }
    };

    [[nodiscard]] inline std::string_view fit_text(const font &f, std::string_view str,
                                                   float max_w, arena &ar,
                                                   std::string &scratch)
    {
        if (text_width(f, str) <= max_w)
            return str;
        const float ell = text_width(f, "...");
        if (max_w <= ell)
            return ar.put("...");
        std::size_t cut = 0;
        float w = 0.0F;
        std::size_t i = 0;
        while (i < str.size())
        {
            char32_t cp = 0;
            const std::size_t n = utf8_decode(str.substr(i), cp);
            const auto *g = f.find(cp);
            const float a = g ? g->advance * f.px : 0.0F;
            if (w + a + ell > max_w)
                break;
            w += a;
            i += n;
            cut = i;
        }
        scratch.assign(str.substr(0, cut));
        scratch += "...";
        return ar.put(scratch);
    }

    inline void wrap_lines(const font &f, std::string_view text, float inner,
                           std::vector<std::string_view> &out)
    {
        out.clear();
        std::size_t line_start = 0;
        std::size_t last_space = std::string_view::npos;
        float w = 0.0F;
        std::size_t i = 0;
        while (i < text.size())
        {
            char32_t cp = 0;
            const std::size_t n = utf8_decode(text.substr(i), cp);
            if (cp == U'\n')
            {
                out.push_back(text.substr(line_start, i - line_start));
                i += n;
                line_start = i;
                last_space = std::string_view::npos;
                w = 0.0F;
                continue;
            }
            const auto *g = f.find(cp);
            const float a = g ? g->advance * f.px : 0.0F;
            if (w + a > inner && i > line_start)
            {
                const std::size_t cut =
                    last_space != std::string_view::npos && last_space > line_start
                        ? last_space
                        : i;
                out.push_back(text.substr(line_start, cut - line_start));
                line_start = cut;
                while (line_start < text.size() && text[line_start] == ' ')
                    ++line_start;
                i = line_start;
                last_space = std::string_view::npos;
                w = 0.0F;
                continue;
            }
            if (cp == U' ')
                last_space = i;
            w += a;
            i += n;
        }
        out.push_back(text.substr(line_start));
    }

    // ═══════════════════════════════════════════════════════════════════════
    // 3. Schema：列是数据，不是节点
    // ═══════════════════════════════════════════════════════════════════════

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

    struct column
    {
        std::string key;
        std::string title;
        std::string group;
        float w{140.0F};
        float min_w{48.0F};
        float max_w{640.0F};
        float weight{0.0F};
        align text_align{align::left};
        bool filterable{true};
        bool wrap{false};
        render_kind render{render_kind::text};
        std::vector<std::pair<std::string, glm::vec4>> tags;

        [[nodiscard]] glm::vec4 tag_color(std::string_view v,
                                          const glm::vec4 &fallback) const
        {
            for (const auto &[k, c] : tags)
                if (k == v)
                    return c;
            return fallback;
        }
    };

    struct schema
    {
        std::vector<column> cols;
        [[nodiscard]] int count() const noexcept
        {
            return static_cast<int>(cols.size());
        }
        [[nodiscard]] bool any_group() const noexcept
        {
            for (const auto &c : cols)
                if (!c.group.empty())
                    return true;
            return false;
        }
        [[nodiscard]] const column &at(col_idx c) const noexcept
        {
            return cols[static_cast<std::size_t>(c)];
        }
    };

    // ═══════════════════════════════════════════════════════════════════════
    // 4. RowSource：唯一的数据插槽
    // ═══════════════════════════════════════════════════════════════════════

    template <class T>
    concept RowSource = requires(const T &t, row_idx r, col_idx c) {
        { t.row_count() } -> std::same_as<int>;
        { t.id_of(r) } -> std::same_as<ui_table::row_id>;
        { t.cell(r, c) } -> std::same_as<std::string_view>;
        { t.version() } -> std::same_as<std::uint32_t>;
    };

    struct row_source_ref
    {
        const void *self{};
        int (*fn_count)(const void *){};
        ui_table::row_id (*fn_id)(const void *, int){};
        std::string_view (*fn_cell)(const void *, int, int){};
        std::uint32_t (*fn_version)(const void *){};

        [[nodiscard]] int row_count() const noexcept
        {
            return fn_count ? fn_count(self) : 0;
        }
        [[nodiscard]] ui_table::row_id id_of(int r) const noexcept
        {
            return fn_id ? fn_id(self, r) : ui_table::row_id{};
        }
        [[nodiscard]] std::string_view at(int r, int c) const noexcept
        {
            return fn_cell ? fn_cell(self, r, c) : std::string_view{};
        }
        [[nodiscard]] std::uint32_t version() const noexcept
        {
            return fn_version ? fn_version(self) : 0;
        }
    };

    // 数组实现：列主序（按列扫 = 顺序访问），行 id 稳定
    struct array_row_source
    {
        std::vector<std::vector<std::string>> cells; // cells[col][row]
        std::vector<ui_table::row_id> ids;
        std::uint32_t ver{1};

        [[nodiscard]] int row_count() const noexcept
        {
            return static_cast<int>(ids.size());
        }
        [[nodiscard]] ui_table::row_id id_of(int r) const noexcept
        {
            return r >= 0 && static_cast<std::size_t>(r) < ids.size()
                       ? ids[static_cast<std::size_t>(r)]
                       : ui_table::row_id{};
        }
        [[nodiscard]] std::string_view cell(int r, int c) const noexcept
        {
            if (c < 0 || static_cast<std::size_t>(c) >= cells.size())
                return {};
            const auto &col = cells[static_cast<std::size_t>(c)];
            if (r < 0 || static_cast<std::size_t>(r) >= col.size())
                return {};
            return col[static_cast<std::size_t>(r)];
        }
        [[nodiscard]] std::uint32_t version() const noexcept
        {
            return ver;
        }
        void touch() noexcept
        {
            ++ver;
        }
        void set_cell(ui_table::row_id id, int c, std::string text)
        {
            const int r = index_of(id);
            if (r < 0 || c < 0 || static_cast<std::size_t>(c) >= cells.size())
                return;
            auto &col = cells[static_cast<std::size_t>(c)];
            if (static_cast<std::size_t>(r) >= col.size())
                return;
            col[static_cast<std::size_t>(r)] = std::move(text);
            touch();
        }
        [[nodiscard]] int index_of(ui_table::row_id id) const noexcept
        {
            for (std::size_t i = 0; i < ids.size(); ++i)
                if (ids[i] == id)
                    return static_cast<int>(i);
            return -1;
        }
        [[nodiscard]] row_source_ref ref() const noexcept
        {
            return {this,
                    [](const void *p) {
                        return static_cast<const array_row_source *>(p)->row_count();
                    },
                    [](const void *p, int r) {
                        return static_cast<const array_row_source *>(p)->id_of(r);
                    },
                    [](const void *p, int r, int c) {
                        return static_cast<const array_row_source *>(p)->cell(r, c);
                    },
                    [](const void *p) {
                        return static_cast<const array_row_source *>(p)->version();
                    }};
        }
    };

    // ═══════════════════════════════════════════════════════════════════════
    // 5. ViewState：意图的累积（按 slice 分版本；不存几何、不存像素）
    // ═══════════════════════════════════════════════════════════════════════

    struct scroll_state
    {
        float x{0.0F}, y{0.0F};
        std::uint32_t ver{1};
        void touch() noexcept
        {
            ++ver;
        }
    };

    struct column_state
    {
        std::vector<int> order;
        std::vector<float> w;
        std::vector<std::uint8_t> user;
        std::uint32_t ver{1};
        void touch() noexcept
        {
            ++ver;
        }
    };

    struct selection_state
    {
        int r0{-1}, c0{-1}, r1{-1}, c1{-1};
        int cur_r{-1}, cur_c{-1};
        std::uint32_t ver{1};
        [[nodiscard]] bool has() const noexcept
        {
            return r0 >= 0 && c0 >= 0;
        }
        void touch() noexcept
        {
            ++ver;
        }
    };

    struct sort_state
    {
        int col{-1};
        int dir{0};
        std::uint32_t ver{1};
        void touch() noexcept
        {
            ++ver;
        }
    };

    struct filter_state
    {
        std::vector<std::string> text;
        std::uint32_t ver{1};
        void touch() noexcept
        {
            ++ver;
        }
    };

    struct edit_state
    {
        int row{-1}, col{-1};
        std::string buf;
        std::string compose;
        std::string orig;
        int caret{0};
        int anchor{0};
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

    struct check_state
    {
        std::vector<std::uint8_t> bits;
        int last{-1};
        std::uint32_t ver{1};
        void touch() noexcept
        {
            ++ver;
        }
    };

    struct undo_state
    {
        struct change
        {
            row_id id{};
            int col{0};
            std::string before;
            std::string after;
        };
        using batch = std::vector<change>;
        std::vector<batch> undo, redo;
    };

    struct view_state
    {
        scroll_state scroll;
        column_state columns;
        selection_state sel;
        sort_state sort;
        filter_state filter;
        edit_state edit;
        check_state checks;
        undo_state undo;

        zone hover{};
        int band_row{-1};
        int band_col{-1};
        float pointer_x{0.0F}, pointer_y{0.0F};
        bool left{false};
        double now{0.0};
        std::uint32_t version{1};

        void touch() noexcept
        {
            ++version;
        }

        void reset(const schema &sc, const row_source_ref &src)
        {
            columns.order.resize(static_cast<std::size_t>(sc.count()));
            std::iota(columns.order.begin(), columns.order.end(), 0);
            columns.w.resize(static_cast<std::size_t>(sc.count()));
            columns.user.assign(static_cast<std::size_t>(sc.count()), 0);
            for (int c = 0; c < sc.count(); ++c)
            {
                columns.w[static_cast<std::size_t>(c)] = sc.at(c).w;
                columns.user[static_cast<std::size_t>(c)] =
                    sc.at(c).weight > 0.0F ? 0 : 1;
            }
            columns.touch();
            filter.text.assign(static_cast<std::size_t>(sc.count()), {});
            filter.touch();
            checks.bits.assign(static_cast<std::size_t>(src.row_count()), 0);
            checks.touch();
            sel = {};
            sort = {};
            edit = {};
            undo = {};
            scroll = {};
            hover = {};
        }
    };

    // ═══════════════════════════════════════════════════════════════════════
    // 6. RowStage：变换插槽（编译期组合）
    // ═══════════════════════════════════════════════════════════════════════

    struct row_view
    {
        std::span<const row_idx> rows;
        std::uint32_t version{0};
        [[nodiscard]] int size() const noexcept
        {
            return static_cast<int>(rows.size());
        }
    };

    template <class T>
    concept RowStage = requires(const T &t, const row_source_ref &src,
                                const view_state &s, std::vector<row_idx> &v) {
        { t.apply(src, s, v) } -> std::same_as<void>;
    };

    [[nodiscard]] inline bool text_contains(std::string_view hay,
                                            std::string_view needle) noexcept
    {
        if (needle.empty())
            return true;
        if (hay.size() < needle.size())
            return false;
        const auto eq = [](char a, char b) noexcept {
            if (a >= 'A' && a <= 'Z')
                a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z')
                b = static_cast<char>(b - 'A' + 'a');
            return a == b;
        };
        for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i)
        {
            std::size_t k = 0;
            while (k < needle.size() && eq(hay[i + k], needle[k]))
                ++k;
            if (k == needle.size())
                return true;
        }
        return false;
    }

    struct filter_stage
    {
        void apply(const row_source_ref &src, const view_state &s,
                   std::vector<row_idx> &v) const;
    };

    struct sort_stage
    {
        void apply(const row_source_ref &src, const view_state &s,
                   std::vector<row_idx> &v) const;
    };

    template <RowStage... S>
    struct stage_pipeline
    {
        std::tuple<S...> stages{};
        std::vector<row_idx> buf_a;
        row_view view_{};
        [[nodiscard]] row_view build(const row_source_ref &src, const view_state &s);
    };

    inline void filter_stage::apply(const row_source_ref &src, const view_state &s,
                                    std::vector<row_idx> &v) const
    {
        const int ncol = static_cast<int>(s.filter.text.size());
        bool any = false;
        for (const auto &f : s.filter.text)
            any = any || !f.empty();
        if (!any)
            return;
        std::vector<row_idx> keep;
        keep.reserve(v.size());
        for (const int r : v)
        {
            bool ok = true;
            for (int c = 0; c < ncol && ok; ++c)
            {
                if (s.filter.text[static_cast<std::size_t>(c)].empty())
                    continue;
                ok = text_contains(src.at(r, c),
                                   s.filter.text[static_cast<std::size_t>(c)]);
            }
            if (ok)
                keep.push_back(r);
        }
        v.swap(keep);
    }

    inline void sort_stage::apply(const row_source_ref &src, const view_state &s,
                                  std::vector<row_idx> &v) const
    {
        if (s.sort.dir == 0 || s.sort.col < 0)
            return;
        const int col = s.sort.col;
        const bool desc = s.sort.dir == 2;
        std::stable_sort(v.begin(), v.end(), [&](int a, int b) {
            const auto x = src.at(a, col);
            const auto y = src.at(b, col);
            return desc ? x > y : x < y;
        });
    }

    template <RowStage... S>
    row_view stage_pipeline<S...>::build(const row_source_ref &src, const view_state &s)
    {
        const int n = src.row_count();
        buf_a.resize(static_cast<std::size_t>(n));
        std::iota(buf_a.begin(), buf_a.end(), 0);
        std::apply([&](const auto &...st) { (st.apply(src, s, buf_a), ...); }, stages);
        view_.rows = buf_a;
        view_.version = src.version() * 131U + s.filter.ver * 17U + s.sort.ver;
        return view_;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // 7. Command / Effect：控制不写数据，只发意图
    // ═══════════════════════════════════════════════════════════════════════

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
    };

    struct cmd_hover
    {
        zone z{};
    };
    struct cmd_press
    {
        float x{0}, y{0};
        zone z{};
        bool dbl{false};
        bool shift{false};
        bool ctrl{false};
    };
    struct cmd_release
    {
        float x{0}, y{0};
    };
    struct cmd_drag
    {
        float x{0}, y{0};
    };
    struct cmd_wheel
    {
        float dy{0}, dx{0};
        bool shift{false};
    };
    struct cmd_key
    {
        ui_table::key k{key::none};
        bool shift{false};
        bool ctrl{false};
    };
    struct cmd_text
    {
        std::string_view utf8{};
    };

    using command = std::variant<cmd_hover, cmd_press, cmd_release, cmd_drag, cmd_wheel,
                                 cmd_key, cmd_text>;

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

    // ═══════════════════════════════════════════════════════════════════════
    // 8. style
    // ═══════════════════════════════════════════════════════════════════════

    struct style
    {
        float header_h{40.0F};
        float row_h{30.0F};
        float row_gap{2.0F};
        float cell_pad{10.0F};
        float bar{8.0F};
        float radius{10.0F};
        float cell_radius{4.0F};
        float sep_hit{3.0F};
        float edge_hit{6.0F};
        float min_w{240.0F};
        float min_h{120.0F};
        float max_w{1.0e30F};
        float max_h{1.0e30F};
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

    // ═══════════════════════════════════════════════════════════════════════
    // 9. Frame：derive 的唯一产物（SoA）
    // ═══════════════════════════════════════════════════════════════════════

    struct zone_rect
    {
        rect r{};
        zone z{};
        std::uint16_t z_order{0};
    };

    struct frame
    {
        rect box{};
        rect header{};
        rect group{};
        rect col_header{};
        rect body{};
        rect frozen_view{};
        rect scrollv{};
        rect gutter_head{};
        rect gutter_body{};

        float group_h{0.0F};
        float gutter_w{0.0F};
        float frozen_w{0.0F};
        float content_w{0.0F};
        float content_h{0.0F};
        float sx{0.0F}, sy{0.0F};
        float step{0.0F};
        int frozen{0};

        std::vector<int> col_index;
        std::vector<float> col_x;
        std::vector<float> col_w;

        std::vector<int> row_index;
        std::vector<float> row_y;
        std::vector<float> row_h;
        std::vector<float> row_off;

        std::vector<zone_rect> zones;
        std::vector<std::uint8_t> visual;

        bool vbar{false}, hbar{false};
        rect vtrack{}, vthumb{}, htrack{}, hthumb{}, corner{};
        rect edge_n{}, edge_w{}, edge_ne{}, edge_nw{}, edge_se{}, edge_sw{};

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
            const bool froz = p < frozen;
            return body.x + gutter_w + (froz ? 0.0F : -sx) +
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
    };

    enum : std::uint8_t
    {
        vis_in_range = 1u << 0,
        vis_active = 1u << 1,
        vis_editing = 1u << 2,
        vis_hover = 1u << 3,
    };

    // ═══════════════════════════════════════════════════════════════════════
    // 10. DL：三股流 + 一个 arena
    // ═══════════════════════════════════════════════════════════════════════

    enum class stream : std::uint8_t
    {
        chrome = 0,
        content = 1,
        proxy = 2,
        Count
    };

    struct shape_op
    {
        rect r{};
        glm::vec4 color{0.0F};
        glm::vec4 border{0.0F};
        float radius{0.0F};
        float border_w{0.0F};
        std::uint32_t zone{0};
    };

    struct text_op
    {
        rect r{};
        glm::vec4 color{1.0F};
        float baseline{0.0F};
        const char *p{nullptr};
        std::uint32_t len{0};
        std::uint32_t zone{0};
    };

    struct dl
    {
        std::array<std::vector<shape_op>, 3> shapes;
        std::array<std::vector<text_op>, 3> texts;
        arena text_arena;

        void clear()
        {
            for (auto &s : shapes)
                s.clear();
            for (auto &t : texts)
                t.clear();
            text_arena.reset();
        }
        void add_shape(stream st, const rect &r, const glm::vec4 &color,
                       float radius = 0.0F, const glm::vec4 &border = {},
                       float border_w = 0.0F, std::uint32_t zk = 0)
        {
            if (r.empty())
                return;
            shapes[static_cast<std::size_t>(st)].push_back(
                {r, color, border, radius, border_w, zk});
        }
        void add_text(stream st, const rect &r, const glm::vec4 &color, float baseline,
                      std::string_view s, std::uint32_t zk = 0)
        {
            if (s.empty() || r.empty())
                return;
            const std::string_view kept = text_arena.put(s);
            texts[static_cast<std::size_t>(st)].push_back(
                {r, color, baseline, kept.data(), static_cast<std::uint32_t>(kept.size()),
                 zk});
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

    // ═══════════════════════════════════════════════════════════════════════
    // 11. Painter：行为插槽
    // ═══════════════════════════════════════════════════════════════════════

    struct cell_ctx
    {
        rect r{};
        int row{0};
        int col{0};
        std::string_view text{};
        std::uint8_t visual{0};
        const column *col_def{nullptr};
        const style *st{nullptr};
        const font *ft{nullptr};
        std::string *scratch{nullptr};
    };

    using cell_painter = void (*)(const cell_ctx &, dl &);

    namespace paint
    {
        inline void text(const cell_ctx &c, dl &out)
        {
            if (c.text.empty())
                return;
            const float inner = std::max(c.r.w - 2.0F * c.st->cell_pad, 1.0F);
            const auto fitted =
                fit_text(*c.ft, c.text, inner, out.text_arena, *c.scratch);
            const float tw = text_width(*c.ft, fitted);
            float dx = 0.0F;
            if (c.col_def->text_align == align::center)
                dx = std::max((inner - tw) * 0.5F, 0.0F);
            else if (c.col_def->text_align == align::right)
                dx = std::max(inner - tw, 0.0F);
            out.add_text(stream::content,
                         {c.r.x + c.st->cell_pad + dx, c.r.y, inner - dx, c.r.h},
                         c.st->text, c.r.y + c.r.h * 0.5F + c.ft->px * 0.30F, fitted,
                         zone_key({part::cell, c.row, c.col}));
        }

        inline void tag(const cell_ctx &c, dl &out)
        {
            if (c.text.empty())
                return;
            const float th = std::min(c.r.h - 10.0F, 20.0F);
            const float inner = std::max(c.r.w - 2.0F * c.st->cell_pad, 1.0F);
            const auto fitted = fit_text(*c.ft, c.text, std::max(inner - 16.0F, 1.0F),
                                         out.text_arena, *c.scratch);
            const float tw = text_width(*c.ft, fitted);
            const float pw = std::min(tw + 16.0F, inner);
            const auto tc = c.col_def->tag_color(fitted, {0.42F, 0.45F, 0.50F, 1.0F});
            const rect pill{c.r.x + c.st->cell_pad, c.r.y + (c.r.h - th) * 0.5F, pw, th};
            const auto zk = zone_key({part::cell, c.row, c.col});
            out.add_shape(stream::content, pill, {tc.r, tc.g, tc.b, 0.30F}, th * 0.5F, {},
                          0.0F, zk);
            out.add_text(
                stream::content,
                {pill.x + 8.0F, pill.y, std::max(pw - 16.0F, 1.0F), pill.h},
                {tc.r * 0.5F + 0.5F, tc.g * 0.5F + 0.5F, tc.b * 0.5F + 0.5F, 1.0F},
                pill.y + pill.h * 0.5F + c.ft->px * 0.30F, fitted, zk);
        }

        inline void bar(const cell_ctx &c, dl &out)
        {
            float frac = 0.0F;
            std::size_t i = 0;
            while (i < c.text.size() && (c.text[i] == ' ' || c.text[i] == '\t'))
                ++i;
            double v = 0.0;
            bool any = false, pct = false;
            while (i < c.text.size() && c.text[i] >= '0' && c.text[i] <= '9')
            {
                v = v * 10.0 + (c.text[i] - '0');
                any = true;
                ++i;
            }
            if (i < c.text.size() && c.text[i] == '.')
            {
                ++i;
                double sc = 0.1;
                while (i < c.text.size() && c.text[i] >= '0' && c.text[i] <= '9')
                {
                    v += (c.text[i] - '0') * sc;
                    sc *= 0.1;
                    any = true;
                    ++i;
                }
            }
            while (i < c.text.size() && (c.text[i] == ' ' || c.text[i] == '%'))
            {
                pct = pct || c.text[i] == '%';
                ++i;
            }
            if (any)
                frac = static_cast<float>(pct || v > 1.0 ? v / 100.0 : v);
            frac = std::clamp(frac, 0.0F, 1.0F);
            const float inner = std::max(c.r.w - 2.0F * c.st->cell_pad, 1.0F);
            const float bh = 6.0F;
            const float by = c.r.y + (c.r.h - bh) * 0.5F + 3.0F;
            const auto zk = zone_key({part::cell, c.row, c.col});
            out.add_shape(stream::content, {c.r.x + c.st->cell_pad, by, inner, bh},
                          {1.0F, 1.0F, 1.0F, 0.10F}, bh * 0.5F, {}, 0.0F, zk);
            out.add_shape(stream::content,
                          {c.r.x + c.st->cell_pad, by, std::max(inner * frac, 2.0F), bh},
                          c.st->accent, bh * 0.5F, {}, 0.0F, zk);
            const auto fitted =
                fit_text(*c.ft, c.text, inner, out.text_arena, *c.scratch);
            const float tw = text_width(*c.ft, fitted);
            out.add_text(stream::content,
                         {c.r.x + c.st->cell_pad + std::max(inner - tw, 0.0F),
                          c.r.y - 7.0F, std::max(tw, 1.0F), c.r.h},
                         c.st->text, c.r.y + c.r.h * 0.5F + c.ft->px * 0.30F - 7.0F,
                         fitted, zk);
        }

        inline void dot(const cell_ctx &c, dl &out)
        {
            if (c.text.empty())
                return;
            const auto dc = c.col_def->tag_color(c.text, {0.42F, 0.45F, 0.50F, 1.0F});
            const float d = 8.0F;
            const auto zk = zone_key({part::cell, c.row, c.col});
            out.add_shape(stream::content,
                          {c.r.x + c.st->cell_pad, c.r.y + (c.r.h - d) * 0.5F, d, d}, dc,
                          d * 0.5F, {}, 0.0F, zk);
            const float inner = std::max(c.r.w - 2.0F * c.st->cell_pad, 1.0F);
            const auto fitted = fit_text(*c.ft, c.text, std::max(inner - d - 8.0F, 1.0F),
                                         out.text_arena, *c.scratch);
            out.add_text(stream::content,
                         {c.r.x + c.st->cell_pad + d + 8.0F, c.r.y,
                          std::max(inner - d - 8.0F, 1.0F), c.r.h},
                         c.st->text, c.r.y + c.r.h * 0.5F + c.ft->px * 0.30F, fitted, zk);
        }
    } // namespace paint

    inline constexpr std::array<cell_painter,
                                static_cast<std::size_t>(render_kind::Count)>
        k_cell_painters{&paint::text, &paint::tag, &paint::bar, &paint::dot};

    // ═══════════════════════════════════════════════════════════════════════
    // 12. Table：编排者（无全局、无 IO、无 GPU）
    // ═══════════════════════════════════════════════════════════════════════

    class table
    {
      public:
        schema sch;
        array_row_source source;
        view_state state;
        style st;
        font ft;

        void bind(schema s, array_row_source src)
        {
            sch = std::move(s);
            source = std::move(src);
            state.reset(sch, source.ref());
            invalidate();
        }

        [[nodiscard]] std::uint32_t version() const noexcept
        {
            return ver_;
        }
        void set_now(double t)
        {
            state.now = t;
        }

        void dispatch(const command &cmd, std::vector<effect> &out)
        {
            std::visit([&](const auto &c) { on(c, out); }, cmd);
        }

        const frame &derive(const rect &viewport)
        {
            if (viewport.w != vp_.w || viewport.h != vp_.h || viewport.x != vp_.x ||
                viewport.y != vp_.y)
            {
                vp_ = viewport;
                invalidate();
            }
            if (dirty_)
            {
                build_frame();
                dirty_ = false;
            }
            return frame_;
        }

        void present(dl &out) const
        {
            out.clear();
            if (frame_.box.empty())
                return;
            paint_frame(out);
            paint_group(out);
            paint_header(out);
            paint_gutter(out);
            paint_rows(out);
            paint_cells(out);
            paint_selection(out);
            paint_separators(out);
            paint_scrollbars(out);
            paint_proxies(out);
        }

        // 位置 → 行/列（只用 frame_）
        [[nodiscard]] int row_at_y(float y) const noexcept
        {
            for (int i = 0; i < frame_.row_count(); ++i)
            {
                const float y0 = frame_.row_y[static_cast<std::size_t>(i)];
                const float y1 = y0 + frame_.row_h[static_cast<std::size_t>(i)];
                if (y >= y0 && y < y1)
                    return frame_.row_index[static_cast<std::size_t>(i)];
            }
            return -1;
        }
        [[nodiscard]] int col_at_x(float x) const noexcept
        {
            if (x < frame_.body.x + frame_.gutter_w)
                return -1;
            for (int i = 0; i < frame_.col_count(); ++i)
            {
                const float x0 = std::max(frame_.col_x_at(i), frame_.clip_x0(i));
                const float x1 = std::min(frame_.col_x_at(i) +
                                              frame_.col_w[static_cast<std::size_t>(i)],
                                          frame_.clip_x1(i));
                if (x >= x0 && x < x1)
                    return frame_.col_index[static_cast<std::size_t>(i)];
            }
            return -1;
        }
        [[nodiscard]] zone hit(float x, float y) const noexcept
        {
            zone best{};
            for (const auto &z : frame_.zones)
                if (x >= z.r.x && x < z.r.x1() && y >= z.r.y && y < z.r.y1())
                    best = z.z;
            return best;
        }
        [[nodiscard]] const frame &current_frame() const noexcept
        {
            return frame_;
        }
        [[nodiscard]] const std::vector<row_idx> &view_rows() const noexcept
        {
            return view_rows_;
        }

      private:
        rect vp_{};
        frame frame_{};
        std::vector<row_idx> view_rows_;
        std::vector<float> row_h_;
        float row_h_key_{-1.0F};
        std::uint32_t ver_{1};
        bool dirty_{true};
        bool header_moved_{false};
        mutable std::string scratch_;

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
        drag_mode drag_{drag_mode::none};
        zone drag_zone_{};
        float drag_base_{0.0F}, drag_base2_{0.0F};
        float drag_start_x_{0.0F}, drag_start_y_{0.0F};
        int reorder_from_{-1}, reorder_to_{-1};
        float user_w_{-1.0F}, user_h_{-1.0F};
        double last_click_t_{-1.0e9};
        float last_click_x_{0}, last_click_y_{0};
        int click_count_{0};

        stage_pipeline<filter_stage, sort_stage> pipe_;
        std::vector<std::string_view> wrap_scratch_;

        void invalidate() noexcept
        {
            ++ver_;
            state.version = ver_;
            dirty_ = true;
        }

        // ── 命令 ────────────────────────────────────────────────────────
        void on(const cmd_hover &c, std::vector<effect> &)
        {
            state.hover = c.z;
            state.band_row = -1;
            state.band_col = -1;
            switch (c.z.kind)
            {
            case part::cell:
            case part::check:
                state.band_row = c.z.row;
                state.band_col = c.z.col;
                break;
            case part::header:
            case part::col_sep:
            case part::header_group:
                state.band_col = c.z.col;
                break;
            default:
                break;
            }
        }

        void on(const cmd_press &c, std::vector<effect> &out)
        {
            state.pointer_x = c.x;
            state.pointer_y = c.y;
            state.left = true;
            drag_start_x_ = c.x;
            drag_start_y_ = c.y;
            header_moved_ = false;

            bool dbl = c.dbl;
            if (!dbl && state.now - last_click_t_ < 0.40 &&
                (c.x - last_click_x_) * (c.x - last_click_x_) +
                        (c.y - last_click_y_) * (c.y - last_click_y_) <
                    25.0F)
            {
                dbl = true;
                click_count_ = std::min(click_count_ + 1, 3);
            }
            else
            {
                click_count_ = 1;
            }
            last_click_t_ = state.now;
            last_click_x_ = c.x;
            last_click_y_ = c.y;

            if (state.edit.active() && c.z.kind == part::cell &&
                state.edit.row == c.z.row && state.edit.col == c.z.col)
            {
                edit_click(c.x);
                return;
            }
            commit_edit(out);
            drag_ = drag_mode::none;
            drag_zone_ = c.z;

            switch (c.z.kind)
            {
            case part::cell:
                select_anchor(c.z.row, c.z.col);
                drag_ = drag_mode::select;
                if (dbl)
                    begin_edit(c.z.row, c.z.col);
                break;
            case part::check:
                toggle_check(c.z.row, c.shift);
                break;
            case part::check_head:
                toggle_check_all();
                break;
            case part::header:
                if (c.z.col >= 0)
                {
                    drag_ = drag_mode::header;
                    reorder_from_ = pos_of_col(c.z.col);
                    reorder_to_ = reorder_from_;
                }
                break;
            case part::col_sep:
                if (c.z.col >= 0)
                {
                    drag_ = drag_mode::col_w;
                    drag_base_ = state.columns.w[static_cast<std::size_t>(c.z.col)];
                }
                break;
            case part::vbar_thumb:
                drag_ = drag_mode::vthumb;
                drag_base_ = state.scroll.y;
                break;
            case part::hbar_thumb:
                drag_ = drag_mode::hthumb;
                drag_base_ = state.scroll.x;
                break;
            case part::edge_n:
            case part::edge_w:
            case part::edge_ne:
            case part::edge_nw:
            case part::edge_se:
            case part::edge_sw:
                drag_ = drag_mode::table_size;
                drag_base_ = user_w_ > 0 ? user_w_ : frame_.box.w;
                drag_base2_ = user_h_ > 0 ? user_h_ : frame_.box.h;
                break;
            case part::frame:
                clear_selection();
                break;
            default:
                break;
            }
            invalidate();
        }

        void on(const cmd_drag &c, std::vector<effect> &)
        {
            state.pointer_x = c.x;
            state.pointer_y = c.y;
            switch (drag_)
            {
            case drag_mode::select:
                if (state.sel.has())
                {
                    const int r = row_at_y(c.y);
                    const int col = col_at_x(c.x);
                    if (r >= 0 && col >= 0)
                    {
                        state.sel.r1 = r;
                        state.sel.c1 = col;
                        state.sel.cur_r = r;
                        state.sel.cur_c = col;
                        state.sel.touch();
                    }
                }
                break;
            case drag_mode::col_w:
                if (drag_zone_.col >= 0)
                {
                    auto &w = state.columns.w[static_cast<std::size_t>(drag_zone_.col)];
                    const auto &def = sch.at(drag_zone_.col);
                    w = std::clamp(drag_base_ + (c.x - drag_start_x_), def.min_w,
                                   def.max_w);
                    state.columns.user[static_cast<std::size_t>(drag_zone_.col)] = 1;
                    state.columns.touch();
                }
                break;
            case drag_mode::vthumb: {
                const float max_sy = std::max(0.0F, frame_.content_h - frame_.body.h);
                const float travel = std::max(frame_.vtrack.h - frame_.vthumb.h, 1.0F);
                state.scroll.y = std::clamp(
                    drag_base_ + (c.y - drag_start_y_) * (max_sy / travel), 0.0F, max_sy);
                state.scroll.touch();
            }
            break;
            case drag_mode::hthumb: {
                const float max_sx =
                    std::max(0.0F, frame_.content_w - frame_.frozen_w - frame_.scrollv.w);
                const float travel = std::max(frame_.htrack.w - frame_.hthumb.w, 1.0F);
                state.scroll.x = std::clamp(
                    drag_base_ + (c.x - drag_start_x_) * (max_sx / travel), 0.0F, max_sx);
                state.scroll.touch();
            }
            break;
            case drag_mode::table_size:
                user_w_ = std::max(drag_base_ + (c.x - drag_start_x_), st.min_w);
                user_h_ = std::max(drag_base2_ + (c.y - drag_start_y_), st.min_h);
                break;
            case drag_mode::header:
                if (reorder_from_ >= 0)
                {
                    if (std::abs(c.x - drag_start_x_) > 4.0F)
                        header_moved_ = true;
                    const int to = pos_at_x(c.x);
                    if (to >= 0)
                        reorder_to_ = to;
                }
                break;
            default:
                break;
            }
            invalidate();
        }

        void on(const cmd_release &, std::vector<effect> &)
        {
            state.left = false;
            if (drag_ == drag_mode::header && reorder_from_ >= 0)
            {
                if (header_moved_ && reorder_to_ >= 0 && reorder_from_ != reorder_to_)
                {
                    apply_reorder();
                }
                else if (!header_moved_ && drag_zone_.col >= 0)
                {
                    auto &s = state.sort;
                    if (s.col != drag_zone_.col)
                    {
                        s.col = drag_zone_.col;
                        s.dir = 1;
                    }
                    else
                    {
                        s.dir = (s.dir + 1) % 3;
                    }
                    if (s.dir == 0)
                        s.col = -1;
                    s.touch();
                }
            }
            drag_ = drag_mode::none;
            reorder_from_ = reorder_to_ = -1;
            invalidate();
        }

        void on(const cmd_wheel &c, std::vector<effect> &)
        {
            if (c.shift)
                state.scroll.x += c.dy;
            else
                state.scroll.y -= c.dy * st.row_h;
            state.scroll.touch();
            invalidate();
        }

        void on(const cmd_key &c, std::vector<effect> &out)
        {
            if (state.edit.active())
            {
                on_key_edit(c, out);
                invalidate();
                return;
            }
            switch (c.k)
            {
            case key::up:
                move_sel(-1, 0, c.shift);
                break;
            case key::down:
                move_sel(1, 0, c.shift);
                break;
            case key::left:
                move_sel(0, -1, c.shift);
                break;
            case key::right:
                move_sel(0, 1, c.shift);
                break;
            case key::home:
                goto_row_col(state.sel.cur_r, 0, c.shift);
                break;
            case key::end:
                goto_row_col(state.sel.cur_r, sch.count() - 1, c.shift);
                break;
            case key::pgup:
                page_move(-1, c.shift);
                break;
            case key::pgdn:
                page_move(1, c.shift);
                break;
            case key::enter:
            case key::f2:
                begin_edit(state.sel.cur_r, state.sel.cur_c);
                break;
            case key::tab:
                move_sel(0, c.shift ? -1 : 1, false);
                break;
            case key::del:
            case key::backspace:
                clear_cells(out);
                break;
            case key::space:
                if (state.sel.cur_r >= 0)
                    toggle_check(state.sel.cur_r, c.shift);
                break;
            case key::a:
                if (c.ctrl)
                    select_all();
                break;
            case key::c:
                if (c.ctrl)
                    copy(out, false);
                break;
            case key::x:
                if (c.ctrl)
                    copy(out, true);
                break;
            case key::z:
                if (c.ctrl)
                    undo(out, false);
                break;
            case key::y:
                if (c.ctrl)
                    undo(out, true);
                break;
            default:
                break;
            }
            invalidate();
        }

        void on(const cmd_text &c, std::vector<effect> &)
        {
            if (state.edit.active())
            {
                replace_sel(c.utf8);
                invalidate();
                return;
            }
            if (state.sel.cur_r >= 0 && state.sel.cur_c >= 0)
            {
                begin_edit(state.sel.cur_r, state.sel.cur_c);
                state.edit.buf.clear();
                state.edit.caret = 0;
                state.edit.anchor = 0;
                replace_sel(c.utf8);
            }
            invalidate();
        }

        // ── 选择 / 编辑 / 撤销 ─────────────────────────────────────────
        void select_anchor(int r, int c)
        {
            state.sel.r0 = state.sel.r1 = state.sel.cur_r = r;
            state.sel.c0 = state.sel.c1 = state.sel.cur_c = c;
            state.sel.touch();
        }
        void clear_selection()
        {
            state.sel = {};
            state.sel.touch();
        }
        [[nodiscard]] bool in_sel(int r, int c) const noexcept
        {
            const auto &s = state.sel;
            return s.has() && r >= std::min(s.r0, s.r1) && r <= std::max(s.r0, s.r1) &&
                   c >= std::min(s.c0, s.c1) && c <= std::max(s.c0, s.c1);
        }
        [[nodiscard]] int view_pos_of(int data_row) const noexcept
        {
            for (int i = 0; i < static_cast<int>(view_rows_.size()); ++i)
                if (view_rows_[static_cast<std::size_t>(i)] == data_row)
                    return i;
            return 0;
        }
        void move_sel(int dr, int dc, bool extend)
        {
            if (state.sel.cur_r < 0)
            {
                if (!view_rows_.empty())
                    select_anchor(view_rows_.front(), 0);
                return;
            }
            if (dc != 0)
            {
                const int c = std::clamp(state.sel.cur_c + dc, 0, sch.count() - 1);
                goto_row_col(state.sel.cur_r, c, extend);
            }
            else
            {
                const int pos = view_pos_of(state.sel.cur_r);
                const int np =
                    std::clamp(pos + dr, 0, static_cast<int>(view_rows_.size()) - 1);
                goto_row_col(view_rows_[static_cast<std::size_t>(np)], state.sel.cur_c,
                             extend);
            }
        }
        void goto_row_col(int r, int c, bool extend)
        {
            if (r < 0 || c < 0)
                return;
            if (!extend)
                select_anchor(r, c);
            else
            {
                state.sel.r1 = r;
                state.sel.c1 = c;
                state.sel.cur_r = r;
                state.sel.cur_c = c;
                state.sel.touch();
            }
        }
        void page_move(int dir, bool extend)
        {
            if (state.sel.cur_r < 0 || view_rows_.empty())
                return;
            const int rows = std::max(static_cast<int>(frame_.body.h / frame_.step), 1);
            const int pos = view_pos_of(state.sel.cur_r);
            const int np =
                std::clamp(pos + dir * rows, 0, static_cast<int>(view_rows_.size()) - 1);
            goto_row_col(view_rows_[static_cast<std::size_t>(np)], state.sel.cur_c,
                         extend);
        }
        void select_all()
        {
            if (view_rows_.empty())
                return;
            state.sel.r0 = view_rows_.front();
            state.sel.r1 = view_rows_.back();
            state.sel.c0 = 0;
            state.sel.c1 = sch.count() - 1;
            state.sel.cur_r = state.sel.r1;
            state.sel.cur_c = state.sel.c1;
            state.sel.touch();
        }

        void begin_edit(int r, int c)
        {
            if (r < 0 || c < 0)
                return;
            state.edit.row = r;
            state.edit.col = c;
            state.edit.buf = std::string(source.cell(r, c));
            state.edit.orig = state.edit.buf;
            state.edit.caret = static_cast<int>(state.edit.buf.size());
            state.edit.anchor = state.edit.caret;
            state.edit.compose.clear();
        }
        void commit_edit(std::vector<effect> &out)
        {
            auto &e = state.edit;
            if (!e.active())
                return;
            if (e.buf != e.orig)
            {
                const auto id = source.id_of(e.row);
                out.push_back(fx_set_cell{id, e.col, e.buf});
                undo_state::batch b;
                b.push_back({id, e.col, e.orig, e.buf});
                push_batch(std::move(b));
            }
            e = {};
        }
        void cancel_edit()
        {
            state.edit = {};
        }
        void edit_click(float x)
        {
            auto &e = state.edit;
            const float left = frame_.box.x + st.cell_pad;
            int best = 0;
            float bestd = 1.0e30F;
            std::size_t i = 0;
            while (true)
            {
                const float w = text_width(ft, std::string_view(e.buf).substr(0, i));
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
            e.caret = best;
            e.anchor = best;
        }
        void replace_sel(std::string_view s)
        {
            auto &e = state.edit;
            const int lo = e.sel_lo(), hi = e.sel_hi();
            e.buf.replace(static_cast<std::size_t>(lo), static_cast<std::size_t>(hi - lo),
                          s);
            e.caret = lo + static_cast<int>(s.size());
            e.anchor = e.caret;
        }
        void on_key_edit(const cmd_key &c, std::vector<effect> &out)
        {
            auto &e = state.edit;
            switch (c.k)
            {
            case key::esc:
                cancel_edit();
                break;
            case key::enter:
                commit_edit(out);
                if (state.sel.cur_r >= 0 && !view_rows_.empty())
                {
                    const int pos = view_pos_of(state.sel.cur_r);
                    if (pos + 1 < static_cast<int>(view_rows_.size()))
                        select_anchor(view_rows_[static_cast<std::size_t>(pos + 1)],
                                      state.sel.cur_c);
                }
                break;
            case key::tab:
                commit_edit(out);
                move_sel(0, c.shift ? -1 : 1, false);
                break;
            case key::backspace:
                if (e.has_sel())
                    replace_sel({});
                else if (e.caret > 0)
                {
                    const std::size_t p = utf8_prev(std::string_view(e.buf), e.caret);
                    e.buf.erase(p, static_cast<std::size_t>(e.caret) - p);
                    e.caret = static_cast<int>(p);
                    e.anchor = e.caret;
                }
                break;
            case key::del:
                if (e.has_sel())
                    replace_sel({});
                else if (e.caret < static_cast<int>(e.buf.size()))
                {
                    const std::size_t n = utf8_next(std::string_view(e.buf), e.caret);
                    e.buf.erase(static_cast<std::size_t>(e.caret),
                                n - static_cast<std::size_t>(e.caret));
                }
                break;
            case key::left:
                e.caret =
                    e.has_sel()
                        ? e.sel_lo()
                        : static_cast<int>(utf8_prev(std::string_view(e.buf), e.caret));
                e.anchor = e.caret;
                break;
            case key::right:
                e.caret =
                    e.has_sel()
                        ? e.sel_hi()
                        : static_cast<int>(utf8_next(std::string_view(e.buf), e.caret));
                e.anchor = e.caret;
                break;
            case key::home:
                e.caret = e.anchor = 0;
                break;
            case key::end:
                e.caret = e.anchor = static_cast<int>(e.buf.size());
                break;
            case key::a:
                if (c.ctrl)
                {
                    e.anchor = 0;
                    e.caret = static_cast<int>(e.buf.size());
                }
                break;
            case key::c:
                if (c.ctrl)
                {
                    out.push_back(fx_clipboard{
                        e.has_sel() ? e.buf.substr(static_cast<std::size_t>(e.sel_lo()),
                                                   static_cast<std::size_t>(e.sel_hi() -
                                                                            e.sel_lo()))
                                    : e.buf});
                }
                break;
            default:
                break;
            }
        }

        void toggle_check(int r, bool shift)
        {
            if (r < 0 || static_cast<std::size_t>(r) >= state.checks.bits.size())
                return;
            const bool on = state.checks.bits[static_cast<std::size_t>(r)] == 0;
            if (shift && state.checks.last >= 0)
            {
                const int lo = std::min(state.checks.last, r);
                const int hi = std::max(state.checks.last, r);
                for (int i = lo; i <= hi; ++i)
                    state.checks.bits[static_cast<std::size_t>(i)] = on ? 1 : 0;
            }
            else
            {
                state.checks.bits[static_cast<std::size_t>(r)] = on ? 1 : 0;
            }
            state.checks.last = r;
            state.checks.touch();
        }
        void toggle_check_all()
        {
            int on = 0;
            for (const int r : view_rows_)
                on += state.checks.bits[static_cast<std::size_t>(r)] != 0;
            const bool all =
                !view_rows_.empty() && on == static_cast<int>(view_rows_.size());
            for (const int r : view_rows_)
                state.checks.bits[static_cast<std::size_t>(r)] = all ? 0 : 1;
            state.checks.touch();
        }

        void clear_cells(std::vector<effect> &out)
        {
            if (!state.sel.has())
                return;
            const int r0 = std::min(state.sel.r0, state.sel.r1);
            const int r1 = std::max(state.sel.r0, state.sel.r1);
            const int c0 = std::min(state.sel.c0, state.sel.c1);
            const int c1 = std::max(state.sel.c0, state.sel.c1);
            undo_state::batch batch;
            for (int r = r0; r <= r1; ++r)
                for (int c = c0; c <= c1; ++c)
                {
                    const auto cur = source.cell(r, c);
                    if (cur.empty())
                        continue;
                    const auto id = source.id_of(r);
                    out.push_back(fx_set_cell{id, c, std::string{}});
                    batch.push_back({id, c, std::string{cur}, std::string{}});
                }
            if (!batch.empty())
                push_batch(std::move(batch));
        }

        void copy(std::vector<effect> &out, bool cut)
        {
            if (!state.sel.has())
                return;
            const int r0 = std::min(state.sel.r0, state.sel.r1);
            const int r1 = std::max(state.sel.r0, state.sel.r1);
            const int c0 = std::min(state.sel.c0, state.sel.c1);
            const int c1 = std::max(state.sel.c0, state.sel.c1);
            std::string tsv;
            for (int r = r0; r <= r1; ++r)
            {
                for (int c = c0; c <= c1; ++c)
                {
                    if (c > c0)
                        tsv += '\t';
                    tsv += source.cell(r, c);
                }
                tsv += '\n';
            }
            out.push_back(fx_clipboard{std::move(tsv)});
            if (cut)
                clear_cells(out);
        }

        void push_batch(undo_state::batch b)
        {
            if (b.empty())
                return;
            state.undo.undo.push_back(std::move(b));
            if (state.undo.undo.size() > 100)
                state.undo.undo.erase(state.undo.undo.begin());
            state.undo.redo.clear();
        }
        void undo(std::vector<effect> &out, bool redo)
        {
            auto &from = redo ? state.undo.redo : state.undo.undo;
            auto &to = redo ? state.undo.undo : state.undo.redo;
            if (from.empty())
                return;
            auto batch = std::move(from.back());
            from.pop_back();
            for (const auto &ch : batch)
                out.push_back(fx_set_cell{ch.id, ch.col, redo ? ch.after : ch.before});
            to.push_back(std::move(batch));
        }

        // ── 列 ──────────────────────────────────────────────────────────
        [[nodiscard]] int pos_of_col(int col) const noexcept
        {
            for (int i = 0; i < static_cast<int>(state.columns.order.size()); ++i)
                if (state.columns.order[static_cast<std::size_t>(i)] == col)
                    return i;
            return -1;
        }
        [[nodiscard]] int pos_at_x(float x) const noexcept
        {
            for (int i = 0; i < frame_.col_count(); ++i)
            {
                const float x0 = std::max(frame_.col_x_at(i), frame_.clip_x0(i));
                const float x1 = std::min(frame_.col_x_at(i) +
                                              frame_.col_w[static_cast<std::size_t>(i)],
                                          frame_.clip_x1(i));
                if (x >= x0 && x < x1)
                    return i;
            }
            return -1;
        }
        void apply_reorder()
        {
            auto &o = state.columns.order;
            if (reorder_from_ < 0 || reorder_to_ < 0 ||
                reorder_from_ >= static_cast<int>(o.size()) ||
                reorder_to_ >= static_cast<int>(o.size()))
                return;
            const int v = o[static_cast<std::size_t>(reorder_from_)];
            o.erase(o.begin() + reorder_from_);
            o.insert(o.begin() + reorder_to_, v);
            state.columns.touch();
        }

        // ── 指标 ────────────────────────────────────────────────────────
        void ensure_metrics()
        {
            float width_key = 0.0F;
            for (const float w : state.columns.w)
                width_key += w * 0.013F;
            const float key = static_cast<float>(source.version()) * 7.0F +
                              static_cast<float>(sch.count()) * 131.0F +
                              st.row_h * 17.0F + (st.wrap ? 1.0F : 0.0F) * 977.0F +
                              width_key;
            if (key == row_h_key_ &&
                row_h_.size() == static_cast<std::size_t>(source.row_count()))
                return;
            row_h_key_ = key;
            row_h_.assign(static_cast<std::size_t>(source.row_count()), st.row_h);
            bool any_wrap = st.wrap;
            for (const auto &c : sch.cols)
                any_wrap = any_wrap || c.wrap;
            if (!any_wrap)
                return;
            for (int r = 0; r < source.row_count(); ++r)
            {
                float h = st.row_h;
                for (int c = 0; c < sch.count(); ++c)
                {
                    if (!sch.at(c).wrap && !st.wrap)
                        continue;
                    const float cw = col_width_of(c);
                    if (cw <= 0)
                        continue;
                    wrap_lines(ft, source.cell(r, c),
                               std::max(cw - 2.0F * st.cell_pad, 4.0F), wrap_scratch_);
                    h = std::max(h, static_cast<float>(wrap_scratch_.size()) *
                                            (ft.px * ft.line_h) +
                                        8.0F);
                }
                row_h_[static_cast<std::size_t>(r)] = h;
            }
        }
        [[nodiscard]] float col_width_of(int data_col) const noexcept
        {
            for (int i = 0; i < frame_.col_count(); ++i)
                if (frame_.col_index[static_cast<std::size_t>(i)] == data_col)
                    return frame_.col_w[static_cast<std::size_t>(i)];
            return 100.0F;
        }

        // ── 布局 ────────────────────────────────────────────────────────
        void build_frame()
        {
            frame_ = {};
            frame_.version = ver_;

            float w = user_w_ > 0 ? user_w_ : vp_.w;
            float h = user_h_ > 0 ? user_h_ : vp_.h;
            w = std::clamp(w, st.min_w, std::min(st.max_w, vp_.w));
            h = std::clamp(h, st.min_h, std::min(st.max_h, vp_.h));
            frame_.box = {vp_.x, vp_.y, w, h};
            frame_.step = st.row_h + st.row_gap;

            const float bar = std::max(st.bar, 0.0F);
            frame_.group_h = sch.any_group() ? std::max(st.group_h, 0.0F) : 0.0F;
            const float header_h = frame_.group_h + st.header_h;

            frame_.header = {frame_.box.x + 1, frame_.box.y + 1, frame_.box.w - 2 - bar,
                             header_h};
            frame_.body = {frame_.header.x, frame_.header.y + header_h, frame_.header.w,
                           frame_.box.h - 2 - header_h - bar};
            if (frame_.body.h < 0)
                frame_.body.h = 0;
            frame_.group = {frame_.header.x, frame_.header.y, frame_.header.w,
                            frame_.group_h};
            frame_.col_header = {frame_.header.x, frame_.header.y + frame_.group_h,
                                 frame_.header.w, st.header_h};

            frame_.gutter_w = st.row_select ? std::max(st.select_w, 0.0F) : 0.0F;
            if (frame_.gutter_w > frame_.body.w * 0.5F)
                frame_.gutter_w = 0;
            frame_.gutter_head = {frame_.col_header.x, frame_.col_header.y,
                                  frame_.gutter_w, frame_.col_header.h};
            frame_.gutter_body = {frame_.body.x, frame_.body.y, frame_.gutter_w,
                                  frame_.body.h};

            solve_columns(std::max(frame_.body.w - frame_.gutter_w, 1.0F));
            frame_.content_w =
                frame_.col_count() > 0
                    ? frame_.col_x.back() + frame_.col_w.back() - frame_.col_x.front()
                    : 0.0F;

            ensure_metrics();
            (void)pipe_.build(source.ref(), state);
            view_rows_.assign(pipe_.view_.rows.begin(), pipe_.view_.rows.end());

            frame_.row_off.assign(view_rows_.size() + 1, 0.0F);
            float acc = 0.0F;
            for (std::size_t i = 0; i < view_rows_.size(); ++i)
            {
                frame_.row_off[i] = acc;
                acc += row_h_[static_cast<std::size_t>(view_rows_[i])] + st.row_gap;
            }
            frame_.row_off.back() = acc;
            frame_.content_h = view_rows_.empty() ? 0.0F : acc - st.row_gap;

            const float bx = frame_.body.x + frame_.gutter_w;
            const float bw = std::max(frame_.body.w - frame_.gutter_w, 0.0F);
            frame_.frozen = std::clamp(st.frozen, 0, frame_.col_count());
            if (frame_.frozen > 0)
            {
                frame_.frozen_w = std::min(
                    frame_.col_x[static_cast<std::size_t>(frame_.frozen - 1)] +
                        frame_.col_w[static_cast<std::size_t>(frame_.frozen - 1)] -
                        frame_.col_x.front(),
                    bw * 0.8F);
                frame_.frozen_view = {bx, frame_.body.y, frame_.frozen_w, frame_.body.h};
                frame_.scrollv = {bx + frame_.frozen_w, frame_.body.y,
                                  std::max(bw - frame_.frozen_w, 0.0F), frame_.body.h};
            }
            else
            {
                frame_.frozen_w = 0;
                frame_.frozen_view = {bx, frame_.body.y, 0, frame_.body.h};
                frame_.scrollv = {bx, frame_.body.y, bw, frame_.body.h};
            }

            frame_.vbar = bar > 0 && frame_.content_h > frame_.body.h + 0.5F;
            frame_.hbar =
                bar > 0 && frame_.content_w - frame_.frozen_w > frame_.scrollv.w + 0.5F;
            const float max_sy = std::max(0.0F, frame_.content_h - frame_.body.h);
            const float max_sx =
                std::max(0.0F, frame_.content_w - frame_.frozen_w - frame_.scrollv.w);
            state.scroll.y = std::clamp(state.scroll.y, 0.0F, max_sy);
            state.scroll.x = std::clamp(state.scroll.x, 0.0F, max_sx);
            frame_.sx = state.scroll.x;
            frame_.sy = state.scroll.y;

            if (!view_rows_.empty() && frame_.body.h > 0)
            {
                const auto it = std::upper_bound(frame_.row_off.begin(),
                                                 frame_.row_off.end(), frame_.sy);
                int first = static_cast<int>(it - frame_.row_off.begin()) - 1;
                first = std::clamp(first, 0, static_cast<int>(view_rows_.size()) - 1);
                float y = frame_.body.y +
                          frame_.row_off[static_cast<std::size_t>(first)] - frame_.sy;
                for (int i = first; i < static_cast<int>(view_rows_.size()); ++i)
                {
                    const int dr = view_rows_[static_cast<std::size_t>(i)];
                    const float rh = row_h_[static_cast<std::size_t>(dr)];
                    if (y + rh > frame_.body.y && y < frame_.body.y1())
                    {
                        const float y0 = std::max(y, frame_.body.y);
                        const float y1 = std::min(y + rh, frame_.body.y1());
                        if (y1 - y0 > 0.5F)
                        {
                            frame_.row_index.push_back(dr);
                            frame_.row_y.push_back(y0);
                            frame_.row_h.push_back(y1 - y0);
                        }
                    }
                    y += rh + st.row_gap;
                    if (y >= frame_.body.y1())
                        break;
                }
            }

            if (bar > 0)
            {
                const float tx = frame_.box.x1() - bar - 1;
                const float ty = frame_.body.y + 2;
                const float th = std::max(frame_.body.h - 4, 1.0F);
                frame_.vtrack = {tx, ty, bar, th};
                if (frame_.vbar)
                {
                    const float ratio = std::clamp(
                        frame_.body.h / std::max(frame_.content_h, 1.0F), 0.06F, 1.0F);
                    const float hgt = std::max(th * ratio, 18.0F);
                    const float t = max_sy > 0 ? frame_.sy / max_sy : 0.0F;
                    frame_.vthumb = {tx, ty + (th - hgt) * t, bar, hgt};
                }
                const float hx = frame_.body.x + 2;
                const float hy = frame_.box.y1() - bar - 1;
                const float hw = std::max(frame_.body.w - 4, 1.0F);
                frame_.htrack = {hx, hy, hw, bar};
                if (frame_.hbar)
                {
                    const float ratio =
                        std::clamp(frame_.scrollv.w /
                                       std::max(frame_.content_w - frame_.frozen_w, 1.0F),
                                   0.06F, 1.0F);
                    const float wid = std::max(hw * ratio, 18.0F);
                    const float t = max_sx > 0 ? frame_.sx / max_sx : 0.0F;
                    frame_.hthumb = {hx + (hw - wid) * t, hy, wid, bar};
                }
                frame_.corner = {frame_.htrack.x1(), frame_.vtrack.y, bar, bar};
            }

            const float e = st.edge_hit;
            const float C = std::max(bar + 2.0F, e + 4.0F);
            const auto &fr = frame_.box;
            frame_.edge_n = {fr.x + C, fr.y - e, std::max(fr.w - 2 * C, 1.0F), 2 * e};
            float wh = fr.h - 2 * C;
            if (frame_.hbar)
                wh = std::min(wh, frame_.htrack.y - (fr.y + C));
            frame_.edge_w = {fr.x - e, fr.y + C, 2 * e, std::max(wh, 1.0F)};
            frame_.edge_nw = {fr.x - e, fr.y - e, C, C};
            frame_.edge_ne = {fr.x1() - C, fr.y - e, C, C};
            frame_.edge_sw = {fr.x - e, fr.y1() - C, C, C};
            frame_.edge_se = {fr.x1() - C, fr.y1() - C, C, C};

            // 不变量：违反就是布局代码写错了
            assert(frame_.header.x == frame_.body.x);
            assert(frame_.header.w == frame_.body.w);
            assert(frame_.header.h == frame_.group_h + st.header_h);
            assert(frame_.col_header.h == st.header_h);
            assert(frame_.vbar == false || frame_.vtrack.x >= frame_.body.x1() - 0.5F);
            assert(frame_.hbar == false || frame_.htrack.y >= frame_.body.y1() - 0.5F);

            build_visual();
            build_zones();
        }

        void solve_columns(float avail)
        {
            const int n = sch.count();
            frame_.col_index.resize(static_cast<std::size_t>(n));
            frame_.col_x.resize(static_cast<std::size_t>(n));
            frame_.col_w.resize(static_cast<std::size_t>(n));
            float fixed_sum = 0, weight_sum = 0;
            for (int i = 0; i < n; ++i)
            {
                const int d = state.columns.order[static_cast<std::size_t>(i)];
                frame_.col_index[static_cast<std::size_t>(i)] = d;
                const auto &def = sch.at(d);
                const bool user = state.columns.user[static_cast<std::size_t>(d)] != 0;
                const float cw = std::clamp(
                    user ? state.columns.w[static_cast<std::size_t>(d)] : def.w,
                    def.min_w, def.max_w);
                frame_.col_w[static_cast<std::size_t>(i)] = cw;
                if (!user && def.weight > 0)
                    weight_sum += def.weight;
                else
                    fixed_sum += cw;
            }
            const float rest = avail - fixed_sum;
            if (weight_sum > 0 && rest > 0)
            {
                for (int i = 0; i < n; ++i)
                {
                    const int d = frame_.col_index[static_cast<std::size_t>(i)];
                    const auto &def = sch.at(d);
                    if (state.columns.user[static_cast<std::size_t>(d)] != 0 ||
                        def.weight <= 0)
                        continue;
                    frame_.col_w[static_cast<std::size_t>(i)] = std::clamp(
                        rest * (def.weight / weight_sum), def.min_w, def.max_w);
                }
            }
            float x = 0;
            for (int i = 0; i < n; ++i)
            {
                frame_.col_x[static_cast<std::size_t>(i)] = x;
                x += frame_.col_w[static_cast<std::size_t>(i)];
            }
        }

        void build_visual()
        {
            frame_.visual.assign(frame_.row_index.size() *
                                     static_cast<std::size_t>(frame_.col_index.size()),
                                 0);
            for (int ri = 0; ri < frame_.row_count(); ++ri)
            {
                const int dr = frame_.row_index[static_cast<std::size_t>(ri)];
                for (int ci = 0; ci < frame_.col_count(); ++ci)
                {
                    const int dc = frame_.col_index[static_cast<std::size_t>(ci)];
                    std::uint8_t v = 0;
                    if (in_sel(dr, dc))
                        v |= vis_in_range;
                    if (dr == state.sel.cur_r && dc == state.sel.cur_c)
                        v |= vis_active;
                    if (state.edit.active() && state.edit.row == dr &&
                        state.edit.col == dc)
                        v |= vis_editing;
                    if (state.hover.kind == part::cell && state.hover.row == dr &&
                        state.hover.col == dc)
                        v |= vis_hover;
                    frame_.visual[static_cast<std::size_t>(ri) * frame_.col_index.size() +
                                  static_cast<std::size_t>(ci)] = v;
                }
            }
        }

        void add_zone(const rect &r, const zone &z, std::uint16_t order)
        {
            if (r.empty())
                return;
            frame_.zones.push_back({r, z, order});
        }

        void build_zones()
        {
            frame_.zones.clear();
            add_zone(frame_.box, {part::frame}, 0);
            if (frame_.group_h > 0)
                add_zone(frame_.group, {part::header_group}, 1);
            for (int i = 0; i < frame_.col_count(); ++i)
            {
                const int dc = frame_.col_index[static_cast<std::size_t>(i)];
                add_zone(clip_x({frame_.col_x_at(i), frame_.col_header.y,
                                 frame_.col_w[static_cast<std::size_t>(i)],
                                 frame_.col_header.h},
                                frame_.clip_x0(i), frame_.clip_x1(i)),
                         {part::header, -1, dc}, 2);
            }
            add_zone(frame_.gutter_head, {part::check_head}, 3);
            for (int ri = 0; ri < frame_.row_count(); ++ri)
            {
                const int dr = frame_.row_index[static_cast<std::size_t>(ri)];
                const float y = frame_.row_y[static_cast<std::size_t>(ri)];
                const float h = frame_.row_h[static_cast<std::size_t>(ri)];
                for (int ci = 0; ci < frame_.col_count(); ++ci)
                {
                    const int dc = frame_.col_index[static_cast<std::size_t>(ci)];
                    rect cr = clip_x({frame_.col_x_at(ci), y,
                                      frame_.col_w[static_cast<std::size_t>(ci)], h},
                                     frame_.clip_x0(ci), frame_.clip_x1(ci));
                    cr = intersect(cr, frame_.body);
                    add_zone(cr, {part::cell, dr, dc}, 5);
                }
                if (frame_.gutter_w > 0)
                    add_zone({frame_.gutter_body.x, y, frame_.gutter_w, h},
                             {part::check, dr, -1}, 6);
            }
            for (int i = 1; i < frame_.col_count(); ++i)
            {
                const int dc = frame_.col_index[static_cast<std::size_t>(i - 1)];
                const float x = frame_.col_x_at(i);
                add_zone(clip_x({x - st.sep_hit, frame_.col_header.y, 2 * st.sep_hit,
                                 frame_.col_header.h},
                                frame_.clip_x0(i), frame_.clip_x1(i)),
                         {part::col_sep, -1, dc}, 7);
            }
            add_zone(frame_.edge_n, {part::edge_n}, 8);
            add_zone(frame_.edge_w, {part::edge_w}, 8);
            add_zone(frame_.edge_ne, {part::edge_ne}, 8);
            add_zone(frame_.edge_nw, {part::edge_nw}, 8);
            add_zone(frame_.edge_se, {part::edge_se}, 8);
            add_zone(frame_.edge_sw, {part::edge_sw}, 8);
            if (frame_.vbar)
            {
                add_zone(frame_.vtrack, {part::vbar_track}, 9);
                add_zone(frame_.vthumb, {part::vbar_thumb}, 10);
            }
            if (frame_.hbar)
            {
                add_zone(frame_.htrack, {part::hbar_track}, 9);
                add_zone(frame_.hthumb, {part::hbar_thumb}, 10);
            }
        }

        // ── 绘制（只读）────────────────────────────────────────────────
        void paint_frame(dl &out) const
        {
            out.add_shape(stream::chrome, frame_.box, st.bg, st.radius, {1, 1, 1, 0.10F},
                          1.0F, zone_key({part::frame}));
        }

        void paint_group(dl &out) const
        {
            if (frame_.group_h <= 0)
                return;
            int i = 0;
            while (i < frame_.col_count())
            {
                const auto &def = sch.at(frame_.col_index[static_cast<std::size_t>(i)]);
                if (def.group.empty())
                {
                    ++i;
                    continue;
                }
                int j = i;
                while (j + 1 < frame_.col_count() &&
                       sch.at(frame_.col_index[static_cast<std::size_t>(j + 1)]).group ==
                           def.group)
                    ++j;
                const float x0 = frame_.col_x_at(i);
                const float x1 =
                    frame_.col_x_at(j) + frame_.col_w[static_cast<std::size_t>(j)];
                for (int k = i; k <= j; ++k)
                {
                    const rect gr = clip_x({frame_.col_x_at(k), frame_.group.y,
                                            frame_.col_w[static_cast<std::size_t>(k)],
                                            frame_.group.h},
                                           frame_.clip_x0(k), frame_.clip_x1(k));
                    if (gr.empty())
                        continue;
                    const glm::vec4 gbg{glm::vec3(st.header_bg) * 0.84F, st.header_bg.a};
                    out.add_shape(
                        stream::chrome, gr, gbg, 0.0F, {}, 0.0F,
                        zone_key({part::header_group, -1,
                                  frame_.col_index[static_cast<std::size_t>(i)]}));
                }
                const bool froz = i < frame_.frozen;
                const float no_sx = froz ? 0.0F : frame_.sx;
                const float base = frame_.body.x + frame_.gutter_w;
                const float sx0 = base + x0 - no_sx;
                const float sx1 = base + x1 - no_sx;
                const rect band = froz ? rect{frame_.frozen_view.x, frame_.group.y,
                                              frame_.frozen_view.w, frame_.group.h}
                                       : rect{frame_.scrollv.x, frame_.group.y,
                                              frame_.scrollv.w, frame_.group.h};
                const rect tr =
                    intersect({sx0, frame_.group.y, sx1 - sx0, frame_.group.h}, band);
                if (tr.w > 2)
                {
                    const auto fitted = fit_text(ft, def.group, std::max(tr.w - 12, 1.0F),
                                                 out.text_arena, scratch_);
                    const float tw = text_width(ft, fitted);
                    out.add_text(
                        stream::chrome,
                        {tr.x + std::max((tr.w - tw) * 0.5F, 0.0F), tr.y,
                         std::max(tw, 1.0F), tr.h},
                        {0.80F, 0.84F, 0.92F, 1.0F}, tr.y + tr.h * 0.5F + ft.px * 0.30F,
                        fitted,
                        zone_key({part::header_group, -1,
                                  frame_.col_index[static_cast<std::size_t>(i)]}));
                }
                i = j + 1;
            }
        }

        [[nodiscard]] bool in_sel_col(int dc) const noexcept
        {
            const auto &s = state.sel;
            return s.has() && dc >= std::min(s.c0, s.c1) && dc <= std::max(s.c0, s.c1);
        }

        void paint_header(dl &out) const
        {
            for (int i = 0; i < frame_.col_count(); ++i)
            {
                const int dc = frame_.col_index[static_cast<std::size_t>(i)];
                const rect hr = clip_x({frame_.col_x_at(i), frame_.col_header.y,
                                        frame_.col_w[static_cast<std::size_t>(i)],
                                        frame_.col_header.h},
                                       frame_.clip_x0(i), frame_.clip_x1(i));
                if (hr.empty())
                    continue;
                glm::vec4 bg = st.header_bg;
                if (state.band_col == dc)
                    bg = glm::vec4(glm::vec3(bg) + glm::vec3(0.06F), bg.a);
                else if (in_sel_col(dc))
                    bg = glm::vec4(glm::vec3(bg) + glm::vec3(0.05F), bg.a);
                out.add_shape(stream::chrome, hr, bg, st.cell_radius, {}, 0.0F,
                              zone_key({part::header, -1, dc}));
                const auto &def = sch.at(dc);
                const float inner = std::max(
                    hr.w - 2 * st.cell_pad - (def.filterable ? 18.0F : 0.0F), 1.0F);
                const float tw = text_width(ft, def.title);
                float dx = 0;
                if (def.text_align == align::center)
                    dx = std::max((inner - tw) * 0.5F, 0.0F);
                else if (def.text_align == align::right)
                    dx = std::max(inner - tw, 0.0F);
                out.add_text(stream::chrome,
                             {hr.x + st.cell_pad + dx, hr.y, inner - dx, hr.h},
                             st.header_text, hr.y + hr.h * 0.5F + ft.px * 0.30F,
                             def.title, zone_key({part::header, -1, dc}));
                if (state.sort.col == dc && state.sort.dir != 0)
                    out.add_shape(stream::chrome, {hr.x, hr.y + hr.h - 2.0F, hr.w, 2.0F},
                                  st.accent, 0.0F, {}, 0.0F,
                                  zone_key({part::header, -1, dc}));
            }
        }

        void paint_gutter(dl &out) const
        {
            if (frame_.gutter_w <= 0)
                return;
            out.add_shape(stream::chrome, frame_.gutter_head, st.header_bg, {},
                          {1, 1, 1, 0.08F}, 1.0F, zone_key({part::check_head}));
            int total = 0, on = 0;
            for (const int r : view_rows_)
            {
                ++total;
                on += state.checks.bits[static_cast<std::size_t>(r)] != 0;
            }
            const bool all = total > 0 && on == total;
            const bool partial = on > 0 && !all;
            const rect bx{frame_.gutter_head.x + (frame_.gutter_head.w - 14) * 0.5F,
                          frame_.gutter_head.y + (frame_.gutter_head.h - 14) * 0.5F, 14,
                          14};
            const auto zk = zone_key({part::check_head});
            if (all)
                out.add_shape(stream::chrome, bx, st.accent, 3.0F, {}, 0.0F, zk);
            else
                out.add_shape(stream::chrome, bx, {0, 0, 0, 0}, 3.0F,
                              glm::vec4(glm::vec3(st.accent), 0.55F), 1.2F, zk);
            if (partial)
                out.add_shape(stream::chrome,
                              {bx.x + 3, bx.y + bx.h * 0.5F - 1, bx.w - 6, 2}, st.accent,
                              1.0F, {}, 0.0F, zk);
        }

        void paint_rows(dl &out) const
        {
            for (int ri = 0; ri < frame_.row_count(); ++ri)
            {
                const int dr = frame_.row_index[static_cast<std::size_t>(ri)];
                const rect rr =
                    intersect({frame_.body.x, frame_.row_y[static_cast<std::size_t>(ri)],
                               frame_.body.w, frame_.row_h[static_cast<std::size_t>(ri)]},
                              frame_.body);
                if (rr.empty())
                    continue;
                glm::vec4 bg = st.row_bg[static_cast<std::size_t>(dr) & 1U];
                if (state.band_row == dr)
                    bg = st.row_hover;
                out.add_shape(stream::chrome, rr, bg, 0.0F, {}, 0.0F,
                              zone_key({part::cell, dr, -1}));
            }
            if (view_rows_.empty())
                out.add_text(stream::chrome,
                             {frame_.body.x, frame_.body.y, frame_.body.w, frame_.body.h},
                             {0.55F, 0.58F, 0.65F, 1.0F},
                             frame_.body.y + frame_.body.h * 0.5F + ft.px * 0.30F,
                             "No data", zone_key({part::frame}));
        }

        void paint_cells(dl &out) const
        {
            for (int ri = 0; ri < frame_.row_count(); ++ri)
            {
                const int dr = frame_.row_index[static_cast<std::size_t>(ri)];
                for (int ci = 0; ci < frame_.col_count(); ++ci)
                {
                    const int dc = frame_.col_index[static_cast<std::size_t>(ci)];
                    rect cr = clip_x({frame_.col_x_at(ci),
                                      frame_.row_y[static_cast<std::size_t>(ri)],
                                      frame_.col_w[static_cast<std::size_t>(ci)],
                                      frame_.row_h[static_cast<std::size_t>(ri)]},
                                     frame_.clip_x0(ci), frame_.clip_x1(ci));
                    cr = intersect(cr, frame_.body);
                    if (cr.empty())
                        continue;
                    const auto zk = zone_key({part::cell, dr, dc});
                    const std::uint8_t v = frame_.visual[static_cast<std::size_t>(ri) *
                                                             frame_.col_index.size() +
                                                         static_cast<std::size_t>(ci)];
                    if ((v & vis_editing) != 0)
                    {
                        out.add_shape(stream::content, cr, {0.06F, 0.08F, 0.12F, 1.0F},
                                      st.cell_radius, {}, 0.0F, zk);
                        paint_edit(out, cr, dr, dc, zk);
                        continue;
                    }
                    const bool hovered = state.hover.kind == part::cell &&
                                         state.hover.row == dr && state.hover.col == dc;
                    if ((v & vis_in_range) != 0)
                        out.add_shape(stream::content, cr, st.sel, st.cell_radius, {},
                                      0.0F, zk);
                    else if (hovered)
                        out.add_shape(stream::content, cr, {1, 1, 1, 0.06F},
                                      st.cell_radius, {}, 0.0F, zk);
                    else if (state.band_col == dc)
                        out.add_shape(stream::content, cr, st.col_hover, st.cell_radius,
                                      {}, 0.0F, zk);
                    if ((v & vis_active) != 0)
                        out.add_shape(stream::content, cr, {0, 0, 0, 0}, 0.0F, st.accent,
                                      1.5F, zk);

                    const auto &def = sch.at(dc);
                    cell_ctx ctx{cr,  dr,  dc,       source.cell(dr, dc), v, &def,
                                 &st, &ft, &scratch_};
                    const auto idx = static_cast<std::size_t>(def.render);
                    k_cell_painters[idx < k_cell_painters.size() ? idx : 0](ctx, out);
                }
            }
        }

        void paint_edit(dl &out, const rect &cr, int dr, int dc, std::uint32_t zk) const
        {
            const auto &e = state.edit;
            if (!e.active() || e.row != dr || e.col != dc)
                return;
            const float inner = std::max(cr.w - 2 * st.cell_pad, 1.0F);
            const float pre = text_width(
                ft, std::string_view(e.buf).substr(0, static_cast<std::size_t>(e.caret)));
            const float off = pre > inner ? pre - inner + 2.0F : 0.0F;
            if (e.has_sel())
            {
                const float lo =
                    text_width(ft, std::string_view(e.buf).substr(0, e.sel_lo()));
                const float hi =
                    text_width(ft, std::string_view(e.buf).substr(0, e.sel_hi()));
                if (hi > lo)
                    out.add_shape(stream::content,
                                  {cr.x + st.cell_pad + lo - off, cr.y + 4, hi - lo,
                                   std::max(cr.h - 8, 1.0F)},
                                  {st.accent.r, st.accent.g, st.accent.b, 0.35F}, 0.0F,
                                  {}, 0.0F, zk);
            }
            const rect tx = intersect({cr.x + st.cell_pad - off, cr.y, inner + off, cr.h},
                                      frame_.body);
            out.add_text(stream::content, tx, st.text, cr.y + cr.h * 0.5F + ft.px * 0.30F,
                         e.buf, zk);
            const float cx =
                std::clamp(cr.x + st.cell_pad + pre - off, cr.x + 1, cr.x + cr.w - 2);
            out.add_shape(stream::content, {cx, cr.y + 4, 1.5F, std::max(cr.h - 8, 1.0F)},
                          st.accent, 0.0F, {}, 0.0F, zk);
            out.add_shape(stream::content, cr, {0, 0, 0, 0}, 0.0F, st.accent, 1.5F, zk);
        }

        void paint_selection(dl &out) const
        {
            if (!state.sel.has() || frame_.row_index.empty())
                return;
            const int r0 = std::min(state.sel.r0, state.sel.r1);
            const int r1 = std::max(state.sel.r0, state.sel.r1);
            const int c0 = std::min(state.sel.c0, state.sel.c1);
            const int c1 = std::max(state.sel.c0, state.sel.c1);
            float y0 = 1e30F, y1 = -1e30F, x0 = 1e30F, x1 = -1e30F;
            for (int ri = 0; ri < frame_.row_count(); ++ri)
            {
                const int dr = frame_.row_index[static_cast<std::size_t>(ri)];
                if (dr >= r0 && dr <= r1)
                {
                    y0 = std::min(y0, frame_.row_y[static_cast<std::size_t>(ri)]);
                    y1 = std::max(y1, frame_.row_y[static_cast<std::size_t>(ri)] +
                                          frame_.row_h[static_cast<std::size_t>(ri)]);
                }
            }
            for (int i = 0; i < frame_.col_count(); ++i)
            {
                const int dc = frame_.col_index[static_cast<std::size_t>(i)];
                if (dc < c0 || dc > c1)
                    continue;
                const float l = std::max(frame_.col_x_at(i), frame_.clip_x0(i));
                const float r = std::min(frame_.col_x_at(i) +
                                             frame_.col_w[static_cast<std::size_t>(i)],
                                         frame_.clip_x1(i));
                if (r > l)
                {
                    x0 = std::min(x0, l);
                    x1 = std::max(x1, r);
                }
            }
            if (y1 <= y0 || x1 <= x0)
                return;
            x0 = std::max(x0, frame_.body.x);
            x1 = std::min(x1, frame_.body.x1());
            y0 = std::max(y0, frame_.body.y);
            y1 = std::min(y1, frame_.body.y1());
            const auto zk = zone_key({part::cell, r0, c0});
            const float t = 2.0F;
            out.add_shape(stream::content, {x0, y0, x1 - x0, t}, st.accent, 0, {}, 0, zk);
            out.add_shape(stream::content, {x0, y1 - t, x1 - x0, t}, st.accent, 0, {}, 0,
                          zk);
            out.add_shape(stream::content, {x0, y0, t, y1 - y0}, st.accent, 0, {}, 0, zk);
            out.add_shape(stream::content, {x1 - t, y0, t, y1 - y0}, st.accent, 0, {}, 0,
                          zk);
        }

        void paint_separators(dl &out) const
        {
            for (int i = 1; i < frame_.col_count(); ++i)
            {
                const int dc = frame_.col_index[static_cast<std::size_t>(i - 1)];
                const float x = frame_.col_x_at(i);
                const auto zk = zone_key({part::col_sep, -1, dc});
                const rect hl =
                    clip_x({x - 0.5F, frame_.col_header.y, 1, frame_.col_header.h},
                           frame_.clip_x0(i), frame_.clip_x1(i));
                const rect bl = clip_x({x - 0.5F, frame_.body.y, 1, frame_.body.h},
                                       frame_.clip_x0(i), frame_.clip_x1(i));
                if (!hl.empty())
                    out.add_shape(stream::chrome, hl, st.sep, 0, {}, 0, zk);
                if (!bl.empty())
                    out.add_shape(stream::chrome, bl, st.sep, 0, {}, 0, zk);
            }
        }

        void paint_scrollbars(dl &out) const
        {
            if (frame_.vbar)
            {
                out.add_shape(stream::chrome, frame_.vtrack, st.bar_track, st.bar * 0.5F,
                              {}, 0.0F, zone_key({part::vbar_track}));
                const bool hot =
                    state.hover.kind == part::vbar_thumb || drag_ == drag_mode::vthumb;
                out.add_shape(stream::chrome, frame_.vthumb,
                              hot ? st.bar_thumb_hot : st.bar_thumb, st.bar * 0.5F, {},
                              0.0F, zone_key({part::vbar_thumb}));
            }
            if (frame_.hbar)
            {
                out.add_shape(stream::chrome, frame_.htrack, st.bar_track, st.bar * 0.5F,
                              {}, 0.0F, zone_key({part::hbar_track}));
                const bool hot =
                    state.hover.kind == part::hbar_thumb || drag_ == drag_mode::hthumb;
                out.add_shape(stream::chrome, frame_.hthumb,
                              hot ? st.bar_thumb_hot : st.bar_thumb, st.bar * 0.5F, {},
                              0.0F, zone_key({part::hbar_thumb}));
            }
        }

        void paint_proxies(dl &out) const
        {
            for (const auto &z : frame_.zones)
                out.add_shape(stream::proxy, z.r, {0, 0, 0, 0}, 0.0F, {}, 0.0F,
                              zone_key(z.z));
        }
    };

    // ═══════════════════════════════════════════════════════════════════════
    // 13. 自测：不依赖窗口 / GPU
    // ═══════════════════════════════════════════════════════════════════════

    inline int selftest()
    {
        int fails = 0;
        const auto check = [&](bool ok, const char *what) {
            if (!ok)
            {
                ++fails;
                std::println("[table-selftest] FAIL: {}", what);
            }
        };

        schema sc;
        {
            column c0;
            c0.key = "name";
            c0.title = "Name";
            c0.w = 200;
            c0.min_w = 100;
            c0.max_w = 400;
            sc.cols.push_back(c0);
            column c1;
            c1.key = "size";
            c1.title = "Size";
            c1.w = 120;
            sc.cols.push_back(c1);
            column c2;
            c2.key = "kind";
            c2.title = "Kind";
            c2.w = 120;
            c2.render = render_kind::tag;
            sc.cols.push_back(c2);
        }

        array_row_source src;
        src.cells.resize(3);
        const char *names[] = {"beta.dat", "alpha.dat", "gamma.dat"};
        const char *sizes[] = {"20 KB", "10 KB", "30 KB"};
        const char *kinds[] = {"png", "gltf", "bin"};
        for (int i = 0; i < 3; ++i)
        {
            src.cells[0].push_back(names[i]);
            src.cells[1].push_back(sizes[i]);
            src.cells[2].push_back(kinds[i]);
            src.ids.push_back(row_id{static_cast<std::uint64_t>(i + 1)});
        }

        table t;
        t.bind(sc, src);
        t.ft.px = 15.0F;
        glyph_metrics gm;
        gm.advance = 0.5F;
        for (int c = 32; c < 127; ++c)
            t.ft.put(static_cast<char32_t>(c), gm);

        std::vector<effect> fx;
        t.set_now(0.0);
        t.dispatch(cmd_press{10, 10, {part::cell, 1, 0}, false, false, false}, fx);
        check(t.state.sel.cur_r == 1 && t.state.sel.cur_c == 0, "press selects cell");

        const auto &fr = t.derive({0, 0, 800, 600});
        check(fr.box.w > 0 && fr.box.h > 0, "derive box");
        check(fr.header.w == fr.body.w, "header/body same width");
        check(fr.col_count() == 3, "3 columns");
        check(!fr.row_index.empty(), "visible rows");

        dl out;
        t.present(out);
        check(out.op_count() > 0, "present emits ops");
        check(!out.shapes[static_cast<std::size_t>(stream::proxy)].empty(),
              "proxy stream");
        check(!out.texts[static_cast<std::size_t>(stream::content)].empty(),
              "content text");

        fx.clear();
        t.dispatch(cmd_key{key::enter, false, false}, fx);
        check(t.state.edit.active(), "enter begins edit");
        t.dispatch(cmd_text{"X"}, fx);
        t.dispatch(cmd_key{key::enter, false, false}, fx);
        check(!t.state.edit.active(), "enter commits edit");
        bool has_set = false;
        for (const auto &e : fx)
            if (const auto *p = std::get_if<fx_set_cell>(&e))
            {
                has_set = true;
                check(p->id == row_id{2}, "effect carries row_id");
            }
        check(has_set, "commit emits fx_set_cell");

        fx.clear();
        t.dispatch(cmd_press{10, 70, {part::header, -1, 0}, false, false, false}, fx);
        t.dispatch(cmd_release{10, 70}, fx);
        t.derive({0, 0, 800, 600});
        const auto &vr = t.view_rows();
        check(!vr.empty() &&
                  std::string_view(t.source.cell(vr.front(), 0)) == "alpha.dat",
              "sort ascending");
        check(t.source.id_of(vr.front()) == row_id{2}, "row_id stable under sort");

        dl a, b;
        t.present(a);
        t.present(b);
        check(a.op_count() == b.op_count(), "deterministic op count");

        std::println("[table-selftest] done, {} failed", fails);
        return fails;
    }

} // namespace ui_table
