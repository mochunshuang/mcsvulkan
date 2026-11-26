> **这是唯一交接文档。** `HANDOFF_table_arena10.md` 是旧的单表记录，已恢复原状，不作为本次交接依据；`test_ui_ability11_table.*` 已由实现者修改，备份在 `build/_bak_a11_before_multi.*`。

# ability11 多实例迁移交接（按 review 修正版）

> 决策：**开新窗口做**。当前窗口 8 轮 / 1100+ 步、上下文约 45%，这次要动
> render / input / picking / IME / lifetime 五条边；本文件是修正后的唯一转交件。
>
> 当前单表基线未提交，仍是绿色：
>
> ```text
> TABLE_SELFTEST=1 -> 171 ok / 0 failed
> TABLE_EVTEST=1   -> 32 ok / 0 failed
> TABLE_PICKCHECK  -> 150 ok / 0 mismatch（实测；旧文档里的 72 是较早一次运行）
> 真实 WM_KEYDOWN：Right/F2/Enter/Ctrl+F/Esc 均走事件链
> ```
>
> 备份：
> `build/_bak_a10_multi_before.cpp|hpp`（当前绿色基线）
> `build/_bak_a10_before_input_cleanup.*`（加采样器之前的旧版）

---

## 0. Review 结论与本文的修正

Review 是对的：原方案有 6 个点会把“单表全局”换成“单窗口全局”，多表仍会坏。
本文已按这 6 点重写：

1. **范围写死**：本迁移只做**单窗口、多张 table**；不做多 GLFW 窗口。
   `table_host` 作为**窗口对象**（app `frameState`）的成员，不再是
   `window_host()` 函数 static。`inputTranslation()` 仍是“每进程一个输入系统”，
   但每窗口的 host 各自 attach；多窗口版本另开任务。
2. **on_hover 的诊断 host 上下文**：`ScreenWidget` 持有 `table_host*`；
   `on_hover(..., ScreenWidget*)` 通过它路由（仅 TABLE_PICKCHECK/诊断），测试直接调用同一个 `on_hover`
   并绑定 test host，不再只调 `h.on_pick` 冒充路由。
3. **版本分层**：`renderVersion` 保持整帧 picking 标签；`view.ui.version` 只是
   每表 CPU 修订号。`on_pick` 不按表比较 `render_version`；禁止全局 `version()` 取 max。
4. **table_at 按绘制顺序**：host 每帧记录 `draw_order`，`mark_drawn` 在
   `table_arena::render` 里调用；`table_at` 反向遍历 draw order，不按 attach 顺序。
5. **生命周期**：`table_host::detach` + `table_arena::~table_arena` 调用；
   host 的析构清空残留指针；host 与窗口同寿命，表可先于窗口销毁。
6. **CPU hover 的 z 序门禁**：host.position 更新 hovered；只有 hovered 表写\n   view.ui.hover，其余清空；重叠时按 draw_order。\n7. **多表测试走真实 on_hover**；覆盖 hover/pick、点击、键盘、字符、焦点、
   重叠 z 序、detach 后不再路由。

另外修正了原方案的 `table_at` 坐标方向、每表 attach、Esc 空 focused、
PICKCHECK 数字。

### 0.1 `hover_fn` 该不该实例化？

**不该。** 这里的 static fn 不是“可变实例状态”，而是一个**类级、单调递增、
稳定不变的 callback 下标**：

```text
hover_fn_ref() 只分配一次
hoverPool[hover_fn] 只绑定一次（无状态 on_hover）
实例身份不靠 hover_fn，而靠 picking_result.entity_index 里的 table 位
```

把它改成“每表一个 hover_fn”会带来：

- hover pool 随表增长，picking 实例布局/池索引更复杂；
- 每表回调仍需要 `target_key.table` 才能从 picking 找到实例，identity 没变；
- 多窗口时反而要维护 per-window pool，成本更高。

所以保留类级 `hover_fn`，只把 `on_hover` 改成**无状态路由**：

```text
on_hover(r, screen)
  -> screen->table_host_ctx
  -> zone_from(r.key.entity_index).table
  -> arena
```

类级 static 的边界：**只允许放不随实例变化的东西**。`hover_fn` 下标、
target_key 布局、owner/visit、policies 属于类级；`current_arena`、
`active_table`、`last_pick` 属于窗口/表，必须搬走。`hoverPool` 目前也是
进程级池；单窗口下它和 `hover_fn` 一样安全（回调无状态），多窗口再按窗口拆池，
本次不动。

### 0.2 范围决策：GPU hover 不在运行时

这一点必须先承认当前代码事实：`hoverManager.hover(...)` 在 ability10/11 现有代码中
**没有调用点**；`hoverPool().bind(&on_hover)` 只注册了回调下标，`on_hover`
不是生产 hover 的消费者。真实 hover 是：

