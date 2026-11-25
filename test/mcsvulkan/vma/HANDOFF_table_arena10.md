# ability10：table 的两条链（draw / input）与汇合点

## 0. 一句话

`test_ui_ability10_table` = ability9 的单文件工程，但 table 与宿主的接缝被重写成两条**直达**的链：

```
draw  : data + view -> geometry -> frame_data -> shader 实例 -> 像素
input : GLFW 事件 -> input_frame -> arbiter -> gesture::Trait -> on_* -> cmd -> systems/visit_owner
汇合  : target_key（draw 的 op.zone == shader 的 entity_index == input 的 resolve key）
```

两条链各只有一个入口、一个出口；输入不再按事件类型手写，走文件里现成的
`input_frame` + `intent::arbiter` + `gesture::Trait`（表自己的 `table_tree`）。

六个接口在 `table_ui` 里都有显式名字：`sync` / `build_frame` / `encode` /
`picking` / `draw`（render）/ `handle_event`（input_event）。它们只是同一套
实现的薄别名，避免“接口只存在于文档里”。

### 0.1 终极架构验收

| 目标 | 落点 |
|---|---|
| 数据 + 算法 + 组件化 | `ui_table` hpp：`table_data` / `comp::view` / `sys::*` |
| 天下有主：target_key / owner | hpp `zone_key` / `to_owner` / `visit_owner` |
| draw 和 input 共用协议 | `shape_op.zone` == `entity_index` == `hit_target` |
| 单一 input_event | `Table::input_event` → `table_arena::handle_event<E>` |
| input_frame + arbiter + gesture | `build_change` → `table_arbiter::feed` → `table_tree` → `on_*` |
| 编译期安全分派 | `consume_t` 决定哪些 leaf 接受哪类 change（`advance_mask`） |
| 回调 | `cb.on_model_changed / on_selection_changed / on_checks_changed` |
| 文件身份 | 头文件注释与 include 都是 ability10 |

---

## 1. 与 ability9 的结构差异

| 项 | ability9（失败版） | ability10 |
|---|---|---|
| 表状态 | 散在 `make_aggregate` 的 10 个 field + 3 个 `static` ref | 唯一 `table_ui::table_arena`（cpp:11680 附近） |
| 数据初始化 | `table_glue::data_ref()/prefs_ref()/font_ref()` 全局静态 | `make_screen` 内构造，move 进 arena |
| 输入入口 | 4 条：事件总线、`poll_keyboard`、`tick` 里 11 个 env 分支、render 里的 IME | 1 条：`Table::input_event` → `table_arena::handle_event<E>` |
| 输入架构 | 每种事件一个手写重载，各自 `translate -> cmd` | `input_frame::build_change` → `arbiter.feed` → `gesture` click/drag/wheel/key → `on_press/on_right_click/on_drag/on_wheel/on_key/on_char` |
| 键盘兜底 | `poll_keyboard` 把 GLFW key 直接 cast 成 `Key`（Tab→eSPACE，误触勾选） | 删除；键盘走同一个 arena，真实按键已验证 |
| draw 入口 | `render` 里混 IME、sx/sy、几何日志、encode、cursor | `draw(arena, box, view, builder)` = `draw_sync` → `draw_build` → `draw_encode` |
| encode | 直接写 `record_builder`，无法脱离 GPU 测试 | `template<Sink>`，生产 sink 是 `addInsteance`，测试 sink 是计数器 |
| 测试钩子 | `tick` 里 11 个 `TABLE_*TEST` env 分支 | 全部删除；`TABLE_SELFTEST`（core）+ `TABLE_EVTEST`（两条链 + arena 入口） |
| 模型变更 | `sync` 发现尺寸变化就 `view.reset()`，清掉排序/筛选/选区 | `view::reconcile()` 增量对齐，行/列 id 稳定 |
| 回调 | 无 | `cb.on_model_changed / on_selection_changed / on_checks_changed` |
| 拾取 | GPU 回读参与输入，但 `touch()` 让版本 1–2 帧内必然失配 | 输入用 `hit_target`（同一套几何）；GPU 回读只做 `TABLE_PICKCHECK` 验证 |

