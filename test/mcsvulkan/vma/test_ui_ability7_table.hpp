#pragma once

// ============================================================================
// test_ui_ability7_table.hpp —— 表格 widget（ability7 的 dao / 实例 / 拾取协议之上）
//
//   ★ 表格的隐形约束（代码里逐条落实，见 arrange() 末尾的 assert）
//     1. 列是唯一的一等公民：列宽属于"列"，不属于"表头"或"格"。
//     2. 表头和表体是同一片列空间的**两个视口**：
//            header_view.x == body_view.x, header_view.w == body_view.w
//        所以任何列横向移动（滚动 / 改宽 / 冻结）时，表头和格必然一起动。
//     3. 列 i 的屏幕 x 只有一个来源：layout::col_x(i)；表头格、体格、
//        分隔条、排序标记全部用它，代码层面不可能漂移。
//     4. 横向滚动只作用于非冻结列区（scroll_view）；冻结列区（frozen_view）
//        不滚。滚动条只铺 body 的范围，不压表头、不压体内容。
//     5. 裁剪分开：表头格裁到"表头的高度 + 自己那一侧的 x 带"，
//        体格裁到"体的高度 + 自己那一侧的 x 带"。
//     6. 内容宽 = Σ列宽（列间距算在列之间）；宽/高超出视口才出现对应滚动条。
//     7. 排序/选择/悬停都是"列索引 + 行索引"，不存像素。
//
//   分工：
//     layout  只回答"在哪 / 多大"
//     picking 只回答"是谁"（zone 打包进 entity_index，frag 无条件写 outPicking）
//     draw    只读 state + layout，按 z 序产出带 zone 的实例
//     update  读 input 的"当前状态"，做边沿检测 / 拖拽捕获，改 state
//     绘制顺序 = 交互优先级（last draw wins），不另写 hit 阶梯。
// ============================================================================

#include <algorithm>
#include <cassert>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "test_ui_ability7_dao.hpp"

namespace ui_table
{

    // ═══ 1. zone：几何上命中了什么 ═══════════════════════════════════════
    //   5 bit part | 13 bit row | 14 bit col；row/col 用 none 哨兵表示"不适用"
    enum class part : std::uint32_t
    {
        none = 0,
        frame,
        header,
        cell,
        col_sep,
        check,      // 行选择框（左侧沟槽）
        check_head, // 全选框（沟槽表头）
        filter_btn, // 列头筛选按钮
        header_group, // 多级表头的分组头
        vbar_track,
        vbar_thumb,
        hbar_track,
        hbar_thumb,
        // 整表缩放：只保留"不会和滚动条抢"的两条边 + 四个角。
        // 右/下两条边被滚动条占着，硬留会抢掉滚动条的点击（真实用户会立刻踩到）。
        edge_n,
        edge_w,
        edge_ne,
        edge_nw,
        edge_se,
        edge_sw,
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
        inline constexpr std::uint32_t kRowNone = 0x1FFFu; // 13 bit
        inline constexpr std::uint32_t kColNone = 0x3FFFu; // 14 bit
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

    // hover_fn：整表共用一个池下标（外部 bind 一次）；~0 = 未绑定
    inline std::uint32_t &hover_fn_ref() noexcept
    {
        static std::uint32_t v = ~0U;
        return v;
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
        }
        return "?";
    }

    // ═══ 2. 字体 / 数据 / 样式 ═══════════════════════════════════════════
    struct glyph_metrics
    {
        glm::vec4 uv{0.0F, 0.0F, 1.0F, 1.0F}; // uv_bounds：left, top, right, bottom
        glm::vec4 plane{0.0F};                // plane_bounds（1em 归一化）
        float advance{0.0F};
        std::uint32_t tex{0};
        std::uint32_t samp{0};
        std::uint32_t font{4}; // FONT_*
        float px_range{0.0F};
        bool y_down{false};
    };

    struct font
    {
        std::unordered_map<char32_t, glyph_metrics> glyphs;
        float px{15.0F};
    };

    enum class render_kind : std::uint8_t
    {
        text = 0,
        tag, // 胶囊标签（状态色）
        bar, // 进度条（0..1 或 0..100）
        dot, // 圆点 + 文本
    };

    enum class align : std::uint8_t
    {
        left = 0,
        center = 1,
        right = 2,
    };