```text
host 转发 position
  -> 每张表 advance()
  -> system_hover()
  -> view.ui.hover
```

因此本任务选择 **A：GPU hover 不接回运行时**：

- `hover_fn_ref` / `on_hover` / `accept_pick` 只服务 `TABLE_PICKCHECK` 和
  多表路由的诊断测试，不是生产 hover；
- 多表 hover 的生产正确性由 CPU 路径断言（`view.ui.hover`），不是由
  `on_hover`；
- `accept_pick` 只放在 `TABLE_PICKCHECK` 的**同帧读回点**，不是放在
  `on_hover` 的运行时路径里；
- 如果以后要接 B（运行时 GPU hover），必须在 picking fence wait/map 之后
  立刻 `accept_pick -> hoverManager.hover -> on_hover`，且保证同帧；
  本文不实现，避免“测试路径被当成生产”。

这样文档与代码一致，不会让实现者去修一个当前没有消费者的 `on_hover`。


---

## 1. 现状：对象是对象，单例是单例

`table_arena` 本身已经是对象：`data/prefs/font/id`、`view/geom/sys/frame`、
`pointer/pick/fx`、`arbiter/input_state`、`cb` 都是成员。

问题在集成层：

| 现状 | 位置 | 归属 |
|---|---|---|
| `current_arena()` 全局 `table_arena*` | cpp:11485 | 窗口 host 的 `focused` |
| `active_table()` 全局 `table_id` | cpp:11679 | 窗口 host 的 `focused` |
| `hover_fn_ref()` 静态回调下标 | hpp:293 | 类级 OK；回调必须无状态 |
| `hoverPool/on_hover` 路由到 current | cpp:4153 / 12148 | host，按 `entity_index.table` |
| 每张表 `inputTranslation().attach(agg...)` | cpp:10867 | 窗口 host attach 一次 |
| `input_ime/version/on_pick/modal` 走 current | cpp:12031/12075/12240/13968 | host |
| 表名注释 “exactly one Table node” | cpp 附近 | 删除/改写 |

单表时这些恰好可用；多表时 hover 会写错表、A/B 同收字符、焦点未定时所有表收键盘、
picking 只按 current 找表。

---

## 2. 目标结构

### 2.1 类级（可 static/constexpr）

`target_key` 位布局、`zone_key/zone_from`、`to_owner/visit_owner/visit_drag`、
event 类型、`input_frame`、`arbiter`、gesture 模板、策略常量、无状态 hover 回调。

### 2.2 对象级 `table_arena`

保持现有成员，只加：

```cpp
bool keyboard_focus{true};       // host 设置；headless 默认 true
table_host *host{nullptr};       // 仅用于 ~table_arena / detach
```

arena 不再写 `active_table()`；`on_key` 只检查 `keyboard_focus`。

### 2.3 窗口级 `table_host`

**单窗口前提**：host 是 app 窗口对象（`named_resource::frameState`）持有的
shared 对象。`frame_state` 定义在文件很早（cpp:727），此时 `table_host` 还不完整，
所以用 **前置声明 + `std::shared_ptr`**，不要直接放值类型：

```cpp
struct table_host;                 // 在 frame_state 之前前置

struct frame_state
{
    ...
    std::shared_ptr<table_host> tableHost;   // make_screen 里 make_shared
};
```

`ScreenWidget` 只存裸指针（不拥有，先 forward declare `table_host`）：

```cpp
struct ScreenWidget
{
    ...
    table_host *table_host_ctx{};    // make_screen 绑定，指向 frameState 的 host
};
```

`make_screen` **不能每次 make_shared 覆盖 host**。分两层：

```cpp
table_host &window_tables() noexcept
{
    auto &slot = named_resource::frameState().tableHost;
    if (!slot)
        slot = std::make_shared<table_host>();
    return *slot;
}
```

- `make_screen` 只负责**一个窗口一个 screen**；内部调用 `window_tables()`，
  不替换已有 host。
- 多张表是**同一个 screen 里的多个 Table 节点**，不是多次 `make_screen`。
  新增构造期 API：

```cpp
struct table_spec
{
    ui_table::table_data data;
    ui_table::prefs prefs;
    ui_table::font font;
    float flex{1.0F};          // Row 里占的份额
};

// 构造期一次建好 N 张表；内部 Row/SizedBox/Flex 布局
ScreenWidget make_table_screen(double w, double h, std::vector<table_spec> specs);
```

`make_screen(w,h)` = `make_table_screen(w,h,{一个 spec})`。如果以后要运行时
`add_table`，必须让 ScreenWidget 暴露可变的 Flex root 并 relayout；本任务不做，
文档明确“**不支持运行时 add_table**”，避免第二次 make_screen 的歧义。

