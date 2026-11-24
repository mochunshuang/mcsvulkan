# table 的最终目标、draw/input 链与手段

## 0. 一句话

table 不是控件类，而是：

> **数据 + 算法 + 组件化**
>
> 表 = RowSource × RowStage × Schema × ViewState  
> 意图 reduce、几何 derive、帧 present、实例 encode；
> 输入和绘制共享同一个 `target_key`，所有物都有 owner，分派只有一个点。

---

## 1. table 是什么

table 是一个**无窗口、无平台依赖的数据与算法对象**：

| 层 | 内容 |
|---|---|
| 数据 | 列、行、单元格、行身份、列身份、选择、编辑、勾选、滚动、布局 |
| 算法 | sort / filter / find / selection / edit / checks / resize / reorder / scroll / menu / tooltip / clipboard / undo |
| 组件 | `comp::view` 及其子状态，`systems` 是算法集合，`frame_data` 是呈现结果 |

table 不知道：

- GLFW；
- Vulkan；
- widget 树；
- shader；
- picking 附件。

这些只在 cpp 适配层。

table 知道：

- 自己有哪些行、列、单元格；
- 每个物是谁（`table_id + row_id + column_id + part`）；
- 输入命中了谁；
- 这一帧要画哪些 DL op。

---

## 2. 核心数据模型

```text
table_data
  ├─ cols: column[]        column_id, key/title/width
  ├─ cells: cell[col][row] column-major
  ├─ ids: row_id[]         稳定行身份
  └─ cell_id(r,c) -> cell_ref{row_id, column_id}
```

```text
target
  ├─ table_id
  ├─ part kind
  ├─ row_id / column_id     稳定身份
  └─ row_index / col_index  几何位置
```

```text
target_key = uint32
  part 5 | table 5 | row 13 | col 9
```

`target_key` 是 **picking 协议**：draw op、dao instance、picking 回读、input 全部用它。

`resolve(data, key) -> target` 是唯一解包点：

- 得到种类；
- 得到稳定 row_id/column_id；
- 得到几何索引；
- 不再有二义。

---

## 3. 六个接口、两个插槽、三条流、一个 arena

### 六个接口

| 接口 | 作用 |
|---|---|
| `sync` | 数据/视图/几何/系统对齐 |
| `input_event` | 唯一输入入口（目标：`input_frame + arbiter`） |
| `build_frame` | 组件 → DL |
| `encode` | DL → dao 实例 |
| `picking` | 几何/实例 → target_key |
| `render` | 适配层调用入口 |

### 两个插槽

1. **RowSource / RowStage**：行从哪来、经过哪些算法；
2. **Presenter**：DL 怎么变成实例（encode）。

### 三条流

```text
意图流   input -> target -> systems -> view
几何流   sync -> geometry
呈现流   build_frame -> DL -> encode -> dao
```

### 一个 arena

`frame_data` 持有 DL 的 arena：

- shape/text 的字符内存；
- 每帧 reset；
- 不跨帧持有指针。

---

## 4. 身份体系：天下之物皆有主

### 身份类型

```cpp
table_id
row_id
column_id
cell_ref { row_id, column_id }
owner_key { table_id, part, row_id, column_id }
```

### owner：每类物一个类型

```cpp
owner = variant<
  none_owner, frame_owner, header_owner, sep_owner, filter_owner,
  group_owner, cell_owner, check_owner, check_all_owner,
  bar_owner, edge_owner, menu_owner
>
```

每个 owner 自带：

- table；
- 稳定身份；
- 几何索引（需要时）。

### 规则

1. 没有 owner 的 DL op 不允许存在（zone=0 的 op 不画/不 pick）；
2. 没有 owner 的输入事件不允许进入系统；
3. owner 只能由 `to_owner(target)` 产生；
4. 分派只能由 `visit_owner` 产生。

---

## 5. draw 链

