# ability11 多实例表格：当前设计文档

> 对象：`test_ui_ability11_table.cpp` / `test_ui_ability11_table.hpp`  
> 目标：单窗口、多张 table，输入/绘制/拾取/生命周期互不干扰；能力 10 的全部表格功能保留。  
> 本文只描述当前代码（不修改代码）。行号为写作时的工作区版本：hpp 6355 行，cpp 14879 行。

---

## 0. 一页结论

这套实现可以概括成：

```text
                 ┌──────────────────────── hpp：纯表格核心 ────────────────────────┐
                 │ table_data + comp::view + prefs                                │
                 │        │ sync                                                  │
                 │        ▼                                                       │
                 │     geometry ──► build_frame ──► frame_data（DL + zone/owner） │
                 │        ▲                              │                        │
                 │        └──────────── hit_target ◄─────┘                        │
                 └────────────────────────────────────────────────────────────────┘
                                          │ encode（cpp 的 table_glue）
                                          ▼
                 shader instance（entity_index = target_key）
                          │ record_builder / replay
                          ▼
                 GPU color + picking attachment
                          │ 同帧 readback（PICKCHECK）
                          ▼
                 picking_result → accept_pick → on_pick(table_id)

输入：
GLFW → glfw_input_translation → table_host::input_event
     → table_arena::handle_event<E> → input_frame → arbiter → gesture → on_*
     → system_event（features / visit_owner）→ data/view + fx

窗口级：
frame_state::tableHost（shared_ptr<table_host>）
ScreenWidget 只存 table_host_ctx 裸指针
每个 Table 节点 = 一个 table_arena
```

真正把两条链合在一起的只有一个键：**`target_key = zone_key(zone)`**。  
绘制侧把它写进 shader 的 `entity_index`，输入侧用同一个 `zone_key` 做 hit-test 与 `resolve`。这就是“编译期可检查的协议 + 运行期唯一合并点”。

当前验收结果：

| 项 | 结果 |
|---|---|
| `TABLE_SELFTEST=1` | 171 ok / 0 failed |
| `TABLE_EVTEST=1` | 32 ok / 0 failed |
| `TABLE_MULTITEST=1` | 71 ok / 0 failed |
| `TABLE_PICKCHECK=1` 真实窗口双表 | 150 ok / 0 mismatch（两张表都有命中） |
| 真实键盘 A/B | Right / repeat / Enter / F2 / Ctrl+F / Esc 通过，Esc 取消 modal 后存活 |

---

## 1. `.hpp` 与 `.cpp` 的职责边界

### 1.1 `test_ui_ability11_table.hpp`：表格核心（平台无关）

文件头注释明确写着：

> 本文只依赖 std + glm；ui_new / glfw / dao / shader 都不认识。  
> make_table / asRenderObject / encode 在 cpp 的宿主层。

它定义 `namespace ui_table`：

| 区域 | 内容 | 代表符号 |
|---|---|---|
| 身份/协议 | table/row/column/cell 身份、zone/target、owner、target_key 位布局 | `table_id`、`row_id`、`zone`、`target`、`zone_key` |
| 数据 | 列主序数据、列定义、行身份 | `table_data`、`column`、`cell` |
| 意图 | 选择/编辑/滚动/筛选/查找/勾选/菜单/tooltip/拖拽 | `comp::view` 与各 `*_state` |
| 样式 | 行高、表头、颜色、最小宽高等 | `prefs` |
| 派生几何 | 列头、可见行、滚动区、冻结、分组、zone_rect、行流水线 | `geometry`、`row_view`、`row_pipeline` |
| 绘制 DL | 4 条 stream 的 shape/text op，带 zone 与 owner | `frame_data`、`shape_op`、`text_op`、`dl` |
| 语义系统 | 15 个 feature + 唯一分派 | `systems`、`table_ctx`、`system_event` |
| 命中/呈现 | hit-test、target_key ↔ target、build_frame | `hit_target`、`build_frame` |
| 自测 | 纯数据/几何/DL/命令自测 | `ui_table::selftest()` |

核心 API：

```cpp
// hpp:4072  data + view + prefs -> geometry
inline void sync(const table_data &d, view &v, const prefs &p, geometry &g,
                 const rect &viewport, const font &ft, systems &sy,
                 row_pipeline &pipe);

// hpp:4756  语义命令 -> feature（没人认领则走 system_gesture）
template <class E>
inline void system_event(table_data &d, view &v, const prefs &p,
                         const geometry &g, systems &sy, const font &ft,
                         const E &ev, std::vector<effect> &fx);

// hpp:4779  屏幕坐标 -> target（输入唯一命中点）
[[nodiscard]] inline target hit_target(const table_data &d, const geometry &g,
                                       const view &v, float x, float y,
                                       table_id tid = {});

// hpp:4785  data + view + geometry -> frame_data（纯 DL）
inline void build_frame(const table_data &d, const view &v, const prefs &p,
                        const geometry &g, const font &ft, const systems &sy,
                        frame_data &out, table_id tid = {});
```

### 1.2 `test_ui_ability11_table.cpp`：宿主/平台层

它包含：

1. **Vulkan / window / command recorder / picking**：`object_key`、`picking_result`、`command::record_builder`、`frame.replay`、主循环。
2. **`ui_new`**：`ScreenWidget`、`Widget`、`Container`、`Row`、`Expanded`、`Flex`、`BoxConstraints` 等布局树。
3. **事件层**：`glfw_input_translation`、`type_erased_invoker`、`input_frame`、`gesture`、`intent` arbiter。
4. **表格宿主**：`table_arena`、`table_host`、`table_spec`、`make_table_screen`。
5. **桥接层**：`table_glue`（`encode`、字体/模型初始化、glfw 光标/IME/clipboard）。
6. **测试和 main**：`event_probe`、`multi_probe`、`main`。

cpp 的 include 顺序也体现了边界（cpp:655-657）：

```cpp
#include "test_ui_ability7_dao.hpp"        // ui_dao::view / sdf_shape / glyph_quad
#include "test_ui_ability7_shader_buffers.hpp"
#include "test_ui_ability11_table.hpp"     // 纯表格核心
```

依赖方向只有：**cpp → hpp**。hpp 不知道 Vulkan、GLFW、`ui_new`、`ui_dao`、shader。

### 1.3 两侧的“接缝”是什么

| 接缝 | hpp 侧 | cpp 侧 |
|---|---|---|
| 数据 | `table_data/prefs/font/view` | `table_arena` 持有它们 |
| 输入语义 | `cmd_*`、`effect`、`systems` | `input_dispatch` 调 `system_event`，并 `apply_effects` |
| 命中 | `target_key`、`zone_from`、`resolve` | shader `entity_index`、picking 回读 |
| DL | `frame_data` | `table_glue::encode` → shader instance |
| 坐标 | `geometry.box`（screen absolute） | `set_pointer` / `table_at` 使用同一坐标空间 |
| 字体 | `ui_table::font/glyph_metrics` | `table_glue::init_font` 从 MSDF atlas 填 |

---

## 2. 设计骨架与不变量

### 2.1 四层模型

```text
真值层     table_data（cells/ids/cols）
意图层     comp::view（selection/edit/scroll/filter/find/checks/menu/...）
派生层     geometry → frame_data
宿主层     table_arena（每表） / table_host（每窗口） / ScreenWidget（每窗口 UI 树）
```

`geometry` 与 `frame_data` 都是**可重建派生数据**，不是第二份真值。  
任何输入只改 `data/view`，下一帧重新 `sync/build_frame`。

