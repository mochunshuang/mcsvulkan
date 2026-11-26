# ability12 重构设计规范（交接给下一个 AI）

> **读者**：接手重构的 AI / 工程师  
> **输入**：`test_ui_ability11_table.cpp/.hpp`（功能已验收，171/32/71/150 全绿）  
> **输出**：ability12 —— 行为等价，但人可读、主线明确、单一责任、最小接口、组合式结构。  
> **硬约束**：不改 ability10/11；不改 `target_key` 位布局 / picking ABI / 命令语义 / 坐标空间 / feature 顺序 / 焦点提升行为；不新增功能。  
> **本文件只描述设计，不包含实现代码。**
>
> **仓库现状**：工作区已有 `test_ui_ability12_table.cpp/.hpp` 和 `vma.cmake` 的 ability12 target，但它目前只是 ability11 的格式化副本（BOM + clang-format，hpp 完全一致，cpp 仅 27/30 行格式差异），**不是已完成的重构**。本规范把它视为 scratch 起点：先确认它与 ability11 行为等价，再按 §10 分阶段重构；不要把它当“已经设计好的 ability12”。

---

## 0. 一句话目标

把 ability11 从“**一张大表 + 一堆并列的万能结构**”重构成：

```text
Model + Intent
     │  session::dispatch（唯一 Model/State 写入口）
     ▼
  Session
     │  session::compose（唯一派生入口）
     ▼
Frame { Layout, DrawList }
     │  platform::encode（唯一 GPU 出口）
     ▼
 Instances

Event → window::route（唯一输入入口）→ Command
Picking → platform::resolve（唯一 GPU 入口）→ Target
```

**七个 schema、五个门、两个 composer、一个 window host。**

- 七个 schema：`Model / State / Interaction / Layout / DrawList / Command / Outcome`
- 五个门：`window::route_event`、`session::dispatch`、`session::compose`、`platform::encode`、`platform::resolve`
- 另有两个只写 `Interaction` 的窄门：`session::observe_pointer` / `session::update_hover`
- 两个 composer：`CommandRouter`（feature 组合）、`FrameComposer`（layout/paint pass 组合）
- 一个 window host：`window::Host`（输入、焦点、z 序、帧号、picking 回读）

### 0.1 方向与方法：三锚点

纯 top-down 会得到“千百种实现”；纯 bottom-up 会得到“漂亮的孤岛拼不回去”。  
ability12 用**三锚点 + 自底向上构造 + 自顶向下验收**：

| 锚点 | 内容 | 作用 |
|---|---|---|
| 底锚 | `Target/TargetKey`、picking ABI、`Model` 数据 schema | 最不可变，决定“可枚举范围” |
| 中锚 | `Command / Outcome` 用例词汇 | 决定“行为边界”，不是 Controller 能猜的 |
| 顶锚 | ability11 行为 oracle：171/32/71/150 + golden DL + 双表 PICKCHECK | 决定“同功能” |

构造顺序按**约束强度递减**：

```text
Target/ABI → Model → Command 词汇 → Layout/DrawList → Feature/Pass 原子
          → CommandRouter/FrameComposer → Session → Host/Platform → oracle 验收
```

这就是 Java `DAO → Service → Controller` 经验的正确用法：

- DAO（数据 schema）约束最强，先写，可枚举；
- Service 的**用例契约**是第二锚点，必须尽早写，不能等 Controller；
- Controller 是适配器，最后写，写法多但影响小。

“倒着推”不是“从任意底层函数往上堆”，而是**从约束最强的一端推**；  
同时用顶锚（旧行为）反向校验，防止自底向上漂移。

### 0.2 必须记住的六件事

1. 两个 Store：`Model`（真值）+ `Intent`（State 持久意图 / Interaction 临时交互）。
2. 三个纯函数：`layout`、`paint`、`hit`；只读，不写 Model/State。
3. 一个写入口：`session::dispatch` 是唯一 Model/State 写入口。
4. 一个输入入口：`window::route_event`；一个派生入口：`session::compose`。
5. 两个 ABI 端口：`platform::encode`（GPU 出）、`platform::resolve`（GPU 入）。
6. 一个 oracle：没有 golden DL + 基线数字，就没有“同功能”。

---

## 1. 为什么 ability11 难读（重构要解决的问题）

不是“层不够”，而是**边不可见**：

| ability11 的隐边 | 后果 |
|---|---|
| `view` 同时装持久意图与临时交互 | 读者无法判断字段生命周期 |
| `table_arena` 同时是 model/state/geometry/frame/input/window 绑定 | 一个结构承担 5 种责任 |
| `table_ctx` 有 `void* out` / `fx*` | 输出不可见，签名不可信 |
| `systems::handle` 15 个 `if` | 同层 feature 的顺序是隐藏依赖 |
| `build_frame` 单体函数 | 画什么、按什么顺序画，不可一眼看出 |
| `hover_fn_ref()` / `next_table_id()` / `trace_on()` 全局 | 核心库带进程级副作用 |
| `make_aggregate<"Table">` / `invoke<"render">` | 关系藏在字符串里，grep/include 不可见 |
| `table_host ↔ table_arena`、`ScreenWidget ↔ host` 裸指针 | 生命周期靠约定 |
| `effect` 向量 + `cb` 回调 + `changes` 标志 | 三种“状态变了”的旁路 |

