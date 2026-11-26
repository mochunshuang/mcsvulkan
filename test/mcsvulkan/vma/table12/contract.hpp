#pragma once
// ============================================================================
// table12/contract.hpp — 共享身份与坐标（最底层，无 table 依赖）
//
//   这一层只放“跨模块都要认”的名词：
//     Rect / Point / Viewport          几何基本量
//     TableId / RowId / ColumnId       稳定身份（跨帧不变）
//     Part / Zone / Target             输入与 picking 的同一套词
//     TargetKey                        唯一合并键（draw 侧 = entity_index）
//     Font / GlyphMetrics / Arena      文本与绘制资源（值语义）
//
//   依赖规则：只依赖 std + glm；不 include 任何其他 table 头，不认识窗口/GPU。
//   F1（TargetKey 位布局）与 F2（picking ABI 的键）都落在这一层。
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

namespace table
{
    // ── 基础 ──────────────────────────────────────────────────────────────
    struct Rect
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

    [[nodiscard]] inline Rect intersect(const Rect &a, const Rect &b) noexcept
    {
        const float l = std::max(a.x, b.x), t = std::max(a.y, b.y);
        const float r = std::min(a.x1(), b.x1()), d = std::min(a.y1(), b.y1());
        if (r <= l || d <= t)
            return {};
        return {l, t, r - l, d - t};
    }
    [[nodiscard]] inline Rect clip_x(const Rect &r, float x0, float x1) noexcept
    {
        const float l = std::max(r.x, x0), q = std::min(r.x1(), x1);
        if (q <= l)
            return {};
        return {l, r.y, q - l, r.h};
    }