`make_table_screen` 里对每个 spec：

```cpp
auto &hs = window_tables();
auto ro = render::asRenderObject(std::move(agg), [](auto &) {});
auto &arena = ro->agg.arena;
arena.host = &hs;
hs.attach(arena);
```

**attach 必须在 `asRenderObject(std::move(agg))` 之后**：`ro->agg.arena` 的地址
此时才稳定；如果先 attach 再 move aggregate，host 保存的是旧地址，悬空。
之后的 `ScreenWidget`/`unique_ptr` 移动不再移动 aggregate 本体。

`screen.table_host_ctx = &hs`；`ScreenWidget` 只存裸指针，host 由 frameState
的 shared_ptr 持有。

**不要**在 `make_screen` 里无条件 `std::make_shared`；第二次 screen 会换 host，
旧 screen 的 `table_host_ctx` 立刻悬空。`window_tables()` 必须幂等。

仍限定**一个窗口一个 screen**。如果旧 screen 被销毁而新 screen 建立，旧表的
`detach` 会从 host 移除；不要同时让两个 screen 共享一个 host 并期望输入按
screen 隔离（那需要 host 也按 screen 分组，超出本次范围）。



`table_host`：

```cpp
struct table_host
{
    std::vector<table_arena *> tables;
    std::vector<table_arena *> draw_order;
    table_arena *focused{};
    table_arena *pressed{};
    table_arena *hovered{};
    struct glfw_input_translation::scoped_binding binding{};
    bool input_attached{};
    double pointer_x{}, pointer_y{};
    bool pointer_inside{true};
    std::uint64_t frame_serial{0};
    picking_result last_pick{};

    ~table_host();

    void attach(table_arena &a);
    void detach(table_arena &a) noexcept;
    table_arena *find(table_id id) const noexcept;
    table_arena *table_at(double wx, double wy) const noexcept;
    void set_focus(table_arena *a) noexcept;
    void begin_frame() noexcept;
    void mark_drawn(table_arena *a) noexcept;
    template <class E> void input_event(const E &e);

    // renderVersion 的唯一消费者：读回点只接受本帧的 picking
    bool accept_pick(const picking_result &r) noexcept;
    void clear_hover() noexcept;
    void apply_cursor(GLFWwindow *win) noexcept;
    void on_pick(const picking_result &r);   // 已通过 accept_pick
};
```

`renderVersion` 的消费点写死：

```cpp
bool table_host::accept_pick(const picking_result &r) noexcept
{
    if (r.key.object_type == 0xFFFFFFFF)
        return false;            // 无命中，不更新 last_pick
    if (r.render_version() != static_cast<std::uint32_t>(frame_serial))
        return false;            // 旧帧/别的帧，直接丢弃
    last_pick = r;
    return true;
}
```

`on_hover` / `TABLE_PICKCHECK` 读回后先 `accept_pick`，再按
`zone_from(r.key.entity_index).table` 路由到 arena。`on_pick` 内部**不再比较**
`render_version`，也不再比较 `view.ui.version`。如果某条路径没有 `screen/host`，
在读回点就丢弃，不进入 on_pick。


`attach` 只在第一次时：

```cpp
inputTranslation().attach(binding, *this);
input_attached = true;
if (draw_order.empty())
    draw_order.push_back(&a);   // 首帧 render 前也能 table_at
```

`attach` 还要 `a.host = this;`、`tables.push_back(&a)`，若 `focused==nullptr`
则 `set_focus(&a)`；`set_focus` 同步所有表的 `keyboard_focus`。

Table aggregate 的 `input_binding` / `input_event` / `postBindInputTranslation`
删除；`asRenderObject` 传空 handler。文件里不允许再出现
`inputTranslation().attach(agg...`。

**table_id 上限**：`target_key` 的 table 只有 5 bit（hpp 位布局），所以单窗口
最多 **31 张表**（0 保留）。`table_host::attach` 里显式检查：

```cpp
if (a.id.value == 0 || a.id.value > 31)
    throw/return;  // 或 next_table_id 分配时保证 <=31
```

多窗口若超过 31，必须把 table 位扩宽或改成注册表句柄；不在本次范围。

### 2.4 hover_fn：类级、诊断-only

`hover_fn_ref()` 保留（所有表共用一个回调下标）。`on_hover` 只在
`TABLE_PICKCHECK` / 多表诊断里调用，不是运行时 hover；生产 hover 走 CPU
`position -> advance -> system_hover`。