重构的核心不是“换模式”，而是：**把每一条边变成类型/签名/include/命名空间，让关系可见。**

---

## 2. 设计法（七条，按优先级）

1. **一个生命周期，一个类型**
   `Model`（表的一生）、`State`（表的一生，可 reset）、`Interaction`（窗口/帧，可丢）、`Layout/DrawList`（帧，可丢）、`Binding`（窗口）、`Host`（窗口）。
2. **一条边界，一个门**
   每层只暴露一个入口函数；跨层只能走这个门。
3. **同层不互调**
   feature 之间、paint pass 之间、layout pass 之间不直接调用；由 composer 统一调度。
4. **组合而非继承**
   用显式有序列表组合孤岛节点：`FeatureList` / `LayoutPassList` / `PaintPassList`；顺序是数据，不是 if 链。
5. **公开最小**
   公共头只暴露 schema 和门；实现进 `detail`；测试通过 test 接口，不摸内部。
6. **无隐藏边**
   公共接口禁止：全局可变状态、`void*`、跨层回调、字符串反射、单例窗口状态。
7. **名字即范围**
   命名空间表达层；类型名表达责任；函数名表达方向。禁止 `Manager/Helper/Util/do/handle/process` 作为公共名。

### 2.1 语义与风格规范（人读优先）

| 约束 | 目标 |
|---|---|
| 类型 | 名词，表达责任：`Layout`、`DrawList`、`Target`；禁止 `Manager/Helper/Util` |
| 函数 | 动词 + 对象：`dispatch_command`、`compose_frame`、`route_event`、`encode_instances`、`resolve_pick` |
| 参数 | ≤ 4 个；超过则用有名字的 `*Context`，禁止 `void*` |
| 返回 | 谓词用 `bool`；多结果用 `Outcome`；禁止 `bool + 旁路输出` |
| 所有权 | 值类型优先；裸指针只出现在 `window::Binding/Host`；核心不持有窗口 |
| 同层 | 节点不互相调用；顺序只写在 composer |
| 文件 | 单文件 < 1500 行；单类型 < 200 行；单函数 < 80 行 |
| 公共头 | < 300 行；只放 schema + 门；`detail` 不对外 |
| 注释 | 写“为什么/约束”，不写“做了什么”；每层顶部一段主线说明 |
| 命名空间 | 5–7 个模块；不要一个 struct 一个 namespace |

**可读性判据**：新读者 3 跳内解释一次点击/一帧绘制；  
如果解释一次点击需要跨 5 个文件、3 个全局、2 个回调，就是没重构好。

### 2.2 零抽象开销与 DOD 约束

零抽象开销 = **抽象在编译期消失**，不是“不要抽象”：

- 核心用 `concept`、`std::tuple<Features...>` + `std::apply`、`constexpr` pass 类型列表；
- 热路径禁止 `virtual`、`std::function`、RTTI、核心反射、运行时 type-erasure；
- 数据用值类型 + `std::span`；pass 批量读，不 per-cell 虚调用；
- 参数/返回 `noexcept`（除明确可能失败的入口）；
- `layout/paint/hit` 是纯函数，便于内联与测试。

**注意**：`std::array<函数指针>` 是“近零开销”，不是严格零开销（每个 pass 一次间接调用）。  
若要求严格零开销，用 `std::tuple<Pass...>` + 折叠表达式；函数指针表只用于诊断/可选扩展。

DOD 的边界：

- ability12 做 **DOD-lite**：列主序 Model、稳定 id、span 批量 pass、无 per-cell 虚调用、无热路径分配；
- **严格 DOD**（SoA 全量、arena、无 `std::string` 行对象）是另一个项目，会改变数据和性能基线；  
  如果 DOD 是硬指标，必须先单独建性能基线和数据模型，不混进本次行为等价重构。

### 2.3 张弛与韧性

- **刚性脊柱**：F1–F10、五个门、七个 schema、两条链；这些不动。
- **柔性叶子**：feature/pass 是孤岛，可独立增删/排序；新增一种绘制只加一个 pass。
- **韧性**：节点小、契约窄、顺序显式、oracle 可回滚；坏一个节点不会污染 Host/其他节点。

---

## 3. 目标文件与命名空间

ability12 新建独立目标，不修改 ability11：

```text
test/mcsvulkan/vma/
  table12/
    contract.hpp        // 共享身份/坐标/目标：TableId, RowId, ColumnId, Target, Rect, Point
    model.hpp           // Model：Table, Column, Cell
    intent.hpp          // State + Interaction
    frame.hpp           // Layout + DrawList + Frame
    command.hpp         // Command + Outcome + Effect
    session.hpp         // Session（组合根）
    detail/
      layout_passes.hpp
      paint_passes.hpp
      features.hpp
  table12_app/
    host.hpp            // window::Host + Binding
    platform.hpp        // encode/resolve
  test_ui_ability12_table.cpp
  test_ui_ability12_table.hpp   // 仅测试入口（可选）
```

命名空间：

```cpp
namespace table            // contract：Target/TableId/Rect/Point...
namespace table::model
namespace table::intent
namespace table::frame
namespace table::command
namespace table::session
namespace table::window     // cpp 宿主
namespace table::platform   // cpp ABI
namespace table::detail     // 内部 pass/feature，不对外
namespace table::test       // 测试
```