### 2.2 所有权与生命周期

| 对象 | 拥有者 | 生命周期 |
|---|---|---|
| `table_host` | `frame_state::tableHost`（shared_ptr） | 与窗口同寿命（单窗口） |
| `ScreenWidget` | `main` 的局部 / `soaCtx` 引用 | 与窗口同寿命 |
| `table_arena` | `ScreenWidget` 树里 `Container` 的 `AggRenderObject<Table>::agg.arena` | 表先于窗口销毁 |
| `table_host::tables/draw_order` | 不拥有，只存 `table_arena*` | attach 到 detach |
| `table_arena::host` | 不拥有，回指 host | `detach` / host 析构清空 |
| `table_host::binding` | RAII 绑定 `inputTranslation()` | host 析构释放 |
| `ScreenWidget::table_host_ctx` | 不拥有，裸指针 | 由 `make_table_screen` 绑定 |

关键顺序：

```cpp
// cpp:12591  make_table_screen：先 move 完 aggregate，再 attach
auto t  = make_table(std::move(spec.data), spec.prefs, std::move(spec.font));
auto ro = render::asRenderObject(std::move(t));
auto &arena = ro->agg.arena;   // 此时地址稳定
arena.host = &hs;
hs.attach(arena);
```

```cpp
// cpp:11854  ~table_arena 只声明；table_host 完整后才定义
inline table_arena::~table_arena()
{
    if (host != nullptr)
        host->detach(*this);
}

inline table_host::~table_host()
{
    for (auto *a : tables)
        if (a != nullptr)
            a->host = nullptr;   // host 先死，避免 arena 回访
}
```

`window_tables()` 先构造 `inputTranslation()`，再创建 `frameState().tableHost`，确保 host 的 `binding` 析构时 sink 还活着。

### 2.3 五条核心不变量

1. **一个键合并两条链**：`entity_index == zone_key(target)`；绘制和输入都只认它。
2. **两个版本时钟**：
   - `host.frame_serial` → `renderVersion`：整帧 GPU picking 标签，只在 `accept_pick` 比较一次；
   - `view.ui.version`：每表 CPU 修订号，用于 hover 去重/重绘，不参与 picking 过滤。
3. **屏幕绝对坐标**：`set_pointer`、`table_at`、`geom.box` 都在同一 framebuffer 像素空间；`set_pointer` 只乘屏幕缩放，不再额外加 owner offset。
4. **z 序是绘制顺序**：`draw_order` 由本帧 `mark_drawn` 重建；`table_at` 反向遍历，不按 attach 顺序。
5. **窗口级单点**：输入只 attach 一次到 `table_host`；cursor 只由 host 写一次；focused 唯一。

---

## 3. 关键结构体与语义结构体（逐个说明）

### 3.1 身份：`table_id` / `row_id` / `column_id`

```cpp
struct table_id  { std::uint32_t value{}; };   // target_key 只留 5 bit -> 1..31
struct row_id    { std::uint64_t v{0}; };      // 稳定行身份，不随排序/插入改变
struct column_id { std::uint32_t value{}; };   // finalize 后 1..N，只补不改
using row_idx = int; using col_idx = int;      // 位置：会随排序/筛选变化
```

语义：

- **id 是身份，index 是位置**。排序/筛选只改 `view` 的可见序列，不改 `row_id`。
- `table_data::ids` 与 `cells` 列主序对齐；`finalize()` 只给没有 id 的列补 id，已有 id 永不改。
- `table_id` 是唯一参与 `zone_key` 的身份，所以窗口内最多 31 张表。

### 3.2 命中协议：`zone` / `target` / `owner_key` / `target_key`

```cpp
enum class part : std::uint32_t {
    none = 0, frame, header, cell, col_sep, check, check_head, filter_btn,
    header_group, vbar_track, vbar_thumb, hbar_track, hbar_thumb,
    edge_n, edge_w, edge_ne, edge_nw, edge_se, edge_sw, menu_item, Count
};

struct zone {
    part kind{part::none};
    int row{-1}, col{-1};
    table_id table{};
};

struct target {
    table_id table{};
    part kind{part::none};
    row_id row{};
    column_id col{};
    int row_index{-1}, col_index{-1};
    [[nodiscard]] zone to_zone() const noexcept
    { return {kind, row_index, col_index, table}; }
};

namespace bits {
    inline constexpr std::uint32_t kPartShift  = 27;  // 5 bit
    inline constexpr std::uint32_t kTableShift = 22;  // 5 bit
    inline constexpr std::uint32_t kRowShift   = 9;   // 13 bit
    inline constexpr std::uint32_t kTableMask  = 0x1Fu;
    inline constexpr std::uint32_t kRowNone    = 0x1FFFu; // 8191
    inline constexpr std::uint32_t kColNone    = 0x1FFu;  // 511
    static_assert(static_cast<std::uint32_t>(part::Count) <= 32, "part > 5bit");
}

using target_key = std::uint32_t;

[[nodiscard]] constexpr std::uint32_t zone_key(const zone &z) noexcept
{
    const auto r = z.row < 0 ? bits::kRowNone : static_cast<std::uint32_t>(z.row);
    const auto c = z.col < 0 ? bits::kColNone : static_cast<std::uint32_t>(z.col);
    return (static_cast<std::uint32_t>(z.kind) << bits::kPartShift) |
           ((z.table.value & bits::kTableMask) << bits::kTableShift) |
           (r << bits::kRowShift) | c;
}
```

`owner_key` / `owner` / `to_owner` / `visit_owner` / `visit_drag` 把“哪一类 part 归谁处理”从整数键还原成**强类型 variant**：

```cpp
using owner = std::variant<none_owner, frame_owner, header_owner, sep_owner,
                           filter_owner, group_owner, cell_owner, check_owner,
                           check_all_owner, bar_owner, edge_owner, menu_owner>;
```

语义：

- `zone` 是几何投影；`target` 是带稳定 row/column id 的强类型目标；
- `target_key` 是两者唯一的压缩表示，跨 C++/GLSL 的 ABI；
- `resolve(data, key)` 回到 `target`，`to_owner(target)` 回到语义 owner；
- **容量边界**：row 最多 8190，col 最多 510（`kRowNone/kColNone` 是 -1 哨兵）。超过会被 mask，属于当前协议上限。

### 3.3 `picking_result` / `object_key` / `render_version`

```cpp
struct object_key { std::uint32_t object_type{0}; std::uint32_t entity_index{0}; };

struct picking_result {
    using version_t = render_version<16>;
    static constexpr std::size_t version_bits = version_t::bit_width; // 16
    static constexpr std::size_t primitive_bits = 32 - version_bits;  // 16

    object_key key;          // xy: (object_type, entity_index=target_key)
    std::uint32_t packed;    // z:  high 16 = render version, low 16 = primitive id
    std::uint32_t hover_fn;  // w:  hover 回调池下标；~0U = 无

    [[nodiscard]] auto render_version() const noexcept { return version_t{packed >> 16}; }
};
static_assert(sizeof(picking_result) == 16);
```

语义：

- `entity_index` 对应目标的 `target_key`；
- `render_version` 是**帧**一致性标签，不是表修订号；
- `hover_fn` 只用于诊断池，生产 hover 不走它；
- `object_type == 0xFFFFFFFF` 表示本帧无命中。

### 3.4 `table_data` / `column` / `cell`（真值）