```cpp
inline void on_hover(picking_result r, bool enter, InputCtx &, ScreenWidget *screen)
{
    if (screen == nullptr || screen->table_host_ctx == nullptr)
    {
        ui_table::trace("[hover] dropped: no screen/host");
        return;
    }
    auto &h = *screen->table_host_ctx;
    if (!h.accept_pick(r))
        return;                       // 帧级 renderVersion 的唯一比较点
    h.on_pick(r);                     // 诊断：只更新 pick/last_pick
}
```

诊断调用点只有 `TABLE_PICKCHECK` / `TABLE_MULTITEST`；生产路径不会调用
`on_hover`。`r.render_version()` 只在 `accept_pick` 比较一次，`on_pick` 不再看版本。

`table_host::on_pick`：

```cpp
auto z = zone_from(r.key.entity_index);
table_arena *t = find(z.table);
if (t == nullptr)
    return;
on_pick(*t, r);                   // 原 free function；版本已在 accept_pick 过滤
```

**版本协议只保留帧级**：

- `renderVersion` 是“这一帧 GPU picking 的帧一致性标签”，写进 shader 实例，
  由 `drawFrame` 统一设置；它**不是**某张表的 `view.ui.version`。实现时直接取
  `host.frame_serial`（`begin_frame` 已递增），删除 `table_ui::version()`；
  不再读任何表的 version；
- `table_arena::view.ui.version` 只是 CPU 状态修订号，用于 hover 去重/重绘，
  不参与 picking 过滤；
- 因此 `on_pick` **不做** `r.render_version() != t->view.ui.version` 的比较。
  这解决 review 指出的 A=10、B=12 时必然误杀一张表的问题，也不动 uvec4 ABI；
- 若以后要按表丢弃旧 picking，必须给 picking 增加每表的 `table_revision`
  字段（改 shader/实例布局），那是独立协议升级，不在本次范围；
- `table_ui::version()` 从生产路径删除，不再有全局 max。

`TABLE_PICKCHECK` 仍按 `entity_index.table` 找表做 `on_pick` 验证，但不比
`view.ui.version`；帧级版本不参与这里。

**hover 职责写清**：

- GPU picking / `on_pick`：只回答“指针下是哪个 `target_key`”，更新该表的
  `pick`/`last_pick` 供验证/缓存；**不负责** `view.ui.hover`。
- 真正的 hover/cursor：host 把 `position2d` 转发给所有表，各表 `advance()` 用
  自己的 pointer + geometry 调 `system_hover`，写自己的 `view.ui.hover`。
- 多表测试必须两者都断言：
  - 转发 position 后，只有指针所在表的 `view.ui.hover` 更新；
  - 调真实 `on_hover` 后，只有目标表的 `pick` 更新；
  不能只断言 pick 就宣称 hover 路由正确。

**cursor 是窗口级状态，不能每表写**。现在每张表 render 末尾都
`apply_cursor(...)`，非 hovered 表会把 arrow 写在 hovered 表的 hand/ibeam 后面。
改为 host 统一写：

```cpp
// drawFrame: screen.render 之后
screen.render(ctx);
host.apply_cursor(win);     // 只看 host.hovered；null 时 arrow
```

```cpp
void table_host::apply_cursor(GLFWwindow *win) noexcept
{
    if (win == nullptr) return;
    if (hovered != nullptr)
        table_glue::apply_cursor(
            win, ui_table::cursor_for(hovered->view.ui.hover.to_zone()));
    else
        table_glue::apply_cursor(win, ui_table::cursor::arrow);
}
```

删除每张表 `render` 里的 `apply_cursor`。cursor 的归属从此只有 host 一个点。

**窗口/指针离开必须清 hovered**：

| 事件 | 处理 |
|---|---|
| `cursor_enter_event{entered=false}` | `hovered=nullptr`；对旧 hovered 表 `system_hover(view,{})` |
| `window_focus_event{focused=false}` | 同上，并清 `pressed` |
| position 在所有表外 | `hovered=nullptr`；`begin_frame`/advance 会清旧表 hover |

```cpp
void table_host::clear_hover()
{
    pointer_inside = false;
    if (hovered != nullptr)
        ui_table::system_hover(hovered->view, {});
    hovered = nullptr;
}
```

否则鼠标移出窗口时没有新的 position，`table_at(pointer)` 仍返回旧表，旧表
会继续 hover/tooltip，cursor 也保持旧形态。

- 这不改变 `hover_fn` 的类级稳定下标：它是**无状态路由器**，不是 hover 状态
  持有者；实例身份来自 `target_key.table`，不需要 per-table hover_fn。

### 2.5 输入：host 一次 attach，按 table/focused 路由

`table_host::input_event<E>`：