    struct RowId
    {
        std::uint64_t v{0};
        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return v != 0;
        }
        friend constexpr bool operator==(const RowId &, const RowId &) = default;
        friend constexpr auto operator<=>(const RowId &, const RowId &) = default;
    };
    using row_idx = int;
    using col_idx = int;
    // ── 身份：table / Column / row / Cell ────────────────────────────────
    struct TableId
    {
        std::uint32_t value{};
        friend constexpr bool operator==(const TableId &, const TableId &) = default;
        friend constexpr auto operator<=>(const TableId &, const TableId &) = default;
    };
    struct ColumnId
    {
        std::uint32_t value{};
        friend constexpr bool operator==(const ColumnId &, const ColumnId &) = default;
        friend constexpr auto operator<=>(const ColumnId &, const ColumnId &) = default;
    };
    struct CellRef
    {
        RowId row{};
        ColumnId col{};
        friend constexpr bool operator==(const CellRef &, const CellRef &) = default;
    };

    // ── Zone ──────────────────────────────────────────────────────────────
    enum class Part : std::uint32_t
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

    struct Zone
    {
        Part kind{Part::none};
        int row{-1};
        int col{-1};
        TableId table{};
        [[nodiscard]] constexpr bool inside() const noexcept
        {
            return kind != Part::none;
        }
        friend constexpr bool operator==(const Zone &, const Zone &) = default;
    };

    struct OwnerKey
    {
        TableId table{};
        Part kind{Part::none};
        RowId row{};
        ColumnId col{};
        friend constexpr bool operator==(const OwnerKey &, const OwnerKey &) = default;
    };
    static_assert(std::is_trivially_copyable_v<OwnerKey>);
    static_assert(sizeof(OwnerKey) <= 32);
    // Target：输入/picking 的强类型目标；Zone 是它的几何投影
    struct Target
    {
        TableId table{};
        Part kind{Part::none};
        RowId row{};
        ColumnId col{};
        int row_index{-1};
        int col_index{-1};
        [[nodiscard]] Zone to_zone() const noexcept
        {
            return {kind, row_index, col_index, table};
        }
        friend constexpr bool operator==(const Target &, const Target &) = default;
    };

    namespace bits
    {
        inline constexpr std::uint32_t kPartShift = 27;
        inline constexpr std::uint32_t kTableShift = 22;
        inline constexpr std::uint32_t kRowShift = 9;
        inline constexpr std::uint32_t kTableMask = 0x1Fu;
        inline constexpr std::uint32_t kRowNone = 0x1FFFu;
        inline constexpr std::uint32_t kColNone = 0x1FFu;
        static_assert(static_cast<std::uint32_t>(Part::Count) <= 32, "Part > 5bit");
    } // namespace bits

    [[nodiscard]] constexpr std::uint32_t zone_key(const Zone &z) noexcept
    {
        const auto r = z.row < 0 ? bits::kRowNone : static_cast<std::uint32_t>(z.row);
        const auto c = z.col < 0 ? bits::kColNone : static_cast<std::uint32_t>(z.col);
        return (static_cast<std::uint32_t>(z.kind) << bits::kPartShift) |
               ((z.table.value & bits::kTableMask) << bits::kTableShift) |
               (r << bits::kRowShift) | c;
    }
    [[nodiscard]] constexpr Zone zone_from(std::uint32_t key) noexcept
    {
        Zone z{};
        z.kind = static_cast<Part>(key >> bits::kPartShift);
        z.table.value = (key >> bits::kTableShift) & bits::kTableMask;
        const auto r = (key >> bits::kRowShift) & bits::kRowNone;
        const auto c = key & bits::kColNone;
        z.row = r == bits::kRowNone ? -1 : static_cast<int>(r);
        z.col = c == bits::kColNone ? -1 : static_cast<int>(c);
        return z;
    }
    [[nodiscard]] inline const char *part_name(Part p) noexcept
    {
        switch (p)
        {
        case Part::frame:
            return "frame";
        case Part::header:
            return "header";
        case Part::cell:
            return "Cell";
        case Part::col_sep:
            return "col_sep";
        case Part::check:
            return "check";
        case Part::check_head:
            return "check_head";
        case Part::filter_btn:
            return "filter_btn";
        case Part::header_group:
            return "header_group";
        case Part::vbar_track:
            return "vbar_track";
        case Part::vbar_thumb:
            return "vbar_thumb";
        case Part::hbar_track:
            return "hbar_track";
        case Part::hbar_thumb:
            return "hbar_thumb";
        case Part::edge_n:
            return "edge_n";
        case Part::edge_w:
            return "edge_w";
        case Part::edge_ne:
            return "edge_ne";
        case Part::edge_nw:
            return "edge_nw";
        case Part::edge_se:
            return "edge_se";
        case Part::edge_sw:
            return "edge_sw";
        default:
            return "none";
        }
    }


    enum class Cursor : std::uint8_t
    {
        arrow = 0,
        ibeam,
        resize_ew,
        resize_ns,
        resize_nwse,
        resize_nesw,
        hand,
    };
    [[nodiscard]] inline Cursor cursor_for(const Zone &z) noexcept
    {
        switch (z.kind)
        {
        case Part::header:
        case Part::header_group:
        case Part::check:
        case Part::check_head:
        case Part::vbar_thumb:
        case Part::hbar_thumb:
            return Cursor::hand;
        case Part::col_sep:
            return Cursor::resize_ew;
        case Part::edge_n:
            return Cursor::resize_ns;
        case Part::edge_w:
            return Cursor::resize_ew;
        case Part::edge_ne:
        case Part::edge_sw:
            return Cursor::resize_nesw;
        case Part::edge_nw:
        case Part::edge_se:
            return Cursor::resize_nwse;
        default:
            return Cursor::arrow;
        }
    }

    // ── 颜色 / 字体 / 文本 / Arena ────────────────────────────────────────
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

    struct GlyphMetrics
    {
        glm::vec4 uv{0, 0, 1, 1};
        glm::vec4 plane{0};
        float advance{0};
        std::uint32_t tex{0}, samp{0}, font_id{4};
        float px_range{0};
        bool y_down{false};
    };

    struct Font
    {
        float px{15.0F};
        float line_h{1.25F};
        std::array<GlyphMetrics, 128> ascii{};
        std::array<std::uint8_t, 128> has{};
        std::unordered_map<char32_t, GlyphMetrics> other;
        void put(char32_t cp, const GlyphMetrics &m)
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
        [[nodiscard]] const GlyphMetrics *find(char32_t cp) const noexcept
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

    [[nodiscard]] inline float text_width(const Font &f, std::string_view s) noexcept
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

    // 稳定块 Arena：reset 后从 0 号块重新用（cur 是"当前块下标"）
    struct Arena
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

    [[nodiscard]] inline std::string_view fit_text(const Font &f, std::string_view s,
                                                   float max_w, Arena &ar,
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

    inline void wrap_lines(const Font &f, std::string_view s, float inner,
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
    enum class RenderKind : std::uint8_t
    {
        text = 0,
        tag,
        bar,
        dot,
        Count
    };
    enum class Align : std::uint8_t
    {
        left = 0,
        center,
        right,
    };

    // picking 协议：entity_index 就是 TargetKey；resolve 得到完整 Target
    using TargetKey = std::uint32_t;
    [[nodiscard]] inline TargetKey key_of(const Target &t) noexcept
    {
        return zone_key(t.to_zone());
    }
    // ── Owner：每类 Part 一个类型，visit_owner 是唯一分派点 ───────────────
    struct none_owner
    {
        TableId table{};
    };
    struct frame_owner
    {
        TableId table{};
    };
    struct header_owner
    {
        TableId table{};
        ColumnId col{};
        int col_index{-1};
    };
    struct sep_owner
    {
        TableId table{};
        ColumnId col{};
        int col_index{-1};
    };
    struct filter_owner
    {
        TableId table{};
        ColumnId col{};
        int col_index{-1};
    };
    struct group_owner
    {
        TableId table{};
        ColumnId col{};
        int col_index{-1};
    };
    struct cell_owner
    {
        TableId table{};
        RowId row{};
        ColumnId col{};
        int row_index{-1};
        int col_index{-1};
    };
    struct check_owner
    {
        TableId table{};
        RowId row{};
        int row_index{-1};
    };
    struct check_all_owner
    {
        TableId table{};
    };
    struct bar_owner
    {
        TableId table{};
        Part kind{Part::none};
    };
    struct edge_owner
    {
        TableId table{};
        Part kind{Part::none};
    };
    struct menu_owner
    {
        TableId table{};
        int index{-1};
    };
    using Owner = std::variant<none_owner, frame_owner, header_owner, sep_owner,
                               filter_owner, group_owner, cell_owner, check_owner,
                               check_all_owner, bar_owner, edge_owner, menu_owner>;

    [[nodiscard]] inline Owner to_owner(const Target &t) noexcept
    {
        switch (t.kind)
        {
        case Part::cell:
            return cell_owner{t.table, t.row, t.col, t.row_index, t.col_index};
        case Part::check:
            return check_owner{t.table, t.row, t.row_index};
        case Part::check_head:
            return check_all_owner{t.table};
        case Part::header:
            return header_owner{t.table, t.col, t.col_index};
        case Part::col_sep:
            return sep_owner{t.table, t.col, t.col_index};
        case Part::filter_btn:
            return filter_owner{t.table, t.col, t.col_index};
        case Part::header_group:
            return group_owner{t.table, t.col, t.col_index};
        case Part::menu_item:
            return menu_owner{t.table, t.col_index};
        case Part::frame:
            return frame_owner{t.table};
        case Part::vbar_track:
        case Part::vbar_thumb:
        case Part::hbar_track:
        case Part::hbar_thumb:
            return bar_owner{t.table, t.kind};
        case Part::edge_n:
        case Part::edge_w:
        case Part::edge_ne:
        case Part::edge_nw:
        case Part::edge_se:
        case Part::edge_sw:
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
    decltype(auto) visit_owner(const Owner &o, Fs &&...fs)
    {
        return std::visit(overloaded{std::forward<Fs>(fs)...}, o);
    }
// ── 视口：逻辑坐标 -> framebuffer 像素（F3：box 已含 owner offset） ──
using Point = glm::vec2;
struct Viewport
{
    Rect screen{};
    float scale_x{1.0F};
    float scale_y{1.0F};
};

} // namespace table