### 3.1 hpp / cpp 契约（必须写死）

| 文件 | 角色 | 可依赖 | 禁止 |
|---|---|---|---|
| `table12/contract.hpp` | 共享身份/坐标 | std/glm | 任何其他 table 头 |
| `table12/model.hpp` | 真值 | contract | intent/frame/command/window/platform |
| `table12/intent.hpp` | 意图 | contract/model | frame/command/window/platform |
| `table12/frame.hpp` | 派生布局/DL | contract/model/intent | command/session/window/platform |
| `table12/command.hpp` | 命令/结果/feature | contract/model/intent/frame | session/window/platform |
| `table12/session.hpp` | 组合根/门 | 上述全部 | window/platform/ui_new/GLFW/Vulkan |
| `table12/detail/*` | pass/feature 内部 | 同层 schema | 公共头反向 include |
| `table12_app/host.hpp` | 窗口 host | session/command/platform | core 内部 detail |
| `table12_app/platform.hpp` | GPU/事件 ABI | frame/contract | host |
| `test_ui_ability12_table.cpp` | main/测试 | app + core | 直接改 core 私有状态 |

规则：

- **cpp/app → hpp/core 单向**：core 永远不知道 app、窗口、GPU；
- **hpp 不 include cpp/GLFW/Vulkan/ui_new**；
- **cpp 不 include `detail`**；需要扩展时通过公共门；
- `model/intent/frame/command` 只依赖 `contract` 和更下层，不允许 include `window/platform`；
- `window` 只依赖 `session` / `command` / `platform`，不直接读 `frame` 内部；
- `platform` 依赖 `frame` / `contract`，不反向依赖 `window`；
- `detail` 只被同层 composer 使用，公共头不 include `detail`。

**不要**为了文件数量而拆到 15 个 namespace；5–7 个模块是上限。

---

## 4. 公共契约（最小接口）

### 4.1 共享身份与坐标

```cpp
namespace table {

struct TableId  { std::uint32_t value{}; }; // 1..31，5 bit
struct RowId    { std::uint64_t value{}; };
struct ColumnId { std::uint32_t value{}; };

enum class Part : std::uint32_t {
    none = 0, frame, header, cell, col_sep, check, check_head, filter_btn,
    header_group, vbar_track, vbar_thumb, hbar_track, hbar_thumb,
    edge_n, edge_w, edge_ne, edge_nw, edge_se, edge_sw, menu_item, Count
};

struct Target {
    TableId table{};
    Part part{Part::none};
    RowId row{};
    ColumnId col{};
    int row_index{-1};
    int col_index{-1};
};

using TargetKey = std::uint32_t; // 位布局保持 ability11 不变

struct Rect  { float x, y, w, h; };
struct Point { float x, y; };

struct Viewport {
    Rect screen;      // framebuffer 像素
    float scale_x{1}; // window logical -> screen
    float scale_y{1};
};

} // namespace table
```

`TargetKey` 的 `zone_key/zone_from` 值布局与 ability11 完全相同，只是搬进 `table` 命名空间。**禁止改 bit。**

### 4.2 命令与结果

```cpp
namespace table::command {

enum class Kind { press, release, drag, wheel, key, char, ... };

struct Command {
    Kind kind;
    Target target;         // 语义目标
    double x, y;           // screen 坐标
    double dx, dy;
    Modifier modifier;
    int wheel_delta;
    Key key;
    std::uint32_t codepoint;
};

enum class EffectKind { set_cell, write_clipboard };

struct Effect {
    EffectKind kind;
    RowId row;
    ColumnId col;
    std::string text;
};

struct Outcome {
    bool handled{false};
    bool model_changed{false};
    bool state_changed{false};
    std::vector<Effect> effects;
};

// dispatch 属于 session 层（见 §5.7）；command 层不认识 Session，避免环形依赖。
}
```

规则：

- `Outcome` 是唯一“命令结果”；
- `Effect` 是值，不是回调；由 `window::Host` 在 `dispatch` 之后应用；
- 禁止 `void* out` / `fx*`；
- `dispatch` 内部只做命令 → feature/gesture 的分发，不画、不读 GPU。

---

## 5. 七个 schema（职责与生命周期）

### 5.1 `model::Table`（真值）

```cpp
namespace table::model {

struct Cell { std::string text; std::string note; };

struct Column {
    ColumnId id{};
    std::string key, title, group;
    float w{140}, min_w{48}, max_w{640}, weight{0};
    Align align{Align::left};
    bool filterable{true}, wrap{false};
    RenderKind render{RenderKind::text};
    std::vector<std::pair<std::string, glm::vec4>> tags;
};

struct Table {
    std::vector<Column> columns;
    std::vector<std::vector<Cell>> cells; // 列主序
    std::vector<RowId> row_ids;

    int rows() const noexcept;
    int columns_count() const noexcept;
    // insert_row / remove_row / insert_column / remove_column / set_cell ...
};
}
```

责任：**只存事实**。不持有 view、不持有窗口、不持有指针。

生命周期：表的一生；可序列化；只有 `dispatch` 能改。

### 5.2 `intent::State`（持久意图）