| 事件 | 路由 |
|---|---|
| `position2d_event` | `pointer_inside=true`；更新 `pointer_x/y`；`hovered = table_at(pointer)`（按上一帧 draw_order）；转发给**所有表**更新各自 input_state 指针，不点击 |
| `mousebutton_event` press | `table_at(pointer)`；命中则 `set_focus(t)`、`pressed=t`、`t->handle_event(e)`；不中忽略 |
| `mousebutton_event` release | 优先 `pressed`（拖出表外仍回原表），否则 `table_at`；发完清 `pressed` |
| `scroll_event` | `table_at(pointer)` |
| `keyboard_event` | `focused`（空则忽略） |
| `char_event` | `focused`（空则忽略） |
| `cursor_enter_event` | `entered=false`：`clear_hover()`（并置 `pointer_inside=false`）；`entered=true`：`pointer_inside=true`，下一次 position 重新命中 |
| `window_focus_event` | 转发所有表；`focused=false` 时 `clear_hover()` + 清 `pressed` |

`table_at` 坐标必须与 `set_pointer` 正向变换一致。当前代码是：

```cpp
s.mx = s.wx * s.sx;       // screen/window scale; geom.box is screen-absolute
s.my = s.wy * s.sy;
```

所以窗口 → screen 绝对坐标是：

```cpp
const float lx = wx * a.sx;       // 不是 + owner.offset，geom.box 已是绝对坐标
const float ly = wy * a.sy;
if (a.geom.box.contains(lx, ly)) ...
```

review 这一条是对的；上一版文档把方向写反了。统一走 `set_pointer` 的同一公式，
不要另写一套。

如果 `ScreenWidget/Widget` 已经维护子节点的全局 rect，优先用 widget 命中结果
（screen hit-test → widget → arena）再回退到这里的 box 计算；手算只作为没有
布局命中 API 时的兜底。

### CPU hover 的 z 序门禁（重叠表）

host 转发 position 给所有表，是为了让每张表的 `input_state.pointer` 一致；
但**只有 host.hovered 允许写 `view.ui.hover`**，否则重叠时 A/B 同时 hover。

`table_host::begin_frame()` 在清 draw_order 之前先按上一帧顺序重算 hovered：

```cpp
void table_host::begin_frame()
{
    ++frame_serial;
    hovered = pointer_inside ? table_at(pointer_x, pointer_y) : nullptr;
    draw_order.clear();
}
```

`table_arena::advance()`（定义必须在 table_host 完整之后，或在 host 里提供
`is_hovered(const table_arena&)`）：

```cpp
if (host != nullptr)
{
    if (host->hovered == this)
    {
        set_pointer(*this, input_state.pointer_x, input_state.pointer_y);
        view.ui.pointer = {mx, my};
        ui_table::system_hover(view, pointer_target(*this, mx, my));
    }
    else
    {
        ui_table::system_hover(view, {});   // 清掉旧 hover/tooltip
    }
}
else
{
    // headless 单表：保持原来的 pointer -> hit -> hover
}
```

规则：

- 重叠时只有 draw_order 最上面的表 hover；
- 指针移出所有表（`table_at == nullptr`）时，上一张表的 hover 被清空；
- 多表测试增加：重叠区、`mark_drawn(b); mark_drawn(a);` 后 advance，
  只有 A 的 `view.ui.hover` 非空，B 为空；交换 draw_order 后只有 B；
- cursor/tooltip 在 render 里读各自 `view.ui.hover`，因此同一帧只有一个表
  写 cursor/tooltip。

这条门禁是生产 hover 的核心；`on_hover` 诊断不参与。

反向遍历 `draw_order`，不是 `tables`。

`draw_order`：

```cpp
void table_host::begin_frame()
{
    ++frame_serial;
    hovered = pointer_inside ? table_at(pointer_x, pointer_y) : nullptr;
    draw_order.clear();
}

void table_host::mark_drawn(table_arena *a)
{
    if (a == nullptr) return;
    if (a->draw_serial == frame_serial) return;
    a->draw_serial = frame_serial;
    draw_order.push_back(a);
}
```

`table_arena` 加 `std::uint64_t draw_serial{}`；`render` 入口调用
`screen->table_host_ctx->mark_drawn(&s)`。

具体到 `drawFrame`：

```cpp
auto &host = window_tables();       // 或 screen->table_host_ctx
host.begin_frame();                 // MUST precede screen.tick()/advance
const std::uint32_t renderVersion = static_cast<std::uint32_t>(host.frame_serial);
screen.tick(...);
screen.render(ctx);                 // 每张表 render 里 mark_drawn
frame.replay(commandBuffer, currentFrame, renderVersion);
```

`table_ui::version()` 这一行删除；`renderVersion` 只来自 host 的帧号。
如果一帧内 `screen.render` 被调用多次，`mark_drawn` 用 `frame_serial` 去重，
`renderVersion` 仍只取一次（同一帧号）。