```cpp
struct cell   { std::string text; std::string note; };

struct column {
    column_id id{};                 // 稳定身份
    std::string key, title, group;
    float w{140}, min_w{48}, max_w{640}, weight{0};
    align a{align::left};
    bool filterable{true}, wrap{false};
    render_kind render{render_kind::text};
    std::vector<std::pair<std::string, glm::vec4>> tags;
};

struct table_data {
    std::vector<column> cols;
    std::vector<std::vector<cell>> cells; // cells[col][row]
    std::vector<row_id> ids;              // 与 cells[col] 等长

    int rows() const noexcept { return static_cast<int>(ids.size()); }
    int cols_count() const noexcept { return static_cast<int>(cols.size()); }
    void finalize();                      // 只补缺失 column id
    // insert_row / remove_row / insert_column / remove_column / set_cell ...
};
```

语义：

- **列主序**便于按列取整列文本/筛选；行由 `ids` 序号定位；
- `column_id/row_id` 稳定，插入/删除/排序后选择、勾选、编辑仍能按身份对齐；
- `cell.note` 是单元格附注（tag 文本/备注）。

### 3.5 `comp::view`（意图）

`view` 不是数据副本，是“用户意图 + 交互状态”：

```cpp
struct view {
    scroll_state scroll;
    sort_state sort;
    filter_state filter;
    selection_state sel;
    checks_state checks;
    columns_state cols;       // order / w / user / hidden
    history_state history;    // undo/redo
    find_state find;
    menu_state menu;
    tooltip_state tip;
    ui_state ui;              // hover/pointer/left/drag/version/now
    edit_state edit;

    void reset(const table_data &d);
    void reconcile(const table_data &d); // 增删行列后按 id 对齐
};
```

`ui_state::touch()` 是**每表 CPU 修订号**：

```cpp
struct ui_state {
    target hover{};
    int band_row{-1}, band_col{-1};
    glm::vec2 pointer{0, 0};
    bool left{false};
    double now{0};
    drag_mode drag{drag_mode::none};
    // ...
    std::uint32_t version{1};
    void touch() noexcept { ++version; }
};
```

语义：

- 输入只改 `view`（选择/滚动/编辑）与必要的 `data`（文本/勾选）；
- `version` 增一表示“下一帧需要重建 geometry/DL”，不用于 picking 过滤；
- `reconcile` 是模型增删与视图身份对齐的唯一入口。

### 3.6 `prefs`（样式与策略）

```cpp
struct prefs {
    float header_h{40}, row_h{30}, row_gap{2}, cell_pad{10};
    float bar{8}, radius{10}, cell_radius{4}, sep_hit{3}, edge_hit{6};
    float min_w{240}, min_h{120}, max_w{1e30}, max_h{1e30};
    int frozen{0};
    float select_w{36};
    bool row_select{true}, wrap{false};
    float group_h{26};
    glm::vec4 bg, header_bg, row_bg[2], row_hover, col_hover, sel, text,
              header_text, sep, accent, bar_track, bar_thumb, bar_thumb_hot;
};
```

两张真实表用不同 `prefs` 证明样式独立：A 30px 行高/40px 表头；B 38px 行高/46px 表头/暖色 accent。

### 3.7 `geometry` / `row_view` / `row_pipeline`（派生几何）

```cpp
struct geometry {
    rect box, header, group, col_header, body, frozen_view, scrollv;
    rect gutter_head, gutter_body;
    float group_h{0}, gutter_w{0}, frozen_w{0}, content_w{0}, content_h{0};
    float sx{0}, sy{0}, step{0};
    int frozen{0};

    std::vector<int> col_index;          // 可见列（按 order/hidden）
    std::vector<float> col_x, col_w;
    std::vector<int> view_rows, row_index; // 可见行（按 sort/filter/scroll）
    std::vector<float> row_y, row_h, row_off, row_h_data;
    std::vector<group_span> groups;

    bool vbar{false}, hbar{false};
    rect vtrack, vthumb, htrack, hthumb, corner;
    rect edge_n, edge_w, edge_ne, edge_nw, edge_se, edge_sw;
    std::uint32_t version{0};
    // col_count()/row_count()/col_x_at()/row_at()/col_at()...
};
```

语义：

- `sync` 的产物，纯函数可重建；
- `box` 已在**屏幕绝对坐标**（`draw_sync` 传入 owner 的 offset/size 后内缩 48px）；
- `view_rows` 是排序/筛选/滚动的结果，输入命中与绘制使用同一份；
- `row_pipeline` 提供可复用的可见行序列缓冲（`base()` 生成 0..N-1，sort/filter 在其上生成 `row_view`）。

### 3.8 `frame_data` / `dl` / `shape_op` / `text_op`（绘制 DL）

```cpp
enum class stream : std::uint8_t { chrome = 0, content = 1, overlay = 2, proxy = 3, Count };

struct shape_op {
    rect r; glm::vec4 color, border;
    float radius, border_w;
    std::uint32_t zone{0};
    owner who{};
};
struct text_op {
    rect r; glm::vec4 color; float baseline;
    const char *p; std::uint32_t len;
    std::uint32_t zone{0};
    owner who{};
};
struct dl {
    std::array<std::vector<shape_op>, 4> shapes;
    std::array<std::vector<text_op>, 4> texts;
    arena ar; // 文本临时存储
};
struct frame_data {
    dl draw;
    std::vector<zone_rect> zones;
    std::vector<std::uint8_t> visual;
    std::uint32_t version{0};
    table_id table{};
};
```

语义：

- 4 条 stream 对应绘制阶段/层（chrome/content/overlay/proxy）；
- 每个 op 都带 `zone` 与 `owner`：绘制顺序决定 z 序，owner 为语义回退；
- `frame_data` 是纯 C++ 值数据，不依赖 GPU；`encode` 才翻译成 shader instance；
- `frame_data.version` 记录产生它的 `view.ui.version`，便于诊断。

### 3.9 `table_ctx` / `frame_ctx` / `systems`（语义分派）

```cpp
struct table_ctx {
    const table_data &d; view &v; const prefs &p; const geometry &g;
    const font &ft; std::vector<effect> *fx; void *out; table_id id{};
};
struct frame_ctx {
    const table_data &d; const view &v; const prefs &p;
    const geometry &g; const font &ft;
};

struct systems {
    filter_feature filter; find_feature find; sort_feature sort;
    resize_feature resize; reorder_feature reorder; freeze_feature freeze;
    checks_feature checks; scroll_feature scroll; table_resize_feature table_resize;
    selection_feature selection; edit_feature edit; clipboard_feature clipboard;
    undo_feature undo; menu_feature menu; tooltip_feature tooltip;

    template <class E> bool handle(table_ctx &c, const E &cmd);
};
```

语义：

- `table_ctx` 是“命令处理上下文”，带 `fx` 供 feature 产出副作用（改文本、剪贴板）；
- `systems::handle` 是**唯一命令分派器**：按固定 feature 顺序尝试，谁先认领谁处理；
- 没有 feature 认领时，`system_event` 退回 `system_gesture`（选择/拖拽/缩放等核心手势）；
- `frame_ctx` 是只读绘制上下文，供 `build_frame` 的 paint 侧使用。

### 3.10 `table_arena`（每表可变状态）