```cpp
namespace table::intent {

struct Selection { RowId r0, r1; ColumnId c0, c1; RowId cur_row; ColumnId cur_col; };
struct Checks    { std::vector<RowId> ids; std::vector<std::uint8_t> on; };
struct Filter    { std::vector<int> edit; std::vector<std::string> text, orig; };
struct Sort      { int col{-1}; bool desc{false}; };
struct Columns   { std::vector<int> order; std::vector<float> width, user_w; std::vector<bool> hidden; };
struct Scroll    { float x{0}, y{0}; };
struct Find      { bool mode{false}; std::string text; };
struct Edit      { RowId row; ColumnId col; std::string buf; int caret; };
struct History   { /* undo/redo */ };

struct State {
    Selection selection;
    Checks checks;
    Filter filter;
    Sort sort;
    Columns columns;
    Scroll scroll;
    Find find;
    Edit edit;
    History history;

    void reset(const model::Table &);
    void reconcile(const model::Table &);
};
}
```

责任：**跨帧的用户意图**；可 reset、可序列化、可撤销。

### 5.3 `intent::Interaction`（临时交互）

```cpp
namespace table::intent {

struct Interaction {
    Target hover{};
    int band_row{-1}, band_col{-1};
    Point pointer{};        // screen 坐标
    bool left{false};
    DragMode drag{DragMode::none};
    Target drag_target{};
    double now{0};
    double last_click_t{-1e9};
    Point last_click_pos{};
    int click_count{0};
    bool pointer_inside{true};
};
}
```

责任：**窗口/帧级的临时状态**；掉帧可丢、不可序列化。

`State + Interaction` 取代 ability11 的 `view`。这是最关键的一拆。

### 5.4 `frame::Layout`（派生几何）

```cpp
namespace table::frame {

struct Layout {
    Rect box, header, group, col_header, body, frozen_view, scrollv;
    Rect gutter_head, gutter_body;
    float group_h, gutter_w, frozen_w, content_w, content_h;
    float sx, sy, step;
    int frozen;

    std::vector<int> col_index;
    std::vector<float> col_x, col_w;
    std::vector<int> view_rows, row_index;
    std::vector<float> row_y, row_h, row_off, row_h_data;
    std::vector<GroupSpan> groups;

    bool vbar, hbar;
    Rect vtrack, vthumb, htrack, hthumb, corner;
    Rect edge_n, edge_w, edge_ne, edge_nw, edge_se, edge_sw;
    std::uint32_t revision{0};

    int col_count() const noexcept;
    int row_count() const noexcept;
    float col_x_at(int) const noexcept;
    int row_at(float) const noexcept;
    int col_at(float) const noexcept;
};
}
```

责任：**一次布局的派生结果**；每帧可重建；不持有 model/state。

### 5.5 `frame::DrawList`（绘制 DL）

```cpp
namespace table::frame {

enum class Stream : std::uint8_t { chrome, content, overlay, proxy };

struct ShapeOp {
    Rect r; glm::vec4 color, border;
    float radius, border_w;
    TargetKey target{0};
    Owner owner{};
};
struct TextOp {
    Rect r; glm::vec4 color; float baseline;
    const char *p; std::uint32_t len;
    TargetKey target{0};
    Owner owner{};
};
struct DrawList {
    std::array<std::vector<ShapeOp>, 4> shapes;
    std::array<std::vector<TextOp>, 4> texts;
    std::vector<ZoneRect> zones;
    std::vector<std::uint8_t> visual;
    std::uint32_t revision{0};
    TableId table{};
};
}
```

责任：**一帧的绘制指令**；只被 `platform::encode` 消费；不持有 GPU。

### 5.6 `command::Outcome`（命令结果）

见 §4.2。  
责任：**唯一“改变了什么”的返回值**；host 据此应用 effect、触发回调、决定重绘。

### 5.7 `Session`（组合根）

```cpp
namespace table::session {

struct Session {
    model::Table model;
    intent::State state;
    intent::Interaction interaction;
    std::uint32_t revision{1};

    // 派生缓存，只供输入 hit-test 使用；compose 写它，它不是真值
    frame::Layout last_layout;
};

// 唯一 Model/State 写入口；内部 CommandRouter 分发
Outcome dispatch(Session &, const command::Command &);

// 窄门：只写 Interaction，不碰 Model/State
void observe_pointer(Session &, Point screen, double now);
void update_hover(Session &, Point screen);
void clear_hover(Session &);

// 唯一派生入口；只写 last_layout 缓存，不写 Model/State
frame::Frame compose(Session &, Viewport, const Theme &);
}
```

`Frame`：

```cpp
namespace table::frame {
struct Frame {
    Layout layout;
    DrawList draw;
    std::uint32_t revision{0};
    TableId table{};
};
}
```

规则：

- `Session` **不持有** `screen/owner/win/sx/sy/mx/my/host`；这些去 `window::Binding`；
- `Session` 不 include `ui_new/GLFW/Vulkan`；
- `compose` 只写 `last_layout` 缓存；Model/State 永远只由 `dispatch` 写；
- `last_layout` 是派生缓存，可随时丢弃；不要把它当第二份真值。

### 5.8 两条链：draw 与 input

**draw 链（从状态到像素）：**

```text
State/Interaction 变化（revision++）
  → session::compose
      → frame::compose
          → LayoutComposer: columns → rows → scrollbars → edges
          → PaintComposer : frame → header → bands → cells → text
                            → separators → bars → edges → overlay
      → Frame { Layout, DrawList }
  → platform::encode
      → shader instances (entity_index = TargetKey)
  → GPU color + picking attachment
  → [PICKCHECK] platform::resolve → Target → Host
```