```text
table 组件
  -> build_frame
       shape_op { rect, color, border, target_key zone, owner who }
       text_op  { rect, color, baseline, target_key zone, owner who }
  -> encode
       ui_dao instance.entity_index = target_key
  -> shader
       像素 + picking 写 entity_index
  -> picking 回读 target_key
  -> resolve(data, key) -> target
```

### 不变量

- 每个 op 的 `zone` 是 `target_key`；
- 每个 op 的 `who` 是 `owner`；
- `zone` 与 `who` 必须指向同一个物；
- shader 不需要认识 owner，它只传主键；
- owner 的还原只在 `resolve` 一个点。

### 当前实现

- `shape_op/text_op` 已带 `target_key zone` 和 `owner who`；
- `build_frame` 已盖 table、填 owner；
- `encode` 仍只把 `zone` 写成 `entity_index`；
- shader 仍是 u32 协议；
- selftest 已覆盖“DL op 带 typed owner”。

---

## 6. input 链

### 目标结构

```text
GLFW event
  -> input_frame::build_change
  -> arbiter.feed
  -> gesture::Trait (click/drag/key/...)
  -> payload { target, x, y, ... }
  -> visit_owner(to_owner(payload.target), overloaded{...})
  -> systems
```

### 唯一 input_event

```cpp
method<"input_event">([](auto &&self, auto e) noexcept {
    const auto now = detail::now_of(self);
    self.now = now;

    using frame_t = decltype(input_frame::build_change(self.input_state, e));
    if constexpr (!std::is_void_v<frame_t>)
    {
        auto f = input_frame::build_change(self.input_state, e);
        self.arbiter.feed(self, f, now);
        input_frame::apply_change(self.input_state, f);
    }
})
```

### Trait 只写两件事

```cpp
struct Trait_press
    : gesture::trait_base<press_payload, static_string{"press"},
                          static_string{"on_press"}, policy::click>
{
    static void collect(time_point, payload_t &d,
                        const gesture::click_event &e) noexcept
    {
        d = {e.x, e.y, e.at};   // target 由 press 时 picking 解析
    }
};
```

- 不写坐标有效性；
- 不写 press/release；
- 不写 drag slop；
- 不写键盘 repeat；
- 这些都在 `input_frame` / `gesture`。

### 语义回调

```cpp
method<"on_press">([](auto &&self, const press_payload &e) {
    const target who = resolve(self.data, e.target_key);
    visit_owner(to_owner(who), overloaded{
        [&](const cell_owner &)   { self.sys.selection.set(self.ctx(), who); },
        [&](const header_owner &) { self.sys.reorder.begin(self.ctx(), who, e.x); },
        [&](const check_owner &)  { self.sys.checks.toggle(self.ctx(), who, e.shift); },
        [&](const edge_owner &)   { self.sys.table_resize.begin(self.ctx(), e.x, e.y, ...); },
        [&](const menu_owner &)   { self.sys.menu.item(self.ctx(), who); },
        ...
    });
})
```

### 规则

1. `input_event` 只有一个；
2. 事件状态只有一份（`input_state`）；
3. 手势识别只有一套（`gesture`）；
4. 系统调用只从 `on_*` 进入；
5. 系统签名只接 `target`/`owner`，不接原始坐标；
6. 禁止再有 `poll_keyboard` 这类旁路。

---

## 7. draw 和 input 如何交汇

```text
                   target_key
draw op.zone  ───────────────┐
                             ├──> dao / picking ──> input
picking entity_index ────────┘
                             │
                       resolve(data)
                             │
                          target
                             │
                        to_owner(target)
                             │
                    visit_owner / visit_drag
```

- 同一个 `target_key`；
- 同一个 `resolve`；
- 同一个 `owner`；
- 同一个 `visit` 分派点；
- draw 和 input 不是两套坐标系，而是同一个主键的两个方向。

---

## 8. 手段：为什么是 visit + variant

### 不用 switch(part)

```cpp
switch (part) { case cell: ...; case header: ...; default: ... }
```