```cpp
struct table_arena {
    ui_table::table_data data;
    ui_table::prefs prefs{};
    ui_table::font font{};
    ui_table::table_id id{};

    ui_table::view view{};
    ui_table::geometry geom{};
    ui_table::row_pipeline pipe{};
    ui_table::systems sys{};
    ui_table::frame_data frame{};

    ui_table::target pointer{};
    picking_result pick{};
    std::uint32_t pick_version{0};
    std::vector<ui_table::effect> fx;

    arena_input::table_arbiter arbiter{};
    input_frame::event_context input_state{};
    intent::time_point now{};
    bool esc_cancelled{false};

    callbacks cb{};     // on_model_changed / on_selection_changed / on_checks_changed
    // cb_* 去重快照

    template <class E> void handle_event(const E &e);
    void advance(double seconds);

    // host 接线
    ui_new::ScreenWidget *screen{};
    ui_new::Widget *owner{};
    GLFWwindow *win{};
    float mx, my;      // screen absolute pointer
    float wx, wy;      // raw window cursor
    float sx, sy;      // window logical -> screen scale
    float vw, vh;

    table_host *host{nullptr};
    bool keyboard_focus{true};
    std::uint64_t draw_serial{};

    ~table_arena();    // 只声明，定义在 table_host 之后
};
```

语义：

- 一个 arena = 一张表的**全部**可变状态（data/view/geom/frame/arbiter/callbacks）；
- `view.ui.version` 是 CPU 修订；`pick_version` 只是诊断记录；
- `draw_serial` 防止一帧内重复 `mark_drawn`；`keyboard_focus` 由 host 唯一设置；
- `~table_arena` 通过 `host->detach` 自动摘除。

### 3.11 `table_host`（每窗口路由器）

```cpp
struct table_host {
    std::vector<table_arena *> tables;     // attach 顺序
    std::vector<table_arena *> draw_order; // 本帧绘制顺序（z）
    table_arena *focused{}, *pressed{}, *hovered{};
    glfw_input_translation::scoped_binding binding{};
    bool input_attached{};
    double pointer_x{}, pointer_y{};
    bool pointer_inside{true};
    std::uint64_t frame_serial{0};
    picking_result last_pick{};

    void attach(table_arena &a);   // 1..31 + 唯一性 + 首次 attach 输入
    void detach(table_arena &a) noexcept;
    table_arena *find(ui_table::table_id id) const noexcept;
    table_arena *table_at(double wx, double wy) const noexcept;
    void set_focus(table_arena *a) noexcept;
    void begin_frame() noexcept;
    void mark_drawn(table_arena *a) noexcept;
    bool accept_pick(const picking_result &r) noexcept;
    void on_pick(const picking_result &r);
    void clear_hover();
    void apply_cursor(GLFWwindow *win) noexcept;
    template <class E> void input_event(const E &e) noexcept;
};
```

语义细节：

- **attach 是构造期**：id 范围/唯一性检查在任何状态改写之前；第一次 attach 才 `inputTranslation().attach(binding, *this)`；
- **focused 提升**（本次修复）：

```cpp
if (focused == &a) {
    focused = nullptr;
    if (!tables.empty())
        set_focus(tables.front()); // 否则剩余表 keyboard_focus 全 false，键盘死
}
```

- `table_at` 使用 `draw_order` 反向，`lx = wx*sx`、`ly = wy*sy`，与 `geom.box` 的 screen-absolute 坐标一致；
- `accept_pick` 是 `render_version` 唯一比较点：

```cpp
bool accept_pick(const picking_result &r) noexcept
{
    if (r.key.object_type == 0xFFFFFFFF) return false;
    if (r.render_version() != render_version_t{(std::uint32_t)frame_serial})
        return false; // 旧帧/别的帧
    last_pick = r;
    return true;
}
```

- `begin_frame`：`++frame_serial` → 用上一帧 `draw_order` 重算 `hovered` → 清空 `draw_order`；
- `mark_drawn`：`draw_serial` 去重后 push，本帧 render 重建 z 序。

### 3.12 `table_spec` / `ScreenWidget` / `frame_state`

```cpp
struct table_spec {
    ui_table::table_data data;
    ui_table::prefs prefs;
    ui_table::font font;
    float flex{1.0F};
};

struct ScreenWidget {
    ui_new::Size size_;
    std::unique_ptr<Widget> root_;
    table_ui::table_host *table_host_ctx{}; // 非拥有
    // layout()/tick()/render()/findByKey()
};

struct frame_state {
    Window *window{};
    // ...
    std::shared_ptr<table_ui::table_host> tableHost; // 窗口级 host
};
```

语义：

- `table_spec` 是**构造期快照**，一次性把 data/prefs/font/flex 交给 `make_table_screen`；
- `ScreenWidget` 只绑定 host，不拥有 host；默认构造的空 screen 也被测试用来做 `on_hover` 路由上下文；
- `frame_state` 是窗口对象，host 与窗口同寿命；`window_tables()` 幂等。

### 3.13 `input_frame` / `event_context` / `frame<T>`（事件归一）

```cpp
struct event_context {
    std::array<HeldState, Key::eSIZE> keys;
    std::array<HeldState, MouseButton::eSIZE> buttons;
    ModifierKey modifier;
    PointerState pointer_state;
    PointerDevice pointer_device;
    double pointer_x, pointer_y;
    bool pointer_position_valid;
};

template <class T>
struct frame { event_context context; T change; };
```

`build_change(ctx, e)` 把平台事件归一为：

| 平台事件 | change |
|---|---|
| `keyboard_event` | `key_change{key, press/release, repeat, modifier}` |
| `mousebutton_event` | `button_change{button, press/release, repeat, modifier}` |
| `position2d_event` | `move_change{x, y, dx, dy}` |
| `scroll_event` | `scroll_event` |
| `cursor_enter_event` | `cursor_enter_event` |
| `char_event` | `char_change{codepoint}` |

不支持的事件返回 `void`，`table_arena::handle_event` 用 `decltype` 自动跳过：

```cpp
using frame_t = decltype(input_frame::build_change(input_state, e));
if constexpr (!std::is_void_v<frame_t>) {
    auto f = input_frame::build_change(input_state, e);
    arbiter.feed(*this, f, now);
    input_frame::apply_change(input_state, f);
}
```

### 3.14 `arena_input` 的 payload / trait / tree（手势语义）

```cpp
struct click_payload { double x, y; time_point at; };
struct drag_payload  { drag_phase phase; double x, y, dx, dy, total_dx, total_dy;
                       ModifierKey modifier; time_point at; };
struct wheel_payload { int delta; time_point at; };
struct key_payload   { Key key; input_frame::Transition transition; time_point at; bool repeat; };
struct char_payload  { unsigned codepoint; time_point at; };

using table_tree =
    intent::all_of<
        intent::leaf<gesture::click_template<Trait_press>>,
        intent::leaf<gesture::click_template<Trait_right_click>>,
        intent::leaf<gesture::drag_template<Trait_drag>>,
        intent::leaf<gesture::wheel_template<Trait_wheel>>,
        intent::leaf<gesture::key_observer_template<Trait_key>>>;

using table_arbiter = intent::debug_arbiter<table_tree>;
```

语义：

- **Trait 只负责 collect**（从 gesture 事件提取 payload）；
- 是否成立（press/release、slop、重复、双击节奏）由 `input_frame` + `gesture` + `arbiter` 决定；
- arbiter 是唯一裁决点；胜出后通过反射调用 arena 的 `on_press/on_drag/on_wheel/on_key/...`。

### 3.15 `glfw_input_translation` / `type_erased_invoker`（窗口输入入口）

```cpp
template <typename Obj>
    requires(requires() {
        proxy_class_type::build<"input_event", data_tuple, Obj>();
    })
constexpr scoped_binding attach(Obj &obj)
{
    return sinks_.attach(&obj,
        proxy_class_type::build<"input_event", data_tuple, Obj>());
}
```