**input 链（从事件到状态）：**

```text
GLFW event
  → window::Host::route_event
      ├ position: session::observe_pointer / update_hover（只写 Interaction）
      ├ press/release/scroll/key/char → Command
      └ focus/cursor: Host 清 hover/pressed
  → session::dispatch
      → CommandRouter（feature 有序组合；无人认领 → Gesture fallback）
      → Outcome { handled, model_changed, state_changed, effects }
  → Host 应用 effects（剪贴板/回调/重绘）
  → 下一帧 compose 看到新的 State/Model
```

两条链只在一个地方合并：`TargetKey`。

- draw 侧：`DrawList.op.target = TargetKey`，`platform::encode` 写入 `entity_index`；
- input 侧：`hit(Layout, Point)` 得到 `Target`，`TargetKey` 是 `zone_key` 的同一布局；
- picking 侧：`platform::resolve` 把 `TargetKey` 还原为 `Target`。

**禁止**：paint 改 State；input 里算 Layout；Host 直接改 feature 内部；resolve/encode 进入 model/intent。

---

## 6. 组合：孤岛节点 → 完整表达

组合不是“再包一层类”，而是：

```text
有序节点列表 + 统一上下文 + 统一输出
```

三条规则：

1. **节点只认上下文和输出**，不认其他节点；
2. **顺序只写在 composer 里**，节点内部不写顺序；
3. **新增节点不改其他节点**，只在列表里插入。

这样既保持 SRP，又不会把主线切碎：主线是 composer 的调用顺序，节点是细节。

### 6.1 `CommandRouter`（负责“调节”命令）

```cpp
namespace table::command {

template <class F>
concept Feature = requires(F &f, Context &c, const Command &cmd) {
    { f.handle(c, cmd) } -> std::same_as<Outcome>;
};

template <class... Fs>
struct Router {
    std::tuple<Fs...> features; // 顺序 = 类型顺序，显式可见

    Outcome dispatch(Context &c, const Command &cmd)
    {
        Outcome total;
        std::apply([&](auto &...f) {
            (merge(total, f.handle(c, cmd)), ...);
        }, features);
        return total;
    }
};
}
```

目标：

- 每个 feature 是孤立节点，只认 `Context`（model/state/interaction/layout）和 `Command`；
- feature 之间不互相调用；
- 顺序写成类型列表，读者一眼看到“谁先谁后”；
- 没有 feature 处理时，才走 `Gesture` fallback；
- `merge` 合并 `Outcome`（handled/model_changed/state_changed/effects）。

**必须覆盖 ability11 的 `cmd_*` 全集**（不要漏）：

```text
cmd_hover / cmd_press / cmd_release / cmd_drag / cmd_wheel /
cmd_key / cmd_clear_filter / cmd_scroll_begin
```

`char` 不走 gesture，但仍是 `Command` 词汇；文本输入不能丢。

**feature 顺序是 F7 不动点，必须与 ability11 完全一致：**

```text
filter → find → sort → resize → reorder → freeze → checks → scroll
       → table_resize → selection → edit → clipboard → undo → menu → tooltip
       → gesture fallback
```

**禁止**：15 个 `if` 的隐式顺序；feature 直接改 host/window。

### 6.2 `FrameComposer`（负责“调节”绘制）

```cpp
namespace table::frame {

struct LayoutContext { const model::Table&; const intent::State&; const intent::Interaction&; Viewport; const Theme&; Layout& out; };
struct PaintContext  { const model::Table&; const intent::State&; const intent::Interaction&; const Layout&; const Theme&; DrawList& out; };

using LayoutPass = void(*)(LayoutContext&);
using PaintPass  = void(*)(PaintContext&);

constexpr std::array<LayoutPass, N> kLayoutPasses = { columns, rows, scrollbars, edges };
constexpr std::array<PaintPass,  M> kPaintPasses  = { frame, header, bands, cells, text, separators, bars, edges, overlay };

Frame compose(const model::Table &, const intent::State &, const intent::Interaction &,
             Viewport, const Theme &);
}
```

目标：

- `compose` 是唯一入口；
- 每个 pass 只做一件事：`columns` 只算列，`rows` 只算行，`bands` 只画行带，`cells` 只画单元格；
- pass 之间不互相调用；共享 `DrawList` 作为输出；
- 顺序写成 `constexpr` 列表，可读、可调、可测；
- 增加一种绘制 = 增加一个 pass + 在列表里插入，不改其他 pass。

**注意**：pass 顺序是行为，必须与 ability11 的绘制顺序一致（frame → header → row bands → cells → text → separators → bars → edges → overlay/menu/tooltip）。测试用 golden DL 锁住。

**零开销选择**：上面的 `std::array<函数指针>` 是“可读性优先”的写法，约等于每 pass 一次间接调用。  
若严格零抽象，改成：

```cpp
using PaintPasses = std::tuple<FramePass, HeaderPass, BandPass, CellPass,
                               TextPass, SeparatorPass, BarPass, EdgePass, OverlayPass>;
template <class... P> void paint_all(PaintContext &c) { (P{}(c), ...); }
```

两种都可以；核心建议用 tuple 折叠，诊断/可选 pass 才用函数指针表。