**时机写死**：主循环顺序是 `input -> begin_frame -> screen.tick -> screen.render`，
`begin_frame()` 必须在 **`screen.tick()` 之前**，不只是 `screen.render()` 之前：

```text
input
  -> begin_frame()   // 更新 hovered（旧 draw_order）、清 draw_order、++frame_serial
  -> screen.tick()   // 各表 advance() 使用本帧 hovered
  -> screen.render() // mark_drawn 重建 draw_order，供下一帧输入
```

因为 `table_arena::advance()` 是 `screen.tick()` 里调的；如果 begin_frame 放在
tick 之后，advance 用的是上一帧/空 hovered，重叠表 hover 会慢一帧，第一帧
所有表都走“清 hover”。因此：

- 本次输入使用的是**上一帧已完成 render 的 draw_order**；
- 本帧 render 期间 `mark_drawn` 重建 draw_order，供下一帧输入使用；
- 第一帧输入时 draw_order 为空，`table_at` 回退到 attach 顺序（attach 时 push
  一份初始顺序）；
- 重排 widget 树后最多一帧内点击仍按旧 z 序，下一帧起正确。

headless 测试可以直接 `h.begin_frame(); h.mark_drawn(&b); h.mark_drawn(&a);`。

焦点/键盘：`set_focus(a)` 更新所有表 `keyboard_focus = (t == a)`；
`on_key` 只 `if (!keyboard_focus) return;`。`on_press/on_drag` 删除
`active_table() = ...`。

### 2.6 生命周期

```cpp
void table_host::detach(table_arena &a) noexcept
{
    std::erase(tables, &a);
    std::erase(draw_order, &a);
    if (focused == &a)
    {
        focused = nullptr;
        if (!tables.empty())
            set_focus(tables.front());   // promote survivor; otherwise keyboard dies
    }
    if (pressed == &a) pressed = nullptr;
    if (hovered == &a) hovered = nullptr;
    a.host = nullptr;
    a.keyboard_focus = true;
}
// table_arena 先只声明，不在类内 inline 定义
struct table_arena { ... ~table_arena(); table_host *host{}; ... };

// table_host 完整定义
struct table_host { ... ~table_host(); ... };

// 再定义 arena 析构（此时 table_host 完整）
inline table_arena::~table_arena()
{
    if (host) host->detach(*this);
}

// 最后定义 host 析构：如果 host 先于 arena 死，先把 arena->host 置空
inline table_host::~table_host()
{
    for (auto *a : tables)
        if (a) a->host = nullptr;
}
```

顺序必须是：`~table_arena` 声明 → `table_host` 定义 → `~table_arena` 定义 →
`~table_host` 定义；不能在 arena 类内 inline 写 `host->detach`，否则不完整类型
编译不过。

`arena.host` 只在 **所有 move 完成后** 设置；moved-from 对象不得持有 `host`，
否则临时对象析构会 detach 最终对象。`make_table_screen` 里 attach 的位置已经
在 `asRenderObject(std::move(agg))` 之后，这是硬约束。

host 由 frameState 的 shared_ptr 持有；如果 host 先销毁，它的析构把残留
`arena->host` 置空，arena 后销毁时不再访问 host。若 arena 先销毁，detach 把
自己从 host 移除。**不要求 weak_ptr**，但要求这个顺序和 shared_ptr 生命周期
写死；多窗口时每个窗口的 shared_ptr 各自独立。


### 2.7 需要替换的 current_arena 调用

| 位置 | 替换 |
|---|---|
| main `input_ime(*current_arena())` | `host.focused ? input_ime(*host.focused) : ...` |
| `modal_open/cancel_modal` | 改用 `host.focused`；签名加 `table_host&` 或 `table_arena*` |
| `on_hover` | `screen->table_host_ctx->on_pick` |
| `version()` | 删除；renderVersion 保持帧级，picking 不按表比较版本 |
| `make_screen` | `host.attach` + `screen.table_host_ctx=&host` |
| main picking | `if (host.accept_pick(r)) host.on_pick(r)` |
| main Esc | `host.focused ? host.focused->esc_cancelled : false` |
| `on_pick` | 保留 `on_pick(table_arena&, r)`，host 调用 |
| `active_table` | 删除；`keyboard_focus` 替代 |

最终 `grep current_arena|active_table|inputTranslation().attach(agg` 生产路径必须为 0。

---

## 3. 迁移顺序

1. 整份备份当前 cpp/hpp → `build/_bak_a10_multi_before.*`（已存在）。
2. 前置声明 `table_host`；`table_arena` 加 `keyboard_focus/draw_serial/host`，
   只声明 `~table_arena`（不 inline）。
3. 实现 `table_host`（draw_order、detach、input_event/on_pick；不保存 screen），
   再定义 `~table_arena`、最后 `~table_host`。
