#pragma once
// ============================================================================
// table12/model.hpp — 真值（表的一生）
//
//   Cell / Column / Table 只存事实：列、列主序单元格、稳定行身份。
//   不持有视图、窗口、指针；只有 session::dispatch 能改（经 Effect 应用）。
//
//   依赖：contract。不 include intent/frame/command/window/platform。
// ============================================================================

#include "contract.hpp"

namespace table::model
{
    struct Cell
    {
        std::string text;
        std::string note;
    };

    struct Column
    {
        ColumnId id{}; // 稳定列身份（finalize 后 = 1..N）
        std::string key;
        std::string title;
        std::string group;
        float w{140.0F}, min_w{48.0F}, max_w{640.0F}, weight{0.0F};
        Align a{Align::left};
        bool filterable{true};
        bool wrap{false};
        RenderKind render{RenderKind::text};
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
    struct Table
    {
        std::vector<Column> cols;
        std::vector<std::vector<Cell>> cells;
        std::vector<RowId> ids;

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
            // 只给还没有身份的列补 id：已有 id 永不改（CellRef / TargetKey 依赖稳定）
            std::uint32_t max_id = 0;
            for (const auto &c : cols)
                max_id = std::max(max_id, c.id.value);
            for (auto &c : cols)
                if (c.id.value == 0)
                    c.id = ColumnId{++max_id};
        }
        [[nodiscard]] RowId new_row_id() const noexcept
        {
            std::uint64_t max_id = 0;
            for (const auto &id : ids)
                max_id = std::max(max_id, id.v);
            return RowId{max_id + 1};
        }
        // ── 模型增删：行身份稳定，列身份只补新列 ─────────────────────────
        RowId insert_row(int at = -1)
        {
            if (ids.empty() || at < 0 || at > rows())
                at = rows();
            for (auto &col : cells)
                col.insert(col.begin() + at, Cell{});
            const RowId id = new_row_id();
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
        ColumnId insert_column(int at, Column c)
        {
            if (at < 0 || at > cols_count())
                at = cols_count();
            std::uint32_t max_id = 0;
            for (const auto &x : cols)
                max_id = std::max(max_id, x.id.value);
            if (c.id.value == 0)
                c.id = ColumnId{max_id + 1};
            const ColumnId id = c.id;
            cols.insert(cols.begin() + at, std::move(c));
            cells.insert(cells.begin() + at,
                         std::vector<Cell>(static_cast<std::size_t>(rows())));
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
        [[nodiscard]] ColumnId column_of(int c) const noexcept
        {
            return c >= 0 && static_cast<std::size_t>(c) < cols.size()
                       ? cols[static_cast<std::size_t>(c)].id
                       : ColumnId{};
        }
        [[nodiscard]] CellRef cell_id(int r, int c) const noexcept
        {
            return {static_cast<std::size_t>(r) < ids.size()
                        ? ids[static_cast<std::size_t>(r)]
                        : RowId{},
                    column_of(c)};
        }
        [[nodiscard]] std::optional<std::pair<int, int>> cell_index(
            CellRef ref) const noexcept
        {
            const int r = index_of(ref.row);
            if (r < 0)
                return std::nullopt;
            for (std::size_t i = 0; i < cols.size(); ++i)
                if (cols[i].id == ref.col)
                    return std::pair<int, int>{r, static_cast<int>(i)};
            return std::nullopt;
        }
        [[nodiscard]] OwnerKey owner_of(TableId t, const Zone &z) const noexcept
        {
            OwnerKey k{};
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
        [[nodiscard]] const Cell &at(int r, int c) const noexcept
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
        [[nodiscard]] int index_of(RowId id) const noexcept
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
    [[nodiscard]] inline Target make_target(Part kind, int row, int col,
                                            TableId table = {}) noexcept
    {
        return {table, kind, {}, {}, row, col};
    }
    [[nodiscard]] inline Target target_of(const Table &d, TableId t,
                                          const Zone &z) noexcept
    {
        const OwnerKey k = d.owner_of(t, z);
        return {k.table, k.kind, k.row, k.col, z.row, z.col};
    }
    [[nodiscard]] inline Target resolve(const Table &d, TargetKey key) noexcept
    {
        const Zone z = zone_from(key);
        return target_of(d, z.table, z);
    }
} // namespace table::model