### 6.3 `window::Host`（负责“调节”窗口）

```cpp
namespace table::window {

struct Binding {
    session::Session *session{};
    ui_new::Widget *owner{};
    GLFWwindow *win{};
    float scale_x{1}, scale_y{1};
    Point pointer{};        // screen
    bool keyboard_focus{true};
    std::uint64_t draw_serial{};
};

struct Host {
    std::vector<std::unique_ptr<Binding>> bindings;
    std::vector<Binding *> draw_order;
    Binding *focused{}, *pressed{}, *hovered{};
    bool pointer_inside{true};
    std::uint64_t frame_serial{0};
    picking_result last_pick{};

    void attach(Binding &);
    void detach(Binding &);
    void route_event(const Event &);       // 唯一输入入口
    void begin_frame();
    void mark_drawn(Binding &);
    void apply_cursor(GLFWwindow *);
};
}
```

规则：

- `Host` 只做窗口级：输入路由、焦点、pressed/hovered、draw_order、帧号、cursor、pick 回读；
- `Host` 不计算 geometry、不改 model/state；
- `route_event` 把平台事件翻译成 `Command`，再调 `session::dispatch`；
- `Binding` 是“表在窗口里的呈现绑定”，不拥有 `Session`（由 screen/树拥有）；
- `Host` 的 `focused` 提升规则保持 ability11：detach focused 时提升存活的 binding。

### 6.4 `platform`（ABI 端口）

```cpp
namespace table::platform {

Instances encode(const frame::DrawList &, const Viewport &, const Theme &);
Target resolve(const PickingResult &);
}
```

- `encode` 唯一 GPU 出口；
- `resolve` 唯一 GPU 入口；
- 不把 `PickingResult` / shader instance 泄漏进 `table::model/intent/frame` 的语义层；
- `RENDER_VERSION_BIT_WIDTH` 与 C++ `render_version<16>` 的同步仍要保留；建议生成共享常量头，消除手写。

---

## 7. 依赖规则（必须机械化检查）

```text
contract   ← 无依赖（std/glm）
model      ← contract
intent     ← contract, model（只读 ids）
frame      ← contract, model, intent（只读）
command    ← contract, model, intent
session    ← contract, model, intent, frame, command
window     ← session, command, platform, platform 类型
platform   ← frame, contract

禁止：model/intent/frame/command/session 依赖 window/platform/ui_new/GLFW/Vulkan
禁止：hpp 出现全局可变状态、getenv、singleton、void*、跨层回调
禁止：window 直接改 session 私有字段；只走 session::dispatch
```

检查方式：

- `#include` 图（CI 脚本）；
- 公共头只暴露 schema + 门；`detail` 不被公共头 include；
- 签名扫描：公共函数无 `void*`、无 `std::function` 回调、无 `GLFWwindow*`；
- 命名空间扫描：`table::window` 不出现在 `table::model/intent/frame/command` 的 include 中。

---

## 8. “数据可以随便加”的目标路径

| 新增 | ability12 只改 |
|---|---|
| Model 字段 | `model::Table` + 消费它的 `layout/paint` pass |
| State 字段 | `intent::State` + `reset/reconcile` + 对应 feature + `layout/paint` |
| Interaction 字段 | `intent::Interaction` + `Host::route_event/advance` |
| Draw 字段 | `frame::DrawList` + 对应 `PaintPass` + `platform::encode` |
| 命令 | `command::Command` + `Outcome` + 对应 feature（不碰 host） |
| 视觉 pass | 新增一个 `PaintPass` + 插入 `kPaintPasses`（不碰其他 pass） |

验收时用两条真实数据验证：A 8 列 60 行、B 3 列 24 行，分别走完整 `dispatch → compose → encode`，确认加字段不穿透 host。

---

## 9. ability11 → ability12 映射

| ability11 | ability12 |
|---|---|
| `ui_table::table_data` | `table::model::Table` |
| `ui_table::view` | `table::intent::State` + `intent::Interaction` |
| `ui_table::geometry` | `table::frame::Layout` |
| `ui_table::frame_data` | `table::frame::DrawList` |
| `ui_table::systems` + `cmd_*` | `table::command::Router` + `Command` |
| `ui_table::table_ctx` | `command::Context` / `frame::PaintContext` |
| `table_arena` | `session::Session` + `window::Binding` 拆分 |
| `table_host` | `table::window::Host` |
| `table_glue::encode` | `table::platform::encode` |
| `picking_result` | `table::platform::PickingResult`（ABI 类型） |
| `effect` / `cb` / `changes` | `command::Outcome.effects` + host apply |
| `ScreenWidget::table_host_ctx` | `window::Host*`（只由 `Host` 持有） |
| `ui_table::selftest` | `table::test::core_selftest` |
| `hover_fn_ref` 全局 | `Host`/`Theme` 字段或 encode 参数（不留在核心） |
| `next_table_id` 全局 | `window::Host::allocate_table_id()` |
| `trace_on` 环境变量 | `Host` 诊断配置，不进入核心 |

---

## 10. 分阶段实施（每阶段可回滚）

> 原则：**先复制 ability11 成 ability12，保持 ability11 全绿；ability12 分阶段替换，不在 ability11 上原地拆。**