---

## 2. draw 链（数据 → 绘制数据 → shader → 像素）

```
table_arena.data / .view
        │  draw_sync(s, box)                         cpp:11299
        ▼
table_arena.geom        （列盒、可见行、滚动区、groups）
        │  draw_build(s)                             cpp:11310
        ▼
table_arena.frame       （shape_op / text_op；每个 op 带 zone + owner）
        │  draw_encode(s, view, sink)                cpp:11319
        ▼
shader 实例             （entity_index = zone_key）
        │  record_builder::addInsteance              cpp:11343
        ▼
GPU（test_ui_ability7.vert/frag）→ 颜色附件 + picking 附件 → 像素
```

入口只有 `table_ui::draw`（cpp:11325）。它按固定顺序调用三步，并统计
`shapes/texts/zoned/instances`（`table_arena::draw_info`）供测试断言。`render`
方法只做三件事：取 owner 尺寸、算 `sx/sy`、调 `draw` + 设置 cursor
（cpp:11693 的 `make_table` 末尾）。

### 不变量

1. 每个可见 `shape_op` / 文字 quad 的 `zone` 都是 `zone_key(target)`；
   装饰性 op（背景/外框/tooltip 底）统一归 `frame` owner，因此 `zone != 0`；
2. 每个 `zone != 0` 的 op 都带 typed `who`（`to_owner(target_of(...))`）；
   proxy 也在 owner 赋值**之后**生成；
3. `encode` 只把 `zone` 写进 `entity_index`，shader 不认识 owner；
4. owner 只在 `resolve` 还原（`table_glue::on_pick` 的验证路径）；
5. `frame.zones` 只生成不可见 proxy 实例（拾取用的命中区），不参与视觉；
6. proxy 的 zone 与 `hit_test` 同源：group 的 proxy 现在按 span 生成并带
   `first_col`（hpp:4862 附近），修掉了 ability9 里 group 回读 col=-1 的问题。

`TABLE_EVTEST` 对基础帧和 overlay（菜单+tooltip）帧都断言
`zero-zone == 0 && no-owner == 0`；新增 part 时 `to_owner` 会返回
`none_owner`，测试当场失败。

---

## 3. input 链（手势 → 数据更新 → 下一帧重画）

```
GLFW 事件（glfw_input_translation 事件总线）
        │  Table::input_event → table_arena::handle_event<E>
        ▼
input_frame::build_change   key_change / button_change / move_change /
        │                   scroll_event / char_change
        ▼
intent::debug_arbiter<table_tree>::feed
        │  唯一裁决点（ready / live / beats）
        ▼
table_tree（gesture 现成识别器）
        click(left/right) / drag / wheel / key_observer
        │  win → 反射调用 arena 的成员
        ▼
on_press / on_right_click / on_drag / on_wheel / on_key / on_char
        │  input_dispatch(cmd_*)
        ▼
ui_table::system_event → systems::handle / visit_owner → view / data
        │  apply_effects（fx_set_cell / fx_clipboard）
        ▼
view.ui.version++  →  下一次 draw_sync 重建 geometry/frame
```

### 3.1 table_tree

全部用 `gesture` 现成识别器：

| leaf | 消费 | 语义 | on_* |
|---|---|---|---|
| `click_template<Trait_press>` | left button | 按下（release 且未拖走） | `on_press`：press+release → 选中/排序/菜单项/勾选 |
| `click_template<Trait_right_click>` | right button | 右键单击 | `on_right_click`：打开列菜单 |
| `drag_template<Trait_drag>` | button + move | 超过 slop | `on_drag`：begin 补一发 down press，再 `cmd_drag`；end 发 `cmd_release` |
| `wheel_template<Trait_wheel>` | scroll | 滚轮 | `on_wheel`：`system_wheel`（Shift/横条横向） |
| `key_observer_template<Trait_key>` | key | 任意键（translation 事件） | `on_key`：Ctrl+V / Esc / `map_key` → `cmd_key` |