语义：

- GLFW 回调只分发到 `glfw_input_translation`；后者用 `multicast_sink` 保存订阅者；
- `attach` 只对 `table_host` 做一次；`table_host::input_event<E>` 是模板，覆盖 `data_tuple` 里所有事件；
- `type_erased_invoker` 用 C++26 反射查找 `input_event`（含函数模板），并 `requires` 检查 **noexcept** 可调用；
- 这是“编译期检查签名、运行期按 type id 分发”的边界。

---

## 4. 绘制链：从头到尾

### 4.1 入口：`table_ui::draw`

```cpp
// cpp:11560
inline const table_arena::draw_info &draw(table_arena &s, const ui_table::rect &box,
                                          const ui_dao::view &view,
                                          command::record_builder &rb)
{
    draw_sync(s, box);       // 1) geometry
    draw_build(s);           // 2) frame_data
    // ... 统计 info ...
    draw_encode(s, view, [&](auto &&inst) {
        ++info.instances;
        rb.addInsteance(std::forward<decltype(inst)>(inst)); // 3) shader instance
    });
    return info;
}
```

调用者：`make_table` 的 `render` 方法（`ScreenWidget::render` DFS 到每个 Table 节点时调用）。

### 4.2 `draw_sync`：data + view + prefs → geometry

```cpp
// cpp:11511
inline void draw_sync(table_arena &s, const ui_table::rect &box)
{
    const float m = 48.0F; // 表格四周留白
    const ui_table::rect vp{box.x + m, box.y + m,
                            std::max(box.w - 2.0F * m, 1.0F),
                            std::max(box.h - 2.0F * m, 1.0F)};
    s.vw = vp.w; s.vh = vp.h;
    ui_table::sync(s.data, s.view, s.prefs, s.geom, vp, s.font, s.sys, s.pipe);
}
```

`box` 来自 owner widget 的 `offset/size`（屏幕绝对坐标）。`sync` 内部：

1. 行/列 reconcile；
2. 计算 `box`（viewport 再按 `min_w/min_h` 钳制）；
3. 冻结列/分组表头/列宽/可见列；
4. 按 sort/filter/scroll 计算 `view_rows`、`row_y`、滚动条；
5. 写入 `geom.version = view.ui.version`。

### 4.3 `draw_build`：geometry → frame_data（DL）

```cpp
// cpp:11545
inline void draw_build(table_arena &s)
{
    ui_table::build_frame(s.data, s.view, s.prefs, s.geom, s.font, s.sys,
                          s.frame, s.id);
    s.frame.table = s.id;
}
```

`build_frame` 是纯函数（除 `dl.ar` 文本临时存储）：

- 先画 frame/header/row band/selection/check；
- 再画 cell 内容、文本、列分隔、滚动条、8 向把手、menu/tooltip；
- 每个 op 写 `zone = zone_key(target)` 与 `owner = to_owner(target)`；
- `stream` 决定 4 条绘制层。

合并点就是这里：**DL 的 `zone` 与输入 `hit_target` 的 `zone_key` 是同一个函数算出来的**。

### 4.4 `draw_encode`：frame_data → shader instance

```cpp
// cpp:11554
template <class Sink>
inline void draw_encode(const table_arena &s, const ui_dao::view &view, Sink &&sink)
{
    table_glue::encode(s.frame, s.font, view, std::forward<Sink>(sink));
}
```

`table_glue::encode`（cpp:10927）：

- 每个 `shape_op` → `ui_dao::sdf_shape` / border；
- 每个 `text_op` → 逐码点 `ui_dao::glyph_quad`（裁剪到 op rect，UV 子树）；
- `s.hover_fn = ui_table::hover_fn_ref()`；
- `s.entity_index = o.zone`（即 `target_key`）；
- 最终由 `rb.addInsteance` 写入 GPU instance buffer。

### 4.5 GPU：record / replay / picking

```cpp
// cpp:14408
tableHost.begin_frame();                 // ++frame_serial
screen.tick(clock);                      // arena.advance
screen.render(ctx);                      // mark_drawn + draw
tableHost.apply_cursor(win);             // cursor 唯一写点
renderVersion = (std::uint32_t)tableHost.frame_serial;
// ...
frame.replay(commandBuffer, currentFrame, renderVersion);
```

- color attachment 走正常 UI 管线；
- picking attachment 用 frag 的 `outPicking = uvec4(object_type, entity_index, packed_z, hover_fn)`；
- `packed_z = (render_version << 16) | primitive_id`；
- `renderVersion` 来自 host 的帧号，不是某张表的 `view.ui.version`。

### 4.6 回读：`accept_pick` → `on_pick`

```cpp
// cpp:14828（PICKCHECK）
picking_result r = *mapped;
if (!pickMouse.valid) r.key.object_type = 0xFFFFFFFF;
if (tableHost.accept_pick(r))
    tableHost.on_pick(r);
```

`on_pick` 按 `zone_from(r.key.entity_index).table` 找 arena，再调 free `on_pick`：

```cpp
inline void on_pick(table_arena &s, const picking_result &r)
{
    s.pick = r;
    s.pick_version = s.view.ui.version;
    // PICKCHECK：GPU 命中 vs CPU hit_target 对比，只诊断
}
```

### 4.7 绘制链的编译期安全性

| 机制 | 保证 | 边界 |
|---|---|---|
| `target_key` 位布局 + `static_assert(part::Count <= 32)` | 编译期 | row/col 上限是协议约定，无 static_assert |
| `picking_result` `static_assert(sizeof == 16)` | 编译期 | GLSL 侧 `RENDER_VERSION_BIT_WIDTH=16` 需手写同步 |
| `build_frame`/`encode` 模板 sink | 编译期类型检查 | op 内容仍是运行期数据 |
| `AggRenderObject` + `invoke<"render">` 反射 | 编译期查找/可调用性 | `Widget::layout` 是虚函数，运行期 |
| `frame.replay` / Vulkan ABI | 编译期结构体字段 | 跨 C++/GLSL 的打包约定靠常量同步 |

结论：**C++ 内部类型安全较强，跨 shader 的 16-bit 版本位宽与 primitive 布局是唯一需要人工守住的 ABI 契约**。

---

## 5. 输入链：从头到尾

### 5.1 GLFW → 事件系统

`glfw.hpp` 的回调（key/mouse/scroll/cursor/char/focus）调用 `event::distribute<dispatcher>`；  
`glfw_input_translation` 构造时订阅全部事件类型，`attach` 时把 `table_host` 注册为一个 sink。

```cpp
// 每个事件到达 table_host：
template <class E>
void table_host::input_event(const E &e) noexcept;
```

### 5.2 `table_host::input_event`：窗口级路由

| 事件 | 路由 |
|---|---|
| `position2d_event` | `pointer_inside=true`；写 `pointer_x/y`；`hovered=table_at`；转发**所有表**更新 input_state（不点击） |
| `mousebutton_event` press/repeat | `table_at`；命中则 `set_focus(t)`、`pressed=t`、`t->handle_event(e)` |
| `mousebutton_event` release | 优先 `pressed`（拖出表外仍回原表），否则 `table_at`；发完清 `pressed` |
| `scroll_event` | `table_at` |
| `keyboard_event` / `char_event` | `focused`（空则忽略） |
| `cursor_enter_event` | leave：`clear_hover()` + `pointer_inside=false`；enter：`pointer_inside=true`；转发所有表 |
| `window_focus_event` | 转发所有表；失焦：`clear_hover()` + 清 `pressed` |
| 其他 | 忽略 |