4. `make_screen` 改为幂等 `window_tables()` + `make_table_screen(specs)`；
   删除 Table aggregate 的 input binding / input_event / postBind。
5. 替换 `on_hover/current_arena/version/modal/IME`；`drawFrame` 里
   `renderVersion` 改取 `host.frame_serial`，删除 `table_ui::version()`。
6. 单表回归：171 / 32 / 150；真实键盘/Esc。
7. 加 `TABLE_MULTITEST`，接真实 `on_hover`、CPU hover、draw order、detach。
8. 如要真实双表 demo：在 `make_table_screen` 里放两个 Table，先 headless 通过再上屏。

---

## 4. 多实例测试（`TABLE_MULTITEST`）

构造 A(0,0,400,300) / B(500,0,400,300)，id 1/2，各 10 行；两个 host 测试
**必须走真实入口**：

```cpp
table_host h;
ScreenWidget screen{};        // 只作为 host 绑定
screen.table_host_ctx = &h;
h.attach(a); h.attach(b);

// headless 必须把坐标/owner 显式初始化，否则 table_at 不命中
a.owner = nullptr; a.sx = a.sy = 1.0F;
b.owner = nullptr; b.sx = b.sy = 1.0F;
table_ui::draw_sync(a, {0, 0, 400, 300});
table_ui::draw_sync(b, {500, 0, 400, 300});
```

用例：

1. **初始焦点**：`h.focused == &a`；A/B hover 都空。
2. **hover 两条路径都测**：
   - CPU：`h.input_event(position2d_event{550,100})` 后对 A/B 调 `advance()`，
     断言 B 的 `view.ui.hover` 是 B 的 cell，A 仍为空；
   - GPU/验证：构造 B 的 `picking_result`（entity_index 带 B table 位），
     调 `table_ui::on_hover(r, true, inputCtx, &screen)`，断言只有 B 的
     `pick` 更新，A 不变；构造 A key 再调，只有 A 变。
   不能只断言 pick 或只断言 hover。
3. **点击 B**：`h.input_event(position/mouse press/release)`；
   focused=B；B 有选区，A 无。
4. **键盘只到 focused**：`h.input_event(key down/repeat)`；B 移动，A 不动。
5. **字符只到 focused**：B `edit` 打开，`h.input_event(char_event{'z'})`；
   B buf 有 z，A 不 active。
6. **点 A 切焦点**：再点 A，focused=A；之后键盘只动 A。
7. **draw order / 重叠 / hover z 序**：
   - 让 A/B 的 box 重叠（测试临时的 `draw_sync(a,{0,0,400,300})` 与
     `draw_sync(b,{0,0,400,300})`）；
   - attach 顺序 B、A；
   - `h.begin_frame(); h.mark_drawn(&b); h.mark_drawn(&a);`
   - `h.input_event(position2d_event{100,100})`；`a.advance(); b.advance();`
   - 断言 `table_at` 命中 A、A 的 `view.ui.hover` 非空、B 为空；
   - 交换 `mark_drawn(&a); mark_drawn(&b);` 再 position+advance，命中/ hover B；
   - position 到所有表外，advance 后 A/B 的 hover 都清空。
8. **帧版本消费点**：
   - `h.begin_frame()` 后 `h.frame_serial` 是本帧号；
   - 构造 `render_version == h.frame_serial` 的 B picking，`accept_pick` 为 true，
     按 table 更新 B；
   - 构造 `render_version != h.frame_serial` 的 B picking，`accept_pick` 为 false，
     B/A 都不更新；
   - A/B 的 `view.ui.version` 不同也不影响，因为 per-table 版本不参与过滤。
9. **detach**：`h.detach(b)` 后 `table_at` 不再返回 B，`on_hover` B key 不更新；
   **`h.focused` 必须提升到 A**（否则剩余表 keyboard_focus 全 false，键盘死）；
   A 仍能接键盘/字符。
   A 仍正常。
10. **Esc**：`h.focused` 为空时主循环判空，不崩。

断言用现有 check 风格；`TABLE_MULTITEST=1` 只跑此测试。

---

## 5. 不能碰

`target_key` 位布局、`resolve/to_owner/visit_owner/visit_drag`、
`draw_sync/draw_build/draw_encode/build_frame`、selection/edit/checks 数据结构、
`table_tree` 的 trait、既有 171/32 断言。只改接线层。

---

## 6. 验收

```powershell
cmake --build E:/0_github_project/mcsvulkan/build \
  --target mcsvulkan-vma-test_ui_ability11_table -j 8

$env:TABLE_SELFTEST='1'; & ...\test_ui_ability11_table.exe   # 171 / 0
$env:TABLE_EVTEST='1';   & ...\test_ui_ability11_table.exe   # 32 / 0
$env:TABLE_MULTITEST='1';& ...\test_ui_ability11_table.exe   # 全部 ok
$env:TABLE_PICKCHECK='1' 真实窗口移动鼠标                   # 150 ok / 0 mismatch
```