### Phase 0：基线
- 现有 `test_ui_ability12_table` 是 ability11 的格式化副本（BOM + clang-format，hpp 相同、cpp 仅格式差异）；先确认它能构建并通过 171/32/71/150；
- 如果不等价，重置为 ability11 的干净副本；不要假设当前副本已经是重构结果；
- 跑 171 / 32 / 71 / 150，记录；
- 打 tag/commit；保存 A/B 两张表的 golden（DL op 数量/类型/zone 序列、截图、PICKCHECK 计数）。

### Phase 1：类型/命名空间骨架（零行为变化）
- 新建 `table::contract/model/intent/frame/command/session` 头；
- 先用别名/包装把 ability11 类型映射进来；
- `ability12` 编译、测试数不变。

**Gate**：171/32/71/150；无行为 diff。

### Phase 2：拆 `view` → `State + Interaction`
- 按 §5.2/5.3 分字段；
- `reset/reconcile/sync/paint/input` 只改引用位置，不改语义；
- 更新测试引用。

**Gate**：同上；选择/编辑/滚动/菜单/tooltip/drag 行为一致。

### Phase 3：`Session` + `Binding` 拆分
- `table_arena` 拆成 `session::Session`（model/state/ix/revision/cache）与 `window::Binding`（owner/win/scale/pointer/keyboard/draw_serial）；
- `Host` 持有 `Binding`；
- 焦点/draw_order/detach 提升保持。

**Gate**：同上；MULTITEST 的 screen/lifecycle 组全绿。

### Phase 4：命令路由器 + `Outcome`
- `systems` 的 15 个 `if` → `Router<...>` + feature `handle`；
- `bool+fx` → `Outcome`；
- `table_ctx` 的 `void*` 删除；`Context` 只含 model/state/interaction/layout。

**Gate**：同上；命令优先级/顺序不变；effect 应用一致。

### Phase 5：`FrameComposer` + pass 列表
- `build_frame` 拆成 `kLayoutPasses` / `kPaintPasses`；
- 保持 ability11 的绘制顺序；
- 用 golden DL 对比 op 序列。

**Gate**：同上；golden DL 一致；PICKCHECK 150/0。

### Phase 6：`Host` 单入口 + `platform` 端口
- `Host::route_event` 唯一输入入口；
- `platform::encode/resolve` 唯一 ABI 端口；
- 删除 `make_aggregate/invoke/reflection` 在表格核心中的使用（保留在平台/窗口外壳，或改成显式 adapter）。

**Gate**：同上；grep 禁止符号；真实键盘/Esc；双表 PICKCHECK。

### Phase 7：清理
- 删除 ability11 旧路径；
- 公共头 < 300 行；每个文件 < 1500 行；
- 文档：一页 contract + 一页主线。

---

## 11. 测试与验收

### 11.0 验收方向：四维，不只是“测试绿”

| 维度 | 验收什么 | 不通过就是没完成 |
|---|---|---|
| 行为等价 | 171/32/71/150、真实键盘/Esc、双表 PICKCHECK | 任一数字下降或行为变化 |
| 结构正确 | 5 个门各只有一处实现；7 个 schema 归属正确；依赖无环 | 出现第二个写入口/隐边 |
| 可读性 | 3 跳解释点击/绘制；文件/类型/函数尺寸达标；无 `void*`/全局/反射 | 读者仍需跨层猜 |
| 性能 | 实例数/帧时间/DrawList 拷贝不劣化 | 明显变慢或引入 hot path 分配 |

**oracle 必须先行**：Phase 0 先固化行为，再重构；没有 oracle 的“自底向上”无法证明同功能。

### 11.1 行为基线（必须不变）

```powershell
TABLE_SELFTEST=1  -> 171 ok / 0 failed
TABLE_EVTEST=1    -> 32 ok / 0 failed
TABLE_MULTITEST=1 -> 71 ok / 0 failed
TABLE_PICKCHECK=1 -> 150 ok / 0 mismatch（A/B 两表都命中）
真实键盘 A/B：Right / repeat / Enter / F2 / Ctrl+F / Esc
Esc 取消 modal 后存活
```

### 11.2 新增结构验收（ability12 特有）

- 公共头只包含 schema + 门，不 include `window/platform/ui_new/GLFW/Vulkan`；
- `model/intent/frame/command/session` 中 grep 不到 `void*`、`getenv`、`hover_fn_ref`、`next_table_id`、`make_aggregate`、`invoke<`；
- 每个门只有一个实现/入口：
  - `window::route_event`
  - `session::dispatch`
  - `session::compose`
  - `platform::encode`
  - `platform::resolve`
- 新增字段路径测试：在 A/B 各加一个纯 Model 字段/纯 Draw pass，确认 host 不改；
- Golden DL：A/B 的 op 数、stream、zone 序列与 ability11 对比；
- 阅读门槛：新读者能在 3 跳内解释一次点击和一次绘制：
  - click: `Host::route_event → dispatch → feature → Outcome`
  - draw: `compose → layout pass → paint pass → encode`

### 11.3 性能

- 不要求更快；不允许明显变慢（实例数、帧时间、DrawList 拷贝次数不劣化）；
- 禁止在 hot path 引入 `std::function`/堆分配/反射。

---

## 12. 禁止事项（红线）

### 12.0 不动点（F1–F10）