关键：**输入只 attach 一次到 host**，每张表不再有自己的 `input_binding/input_event`。

### 5.3 `table_arena::handle_event<E>`：归一

```cpp
template <class E>
inline void table_arena::handle_event(const E &e)
{
    if constexpr (std::same_as<E, ev::window_focus_event>) {
        if (!e.focused) { view.ui.left=false; view.ui.drag=none;
                          view.menu.open=false; view.ui.touch(); }
        return;
    } else {
        now = mcs::app::now_ms();
        view.ui.now = ...;
        if constexpr (std::same_as<E, ev::char_event>) {
            on_char(arena_input::char_payload{e.codepoint, now});
            return;
        }
        if constexpr (std::same_as<E, ev::keyboard_event>) {
            handle_key_event(e);
            return;
        }
        using frame_t = decltype(input_frame::build_change(input_state, e));
        if constexpr (!std::is_void_v<frame_t>) {
            auto f = input_frame::build_change(input_state, e);
            arbiter.feed(*this, f, now);
            input_frame::apply_change(input_state, f);
        }
    }
}
```

- 字符不是手势：直接 `on_char`；
- 键盘走 `handle_key_event` 单独路径（重复键、焦点、Esc 特殊处理）；
- 指针/按钮/滚轮/进入离开走 `input_frame` → arbiter。

### 5.4 arbiter → gesture → `on_*`

`table_tree` 是 5 个识别器：
`click(left)`、`click(right)`、`drag`、`wheel`、`key_observer`。

胜出后通过 `invoke<Name>` 调用 arena：

```cpp
void on_press(const arena_input::click_payload &e);
void on_right_click(const arena_input::click_payload &e);
void on_drag(const arena_input::drag_payload &e);
void on_wheel(const arena_input::wheel_payload &e);
void on_key(const arena_input::key_payload &e);
void on_char(const arena_input::char_payload &e);
```

### 5.5 `on_*`：语义入口

- `on_press` → `click_dispatch`：`set_pointer` → `pointer_target` → `cmd_press` → `cmd_release`；
- `on_right_click` → `sys.menu.open`；
- `on_drag` → 按下补 press，再按 `drag_phase` 发 press/drag/release；
- `on_wheel` → `system_wheel`；
- `on_key` → Ctrl+V 粘贴、Esc `cancel_modal`、其余 `cmd_key`；
- `on_char` → `system_text`。

`click_dispatch` 关键：

```cpp
set_pointer(s, x, y);                 // window logical -> screen
const auto tgt = pointer_target(s, s.mx, s.my); // hit_target
input_dispatch(s, ui_table::cmd_press{...});
input_dispatch(s, ui_table::cmd_release{...});
```

### 5.6 `input_dispatch` → `system_event` → data/view

```cpp
template <class Cmd>
inline void input_dispatch(table_arena &s, const Cmd &cmd)
{
    ui_table::system_event(s.data, s.view, s.prefs, s.geom, s.sys,
                           s.font, cmd, s.fx);
    apply_effects(s);    // fx_set_cell / fx_clipboard
    notify_change(s);    // selection/checks 回调（去重）
    ++s.inputs;
}
```

`system_event`：

```cpp
if constexpr (std::is_same_v<E, cmd_press>) note_click(v, ev);
table_ctx tc{d, v, p, g, ft, &fx, nullptr};
if (!sy.handle(tc, ev))
    system_gesture(d, v, p, g, sy, ft, ev, fx);
```

- `systems::handle` 按 feature 顺序认领（filter/find/sort/resize/reorder/freeze/checks/scroll/table_resize/selection/edit/clipboard/undo/menu/tooltip）；
- 无人认领则 `system_gesture` 走选择/拖拽/缩放等核心手势；
- 任何成功修改都会通过 `view.ui.touch()` 推进表修订号，下一帧 `sync/build_frame` 看到新状态。

### 5.7 `advance`：CPU hover 的 z 序门禁

```cpp
inline void table_arena::advance(double)
{
    now = mcs::app::now_ms();
    view.ui.now = ...;
    if (!view.ui.left) {
        // 所有表的指针坐标都保持最新（PICKCHECK/诊断需要）
        set_pointer(*this, input_state.pointer_x, input_state.pointer_y);
        if (host != nullptr) {
            if (host->hovered == this) {
                view.ui.pointer = {mx, my};
                ui_table::system_hover(view, pointer_target(*this, mx, my));
            } else {
                ui_table::system_hover(view, {}); // 清旧 hover/tooltip
            }
        } else {
            // headless 单表
            view.ui.pointer = {mx, my};
            ui_table::system_hover(view, pointer_target(*this, mx, my));
        }
    }
    arbiter.tick(*this, now);
    notify_change(*this);
}
```

只有 `host.hovered`（z 序最上面）能写 `view.ui.hover`；其余表清空。  
`begin_frame` 在 tick 之前，所以本帧 advance 用的是本帧 hovered；输入使用的是上一帧 render 建好的 `draw_order`。

### 5.8 焦点与生命周期

```cpp
void table_host::set_focus(table_arena *a) noexcept
{
    focused = a;
    for (auto *t : tables)
        if (t != nullptr)
            t->keyboard_focus = (t == a);
}
```

- press 命中表 → `set_focus`；
- `on_key`：`if (!keyboard_focus) return;`；
- host 只把 keyboard/char 发给 `focused`；
- `detach(focused)` 时提升 `tables.front()`，避免剩余表 `keyboard_focus` 全 false；
- `window_focus(false)` / `cursor_enter(false)` 清 hover，失焦另清 `pressed`。

### 5.9 输入链的编译期安全性

| 机制 | 保证 | 边界 |
|---|---|---|
| `glfw_input_translation::attach` 的 `requires` | 编译期要求 `input_event` 签名/noexcept | 事件类型集合固定为 `data_tuple` |
| `table_host::input_event<E>` 的 `if constexpr` | 不支持的事件编译成 no-op | 新增事件若忘记分支会静默 no-op |
| `input_frame::build_change` 的返回类型 | 编译期区分“有 change / void” | change 语义仍运行期 |
| `is_change` 的 `static_assert` | 编译期限制 change 类型 | — |
| `arena_input`/gesture 模板 | 编译期检查 collect/payload/policy | 识别器时序运行期 |
| `invoke<Name>` 反射 | 编译期检查 `on_*` 存在且可调用 | — |

结论：**输入链的类型/分派安全是编译期保证的；唯一“静默”风险是事件集新增而 host 忘记加分支**。

---

## 6. 帧时序（单窗口多表）

```text
每帧：
  1. Window::pollEvents()
       └─ glfw_input_translation → table_host::input_event
            ├─ position: 所有表更新 input_state，host.hovered = table_at(旧 draw_order)
            ├─ press:    table_at → set_focus / pressed → arena.handle_event
            ├─ release:  pressed (或 table_at)
            ├─ keyboard/char: focused
            └─ focus/cursor: hover/pressed 清理

  2. drawFrame / mainPipeline.draw
       ├─ screen.updateSize(W,H); screen.layout()
       ├─ tableHost.begin_frame()
       │     ++frame_serial; hovered = pointer_inside ? table_at(旧 draw_order) : null;
       │     draw_order.clear()
       ├─ screen.tick(clock)
       │     └─ 每个 arena.advance(): host.hovered 门禁 → system_hover; arbiter.tick
       ├─ screen.render(ctx)
       │     └─ 每个 Table: mark_drawn → draw_sync → draw_build → encode → addInsteance
       ├─ tableHost.apply_cursor(win)   // 只由 host 写
       ├─ renderVersion = (u32)tableHost.frame_serial
       └─ frame.replay(cmd, frameIndex, renderVersion)
  3. submit / present
  4. [TABLE_PICKCHECK] fence wait → map picking → accept_pick → on_pick

下一帧：
  input 使用上一帧 render 建好的 draw_order；
  begin_frame 用同一份顺序先算 hovered，再清空重建。
```