双击/三击不另设识别器：`on_press` 每次都发 press+release，core 的
`note_click` 按时间/位置累计。这样单击不会被“等待双击”的 300ms 吞掉。
hover 是帧派生态：`advance()` 每帧用 `input_state.pointer` + `hit_target`
更新 `system_hover`，不是手写输入分支。

### 3.2 键盘：translation 事件直达 arena

键盘只有事件源，没有第二套采样：

```text
GLFW keyCallback
  -> keyboard_event_dispatcher
  -> glfw_input_translation::forwarding<keyboard_event>
  -> Table::input_event -> table_arena::handle_event(keyboard_event)
  -> handle_key_event
  -> input_frame::build_change
  -> arbiter.feed -> key_observer -> on_key
```

- `on_key` 里只有三件事：Ctrl+V 粘贴、Esc 取消、其余 `map_key` 转 `cmd_key`。
- repeat：只有方向键/Home/End/PgUp/PgDn 接受 `key_change.repeat`；Ctrl+V、
  F2、字母等只认首次 press。`key_payload` 带 `repeat` 字段，`key_event` 也带。 
- 修饰键同时从 `input_state.modifier` 和 `input_state` 的 Ctrl/Shift 键状态读取，
  不依赖事件 mods 是否完整。
- headless 测试直接喂 `keyboard_event`，走同一个 `handle_event`；
  没有 `sample_keyboard` / `keyboard_from_events` 之类的第二状态源。
- Esc 是唯一平台例外：`Window::keyCallback` 在 press 时仍置 `shouldClose`，
  同时把 Esc 也分发进事件总线；`on_key` 取消 modal 并置
  `arena->esc_cancelled`，主循环看到该标志就把 `shouldClose` 清掉
  （无 modal 时不置标志，Esc 正常关窗）。Esc release 不再重复置 close。
- 真实键盘验证（PostMessage 走真实 WM_KEYDOWN 链）：
  `RIGHT` 使 cur_c 0→1，`F2` 进编辑，`ENTER` 提交，`Ctrl+F` 打开查找，
  `Esc` 取消编辑且进程存活。

### 3.3 回调

`table_arena::cb`：

| 回调 | 触发点 |
|---|---|
| `on_selection_changed` | `notify_change`：输入、筛选、模型对齐后的 `ui_signature` 选区变化 |
| `on_checks_changed` | `notify_change`：勾选位集 hash 变化 |
| `on_model_changed` | `fx_set_cell` 写回、`table_ui::insert_row/remove_row/insert_column/remove_column/set_cell` |

模型 API 会顺手 `view.reconcile(data)` + `ui.touch()`，宿主不用自己对齐。
`view::reconcile` 现在按 `row_id` 重排勾选和选区：中间插入/删除行时，
`checks.on / sel.cur_r / sel.r0 / sel.r1` 会跟着原来的行移动（TABLE_EVTEST 有
四条例证）。

宿主挂回调：

```cpp
if (auto *a = table_ui::current_arena())
{
    a->cb.on_model_changed     = [] { /* 行/列增删、单元格写回 */ };
    a->cb.on_selection_changed = [] { /* 选区变化 */ };
    a->cb.on_checks_changed    = [] { /* 勾选变化 */ };
}
```

### 3.4 IME

IME 预编辑是轮询量，不是事件：主循环每帧调一次
`table_ui::input_ime(*current_arena())`，只在 edit/filter/find 读
`ImmGetCompositionStringW`。render 不再有输入副作用。

---

## 4. 汇合点：target_key

```
                     zone_key(target)
draw op.zone ───────────────┐
                            ├──> frame_data / shader entity_index / picking 回读
input hit_target ───────────┘
                            │
                     resolve(data, key)
                            ▼
                         target
                            │
                     to_owner(target)
                            ▼
             visit_owner(...)  ← 唯一分派
```

- `target_key` 的位布局：`part 5 | table 5 | row 13 | col 9`（hpp:218）。
- `resolve` 只有一处（hpp:745 附近），`to_owner` 只有一处（hpp:821 附近）。
- `hit_target` 与 draw 共用 `geometry`；菜单/过滤按钮/边框的命中顺序与
  绘制顺序一致。