| ID | 不动点 | 原因 | 验收 |
|---|---|---|---|
| F1 | `TargetKey` 位布局：part 5 / table 5 / row 13 / col 9，`kRowNone/kColNone` 哨兵 | 跨 C++/GLSL 的唯一合并键 | zone_key/zone_from 往返；A/B 两表 picking |
| F2 | `picking_result` 16B，`object_key` 8B，version 16-bit，primitive 16-bit | shader ABI | `static_assert` + PICKCHECK |
| F3 | 坐标空间：window logical → screen（`mx=wx*sx`），`geom.box` 已含 owner offset | 多表 sub-rect 命中 | 四点位 PICKCHECK 0 mismatch |
| F4 | draw_order 反向命中；begin_frame 在 tick 前；mark_drawn 每帧重建 | 重叠 z 序 | MULTITEST zorder 组 |
| F5 | 每窗口只 attach 一次输入；cursor 只由 Host 写一次 | 多表输入隔离 | 真实键盘 A/B + 光标 |
| F6 | `table_id` 1..31 唯一；detach focused 提升存活 binding | 键盘不死、路由不歧义 | MULTITEST detach/duplicate 组 |
| F7 | feature 顺序、命令语义、绘制顺序与 ability11 一致 | 行为等价 | 171/32/71 + golden DL |
| F8 | `frame_serial`（GPU 帧）与 `view revision`（CPU）分离；`accept_pick` 只比较帧版本 | 避免 per-table 版本误杀 | version 组 |
| F9 | `renderVersion` 只来自 Host；`on_pick` 不再比较 per-table 版本 | picking 协议 | PICKCHECK 150/0 |
| F10 | 核心不出现平台类型/全局/`void*`/反射；公共头只放 schema+门 | 可读性与依赖方向 | §7 检查脚本 |

**F1–F10 是“不可改点”；任何一条需要改，先停止重构，单独评审。**

除 F1–F10 外的硬红线：

1. 不改 `target_key` 位布局、`picking_result` ABI、`RENDER_VERSION_BIT_WIDTH=16`；
2. 不改 feature 优先级/命令语义/坐标空间/绘制顺序/焦点提升；
3. 不新增功能，不改 ability10/11；
4. 不引入 MVVM/observable/双向绑定/ECS；
5. 不把 `table` 核心拆成 15 个 namespace/文件；5–7 个模块上限；
6. 不在公共接口使用 `void*`、跨层回调、全局单例、字符串反射；
7. 不在 hot path 用虚函数/`std::function`（feature/pass 用模板/concept/constexpr 列表）；
8. 不一次性重写；每阶段必须能独立回滚并通过基线。

---

## 13. 给下一个 AI 的执行清单

- [ ] 读 ability11 设计文档 + 本规范 + 当前 `HANDOFF_table_multi_instance.md`；
- [ ] 确认现有 ability12 副本与 ability11 行为等价（或重置为干净副本），跑基线，打 tag；
- [ ] Phase 1：contract/model/intent/frame/command/session 骨架；
- [ ] Phase 2：拆 `view`；
- [ ] Phase 3：拆 `Session` / `Binding`；
- [ ] Phase 4：`Router` + `Outcome`；
- [ ] Phase 5：`FrameComposer` + pass 列表 + golden DL；
- [ ] Phase 6：`Host::route_event` + `platform` 端口 + 去核心 meta；
- [ ] Phase 7：清理、文档、性能检查；
- [ ] 每个 Phase 后：171/32/71/150 + 结构 grep + 真实键盘/Esc；
- [ ] 最终：一页 contract + 一页主线图 + 符号索引。

---

## 14. Definition of Done

- ability12 行为与 ability11 等价（全部基线通过）；
- 公共主线的 5 个门清晰可见，每个只有一处实现；
- `Model / State / Interaction / Layout / DrawList / Command / Outcome` 七个 schema 各有唯一责任和生命周期；
- `CommandRouter` / `FrameComposer` 用显式列表组合孤岛节点；
- 公共头无平台依赖、无全局、无 `void*`、无反射；
- 新增数据/新增 pass 的路径可预测，不穿透 host；
- 新读者能在 3 跳内解释“一次点击/一帧绘制”；
- ability10/11 不动，基线不回归。

### 14.1 可测的 DoD 指标

| 指标 | 目标 |
|---|---|
| 行为 | 171/32/71/150；真实键盘 Right/repeat/Enter/F2/Ctrl+F/Esc；Esc 取消 modal 存活 |
| 结构 | 5 个门各一处实现；7 个 schema 各一个 owner；include 图无环 |
| 禁止 | 核心 grep：`void*`、`getenv`、`hover_fn_ref`、`next_table_id`、`make_aggregate`、`invoke<` 全 0 |
| 尺寸 | 公共头 < 300 行；单文件 < 1500 行；单类型 < 200 行；单函数 < 80 行 |
| 参数 | 公共函数参数 ≤ 4；超过必须命名 Context |
| 可读 | 3 跳解释 click/draw；新读者 30 分钟内画出主线 |
| 扩展 | 新增 Model 字段/新增 Paint pass 不修改 Host；新增 Command 不修改 Layout/Paint |
| 性能 | 实例数不变；帧时间不劣化；hot path 无 `std::function`/堆分配 |

---

## 15. 一句话

**ability12 不是“再加一层架构”，而是把 ability11 的隐边变成显式的七个 schema、五个门、两个 composer；让主线一眼可见，让孤岛节点通过组合形成完整表达。**