---

## 7. 多表实例化：两张不同的表

`make_table_screen(w,h,specs)` 负责构造：

```cpp
ui_table::hover_fn_ref() = hover_index();
auto &hs = window_tables();            // 幂等，窗口唯一 host
auto row = ui_new::Row("tables");
row.mainAxisSize(ui_new::MainAxisSize::max);
row.crossAxisAlignment(ui_new::CrossAxisAlignment::stretch);

for (i, spec : specs) {
    auto t  = make_table(std::move(spec.data), spec.prefs, std::move(spec.font));
    auto ro = render::asRenderObject(std::move(t));
    auto &arena = ro->agg.arena;       // move 完成后地址稳定
    arena.host = &hs;
    hs.attach(arena);
    row.addChild(ui_new::Expanded("table"+i)
        .flex(flex)
        .child(ui_new::Container("panel"+i).renderObject(std::move(ro))));
}
screen.table_host_ctx = &hs;
```

`make_screen(w,h)` 固定两表：

| | A（左） | B（右） |
|---|---|---|
| 列 | 8 | 3 |
| 行 | 60 | 24 |
| 行高 | 30 | 38 |
| 表头 | 40 | 46 |
| accent | 蓝灰 | 暖橙 |
| owner | `(768x1000 @ 0,0)` | `(768x1000 @ 768,0)` |

同一 `table_host` 下两个 `table_arena`，`focused` 初始 A；点击 B 后焦点切到 B；两表 view/data/geom 完全独立。

---

## 8. 两个版本时钟与坐标空间

### 8.1 版本

| 名称 | 类型/来源 | 用途 | 比较点 |
|---|---|---|---|
| `view.ui.version` | 每表 `uint32`，`touch()` 递增 | CPU 修订，驱动 geometry/DL 重建 | `sync` 写入 `geom.version`/`frame.version`；不参与 picking |
| `host.frame_serial` → `renderVersion` | 每帧 `uint64` | GPU picking 帧一致性 | 只在 `accept_pick` 比较一次（按 16-bit 掩码） |
| `picking_result::packed` | `version<<16 + primitive`（高 16 位版本，低 16 位 primitive） | shader ABI | C++ `render_version<16>` 与 GLSL `RENDER_VERSION_BIT_WIDTH=16` 必须一致 |

`on_pick` **不**比较 `view.ui.version`，避免 A=10、B=12 时误杀。

### 8.2 坐标

```text
GLFW window logical (wx, wy)
        │ sx = screen.width / glfwWindowWidth
        │ sy = screen.height / glfwWindowHeight
        ▼
screen/framebuffer px (mx, my)
        │ 直接与 geom.box 比较
        ▼
target_key

geom.box 由 owner.offset/size 经 draw_sync 内缩 48px 得到；
owner.offset 已在布局阶段累加到屏幕绝对坐标，所以 set_pointer/table_at 不再加 offset。
```

已验证：两张不同表在 20/40/60/80% 四个位置 PICKCHECK 0 mismatch，且 table 位分别落在 1/2。

---

## 9. 测试用例：每个测试到底测了什么

### 9.1 `TABLE_SELFTEST`（hpp，171 ok）

纯表格核心、无窗口/GPU：

- **身份**：column id 分配/稳定；row_id ↔ index；`zone_key/zone_from` 的 table 位往返；
- **数据操作**：插入/删除行、插入/删除列、`finalize` 保留已有 id、`index_of`；
- **选择/勾选/编辑/撤销/重做/剪贴板**：press 选行、空格勾选、F2 编辑、Enter 提交、Esc 取消、Ctrl+Z/Y；
- **筛选/排序/查找/列隐藏/显示全部/列重排/列宽自适应**；
- **几何**：header/body、冻结列、wrap、滚动、hbar/vbar、表格缩放、列宽拖拽；
- **命中**：cell/header/边缘/滚动条/菜单/tooltip；
- **DL**：每 op 都有 zone 与 owner；frame 可复用；menu overlay 也带 key；
- **导航**：方向键/PageUp/PageDown/Shift 扩展选择/自动滚动。

### 9.2 `TABLE_EVTEST`（cpp，32 ok）

单表 headless，验证两条链在**同一份数据**上对齐：

1. `draw.sync` → geometry；`draw.build` → frame_data；`draw.encode` → instance；
2. 每个 DL op 都有 `target_key` 与 owner；
3. `frame op.zone == hit_target` 的 `zone_key`；
4. 输入 dispatch：press → selection、version 递增、selection 回调；
5. arena 事件链：click、keyboard、repeat、wheel、drag、double click、char、右键菜单；
6. 模型回调：insert/remove row 保持 checks/selection；`insert_row` reconcile；
7. 输入 → 下一帧 `geometry.version` 跟上、DL 重新生成。

### 9.3 `TABLE_MULTITEST`（cpp，71 ok）

单窗口两表 headless，覆盖多实例：

| 组 | 断言内容 |
|---|---|
| 初始 | focused=A；A/B hover 空 |
| CPU hover | `host.hovered==B`；只有 B 的 `view.ui.hover` 是 cell；A 空 |
| GPU on_hover | B pick 更新、A 不变；A pick 更新、B 不变（经 `ScreenWidget::table_host_ctx` 真实路由） |
| 点击 | 点 B → focused=B、B 有选区、A 不变 |
| 键盘 | focused B 移动、A 不动 |
| 字符 | F2 打开 B 编辑、char 进 B、A 编辑不打开 |
| 焦点 | 点 A → focused=A；之后键盘只动 A |
| 滚轮 | 指针在 A 只滚 A；在 B 只滚 B |
| 右键 | 只在 B 开菜单、focused=B、Esc 只关 B 菜单 |
| 重复 id | attach 抛错，host 不被污染，被拒 arena `host==nullptr` |
| z 序 | `mark b,a` → top A；`mark a,b` → top B；指针移出全部清空 |
| 版本 | 同帧 pick 接受；per-table `view.ui.version` 不同不影响； stale 丢弃且不更新 |
| detach | 移除 B；**focused 提升到 A**；A `keyboard_focus=true`；A 键/字符仍工作；B 不再路由 |
| 空 focused | `modal_open/cancel_modal` 安全；keyboard 忽略不崩 |
| 真实 `make_screen` | 同 screen 2 个 Table、2 个不同 arena、60/24 行、8/3 列、左右并排、distinct id、首表 focus、作用域退出 detach |
| 生命周期 | arena 先于 host 销毁 → 自动 detach；host 清指针；`table_at` 不返回已销毁表；`on_hover` 安全 no-op |

### 9.4 `TABLE_PICKCHECK`（真实窗口，150 ok / 0 mismatch）

- 真实 GLFW 窗口，鼠标移到左右两表；
- GPU picking 回读 `entity_index` 还原 table 位：table1/table2 都有命中；
- CPU `hit_target` 与 GPU `resolve` 的 `target` 一致；
- 双表坐标修复后，四点位（20/40/60/80%）也是 0 mismatch。

### 9.5 真实窗口手工/自动验证