问题：

- 新增 part 不会编译失败；
- default 吞错误；
- 处理散落；
- 编译期无保证。

### 用 owner variant + visit

```cpp
visit_owner(owner, overloaded{
    [&](const cell_owner&)   { ... },
    [&](const header_owner&) { ... },
    ...
});
```

保证：

- 每个 owner 类型必须有 handler；
- 新增类型编译失败；
- 分派只有一个点；
- 处理是类型化 lambda；
- 分发和统一处理不冲突：统一在 visit，差异在类型。

### drag 同理

```cpp
using drag_ref = variant<drag_none, drag_select, drag_col_w,
                         drag_header, drag_vthumb, drag_hthumb,
                         drag_table_size>;
visit_drag(mode, overloaded{...});
```

---

## 8.5 与 `test_ui_ability7_table` 的差异（未迁移完整）

ability7 的 selftest 有 86 条 check，ability9 目前 40 条；差距不只是“少测试”，有些是代码真的没迁过来。

### 8.5.1 选中/框选

| 能力 | ability7 | ability9 | 状态 |
|---|---|---|---|
| Shift+点击扩选 | `in.shift ? sel_extend : sel_anchor` | `selection.set` 永远 anchor，忽略 shift | **缺** |
| 拖拽框选越界自动滚动 | `drag::select` 在 body 外按 `L.step` 滚 `sy` | `drag_select` 只 `extend`，不滚动 | **缺** |
| 表头组点击选整组 | `first_col/last_col` 后 `sel_anchor + sel_extend` | `group_owner` handler 为空 | **stub** |
| 拖拽框选 | 有 | 有 `drag_mode::select` | 有，但无越界滚动 |
| 选区外框/列头高亮 | 有 | 有 | ✅ |
| Shift+方向键扩选 | 有 | `selection.move(..., shift)` 有 | ✅ |
| Space 切当前行 | 有 | 有 | ✅ |
| Shift+勾选范围 | `row_check_range` | `checks.toggle` 内做区间 | ✅ |

“选中框选”主要缺的是：

1. **Shift+点击扩选**；
2. **框选拖到视口外自动滚动**；
3. **表头组整组选择**。

### 8.5.2 编辑

| 能力 | ability7 | ability9 |
|---|---|---|
| 双击选词 | `edit_select_word` | 无 |
| 三连击全选 | 有 | 无 |
| Ctrl+←/→ 按词移动 | `utf8_word_left/right` | 无 |
| Ctrl+Backspace/Delete 删词 | 有 | 无 |
| 编辑框内 Ctrl+A/C/X/V | 有 | 部分有（copy/paste，词级无） |
| IME 预编辑 | 有 | 有 |
| 编辑提交/取消 | 有 | 有 |

### 8.5.3 导航可见性

| 能力 | ability7 | ability9 |
|---|---|---|
| `ensure_visible(row,col)` | 有 | **无** |
| 键盘移动后自动滚动 | 有 | 无 |
| find 命中后滚动到命中行 | 有 | 无 |

### 8.5.4 模型变更 / 回调

| 能力 | ability7 | ability9 |
|---|---|---|
| 插入/删除行 | `remove_row_at` | **无** |
| 插入/删除列 | `insert_column/remove_column_at` | **无** |
| 稳定 id 随增删维护 | 有 | 无 |
| `on_model_changed` | 有 | **无** |
| `on_selection_changed` | 有 | **无** |
| 行勾选变化回调 | 有 | 无 |

这是 integration 层的大缺口：现在 table 只能读静态 `table_data`，宿主无法通过 table 安全地增删行列。

### 8.5.5 列操作

`column_ops_feature` 目前是空壳：

```cpp
struct column_ops_feature
{
    template <class E> bool on_command(table_ctx &, const E &) { return false; }
    void rows(table_ctx &, std::vector<row_idx> &) {}
    void paint(const frame_ctx &, frame_data &) const {}
};
```