grep 必须为 0：

```text
current_arena        // 生产路径
active_table
inputTranslation().attach(agg
table_ui::version()  // picking 不再用全局版本
```

真实键盘：Right/F2/Enter/Ctrl+F/Esc + repeat 仍通过；Esc 取消 modal 后存活。

---

## 7. 关键风险（按 review 补充）

1. **host 位置**：必须是窗口对象成员（`frameState.tableHost`），不是
   `window_host()` 函数 static；`ScreenWidget` 只存指针。单窗口前提写死。
2. **make_screen 幂等**：`window_tables()` 只在 slot 为空时 make_shared；
   多张表走 `make_table_screen(specs)`，不支持运行时 add_table。
3. **析构顺序**：`~table_arena` 只声明，host 完整后再定义；host 析构先把
   残留 `arena->host` 置空。arena 的所有 move 必须在 attach 之前完成。
4. **renderVersion**：`begin_frame` 递增 `frame_serial`，`drawFrame` 在
   `screen.render` 前取 `host.frame_serial` 写入 `renderVersion`，删除
   `table_ui::version()`；一帧多次 render 同号。
5. **hover 两路**：on_hover 只更新 pick/last_pick；CPU position+advance 才是
   `view.ui.hover`；多表测试两者都要断言。
6. **on_hover 测试**：测试必须构造 `ScreenWidget{table_host_ctx=&h}` 并调
   `on_hover`，不能直接调 `h.on_pick`；否则路由未被验证。
7. **版本**：`renderVersion` 帧级；`view.ui.version` 表级 CPU 修订；两者不混。
   `on_pick` 不做 per-table 版本过滤。
8. **draw order**：`begin_frame` 在 input 之后、render 之前；输入用上一帧
   draw_order；重叠按 draw_order 反向，不按 attach。
`lx = wx*sx`；`geom.box` 是 screen-absolute 坐标，不再加 owner.offset。
不是 `(wx-offset)/sx`；headless 设置 owner=nullptr、sx=sy=1。
10. **detach**：arena 析构 detach；host 析构清残留；`ScreenWidget` 销毁后
    host 不得再访问 arena。
11. **每表 attach**：删除 aggregate `input_binding/input_event`；`asRenderObject`
    空 handler；host 只 attach 一次。
12. **Esc**：`host.focused` 判空；`esc_cancelled` 只读 focused 的。
13. **测试数字**：PICKCHECK 以 150 为基线，不要写 72。
14. **begin_frame 在 screen.tick 之前**：advance 读 host.hovered，不能放在 tick 之后。
15. **cursor 只能 host 写**：每表 render 不再 apply_cursor；`host.apply_cursor(win)` 只看 hovered，null 写 arrow。
16. **离开窗口清 hovered**：cursor_enter(false) / focus(false) 调 clear_hover，并置 pointer_inside=false，否则 begin_frame 用旧坐标复活旧表 hover。

---

## 8. 当前代码位置

```text
cpp:10867  postBindInputTranslation（删）
cpp:10822  app demo input_event（与本任务无关，保留）
cpp:11485  current_arena（生产路径删）
cpp:11571  picking/on_pick 声明
cpp:11679  active_table（删）
cpp:12126  on_pick(table_arena&, picking_result)
cpp:12148  on_hover（改 screen->host）
cpp:12156  hover_index
cpp:12240  version（删/改 per table）
cpp:12256  make_screen（host attach）
cpp:13968  main picking（host）
cpp:13984  main Esc（host.focused，判空）
cpp:13992  main IME（host.focused）
hpp:293    hover_fn_ref（保留，回调无状态）
```

工作树：

```text
M test/mcsvulkan/vma/test_ui_ability11_table.cpp
M test/mcsvulkan/vma/test_ui_ability11_table.hpp
M include/detail/wsi/glfw.hpp        // Esc press 分发 + close 时机，保留
?? HANDOFF_table_arena10.md
?? HANDOFF_table_multi_instance.md   // 本文件
?? _shots/
```

---

## 9. 完成定义

- 单窗口多表：A/B 的 hover、pick、点击、键盘、字符、焦点完全隔离；
- `on_hover` 经 `ScreenWidget::table_host_ctx` 真实路由，不是测试直调；
- picking 的 renderVersion 只在 `accept_pick` 比较一次；`on_pick` 不比版本；
- `table_at` 按 draw_order 反向；
- detach 后 host 不再访问已销毁 arena；
- 生产路径 0 个 `current_arena/active_table/global version`；
- 单表 171/32/150 不回归，真实键盘/Esc 通过。