- `TABLE_PICKCHECK=1` 时，picking 回读改到**本帧 submit 之后**：等本帧 fence，
  读本帧 buffer，再与 `hit_target` 逐帧比对。移动鼠标也不再出现 MISMATCH
  （实测 72 次 ok / 0 mismatch）。

---

## 5. table core 的功能回补（以 ability7 为准）

| 能力 | ability10 实现 |
|---|---|
| Shift+单击扩选 | `selection_feature::set_or_extend`（hpp:2111）；`cmd_press.shift` 现在被读取 |
| 点非格区域清选区 | `none_owner` → `selection.clear` |
| 拖拽框选自动滚动 | `selection_feature::drag_extend`（hpp:2120）：出体区按 step 滚，目标夹到首/末行 |
| 分组头整组选择 | `geometry::groups`（hpp:1547）+ `hit_test` 带 first_col + `all_of_group`（hpp:2147） |
| 键盘可见性 | `ensure_visible`（hpp:1662），由 selection.move/to、edit Enter/Tab、find 调用 |
| 双击选词 / 三击全选 | `edit_feature::select_word/select_all`（hpp:2567）；`click_count` 真正维护 |
| Ctrl+←/→、Ctrl+Backspace/Delete | `utf8_word_left/right`（hpp:474/488）+ edit 的 ctrl 分支 |
| Undo 合并 | 0.8s 内连续输入只占一步 undo；undo/redo 后重新起段 |
| Ctrl+X | edit 内剪切（复制到 `fx_clipboard` + 删除选区） |
| Enter / F2 / Tab / PgUp/PgDn | Enter 下移（ability7 语义）、F2 编辑、Tab 行末换行、PgUp/PgDn 翻页、Ctrl+Home/End 首/末 |
| 排序后清选区 | header release → `sort.toggle` 后 `selection.clear` |
| 滚轮 | 指针在表内才滚；`dy * (row_h+row_gap)`；Shift/横条 → 横向 |
| 滚动条轨道 | 点 vbar/hbar 轨道翻一页（按点击点相对 thumb 的位置决定方向） |
| 8 向整表缩放 | 左/上把手方向取反；边只改单轴，角改双轴（`trs_edge`） |
| 筛选实时生效 | 输入每个字符就写 `filter.text[edit]`，不用等 Enter；Delete 清空 |
| 查找命中滚动 | 输入/退格/Enter 都会 `ensure_visible(hit)`，点别处关闭查找 |
| 双击列分隔条 | `autofit_column`（菜单 Fit 与双击共用，hpp:1849 附近） |
| 隐藏列导航 | 键盘左右/Home/End 只走 `g.col_index` 的可见列 |
| Shift+勾选区间 | 按 `view_rows` 的可见顺序勾选（排序/筛选后不会勾到看不见的行） |
| 折行渲染 | `build_frame` 逐行 emit，行高由 `wrap_lines` 算（hpp:4920 附近） |
| IME 预编辑 | `glfwGetWin32Window()` 取真正 HWND；edit/filter/find 都显示 composition |
| Esc 取消 | 主循环里在窗口关闭前拦截（`cancel_modal`）：编辑还原、筛选还原 orig、查找/菜单关闭；不会误关窗口 |
| 模型增删 | `table_data::insert_row/remove_row/insert_column/remove_column`（hpp:704–745）；`finalize` 只补新列 id |
| 模型变更后的 view | `view::reconcile`（hpp:1269）：保留 order/宽度/勾选/选区，只补新行列 |
| 变化通知 | `table_arena::cb.on_selection_changed / on_checks_changed / on_model_changed`，由 `input_dispatch` 与模型 API 触发 |