Fit / Reset sort / Hide / Show all 实际在 `menu_feature::item` 里；`column_ops` 注释写了四种操作但没有实现，也没有 insert/remove。

### 8.5.6 测试覆盖

ability7 有而 ability9 缺的 check 类别：

- zone 全 part 往返；
- cursor（header/frame）；
- 点非格区域清选区；
- 键盘导航（End/Ctrl+Home/左右上下）；
- wheel、竖/横条拖拽、轨道翻页；
- sort 三态；
- resize ne；
- reorder 拖动；
- edit 输入/Backspace/Enter/写回；
- Ctrl+←/Backspace 词级；
- undo 合并/重做/文本撤销不入栈；
- check 半选/全选/Space；
- filter 交互；
- find；
- wrap（折行/显式换行/列级开关/行高缓存）；
- 动态行 id / 行选择数组增长；
- 勾选回调 / 选区回调；
- 排序后行 id 稳定；
- 增删行触发 `on_model_changed`。

这些不是都“功能没有”，但至少说明 **ability9 没有验证过**；其中 Shift+click、自动滚动、ensure_visible、词级编辑、模型增删/回调是代码层确实缺的。

## 9. 当前实现与目标差距

| 项 | 当前 | 目标 |
|---|---|---|
| table 身份 | table_id/row_id/column_id/cell_ref | 保持 |
| picking | target_key + resolve | 保持 |
| draw op | 带 zone + owner | 保持 |
| draw 到 shader | 只有 u32 | 仍可 u32，但 owner 只在 resolve 还原 |
| input 入口 | 手写 `input_event` 分支 | `input_frame + arbiter` |
| 手势 | 手写 press/release/drag | `gesture` Trait |
| 键盘 | `poll_keyboard` 补丁 | `key_change` Trait |
| 分派 | `visit_owner` 已用于 press/drag | 全部语义入口 |
| 编译期 | owner/drag visit 已严格 | 输入/绘制全链 |

---

## 10. 迁移顺序

1. 定义 `table_arena`：
   - `input_frame::event_context`
   - `intent::arbiter<table_tree>`
   - 注入 data/view/prefs/geom/sys/font
2. 定义 `table_tree`：
   - `Trait_press`
   - `Trait_drag`
   - `Trait_key`
3. 新 `input_event` 接 `input_frame`；
4. 新 `on_press/on_drag/on_key` 走 `visit_owner`；
5. 验证：
   - selftest；
   - evtest；
   - 真实鼠标：点击/双击/拖拽/滚动/列宽/菜单；
   - 真实键盘：Enter/Esc/方向键/Ctrl+C/V；
6. 删除：
   - 手写 `input_event` 分支；
   - `poll_keyboard`；
   - `host.mx/my`、`ui.left` 手写状态。

---

## 11. 验证与回滚

### 构建

```powershell
cd E:/0_github_project/mcsvulkan/build
cmake --build . --target mcsvulkan-vma-test_ui_ability9_table -j 8
```

### 测试

```powershell
$env:TABLE_SELFTEST='1'
& 'E:/0_github_project/mcsvulkan/output/test_program/mcsvulkan/vma/test_ui_ability9_table.exe'

$env:TABLE_EVTEST='1'
& 'E:/0_github_project/mcsvulkan/output/test_program/mcsvulkan/vma/test_ui_ability9_table.exe'
```

### 回滚

```
build/_bak_before_arena.hpp
build/_bak_before_arena.cpp
```

hpp: `DA0AF42B...C4A3`  
cpp: `18EF3C34...2017`

---

## 12. 约束

- 单文件 + namespace，不拆文件；
- table core 不依赖 GLFW/Vulkan；
- 数据 + 算法 + 组件化，不做 Qt 式控件框架；
- 分派只在 `visit_owner/visit_drag`；
- 每个物必须有 owner；
- 每次改动必须 selftest + evtest 通过；
- 大改前必须备份。