- 点击左表 `(384,450)` → `cell row=10 col=1`；右表 `(1152,450)` → `cell row=8 col=1`（不同行高，证明独立几何）；
- Right（含 repeat）、Enter、F2、Ctrl+F、Esc 均进入事件链；
- F2→Esc、Ctrl+F→Esc 后窗口存活；最后无 modal Esc 正常关窗；
- 验证时另存截图：`_shots/a11_two_tables.png`（截图不随本文提交）。

### 9.6 运行命令

```powershell
cmake --build E:/0_github_project/mcsvulkan/build \
  --target mcsvulkan-vma-test_ui_ability11_table -j 8

$exe = "E:/0_github_project/mcsvulkan/output/test_program/mcsvulkan/vma/test_ui_ability11_table.exe"
$env:TABLE_SELFTEST='1';  & $exe          # 171 ok
$env:TABLE_EVTEST='1';    & $exe          # 32 ok
$env:TABLE_MULTITEST='1'; & $exe          # 71 ok
$env:TABLE_PICKCHECK='1'; & $exe          # 真实窗口，移动鼠标，150/0
```

诊断环境变量：`TABLE_SELFTEST_VERBOSE`、`TABLE_INPUTLOG`、`TABLE_GEOMLOG`、`TABLE_PICKCHECK`、`DSH_AUTOEXIT`。

---

## 10. 编译期安全与稳定性评估

### 10.1 编译期已经保证的

1. **身份/协议位布局**：`target_key` 的 shift/mask 是 constexpr；`part::Count <= 32` static_assert；`picking_result` 16B static_assert。
2. **事件归一**：`build_change` 的返回类型参与 `if constexpr`；不支持的平台事件不会生成无效代码。
3. **事件签名**：`glfw_input_translation::attach` 用 `requires` 检查 `input_event` 存在且 noexcept；`table_host` 是模板覆盖所有事件。
4. **组件 DSL**：`WidgetBuild`/`FlexChildBuilder`/`constraints` 概念约束 Container/Row/Expanded 组合。
5. **渲染入口**：`AggRenderObject<Table>` 的 `invoke<"tick"/"render">` 由反射在编译期查找；签名不匹配直接编译失败。
6. **命令分派**：`system_event`/`systems::handle`/`system_gesture` 都是模板，`cmd_*` 类型编译期分派。

### 10.2 仍是运行期的

| 项 | 原因 | 当前保护 |
|---|---|---|
| 表的数量/身份 | 表在运行期构造 | `attach` 检查 1..31 + 唯一性，构造期抛错 |
| 布局 | `Widget::layout` 虚函数 | 单元测试 + 双表结构断言 |
| focus/hover/draw_order | 交互状态 | MULTITEST 全覆盖 |
| picking 回读 | GPU/驱动 | `accept_pick` 版本过滤 + PICKCHECK |
| shader 16-bit 位宽 | GLSL 常量手写 | C++ `render_version<16>` + shader `RENDER_VERSION_BIT_WIDTH=16`；需人工同步 |

### 10.3 稳定性结论

- **C++ 类型层**：稳定。改错成员名/签名会在编译期失败。
- **事件层**：签名编译期检查，但**新增事件需要同时更新 `data_tuple` 与 host 分支**；忘记分支会静默 no-op。
- **ABI 层**：`picking_result` 与 shader 的 16-bit 打包是**手工契约**；改动 `render_version<16>` 必须同步 GLSL 常量。
- **运行期不变量**：表 id 范围/唯一、focus 提升、draw_order、坐标空间都有测试兜底。

---

## 11. 从头整理后的设计改进点（候选，不在本次实现）

1. **强类型坐标**：`screen_point` / `window_point` 两个类型，禁止把 window 坐标直接传 `table_at`；当前 `wx*sx` 靠约定，编译期无法防混用。
2. **统一帧令牌**：`frame_token` 强类型封装 `uint64 frame_serial` 与 `render_version<16>` 的截断，避免 `static_cast` 散落。
3. **C++/GLSL 共享常量**：用生成头文件或 shader 编译宏统一 `RENDER_VERSION_BIT_WIDTH`，消除手写 16。
4. **事件路由表**：把 `data_tuple` 与 host 分支绑定成编译期表（类型列表 + 处理函数），新增事件未处理时编译失败，而不是静默 no-op。
5. **table registry**：把 5-bit id 分配、唯一性、slot 生命周期收口到 `table_registry`，`attach` 只接受 token，避免运行期抛错。
6. **host 与 ui_new 解耦**：`ScreenWidget::table_host_ctx` 目前是裸指针 + 约定；可抽一个 `table_scene_host` 接口，host 不直接依赖 `ui_new::ScreenWidget`。
7. **多窗口准备**：当前 `inputTranslation()` / `hoverPool()` 仍是进程级；多窗口版本需要 per-window input + hover pool。
8. **测试补强**：`zone_key` 边界属性测试（row 8190/8191、col 510/511、table 31/32）；生命周期 fuzz（随机 detach/销毁顺序）。

---

## 12. 附录：符号索引（当前工作区）

### hpp

| 符号 | 行 |
|---|---|
| `ui_table` 命名空间 | 48 |
| `part` | 154 |
| `zone` / `owner_key` / `target` | 179 / 192 / 203 |
| `bits` / `zone_key` / `zone_from` | 218 / 229 / 237 |
| `target_of` / `resolve` / `to_owner` / `visit_owner` | 822 / 835 / 911 / 959 |
| `table_data` / `column` / `cell` | 672 / 650 / 644 |
| `prefs` | 964 |
| `comp::view` | 1248 |
| `geometry` | 1608 |
| `stream` / `shape_op` / `text_op` / `dl` / `frame_data` | 1766 / 1775 / 1784 / 1795 / 1836 |
| `table_ctx` / `frame_ctx` / `systems` | 1861 / 1872 / 3873 |
| `sync` | 4072 |
| `system_gesture` | 4372 |
| `system_event` | 4756 |
| `hit_target` / `build_frame` | 4779 / 4785 |
| `selftest` | 5219 |

### cpp

| 符号 | 行 |
|---|---|
| `object_key` / `picking_result` | 495 / 588 |
| `frame_state` | 732 |
| `ScreenWidget` | 4509 |
| `glfw_input_translation` | 8458 |
| `input_frame` | 9472 |
| `gesture` | 9753 |
| `AggRenderObject` / `asRenderObject` | 10861 / 10883 |
| `table_glue` | 10927 |
| `table_arena` | 11394 |
| `draw_sync` / `draw_build` / `draw_encode` / `draw` | 11511 / 11545 / 11554 / 11560 |
| `table_host` | 11616 |
| `on_hover` | 12467 |
| `make_table_screen` / `make_screen` | 12577 / 12613 |
| `event_probe` / `multi_probe` | 12649 / 12985 |
| 帧时序（begin_frame/tick/render/cursor/renderVersion） | 14408–14417 |
| `frame.replay` | 14466 |
| PICKCHECK `accept_pick/on_pick` | 14828 |

### 已知文档小瑕疵（未改代码）

- `test_ui_ability11_table.hpp` 顶部注释仍写 `test_ui_ability10_table.hpp`；
- 该文件头注释中的“apply”实际对应 `system_event/systems`。

---

## 13. 一句话总结

**ability11 = 每表 `table_arena` + 每窗 `table_host` + 同一个 `target_key` 合并点 + 两个独立版本时钟。**  
绘制链和输入链在 C++ 类型层是编译期安全的；跨 shader 的 16-bit 版本/primitive 打包，以及运行期 id/布局/focus，是仍需遵守契约和测试兜底的边界。