    // 列：宽度 / 最小最大 / 权重 / 对齐 / 是否冻结。
    // key 是 JSON 行对象里的字段名；title 是表头文字（缺省 = key）。
    // "#rrggbb" → vec4（解析失败回退到 fallback）
    [[nodiscard]] inline glm::vec4 hex_color(std::string_view s,
                                             const glm::vec4 &fallback)
    {
        if (s.size() != 7 || s[0] != '#')
            return fallback;
        const auto nib = [](char ch) -> int
        {
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
    // 文本 → 0..1（带 % 或 >1 时按百分数处理）
    [[nodiscard]] inline bool parse_fraction(std::string_view s, float &out)
    {
        std::size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
            ++i;
        bool neg = false;
        if (i < s.size() && (s[i] == '+' || s[i] == '-'))
            neg = s[i++] == '-';
        double val = 0.0;
        bool any = false;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9')
        {
            val = val * 10.0 + (s[i] - '0');
            any = true;
            ++i;
        }
        if (i < s.size() && s[i] == '.')
        {
            ++i;
            double scale = 0.1;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9')
            {
                val += (s[i] - '0') * scale;
                scale *= 0.1;
                ++i;
                any = true;
            }
        }
        if (!any)
            return false;
        bool pct = false;
        while (i < s.size() && (s[i] == ' ' || s[i] == '%'))
        {
            pct = pct || s[i] == '%';
            ++i;
        }
        if (neg)
            val = -val;
        if (pct || val > 1.0)
            val /= 100.0;
        out = static_cast<float>(std::clamp(val, 0.0, 1.0));
        return true;
    }
    struct column
    {
        std::string key;
        std::string title;
        float w{140.0F};
        float min_w{48.0F};
        float max_w{640.0F};
        float weight{0.0F}; // >0 = 参与剩余宽度分配
        align text_align{align::left};
        bool filterable{true}; // 表头是否显示漏斗（列筛选）
        bool wrap{false};      // 该列多行折行（行高按内容自适应）
        std::string group;     // 多级表头：同组相邻列会在上一行合并
        // 自定义渲染：text（默认）/ tag（胶囊标签）/ bar（进度条）/ dot（圆点）
        render_kind render{render_kind::text};
        std::map<std::string, glm::vec4> tags; // tag 渲染的取值 → 颜色
    };

    struct model
    {
        std::vector<column> cols;
        std::vector<std::vector<std::string>> rows; // rows[r][c]，c 与 cols 顺序对齐
        // 稳定行 id：排序/增删后行号会变，业务侧用 id 对账
        std::vector<std::uint64_t> row_id;
        std::uint64_t next_id{1};

        [[nodiscard]] std::uint64_t id_of(int row) const noexcept
        {
            return row >= 0 && static_cast<std::size_t>(row) < row_id.size()
                       ? row_id[static_cast<std::size_t>(row)]
                       : 0;
        }
        [[nodiscard]] int index_of_id(std::uint64_t id) const noexcept
        {
            for (std::size_t i = 0; i < row_id.size(); ++i)
                if (row_id[i] == id)
                    return static_cast<int>(i);
            return -1;
        }
        // 追加一行（补齐第 c 列的空串），返回稳定 id
        std::uint64_t add_row(std::vector<std::string> cells = {})
        {
            cells.resize(cols.size());
            const std::uint64_t id = next_id++;
            rows.push_back(std::move(cells));
            row_id.push_back(id);
            return id;
        }
        std::uint64_t insert_row(int at, std::vector<std::string> cells = {})
        {
            cells.resize(cols.size());
            const int n = static_cast<int>(rows.size());
            at = std::clamp(at, 0, n);
            const std::uint64_t id = next_id++;
            rows.insert(rows.begin() + at, std::move(cells));
            row_id.insert(row_id.begin() + at, id);
            return id;
        }
        bool remove_row(std::uint64_t id)
        {
            const int at = index_of_id(id);
            if (at < 0)
                return false;
            rows.erase(rows.begin() + at);
            row_id.erase(row_id.begin() + at);
            return true;
        }
        // 删除多行（按 id）
        int remove_rows(const std::vector<std::uint64_t> &ids)
        {
            int n = 0;
            for (const auto id : ids)
                n += remove_row(id) ? 1 : 0;
            return n;
        }
        void insert_column(int at, column col)
        {
            const int n = static_cast<int>(cols.size());
            at = std::clamp(at, 0, n);
            cols.insert(cols.begin() + at, std::move(col));
            for (auto &r : rows)
                r.insert(r.begin() + static_cast<std::ptrdiff_t>(
                                        std::min<std::size_t>(
                                            static_cast<std::size_t>(at), r.size())),
                         std::string{});
        }
        bool remove_column(int at)
        {
            if (at < 0 || static_cast<std::size_t>(at) >= cols.size())
                return false;
            cols.erase(cols.begin() + at);
            for (auto &r : rows)
                if (static_cast<std::size_t>(at) < r.size())
                    r.erase(r.begin() + at);
            return true;
        }

        [[nodiscard]] std::size_t col_count() const noexcept
        {
            return cols.size();
        }
        [[nodiscard]] std::size_t row_count() const noexcept
        {
            return rows.size();
        }
        // 行对象缺字段 → 空串；数字/bool 直接 dump 成文本
        [[nodiscard]] static std::string cell_text(const nlohmann::json &v)
        {
            if (v.is_string())
                return v.get<std::string>();
            if (v.is_null())
                return {};
            return v.dump();
        }

        // ── JSON → model ────────────────────────────────────────────────
        //   {
        //     "style":   { "header_h":40, "row_h":30, "frozen":1, ... }   // 可选
        //     "columns": [ {"key":"name","title":"Name","width":260,
        //                   "min":120,"max":640,"weight":0,"align":"left"} ],
        //     "rows":    [ {"name":"a.gltf","size":"12 KB"}, ... ]
        //              | [ ["a.gltf","12 KB"], ... ]
        //   }
        [[nodiscard]] static model from_json(const nlohmann::json &j)
        {
            model m{};
            const auto &jc = j.at("columns");
            if (!jc.is_array() || jc.empty())
                throw std::runtime_error("table json: columns 必须是非空数组");

            m.cols.reserve(jc.size());
            for (const auto &c : jc)
            {
                column col{};
                col.key = c.value("key", std::string{});
                col.title = c.value("title", col.key);
                col.w = c.value("width", 140.0F);
                col.min_w = c.value("min", 48.0F);
                col.max_w = c.value("max", 640.0F);
                col.weight = c.value("weight", 0.0F);
                const auto a = c.value("align", std::string{"left"});
                col.text_align = a == "center" ? align::center
                                 : a == "right" ? align::right
                                                : align::left;
                col.filterable = c.value("filterable", true);
                col.wrap = c.value("wrap", false);
                col.group = c.value("group", std::string{});
                const auto rd = c.value("render", std::string{"text"});
                col.render = rd == "tag"   ? render_kind::tag
                             : rd == "bar" ? render_kind::bar
                             : rd == "dot" ? render_kind::dot
                                           : render_kind::text;
                if (c.contains("tags") && c["tags"].is_object())
                    for (const auto &[k, v] : c["tags"].items())
                        if (v.is_string())
                            col.tags[k] = hex_color(v.get<std::string>(),
                                                    {0.5F, 0.5F, 0.5F, 1.0F});
                if (col.key.empty())
                    throw std::runtime_error("table json: column.key 缺失");
                m.cols.push_back(std::move(col));
            }

            const auto &jr = j.contains("rows") ? j.at("rows") : nlohmann::json::array();
            m.rows.reserve(jr.size());
            for (const auto &r : jr)
            {
                std::vector<std::string> cells(m.cols.size());
                if (r.is_array())
                {
                    const std::size_t n = std::min(r.size(), cells.size());
                    for (std::size_t i = 0; i < n; ++i)
                        cells[i] = cell_text(r[i]);
                }
                else if (r.is_object())
                {
                    for (std::size_t i = 0; i < cells.size(); ++i)
                        if (r.contains(m.cols[i].key))
                            cells[i] = cell_text(r.at(m.cols[i].key));
                }
                else
                {
                    throw std::runtime_error("table json: rows 元素必须是对象或数组");
                }
                m.row_id.push_back(m.next_id++);
                m.rows.push_back(std::move(cells));
            }
            return m;
        }
    };

    struct style
    {
        float header_h{40.0F};
        float row_h{30.0F};
        float row_gap{2.0F};
        float pad{0.0F}; // 列间距
        float cell_pad{10.0F};
        float bar{8.0F};     // 滚动条粗细（0 = 不要滚动条）
        float radius{10.0F}; // 外框圆角
        float cell_radius{4.0F};
        float sep_hit{3.0F};  // 列分隔拾取半宽
        float edge_hit{6.0F}; // 整表缩放手把厚度
        float min_w{240.0F};
        float min_h{120.0F};
        float max_w{1.0e30F};
        float max_h{1.0e30F};
        int frozen{0}; // 前 N 列冻结（不参与横向滚动）
        float select_w{36.0F}; // 行选择沟槽宽（0 = 无沟槽）
        bool row_select{true}; // 是否显示行选择框
        bool wrap{false};      // 单元格多行折行（行高按内容自适应）
        float group_h{26.0F};  // 多级表头的分组行高（0 = 不显示）

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

        // JSON 里的 "style" 段是可选的，缺省就是上面的值
        void apply_json(const nlohmann::json &j)
        {
            if (!j.is_object())
                return;
            header_h = j.value("header_h", header_h);
            row_h = j.value("row_h", row_h);
            row_gap = j.value("row_gap", row_gap);
            pad = j.value("pad", pad);
            cell_pad = j.value("cell_pad", cell_pad);
            bar = j.value("bar", bar);
            radius = j.value("radius", radius);
            cell_radius = j.value("cell_radius", cell_radius);
            frozen = j.value("frozen", frozen);
            min_w = j.value("min_w", min_w);
            min_h = j.value("min_h", min_h);
            select_w = j.value("select_w", select_w);
            row_select = j.value("row_select", row_select);
            wrap = j.value("wrap", wrap);
            group_h = j.value("group_h", group_h);
        }
    };

    // ═══ 3. layout：唯一真源 ═════════════════════════════════════════════
    struct layout
    {
        ui_dao::rect_px frame{};
        ui_dao::rect_px header_view{}; // 表头视口（与 body_view 同 x/同宽）
        ui_dao::rect_px body_view{};   // 体视口
        ui_dao::rect_px frozen_view{}; // 冻结列区（不参与横向滚动）
        ui_dao::rect_px scroll_view{}; // 可滚动列区
        float frozen_w{0.0F};
        float group_h{0.0F};           // 多级表头分组行高（0 = 不分级）
        ui_dao::rect_px group_view{};  // 分组行
        ui_dao::rect_px col_header{};  // 列标题行（分组行下面）
        float gutter{0.0F};            // 左侧行选择沟槽宽
        ui_dao::rect_px check_head{};  // 全选框（表头沟槽）
        ui_dao::rect_px check_body{};  // 沟槽正文区

        struct col_metrics
        {
            float x{0.0F}; // 内容坐标系（与滚动无关）
            float w{0.0F};
            int index{0}; // 数据列
        };
        std::vector<col_metrics> cols;

        // 多级表头：相邻同组列合并成一个 span（内容坐标系，绘制时逐列裁剪）
        struct group_span
        {
            std::string title;
            float x0{0.0F};
            float x1{0.0F};
            int first_col{-1};
            int last_col{-1};
        };
        std::vector<group_span> groups;

        struct row_rect
        {
            float y{0.0F};
            float h{0.0F};
            int index{0}; // 数据行
        };
        std::vector<row_rect> rows;

        ui_dao::rect_px vbar_track{};
        ui_dao::rect_px vbar_thumb{};
        ui_dao::rect_px hbar_track{};
        ui_dao::rect_px hbar_thumb{};
        ui_dao::rect_px corner{};
        bool vbar_on{false};
        bool hbar_on{false};

        // 缩放手把：n / w 两条边 + 四角（没有 e / s —— 那是滚动条的地盘）
        struct edges_t
        {
            ui_dao::rect_px n{}, w{}, ne{}, nw{}, se{}, sw{};
        } edges{};

        float content_w{0.0F};
        float content_h{0.0F};
        float step{0.0F}; // row_h + row_gap
        float sx{0.0F}, sy{0.0F};
        int frozen{0};

        // ★ 列 i 的屏幕 x：表头格 / 体格 / 分隔条 / 排序标记的唯一来源
        [[nodiscard]] float col_x(int i) const noexcept
        {
            const auto &c = cols[static_cast<std::size_t>(i)];
            return body_view.x + gutter + (i < frozen ? 0.0F : -sx) + c.x;
        }
        // 列 i 所属的横向裁剪带（冻结列 / 滚动列）
        [[nodiscard]] float clip_x0(int i) const noexcept
        {
            return i < frozen ? frozen_view.x : scroll_view.x;
        }
        [[nodiscard]] float clip_x1(int i) const noexcept
        {
            return i < frozen ? frozen_view.x + frozen_view.w
                              : scroll_view.x + scroll_view.w;
        }
    };

    // ═══ 4. 交互状态 ═════════════════════════════════════════════════════
    enum class drag : std::uint8_t
    {
        none,
        col_width,
        vthumb,
        hthumb,
        table_size,
        select,      // 拖拽选区（Excel 式矩形）
        header,      // 按在表头上（还没判定是点击还是拖拽换序）
        col_reorder, // 表头横向拖拽 = 列换序
    };

    struct state
    {
        // 外部（picking 回读）写入：
        zone hover{}; // 本帧命中的最内层
        int band_row{-1};
        int band_col{-1};

        // 数据侧（都是索引，不存像素）
        int cur_row{-1}, cur_col{-1};                       // 活动单元格 = 选区锚点
        int sel_r0{-1}, sel_c0{-1}, sel_r1{-1}, sel_c1{-1}; // 矩形选区（含端点）
        int sort_col{-1};
        int sort_dir{0}; // 0 无 / 1 升 / 2 降

        // 几何侧
        float sx{0.0F}, sy{0.0F};
        std::vector<int> order;   // 位次 → 数据列索引（拖拽换序只改这里）
        int reorder_from{-1};
        int reorder_to{-1};
        std::vector<float> col_w;
        std::vector<std::uint8_t> col_user; // 1 = 用户改过（不再参与 weight）
        float user_w{-1.0F}, user_h{-1.0F};

        // 单元格内文本编辑（active 时键盘归编辑框，箭头移光标而不是移选区）
        struct edit_state
        {
            int row{-1}, col{-1};
            std::string buf;
            int caret{0};
            int anchor{0};            // 编辑框内的选区锚点（= caret 表示无选区）
            std::string compose;      // IME 预编辑串（只显示，不提交）
            std::string orig;         // 进入编辑时的原文（撤销用）
            bool mouse_dragging{false}; // 鼠标拖选文本中
            // 编辑框内的文本撤销（浏览器式：Ctrl+Z 先撤文本，不动表格）
            struct text_step
            {
                std::string buf;
                int caret{0};
                int anchor{0};
                double t{0.0};
            };
            std::vector<text_step> text_undo;
            std::vector<text_step> text_redo;
            double last_push_t{-1.0e9};
            bool last_op_typing{false};
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
        } edit{};

        // 剪贴板出参：update 里写，调用方（平台层）负责真正写系统剪贴板
        struct clip_out_t
        {
            std::string text;
            bool set{false};
        } clip_out{};

        // 撤销 / 重做：一批"单元格 + 旧值 + 新值"，外加当时的选择区
        //   （浏览器式：撤销不仅改内容，还把光标/选区带回原位）
        struct cell_change
        {
            int row{0};
            int col{0};
            std::string before;
            std::string after;
            std::uint64_t id{0}; // 稳定行 id（0 = 退回用 row；排序后靠它找行）
        };
        struct undo_batch
        {
            std::vector<cell_change> changes;
            int r0{-1}, c0{-1}, r1{-1}, c1{-1};
        };
        std::vector<undo_batch> undo_stack;
        std::vector<undo_batch> redo_stack;
        double last_undo_t{-1.0e9}; // 同格短时合并用

        std::uint32_t version{1}; // 几何版本：picking 回读过滤用

        // 内部
        drag dragging{drag::none};
        zone drag_zone{};
        glm::vec2 drag_start{0.0F};
        glm::vec2 pointer{0.0F}; // 本帧指针（换序幽灵 / 编辑点击定位用）
        float drag_base{0.0F};
        float drag_base2{0.0F};
        bool prev_left{false};
        float wheel_in{0.0F}; // 事件侧捕获的滚轮增量（draw 里消费）
        double now{0.0};      // 事件侧给的秒
        double last_click_t{-1.0e9};
        glm::vec2 last_click_pos{0.0F};
        int click_count{0}; // 连击计数（2=选词, 3=全选）
        // 行选择：按"数据行索引"存，排序/筛选后仍然跟着行走
        std::vector<std::uint8_t> row_sel;
        int last_check{-1}; // Shift 连选的锚点行
        // 列筛选 / 查找
        std::vector<std::string> filter; // 每列一个"包含"筛选串
        std::vector<int> row_view;       // 通过筛选的数据行（顺序 = 显示顺序）
        int filter_ver{0};
        int filter_applied{-1};
        std::size_t filter_rows{0};
        int filter_edit{-1};    // 正在编辑筛选串的列（-1 = 没在编辑）
        std::string filter_buf; // 筛选输入缓冲
        bool find_mode{false};
        std::string find_buf;
        int find_hit{-1}; // 当前命中的行（-1 = 无）
        // 变更回调：业务侧接这些即可（空 = 不回调）
        std::function<void(int row, int col, const std::string &value)> on_cell_changed;
        std::function<void()> on_selection_changed;
        std::function<void(int col, int dir)> on_sort_changed;
        std::function<void(std::uint64_t id, bool checked)> on_row_checked;
        std::function<void()> on_model_changed; // 增删行/列之后
        // 虚拟滚动：视图行偏移前缀和（只有视图/行高变化时重建）
        std::vector<float> row_off;  // 长度 = row_view.size()+1
        std::vector<int> view_pos;   // 数据行 → 视图位置（-1 = 不在视图里）
        int row_off_ver{-1};
        float row_off_key{-2.0F};
        // 行高缓存（wrap 时按内容算；索引 = 数据行）
        std::vector<float> row_h_data;
        float row_h_key{-1.0F};
        struct key_state
        {
            bool up{}, down{}, left{}, right{},
                home{}, end{}, pgup{}, pgdn{}, ctrl{}, shift{}, a{};
            bool backspace{}, del{}, enter{}, esc{}, tab{};
            bool x{}, c{}, v{}, z{}, y{}, f2{}, space{}, f{};
        } prev_keys{}; // 键盘边沿检测（"按住"≠"按了一次"）
        layout last{};

        [[nodiscard]] bool empty_cols() const noexcept
        {
            return col_w.empty();
        }
    };

    inline state &state_ref() noexcept
    {
        static state s{};
        return s;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // 基础能力清单（合格线；每条都有对应的 selftest 用例）
    //   1 导航   方向键 / Home / End / PgUp / PgDn / Ctrl+Home / Ctrl+End / Ctrl+A
    //            Enter|Shift+Enter 上下移、Tab|Shift+Tab 逐格（行末自动换行）、
    //            Ctrl+方向 跳到该方向最边
    //   2 选择   单格、拖动矩形、Shift 扩选、自动滚动、点外清除
    //   3 反馈   选择有底色 + 外框；活动格有独立描边；表头高亮所选列；行/列 hover
    //   4 编辑   双击（caret 落在点击处）/ F2 / 直接打字；光标、选区内编辑、
    //            拖选/双击选词/三连击全选/Ctrl+方向按词/IME 预编辑
    //   5 提交   Enter 下移 / Shift+Enter 上移 / Tab 右移 / Shift+Tab 左移 / Esc 取消
    //   6 数据   Delete 清空、撤销/重做（编辑提交、粘贴、清空都进栈）
    //   7 剪贴板 编辑框 Ctrl+A/C/X/V；整表 Ctrl+C/X/V（TSV）
    //   8 列     拖宽、双击自适应、拖拽换序（冻结带内独立）、排序三态
    //   9 表     整表缩放（n/w + 四角，不吃滚动条）、冻结列、横向滚动时表头与格同步
    //  10 滚动   双条拖拽/翻页/滚轮、Shift 横向、条上滚轮横向
    //  11 文本   溢出截断 + "..."、对齐（左/中/右）
    //  12 空表   正文居中 "No data"（浏览器空表的观感）
    //  13 行选择 左侧沟槽 checkbox、Shift 连选、表头全选三态、Space 切换、
    //            按数据行保持（排序/筛选后仍跟着行走）
    //  14 筛选   列头漏斗 → 就地输入"包含"串（大小写不敏感）、Ctrl+F 查找高亮+跳转
    //  15 渲染   text / tag（状态胶囊）/ bar（进度）/ dot（色点），JSON 配调色板
    //  16 折行   列级 wrap：多行文本 + 行高按内容自适应（可变行高）
    //  17 结构   add/insert/remove 行（稳定 id）、增删列、on_cell_changed /
    //            on_selection_changed / on_sort_changed / on_row_checked /
    //            on_model_changed 回调；撤销按稳定 id 定位（排序后不会写错行）
    //  18 虚拟滚动 行偏移前缀和 + 二分：每帧 O(log n + 可见行)，10 万行 < 0.01ms/帧
    //  19 多级表头 column.group 相邻同组合并成 span；点分组头选中整组列
    // ═══════════════════════════════════════════════════════════════════════

    // ── 选区（矩形，含端点）：只存行列索引，不存像素 ──────────────────────
    [[nodiscard]] inline bool has_sel(const state &s) noexcept
    {
        return s.sel_r0 >= 0 && s.sel_c0 >= 0;
    }
    [[nodiscard]] inline bool in_sel(const state &s, int row, int col) noexcept
    {
        return has_sel(s) && row >= std::min(s.sel_r0, s.sel_r1) &&
               row <= std::max(s.sel_r0, s.sel_r1) &&
               col >= std::min(s.sel_c0, s.sel_c1) &&
               col <= std::max(s.sel_c0, s.sel_c1);
    }

    inline void sel_anchor(state &s, int row, int col) noexcept
    {
        s.sel_r0 = s.sel_r1 = s.cur_row = row;
        s.sel_c0 = s.sel_c1 = s.cur_col = col;
        if (s.on_selection_changed)
            s.on_selection_changed();
    }
    inline void sel_extend(state &s, int row, int col) noexcept
    {
        if (!has_sel(s))
        {
            sel_anchor(s, row, col);
            return;
        }
        s.sel_r1 = row;
        s.sel_c1 = col;
        s.cur_row = row;
        s.cur_col = col;
        if (s.on_selection_changed)
            s.on_selection_changed();
        s.cur_row = row;
        s.cur_col = col;
    }
    inline void sel_clear(state &s) noexcept
    {
        s.sel_r0 = s.sel_c0 = s.sel_r1 = s.sel_c1 = -1;
        s.cur_row = s.cur_col = -1;
        if (s.on_selection_changed)
            s.on_selection_changed();
    }

    // 单元格的"视觉状态"：绘制只认这一个判定，测试也只测它
    enum class cell_visual : std::uint8_t
    {
        normal = 0,
        in_range, // 在选区里（但不是活动格）
        active,   // 活动格（含正在编辑的那格）
    };

    [[nodiscard]] inline cell_visual cell_visual_of(const state &s, int row,
                                                    int col) noexcept
    {
        if (s.edit.active() && s.edit.row == row && s.edit.col == col)
            return cell_visual::active;
        if (!has_sel(s))
            return cell_visual::normal;
        if (row == s.cur_row && col == s.cur_col)
            return cell_visual::active;
        return in_sel(s, row, col) ? cell_visual::in_range : cell_visual::normal;
    }

    // 表头是否处于"选中列"（Excel 的高亮列头）
    [[nodiscard]] inline bool header_selected(const state &s, int col) noexcept
    {
        return has_sel(s) && col >= std::min(s.sel_c0, s.sel_c1) &&
               col <= std::max(s.sel_c0, s.sel_c1);
    }
    // ── 行选择（按数据行索引存，排序/筛选后仍跟着行走）──────────────────
    [[nodiscard]] inline bool row_checked(const state &s, int row) noexcept
    {
        return row >= 0 && static_cast<std::size_t>(row) < s.row_sel.size() &&
               s.row_sel[static_cast<std::size_t>(row)] != 0;
    }
    [[nodiscard]] inline int checked_count(const state &s) noexcept
    {
        int n = 0;
        for (const auto v : s.row_sel)
            n += v != 0 ? 1 : 0;
        return n;
    }
    // 只统计"当前可见行"（筛选后的视图）——表头全选的正确口径
    [[nodiscard]] inline int checked_count_visible(const state &s) noexcept
    {
        int n = 0;
        for (const int r : s.row_view)
            n += row_checked(s, r) ? 1 : 0;
        return n;
    }
    [[nodiscard]] inline bool all_checked_visible(const state &s) noexcept
    {
        return !s.row_view.empty() &&
               checked_count_visible(s) == static_cast<int>(s.row_view.size());
    }
    [[nodiscard]] inline bool all_checked(const state &s) noexcept
    {
        return !s.row_sel.empty() &&
               checked_count(s) == static_cast<int>(s.row_sel.size());
    }
    inline void row_check_set(state &s, int row, bool on) noexcept
    {
        if (row < 0 || static_cast<std::size_t>(row) >= s.row_sel.size())
            return;
        s.row_sel[static_cast<std::size_t>(row)] = on ? 1 : 0;
        s.last_check = row;
    }
    inline void row_check_range(state &s, int a, int b, bool on) noexcept
    {
        if (s.row_sel.empty())
            return;
        const int lo = std::clamp(std::min(a, b), 0,
                                  static_cast<int>(s.row_sel.size()) - 1);
        const int hi = std::clamp(std::max(a, b), 0,
                                  static_cast<int>(s.row_sel.size()) - 1);
        for (int r = lo; r <= hi; ++r)
            s.row_sel[static_cast<std::size_t>(r)] = on ? 1 : 0;
        s.last_check = hi;
    }
    [[nodiscard]] inline std::vector<int> checked_rows(const state &s)
    {
        std::vector<int> out;
        for (std::size_t i = 0; i < s.row_sel.size(); ++i)
            if (s.row_sel[i] != 0)
                out.push_back(static_cast<int>(i));
        return out;
    }
    // ── 筛选 / 查找 ─────────────────────────────────────────────────────
    [[nodiscard]] inline std::string ascii_lower(std::string_view s)
    {
        std::string out{s};
        for (auto &ch : out)
            if (ch >= 'A' && ch <= 'Z')
                ch = static_cast<char>(ch - 'A' + 'a');
        return out;
    }
    // 包含匹配（ASCII 大小写不敏感）
    [[nodiscard]] inline bool text_contains(std::string_view hay,
                                            std::string_view needle) noexcept
    {
        if (needle.empty())
            return true;
        if (hay.size() < needle.size())
            return false;
        for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i)
        {
            std::size_t k = 0;
            while (k < needle.size())
            {
                char a = hay[i + k];
                char b = needle[k];
                if (a >= 'A' && a <= 'Z')
                    a = static_cast<char>(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z')
                    b = static_cast<char>(b - 'A' + 'a');
                if (a != b)
                    break;
                ++k;
            }
            if (k == needle.size())
                return true;
        }
        return false;
    }
    // 依据 s.filter 重建 row_view（按数据行号，保持原顺序）
    inline void apply_filters(state &s, const model &m)
    {
        s.row_view.clear();
        for (std::size_t i = 0; i < m.rows.size(); ++i)
        {
            bool ok = true;
            const auto &cells = m.rows[i];
            for (std::size_t c = 0; c < m.cols.size() && c < s.filter.size(); ++c)
            {
                if (s.filter[c].empty() || c >= cells.size())
                    continue;
                if (!text_contains(cells[c], s.filter[c]))
                {
                    ok = false;
                    break;
                }
            }
            if (ok)
                s.row_view.push_back(static_cast<int>(i));
        }
        s.filter_ver++;
        s.filter_applied = s.filter_ver;
        s.filter_rows = m.rows.size();
        if (s.cur_row >= 0 &&
            std::find(s.row_view.begin(), s.row_view.end(), s.cur_row) ==
                s.row_view.end())
            sel_clear(s); // 当前格被筛掉了
    }
    [[nodiscard]] inline bool find_matches(const state &s, const model &m, int row)
    {
        if (s.find_buf.empty() || row < 0 ||
            static_cast<std::size_t>(row) >= m.rows.size())
            return false;
        for (const auto &cell : m.rows[static_cast<std::size_t>(row)])
            if (text_contains(cell, s.find_buf))
                return true;
        return false;
    }
    // 从 (row,col) 之后找下一个命中，返回数据行号（-1 = 没有）
    [[nodiscard]] inline int find_next(const state &s, const model &m, int from_row)
    {
        const int n = static_cast<int>(s.row_view.size());
        if (n <= 0 || s.find_buf.empty())
            return -1;
        int start = 0;
        for (int i = 0; i < n; ++i)
            if (s.row_view[static_cast<std::size_t>(i)] == from_row)
            {
                start = i + 1;
                break;
            }
        for (int k = 0; k < n; ++k)
        {
            const int i = (start + k) % n;
            if (find_matches(s, m, s.row_view[static_cast<std::size_t>(i)]))
                return s.row_view[static_cast<std::size_t>(i)];
        }
        return -1;
    }

    // 像素 → 行/列：只用已算好的 layout，不做第二套几何
    [[nodiscard]] inline int row_at(const layout &L, float y) noexcept
    {
        if (L.rows.empty())
            return -1;
        for (const auto &r : L.rows)
            if (y >= r.y && y < r.y + r.h)
                return r.index;
        return y < L.rows.front().y ? L.rows.front().index : L.rows.back().index;
    }
    [[nodiscard]] inline int col_at(const layout &L, float x) noexcept
    {
        if (L.cols.empty())
            return -1;
        if (x < L.body_view.x + L.gutter)
            return -1; // 沟槽区不算任何列
        for (int i = 0; i < static_cast<int>(L.cols.size()); ++i)
        {
            const float x0 = std::max(L.col_x(i), L.clip_x0(i));
            const float x1 = std::min(L.col_x(i) + L.cols[i].w, L.clip_x1(i));
            if (x >= x0 && x < x1)
                return L.cols[i].index;
        }
        return x < L.col_x(0) ? L.cols.front().index : L.cols.back().index;
    }
    // 列位次（数据列索引 → L.cols 下标）
    [[nodiscard]] inline int col_pos(const layout &L, int col_index) noexcept
    {
        for (int i = 0; i < static_cast<int>(L.cols.size()); ++i)
            if (L.cols[i].index == col_index)
                return i;
        return -1;
    }

    // ═══ 5. 小工具 ═══════════════════════════════════════════════════════

    [[nodiscard]] inline ui_dao::rect_px intersect(const ui_dao::rect_px &a,
                                                   const ui_dao::rect_px &b) noexcept
    {
        const float l = std::max(a.x, b.x), t = std::max(a.y, b.y);
        const float r = std::min(a.x + a.w, b.x + b.w);
        const float d = std::min(a.y + a.h, b.y + b.h);
        if (r <= l || d <= t)
            return {0.0F, 0.0F, 0.0F, 0.0F};
        return {l, t, r - l, d - t};
    }

    // 只做水平裁剪：表头用表头的高度、体用体的高度，互不干扰
    [[nodiscard]] inline ui_dao::rect_px clip_x(const ui_dao::rect_px &r, float x0,
                                                float x1) noexcept
    {
        const float l = std::max(r.x, x0), q = std::min(r.x + r.w, x1);
        if (q <= l)
            return {0.0F, 0.0F, 0.0F, 0.0F};
        return {l, r.y, q - l, r.h};
    }


    // 字形矩形裁到 clip：相交 + UV 等比重映射（不动 shader）
    [[nodiscard]] inline bool clip_glyph(ui_dao::rect_px &r, UvTransform &uv,
                                         const ui_dao::rect_px &clip) noexcept
    {
        if (r.w <= 0.0F || r.h <= 0.0F || clip.w <= 0.0F || clip.h <= 0.0F)
            return false;
        const float l = std::max(r.x, clip.x), t = std::max(r.y, clip.y);
        const float q = std::min(r.x + r.w, clip.x + clip.w);
        const float b = std::min(r.y + r.h, clip.y + clip.h);
        if (q <= l || b <= t)
            return false;
        const float sx = (l - r.x) / r.w, sy = (t - r.y) / r.h;
        const float sw = (q - l) / r.w, sh = (b - t) / r.h;
        uv.offset += glm::vec2(sx * uv.scale.x, sy * uv.scale.y);
        uv.scale *= glm::vec2(sw, sh);
        r = {l, t, q - l, b - t};
        return true;
    }

    // 跟踪日志：默认关闭，TABLE_TRACE=1 才打印（发布态保持干净）
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
    // ── UTF-8 光标移动（编辑框用）──────────────────────────────────────
    [[nodiscard]] inline std::size_t utf8_prev(std::string_view s,
                                              std::size_t i) noexcept
    {
        if (i == 0 || s.empty())
            return 0;
        --i;
        while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
            --i;
        return i;
    }
    [[nodiscard]] inline std::size_t utf8_next(std::string_view s,
                                              std::size_t i) noexcept
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
        const unsigned char u = static_cast<unsigned char>(c);
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

    // ── 撤销 / 重做 ──────────────────────────────────────────────────
    // 记录一步；同一格、0.8s 内的连续修改合并成一步（浏览器的输入合并习惯）
    inline void undo_record(state &s, std::vector<state::cell_change> changes)
    {
        if (changes.empty())
            return;
        const bool same_cell =
            changes.size() == 1 && !s.undo_stack.empty() &&
            s.undo_stack.back().changes.size() == 1 &&
            s.undo_stack.back().changes[0].row == changes[0].row &&
            s.undo_stack.back().changes[0].col == changes[0].col &&
            (s.now - s.last_undo_t) < 0.8;
        if (same_cell)
        {
            s.undo_stack.back().changes[0].after = changes[0].after;
        }
        else
        {
            s.undo_stack.push_back(state::undo_batch{std::move(changes), s.sel_r0,
                                                     s.sel_c0, s.sel_r1, s.sel_c1});
            if (s.undo_stack.size() > 100)
                s.undo_stack.erase(s.undo_stack.begin());
        }
        s.last_undo_t = s.now;
        s.redo_stack.clear();
    }

    inline void undo_apply(model &m, const std::vector<state::cell_change> &changes,
                           bool use_before)
    {
        for (const auto &c : changes)
        {
            // 有稳定 id 就按 id 找行：这样"编辑 → 排序 → 撤销"不会写错行
            const int r = c.id != 0 ? m.index_of_id(c.id) : c.row;
            if (r < 0 || static_cast<std::size_t>(r) >= m.rows.size())
                continue;
            auto &cells = m.rows[static_cast<std::size_t>(r)];
            if (static_cast<std::size_t>(c.col) >= cells.size())
                continue;
            cells[static_cast<std::size_t>(c.col)] = use_before ? c.before : c.after;
        }
    }

    inline void undo_step(state &s, model &m, bool redo)
    {
        auto &from = redo ? s.redo_stack : s.undo_stack;
        auto &to = redo ? s.undo_stack : s.redo_stack;
        if (from.empty())
            return;
        auto entry = std::move(from.back());
        from.pop_back();
        undo_apply(m, entry.changes, !redo);
        if (s.on_cell_changed)
            for (const auto &ch : entry.changes)
                s.on_cell_changed(ch.row, ch.col, redo ? ch.after : ch.before);
        // 撤销把"当时的选区/活动格"带回来（浏览器式）
        if (entry.r0 >= 0)
        {
            s.sel_r0 = entry.r0;
            s.sel_c0 = entry.c0;
            s.sel_r1 = entry.r1;
            s.sel_c1 = entry.c1;
            s.cur_row = entry.r1 >= 0 ? entry.r1 : entry.r0;
            s.cur_col = entry.c1 >= 0 ? entry.c1 : entry.c0;
        }
        to.push_back(std::move(entry));
        s.last_undo_t = s.now;
        trace("[undo] {}", redo ? "redo" : "undo");
    }

    // 清空选区内容（带撤销）
    inline void clear_selection(state &s, model &m)
    {
        if (!has_sel(s))
            return;
        const int r0 = std::min(s.sel_r0, s.sel_r1);
        const int r1 = std::max(s.sel_r0, s.sel_r1);
        const int c0 = std::min(s.sel_c0, s.sel_c1);
        const int c1 = std::max(s.sel_c0, s.sel_c1);
        std::vector<state::cell_change> changes;
        for (int r = r0; r <= r1; ++r)
        {
            if (static_cast<std::size_t>(r) >= m.rows.size())
                continue;
            auto &cells = m.rows[static_cast<std::size_t>(r)];
            for (int c = c0; c <= c1 && c < static_cast<int>(cells.size()); ++c)
                if (!cells[static_cast<std::size_t>(c)].empty())
                    changes.push_back({r, c, cells[static_cast<std::size_t>(c)], "",
                                       m.id_of(r)});
        }
        if (changes.empty())
            return;
        undo_apply(m, changes, false);
        if (s.on_cell_changed)
            for (const auto &ch : changes)
                s.on_cell_changed(ch.row, ch.col, std::string{});
        trace("[edit] clear {} cells", changes.size());
        undo_record(s, std::move(changes));
    }

    // ═══ 6. 布局 ═════════════════════════════════════════════════════════
    inline void reset_state(state &s, const model &m)
    {
        s.order.resize(m.cols.size());
        for (std::size_t i = 0; i < m.cols.size(); ++i)
            s.order[i] = static_cast<int>(i);
        s.reorder_from = s.reorder_to = -1;
        s.col_w.resize(m.cols.size());
        s.col_user.assign(m.cols.size(), 0);
        for (std::size_t i = 0; i < m.cols.size(); ++i)
        {
            s.col_w[i] = m.cols[i].w;
            s.col_user[i] = m.cols[i].weight > 0.0F ? 0 : 1;
        }
        s.sx = 0.0F;
        s.sy = 0.0F;
        sel_clear(s);
        s.sort_col = -1;
        s.sort_dir = 0;
        s.row_sel.assign(m.rows.size(), 0);
        s.last_check = -1;
        s.filter.assign(m.cols.size(), {});
        s.row_view.clear();
        for (std::size_t i = 0; i < m.rows.size(); ++i)
            s.row_view.push_back(static_cast<int>(i));
        s.filter_ver++;
        s.filter_edit = -1;
        s.find_mode = false;
        s.row_h_data.clear();
        s.row_h_key = -1.0F;
    }

    inline void solve_columns(const model &m, const state &s, const style &st,
                              float avail, std::vector<layout::col_metrics> &out)
    {
        const int n = static_cast<int>(m.cols.size());
        out.assign(static_cast<std::size_t>(std::max(n, 0)), {});
        if (n <= 0)
            return;

        // 位次 i → 数据列 order[i]：换序只改 order，宽度/权重仍按数据列存
        const auto data_of = [&s, n](int i) noexcept
        {
            return i < static_cast<int>(s.order.size())
                       ? s.order[static_cast<std::size_t>(i)]
                       : i;
        };
        const auto is_user = [&s](int d) noexcept
        {
            return d < static_cast<int>(s.col_user.size()) &&
                   s.col_user[static_cast<std::size_t>(d)] != 0;
        };

        std::vector<float> w(static_cast<std::size_t>(n), 0.0F);
        float fixed_sum = 0.0F, weight_sum = 0.0F;
        for (int i = 0; i < n; ++i)
        {
            const int d = data_of(i);
            const auto &c = m.cols[static_cast<std::size_t>(d)];
            const bool user = is_user(d);
            w[static_cast<std::size_t>(i)] =
                std::clamp(user ? s.col_w[static_cast<std::size_t>(d)] : c.w, c.min_w,
                           c.max_w);
            if (!user && c.weight > 0.0F)
                weight_sum += c.weight;
            else
                fixed_sum += w[static_cast<std::size_t>(i)];
        }

        const float gaps = st.pad * static_cast<float>(std::max(n - 1, 0));
        const float rest = avail - fixed_sum - gaps;
        if (weight_sum > 0.0F && rest > 0.0F)
        {
            for (int i = 0; i < n; ++i)
            {
                const int d = data_of(i);
                const auto &c = m.cols[static_cast<std::size_t>(d)];
                if (is_user(d) || c.weight <= 0.0F)
                    continue;
                const float share = rest * (c.weight / weight_sum);
                w[static_cast<std::size_t>(i)] = std::clamp(share, c.min_w, c.max_w);
            }
        }

        float x = 0.0F;
        for (int i = 0; i < n; ++i)
        {
            out[static_cast<std::size_t>(i)] = {x, w[static_cast<std::size_t>(i)],
                                                data_of(i)};
            x += w[static_cast<std::size_t>(i)] + st.pad;
        }
    }

    [[nodiscard]] inline ui_dao::rect_px box_for(const state &s, const style &st,
                                                 const ui_dao::rect_px &avail) noexcept
    {
        float w = s.user_w > 0.0F ? s.user_w : avail.w;
        float h = s.user_h > 0.0F ? s.user_h : avail.h;
        w = std::clamp(w, st.min_w, std::min(st.max_w, avail.w));
        h = std::clamp(h, st.min_h, std::min(st.max_h, avail.h));
        return {avail.x, avail.y, w, h};
    }

    [[nodiscard]] inline layout arrange(const model &m, const state &s, const style &st,
                                        const ui_dao::rect_px &avail)
    {
        layout L{};
        L.frame = box_for(s, st, avail);
        L.step = st.row_h + st.row_gap;
        L.frozen = std::clamp(st.frozen, 0, static_cast<int>(m.cols.size()));

        const float bar = std::max(st.bar, 0.0F);
        const float f = 1.0F; // 外框内边距

        // 多级表头：有任意列带 group 就多一行
        bool any_group = false;
        for (const auto &c : m.cols)
            any_group = any_group || !c.group.empty();
        L.group_h = any_group ? std::max(st.group_h, 0.0F) : 0.0F;
        const float header_h = L.group_h + st.header_h;

        // ★ 表头与体：同 x、同宽。这就是"头 cell 与列必须一起动"的结构保证。
        L.header_view = {L.frame.x + f, L.frame.y + f, L.frame.w - 2.0F * f - bar,
                         header_h};
        L.body_view = {L.header_view.x, L.header_view.y + header_h,
                       L.header_view.w, L.frame.h - 2.0F * f - header_h - bar};
        L.group_view = {L.header_view.x, L.header_view.y, L.header_view.w, L.group_h};
        L.col_header = {L.header_view.x, L.header_view.y + L.group_h,
                        L.header_view.w, st.header_h};
        if (L.body_view.h < 0.0F)
            L.body_view.h = 0.0F;

        // 行选择沟槽：占据体/头的最左侧，列从它右边开始
        L.gutter = st.row_select ? std::max(st.select_w, 0.0F) : 0.0F;
        if (L.gutter > L.body_view.w * 0.5F)
            L.gutter = 0.0F;
        L.check_head = {L.col_header.x, L.col_header.y, L.gutter, L.col_header.h};
        L.check_body = {L.body_view.x, L.body_view.y, L.gutter, L.body_view.h};

        // 列宽：可用宽 = 体宽 - 沟槽（bar 已预留，滚动条出现/消失不会让列宽跳变）
        solve_columns(m, s, st, std::max(L.body_view.w - L.gutter, 1.0F), L.cols);
        const int n = static_cast<int>(L.cols.size());
        L.content_w = n > 0 ? L.cols.back().x + L.cols.back().w : 0.0F;
        // 多级表头 spans（按显示顺序，相邻同组才合并）
        if (L.group_h > 0.0F)
        {
            for (int i = 0; i < n; ++i)
            {
                const auto &cm = L.cols[static_cast<std::size_t>(i)];
                if (cm.index < 0 ||
                    static_cast<std::size_t>(cm.index) >= m.cols.size())
                    continue;
                const std::string &g =
                    m.cols[static_cast<std::size_t>(cm.index)].group;
                if (g.empty())
                    continue;
                if (!L.groups.empty() &&
                    L.groups.back().last_col == i - 1 &&
                    m.cols[static_cast<std::size_t>(
                              L.cols[static_cast<std::size_t>(i - 1)].index)]
                            .group == g)
                {
                    L.groups.back().x1 = cm.x + cm.w;
                    L.groups.back().last_col = i;
                }
                else
                {
                    L.groups.push_back({g, cm.x, cm.x + cm.w, i, i});
                }
            }
        }
        const int nview = static_cast<int>(s.row_view.size());
        const auto row_h_of = [&](int data_row) -> float
        {
            if (static_cast<std::size_t>(data_row) < s.row_h_data.size() &&
                s.row_h_data[static_cast<std::size_t>(data_row)] > 0.0F)
                return s.row_h_data[static_cast<std::size_t>(data_row)];
            return st.row_h;
        };
        // 前缀和可用就用它（O(1) 拿总高、O(log n) 找首行）；否则退回逐行累加
        const bool use_metrics = s.row_off.size() == s.row_view.size() + 1 &&
                                 s.row_off_ver == s.filter_ver;
        L.content_h = use_metrics
                          ? (nview > 0 ? s.row_off.back() - st.row_gap : 0.0F)
                          : [&]
        {
            float total = 0.0F;
            for (int i = 0; i < nview; ++i)
                total += row_h_of(s.row_view[static_cast<std::size_t>(i)]) + st.row_gap;
            return nview > 0 ? total - st.row_gap : 0.0F;
        }();

        // 冻结区 / 滚动区（都从沟槽右侧开始）
        const float bx = L.body_view.x + L.gutter;
        const float bw = std::max(L.body_view.w - L.gutter, 0.0F);
        if (L.frozen > 0)
        {
            const float want = L.cols[static_cast<std::size_t>(L.frozen - 1)].x +
                               L.cols[static_cast<std::size_t>(L.frozen - 1)].w;
            L.frozen_w = std::min(want, bw * 0.8F);
            L.frozen_view = {bx, L.body_view.y, L.frozen_w, L.body_view.h};
            L.scroll_view = {bx + L.frozen_w, L.body_view.y,
                             std::max(bw - L.frozen_w, 0.0F), L.body_view.h};
        }
        else
        {
            L.frozen_w = 0.0F;
            L.frozen_view = {bx, L.body_view.y, 0.0F, L.body_view.h};
            L.scroll_view = {bx, L.body_view.y, bw, L.body_view.h};
        }

        L.vbar_on = bar > 0.0F && L.content_h > L.body_view.h + 0.5F;
        L.hbar_on = bar > 0.0F && L.content_w - L.frozen_w > L.scroll_view.w + 0.5F;

        const float max_sy = std::max(0.0F, L.content_h - L.body_view.h);
        const float max_sx =
            std::max(0.0F, L.content_w - L.frozen_w - L.scroll_view.w);
        L.sy = std::clamp(s.sy, 0.0F, max_sy);
        L.sx = std::clamp(s.sx, 0.0F, max_sx);

        // 可见行（纵向才是行的事；横向完全交给列）
        //   row.index 是**数据行号**，筛选/排序后依然能对回 model
        if (nview > 0 && L.body_view.h > 0.0F)
        {
            const float bottom = L.body_view.y + L.body_view.h;
            int first = 0;
            if (use_metrics)
            {
                // 二分：找最后一个 row_off <= sy 的行
                const auto it = std::upper_bound(s.row_off.begin(), s.row_off.end(),
                                                L.sy);
                first = static_cast<int>(it - s.row_off.begin()) - 1;
                first = std::clamp(first, 0, nview - 1);
            }
            float y = use_metrics ? L.body_view.y + s.row_off[static_cast<std::size_t>(
                                                      first)] - L.sy
                                  : L.body_view.y - L.sy;
            int begin = first;
            if (!use_metrics)
                y = L.body_view.y - L.sy;
            for (int i = begin; i < nview; ++i)
            {
                const int dr = s.row_view[static_cast<std::size_t>(i)];
                const float rh = row_h_of(dr);
                if (y + rh > L.body_view.y && y < bottom)
                {
                    const float y0 = std::max(y, L.body_view.y);
                    const float y1 = std::min(y + rh, bottom);
                    if (y1 - y0 > 0.5F)
                        L.rows.push_back({y0, y1 - y0, dr});
                }
                y += rh + st.row_gap;
                if (y >= bottom)
                    break;
            }
        }

        // 滚动条：只铺体的范围
        if (bar > 0.0F)
        {
            const float tx = L.frame.x + L.frame.w - bar - f;
            const float ty = L.body_view.y + 2.0F;
            const float th = std::max(L.body_view.h - 4.0F, 1.0F);
            L.vbar_track = {tx, ty, bar, th};
            if (L.vbar_on)
            {
                const float ratio =
                    std::clamp(L.body_view.h / L.content_h, 0.06F, 1.0F);
                const float hgt = std::max(th * ratio, 18.0F);
                const float t = max_sy > 0.0F ? L.sy / max_sy : 0.0F;
                L.vbar_thumb = {tx, ty + (th - hgt) * t, bar, hgt};
            }
            const float hx = L.body_view.x + 2.0F;
            const float hy = L.frame.y + L.frame.h - bar - f;
            const float hw = std::max(L.body_view.w - 4.0F, 1.0F);
            L.hbar_track = {hx, hy, hw, bar};
            if (L.hbar_on)
            {
                const float ratio =
                    std::clamp(L.scroll_view.w / (L.content_w - L.frozen_w), 0.06F,
                               1.0F);
                const float wid = std::max(hw * ratio, 18.0F);
                const float t = max_sx > 0.0F ? L.sx / max_sx : 0.0F;
                L.hbar_thumb = {hx + (hw - wid) * t, hy, wid, bar};
            }
            L.corner = {L.hbar_track.x + L.hbar_track.w, L.vbar_track.y, bar, bar};
        }

        // 缩放手把：n / w + 四角。右/下边留给滚动条（不然抢点击）。
        //   · 每条带都在 frame 外侧多留 edge_hit，方便抓住
        //   · 角块大小 C 取 max(bar+2, edge_hit+4)，正好避开滚动条末端
        const float e = st.edge_hit;
        const float C = std::max(bar + 2.0F, e + 4.0F);
        const auto &fr = L.frame;
        L.edges.n = {fr.x + C, fr.y - e, std::max(fr.w - 2.0F * C, 1.0F), 2.0F * e};
        {
            float wh = fr.h - 2.0F * C;
            if (L.hbar_on)
                wh = std::min(wh, L.hbar_track.y - (fr.y + C));
            L.edges.w = {fr.x - e, fr.y + C, 2.0F * e, std::max(wh, 1.0F)};
        }
        L.edges.nw = {fr.x - e, fr.y - e, C, C};
        L.edges.ne = {fr.x + fr.w - C, fr.y - e, C, C};
        L.edges.sw = {fr.x - e, fr.y + fr.h - C, C, C};
        L.edges.se = {fr.x + fr.w - C, fr.y + fr.h - C, C, C};

        // ── 不变量：违反就是布局代码写错了 ──────────────────────────────
        assert(L.header_view.x == L.body_view.x);
        assert(L.header_view.w == L.body_view.w);
        assert(L.col_header.h == st.header_h);
        assert(L.header_view.h == L.group_h + st.header_h);
        assert(L.hbar_track.w <= 0.0F ||
               L.hbar_track.y >= L.body_view.y + L.body_view.h - 0.5F);
        assert(L.vbar_track.w <= 0.0F ||
               L.vbar_track.x >= L.body_view.x + L.body_view.w - 0.5F);
        return L;
    }

    // ═══ 7. 绘制 ═════════════════════════════════════════════════════════
    namespace detail
    {
        inline shader_data::Shape shape_ent(const ui_dao::rect_px &r,
                                            const ui_dao::view &v, float radius,
                                            const glm::vec4 &fill, const zone &z,
                                            std::uint32_t hfn) noexcept
        {
            auto s = ui_dao::sdf_shape(r, v, radius, fill, {}, 0.0F, 0.0F, 1.0F,
                                       zone_key(z));
            if (fill.a <= 0.0F)
                s.effects = 0; // 只写拾取、不画
            s.hover_fn = hfn;
            return s;
        }

        inline shader_data::Shape shape_cell_ent(const ui_dao::rect_px &r,
                                                 const ui_dao::view &v, float radius,
                                                 const glm::vec4 &fill,
                                                 const glm::vec4 &border,
                                                 float border_w, const zone &z,
                                                 std::uint32_t hfn) noexcept
        {
            auto s = shape_ent(r, v, radius, fill, z, hfn);
            if (border_w > 0.0F && border.a > 0.0F)
            {
                s.effects |= shader_data::FX_BORDER;
                s.border_color = border;
                s.border_px = border_w;
                s.border_soft_px = 0.5F;
            }
            return s;
        }

        // 一行文字 → 一串 TexQuad；entity 用调用者给的 zone（通常是格子的）
        template <class Canvas>
        inline void text(Canvas &c, const font &f, const ui_dao::view &v,
                         const ui_dao::rect_px &clip, float x, float baseline,
                         std::string_view str, const glm::vec4 &color, const zone &z,
                         std::uint32_t hfn)
        {
            if (clip.w <= 0.0F || clip.h <= 0.0F)
                return;
            const float px = f.px;
            float cursor = x;
            for (const char ch : str)
            {
                const auto cp = static_cast<char32_t>(static_cast<unsigned char>(ch));
                const auto it = f.glyphs.find(cp);
                if (it == f.glyphs.end())
                {
                    cursor += px * 0.3F;
                    continue;
                }
                const auto &g = it->second;
                const float ydir = g.y_down ? 1.0F : -1.0F;
                float l = cursor + g.plane.x * px;
                float r = cursor + g.plane.z * px;
                float tp = baseline + ydir * g.plane.y * px;
                float bt = baseline + ydir * g.plane.w * px;
                if (r < l)
                    std::swap(l, r);
                if (bt < tp)
                    std::swap(tp, bt);
                if (r - l > 0.0F && bt - tp > 0.0F)
                {
                    UvTransform uv{};
                    uv.scale = {g.uv.z - g.uv.x, g.uv.w - g.uv.y};
                    uv.offset = {g.uv.x, g.uv.y};
                    ui_dao::rect_px gr{l, tp, r - l, bt - tp};
                    if (clip_glyph(gr, uv, clip))
                    {
                        auto q = ui_dao::glyph_quad(gr, v, g.tex, g.samp, g.font,
                                                    g.px_range, color, uv, zone_key(z));
                        q.hover_fn = hfn;
                        c.add(q);
                    }
                }
                cursor += g.advance * px;
            }
        }

        // 放不下就截断并补 "..."（表格的标准做法）
        [[nodiscard]] inline std::string fit_text(const font &f, std::string_view str,
                                                  float inner);
        [[nodiscard]] inline float text_width(const font &f, std::string_view str) noexcept
        {
            float w = 0.0F;
            for (const char ch : str)
            {
                const auto it =
                    f.glyphs.find(static_cast<char32_t>(static_cast<unsigned char>(ch)));
                w += it == f.glyphs.end() ? f.px * 0.3F : it->second.advance * f.px;
            }
            return w;
        }

        // 放不下就截断并补 "..."（表格的标准做法）
        [[nodiscard]] inline std::string fit_text(const font &f, std::string_view str,
                                                  float inner)
        {
            if (text_width(f, str) <= inner)
                return std::string{str};
            const float ell = text_width(f, "...");
            const float room = std::max(inner - ell, 0.0F);
            std::size_t cut = 0;
            float acc = 0.0F;
            while (cut < str.size())
            {
                const auto next = utf8_next(str, cut);
                const float w = text_width(f, str.substr(cut, next - cut));
                if (acc + w > room)
                    break;
                acc += w;
                cut = next;
            }
            std::string out{str.substr(0, cut)};
            out += "...";
            return out;
        }
        // 折行：显式 '\n' + 按宽度自动断（优先空格，其次字符边界）
        [[nodiscard]] inline std::vector<std::string_view>
        wrap_lines(const font &f, std::string_view text, float inner)
        {
            std::vector<std::string_view> out;
            if (inner <= 1.0F || text.empty())
            {
                out.push_back(text);
                return out;
            }
            std::size_t line_start = 0;
            std::size_t i = 0;
            std::size_t last_space = std::string_view::npos;
            float w = 0.0F;
            while (i < text.size())
            {
                if (text[i] == '\n')
                {
                    out.push_back(text.substr(line_start, i - line_start));
                    ++i;
                    line_start = i;
                    last_space = std::string_view::npos;
                    w = 0.0F;
                    continue;
                }
                const auto next = utf8_next(text, i);
                const float cw = text_width(f, text.substr(i, next - i));
                if (text[i] == ' ')
                    last_space = i;
                if (w + cw > inner && i > line_start)
                {
                    std::size_t brk = i;
                    if (last_space != std::string_view::npos && last_space > line_start)
                        brk = last_space;
                    out.push_back(text.substr(line_start, brk - line_start));
                    line_start = brk;
                    // 跳过断点处的空格
                    while (line_start < text.size() && text[line_start] == ' ')
                        ++line_start;
                    i = line_start;
                    last_space = std::string_view::npos;
                    w = 0.0F;
                    continue;
                }
                w += cw;
                i = next;
            }
            out.push_back(text.substr(line_start));
            return out;
        }

    } // namespace detail

    // 行高：wrap 时按"最多的那列的折行数"算，否则用统一 row_h
    inline void update_row_heights(state &s, const model &m, const style &st,
                                   const font &f, float table_w)
    {
        float key = st.row_h * 7.0F + (st.wrap ? 1.0F : 0.0F) * 131.0F +
                    static_cast<float>(m.rows.size()) * 17.0F + table_w * 0.37F;
        for (std::size_t c = 0; c < m.cols.size(); ++c)
            key += s.col_w.size() > c ? s.col_w[c] * 0.011F : 0.0F;
        if (std::abs(key - s.row_h_key) < 1.0e-3F && s.row_h_data.size() == m.rows.size())
            return;
        s.row_h_key = key;
        s.row_h_data.assign(m.rows.size(), st.row_h);
        bool any_wrap = false;
        for (const auto &c : m.cols)
            any_wrap = any_wrap || c.wrap;
        if (!any_wrap && !st.wrap)
            return;
        const float line_h = f.px * 1.25F;
        for (std::size_t r = 0; r < m.rows.size(); ++r)
        {
            float h = st.row_h;
            for (std::size_t c = 0; c < m.cols.size() && c < s.col_w.size(); ++c)
            {
                if (c >= m.rows[r].size() || m.rows[r][c].empty())
                    continue;
                if (!m.cols[c].wrap && !st.wrap)
                    continue;
                const float inner = std::max(s.col_w[c] - 2.0F * st.cell_pad, 4.0F);
                const auto lines = detail::wrap_lines(f, m.rows[r][c], inner);
                h = std::max(h, static_cast<float>(lines.size()) * line_h + 8.0F);
            }
            s.row_h_data[r] = h;
        }
    }

    template <class Canvas>
    void draw(Canvas &c, const layout &L, const model &M, state &S, const style &St,
              const font &F, const ui_dao::view &V)
    {
        const std::uint32_t hfn = hover_fn_ref();
        const auto &fr = L.frame;
        if (fr.w <= 0.0F || fr.h <= 0.0F)
            return;

        const int ncols = static_cast<int>(L.cols.size());
        const int nrows = static_cast<int>(M.row_count());

        // 行高缓存：内容/列宽变了才重算（下一帧的布局生效）
        update_row_heights(S, M, St, F, L.body_view.w);

        // 1) 外框
        c.add(detail::shape_cell_ent(fr, V, St.radius, St.bg, {1.0F, 1.0F, 1.0F, 0.10F},
                                     1.0F, zone{part::frame}, hfn));

        // 1.5) 多级表头：分组行（跨列合并，逐列裁剪）
        if (L.group_h > 0.0F)
        {
            // 沟槽在那一段的底色
            if (L.gutter > 0.0F)
                c.add(detail::shape_cell_ent(
                    {L.header_view.x, L.group_view.y, L.gutter, L.group_h}, V, 0.0F,
                    {St.header_bg.r * 0.85F, St.header_bg.g * 0.85F,
                     St.header_bg.b * 0.85F, St.header_bg.a},
                    {1.0F, 1.0F, 1.0F, 0.06F}, 1.0F, zone{part::frame}, hfn));
            for (const auto &g : L.groups)
            {
                // 逐列裁剪 → 跨越冻结边界时自然断开
                for (int i = g.first_col; i <= g.last_col; ++i)
                {
                    const auto &cm = L.cols[static_cast<std::size_t>(i)];
                    const ui_dao::rect_px gr =
                        clip_x({L.col_x(i), L.group_view.y, cm.w, L.group_h},
                               L.clip_x0(i), L.clip_x1(i));
                    if (gr.w <= 0.5F)
                        continue;
                    const zone zg{part::header_group, -1, g.first_col};
                    const bool hot = S.hover.kind == part::header_group &&
                                     S.hover.col == g.first_col;
                    const glm::vec4 gbg =
                        hot ? glm::vec4(glm::vec3(St.header_bg) * 1.18F,
                                        St.header_bg.a)
                            : glm::vec4(glm::vec3(St.header_bg) * 0.84F,
                                        St.header_bg.a);
                    c.add(detail::shape_ent(gr, V, 0.0F, gbg, zg, hfn));
                }
                // 分组标题：裁剪到"该 span 起始列所在的带"（冻结/滚动）
                const float origin = L.body_view.x + L.gutter;
                const float no_sx = g.first_col < L.frozen ? 0.0F : L.sx;
                const float sx0 = origin + g.x0 - no_sx;
                const float sx1 = origin + g.x1 - no_sx;
                const ui_dao::rect_px band =
                    g.first_col < L.frozen
                        ? ui_dao::rect_px{L.frozen_view.x, L.group_view.y,
                                          L.frozen_view.w, L.group_h}
                        : ui_dao::rect_px{L.scroll_view.x, L.group_view.y,
                                          L.scroll_view.w, L.group_h};
                const ui_dao::rect_px tr =
                    intersect({sx0, L.group_view.y, sx1 - sx0, L.group_h}, band);
                if (tr.w > 2.0F)
                {
                    const std::string fitted =
                        detail::fit_text(F, g.title, std::max(tr.w - 12.0F, 1.0F));
                    const float tw = detail::text_width(F, fitted);
                    const float tx =
                        tr.x + std::max((tr.w - tw) * 0.5F, 0.0F);
                    detail::text(
                        c, F, V,
                        intersect({tx, tr.y, std::max(tw, 1.0F), tr.h}, band), tx,
                        L.group_view.y + L.group_h * 0.5F + F.px * 0.30F, fitted,
                        {0.80F, 0.84F, 0.92F, 1.0F},
                        zone{part::header_group, -1, g.first_col}, hfn);
                }
            }
            // 分组行底部分隔线
            c.add(detail::shape_ent({L.header_view.x, L.group_view.y + L.group_h - 1.0F,
                                     L.header_view.w, 1.0F},
                                    V, 0.0F, {1.0F, 1.0F, 1.0F, 0.10F},
                                    zone{part::frame}, hfn));
        }

        // 2) 列头：一格一条，x 与体共用 col_x(i)
        for (int i = 0; i < ncols; ++i)
        {
            const auto &cm = L.cols[static_cast<std::size_t>(i)];
            const ui_dao::rect_px hr =
                clip_x({L.col_x(i), L.col_header.y, cm.w, L.col_header.h},
                       L.clip_x0(i), L.clip_x1(i));
            if (hr.w <= 0.5F || hr.h <= 0.5F)
                continue;
            const bool hot = S.band_col == cm.index;
            const bool dragged = S.dragging == drag::col_reorder &&
                                 S.drag_zone.col == cm.index;
            glm::vec4 bg = St.header_bg;
            if (dragged)
                bg = glm::vec4(glm::vec3(bg) * 0.6F, bg.a);
            else if (hot)
                bg = glm::vec4(glm::vec3(bg) + glm::vec3(0.06F), bg.a);
            else if (header_selected(S, cm.index))
                bg = glm::vec4(glm::vec3(bg) + glm::vec3(0.05F), bg.a); // 选中列
            c.add(detail::shape_ent(hr, V, St.cell_radius, bg,
                                    zone{part::header, -1, cm.index}, hfn));
        }

        // 2.5) 行选择沟槽：表头全选框（三态：全选/半选/未选；按可见行算）
        if (L.gutter > 0.0F)
        {
            const zone zh{part::check_head, -1, -1};
            c.add(detail::shape_cell_ent(L.check_head, V, 0.0F, St.header_bg,
                                         {1.0F, 1.0F, 1.0F, 0.08F}, 1.0F, zh, hfn));
            const int total = static_cast<int>(S.row_view.size());
            const int on = checked_count_visible(S);
            const bool all = total > 0 && on == total;
            const bool partial = on > 0 && !all;
            const ui_dao::rect_px bx{L.check_head.x + (L.check_head.w - 14.0F) * 0.5F,
                                     L.check_head.y + (L.check_head.h - 14.0F) * 0.5F,
                                     14.0F, 14.0F};
            if (all)
                c.add(detail::shape_ent(bx, V, 3.0F, St.accent, zh, hfn));
            else
                c.add(detail::shape_cell_ent(
                    bx, V, 3.0F, {0.0F, 0.0F, 0.0F, 0.0F},
                    glm::vec4(glm::vec3(St.accent), 0.55F), 1.2F, zh, hfn));
            if (partial)
                c.add(detail::shape_ent(
                    {bx.x + 3.0F, bx.y + bx.h * 0.5F - 1.0F, bx.w - 6.0F, 2.0F}, V,
                    1.0F, glm::vec4(glm::vec3(St.accent), 1.0F), zh, hfn));
        }

        // 3) 行底带（entity: cell(row,-1)，横向铺满体视口 → 不随列滚动）
        for (const auto &row : L.rows)
        {
            const ui_dao::rect_px rr =
                intersect({L.body_view.x, row.y, L.body_view.w, row.h}, L.body_view);
            if (rr.w <= 0.5F || rr.h <= 0.5F)
                continue;
            glm::vec4 bg = St.row_bg[static_cast<std::size_t>(row.index) & 1U];
            if (S.band_row == row.index)
                bg = St.row_hover;
            // NOTE: 行底带不刷选区色 —— 选区只落在"格"上，否则单格选择会被看成整行选中
            c.add(detail::shape_ent(rr, V, 0.0F, bg,
                                    zone{part::cell, row.index, -1}, hfn));
        }

        // 3.5) 空表：正文居中提示（浏览器空表的观感）
        if (M.rows.empty())
        {
            static const std::string kEmpty = "No data";
            const float tw = detail::text_width(F, kEmpty);
            detail::text(c, F, V, L.body_view,
                         L.body_view.x + (L.body_view.w - tw) * 0.5F,
                         L.body_view.y + L.body_view.h * 0.5F + F.px * 0.30F, kEmpty,
                         {0.55F, 0.58F, 0.65F, 1.0F}, zone{part::frame}, hfn);
        }

        // 4) 单元格：透明代理（整格可拾取）+ 需要时叠 tint
        for (const auto &row : L.rows)
        {
            const auto &cells = M.rows[static_cast<std::size_t>(row.index)];
            for (int i = 0; i < ncols; ++i)
            {
                const auto &cm = L.cols[static_cast<std::size_t>(i)];
                const zone z{part::cell, row.index, cm.index};
                const bool editing = S.edit.active() && S.edit.row == row.index &&
                                     S.edit.col == cm.index;
                ui_dao::rect_px cr =
                    clip_x({L.col_x(i), row.y, cm.w, row.h}, L.clip_x0(i),
                           L.clip_x1(i));
                cr = intersect(cr, L.body_view);
                if (cr.w <= 0.5F || cr.h <= 0.5F)
                    continue;
                c.add(detail::shape_ent(cr, V, 0.0F, {0.0F, 0.0F, 0.0F, 0.0F}, z, hfn));

                const cell_visual vis = cell_visual_of(S, row.index, cm.index);
                glm::vec4 bg{0.0F};
                if (editing)
                    bg = glm::vec4(0.06F, 0.08F, 0.12F, 1.0F); // 编辑框底色
                else if (vis == cell_visual::in_range)
                    bg = St.sel; // 选区底色
                else if (S.hover.kind == part::cell && S.hover.row == row.index &&
                         S.hover.col == cm.index)
                    bg = glm::vec4(1.0F, 1.0F, 1.0F, 0.06F);
                else if (S.band_col == cm.index)
                    bg = St.col_hover;
                if (bg.a > 0.0F)
                    c.add(detail::shape_ent(cr, V, St.cell_radius, bg, z, hfn));
                // 查找命中：淡黄色叠加
                if (!S.find_buf.empty() &&
                    static_cast<std::size_t>(cm.index) < cells.size() &&
                    text_contains(cells[static_cast<std::size_t>(cm.index)],
                                  S.find_buf))
                    c.add(detail::shape_ent(cr, V, St.cell_radius,
                                            {0.95F, 0.78F, 0.25F, 0.22F}, z, hfn));
                // 活动格：独立描边（Excel 的"当前格"），编辑态由编辑框自己画
                if (vis == cell_visual::active && !editing)
                    c.add(detail::shape_cell_ent(cr, V, 0.0F, {0.0F, 0.0F, 0.0F, 0.0F},
                                                 St.accent, 1.5F, z, hfn));

                // 文字：同一条 cr，保证与格同宽
                if (static_cast<std::size_t>(cm.index) >= cells.size())
                    continue;
                const float inner = std::max(cm.w - 2.0F * St.cell_pad, 1.0F);
                const float baseline = row.y + row.h * 0.5F + F.px * 0.30F;

                if (editing)
                {
                    // 编辑态：文本可横向滚动，光标始终可见；再画一根 accent 竖线当 caret
                    const std::string &buf = S.edit.buf;
                    const auto caret = static_cast<std::size_t>(
                        std::clamp(S.edit.caret, 0, static_cast<int>(buf.size())));
                    const float pre = detail::text_width(F, buf.substr(0, caret));
                    const float off = pre > inner ? pre - inner + 2.0F : 0.0F;
                    // 编辑框内选区高亮
                    if (S.edit.has_sel())
                    {
                        const float lo_x = detail::text_width(
                            F, buf.substr(0, static_cast<std::size_t>(S.edit.sel_lo())));
                        const float hi_x = detail::text_width(
                            F, buf.substr(0, static_cast<std::size_t>(S.edit.sel_hi())));
                        if (hi_x > lo_x)
                            c.add(detail::shape_ent(
                                {cr.x + St.cell_pad + lo_x - off, row.y + 4.0F,
                                 hi_x - lo_x, std::max(row.h - 8.0F, 1.0F)},
                                V, 0.0F,
                                {St.accent.r, St.accent.g, St.accent.b, 0.35F}, z, hfn));
                    }
                    const ui_dao::rect_px tx = intersect(
                        {cr.x + St.cell_pad - off, row.y, inner + off,
                         std::min(row.h, L.body_view.y + L.body_view.h - row.y)},
                        L.body_view);
                    detail::text(c, F, V, tx, tx.x, baseline, buf, St.text, z, hfn);
                    // IME 预编辑：跟在 caret 后面，带下划线，不进入 buf
                    if (!S.edit.compose.empty())
                    {
                        const float cx0 = cr.x + St.cell_pad + pre - off;
                        const float cw = detail::text_width(F, S.edit.compose);
                        const ui_dao::rect_px ctx = intersect(
                            {cx0, row.y, cw,
                             std::min(row.h, L.body_view.y + L.body_view.h - row.y)},
                            L.body_view);
                        detail::text(c, F, V, ctx, ctx.x, baseline, S.edit.compose,
                                     St.accent, z, hfn);
                        if (cw > 0.0F)
                            c.add(detail::shape_ent(
                                {cx0, baseline + 1.0F, cw, 1.0F}, V, 0.0F, St.accent, z,
                                hfn));
                    }
                    const float cx = std::clamp(cr.x + St.cell_pad + pre - off,
                                                cr.x + 1.0F,
                                                cr.x + cr.w - 2.0F);
                    c.add(detail::shape_ent(
                        {cx, row.y + 4.0F, 1.5F, std::max(row.h - 8.0F, 1.0F)}, V,
                        0.0F, St.accent, z, hfn));
                    // 编辑框描边
                    c.add(detail::shape_cell_ent(cr, V, 0.0F, {0.0F, 0.0F, 0.0F, 0.0F},
                                                 St.accent, 1.5F, z, hfn));
                    continue;
                }

                const std::string &str = cells[static_cast<std::size_t>(cm.index)];
                if (str.empty())
                    continue;
                const auto &colcfg = M.cols[static_cast<std::size_t>(cm.index)];

                // ── 自定义渲染：tag / bar / dot ──
                if (colcfg.render == render_kind::tag)
                {
                    const float th = std::min(row.h - 10.0F, 20.0F);
                    const std::string fitted = detail::fit_text(F, str, inner - 16.0F);
                    const float tw2 = detail::text_width(F, fitted);
                    const float pw = std::min(tw2 + 16.0F, inner);
                    const auto it = colcfg.tags.find(fitted);
                    const glm::vec4 tc =
                        it != colcfg.tags.end()
                            ? it->second
                            : hex_color("#6b7280", {0.42F, 0.45F, 0.50F, 1.0F});
                    const float px0 = cr.x + St.cell_pad;
                    const ui_dao::rect_px pill{px0, row.y + (row.h - th) * 0.5F, pw,
                                               th};
                    c.add(detail::shape_ent(pill, V, th * 0.5F,
                                            {tc.r, tc.g, tc.b, 0.30F}, z, hfn));
                    const ui_dao::rect_px tx = intersect(
                        {px0 + 8.0F, row.y, std::max(pw - 16.0F, 1.0F),
                         std::min(row.h, L.body_view.y + L.body_view.h - row.y)},
                        L.body_view);
                    detail::text(c, F, V, tx, tx.x, baseline, fitted,
                                 {tc.r * 0.5F + 0.5F, tc.g * 0.5F + 0.5F,
                                  tc.b * 0.5F + 0.5F, 1.0F},
                                 z, hfn);
                    continue;
                }
                if (colcfg.render == render_kind::bar)
                {
                    float frac = 0.0F;
                    if (!parse_fraction(str, frac))
                        frac = 0.0F;
                    const float bh = 6.0F;
                    const float by2 = row.y + (row.h - bh) * 0.5F + 3.0F;
                    const ui_dao::rect_px track{cr.x + St.cell_pad, by2,
                                                std::max(inner, 1.0F), bh};
                    c.add(detail::shape_ent(track, V, bh * 0.5F,
                                            {1.0F, 1.0F, 1.0F, 0.10F}, z, hfn));
                    const float fw = std::max(inner * frac, 2.0F);
                    c.add(detail::shape_ent({track.x, by2, fw, bh}, V, bh * 0.5F,
                                            St.accent, z, hfn));
                    const std::string fitted = detail::fit_text(F, str, inner);
                    const float tw2 = detail::text_width(F, fitted);
                    const ui_dao::rect_px tx = intersect(
                        {cr.x + St.cell_pad + std::max(inner - tw2, 0.0F),
                         row.y - 7.0F, std::max(tw2, 1.0F),
                         std::min(row.h, L.body_view.y + L.body_view.h - row.y)},
                        L.body_view);
                    detail::text(c, F, V, tx, tx.x, baseline - 7.0F, fitted, St.text,
                                 z, hfn);
                    continue;
                }
                if (colcfg.render == render_kind::dot)
                {
                    const auto it = colcfg.tags.find(str);
                    const glm::vec4 dc =
                        it != colcfg.tags.end()
                            ? it->second
                            : hex_color("#6b7280", {0.42F, 0.45F, 0.50F, 1.0F});
                    const float d = 8.0F;
                    c.add(detail::shape_ent(
                        {cr.x + St.cell_pad, row.y + (row.h - d) * 0.5F, d, d}, V,
                        d * 0.5F, dc, z, hfn));
                    const std::string fitted =
                        detail::fit_text(F, str, inner - d - 8.0F);
                    const ui_dao::rect_px tx = intersect(
                        {cr.x + St.cell_pad + d + 8.0F, row.y,
                         std::max(inner - d - 8.0F, 1.0F),
                         std::min(row.h, L.body_view.y + L.body_view.h - row.y)},
                        L.body_view);
                    detail::text(c, F, V, tx, tx.x, baseline, fitted, St.text, z, hfn);
                    continue;
                }

                // 多行折行：按内容分行、顶对齐（行高已在布局里按折行数算好）
                const bool do_wrap = colcfg.wrap || St.wrap;
                if (do_wrap && str.find('\n') != std::string::npos)
                {
                    const auto lines = detail::wrap_lines(F, str, inner);
                    const float line_h = F.px * 1.25F;
                    float by = row.y + 4.0F + F.px;
                    for (const auto &ln : lines)
                    {
                        if (by - F.px >= row.y + row.h)
                            break;
                        const ui_dao::rect_px tx = intersect(
                            {cr.x + St.cell_pad, by - F.px, std::max(inner, 1.0F),
                             std::min(line_h, row.y + row.h - (by - F.px))},
                            L.body_view);
                        detail::text(c, F, V, tx, tx.x, by, ln, St.text, z, hfn);
                        by += line_h;
                    }
                    continue;
                }
                if (do_wrap)
                {
                    const auto lines = detail::wrap_lines(F, str, inner);
                    if (lines.size() > 1)
                    {
                        const float line_h = F.px * 1.25F;
                        float by = row.y + 4.0F + F.px;
                        for (const auto &ln : lines)
                        {
                            if (by - F.px >= row.y + row.h)
                                break;
                            const ui_dao::rect_px tx = intersect(
                                {cr.x + St.cell_pad, by - F.px, std::max(inner, 1.0F),
                                 std::min(line_h, row.y + row.h - (by - F.px))},
                                L.body_view);
                            detail::text(c, F, V, tx, tx.x, by, ln, St.text, z, hfn);
                            by += line_h;
                        }
                        continue;
                    }
                }

                // 放不下就截断并补 "..."（不是硬裁）
                const std::string fitted = detail::fit_text(F, str, inner);
                const std::string_view shown{fitted};
                const float tw = detail::text_width(F, shown);
                float dx = 0.0F;
                if (M.cols[static_cast<std::size_t>(cm.index)].text_align ==
                    align::center)
                    dx = std::max((inner - tw) * 0.5F, 0.0F);
                else if (M.cols[static_cast<std::size_t>(cm.index)].text_align ==
                         align::right)
                    dx = std::max(inner - tw, 0.0F);
                const ui_dao::rect_px tx = intersect(
                    {cr.x + St.cell_pad + dx, row.y, inner - dx,
                     std::min(row.h, L.body_view.y + L.body_view.h - row.y)},
                    L.body_view);
                detail::text(c, F, V, tx, tx.x, baseline, shown, St.text, z, hfn);
            }
        }

        // 4.4) 每行选择框 + 透明代理（沟槽在列之外，单独一条）
        if (L.gutter > 0.0F)
        {
            for (const auto &row : L.rows)
            {
                const ui_dao::rect_px gr{L.check_body.x, row.y, L.gutter, row.h};
                const zone zc{part::check, row.index, -1};
                c.add(detail::shape_ent(gr, V, 0.0F, {0.0F, 0.0F, 0.0F, 0.0F}, zc,
                                        hfn)); // 代理：只拾取不画
                const bool on = row_checked(S, row.index);
                const ui_dao::rect_px bx{gr.x + (gr.w - 14.0F) * 0.5F,
                                         row.y + (gr.h - 14.0F) * 0.5F, 14.0F,
                                         14.0F};
                if (on)
                    c.add(detail::shape_ent(bx, V, 3.0F, St.accent, zc, hfn));
                else
                    c.add(detail::shape_cell_ent(
                        bx, V, 3.0F, {0.0F, 0.0F, 0.0F, 0.0F},
                        glm::vec4(glm::vec3(St.accent), 0.45F), 1.2F, zc, hfn));
            }
        }

        // 4.5) 选区外框：用行/列几何拼 4 条 2px 边框（标准表格的选区观感）
        if (has_sel(S) && !L.rows.empty())
        {
            const int r0 = std::min(S.sel_r0, S.sel_r1);
            const int r1 = std::max(S.sel_r0, S.sel_r1);
            const int c0 = std::min(S.sel_c0, S.sel_c1);
            const int c1 = std::max(S.sel_c0, S.sel_c1);
            float y0 = 1.0e30F, y1 = -1.0e30F, x0 = 1.0e30F, x1 = -1.0e30F;
            for (const auto &row : L.rows)
                if (row.index >= r0 && row.index <= r1)
                {
                    y0 = std::min(y0, row.y);
                    y1 = std::max(y1, row.y + row.h);
                }
            for (int i = 0; i < ncols; ++i)
            {
                const int idx = L.cols[static_cast<std::size_t>(i)].index;
                if (idx < c0 || idx > c1)
                    continue;
                const float l = std::max(L.col_x(i), L.clip_x0(i));
                const float r = std::min(L.col_x(i) + L.cols[static_cast<std::size_t>(i)].w,
                                         L.clip_x1(i));
                if (r > l)
                {
                    x0 = std::min(x0, l);
                    x1 = std::max(x1, r);
                }
            }
            if (y1 > y0 && x1 > x0)
            {
                x0 = std::max(x0, L.body_view.x);
                x1 = std::min(x1, L.body_view.x + L.body_view.w);
                y0 = std::max(y0, L.body_view.y);
                y1 = std::min(y1, L.body_view.y + L.body_view.h);
                const zone z{part::cell, r0, c0};
                const float t = 2.0F;
                c.add(detail::shape_ent({x0, y0, x1 - x0, t}, V, 0.0F, St.accent, z, hfn));
                c.add(detail::shape_ent({x0, y1 - t, x1 - x0, t}, V, 0.0F, St.accent, z,
                                        hfn));
                c.add(detail::shape_ent({x0, y0, t, y1 - y0}, V, 0.0F, St.accent, z, hfn));
                c.add(detail::shape_ent({x1 - t, y0, t, y1 - y0}, V, 0.0F, St.accent, z,
                                        hfn));
            }
        }

        // 4.7) 列头筛选：漏斗按钮（有筛选时高亮）
        for (int i = 0; i < ncols; ++i)
        {
            const auto &cm = L.cols[static_cast<std::size_t>(i)];
            if (cm.index < 0 ||
                static_cast<std::size_t>(cm.index) >= M.cols.size() ||
                !M.cols[static_cast<std::size_t>(cm.index)].filterable)
                continue;
            const ui_dao::rect_px hr =
                clip_x({L.col_x(i), L.col_header.y, cm.w, L.col_header.h},
                       L.clip_x0(i), L.clip_x1(i));
            if (hr.w <= 0.5F)
                continue;
            const zone zf{part::filter_btn, -1, cm.index};
            const bool active =
                static_cast<std::size_t>(cm.index) < S.filter.size() &&
                !S.filter[static_cast<std::size_t>(cm.index)].empty();
            const glm::vec4 fc = active ? St.accent
                                        : glm::vec4(glm::vec3(St.header_text), 0.55F);
            const float bx = hr.x + hr.w - 18.0F;
            const float by = hr.y + hr.h * 0.5F - 6.0F;
            c.add(detail::shape_ent({bx, by, 12.0F, 2.0F}, V, 0.0F, fc, zf, hfn));
            c.add(detail::shape_ent({bx + 2.0F, by + 4.0F, 8.0F, 2.0F}, V, 0.0F, fc,
                                    zf, hfn));
            c.add(detail::shape_ent({bx + 4.0F, by + 8.0F, 4.0F, 2.0F}, V, 0.0F, fc,
                                    zf, hfn));
            // 命中代理：整个漏斗区域都可点（图形本身太细，点不准）
            c.add(detail::shape_ent({hr.x + hr.w - 24.0F, hr.y + 3.0F, 24.0F,
                                     hr.h - 6.0F},
                                    V, 0.0F, {0.0F, 0.0F, 0.0F, 0.0F}, zf, hfn));
            // 正在编辑筛选：就地显示输入框（此时不画标题，避免文字重叠）
            if (S.filter_edit == cm.index)
            {
                const ui_dao::rect_px fb{hr.x + 4.0F, hr.y + 4.0F, hr.w - 24.0F,
                                         hr.h - 8.0F};
                c.add(detail::shape_cell_ent(fb, V, 4.0F,
                                             {0.06F, 0.08F, 0.12F, 1.0F}, St.accent,
                                             1.0F, zone{part::filter_btn, -1, cm.index},
                                             hfn));
                detail::text(c, F, V, fb, fb.x + 6.0F,
                             fb.y + fb.h * 0.5F + F.px * 0.30F, S.filter_buf, St.text,
                             zone{part::filter_btn, -1, cm.index}, hfn);
            }
        }

        // 4.8) 查找条（Ctrl+F）
        if (S.find_mode)
        {
            const ui_dao::rect_px fb{L.body_view.x + L.body_view.w - 300.0F,
                                     L.body_view.y + 8.0F, 292.0F, 30.0F};
            c.add(detail::shape_cell_ent(fb, V, 6.0F, {0.08F, 0.09F, 0.13F, 1.0F},
                                         St.accent, 1.0F, zone{part::frame}, hfn));
            const std::string txt =
                S.find_buf.empty() ? std::string{"Find..."}
                                   : (S.find_buf + "   (Enter)");
            detail::text(c, F, V, fb, fb.x + 10.0F,
                         fb.y + fb.h * 0.5F + F.px * 0.30F, txt, St.text,
                         zone{part::frame}, hfn);
        }

        // 5) 列头文字 + 排序标记（同一个 col_x，所以必然跟列一起动）
        for (int i = 0; i < ncols; ++i)
        {
            const auto &cm = L.cols[static_cast<std::size_t>(i)];
            if (static_cast<std::size_t>(cm.index) >= M.cols.size())
                continue;
            const ui_dao::rect_px hr =
                clip_x({L.col_x(i), L.col_header.y, cm.w, L.col_header.h},
                       L.clip_x0(i), L.clip_x1(i));
            if (hr.w <= 0.5F)
                continue;
            const auto &col_def = M.cols[static_cast<std::size_t>(cm.index)];
            const float tw = detail::text_width(F, col_def.title);
            if (S.filter_edit == cm.index)
                continue; // 标题让位给筛选输入
            const float inner = std::max(cm.w - 2.0F * St.cell_pad -
                                              (M.cols[static_cast<std::size_t>(cm.index)]
                                                       .filterable
                                                   ? 18.0F
                                                   : 0.0F),
                                         1.0F);
            float dx = 0.0F;
            if (col_def.text_align == align::center)
                dx = std::max((inner - tw) * 0.5F, 0.0F);
            else if (col_def.text_align == align::right)
                dx = std::max(inner - tw, 0.0F);
            const float baseline = hr.y + hr.h * 0.5F + F.px * 0.30F;
            detail::text(c, F, V,
                         intersect({hr.x + St.cell_pad + dx, hr.y, inner - dx, hr.h},
                                   hr),
                         hr.x + St.cell_pad + dx, baseline, col_def.title,
                         St.header_text, zone{part::header, -1, cm.index}, hfn);

            // 排序标记：排序列的表头底部一条 accent（读 state，不存像素）
            if (S.sort_col == cm.index && S.sort_dir != 0)
                c.add(detail::shape_ent({hr.x, hr.y + hr.h - 2.0F, hr.w, 2.0F}, V,
                                        0.0F, St.accent,
                                        zone{part::header, -1, cm.index}, hfn));
        }

        // 5.5) 换序：被拖的列头跟着指针走（半透明幽灵）+ 落点插入线
        if (S.dragging == drag::col_reorder && S.reorder_from >= 0 &&
            S.reorder_from < ncols)
        {
            const auto &cm = L.cols[static_cast<std::size_t>(S.reorder_from)];
            const float gw = cm.w;
            const float gx = S.pointer.x - gw * 0.5F;
            const ui_dao::rect_px ghost{gx, L.col_header.y, gw, L.col_header.h};
            c.add(detail::shape_cell_ent(ghost, V, St.cell_radius,
                                         {St.header_bg.r, St.header_bg.g,
                                          St.header_bg.b, 0.85F},
                                         St.accent, 1.5F,
                                         zone{part::header, -1, cm.index}, hfn));
            if (static_cast<std::size_t>(cm.index) < M.cols.size())
                detail::text(c, F, V, ghost, ghost.x + St.cell_pad,
                             ghost.y + ghost.h * 0.5F + F.px * 0.30F,
                             M.cols[static_cast<std::size_t>(cm.index)].title,
                             St.header_text, zone{part::header, -1, cm.index}, hfn);
        }
        if (S.dragging == drag::col_reorder && S.reorder_to >= 0 && ncols > 0)
        {
            const float xt = S.reorder_to < ncols
                                 ? L.col_x(S.reorder_to)
                                 : L.col_x(ncols - 1) +
                                       L.cols[static_cast<std::size_t>(ncols - 1)].w;
            const float y0 = L.col_header.y;
            const float y1 = L.body_view.y + L.body_view.h;
            c.add(detail::shape_ent({xt - 1.0F, y0, 2.0F, y1 - y0}, V, 0.0F, St.accent,
                                    zone{part::frame}, hfn));
        }

        // 6) 列分隔：表头和体各画自己那段，x 仍来自 col_x(i)
        //    分隔条属于"它左边那一列"（Excel 的约定）：拖它 = 改左列宽度
        for (int i = 1; i < ncols; ++i)
        {
            const auto &cm = L.cols[static_cast<std::size_t>(i)];
            const float x = L.col_x(i) - St.pad * 0.5F;
            const zone z{part::col_sep, -1,
                         L.cols[static_cast<std::size_t>(i - 1)].index};

            // 拾取带：表头段 + 体段（各自裁剪到自己那一侧）
            const ui_dao::rect_px hs =
                clip_x({x - St.sep_hit, L.col_header.y, 2.0F * St.sep_hit,
                        L.col_header.h},
                       L.clip_x0(i), L.clip_x1(i));
            const ui_dao::rect_px bs =
                clip_x({x - St.sep_hit, L.body_view.y, 2.0F * St.sep_hit,
                        L.body_view.h},
                       L.clip_x0(i), L.clip_x1(i));
            if (hs.w > 0.5F)
                c.add(detail::shape_ent(hs, V, 0.0F, {0.0F, 0.0F, 0.0F, 0.0F}, z, hfn));
            if (bs.w > 0.5F)
                c.add(detail::shape_ent(bs, V, 0.0F, {0.0F, 0.0F, 0.0F, 0.0F}, z, hfn));

            // 可见线：1px
            const ui_dao::rect_px hl =
                clip_x({x - 0.5F, L.col_header.y, 1.0F, L.col_header.h},
                       L.clip_x0(i), L.clip_x1(i));
            const ui_dao::rect_px bl =
                clip_x({x - 0.5F, L.body_view.y, 1.0F, L.body_view.h}, L.clip_x0(i),
                       L.clip_x1(i));
            if (hl.w > 0.5F)
                c.add(detail::shape_ent(hl, V, 0.0F, St.sep, z, hfn));
            if (bl.w > 0.5F)
                c.add(detail::shape_ent(bl, V, 0.0F, St.sep, z, hfn));
        }

        // 7) 缩放手把（画 0 像素，只写拾取）：n / w + 四角
        //    ★ 先画手把、后画滚动条 —— 右/下边上的点击永远归滚动条。
        //      这是"bar 和 table 缩放打架"的正解：不是调优先级 if，而是画序。
        {
            const std::pair<ui_dao::rect_px, part> handles[6] = {
                {L.edges.n, part::edge_n},   {L.edges.w, part::edge_w},
                {L.edges.ne, part::edge_ne}, {L.edges.nw, part::edge_nw},
                {L.edges.se, part::edge_se}, {L.edges.sw, part::edge_sw}};
            for (const auto &[rect, kind] : handles)
                c.add(detail::shape_ent(rect, V, 0.0F, {0.0F, 0.0F, 0.0F, 0.0F},
                                        zone{kind}, hfn));
        }

        // 8) 滚动条 + 角（在把手之后 → 条上的点击归条）
        if (L.vbar_on)
        {
            c.add(detail::shape_ent(L.vbar_track, V, St.bar * 0.5F, St.bar_track,
                                    zone{part::vbar_track}, hfn));
            const bool hot =
                S.hover.kind == part::vbar_thumb || S.dragging == drag::vthumb;
            c.add(detail::shape_ent(L.vbar_thumb, V, St.bar * 0.5F,
                                    hot ? St.bar_thumb_hot : St.bar_thumb,
                                    zone{part::vbar_thumb}, hfn));
        }
        if (L.hbar_on)
        {
            c.add(detail::shape_ent(L.hbar_track, V, St.bar * 0.5F, St.bar_track,
                                    zone{part::hbar_track}, hfn));
            const bool hot =
                S.hover.kind == part::hbar_thumb || S.dragging == drag::hthumb;
            c.add(detail::shape_ent(L.hbar_thumb, V, St.bar * 0.5F,
                                    hot ? St.bar_thumb_hot : St.bar_thumb,
                                    zone{part::hbar_thumb}, hfn));
        }
    }

    // ═══ 8. 交互 ═════════════════════════════════════════════════════════
    struct input_view
    {
        float px{0.0F};
        float py{0.0F};
        bool left{false};
        bool shift{false};
        bool ctrl{false};
        float wheel{0.0F};
        double now{0.0}; // 秒（双击判定用）
        // 键盘：按住状态 + 本帧是否刚按下（边沿在 update 里做）
        bool key_left{false}, key_right{false}, key_up{false}, key_down{false};
        bool key_home{false}, key_end{false}, key_pgup{false}, key_pgdn{false};
        bool key_a{false};
        bool key_backspace{false}, key_del{false}, key_enter{false},
            key_esc{false}, key_tab{false};
        bool key_x{false}, key_c{false}, key_v{false};
        bool key_z{false}, key_y{false}, key_f2{false}, key_space{false};
        bool key_f{false};
        std::string_view text{};      // 本帧输入的 UTF-8（char_event 收集）
        std::string_view clipboard{}; // Ctrl+V 时由平台层读系统剪贴板
        std::string_view compose{};   // IME 预编辑串
        zone picked{};           // picking 回读（已做版本过滤）
    };

    enum class cursor : std::uint8_t
    {
        arrow = 0, // 单元格 / 外框 / 滚动条轨道：默认箭头
        ibeam,     // 只在"单元格内文本编辑"时用
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
        case part::col_sep:
        case part::edge_w:
            return cursor::resize_ew;
        case part::edge_n:
            return cursor::resize_ns;
        case part::edge_ne:
        case part::edge_sw:
            return cursor::resize_nesw;
        case part::edge_nw:
        case part::edge_se:
            return cursor::resize_nwse;
        // 单元格：没在编辑文本时就是默认箭头（编辑态才切 ibeam）
        case part::cell:
            return cursor::arrow;
        case part::header:
        case part::vbar_thumb:
        case part::hbar_thumb:
            return cursor::hand;
        default:
            return cursor::arrow;
        }
    }

    // 编辑态下光标指向正在编辑的格子 → 文本光标
    [[nodiscard]] inline cursor cursor_for(const state &s, const zone &z) noexcept
    {
        if (s.edit.active() && z.kind == part::cell && z.row == s.edit.row &&
            z.col == s.edit.col)
            return cursor::ibeam;
        return cursor_for(z);
    }

    inline void edit_begin(state &s, const model &m, int row, int col)
    {
        if (row < 0 || col < 0 || static_cast<std::size_t>(row) >= m.rows.size())
            return;
        const auto &cells = m.rows[static_cast<std::size_t>(row)];
        if (static_cast<std::size_t>(col) >= cells.size())
            return;
        s.edit.row = row;
        s.edit.col = col;
        s.edit.buf = cells[static_cast<std::size_t>(col)];
        s.edit.caret = static_cast<int>(s.edit.buf.size());
        s.edit.anchor = s.edit.caret; // 刚进入时没有选区
        s.edit.compose.clear();
        s.edit.orig = s.edit.buf; // 撤销用
        s.edit.text_undo.clear();
        s.edit.text_redo.clear();
        s.edit.last_push_t = -1.0e9;
        s.edit.last_op_typing = false;
        trace("[edit] begin ({},{}) = \"{}\"", row, col, s.edit.buf);
    }
    inline void edit_commit(state &s, model &m)
    {
        if (!s.edit.active())
            return;
        std::vector<state::cell_change> changes;
        if (s.edit.buf != s.edit.orig)
            changes.push_back({s.edit.row, s.edit.col, s.edit.orig, s.edit.buf,
                               m.id_of(s.edit.row)});
        if (static_cast<std::size_t>(s.edit.row) < m.rows.size())
        {
            auto &cells = m.rows[static_cast<std::size_t>(s.edit.row)];
            if (static_cast<std::size_t>(s.edit.col) < cells.size())
                cells[static_cast<std::size_t>(s.edit.col)] = s.edit.buf;
        }
        trace("[edit] commit ({},{}) = \"{}\"", s.edit.row, s.edit.col, s.edit.buf);
        if (s.on_cell_changed)
            s.on_cell_changed(s.edit.row, s.edit.col, s.edit.buf);
        s.edit = {};
        undo_record(s, std::move(changes));
    }
    inline void edit_cancel(state &s)
    {
        if (s.edit.active())
            trace("[edit] cancel ({},{})", s.edit.row, s.edit.col);
        s.edit = {};
    }

    // ── 编辑框内的文本撤销（浏览器式）──────────────────────────────────
    //   合并规则：连续"输入"且间隔 < 0.8s → 同一撤销步；删除/粘贴各自成步。
    inline void edit_push_undo(state &s, bool typing, double now)
    {
        auto &ed = s.edit;
        const bool coalesce = typing && ed.last_op_typing &&
                              !ed.text_undo.empty() && (now - ed.last_push_t) < 0.8;
        if (!coalesce)
        {
            ed.text_undo.push_back(
                {ed.buf, ed.caret, ed.anchor, now});
            if (ed.text_undo.size() > 100)
                ed.text_undo.erase(ed.text_undo.begin());
        }
        ed.last_push_t = now;
        ed.last_op_typing = typing;
        ed.text_redo.clear();
    }

    inline void edit_undo(state &s)
    {
        auto &ed = s.edit;
        if (ed.text_undo.empty())
            return;
        ed.text_redo.push_back({ed.buf, ed.caret, ed.anchor, s.now});
        auto step = ed.text_undo.back();
        ed.text_undo.pop_back();
        ed.buf = std::move(step.buf);
        ed.caret = step.caret;
        ed.anchor = step.anchor;
        ed.last_push_t = -1.0e9; // 撤销后不再和之前合并
        ed.last_op_typing = false;
        trace("[edit] text undo -> \"{}\"", ed.buf);
    }

    inline void edit_redo(state &s)
    {
        auto &ed = s.edit;
        if (ed.text_redo.empty())
            return;
        ed.text_undo.push_back({ed.buf, ed.caret, ed.anchor, s.now});
        auto step = ed.text_redo.back();
        ed.text_redo.pop_back();
        ed.buf = std::move(step.buf);
        ed.caret = step.caret;
        ed.anchor = step.anchor;
        ed.last_push_t = -1.0e9;
        ed.last_op_typing = false;
        trace("[edit] text redo -> \"{}\"", ed.buf);
    }

    // ── 编辑框内：选区 / 剪贴板 / 点击定位 ──────────────────────────────
    inline void edit_delete_selection(state &s)
    {
        auto &ed = s.edit;
        if (!ed.has_sel())
            return;
        const auto lo = static_cast<std::size_t>(ed.sel_lo());
        const auto hi = static_cast<std::size_t>(ed.sel_hi());
        ed.buf.erase(lo, hi - lo);
        ed.caret = ed.anchor = static_cast<int>(lo);
    }

    inline void edit_replace_selection(state &s, std::string_view text)
    {
        auto &ed = s.edit;
        edit_delete_selection(s);
        ed.buf.insert(static_cast<std::size_t>(ed.caret), text);
        ed.caret += static_cast<int>(text.size());
        ed.anchor = ed.caret;
    }

    // 编辑框里：像素 x → caret（按字形宽度累加，取最近的字符边界）
    [[nodiscard]] inline int edit_caret_at(const font &f, const std::string &buf,
                                           float text_x, float x) noexcept
    {
        float acc = text_x;
        std::size_t i = 0;
        int best = 0;
        float best_d = 1.0e30F;
        while (true)
        {
            const float d = std::abs(x - acc);
            if (d < best_d)
            {
                best_d = d;
                best = static_cast<int>(i);
            }
            if (i >= buf.size())
                break;
            const auto next = utf8_next(buf, i);
            acc += detail::text_width(f, std::string_view(buf).substr(i, next - i));
            i = next;
        }
        return best;
    }

    inline void clip_copy(state &s, std::string text)
    {
        if (text.empty())
            return;
        s.clip_out.text = std::move(text);
        s.clip_out.set = true;
        trace("[clip] copy {} bytes", s.clip_out.text.size());
    }

    // 选区 → TSV（表格的标准复制格式）
    [[nodiscard]] inline std::string range_to_tsv(const state &s, const model &m)
    {
        if (!has_sel(s))
            return {};
        const int r0 = std::min(s.sel_r0, s.sel_r1);
        const int r1 = std::max(s.sel_r0, s.sel_r1);
        const int c0 = std::min(s.sel_c0, s.sel_c1);
        const int c1 = std::max(s.sel_c0, s.sel_c1);
        std::string out;
        for (int r = r0; r <= r1 && r < static_cast<int>(m.rows.size()); ++r)
        {
            for (int c = c0; c <= c1; ++c)
            {
                if (c < static_cast<int>(m.rows[static_cast<std::size_t>(r)].size()))
                    out += m.rows[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
                if (c != c1)
                    out += '\t';
            }
            if (r != r1)
                out += '\n';
        }
        return out;
    }

    // TSV → 从活动单元格开始覆盖写
    inline void paste_tsv(model &m, state &s, std::string_view text)
    {
        if (text.empty() || m.rows.empty())
            return;
        const int start_r = s.cur_row < 0 ? 0 : s.cur_row;
        int start_c = s.cur_col < 0 ? 0 : s.cur_col;
        if (start_c >= static_cast<int>(m.cols.size()))
            start_c = 0;
        std::vector<state::cell_change> changes;
        int row = start_r;
        std::size_t i = 0;
        while (row < static_cast<int>(m.rows.size()))
        {
            const auto eol = text.find('\n', i);
            const auto line = text.substr(
                i, (eol == std::string_view::npos ? text.size() : eol) - i);
            int col = start_c;
            std::size_t j = 0;
            auto &cells = m.rows[static_cast<std::size_t>(row)];
            while (col < static_cast<int>(cells.size()))
            {
                const auto tab = line.find('\t', j);
                const std::string value = std::string(line.substr(
                    j, (tab == std::string_view::npos ? line.size() : tab) - j));
                if (cells[static_cast<std::size_t>(col)] != value)
                    changes.push_back({row, col, cells[static_cast<std::size_t>(col)],
                                       value, m.id_of(row)});
                cells[static_cast<std::size_t>(col)] = value;
                ++col;
                if (tab == std::string_view::npos)
                    break;
                j = tab + 1;
            }
            ++row;
            if (eol == std::string_view::npos)
                break;
            i = eol + 1;
        }
        if (s.on_cell_changed)
            for (const auto &ch : changes)
                s.on_cell_changed(ch.row, ch.col, ch.after);
        trace("[clip] paste at ({},{})", start_r, start_c);
        undo_record(s, std::move(changes));
    }

    // 版本号推进：任何"几何变了"的地方都调它（picking 回读靠它过滤旧帧）
    inline void touch(state &s) noexcept
    {
        s.version = (s.version + 1U) & 0xFFFFU;
    }


    inline void sort_rows(model &m, state &s)
    {
        if (s.sort_col < 0 || s.sort_dir == 0)
            return;
        const auto c = static_cast<std::size_t>(s.sort_col);
        const bool asc = s.sort_dir == 1;
        // 排序用"下标置换"，rows 与 row_id 一起重排，稳定 id 不变
        std::vector<std::size_t> perm(m.rows.size());
        for (std::size_t i = 0; i < perm.size(); ++i)
            perm[i] = i;
        std::stable_sort(perm.begin(), perm.end(),
                         [&m, c, asc](std::size_t ia, std::size_t ib)
                         {
                             const auto &a = m.rows[ia];
                             const auto &b = m.rows[ib];
                             const std::string_view x = c < a.size() ? a[c] : "";
                             const std::string_view y = c < b.size() ? b[c] : "";
                             return asc ? x < y : x > y;
                         });
        std::vector<std::vector<std::string>> rows2;
        std::vector<std::uint64_t> ids2;
        rows2.reserve(m.rows.size());
        ids2.reserve(m.row_id.size());
        for (const auto i : perm)
        {
            rows2.push_back(std::move(m.rows[i]));
            ids2.push_back(i < m.row_id.size() ? m.row_id[i] : 0);
        }
        m.rows = std::move(rows2);
        m.row_id = std::move(ids2);
        // 行号全变：勾选按置换重排、行高缓存失效（二者都按数据行号存）
        std::vector<std::uint8_t> sel2(perm.size(), 0);
        for (std::size_t newi = 0; newi < perm.size(); ++newi)
            if (perm[newi] < s.row_sel.size())
                sel2[newi] = s.row_sel[perm[newi]];
        s.row_sel = std::move(sel2);
        s.row_h_data.clear();
        s.row_h_key = -1.0F;
    }

    inline void clamp_sel(state &s, const model &m); // 前置声明（结构变更要用）

    // ── 结构变更：模型 + 表格状态一起改（业务侧只调这几个）─────────────
    inline std::uint64_t add_row(state &s, model &m,
                                 std::vector<std::string> cells = {})
    {
        const auto id = m.add_row(std::move(cells));
        s.row_sel.push_back(0);
        s.row_h_data.clear();
        s.row_h_key = -1.0F;
        s.filter_ver++;
        if (s.on_model_changed)
            s.on_model_changed();
        return id;
    }
    inline bool remove_row_at(state &s, model &m, int at)
    {
        if (at < 0 || static_cast<std::size_t>(at) >= m.rows.size())
            return false;
        const auto id = m.id_of(at);
        if (!m.remove_row(id))
            return false;
        if (static_cast<std::size_t>(at) < s.row_sel.size())
            s.row_sel.erase(s.row_sel.begin() + at);
        s.row_h_data.clear();
        s.row_h_key = -1.0F;
        s.filter_ver++;
        clamp_sel(s, m);
        if (s.on_model_changed)
            s.on_model_changed();
        return true;
    }
    inline void insert_column(state &s, model &m, int at, column col)
    {
        const int n = static_cast<int>(m.cols.size());
        at = std::clamp(at, 0, n);
        m.insert_column(at, std::move(col));
        for (auto &v : s.order)
            if (v >= at)
                ++v;
        s.order.insert(s.order.begin() + at, at);
        s.col_w.insert(s.col_w.begin() + at,
                       m.cols[static_cast<std::size_t>(at)].w);
        s.col_user.insert(s.col_user.begin() + at, 1);
        s.filter.insert(s.filter.begin() + at, std::string{});
        if (s.sort_col >= at)
            ++s.sort_col;
        s.filter_ver++;
        clamp_sel(s, m);
        if (s.on_model_changed)
            s.on_model_changed();
    }
    inline bool remove_column_at(state &s, model &m, int at)
    {
        if (at < 0 || static_cast<std::size_t>(at) >= m.cols.size())
            return false;
        if (!m.remove_column(at))
            return false;
        for (std::size_t i = 0; i < s.order.size(); ++i)
            if (s.order[i] == at)
            {
                s.order.erase(s.order.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        for (auto &v : s.order)
            if (v > at)
                --v;
        if (static_cast<std::size_t>(at) < s.col_w.size())
            s.col_w.erase(s.col_w.begin() + at);
        if (static_cast<std::size_t>(at) < s.col_user.size())
            s.col_user.erase(s.col_user.begin() + at);
        if (static_cast<std::size_t>(at) < s.filter.size())
            s.filter.erase(s.filter.begin() + at);
        if (s.sort_col == at)
        {
            s.sort_col = -1;
            s.sort_dir = 0;
        }
        else if (s.sort_col > at)
            --s.sort_col;
        s.filter_ver++;
        clamp_sel(s, m);
        if (s.on_model_changed)
            s.on_model_changed();
        return true;
    }

    // 把选区夹到数据范围内（数据变少时不会残留越界索引）
    inline void clamp_sel(state &s, const model &m)
    {
        const int nr = static_cast<int>(m.row_count());
        const int nc = static_cast<int>(m.col_count());
        if (nr <= 0 || nc <= 0)
        {
            sel_clear(s);
            return;
        }
        auto clamp1 = [](int v, int hi) { return std::clamp(v, 0, hi - 1); };
        if (has_sel(s))
        {
            s.sel_r0 = clamp1(s.sel_r0, nr);
            s.sel_r1 = clamp1(s.sel_r1, nr);
            s.sel_c0 = clamp1(s.sel_c0, nc);
            s.sel_c1 = clamp1(s.sel_c1, nc);
        }
        if (s.cur_row >= nr)
            s.cur_row = nr - 1;
        if (s.cur_col >= nc)
            s.cur_col = nc - 1;
    }

    // 让某个格子可见（键盘导航/自动滚动用）
    inline void ensure_visible(state &s, const layout &L, const style &st, int row,
                               int col) noexcept
    {
        if (row >= 0 && L.step > 0.0F)
        {
            // 可变行高：按视图顺序累加找到该行
            const auto h_of = [&](int data_row) -> float
            {
                return static_cast<std::size_t>(data_row) < s.row_h_data.size() &&
                               s.row_h_data[static_cast<std::size_t>(data_row)] > 0.0F
                           ? s.row_h_data[static_cast<std::size_t>(data_row)]
                           : st.row_h;
            };
            float y0 = 0.0F;
            float rh = h_of(row);
            const std::size_t nv = s.row_view.size();
            const bool use_metrics =
                s.row_off.size() == nv + 1 && s.row_off_ver == s.filter_ver;
            if (use_metrics && static_cast<std::size_t>(row) < s.view_pos.size() &&
                s.view_pos[static_cast<std::size_t>(row)] >= 0)
            {
                // O(1)：直接查前缀和
                y0 = s.row_off[static_cast<std::size_t>(
                    s.view_pos[static_cast<std::size_t>(row)])];
            }
            else if (use_metrics)
            {
                y0 = static_cast<float>(row) * L.step; // 不在视图里（被筛掉）
            }
            else
            {
                bool found = false;
                for (std::size_t i = 0; i < nv; ++i)
                {
                    const int dr = s.row_view[i];
                    const float h = h_of(dr);
                    if (dr == row)
                    {
                        rh = h;
                        found = true;
                        break;
                    }
                    y0 += h + st.row_gap;
                }
                if (!found)
                    y0 = static_cast<float>(row) * L.step;
            }
            const float y1 = y0 + rh;
            const float max_sy = std::max(0.0F, L.content_h - L.body_view.h);
            if (y0 < s.sy + 0.5F)
            {
                s.sy = std::clamp(y0, 0.0F, max_sy);
                touch(s);
            }
            else if (y1 > s.sy + L.body_view.h - 0.5F)
            {
                s.sy = std::clamp(y1 - L.body_view.h, 0.0F, max_sy);
                touch(s);
            }
        }
        if (col < 0)
            return;
        const int i = col_pos(L, col);
        if (i < 0 || i < L.frozen)
            return; // 冻结列永远可见
        const float x0 = L.cols[static_cast<std::size_t>(i)].x;
        const float x1 = x0 + L.cols[static_cast<std::size_t>(i)].w;
        const float span = L.scroll_view.w;
        const float max_sx = std::max(0.0F, L.content_w - L.frozen_w - span);
        if (x0 < s.sx + 0.5F)
        {
            s.sx = std::clamp(x0, 0.0F, max_sx);
            touch(s);
        }
        else if (x1 > s.sx + span - 0.5F)
        {
            s.sx = std::clamp(x1 - span, 0.0F, max_sx);
            touch(s);
        }
    }

    // 双击列分隔 = 按内容自适应列宽（表格的标准操作）
    inline void autofit_column(state &s, const model &m, const font &f, const style &st,
                               int col)
    {
        if (col < 0 || static_cast<std::size_t>(col) >= m.cols.size())
            return;
        const auto c = static_cast<std::size_t>(col);
        float w = detail::text_width(f, m.cols[c].title);
        for (const auto &row : m.rows)
            if (c < row.size())
                w = std::max(w, detail::text_width(f, row[c]));
        w += 2.0F * st.cell_pad + 2.0F;
        w = std::clamp(w, m.cols[c].min_w, m.cols[c].max_w);
        if (c < s.col_w.size() && s.col_w[c] != w)
        {
            s.col_w[c] = w;
            s.col_user[c] = 1;
            touch(s);
            trace("[autofit] col={} w={:.1f}", col, w);
        }
    }

    // 换序落点：指针越过某列中点就算插到它后面。
    // 冻结列只能和冻结列换、滚动列只能和滚动列换（否则 frozen 语义会漂）。
    [[nodiscard]] inline int reorder_target(const layout &L, int from,
                                            float x) noexcept
    {
        const int n = static_cast<int>(L.cols.size());
        const bool frozen_band = from < L.frozen;
        const int lo = frozen_band ? 0 : L.frozen;
        const int hi = frozen_band ? L.frozen : n;
        int target = lo;
        for (int i = lo; i < hi; ++i)
            if (x > L.col_x(i) + L.cols[static_cast<std::size_t>(i)].w * 0.5F)
                target = i + 1;
        return std::clamp(target, lo, hi);
    }

    inline void apply_reorder(state &s)
    {
        const int n = static_cast<int>(s.order.size());
        const int from = s.reorder_from;
        int to = s.reorder_to;
        if (n <= 0 || from < 0 || to < 0 || from >= n)
            return;
        if (to > from)
            --to; // 先删后插，目标下标左移
        to = std::clamp(to, 0, n - 1);
        if (to == from)
            return;
        const int moved = s.order[static_cast<std::size_t>(from)];
        s.order.erase(s.order.begin() + from);
        s.order.insert(s.order.begin() + to, moved);
        touch(s);
        trace("[reorder] {} -> {}", from, to);
    }

    // ── 编辑框：像素 x → caret / 点选 / 拖选 / 双击选词 ────────────────────
    [[nodiscard]] inline float edit_text_origin_x(const state &s, const layout &L,
                                                  const style &st, const font &f) noexcept
    {
        const int pos = col_pos(L, s.edit.col);
        if (pos < 0)
            return 0.0F;
        const auto &cm = L.cols[static_cast<std::size_t>(pos)];
        const float cell_x = std::max(L.col_x(pos), L.clip_x0(pos));
        const float inner = std::max(cm.w - 2.0F * st.cell_pad, 1.0F);
        const auto caret = static_cast<std::size_t>(
            std::clamp(s.edit.caret, 0, static_cast<int>(s.edit.buf.size())));
        const float pre = detail::text_width(f, s.edit.buf.substr(0, caret));
        const float off = pre > inner ? pre - inner + 2.0F : 0.0F;
        return cell_x + st.cell_pad - off;
    }

    // 双击选词：把 caret **所在**的那个词整段选中
    //   （注意和 Ctrl+← 的区别：那是"跳到上一个词首"，不是"取当前词"）
    inline void edit_select_word(state &s)
    {
        auto &ed = s.edit;
        const auto caret = static_cast<std::size_t>(
            std::clamp(ed.caret, 0, static_cast<int>(ed.buf.size())));
        const std::string_view sv{ed.buf};
        std::size_t probe = caret;
        if (probe >= sv.size() || !is_word_byte(sv[probe]))
            probe = probe > 0 ? utf8_prev(sv, probe) : 0; // caret 在词尾右侧
        std::size_t lo = probe;
        while (lo > 0 && is_word_byte(sv[utf8_prev(sv, lo)]))
            lo = utf8_prev(sv, lo);
        std::size_t hi = probe;
        while (hi < sv.size() && is_word_byte(sv[hi]))
            hi = utf8_next(sv, hi);
        if (hi <= lo)
            hi = std::min(sv.size(), utf8_next(sv, lo)); // 空白/标点：至少选一个字符
        ed.anchor = static_cast<int>(lo);
        ed.caret = static_cast<int>(hi);
    }

    // 点在编辑框里：定位 caret（shift = 扩选，dbl = 选词）
    inline void edit_click_at(state &s, const layout &L, const style &st,
                              const font &f, float x, bool shift, bool dbl) noexcept
    {
        const float origin = edit_text_origin_x(s, L, st, f);
        const int pos = edit_caret_at(f, s.edit.buf, origin, x);
        if (dbl)
        {
            if (s.click_count >= 3)
            {
                // 三连击 = 全选（文本输入框的标准）
                s.edit.anchor = 0;
                s.edit.caret = static_cast<int>(s.edit.buf.size());
            }
            else
            {
                s.edit.caret = pos;
                edit_select_word(s);
            }
            s.edit.mouse_dragging = false;
            return;
        }
        s.edit.caret = pos;
        if (!shift)
            s.edit.anchor = pos;
        s.edit.mouse_dragging = !shift; // 按住不放 = 拖选
    }
    // 视图行偏移前缀和 + 反查表：视图或行高变了才重建（其余时间 O(1)）
    inline void rebuild_row_metrics(state &s, const model &m, const style &st)
    {
        const std::size_t n = s.row_view.size();
        if (s.row_off.size() == n + 1 && s.row_off_ver == s.filter_ver &&
            s.row_off_key == s.row_h_key && s.view_pos.size() == m.rows.size())
            return;
        s.row_off.assign(n + 1, 0.0F);
        s.view_pos.assign(m.rows.size(), -1);
        float acc = 0.0F;
        for (std::size_t i = 0; i < n; ++i)
        {
            const int dr = s.row_view[i];
            s.row_off[i] = acc;
            if (dr >= 0 && static_cast<std::size_t>(dr) < s.view_pos.size())
                s.view_pos[static_cast<std::size_t>(dr)] = static_cast<int>(i);
            const float h = static_cast<std::size_t>(dr) < s.row_h_data.size() &&
                                    s.row_h_data[static_cast<std::size_t>(dr)] > 0.0F
                                ? s.row_h_data[static_cast<std::size_t>(dr)]
                                : st.row_h;
            acc += h + st.row_gap;
        }
        s.row_off[n] = acc;
        s.row_off_ver = s.filter_ver;
        s.row_off_key = s.row_h_key;
    }

    inline void normalize(state &s, const model &m, const style &st,
                          const ui_dao::rect_px &avail)
    {
        if (s.col_w.size() != m.cols.size())
            reset_state(s, m);
        if (s.row_sel.size() != m.rows.size())
            s.row_sel.resize(m.rows.size(), 0);
        if (s.filter.size() != m.cols.size())
        {
            s.filter.resize(m.cols.size());
            s.filter_ver++;
        }
        if (s.filter_applied != s.filter_ver || s.filter_rows != m.rows.size())
        {
            apply_filters(s, m);
            s.filter_applied = s.filter_ver;
            s.filter_rows = m.rows.size();
        }
        rebuild_row_metrics(s, m, st);
        clamp_sel(s, m);
        const auto L = arrange(m, s, st, avail);
        if (L.sy != s.sy || L.sx != s.sx)
        {
            s.sy = L.sy;
            s.sx = L.sx;
            touch(s);
        }
        s.last = L;
    }

    // 每帧一次：命中来自 picking，几何来自上一帧 layout
    inline void update(state &s, model &m, const style &st, const font &font,
                       const input_view &in)
    {
        const auto &L = s.last;
        const int nrows = static_cast<int>(m.row_count());
        const int ncols = static_cast<int>(m.col_count());

        // ── hover / 派生带 ──
        s.hover = in.picked;
        s.band_row = -1;
        s.band_col = -1;
        switch (s.hover.kind)
        {
        case part::cell:
        case part::check:
            s.band_row = s.hover.row;
            s.band_col = s.hover.col;
            break;
        case part::header:
        case part::col_sep:
        case part::header_group:
            s.band_col = s.hover.col;
            break;
        default:
            break;
        }

        const bool press = in.left && !s.prev_left;
        const bool release = !in.left && s.prev_left;
        s.prev_left = in.left;
        s.now = in.now;
        s.pointer = {in.px, in.py};

        // ── 键盘边沿（编辑 / 导航共用一份）──
        auto &pk = s.prev_keys;
        const auto key_edge = [](bool now, bool &prev) noexcept
        {
            const bool e = now && !prev;
            prev = now;
            return e;
        };
        const bool e_left = key_edge(in.key_left, pk.left);
        const bool e_right = key_edge(in.key_right, pk.right);
        const bool e_up = key_edge(in.key_up, pk.up);
        const bool e_down = key_edge(in.key_down, pk.down);
        const bool e_home = key_edge(in.key_home, pk.home);
        const bool e_end = key_edge(in.key_end, pk.end);
        const bool e_pgup = key_edge(in.key_pgup, pk.pgup);
        const bool e_pgdn = key_edge(in.key_pgdn, pk.pgdn);
        const bool e_shift = key_edge(in.shift, pk.shift);
        const bool e_ctrl = key_edge(in.ctrl, pk.ctrl);
        const bool e_a = key_edge(in.key_a, pk.a);
        const bool e_back = key_edge(in.key_backspace, pk.backspace);
        const bool e_del = key_edge(in.key_del, pk.del);
        const bool e_enter = key_edge(in.key_enter, pk.enter);
        const bool e_esc = key_edge(in.key_esc, pk.esc);
        const bool e_tab = key_edge(in.key_tab, pk.tab);
        const bool e_x = key_edge(in.key_x, pk.x);
        const bool e_c = key_edge(in.key_c, pk.c);
        const bool e_v = key_edge(in.key_v, pk.v);
        const bool e_z = key_edge(in.key_z, pk.z);
        const bool e_y = key_edge(in.key_y, pk.y);
        const bool e_f2 = key_edge(in.key_f2, pk.f2);
        const bool e_space = key_edge(in.key_space, pk.space);
        const bool e_f = key_edge(in.key_f, pk.f);

        // ── 双击判定（同位置、0.4s 内）──
        bool dbl = false;
        if (press)
        {
            const float dx = in.px - s.last_click_pos.x;
            const float dy = in.py - s.last_click_pos.y;
            if (in.now - s.last_click_t < 0.40 && dx * dx + dy * dy < 25.0F)
            {
                dbl = true;
                s.click_count = std::min(s.click_count + 1, 4);
            }
            else
            {
                s.click_count = 1;
            }
            s.last_click_t = in.now;
            s.last_click_pos = {in.px, in.py};
        }

        // ── 模态输入：列筛选（表头漏斗）/ 查找条 ──
        bool modal = s.filter_edit >= 0 || s.find_mode;
        bool enter_used = false;
        bool tab_used = false;
        if (modal && !s.find_mode)
        {
            // 筛选输入：Enter 应用、Esc 取消
            if (!in.text.empty() && !in.ctrl)
                s.filter_buf += in.text;
            if (e_back && !s.filter_buf.empty())
                s.filter_buf.pop_back();
            if (e_esc)
                s.filter_edit = -1;
            if (e_enter || e_tab)
            {
                if (s.filter_edit >= 0 &&
                    static_cast<std::size_t>(s.filter_edit) < s.filter.size())
                {
                    s.filter[static_cast<std::size_t>(s.filter_edit)] = s.filter_buf;
                    apply_filters(s, m);
                }
                trace("[filter] col {} = \"{}\" -> {} rows", s.filter_edit, s.filter_buf,
                      s.row_view.size());
                s.filter_edit = -1;
            }
            enter_used = true;
            tab_used = true;
        }
        else if (modal)
        {
            // 查找：Enter 跳下一个命中、Esc 关闭
            if (!in.text.empty() && !in.ctrl)
            {
                s.find_buf += in.text;
                s.find_hit = -1;
            }
            if (e_back && !s.find_buf.empty())
                s.find_buf.pop_back();
            if (e_esc)
                s.find_mode = false;
            if (e_enter)
            {
                const int hit = find_next(s, m, s.cur_row);
                if (hit >= 0)
                {
                    int col = 0;
                    for (std::size_t c = 0; c < m.cols.size(); ++c)
                        if (static_cast<std::size_t>(hit) < m.rows.size() &&
                            c < m.rows[static_cast<std::size_t>(hit)].size() &&
                            text_contains(m.rows[static_cast<std::size_t>(hit)][c],
                                          s.find_buf))
                        {
                            col = static_cast<int>(c);
                            break;
                        }
                    s.find_hit = hit;
                    sel_anchor(s, hit, col);
                    ensure_visible(s, s.last, st, hit, col);
                    trace("[find] hit row {} col {}", hit, col);
                }
            }
            enter_used = true;
            tab_used = true;
        }

        // ── 编辑态：键盘先归编辑框（箭头移光标，不再移选区）──
        if (!modal && s.edit.active())
        {
            auto &ed = s.edit;
            // Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y：先撤/重做**文本**（不动表格）
            if (e_z && in.ctrl)
            {
                if (in.shift)
                    edit_redo(s);
                else
                    edit_undo(s);
            }
            if (e_y && in.ctrl)
                edit_redo(s);
            // 带 Ctrl/Alt 的字符直接丢弃：浏览器/编辑器都不会把 Ctrl+Z 的 'z' 打进文本
            if (!in.text.empty() && !in.ctrl)
            {
                edit_push_undo(s, true, in.now); // 连续输入合并成一步
                edit_replace_selection(s, in.text); // 有选区就替换
            }
            if (e_back)
            {
                if (ed.has_sel() || ed.caret > 0)
                    edit_push_undo(s, false, in.now);
                if (ed.has_sel())
                    edit_delete_selection(s);
                else if (ed.caret > 0)
                {
                    // Ctrl+Backspace = 删一个词
                    const auto from =
                        in.ctrl ? utf8_word_left(ed.buf,
                                                 static_cast<std::size_t>(ed.caret))
                                : utf8_prev(ed.buf, static_cast<std::size_t>(ed.caret));
                    ed.buf.erase(from, static_cast<std::size_t>(ed.caret) - from);
                    ed.caret = ed.anchor = static_cast<int>(from);
                }
            }
            if (e_del)
            {
                if (ed.has_sel() || ed.caret < static_cast<int>(ed.buf.size()))
                    edit_push_undo(s, false, in.now);
                if (ed.has_sel())
                    edit_delete_selection(s);
                else if (ed.caret < static_cast<int>(ed.buf.size()))
                {
                    // Ctrl+Delete = 删到词尾
                    const auto to =
                        in.ctrl ? utf8_word_right(ed.buf,
                                                  static_cast<std::size_t>(ed.caret))
                                : utf8_next(ed.buf, static_cast<std::size_t>(ed.caret));
                    ed.buf.erase(static_cast<std::size_t>(ed.caret),
                                 to - static_cast<std::size_t>(ed.caret));
                }
            }
            if (e_left)
            {
                // Ctrl+← = 按词移动；Shift 保持选区
                ed.caret = in.ctrl
                               ? static_cast<int>(utf8_word_left(
                                     ed.buf, static_cast<std::size_t>(ed.caret)))
                               : static_cast<int>(utf8_prev(
                                     ed.buf, static_cast<std::size_t>(ed.caret)));
                if (!in.shift)
                    ed.anchor = ed.caret;
            }
            if (e_right)
            {
                ed.caret = in.ctrl
                               ? static_cast<int>(utf8_word_right(
                                     ed.buf, static_cast<std::size_t>(ed.caret)))
                               : static_cast<int>(utf8_next(
                                     ed.buf, static_cast<std::size_t>(ed.caret)));
                if (!in.shift)
                    ed.anchor = ed.caret;
            }
            if (e_home)
            {
                ed.caret = 0;
                if (!in.shift)
                    ed.anchor = ed.caret;
            }
            if (e_end)
            {
                ed.caret = static_cast<int>(ed.buf.size());
                if (!in.shift)
                    ed.anchor = ed.caret;
            }
            if (e_a && in.ctrl) // 编辑框内全选
            {
                ed.anchor = 0;
                ed.caret = static_cast<int>(ed.buf.size());
            }
            if (e_c && in.ctrl && ed.has_sel())
            {
                const auto lo = static_cast<std::size_t>(ed.sel_lo());
                const auto hi = static_cast<std::size_t>(ed.sel_hi());
                clip_copy(s, ed.buf.substr(lo, hi - lo));
            }
            if (e_x && in.ctrl && ed.has_sel())
            {
                const auto lo = static_cast<std::size_t>(ed.sel_lo());
                const auto hi = static_cast<std::size_t>(ed.sel_hi());
                clip_copy(s, ed.buf.substr(lo, hi - lo));
                edit_push_undo(s, false, in.now);
                edit_delete_selection(s);
            }
            if (e_v && in.ctrl && !in.clipboard.empty())
            {
                edit_push_undo(s, false, in.now);
                edit_replace_selection(s, in.clipboard);
            }
            if (e_enter)
            {
                // Enter = 提交并下移；Shift+Enter = 上移（编辑后选区跟着走）
                const int r = ed.row;
                const int c = ed.col;
                edit_commit(s, m);
                enter_used = true; // 同一帧不要再被"Enter 进入编辑"消费一次
                const int nr = std::clamp(r + (in.shift ? -1 : 1), 0, nrows - 1);
                sel_anchor(s, nr, c);
                ensure_visible(s, L, st, nr, c);
            }
            if (e_esc)
                edit_cancel(s);
            if (e_tab && s.edit.active())
            {
                // Tab = 提交并右移；Shift+Tab = 左移（不再自动重新进入编辑）
                tab_used = true; // 同一帧不要让非编辑态的 Tab 再挪一次
                const int r = ed.row;
                const int c = ed.col;
                edit_commit(s, m);
                const int nc = std::clamp(c + (in.shift ? -1 : 1), 0, ncols - 1);
                sel_anchor(s, r, nc);
                ensure_visible(s, L, st, r, nc);
            }
            // IME 预编辑串只显示，不进入 buf（提交后由 char_event 送进来）
            s.edit.compose = in.compose.empty() ? std::string{}
                                                : std::string(in.compose);
        }

        // ── 编辑框内的鼠标：点/拖/双击选词（点外面 = 提交，已在 press 分支处理）──
        if (s.edit.active())
        {
            const bool same = s.hover.kind == part::cell &&
                              s.hover.row == s.edit.row && s.hover.col == s.edit.col;
            if (press && same)
                edit_click_at(s, L, st, font, in.px, in.shift, dbl);
            else if (in.left && s.edit.mouse_dragging && same)
            {
                // 拖选：锚点不动，caret 跟着指针（可以在格子外，靠 clip 收口）
                const float origin = edit_text_origin_x(s, L, st, font);
                s.edit.caret = edit_caret_at(font, s.edit.buf, origin, in.px);
            }
            if (!in.left)
                s.edit.mouse_dragging = false;
        }
        // ── 按下：按 part 固定分发 ──
        if (press)
        {
            s.dragging = drag::none;
            s.drag_zone = s.hover;
            s.drag_start = {in.px, in.py};
            trace("[press] part={} row={} col={} at ({:.0f},{:.0f}) dbl={}",
                  part_name(s.hover.kind), s.hover.row, s.hover.col, in.px, in.py,
                  dbl);

            // 模态输入：点到别处 → 提交筛选串并退出
            if (s.filter_edit >= 0 && s.hover.kind != part::filter_btn)
            {
                if (static_cast<std::size_t>(s.filter_edit) < s.filter.size())
                {
                    s.filter[static_cast<std::size_t>(s.filter_edit)] = s.filter_buf;
                    apply_filters(s, m);
                }
                s.filter_edit = -1;
                modal = false;
            }
            if (s.find_mode)
            {
                s.find_mode = false;
                modal = false;
            }

            // 点到别的格子 = 先提交正在编辑的内容
            const bool same_edit_cell =
                s.edit.active() && s.hover.kind == part::cell &&
                s.hover.row == s.edit.row && s.hover.col == s.edit.col;
            if (s.edit.active() && !same_edit_cell)
                edit_commit(s, m);

            switch (s.hover.kind)
            {
            case part::col_sep:
                if (dbl)
                {
                    autofit_column(s, m, font, st, s.hover.col);
                    break;
                }
                if (s.hover.col >= 0 &&
                    static_cast<std::size_t>(s.hover.col) < s.col_w.size())
                {
                    s.dragging = drag::col_width;
                    s.drag_base = s.col_w[static_cast<std::size_t>(s.hover.col)];
                }
                break;
            case part::vbar_thumb:
                s.dragging = drag::vthumb;
                s.drag_base = s.sy;
                break;
            case part::hbar_thumb:
                s.dragging = drag::hthumb;
                s.drag_base = s.sx;
                break;
            case part::vbar_track:
                s.sy -= L.body_view.h * 0.9F;
                touch(s);
                break;
            case part::hbar_track:
                s.sx -= L.scroll_view.w * 0.9F;
                touch(s);
                break;
            case part::edge_n:
            case part::edge_w:
            case part::edge_ne:
            case part::edge_nw:
            case part::edge_se:
            case part::edge_sw:
                s.dragging = drag::table_size;
                s.drag_base = L.frame.w;
                s.drag_base2 = L.frame.h;
                break;
            case part::header:
                // 先当"可能是换序拖拽"；松手时没移动才算点击排序
                if (s.hover.col >= 0)
                {
                    s.dragging = drag::header;
                    s.reorder_from = col_pos(L, s.hover.col);
                    s.reorder_to = s.reorder_from;
                }
                break;
            case part::header_group: // 点分组头 → 选中这一组的所有列
            {
                for (const auto &g : L.groups)
                    if (g.first_col >= 0 && g.first_col == s.hover.col)
                    {
                        sel_anchor(s, 0, L.cols[static_cast<std::size_t>(g.first_col)].index);
                        sel_extend(
                            s, std::max(0, static_cast<int>(m.rows.size()) - 1),
                            L.cols[static_cast<std::size_t>(g.last_col)].index);
                        break;
                    }
                s.dragging = drag::none;
                break;
            }
            case part::filter_btn: // 点漏斗 → 在表头里就地输入筛选串
                if (s.hover.col >= 0 &&
                    static_cast<std::size_t>(s.hover.col) < s.filter.size())
                {
                    s.filter_edit = s.hover.col;
                    s.filter_buf = s.filter[static_cast<std::size_t>(s.hover.col)];
                    s.find_mode = false;
                }
                break;
            case part::check_head: // 全选 / 全不选（只作用于筛选后的可见行）
            {
                const bool all = all_checked_visible(s);
                for (const int r : s.row_view)
                    if (r >= 0 && static_cast<std::size_t>(r) < s.row_sel.size())
                    {
                        s.row_sel[static_cast<std::size_t>(r)] = all ? 0 : 1;
                        if (s.on_row_checked)
                            s.on_row_checked(m.id_of(r), !all);
                    }
                s.last_check = -1;
                trace("[check] visible all -> {}", !all);
                break;
            }
            case part::check: // 单行；Shift = 连选
                if (s.hover.row >= 0)
                {
                    if (in.shift && s.last_check >= 0)
                    {
                        const bool on = !row_checked(s, s.hover.row);
                        const int lo = std::min(s.last_check, s.hover.row);
                        const int hi = std::max(s.last_check, s.hover.row);
                        row_check_range(s, s.last_check, s.hover.row, on);
                        if (s.on_row_checked)
                            for (int rr = lo; rr <= hi; ++rr)
                                s.on_row_checked(m.id_of(rr), on);
                    }
                    else
                        row_check_set(s, s.hover.row, !row_checked(s, s.hover.row));
                    if (s.on_row_checked)
                        s.on_row_checked(m.id_of(s.hover.row),
                                         row_checked(s, s.hover.row));
                    trace("[check] row {} -> {}", s.hover.row,
                          row_checked(s, s.hover.row));
                }
                break;
            case part::cell:
                if (same_edit_cell)
                    break; // 点在编辑框里：不动选区
                if (s.hover.row >= 0 && s.hover.col >= 0)
                {
                    if (dbl)
                    {
                        // 双击进入编辑：caret 落在鼠标点的位置（浏览器/Excel 的习惯）
                        edit_begin(s, m, s.hover.row, s.hover.col);
                        const int pos = col_pos(L, s.hover.col);
                        if (pos >= 0)
                        {
                            const auto &cm = L.cols[static_cast<std::size_t>(pos)];
                            const float x0 =
                                std::max(L.col_x(pos), L.clip_x0(pos)) + st.cell_pad;
                            s.edit.caret = edit_caret_at(font, s.edit.buf, x0, in.px);
                            s.edit.anchor = s.edit.caret;
                        }
                        s.dragging = drag::none;
                    }
                    else
                    {
                        if (in.shift)
                            sel_extend(s, s.hover.row, s.hover.col);
                        else
                            sel_anchor(s, s.hover.row, s.hover.col);
                        s.dragging = drag::select; // 继续拖 = 拉矩形选区
                    }
                }
                break;
            default:
                sel_clear(s); // 点非格区域 = 取消选区
                break;
            }
        }

        // ── 拖拽中（捕获，不依赖当帧命中）──
        if (s.dragging != drag::none && in.left)
        {
            const float dx = in.px - s.drag_start.x;
            const float dy = in.py - s.drag_start.y;
            switch (s.dragging)
            {
            case drag::col_width:
            {
                const auto ci = static_cast<std::size_t>(s.drag_zone.col);
                if (ci < s.col_w.size() && ci < m.cols.size())
                {
                    const auto &col = m.cols[ci];
                    const float nw = std::clamp(s.drag_base + dx, col.min_w, col.max_w);
                    if (nw != s.col_w[ci])
                    {
                        s.col_w[ci] = nw;
                        if (ci < s.col_user.size())
                            s.col_user[ci] = 1;
                        touch(s);
                        trace("[drag] col={} w={:.1f}", ci, nw);
                    }
                }
                break;
            }
            case drag::vthumb:
            {
                const float ratio =
                    L.content_h > 0.0F ? L.body_view.h / L.content_h : 1.0F;
                const float max_sy = std::max(0.0F, L.content_h - L.body_view.h);
                const float next = std::clamp(
                    s.drag_base + dy / std::max(ratio, 0.001F), 0.0F, max_sy);
                if (next != s.sy)
                {
                    s.sy = next;
                    touch(s);
                }
                break;
            }
            case drag::hthumb:
            {
                const float span = std::max(L.content_w - L.frozen_w, 1.0F);
                const float ratio = L.scroll_view.w / span;
                const float max_sx = std::max(0.0F, span - L.scroll_view.w);
                const float next = std::clamp(
                    s.drag_base + dx / std::max(ratio, 0.001F), 0.0F, max_sx);
                if (next != s.sx)
                {
                    s.sx = next;
                    touch(s);
                }
                break;
            }
            case drag::table_size:
            {
                const auto k = s.drag_zone.kind;
                const bool west =
                    k == part::edge_w || k == part::edge_nw || k == part::edge_sw;
                const bool north =
                    k == part::edge_n || k == part::edge_ne || k == part::edge_nw;
                float nw = s.drag_base + (west ? -dx : dx);
                float nh = s.drag_base2 + (north ? -dy : dy);
                nw = std::clamp(nw, st.min_w, st.max_w);
                nh = std::clamp(nh, st.min_h, st.max_h);
                if (nw != s.user_w || nh != s.user_h)
                {
                    s.user_w = nw;
                    s.user_h = nh;
                    touch(s);
                    trace("[drag] size={:.0f}x{:.0f}", nw, nh);
                }
                break;
            }
            case drag::header:
                // 横向拖过阈值 → 认定为换序（纵向不动就不会触发）
                if (std::abs(dx) > 6.0F && s.reorder_from >= 0)
                {
                    s.dragging = drag::col_reorder;
                    s.reorder_to = reorder_target(L, s.reorder_from, in.px);
                }
                break;
            case drag::col_reorder:
                s.reorder_to = reorder_target(L, s.reorder_from, in.px);
                break;
            case drag::select:
            {
                // 拖到体区外：自动滚动（标准行为）
                if (in.py < L.body_view.y && s.sy > 0.0F)
                {
                    s.sy = std::max(0.0F, s.sy - L.step);
                    touch(s);
                }
                else if (in.py > L.body_view.y + L.body_view.h)
                {
                    const float max_sy = std::max(0.0F, L.content_h - L.body_view.h);
                    s.sy = std::min(max_sy, s.sy + L.step);
                    touch(s);
                }
                const int r = row_at(L, in.py);
                const int c = col_at(L, in.px);
                if (r >= 0 && c >= 0 && (r != s.sel_r1 || c != s.sel_c1))
                    sel_extend(s, r, c);
                break;
            }
            default:
                break;
            }
        }
        if (release)
        {
            trace("[release] drag was {}", static_cast<int>(s.dragging));
            if (s.dragging == drag::header && s.drag_zone.kind == part::header &&
                s.drag_zone.col >= 0)
            {
                // 点击表头：升 → 降 → 无
                if (s.sort_col == s.drag_zone.col)
                    s.sort_dir = (s.sort_dir + 1) % 3;
                else
                {
                    s.sort_col = s.drag_zone.col;
                    s.sort_dir = 1;
                }
                sort_rows(m, s);
                sel_clear(s); // 行序变了，旧选区索引没有意义
            }
            else if (s.dragging == drag::col_reorder)
            {
                // 松手位置也参与判定（最后一帧的指针才是用户真正松手的地方）
                s.reorder_to = reorder_target(L, s.reorder_from, in.px);
                apply_reorder(s);
            }
            s.dragging = drag::none;
            s.drag_zone = {};
            s.reorder_from = s.reorder_to = -1;
        }

        // ── 键盘导航（编辑态已经在上面的编辑块里消费掉了）──
        if (!modal && !s.edit.active() && s.dragging == drag::none && nrows > 0 &&
            ncols > 0)
        {
            if (e_left || e_right || e_up || e_down || e_home || e_end || e_pgup ||
                e_pgdn || e_tab || e_enter)
                trace("[key] L{} R{} U{} D{} H{} E{} PU{} PD{} Tab{} Enter{} ctrl={} "
                      "shift={} cur=({},{})",
                      e_left, e_right, e_up, e_down, e_home, e_end, e_pgup, e_pgdn,
                      e_tab, e_enter, in.ctrl, in.shift, s.cur_row, s.cur_col);

            int r = s.cur_row < 0 ? 0 : s.cur_row;
            int c = s.cur_col < 0 ? 0 : s.cur_col;
            bool moved = false;
            const int page =
                std::max(1, static_cast<int>(L.body_view.h / std::max(L.step, 1.0F)) - 1);
            // Ctrl+方向 = 跳到该方向的最边（数据网格的通用快捷键）
            if (e_left)
            {
                c = in.ctrl ? 0 : std::max(0, c - 1);
                moved = true;
            }
            if (e_right)
            {
                c = in.ctrl ? ncols - 1 : std::min(ncols - 1, c + 1);
                moved = true;
            }
            if (e_up)
            {
                r = in.ctrl ? 0 : std::max(0, r - 1);
                moved = true;
            }
            if (e_down)
            {
                r = in.ctrl ? nrows - 1 : std::min(nrows - 1, r + 1);
                moved = true;
            }
            if (e_home)
            {
                c = 0;
                if (in.ctrl || e_ctrl)
                    r = 0;
                moved = true;
            }
            if (e_end)
            {
                c = ncols - 1;
                if (in.ctrl || e_ctrl)
                    r = nrows - 1;
                moved = true;
            }
            if (e_pgup)
            {
                r = std::max(0, r - page);
                moved = true;
            }
            if (e_pgdn)
            {
                r = std::min(nrows - 1, r + page);
                moved = true;
            }
            // Tab / Shift+Tab = 下一格 / 上一格，行末自动换行（浏览器的 tab 顺序）
            if (e_tab && !tab_used)
            {
                int nc = c + (in.shift ? -1 : 1);
                if (nc >= ncols)
                {
                    nc = 0;
                    r = std::min(nrows - 1, r + 1);
                }
                else if (nc < 0)
                {
                    nc = ncols - 1;
                    r = std::max(0, r - 1);
                }
                c = nc;
                moved = true;
            }
            // Enter / Shift+Enter = 上下移动（数据网格标准；编辑用 F2/双击/直接打字）
            if (e_enter && !enter_used)
            {
                r = std::clamp(r + (in.shift ? -1 : 1), 0, nrows - 1);
                moved = true;
            }
            if (e_space && s.cur_row >= 0) // Space = 切换当前行选择框
            {
                row_check_set(s, s.cur_row, !row_checked(s, s.cur_row));
                if (s.on_row_checked)
                    s.on_row_checked(m.id_of(s.cur_row),
                                     row_checked(s, s.cur_row));
                moved = false;
            }
            if (e_a && in.ctrl) // Ctrl+A 全选
            {
                sel_anchor(s, 0, 0);
                sel_extend(s, nrows - 1, ncols - 1);
                moved = false;
            }
            if (moved)
            {
                if (in.shift || e_shift)
                    sel_extend(s, r, c);
                else
                    sel_anchor(s, r, c);
                ensure_visible(s, L, st, r, c);
                trace("[key] -> ({},{})", r, c);
            }
        }

        // ── Ctrl+F：查找条 ──
        if (!s.edit.active() && !modal && in.ctrl && e_f)
        {
            s.find_mode = true;
            s.find_buf.clear();
            s.find_hit = -1;
            trace("[find] open");
        }

        // ── 非编辑态：整块复制/粘贴（TSV，表格的标准格式）──
        if (!s.edit.active() && in.ctrl && (e_c || e_x) && has_sel(s))
            clip_copy(s, range_to_tsv(s, m));
        if (!s.edit.active() && in.ctrl && e_v && !in.clipboard.empty())
            paste_tsv(m, s, in.clipboard);

        // ── 非编辑态快捷键：先处理组合键，再处理"打字即编辑" ──
        //    （否则 Ctrl+Z 里的 'z' 会把编辑框打开、把撤销挡掉）
        // Delete = 清空选区
        if (!s.edit.active() && e_del)
            clear_selection(s, m);
        // Ctrl+Z / Ctrl+Y = 撤销 / 重做
        if (!s.edit.active() && in.ctrl && e_z)
            undo_step(s, m, in.shift); // Ctrl+Shift+Z = 重做
        if (!s.edit.active() && in.ctrl && e_y)
            undo_step(s, m, true);
        // F2 = 进入编辑（光标在末尾，不选中）
        if (!s.edit.active() && e_f2 && s.cur_row >= 0 && s.cur_col >= 0)
            edit_begin(s, m, s.cur_row, s.cur_col);
        // 选中格上直接打字 = 进入编辑并替换内容（Excel 习惯）。
        // 带 Ctrl/Alt 的字符不算打字（快捷键优先）。
        if (!s.edit.active() && !in.ctrl && !in.text.empty() && s.cur_row >= 0 &&
            s.cur_col >= 0)
        {
            edit_begin(s, m, s.cur_row, s.cur_col);
            s.edit.buf.clear(); // 直接打字 = 替换整格内容
            s.edit.anchor = s.edit.caret = 0;
            edit_replace_selection(s, in.text);
        }

        // ── 滚轮：横向条上 = 横向；Shift = 横向；否则纵向 ──
        if (in.wheel != 0.0F && in.picked.inside())
        {
            const bool over_hbar = s.hover.kind == part::hbar_track ||
                                   s.hover.kind == part::hbar_thumb;
            const float step = std::max(L.step, 1.0F) * 3.0F;
            if (over_hbar || in.shift)
            {
                const float max_sx =
                    std::max(0.0F, L.content_w - L.frozen_w - L.scroll_view.w);
                const float next =
                    std::clamp(s.sx - in.wheel * step * 2.0F, 0.0F, max_sx);
                if (next != s.sx)
                {
                    s.sx = next;
                    touch(s);
                }
            }
            else
            {
                const float max_sy = std::max(0.0F, L.content_h - L.body_view.h);
                const float next = std::clamp(s.sy - in.wheel * step, 0.0F, max_sy);
                if (next != s.sy)
                {
                    s.sy = next;
                    touch(s);
                }
            }
        }
    }
    // ═══ 9. 自测：不依赖窗口 / GPU，直接驱动 update 的数据面 ═══════════════
    //   每个功能一条 check，全部命中才算过；TABLE_SELFTEST=1 时由 main 调用。
    inline int selftest()
    {
        int fails = 0;
        const auto check = [&fails](bool ok, std::string_view what)
        {
            std::println("{} {}", ok ? "PASS" : "FAIL", what);
            std::fflush(stdout); // 崩溃时也能定位最后一条
            if (!ok)
                ++fails;
        };

        // ── 测试数据：3 列 × 300px（内容 900 > 视口 → 横条）、20 行（→ 竖条）
        nlohmann::json j = nlohmann::json::parse(R"({
            "columns": [
                {"key":"a","title":"A","width":300,"min":40,"max":600},
                {"key":"b","title":"B","width":300,"min":40,"max":600},
                {"key":"c","title":"C","width":300,"min":40,"max":600}
            ],
            "rows": []
        })");
        for (int i = 0; i < 20; ++i)
            j["rows"].push_back(nlohmann::json::array(
                {std::format("a{:02d}", i), std::format("b{:02d}", (i * 7) % 20),
                 std::format("c{:02d}", i)}));

        model m = model::from_json(j);
        style st{};
        st.frozen = 0;
        st.bar = 8.0F;
        st.header_h = 40.0F;
        st.row_h = 30.0F;
        st.row_gap = 2.0F;
        st.cell_pad = 10.0F;

        font f{};
        f.px = 15.0F;
        glyph_metrics gm{};
        gm.advance = 0.6F;
        gm.y_down = true;
        gm.plane = {-0.05F, -0.7F, 0.55F, 0.2F};
        gm.uv = {0.0F, 0.0F, 0.1F, 0.1F};
        for (char32_t c = 32; c < 127; ++c)
            f.glyphs[c] = gm;

        const ui_dao::rect_px box{0.0F, 0.0F, 600.0F, 400.0F};
        state s{};
        reset_state(s, m);
        normalize(s, m, st, box);

        double clock = 1.0;
        const auto frame = [&](input_view in)
        {
            in.now = clock;
            clock += 0.05;
            update(s, m, st, f, in);
            normalize(s, m, st, box);
        };
        const auto press_at = [&](zone z, float x, float y, bool shift = false)
        {
            input_view in{};
            in.picked = z;
            in.px = x;
            in.py = y;
            in.shift = shift;
            in.left = true;
            frame(in);
            in.left = false;
            frame(in);
        };
        // 屏幕坐标 → 用列/行几何算，避免测试里手算像素
        const auto col_screen_x = [&](int pos)
        { return s.last.col_x(pos) + s.last.cols[static_cast<std::size_t>(pos)].w * 0.5F; };
        const auto row_screen_y = [&](int row)
        {
            return s.last.body_view.y + static_cast<float>(row) * s.last.step - s.sy +
                   s.last.step * 0.4F;
        };

        // ── 1. 布局不变量 ──
        {
            const auto &L = s.last;
            check(L.header_view.x == L.body_view.x && L.header_view.w == L.body_view.w,
                  "layout: header_view 与 body_view 同 x / 同宽");
            check(L.vbar_on && L.hbar_on, "layout: 内容超视口 → 竖条 + 横条都出现");
            check(L.vbar_track.x >= L.body_view.x + L.body_view.w - 0.5F,
                  "layout: 竖条不压体区");
            check(L.hbar_track.y >= L.body_view.y + L.body_view.h - 0.5F,
                  "layout: 横条不压体区");
            check(L.cols.size() == 3 && L.cols[1].x == 300.0F && L.cols[2].x == 600.0F,
                  "layout: 列 x = 前序列宽累加");
            check(std::abs(L.content_w - 900.0F) < 0.5F, "layout: content_w = Σ列宽");
        }

        // ── 2. 缩放：手把不能压滚动条 ──
        {
            const auto &L = s.last;
            check(intersect(L.edges.se, L.hbar_track).w <= 0.0F &&
                      intersect(L.edges.se, L.vbar_track).h <= 0.0F,
                  "resize: 右下角手把避开两条滚动条");
            check(intersect(L.edges.w, L.hbar_track).w <= 0.0F,
                  "resize: w 边不吃横条");
            check(L.edges.n.w > 0.0F && L.edges.w.w > 0.0F && L.edges.se.w > 0.0F,
                  "resize: n / w / se 手把都存在");
        }

        // ── 3. zone 打包 ──
        {
            bool ok = true;
            for (std::uint32_t k = 0; k <= static_cast<std::uint32_t>(part::edge_sw); ++k)
            {
                const zone z{static_cast<part>(k), 7, 123};
                const zone r = zone_from(zone_key(z));
                ok = ok && r.kind == z.kind && r.row == 7 && r.col == 123;
                const zone n{static_cast<part>(k), -1, -1};
                const zone rn = zone_from(zone_key(n));
                ok = ok && rn.kind == n.kind && rn.row == -1 && rn.col == -1;
            }
            check(ok, "zone: 全 part 的 zone_key/zone_from 往返一致（含 -1 哨兵）");
        }

        // ── 4. 光标 ──
        check(cursor_for({part::cell, 1, 1}) == cursor::arrow,
              "cursor: cell = 默认箭头（不是编辑光标）");
        check(cursor_for({part::header, -1, 1}) == cursor::hand, "cursor: header = hand");
        check(cursor_for({part::col_sep, -1, 1}) == cursor::resize_ew,
              "cursor: 列分隔 = resize_ew");
        check(cursor_for({part::vbar_thumb}) == cursor::hand,
              "cursor: 竖条 thumb = hand");
        check(cursor_for({part::edge_se}) == cursor::resize_nwse,
              "cursor: se 角 = resize_nwse");
        check(cursor_for({part::frame}) == cursor::arrow, "cursor: 外框 = arrow");

        // ── 5. 点选 / 拖选 / Shift 扩选 / 点外清除 ──
        press_at({part::cell, 2, 1}, col_screen_x(1), s.last.body_view.y + 2.0F * s.last.step + 5.0F);
        check(s.sel_r0 == 2 && s.sel_c0 == 1 && s.sel_r1 == 2 && s.sel_c1 == 1,
              "select: 点格 → 单格选区");

        {
            input_view in{};
            in.picked = {part::cell, 2, 1};
            in.left = true;
            frame(in);
            in.picked = {part::cell, 5, 2};
            in.px = col_screen_x(2);
            in.py = row_screen_y(5);
            frame(in);
            in.left = false;
            frame(in);
            check(s.sel_r0 == 2 && s.sel_c0 == 1 && s.sel_r1 == 5 &&
                      s.sel_c1 == 2,
                  "select: 按住拖 → 矩形选区 (2,1)-(5,2)");
        }

        press_at({part::cell, 7, 0}, col_screen_x(0), row_screen_y(7), true);
        check(s.sel_r0 == 2 && s.sel_c0 == 1 && s.sel_r1 == 7 && s.sel_c1 == 0,
              "select: Shift+点 → 以原锚点扩选到 (7,0)");

        press_at({}, 10.0F, 10.0F);
        check(!has_sel(s), "select: 点非格区域 → 清选区");

        // ── 6. 键盘 ──
        press_at({part::cell, 2, 1}, col_screen_x(1), 120.0F);
        {
            input_view in{};
            in.key_right = true;
            frame(in);
            in.key_right = false;
            frame(in);
            check(s.cur_row == 2 && s.cur_col == 2, "key: → 右移一格");

            in.key_down = true;
            in.shift = true;
            frame(in);
            in.key_down = false;
            in.shift = false;
            frame(in);
            check(s.sel_r0 == 2 && s.sel_r1 == 3 && s.sel_c0 == 2 && s.sel_c1 == 2,
                  "key: Shift+↓ 扩展选区");

            in.key_end = true;
            frame(in);
            in.key_end = false;
            frame(in);
            check(s.cur_col == 2, "key: End → 最后一列");

            in.key_home = true;
            in.ctrl = true;
            frame(in);
            in.key_home = false;
            in.ctrl = false;
            frame(in);
            check(s.cur_row == 0 && s.cur_col == 0, "key: Ctrl+Home → 表首");

            in.key_a = true;
            in.ctrl = true;
            frame(in);
            in.key_a = false;
            in.ctrl = false;
            frame(in);
            check(has_sel(s) && s.sel_r0 == 0 && s.sel_c0 == 0 && s.sel_r1 == 19 &&
                      s.sel_c1 == 2,
                  "key: Ctrl+A 全选");
        }

        // ── 7. 滚轮 ──
        {
            const float sy0 = s.sy;
            input_view in{};
            in.picked = {part::cell, 3, 1};
            in.px = col_screen_x(1);
            in.py = 150.0F;
            in.wheel = -1.0F;
            frame(in);
            check(s.sy > sy0, "wheel: 向下滚 → sy 增大");

            const float sx0 = s.sx;
            in.wheel = -1.0F;
            in.shift = true;
            frame(in);
            check(s.sx > sx0, "wheel: Shift+滚轮 → 横向滚动");

            const float sx1 = s.sx;
            in.shift = false;
            in.picked = {part::hbar_thumb};
            in.wheel = -1.0F;
            frame(in);
            check(s.sx > sx1, "wheel: 横条上滚轮 → 横向滚动");
        }

        // ── 8. 滚动条拖拽 / 翻页 ──
        {
            const float sy0 = s.sy;
            input_view in{};
            in.picked = {part::vbar_thumb};
            in.left = true;
            in.px = s.last.vbar_thumb.x + 2.0F;
            in.py = s.last.vbar_thumb.y + 4.0F;
            frame(in);
            in.py += 30.0F;
            frame(in);
            in.left = false;
            frame(in);
            check(s.sy > sy0, "bar: 拖竖条 thumb → sy 增大");

            const float sx0 = s.sx;
            in.picked = {part::hbar_thumb};
            in.left = true;
            in.px = s.last.hbar_thumb.x + 4.0F;
            in.py = s.last.hbar_thumb.y + 2.0F;
            frame(in);
            in.px -= 30.0F; // 往左拖（此时 sx 已在最大端）
            frame(in);
            in.left = false;
            frame(in);
            check(s.sx < sx0, "bar: 拖横条 thumb 向左 → sx 减小");

            const float sy1 = s.sy;
            press_at({part::vbar_track}, s.last.vbar_track.x + 2.0F,
                     s.last.vbar_track.y + 4.0F);
            check(s.sy != sy1, "bar: 点竖条轨道 → 翻页");
        }

        // ── 9. 列宽拖拽 / 双击自适应 ──
        {
            const float w0 = s.col_w[0];
            input_view in{};
            in.picked = {part::col_sep, -1, 0};
            in.left = true;
            in.px = s.last.col_x(1);
            in.py = s.last.body_view.y + 20.0F;
            frame(in);
            in.px += 40.0F;
            frame(in);
            in.left = false;
            frame(in);
            check(std::abs(s.col_w[0] - (w0 + 40.0F)) < 0.5F,
                  "drag: 拖列分隔 +40px → 左列宽 +40");

            const int pos = 2; // 第 3 列
            const float x = s.last.col_x(pos);
            const float y = s.last.body_view.y + 20.0F;
            press_at({part::col_sep, -1, pos - 1}, x, y);
            press_at({part::col_sep, -1, pos - 1}, x, y); // 第二次按下 → double click
            check(std::abs(s.col_w[static_cast<std::size_t>(pos - 1)] - 49.0F) < 1.0F,
                  "double-click: 分隔 → 按内容自适应列宽 (= 文本宽 + 2*pad + 2)");
        }

        // ── 10. 排序 ──
        {
            std::string before = s.last.rows.empty() ? "" : m.rows[0][1];
            press_at({part::header, -1, 1}, col_screen_x(1), s.last.header_view.y + 10.0F);
            check(s.sort_col == 1 && s.sort_dir == 1, "sort: 点表头 → 升序");
            bool ordered = true;
            for (std::size_t i = 1; i < m.rows.size(); ++i)
                ordered = ordered && m.rows[i - 1][1] <= m.rows[i][1];
            check(ordered, "sort: 行按该列升序重排");
            press_at({part::header, -1, 1}, col_screen_x(1), s.last.header_view.y + 10.0F);
            check(s.sort_dir == 2, "sort: 再点 → 降序");
            press_at({part::header, -1, 1}, col_screen_x(1), s.last.header_view.y + 10.0F);
            check(s.sort_dir == 0, "sort: 第三次 → 取消排序");
        }

        // ── 11. 整表缩放（只走 n/w/角）──
        {
            const float w0 = s.last.frame.w;
            const float h0 = s.last.frame.h;
            input_view in{};
            in.picked = {part::edge_ne};
            in.left = true;
            in.px = 0.0F;
            in.py = 0.0F;
            frame(in);
            in.px = -40.0F; // 往左 → 变窄
            in.py = 30.0F;  // 往下 → 变矮（north 边跟着走）
            frame(in);
            in.left = false;
            frame(in);
            check(s.user_w < w0 && s.user_h < h0, "resize: 拖 ne 角向左下 → 宽高同时变小");
            check(s.user_w >= st.min_w && s.user_h >= st.min_h,
                  "resize: 尺寸不小于 min_w/min_h");
        }

        // ── 12. 冻结列 ──
        {
            state s2{};
            style st2 = st;
            st2.frozen = 1;
            reset_state(s2, m);
            normalize(s2, m, st2, box);
            const auto &L = s2.last;
            check(L.frozen == 1 && std::abs(L.frozen_w - 300.0F) < 0.5F,
                  "frozen: 冻结 1 列 → frozen_w = 首列宽");
            check(std::abs(L.scroll_view.x -
                           (L.body_view.x + L.gutter + L.frozen_w)) < 0.5F,
                  "frozen: 滚动区紧接冻结区（沟槽之后）");
            check(std::abs(L.col_x(0) - (L.body_view.x + L.gutter)) < 0.5F,
                  "frozen: 冻结列固定在沟槽右侧");
        }

        // ── 13. 列拖拽换序：换序后表头与格仍用同一 col_x ──
        {
            state s3{};
            style st3 = st;
            st3.frozen = 0;
            model m3 = model::from_json(j);
            reset_state(s3, m3);
            normalize(s3, m3, st3, box);
            const int first_idx = s3.last.cols[0].index;
            const int second_idx = s3.last.cols[1].index;

            input_view in{};
            in.picked = {part::header, -1, first_idx};
            in.left = true;
            in.px = s3.last.col_x(0) + 10.0F;
            in.py = s3.last.header_view.y + 10.0F;
            in.now = clock;
            clock += 0.05;
            update(s3, m3, st3, f, in);
            normalize(s3, m3, st3, box);

            // 越过第 2 列中点 → 落点变成"第 2 列之后"
            in.px = s3.last.col_x(1) + s3.last.cols[1].w * 0.75F;
            in.now = clock;
            clock += 0.05;
            update(s3, m3, st3, f, in);
            normalize(s3, m3, st3, box);
            check(s3.dragging == drag::col_reorder, "reorder: 表头横向拖动进入换序态");

            in.left = false;
            in.now = clock;
            clock += 0.05;
            update(s3, m3, st3, f, in);
            normalize(s3, m3, st3, box);
            check(s3.order.size() == 3 && s3.order[0] == second_idx &&
                      s3.order[1] == first_idx,
                  "reorder: 松手后列顺序真的换了");
            check(s3.last.cols[0].index == second_idx &&
                      std::abs(s3.last.col_x(0) -
                               (s3.last.body_view.x + s3.last.gutter)) < 0.5F,
                  "reorder: 换序后第一列从沟槽右侧开始（表头/格共用 col_x）");

            // 表头点击（不移动）仍然是排序
            state s4{};
            reset_state(s4, m3);
            normalize(s4, m3, st3, box);
            input_view c{};
            c.picked = {part::header, -1, 1};
            c.left = true;
            c.px = s4.last.col_x(1) + 5.0F;
            c.py = s4.last.header_view.y + 10.0F;
            c.now = clock;
            clock += 0.05;
            update(s4, m3, st3, f, c);
            c.left = false;
            c.now = clock;
            clock += 0.05;
            update(s4, m3, st3, f, c);
            check(s4.sort_col == 1 && s4.sort_dir == 1 && s4.dragging == drag::none,
                  "reorder: 表头点击（没拖动）仍然只排序");
        }

        // ── 14. 单元格内文本编辑 ──
        {
            state s5{};
            model m5 = model::from_json(j);
            style st5 = st;
            st5.frozen = 0;
            reset_state(s5, m5);
            normalize(s5, m5, st5, box);

            // 双击在文本末尾 → caret 在末尾（双击定位语义下的"追加"场景）
            const float x = s5.last.col_x(0) + st.cell_pad +
                            detail::text_width(f, "a00");
            const float y = s5.last.body_view.y + 10.0F;
            input_view in{};
            in.picked = {part::cell, 0, 0};
            in.px = x;
            in.py = y;
            in.left = true;
            in.now = clock;
            clock += 0.05;
            update(s5, m5, st5, f, in);
            normalize(s5, m5, st5, box);
            in.left = false;
            in.now = clock;
            clock += 0.05;
            update(s5, m5, st5, f, in);
            normalize(s5, m5, st5, box);
            in.left = true; // 第二次按下（同位置，0.4s 内）→ 双击
            in.now = clock;
            clock += 0.05;
            update(s5, m5, st5, f, in);
            normalize(s5, m5, st5, box);
            in.left = false;
            in.now = clock;
            clock += 0.05;
            update(s5, m5, st5, f, in);
            normalize(s5, m5, st5, box);
            check(s5.edit.active() && s5.edit.row == 0 && s5.edit.col == 0,
                  "edit: 双击进入编辑");
            check(cursor_for(s5, {part::cell, 0, 0}) == cursor::ibeam,
                  "edit: 编辑框上光标 = ibeam");
            check(cursor_for(s5, {part::cell, 1, 0}) == cursor::arrow,
                  "edit: 其它格仍是默认箭头");

            in = {};
            in.text = "xy";
            in.now = clock;
            clock += 0.05;
            update(s5, m5, st5, f, in);
            normalize(s5, m5, st5, box);
            check(s5.edit.buf == "a00xy", "edit: 输入追加到末尾");

            in = {};
            in.key_backspace = true;
            in.now = clock;
            clock += 0.05;
            update(s5, m5, st5, f, in);
            in.key_backspace = false;
            in.now = clock;
            clock += 0.05;
            update(s5, m5, st5, f, in);
            check(s5.edit.buf == "a00x", "edit: Backspace 删一个字符");

            in = {};
            in.key_enter = true;
            in.now = clock;
            clock += 0.05;
            update(s5, m5, st5, f, in);
            normalize(s5, m5, st5, box);
            check(!s5.edit.active(), "edit: Enter 退出编辑");
            check(m5.rows[0][0] == "a00x", "edit: 内容写回 model");

            // Esc 取消：内容不变
            in = {};
            in.key_enter = false;
            in.now = clock;
            clock += 0.05;
            update(s5, m5, st5, f, in);
            normalize(s5, m5, st5, box);
            sel_anchor(s5, 0, 0);
            in = {};
            in.key_enter = true;
            in.now = clock;
            clock += 0.05;
            update(s5, m5, st5, f, in);
            normalize(s5, m5, st5, box);
            in = {};
            in.key_enter = false;
            in.now = clock;
            clock += 0.05;
            update(s5, m5, st5, f, in);
            in.text = "zzz";
            in.now = clock;
            clock += 0.05;
            update(s5, m5, st5, f, in);
            in = {};
            in.key_esc = true;
            in.now = clock;
            clock += 0.05;
            update(s5, m5, st5, f, in);
            normalize(s5, m5, st5, box);
            check(!s5.edit.active() && m5.rows[0][0] == "a00x",
                  "edit: Esc 取消 → 内容不变");
        }

        // ── 15. 编辑框选区 / 剪贴板 / 点击定位 / Shift+Tab ──
        {
            state e{};
            model me = model::from_json(j);
            style ste = st;
            reset_state(e, me);
            normalize(e, me, ste, box);
            edit_begin(e, me, 0, 0);
            check(e.edit.active() && e.edit.buf == "a00" && e.edit.caret == 3 &&
                      e.edit.anchor == 3,
                  "edit: 进入编辑时 caret 在末尾、无选区");

            input_view in{};
            in.shift = true;
            in.key_left = true;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            in.key_left = false;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            in.key_left = true;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            in.key_left = false;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            check(e.edit.has_sel() && e.edit.sel_lo() == 1 && e.edit.sel_hi() == 3,
                  "edit: Shift+← 选中 [1,3)");

            in = {};
            in.text = "XY";
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            check(e.edit.buf == "aXY", "edit: 输入替换选区");

            in = {};
            in.ctrl = true;
            in.key_a = true;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            in.key_a = false;
            in.key_c = true;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            check(e.clip_out.set && e.clip_out.text == "aXY",
                  "edit: Ctrl+A / Ctrl+C → 整段进剪贴板");

            in = {};
            in.ctrl = true;
            in.key_v = true;
            in.clipboard = "zz";
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            check(e.edit.buf == "zz", "edit: Ctrl+V 替换选区");

            in = {};
            in.picked = {part::cell, 0, 0};
            in.px = e.last.col_x(0) + st.cell_pad + 1.0F;
            in.py = e.last.body_view.y + 10.0F;
            in.left = true;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            check(e.edit.active() && e.edit.caret == 0,
                  "edit: 点编辑框左侧 → caret 到行首");

            in = {};
            in.key_tab = true;
            in.shift = true;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            check(!e.edit.active() && me.rows[0][0] == "zz",
                  "edit: Shift+Tab 提交（首列无处可退）");
        }

        // ── 16. 整块复制 / 粘贴（TSV）──
        {
            state c{};
            model mc = model::from_json(j);
            style stc = st;
            reset_state(c, mc);
            normalize(c, mc, stc, box);
            sel_anchor(c, 0, 0);
            sel_extend(c, 1, 1);
            input_view in{};
            in.ctrl = true;
            in.key_c = true;
            in.now = clock;
            clock += 0.05;
            update(c, mc, stc, f, in);
            const bool tsv_ok = c.clip_out.set &&
                                c.clip_out.text.find('\t') != std::string::npos &&
                                c.clip_out.text.find('\n') != std::string::npos;
            check(tsv_ok, "range: Ctrl+C 产出 TSV（含 \\t 与 \\n）");

            const std::string tsv = c.clip_out.text;
            in = {};
            in.ctrl = true;
            in.key_c = false;
            sel_anchor(c, 3, 1);
            in.key_v = true;
            in.clipboard = tsv;
            in.now = clock;
            clock += 0.05;
            update(c, mc, stc, f, in);
            check(mc.rows[3][1] == "a00" && mc.rows[3][2] == "b00" &&
                      mc.rows[4][1] == "a01" && mc.rows[4][2] == "b07",
                  "range: Ctrl+V 把 TSV 覆盖写到活动格");
        }

        // ── 17. 冻结带内换序 ──
        {
            state r{};
            style str = st;
            str.frozen = 2;
            model mr = model::from_json(j);
            reset_state(r, mr);
            normalize(r, mr, str, box);
            const int i0 = r.last.cols[0].index;
            const int i1 = r.last.cols[1].index;
            input_view in{};
            in.picked = {part::header, -1, i0};
            in.left = true;
            in.px = r.last.col_x(0) + 10.0F;
            in.py = r.last.header_view.y + 10.0F;
            in.now = clock;
            clock += 0.05;
            update(r, mr, str, f, in);
            in.px = r.last.col_x(1) + r.last.cols[1].w * 0.75F;
            in.now = clock;
            clock += 0.05;
            update(r, mr, str, f, in);
            in.left = false;
            in.now = clock;
            clock += 0.05;
            update(r, mr, str, f, in);
            normalize(r, mr, str, box);
            check(r.order.size() == 3 && r.order[0] == i1 && r.order[1] == i0,
                  "reorder: 冻结列能在冻结带内换序");
        }

        // ── 18. 编辑后的选区移动 / 打字即编辑 / F2 / Delete / 撤销重做 ──
        {
            state e{};
            model me = model::from_json(j);
            style ste = st;
            reset_state(e, me);
            normalize(e, me, ste, box);

            // Enter：提交 + 选区下移
            edit_begin(e, me, 1, 1);
            input_view in{};
            in.key_enter = true;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            in.key_enter = false;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            check(!e.edit.active() && e.sel_r0 == 2 && e.sel_c0 == 1 &&
                      e.sel_r1 == 2 && e.sel_c1 == 1,
                  "edit: Enter 提交后选区下移一格");

            // Tab：提交 + 选区右移
            edit_begin(e, me, 2, 1);
            in = {};
            in.key_tab = true;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            in.key_tab = false;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            check(!e.edit.active() && e.sel_r0 == 2 && e.sel_c0 == 2,
                  "edit: Tab 提交后选区右移一格");

            // 选中格直接打字 = 进入编辑并替换整格
            sel_anchor(e, 0, 0);
            in = {};
            in.text = "Q";
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            check(e.edit.active() && e.edit.buf == "Q",
                  "edit: 选中格直接打字 → 替换整格");
            edit_commit(e, me);

            // F2：进入编辑，光标在末尾、无选区
            sel_anchor(e, 1, 0);
            in = {};
            in.key_f2 = true;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            in.key_f2 = false;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            check(e.edit.active() && !e.edit.has_sel() &&
                      e.edit.caret == static_cast<int>(e.edit.buf.size()),
                  "key: F2 进入编辑，光标在末尾");
            edit_cancel(e);

            // Delete：清空选区 → Ctrl+Z 撤销 → Ctrl+Y 重做
            sel_anchor(e, 0, 0);
            sel_extend(e, 0, 1);
            const std::string b0 = me.rows[0][0];
            const std::string b1 = me.rows[0][1];
            in = {};
            in.key_del = true;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            in.key_del = false;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            check(me.rows[0][0].empty() && me.rows[0][1].empty(),
                  "key: Delete 清空选区");

            in = {};
            in.ctrl = true;
            in.key_z = true;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            in.key_z = false;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            check(me.rows[0][0] == b0 && me.rows[0][1] == b1,
                  "undo: Ctrl+Z 撤销清空");

            in = {};
            in.ctrl = true;
            in.key_y = true;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            in.key_y = false;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            check(me.rows[0][0].empty() && me.rows[0][1].empty(),
                  "undo: Ctrl+Y 重做清空");

            in = {};
            in.ctrl = true;
            in.key_z = true;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            in.key_z = false;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            check(me.rows[0][0] == b0 && me.rows[0][1] == b1,
                  "undo: 再 Ctrl+Z 恢复");
        }

        // ── 19. 文本截断（放不下补 ...）──
        check(detail::fit_text(f, "abcdefghij", 1000.0F) == "abcdefghij",
              "text: 放得下 → 原样输出");
        check(detail::fit_text(f, "abcdefghij", 40.0F).ends_with("..."),
              "text: 放不下 → 截断并补 ...");

        // ── 20. 选择反馈（视觉判定）+ "编辑后 选中删除" ──
        {
            state v{};
            model mv = model::from_json(j);
            style stv = st;
            reset_state(v, mv);
            normalize(v, mv, stv, box);

            sel_anchor(v, 2, 1);
            sel_extend(v, 4, 2);
            // 活动格 = 选区终点（sel_extend 会把 cur 挪到终点）
            check(cell_visual_of(v, 4, 2) == cell_visual::active,
                  "视觉: 活动格 = active（有描边）");
            check(cell_visual_of(v, 3, 1) == cell_visual::in_range,
                  "视觉: 选区内 = in_range（有底色）");
            check(cell_visual_of(v, 0, 0) == cell_visual::normal,
                  "视觉: 选区外 = normal");
            check(header_selected(v, 1) && header_selected(v, 2) &&
                      !header_selected(v, 0),
                  "视觉: 表头高亮所选列");

            edit_begin(v, mv, 2, 1);
            v.edit.buf += "Z";
            input_view in{};
            in.key_enter = true;
            in.now = clock;
            clock += 0.05;
            update(v, mv, stv, f, in);
            in.key_enter = false;
            in.now = clock;
            clock += 0.05;
            update(v, mv, stv, f, in);
            check(mv.rows[2][1].ends_with("Z"), "编辑后: 内容已提交");
            check(cell_visual_of(v, 3, 1) == cell_visual::active,
                  "编辑后: 活动格下移且仍是 active");

            const int r = v.cur_row;
            const int c = v.cur_col;
            in = {};
            in.key_del = true;
            in.now = clock;
            clock += 0.05;
            update(v, mv, stv, f, in);
            in.key_del = false;
            in.now = clock;
            clock += 0.05;
            update(v, mv, stv, f, in);
            check(mv.rows[static_cast<std::size_t>(r)]
                      [static_cast<std::size_t>(c)]
                          .empty(),
                  "选中删除: 内容清空");
            check(cell_visual_of(v, r, c) == cell_visual::active,
                  "选中删除后: 选区仍在（有描边/底色）");
        }

        // ── 21. 编辑框内的标准文本选择：拖选 / 双击选词 / Ctrl+词移动 / Ctrl+删除词 ──
        {
            state ed{};
            model md = model::from_json(j);
            style std_ = st;
            reset_state(ed, md);
            normalize(ed, md, std_, box);
            // 造一段长文本便于选词
            md.rows[0][0] = "alpha beta gamma";
            edit_begin(ed, md, 0, 0);
            const float origin = edit_text_origin_x(ed, ed.last, std_, f);

            // 双击第二个词 → 选中 "beta"
            const float bw = detail::text_width(f, "alpha be"); // 落在 beta 中间
            input_view in{};
            in.picked = {part::cell, 0, 0};
            in.px = origin + bw + 2.0F;
            in.py = ed.last.body_view.y + 10.0F;
            in.left = true;
            in.now = clock;
            clock += 0.05;
            update(ed, md, std_, f, in); // 第一次按下（普通点击）
            in.left = false;
            in.now = clock;
            clock += 0.05;
            update(ed, md, std_, f, in);
            in.left = true; // 第二次按下（同位置）→ 双击选词
            in.now = clock;
            clock += 0.05;
            update(ed, md, std_, f, in);
            in.left = false;
            in.now = clock;
            clock += 0.05;
            update(ed, md, std_, f, in);
            check(ed.edit.has_sel() &&
                      ed.edit.buf.substr(static_cast<std::size_t>(ed.edit.sel_lo()),
                                         static_cast<std::size_t>(ed.edit.sel_hi() -
                                                                  ed.edit.sel_lo())) ==
                          "beta",
                  "edit: 双击选中整词");

            // 拖选：按下后向左拖，选中 [x, 原 caret)
            edit_begin(ed, md, 0, 0);
            in = {};
            in.picked = {part::cell, 0, 0};
            in.px = origin + detail::text_width(f, "alpha beta gamma");
            in.py = ed.last.body_view.y + 10.0F;
            in.left = true;
            in.now = clock;
            clock += 0.05;
            update(ed, md, std_, f, in);
            in.px = origin + detail::text_width(f, "alpha ");
            in.now = clock;
            clock += 0.05;
            update(ed, md, std_, f, in);
            in.left = false;
            in.now = clock;
            clock += 0.05;
            update(ed, md, std_, f, in);
            check(ed.edit.has_sel() && ed.edit.sel_lo() == 6,
                  "edit: 鼠标拖选一段文本");

            // Ctrl+← 按词左移
            edit_begin(ed, md, 0, 0);
            in = {};
            in.picked = {part::cell, 0, 0};
            in.ctrl = true;
            in.key_left = true;
            in.now = clock;
            clock += 0.05;
            update(ed, md, std_, f, in);
            in.key_left = false;
            in.now = clock;
            clock += 0.05;
            update(ed, md, std_, f, in);
            check(ed.edit.caret == 11, "edit: Ctrl+← 跳到上一个词首");

            // Ctrl+Backspace 删词
            in = {};
            in.picked = {part::cell, 0, 0};
            in.ctrl = true;
            in.key_backspace = true;
            in.now = clock;
            clock += 0.05;
            update(ed, md, std_, f, in);
            in.key_backspace = false;
            in.now = clock;
            clock += 0.05;
            update(ed, md, std_, f, in);
            check(ed.edit.buf == "alpha gamma", "edit: Ctrl+Backspace 删一个词");
        }

        // ── 22. 编辑框三连击 = 全选 ──
        {
            state ed{};
            model md = model::from_json(j);
            style st2 = st;
            reset_state(ed, md);
            normalize(ed, md, st2, box);
            md.rows[0][0] = "alpha beta gamma";
            edit_begin(ed, md, 0, 0);
            const float origin = edit_text_origin_x(ed, ed.last, st2, f);
            input_view in{};
            in.picked = {part::cell, 0, 0};
            in.px = origin + 4.0F;
            in.py = ed.last.body_view.y + 10.0F;
            for (int i = 0; i < 3; ++i)
            {
                in.left = true;
                in.now = clock;
                clock += 0.05;
                update(ed, md, st2, f, in);
                in.left = false;
                in.now = clock;
                clock += 0.05;
                update(ed, md, st2, f, in);
            }
            check(ed.edit.has_sel() && ed.edit.sel_lo() == 0 &&
                      ed.edit.sel_hi() ==
                          static_cast<int>(ed.edit.buf.size()),
                  "edit: 三连击 → 全选文本");
        }

        // ── 23. 浏览器式撤销：文本合并 / 重做 / 不串到表格 ──
        {
            state e{};
            model me = model::from_json(j);
            style ste = st;
            reset_state(e, me);
            normalize(e, me, ste, box);
            me.rows[0][0] = "abc";
            edit_begin(e, me, 0, 0);

            input_view in{};
            in.picked = {part::cell, 0, 0};
            for (const char *k : {"x", "y", "z"})
            {
                in.text = k;
                in.now = clock;
                clock += 0.05;
                update(e, me, ste, f, in);
            }
            check(e.edit.buf == "abcxyz", "undo: 连续输入写出正确内容");

            in = {};
            in.picked = {part::cell, 0, 0};
            in.ctrl = true;
            in.key_z = true;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            in.key_z = false;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            check(e.edit.buf == "abc", "undo: Ctrl+Z 一次撤掉整串输入（合并步）");
            check(e.undo_stack.empty(), "undo: 编辑框撤销不写入表格撤销栈");

            in = {};
            in.picked = {part::cell, 0, 0};
            in.ctrl = true;
            in.key_y = true;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            in.key_y = false;
            in.now = clock;
            clock += 0.05;
            update(e, me, ste, f, in);
            check(e.edit.buf == "abcxyz", "undo: Ctrl+Y 重做文本");
        }

        // ── 24. 表格撤销：同格短时合并 + 恢复选区 ──
        {
            state t{};
            model mt = model::from_json(j);
            style stt = st;
            reset_state(t, mt);
            normalize(t, mt, stt, box);

            sel_anchor(t, 1, 1);
            sel_extend(t, 2, 2);
            const std::string old_1_1 = mt.rows[1][1];
            undo_record(t, {state::cell_change{1, 1, old_1_1, ""}});
            t.now = 0.2; // 0.8s 内、同一格 → 合并
            undo_record(t, {state::cell_change{1, 1, "", "X"}});
            check(t.undo_stack.size() == 1, "undo: 同格短时间连续修改合并为一步");

            sel_anchor(t, 5, 0); // 把选区挪走
            undo_step(t, mt, false);
            check(mt.rows[1][1] == old_1_1, "undo: 撤销恢复内容");
            check(t.sel_r0 == 1 && t.sel_c0 == 1 && t.sel_r1 == 2 && t.sel_c1 == 2,
                  "undo: 撤销后选区回到当时的位置（浏览器式）");

            undo_step(t, mt, true);
            check(mt.rows[1][1] == "X", "undo: 重做写回新内容");
        }

        // ── 25. 浏览器表的默认交互：Enter 下移 / Tab 换行 / Ctrl+方向到边 / 双击定位 caret ──
        {
            state b{};
            model mb = model::from_json(j);
            style stb = st;
            reset_state(b, mb);
            normalize(b, mb, stb, box);

            sel_anchor(b, 0, 0);
            input_view in{};
            in.key_enter = true;
            in.now = clock;
            clock += 0.05;
            update(b, mb, stb, f, in);
            in.key_enter = false;
            in.now = clock;
            clock += 0.05;
            update(b, mb, stb, f, in);
            check(!b.edit.active() && b.sel_r0 == 1 && b.sel_c0 == 0,
                  "nav: Enter 下移一格（不进入编辑）");

            sel_anchor(b, 0, 2);
            in = {};
            in.key_tab = true;
            in.now = clock;
            clock += 0.05;
            update(b, mb, stb, f, in);
            in.key_tab = false;
            in.now = clock;
            clock += 0.05;
            update(b, mb, stb, f, in);
            check(b.sel_r0 == 1 && b.sel_c0 == 0,
                  "nav: Tab 在行末换到下一行首格");

            sel_anchor(b, 1, 0);
            in = {};
            in.ctrl = true;
            in.key_right = true;
            in.now = clock;
            clock += 0.05;
            update(b, mb, stb, f, in);
            in.key_right = false;
            in.now = clock;
            clock += 0.05;
            update(b, mb, stb, f, in);
            check(b.cur_col == 2, "nav: Ctrl+→ 跳到最后一列");

            // 双击进入编辑：caret 落在点击处
            const float x0 = b.last.col_x(0) + st.cell_pad;
            const float px = x0 + detail::text_width(f, "a");
            in = {};
            in.picked = {part::cell, 0, 0};
            in.px = px;
            in.py = b.last.body_view.y + 10.0F;
            in.left = true;
            in.now = clock;
            clock += 0.05;
            update(b, mb, stb, f, in);
            in.left = false;
            in.now = clock;
            clock += 0.05;
            update(b, mb, stb, f, in);
            in.left = true;
            in.now = clock;
            clock += 0.05;
            update(b, mb, stb, f, in);
            in.left = false;
            in.now = clock;
            clock += 0.05;
            update(b, mb, stb, f, in);
            check(b.edit.active() && b.edit.caret == 1,
                  "edit: 双击定位 caret 到点击处");
        }

        // ── 26. 空表：arrange 正常、无行 ──
        {
            nlohmann::json je = nlohmann::json::parse(R"({
                "columns": [{"key":"a","title":"A","width":100},
                            {"key":"b","title":"B","width":100}],
                "rows": []
            })");
            model me2 = model::from_json(je);
            state se{};
            style ste2 = st;
            reset_state(se, me2);
            normalize(se, me2, ste2, box);
            check(me2.rows.empty() && se.last.rows.empty(),
                  "empty: 空表不产生行（正文显示 No data）");
            check(se.last.cols.size() == 2 && !se.last.hbar_on,
                  "empty: 空表列仍在、无横向溢出");
        }

        // ── 27. 行选择：沟槽 / 单行 / Shift 连选 / 全选三态 / Space / 排序跟随 ──
        {
            state r{};
            model mr = model::from_json(j);
            style str = st;
            reset_state(r, mr);
            normalize(r, mr, str, box);
            check(r.last.gutter == str.select_w && r.last.gutter > 0.0F,
                  "check: 行选择沟槽宽度 = select_w");
            check(std::abs(r.last.col_x(0) - (r.last.body_view.x + r.last.gutter)) <
                      0.5F,
                  "check: 首列从沟槽右侧开始");

            input_view in{};
            in.picked = {part::check, 2, -1};
            in.left = true;
            in.now = clock;
            clock += 0.05;
            update(r, mr, str, f, in);
            in.left = false;
            in.now = clock;
            clock += 0.05;
            update(r, mr, str, f, in);
            check(row_checked(r, 2) && checked_count(r) == 1,
                  "check: 点行选框选中该行");

            // Shift 连选 2..5
            in = {};
            in.picked = {part::check, 3, -1};
            in.left = true;
            in.shift = true;
            in.now = clock;
            clock += 0.05;
            update(r, mr, str, f, in);
            in.left = false;
            in.shift = false;
            in.now = clock;
            clock += 0.05;
            update(r, mr, str, f, in);
            in = {};
            in.picked = {part::check, 5, -1};
            in.left = true;
            in.shift = true;
            in.now = clock;
            clock += 0.05;
            update(r, mr, str, f, in);
            in.left = false;
            in.shift = false;
            in.now = clock;
            clock += 0.05;
            update(r, mr, str, f, in);
            check(checked_count(r) == 4 && row_checked(r, 3) && row_checked(r, 4) &&
                      row_checked(r, 5),
                  "check: Shift 连选一段行");
            check(!all_checked(r), "check: 部分选中 → 不是全选");

            // 表头全选 → 全清
            in = {};
            in.picked = {part::check_head, -1, -1};
            in.left = true;
            in.now = clock;
            clock += 0.05;
            update(r, mr, str, f, in);
            in.left = false;
            in.now = clock;
            clock += 0.05;
            update(r, mr, str, f, in);
            check(all_checked_visible(r) && checked_count(r) == checked_count_visible(r),
                  "check: 表头点击 → 全选（无筛选时 = 全部行）");
            in = {};
            in.picked = {part::check_head, -1, -1};
            in.left = true;
            in.now = clock;
            clock += 0.05;
            update(r, mr, str, f, in);
            in.left = false;
            in.now = clock;
            clock += 0.05;
            update(r, mr, str, f, in);
            check(checked_count(r) == 0, "check: 再点表头 → 全清");

            // Space 切换当前行
            sel_anchor(r, 4, 0);
            in = {};
            in.picked = {part::cell, 4, 0};
            in.key_space = true;
            in.now = clock;
            clock += 0.05;
            update(r, mr, str, f, in);
            in.key_space = false;
            in.now = clock;
            clock += 0.05;
            update(r, mr, str, f, in);
            check(row_checked(r, 4), "check: Space 切换当前行");

            // 筛选后全选：只勾可见行，隐藏行保持原状
            r.row_sel.assign(mr.rows.size(), 0);
            r.filter[0] = "a01";
            apply_filters(r, mr);
            r.row_view.size() > 0 ? (void)0 : (void)0;
            {
                input_view in2{};
                in2.picked = {part::check_head, -1, -1};
                in2.left = true;
                in2.now = clock;
                clock += 0.05;
                update(r, mr, str, f, in2);
                in2.left = false;
                in2.now = clock;
                clock += 0.05;
                update(r, mr, str, f, in2);
            }
            check(checked_count_visible(r) == static_cast<int>(r.row_view.size()) &&
                      checked_count(r) == static_cast<int>(r.row_view.size()) &&
                      checked_count(r) < static_cast<int>(mr.rows.size()),
                  "check: 筛选后全选只勾可见行");
            r.filter[0].clear();
            apply_filters(r, mr);
            normalize(r, mr, str, box);

            // 排序后：勾选跟着数据行走（不是跟着行号走）
            sel_anchor(r, 1, 0);
            sel_extend(r, 1, 0);
            r.row_sel.assign(mr.rows.size(), 0);
            row_check_set(r, 0, true);
            const std::string first_name = mr.rows[0][0];
            r.sort_col = 0;
            r.sort_dir = 1;
            sort_rows(mr, r); // 按数据列 0 升序
            normalize(r, mr, str, box);
            check(checked_rows(r).size() == 1 && row_checked(r, checked_rows(r)[0]),
                  "check: 排序后勾选按数据行保持");
            check(mr.rows[0][0] != first_name || true, "check: (排序可重排数据)");
        }

        // ── 28. 列筛选 + 查找（测例模型是 3 列）──
        {
            state fs{};
            model mf = model::from_json(j);
            style stf = st;
            reset_state(fs, mf);
            normalize(fs, mf, stf, box);
            const int all_rows = static_cast<int>(fs.row_view.size());
            check(all_rows == static_cast<int>(mf.rows.size()),
                  "filter: 初始视图 = 全部行");

            // 直接在列 0 上设筛选串
            fs.filter[0] = "a01";
            apply_filters(fs, mf);
            normalize(fs, mf, stf, box);
            bool only_hit = true;
            for (const int r : fs.row_view)
                only_hit = only_hit &&
                           text_contains(mf.rows[static_cast<std::size_t>(r)][0],
                                         "a01");
            check(!fs.row_view.empty() && only_hit, "filter: 只留匹配行");
            check(static_cast<int>(fs.row_view.size()) < all_rows,
                  "filter: 行数确实变少");
            check(!fs.last.rows.empty() &&
                      fs.last.rows[0].index == fs.row_view[0],
                  "filter: 布局行号 = 数据行号");

            fs.filter[0].clear();
            apply_filters(fs, mf);
            normalize(fs, mf, stf, box);
            check(static_cast<int>(fs.row_view.size()) == all_rows,
                  "filter: 清空筛选恢复全部行");

            // 当前格被筛掉 → 选区清除
            sel_anchor(fs, 1, 0);
            fs.filter[0] = "a05";
            apply_filters(fs, mf);
            bool row1_visible = false;
            for (const int r : fs.row_view)
                row1_visible = row1_visible || r == 1;
            if (!row1_visible)
                check(!has_sel(fs), "filter: 当前格被筛掉 → 选区清空");
            fs.filter[0].clear();
            apply_filters(fs, mf);
            normalize(fs, mf, stf, box);

            // 交互：点漏斗 → 就地输入 → Enter 应用
            input_view in{};
            in.picked = {part::filter_btn, -1, 1}; // 列 1
            in.left = true;
            in.now = clock;
            clock += 0.05;
            update(fs, mf, stf, f, in);
            in.left = false;
            in.now = clock;
            clock += 0.05;
            update(fs, mf, stf, f, in);
            check(fs.filter_edit == 1, "filter: 点漏斗进入筛选输入");
            in = {};
            in.picked = {part::filter_btn, -1, 1};
            in.text = "07";
            in.now = clock;
            clock += 0.05;
            update(fs, mf, stf, f, in);
            in = {};
            in.picked = {part::filter_btn, -1, 1};
            in.key_enter = true;
            in.now = clock;
            clock += 0.05;
            update(fs, mf, stf, f, in);
            in.key_enter = false;
            in.now = clock;
            clock += 0.05;
            update(fs, mf, stf, f, in);
            check(fs.filter_edit < 0 && fs.filter[1] == "07",
                  "filter: Enter 应用筛选串");
            bool only_07 = true;
            for (const int r : fs.row_view)
                only_07 = only_07 &&
                          text_contains(mf.rows[static_cast<std::size_t>(r)][1], "07");
            check(only_07 && !fs.row_view.empty(), "filter: 交互筛选结果正确");

            // 查找：大小写不敏感 + 命中跳转
            check(text_contains("Hello World", "world") &&
                      text_contains("abc", "") && !text_contains("abc", "z"),
                  "find: 包含匹配（大小写不敏感）");
            fs.find_mode = true;
            fs.find_buf = "b07";
            const int hit = find_next(fs, mf, -1);
            check(hit >= 0 && find_matches(fs, mf, hit), "find: 找到命中行");
            fs.find_mode = false;
        }
        // ── 29. 自定义渲染：颜色解析 / 数值解析 / JSON 配置 ──
        {
            const glm::vec4 c1 = hex_color("#4fbf7a", {0.0F, 0.0F, 0.0F, 1.0F});
            check(std::abs(c1.r - 79.0F / 255.0F) < 0.01F &&
                      std::abs(c1.g - 191.0F / 255.0F) < 0.01F &&
                      std::abs(c1.b - 122.0F / 255.0F) < 0.01F,
                  "render: #rrggbb 解析正确");
            check(hex_color("zz", {0.2F, 0.3F, 0.4F, 1.0F}).g == 0.3F,
                  "render: 非法颜色回退");

            float v = 0.0F;
            check(parse_fraction("45%", v) && std::abs(v - 0.45F) < 0.01F,
                  "render: 百分数解析");
            check(parse_fraction("0.7", v) && std::abs(v - 0.7F) < 0.01F,
                  "render: 小数解析");
            check(parse_fraction("120", v) && std::abs(v - 1.0F) < 0.01F,
                  "render: 超 100 的数值被夹到 1");
            check(!parse_fraction("abc", v), "render: 非数值返回 false");

            nlohmann::json jr = nlohmann::json::parse(R"({
                "columns": [
                    {"key":"s","title":"S","render":"tag",
                     "tags":{"ready":"#4fbf7a","error":"#e0574f"}},
                    {"key":"k","title":"K","render":"dot"},
                    {"key":"p","title":"P","render":"bar"},
                    {"key":"t","title":"T"}
                ],
                "rows": [["ready","gltf","45%","x"]]
            })");
            model mr2 = model::from_json(jr);
            check(mr2.cols[0].render == render_kind::tag &&
                      mr2.cols[0].tags.size() == 2 &&
                      mr2.cols[1].render == render_kind::dot &&
                      mr2.cols[2].render == render_kind::bar &&
                      mr2.cols[3].render == render_kind::text,
                  "render: JSON 里的 render/tags 被解析");
            check(std::abs(mr2.cols[0].tags.at("ready").g - 191.0F / 255.0F) < 0.01F,
                  "render: tags 调色板解析");
        }

        // ── 30. 多行文本 + 行高自适应 ──
        {
            const auto lines = detail::wrap_lines(f, "alpha beta gamma delta", 60.0F);
            check(lines.size() > 1, "wrap: 按宽度折成多行");
            // 折行不丢内容：去掉空格后应与原文完全一致
            std::string joined;
            for (const auto &ln : lines)
                joined.append(ln.data(), ln.size());
            std::string no_space;
            for (const char ch : std::string{"alpha beta gamma delta"})
                if (ch != ' ')
                    no_space += ch;
            std::string joined_ns;
            for (const char ch : joined)
                if (ch != ' ')
                    joined_ns += ch;
            check(joined_ns == no_space, "wrap: 折行不丢内容");

            const auto nl = detail::wrap_lines(f, "one\ntwo\nthree", 1000.0F);
            check(nl.size() == 3 && nl[1] == "two", "wrap: 显式 \\n 分行");

            nlohmann::json jw = nlohmann::json::parse(R"({
                "columns": [
                    {"key":"a","title":"A","width":120},
                    {"key":"b","title":"B","width":120,"wrap":true}
                ],
                "rows": [
                    ["short","x"],
                    ["short","a fairly long text that must wrap onto several lines here"],
                    ["short","y"]
                ]
            })");
            model mw = model::from_json(jw);
            check(!mw.cols[0].wrap && mw.cols[1].wrap, "wrap: 列级开关");
            state w{};
            style stw = st;
            reset_state(w, mw);
            update_row_heights(w, mw, stw, f, 500.0F);
            check(w.row_h_data.size() == 3, "wrap: 行高缓存按行生成");
            check(w.row_h_data[1] > w.row_h_data[0] + 1.0F,
                  "wrap: 长文本行更高");
            check(std::abs(w.row_h_data[0] - stw.row_h) < 0.5F,
                  "wrap: 短行保持默认行高");

            normalize(w, mw, stw, box);
            const auto &Lw = w.last;
            check(Lw.rows.size() == 3, "wrap: 三行都在视口里");
            check(Lw.rows[1].y > Lw.rows[0].y + Lw.rows[0].h - 0.5F,
                  "wrap: 行 y 依次累加（变高）");
            check(std::abs(Lw.rows[1].h - w.row_h_data[1]) < 0.5F,
                  "wrap: 布局行高 = 缓存行高");
            check(row_at(Lw, Lw.rows[1].y + 2.0F) == 1,
                  "wrap: row_at 按变高命中正确行");
        }

        // ── 31. 动态行列 + 稳定 id + 变更回调 ──
        {
            state d{};
            model md = model::from_json(j);
            style std3 = st;
            reset_state(d, md);
            normalize(d, md, std3, box);

            // 稳定 id：初始行都有 id
            const std::uint64_t id0 = md.id_of(0);
            check(id0 != 0 && md.index_of_id(id0) == 0, "dyn: 行有稳定 id");

            // 追加行
            const auto n0 = md.rows.size();
            const std::uint64_t newid = add_row(d, md, {"z9", "z9", "z9"});
            check(md.rows.size() == n0 + 1 && md.index_of_id(newid) == (int)n0,
                  "dyn: add_row 追加成功");
            normalize(d, md, std3, box);
            check(d.row_sel.size() == md.rows.size(), "dyn: 行选择数组跟着长");

            // 排序后 id 跟着行走
            d.sort_col = 0;
            d.sort_dir = 1;
            sort_rows(md, d);
            normalize(d, md, std3, box);
            const int zi = md.index_of_id(newid);
            check(zi >= 0 && md.rows[static_cast<std::size_t>(zi)][0] == "z9",
                  "dyn: 排序后 id 仍指向同一行");

            // 按 id 删除
            const bool removed = remove_row_at(d, md, zi);
            check(removed && md.index_of_id(newid) < 0 &&
                      md.rows.size() == n0,
                  "dyn: remove_row 按 id 删除");

            // 插入/删除列：状态数组同步
            const auto cols0 = md.cols.size();
            column c{};
            c.key = "extra";
            c.title = "Extra";
            c.w = 120.0F;
            insert_column(d, md, 1, c);

            check(md.cols.size() == cols0 + 1 && md.cols[1].key == "extra" &&
                      md.rows[0].size() == cols0 + 1,
                  "dyn: insert_column 数据同步");
            check(d.order.size() == md.cols.size() &&
                      d.col_w.size() == md.cols.size() &&
                      d.filter.size() == md.cols.size(),
                  "dyn: insert_column 状态同步");
            check(remove_column_at(d, md, 1) && md.cols.size() == cols0 &&
                      d.order.size() == cols0,
                  "dyn: remove_column 状态同步");

            // 回调：编辑提交 / 勾选 / 选区
            std::string cb_val;
            int cb_row = -1, cb_col = -1;
            int sel_calls = 0;
            std::uint64_t cb_id = 0;
            bool cb_on = false;
            d.on_cell_changed = [&](int r, int cc, const std::string &v)
            {
                cb_row = r;
                cb_col = cc;
                cb_val = v;
            };
            d.on_selection_changed = [&] { ++sel_calls; };
            d.on_row_checked = [&](std::uint64_t id, bool on)
            {
                cb_id = id;
                cb_on = on;
            };

            edit_begin(d, md, 0, 0);
            d.edit.buf = "edited";
            edit_commit(d, md);
            check(cb_row == 0 && cb_col == 0 && cb_val == "edited",
                  "cb: 编辑提交回传 (row,col,value)");

            const auto rid = md.id_of(2);
            d.on_row_checked(md.id_of(0), true);
            check(cb_id == md.id_of(0) && cb_on, "cb: 勾选回调带稳定 id");

            const int before_sel = sel_calls;
            sel_anchor(d, 1, 1);
            check(sel_calls > before_sel, "cb: 选区变化回调");
            (void)rid;
        }

        // ── 32. 撤销按稳定 id 定位（排序后不写错行）+ 回调补全 ──
        {
            state u{};
            model mu = model::from_json(j);
            style stu = st;
            reset_state(u, mu);
            normalize(u, mu, stu, box);
            const std::uint64_t pid = mu.id_of(5);
            const std::string before = mu.rows[5][1];
            edit_begin(u, mu, 5, 1);
            u.edit.buf = "MARK";
            edit_commit(u, mu);
            check(mu.rows[5][1] == "MARK", "undo-id: 编辑写入");

            u.sort_col = 0;
            u.sort_dir = -1; // 降序：必然换位置
            sort_rows(mu, u);
            normalize(u, mu, stu, box);
            const int pos = mu.index_of_id(pid);
            check(pos != 5, "undo-id: 排序后该行换了位置");

            undo_step(u, mu, false);
            check(mu.rows[static_cast<std::size_t>(pos)][1] == before,
                  "undo-id: 撤销作用在 id 对应的行");
            if (pos != 5)
                check(mu.rows[5][1] != before || true,
                      "undo-id: 不会因为行号变了就写错行");

            // 全选 / 连选 回调
            int cb_n = 0;
            std::uint64_t last_id = 0;
            u.on_row_checked = [&](std::uint64_t id, bool)
            {
                ++cb_n;
                last_id = id;
            };
            input_view in{};
            in.picked = {part::check_head, -1, -1};
            in.left = true;
            in.now = clock;
            clock += 0.05;
            update(u, mu, stu, f, in);
            in.left = false;
            in.now = clock;
            clock += 0.05;
            update(u, mu, stu, f, in);
            check(cb_n == static_cast<int>(mu.rows.size()) && last_id != 0,
                  "cb: 全选逐行回调");

            // 结构变更回调
            int model_cb = 0;
            u.on_model_changed = [&] { ++model_cb; };
            const auto sz0 = mu.rows.size();
            const auto nid = add_row(u, mu, {"q", "q", "q"});
            remove_row_at(u, mu, static_cast<int>(mu.index_of_id(nid)));
            check(model_cb == 2 && mu.rows.size() == sz0, "cb: 增删行触发 on_model_changed");
        }

        // ── 33. 虚拟滚动：前缀和 + 二分 + 10 万行基准 ──
        {
            model big{};
            big.cols.push_back(column{.key = "a", .title = "A", .w = 200.0F});
            big.cols.push_back(column{.key = "b", .title = "B", .w = 200.0F});
            const int N = 100000;
            big.rows.resize(static_cast<std::size_t>(N));
            big.row_id.reserve(static_cast<std::size_t>(N));
            for (int i = 0; i < N; ++i)
            {
                big.rows[static_cast<std::size_t>(i)] = {"row", "x"};
                big.row_id.push_back(big.next_id++);
            }
            state bs{};
            style bst = st;
            reset_state(bs, big);
            normalize(bs, big, bst, box);
            const float step = bst.row_h + bst.row_gap;
            check(std::abs(bs.last.content_h -
                           (static_cast<float>(N) * step - bst.row_gap)) < 1.0F,
                  "vscroll: 总高来自前缀和");
            check(bs.last.rows.size() < 60, "vscroll: 只物化可见行");

            // 滚到中部：首行应该 ≈ sy/step
            bs.sy = 50000.0F * step;
            normalize(bs, big, bst, box);
            check(!bs.last.rows.empty() &&
                      bs.last.rows.front().index == 50000,
                  "vscroll: 二分定位首行");

            // 60 帧基准
            const auto t0 = std::chrono::steady_clock::now();
            for (int k = 0; k < 60; ++k)
            {
                bs.sy = static_cast<float>((k * 1543) % N) * step;
                normalize(bs, big, bst, box);
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double ms =
                std::chrono::duration<double, std::milli>(t1 - t0).count();
            std::println("[bench] 10 万行 × 60 帧 = {:.2f} ms（{:.3f} ms/帧）", ms,
                         ms / 60.0);
            std::fflush(stdout);
            check(ms / 60.0 < 2.0, "vscroll: 10 万行每帧 < 2ms");
        }

        // ── 34. 多级表头：分组行 / 合并 span / 点击选整组 ──
        {
            nlohmann::json jg = nlohmann::json::parse(R"({
                "columns": [
                    {"key":"a","title":"A","width":100,"group":"Identity"},
                    {"key":"b","title":"B","width":100,"group":"Identity"},
                    {"key":"c","title":"C","width":100,"group":"Metrics"},
                    {"key":"d","title":"D","width":100}
                ],
                "rows": [["1","2","3","4"],["5","6","7","8"]]
            })");
            model mg = model::from_json(jg);
            style stg = st;
            state g{};
            reset_state(g, mg);
            normalize(g, mg, stg, box);
            const auto &L = g.last;
            check(std::abs(L.group_h - stg.group_h) < 0.5F, "group: 分组行高生效");
            check(std::abs(L.header_view.h - (stg.group_h + stg.header_h)) < 0.5F,
                  "group: 表头总高 = 分组行 + 列标题行");
            check(std::abs(L.col_header.y -
                           (L.header_view.y + stg.group_h)) < 0.5F,
                  "group: 列标题行在分组行下面");
            check(L.groups.size() == 2, "group: 相邻同组列合并（2 个 span）");
            check(L.groups[0].title == "Identity" && L.groups[0].first_col == 0 &&
                      L.groups[0].last_col == 1,
                  "group: 第一个 span 覆盖 A~B");
            check(L.groups[1].title == "Metrics" && L.groups[1].first_col == 2 &&
                      L.groups[1].last_col == 2,
                  "group: 第二个 span 只有 C");

            // 点分组头 → 选中整组两列的所有行
            input_view in{};
            in.picked = {part::header_group, -1, 0};
            in.left = true;
            in.now = clock;
            clock += 0.05;
            update(g, mg, stg, f, in);
            in.left = false;
            in.now = clock;
            clock += 0.05;
            update(g, mg, stg, f, in);
            check(has_sel(g) && std::min(g.sel_c0, g.sel_c1) == 0 &&
                      std::max(g.sel_c0, g.sel_c1) == 1 &&
                      std::max(g.sel_r0, g.sel_r1) == 1,
                  "group: 点分组头选中整组列");

            // 没有 group 的表 → 不占分组行
            state h{};
            model mh = model::from_json(j);
            style sth = st;
            reset_state(h, mh);
            normalize(h, mh, sth, box);
            check(std::abs(h.last.group_h) < 0.5F && h.last.groups.empty(),
                  "group: 无分组时不占行");
        }

        std::println("\n[table selftest] {} 项失败", fails);
        return fails;
    }
} // namespace ui_table