`ui_table::selftest()` 从 46 条 check 扩到 171 条，新增覆盖：
shift+click、点空白清选区、group 命中/整组选择、键盘滚动可见性、
拖拽越界滚动、Ctrl+词操作、双击选词、三击全选、模型增删/身份稳定、
筛选实时/清空、查找命中滚动、双击分隔条自适应、隐藏列导航、折行渲染。
`TABLE_SELFTEST_VERBOSE=1` 会逐条打印 ok。`TABLE_EVTEST` 另有 32 条链断言（含 arena 入口：click/key/wheel/drag/char + 回调 + owner 全覆盖）。

---

## 6. 验证

```powershell
# 构建
cd E:/0_github_project/mcsvulkan/build
cmake --build . --target mcsvulkan-vma-test_ui_ability10_table -j 8

# 1) table core（不建窗口）
$env:TABLE_SELFTEST='1'
& 'E:/0_github_project/mcsvulkan/output/test_program/mcsvulkan/vma/test_ui_ability10_table.exe'
# => [table-selftest] done, 0 failed

# 2) 两条链（不建窗口 / 不碰 GPU）
$env:TABLE_EVTEST='1'
& 'E:/0_github_project/mcsvulkan/output/test_program/mcsvulkan/vma/test_ui_ability10_table.exe'
# => [chain] ... 12 条 ok, done, 0 failed

# 3) 真实窗口 + GPU 回读校验
$env:TABLE_PICKCHECK='1'
& 'E:/0_github_project/mcsvulkan/output/test_program/mcsvulkan/vma/test_ui_ability10_table.exe'
# 稳定悬停：[pick] ok ...；刚移动 1 帧可能 MISMATCH（回读延迟，预期）

# 4) 坐标/事件诊断（调试输入映射时用，正常不用开）
$env:TABLE_GEOMLOG='1'    # 打印 box/body/scroll/列位置；布局变化时各一次
$env:TABLE_INPUTLOG='1'   # 打印 move/press/key/char 的原始坐标与分派目标
```

真实交互已验证（截图在 `_shots/`）：

- 单击选中、方向键逐格移动、Shift+单击扩选、分组头整组选择；
- 双击进入编辑并选词，输入替换、Enter 提交、Esc 取消并还原；
- 滚轮滚动、拖拽框选并自动滚动、列宽拖拽、列序拖拽、整表缩放、滚动条拖拽；
- 筛选：输入即过滤（Name/Status、两列 AND）、Delete 清空、Esc 取消并还原；
- 排序三态、查找命中并滚到目标行、勾选/全选、右键菜单 Fit/Hide/Show all；
- `_shots/live_*`（实时筛选）、`_shots/find_*`（查找滚动）、
  `_shots/combo2_*`（筛选+排序+多列 AND）、`_shots/esc_*`（Esc 取消）、
  `_shots/arena_drag.png` / `arena_shift.png` / `arena_menu.png` /
  `arena_check.png` / `arena_group.png` / `arena_tooltip.png`
  （全部经 arena 输入链，不是直接调 cmd）。

---

## 7. 本次功能核对发现的真实问题（已修）

在真实窗口 + 校准过的鼠标坐标下逐项核对，发现并修复：

| 现象 | 根因 | 修复 |
|---|---|---|
| input 仍是每种事件手写重载，`input_frame/arbiter/gesture` 没用上 | 历史遗留的 table_ui 适配层 | 改为单一 `handle_event<E>`；`table_tree` + 7 类 Trait；事件只做 build_change/feed/apply_change |
| 没有模型/选择回调 | ability7 的回调没迁 | 加 `cb.on_model_changed/on_selection_changed/on_checks_changed` + 模型 API |
| 文件头还写 ability9 | 复制演化残留 | 改为 ability10 |
| 点漏斗后输入，行不变（看起来 filter 失效） | ① 只有 Enter 才把 `filter.input` 写进 `filter.text`；② 漏斗热区只有 14px，点在图标左侧会落到 header | ① 输入/退格实时写 `filter.text`；② 热区扩到 19px 且避开分隔条 |
| 列宽拖不动 | 漏斗热区 `x1-18..x1` 完全覆盖分隔条 `x1-3..x1+3`，命中总是 filter_btn | 漏斗热区止于 `x1-5`，分隔条保留 |
| 查找命中在视口外时按 Enter 没反应 | `find` 只改 selection，不滚动；paint 只高亮可见行 | 输入/退格/Enter 调 `ensure_visible(hit)` |
| 查找条打开后点别处不关闭 | `find` 没有 `cmd_press` 分支 | 点任意处关闭查找，本次点击继续正常分派 |
| 筛选后选区停在不可见行，空格/F2 作用到看不见的行 | `sync` 不检查选区是否还在 `view_rows` | 当前格被筛掉时清选区 |
| 排序/筛选后 Shift+勾选会勾到不可见行 | 区间按数据行号遍历 | 改为按 `view_rows` 的可见顺序遍历 |
| 隐藏列后键盘左右/Home/End 落在隐藏列 | 导航按数据列号 clamp | 只走 `g.col_index` 的可见列 |
| 双击列分隔条不自适应 | `sep_owner` 只做 resize | 双击调 `autofit_column`（与菜单 Fit 共用） |
| wrap 行高变高但只画第一行 | `build_frame` 只用 `fit_text` 单行 | 按 `wrap_lines` 逐行 emit |
| IME 预编辑不显示 | `ImmGetContext` 把 `GLFWwindow*` 当 HWND | 改用 `glfwGetWin32Window()`；edit/filter/find 都显示 compose |
| `column_ops_feature` 空壳，逻辑散在 menu | 历史遗留 | 删除空壳；fit/hide/show/reset 抽成 `sys::*` 自由函数，菜单调用 |
| 点滚动条轨道无反应 | `bar_owner` 只处理 thumb | vbar/hbar track 点按翻页 |
| 拖上/左边框整表缩放方向反、边把手同时改两轴 | `table_resize` 只按 x/y 增量 | `trs_edge` 记录把手；左/上取反，边单轴、角双轴 |
| 真实键盘不稳：部分键根本不到 arena | Esc 被 Window 的 keyCallback 消费，没有进 dispatcher | Window 在 Esc press 时同时分发 keyboard_event；表格只保留 translation 事件源，Esc 的 modal 取消走 on_key + esc_cancelled，再无采样/双状态 |
| 鼠标移动帧 `TABLE_PICKCHECK` MISMATCH | 回读上一帧 buffer，且 filter 热区比 proxy 宽 5px | submit 后等本帧 fence 读本帧；filter proxy 与 `hit_target` 统一为 `x1-24,19` |

另外用 headless probe 核对了：sort 三态/稳定 id、filter 大小写/多列 AND/清空/筛选后编辑、
sort+filter、checks 区间/全选、find 命中循环、resize/reorder/freeze、table resize、
menu 四项/点外关闭、tooltip dwell、zone 往返、hit_test 各 part、encode 的 entity 解析。
结果全部通过；这些检查已固化进 `selftest()`。

---

## 8. 已知差异 / 取舍

1. **`TABLE_PICKCHECK` 同步等 GPU**：为了同一帧回读，验证模式会在 submit 后
   `waitForFences`。这只在 `TABLE_PICKCHECK=1` 时启用；正常渲染不等，交互
   输入本来就不依赖 GPU 回读，所以没有性能损失。
2. **selftest 171 条，与 ability7 持平**：新增覆盖 reorder 拖拽全过程、
   End/Ctrl+Home、ne 缩放、横条拖拽、编辑写回、undo/redo、勾选三态、
   wrap 显式换行、动态行、全 part zone/owner、cursor。
3. **未加 check 的细节**：部分纯视觉像素差异（阴影/圆角）仍靠真实窗口截图
   核对，不做逐像素断言；这不属于功能缺口。

---

## 9. 约束（保持）

- 单文件 + namespace，不拆文件；
- table core 不依赖 GLFW / Vulkan；
- 输入只有一个入口，draw 只有一个入口；
- 分派只在 `visit_owner` / `visit_drag`；
- 每个物必须有 owner；没有 owner 的 op 不画/不 pick；
- draw 和 input 共享 `target_key`，不许出现第二套坐标/主键。
