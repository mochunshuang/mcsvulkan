# ability12 修改计划（v4）：从「行为等价半拆版」到四维验收

> 状态：**待批准，尚未改代码**。
> v4 在 v3 基础上补了你指出的 **2 个编译级/语义级阻塞**（ViewFacade 与 36 处 `command::Context` 的兼容 →
> 选方案 A 隐式转换；"touch 每命令只多一次 blanket"而不是"总共一次"）+ 6 条小口径。
> 依据：你的两轮裁定 + 我的复核（§1，数字全部实测）。

---

## 0. 结论与裁定

**可以执行**的条件是先把三件事变成计划里的一等公民：

1. **oracle 恢复 = 41 label + 1 断言值 + 18 处 `v.ui.*` 表达式路径** → 用 **test-only facade** 让 18 处路径原样保留；
2. **pimpl 不能打断 selftest** → 用 **test-only accessor** 暴露 `router/pipe`，38 处 `sy.get<>` 与 36 处
   `command::Context` 原样不动；
3. **写入口口径写死** → `dispatch` 只写 State/Interaction，`session::apply_effects` 才写 Model，
   `touch()` 只发生一次；HANDOFF/CONTRACT 同步这一句。

v4 的分阶段与 v3/v2 一致（P0→P6），但 **P0/P1/P2 的产物和验收都变了**，见 §0.1 与 §4。

### 0.1 v4 相对 v3 的变更（逐条对应你第二轮的裁定）

| # | 你的裁定 | v4 落点 |
|---|---|---|
| N1 | ViewFacade 与 **36 处 `command::Context`** 不兼容 | §P0-1 方案 **A**：facade 提供 `operator intent::State&()` / `const State&()`；36+2 处原样编译；B 方案不采用 |
| N2 | "touch 只一次"表述错误，会误伤 65 处 | §P0-2 改为"**每命令多一次 blanket touch**"：feature/gesture 65 处原样 + dispatch 末尾 1 次；apply/Host 不 touch |
| N3 | D1 补充：host.hpp 不能依赖 P4 才有的类型 | §P1-2：`shader_abi.hpp` **P1 就建**，host.hpp 用 `platform::PickingResult`；P4 只做 resolve |
| N4 | D4 补充：PickingResult 搬迁后的引用清理 | §P4-2 增加 grep 验收（`picking_result` = 0） |
| N5 | check_oracle.py 进仓库、不挂 CTest 硬依赖 | §5：路径 `table12/test/check_oracle.py` + 可选 `.ps1` 包装 |
| N6 | inplace_vector 容量/溢出/可移植性 | §P3-1：容量 8 + 溢出 abort（不静默丢）+ `SmallVector` 退化 |
| N7 | `target_sources` 的 `TARGET_NAME` 兜底 | §P2-1：先 `${TARGET_NAME}`，失败立刻换显式目标名，不拖到后半 |
| N8 | window_draw/input 的 include 约束 | §P1-2：只被 demo cpp 在 host.hpp 之后 include，不得反向 |
| N9 | `SessionAccess` friend 前向声明顺序 | §P2-2：`session.hpp` 顶部先前向声明 `table::test::SessionAccess` |

### 0.2 v3 相对 v2 的变更（保留备查）

| # | 第一轮裁定 | v3 落点 |
|---|---|---|
| B1 | P0 补 test facade + oracle 三项口径 | §P0-1、§P0-3、附录 B |
| B2 | P2 补 test-only accessor，38 处 `sy.get` 不能动 | §P2-2、附录 D |
| B3 | P0 明确 dispatch/apply 分工 + touch 口径 | §P0-2（v4 已修正表述） |
| C1 | P1 一次改名到最终名，P2 只做 pimpl | §P1-1 改名表 |
| C2 | 处理「字段 model 与方法 model() 同名」 | §P2-3：删访问器，字段用最终名 |
| C3 | `target_sources` 先做最小 session.cpp 冒烟 | §P2-1 两步走 |
| C4 | P4 解决 `PickingResult` 可见性 | §P1-2（提前到 P1）+ §P4-1 |
| C5 | P1 给出 header include DAG | §P1-2 DAG |
| C6 | P3 小缓冲要真零分配 | §P3-1：`std::inplace_vector<Effect,8>` |
| D1–D4 | 条件式批准 | §7 裁定落地映射 |

---

## 1. 复核（实测数字）

| 项 | 实测 | 位置 |
|---|---|---|
| demo cpp 行数 | **14,785** | `test_ui_ability12_table.cpp` |
| 其中 ability12 自己的 | **2,395**（`table_glue` 227 + `table_ui` 2,168） | cpp:10930–13324 |
| 其中 demo 框架（非 ability12） | **12,390** | 附录 A |
| `table12_app/host.hpp` | 25 行，只有 `using` 别名 | — |
| `Binding` / `Host` 实现 | demo cpp 的 `table_ui` | cpp:11320 / cpp:11529 |
| `session.hpp` | 304 行，include 3 个 detail + 5 个访问器 | session.hpp:27–29 |
| `detail/features.hpp` | 2,075 行 | — |
| `Outcome` 旁路 | `Context::fx`；dispatch 每次 2 个 vector | command.hpp / session.hpp |
| `using Effect` 重复 | 2 处 | command.hpp:103、:110 |
| 五门实际名 | `Host::input_event`（37 调用）/ `model::resolve` / 多一个 `session::hit` | demo cpp |
| **selftest 与 ability11 的差异** | **41 label + 1 断言值 + 18 处 `v.ui.*`**（`last_click_t` 15、`now` 2、`hover` 1） | selftest.hpp |
| selftest 摸内部 | `detail::*` 35 处（19 个名字）、`sy.get<>` 38 处、`command::Context` 36 处、`compose_draw` 11、`compose_layout` 2、`hit_target` 5 | selftest.hpp |
| `touch()` 分布 | feature 58 + gesture 7 = **65**（行为 touch）；另有 layout 3 / common 2 / intent 1 / session 12 = 共 83 | table12/** |
| selftest 的 version 断言 | **0 处**（所以 blanket touch 多一次不会改断言，P0 复跑确认） | selftest.hpp |
| app 是否碰 `sys/pipe/fx` | **0 处** | demo cpp |
| pass 表 | `std::array<void(*)(...),9>` / `,10>` | layout/paint_passes.hpp |
| `std::inplace_vector` 可用性 | **GCC 16 `-std=c++26` 语法探针通过** | 已实测 |

两条推论：
- `sys/pipe` 只有 selftest 用（38+2 处），app 不用 → **pimpl 可行，但必须配 test accessor**（B2）。
- `v.ui.*` 是 18 处**表达式**（有读有写：`last_click_t = -1e9`），不是注释 → **必须引用代理，不能只改名**（B1）。

---

## 2. 目标（可测）

| 维度 | 指标 | 怎么测 |
|---|---|---|
| 行为 | `171/32/71/150`；**140 个 check：表达式、label、计数三者与 ability11 一致**（驱动差异仅限白名单 W1–W8） | 附录 B 脚本，差异 0 |
| 结构 | 五门 = `Host::route_event` / `session::dispatch` / `session::compose` / `platform::encode` / `platform::resolve` | grep 唯一实现 |
| 结构 | `session.hpp` 不含 `detail/`；`Context` 无 `fx`；feature 返回 `Outcome` | grep |
| 结构 | `dispatch` 写 State、`apply_effects` 写 Model；每命令只多 1 次 blanket touch（feature/gesture 65 处不变） | 代码审查 + 计数器 |
| 可读 | ability12 拥有的文件 < 1500；公共 schema 头 < 300；demo cpp 里 ability12 段落 < 400 | 行数表 |
| 性能 | 常见路径 dispatch 0 堆分配；A/B compose 与 ability11 差 < 5% | 附录 C 基准 |

---

## 3. 执行顺序

```text
P0  恢复 oracle（41 label + 1 值 + 18 表达式）+ facade + 写入口口径   小、地基
P1  app 层搬出 demo cpp + 一次性改名到最终字段名 + DAG               大、纯搬移
P2  Session pimpl + session.cpp + test accessor（先冒烟 target_sources）  中、接口收敛
P3  Outcome 唯一结果（inplace_vector 小缓冲、去 Context::fx）         中、feature 签名
P4  五门命名 + platform::resolve + 文档（shader_abi 已在 P1）       中
P5  尺寸收尾（features/contract/intent/frame 拆分）+ HANDOFF 测量口径  中、纯切分
P6  零开销（tuple 折叠）+ 基准（含 0 分配计数）                        小、可测
```

每阶段结束跑统一门（§5），红了就地回滚。

---

## 4. 分阶段步骤

### P0：恢复 oracle（含 facade 与写入口口径）

#### P0-1　test-only facade（解决 18 处 `v.ui.*`）

新增 `table12/test/view_facade.hpp`（**只在 test/ 下，不进公共头、不进 umbrella**）：

```cpp
#pragma once
// test-only：ability11 的断言体用 v.sel / v.ui.hover / v.ui.last_click_t ...，
// 而 ability12 把 ui_state 拆成了 Interaction。这里用“引用代理”让同一批表达式
// 原样编译、原样语义（写 last_click_t 就是写 Interaction.last_click_t）。
// 约束：Session 不得在 facade 生命周期内移动（selftest 里 Session 在栈上不动）。
namespace table::test
{
struct UiFacade
{
    intent::Interaction &ix;
    Target &hover = ix.hover;
    int &band_row = ix.band_row;
    int &band_col = ix.band_col;
    glm::vec2 &pointer = ix.pointer;
    bool &left = ix.left;
    double &now = ix.now;
    intent::DragMode &drag = ix.drag;
    Target &drag_target = ix.drag_target;
    double &last_click_t = ix.last_click_t;
    glm::vec2 &last_click_pos = ix.last_click_pos;
    int &click_count = ix.click_count;
    bool &header_moved = ix.header_moved;
};

struct ViewFacade
{
    intent::State &state;
    intent::Interaction &interaction;
    intent::Selection &sel = state.sel;
    intent::Checks &checks = state.checks;
    intent::Edit &edit = state.edit;
    intent::Filter &filter = state.filter;
    intent::Columns &cols = state.cols;
    intent::Scroll &scroll = state.scroll;
    intent::Sort &sort = state.sort;
    intent::History &history = state.history;
    intent::Find &find = state.find;
    intent::Menu &menu = state.menu;
    intent::Tooltip &tip = state.tip;
    UiFacade ui{interaction};

    // ability11 的 view::reset 会同时清 UI（hover/拖拽），这里等价委托
    void reset(const model::Table &m)
    {
        state.reset(m);
        interaction.reset();
    }

    // ── 为什么需要隐式转换（选 A，不选 B）──────────────────────────────
    //   selftest 里有 36 处  command::Context tc{d, v, ix, p, g, ft, &fx, false, S.id};
    //   其中第二参要求 intent::State&；v 若是 facade，没有转换就编译失败。
    //   36 处是主体不是边角，所以用 A（facade 转换），36 行 + 2 处 wheel 全部原样。
    //   约束：只转引用，不转值（避免把 facade 复制成 State 的隐患）。
    operator intent::State &() noexcept { return state; }
    operator const intent::State &() const noexcept { return state; }
    // 注：若某个调用点同时接受 State& / const State& 两个重载而出现歧义，
    //     只保留非 const 版本（const 参数可由非 const 引用经限定转换绑定），并记录该调用点。
};
} // namespace table::test
```

**方案选择：A（facade 隐式转换），不选 B（改 36 处为 `v.state`）**

| | A：转换运算符 | B：改 36 处调用点 |
|---|---|---|
| 36 处 `command::Context` | 原样 | 全部改写 |
| 2 处 `intent::wheel` | 原样 | 全部改写 |
| 表达式比对 | 与 ability11 同形 | 需要 W7 覆盖 36 行 |
| 风险 | 只转引用；签名仍是 `State&`，核心不受影响 | 白名单被撑大，oracle 说服力下降 |

要求：转换只出现在 `table12/test/`；**核心任何签名都不接受 ViewFacade**；
若将来出现按值取 `State` 的重载，禁止让 facade 隐式转换（会切片），改调用点。

selftest 开头只改**一行**：`test::ViewFacade v{S.view, S.ix};`（P1 改名后是 `{S.state, S.interaction}`），
其余 `v.sel` / `v.ui.last_click_t` / `v.filter` / `v.reset(d)` / 36 处 `command::Context` / 2 处
`intent::wheel` … 全部原样（靠上面的转换运算符）。

> 现实清单（实测，不是"若有"）：
> - `v.reset(d)` 2 处 → facade 的 `reset`；
> - `intent::wheel(v, ix, p, ...)` 2 处 → 转换运算符即可，不需要 `v.state`；
> - `command::Context tc{d, v, ix, ...}` **36 处** → 转换运算符即可；
> - 把 `v` 传给 `State&` 的其他调用：0 处（已 grep `\(v[,)]` 全量核对）。

#### P0-2　写入口口径（A 方案的完整定义）

```text
dispatch(Session&, Command)
    → 只写 State / Interaction（feature + gesture）
    → 产出 Outcome{handled, state_changed, effects}；不写 Model
apply_effects(Session&, const Outcome&) -> bool
    → 只写 Model（SetCell → Table::set_cell）
    → 返回 model_changed；effects 原样保留（Host 继续消费 WriteClipboard）
Host 顺序：dispatch → apply_effects → 剪贴板 → 回调 → 重绘
```

- `Outcome.model_changed` 在 dispatch 阶段**恒 false**，由 `apply_effects` 返回值补上；
- **touch 口径（等价 ability11，不是"总共一次"）**：
  - ability11 = feature/gesture 自身的 touch（**65 处**：features 58 + gestures 7，实测）
    **+** `apply_effects` 末尾 1 次 blanket `ui.touch()`（cpp:11910）；
  - ability12 = 那 **65 处原样保留** **+** `dispatch` 末尾补 1 次 blanket touch；
    `apply_effects` 只写 Model、**不** touch；Host **不** touch；
  - 即"每命令只多一次 blanket touch"，**不是**"总共只剩一次"——按字面砍掉 65 处会改变版本推进与重绘，直接偏离 ability11；
  - 核心 `touch()` 调用共 83 处（65 + layout_passes 3 + common 2 + intent 1 + session 12），除 blanket 外全部保留；
  - **selftest 的已知差异**：`send()` 只走 dispatch（不经 Host `apply_effects`），所以 ability12 的
    selftest 每次 send 比 ability11 多 1 次 blanket touch。已实测 selftest 里 `version`/`revision`
    引用 **0 处**，不构成断言影响；P0 仍以 171 全绿为验收。
- `HANDOFF` 与 `CONTRACT` 同步改一句话：
  "dispatch 写 State/Interaction，apply_effects 写 Model；二者是唯一写路径，Host 只按序调用。"
  （§5.7 的"dispatch 是唯一写入口"改成"dispatch + apply_effects 是唯一写路径"，措辞由你定，v4 用前者）

#### P0-3　oracle 恢复与比对口径（三样都比 + 驱动白名单）

**恢复项**

| 恢复项 | 数量 | 做法 |
|---|---|---|
| label 字符串 | 41 | 从 ability11 原文拷回，**一字不改**（禁止类型名大写化） |
| 断言值 | 1 | `alpha.datX` → `alpha.dat` |
| 测试体里的 `v.ui.*` | 18（`last_click_t` 15、`now` 2、`hover` 1） | 由 facade 承载，文本保持 `v.ui.last_click_t` 等原样 |
| 其余断言体 | — | 只允许类型名替换（`ui_table::X` → `table::Y`） |

**比较对象（`check_oracle.py`，三项都必须 0 差异）**
1. 140 个 `check(...)` 的**首个参数表达式**（归一化：仅类型名映射）；
2. 140 个 **label 串**（0 容忍）；
3. 调用点计数 140 + 运行时 `ok:` 计数 171。

**驱动代码白名单**（这些是"换门"必然产生的适配，允许不同；超出白名单即视为改 oracle）：

| # | ability11 驱动 | ability12 驱动 | 原因 |
|---|---|---|---|
| W1 | `sync(d, v, p, g, vp, ft, sy, pipe)` | `sync(vp)` → `session::compose` | 门 |
| W2 | `build_frame(d, v, p, g, ft, sy, fr)` | `detail::compose_draw(...)` | 门 |
| W3 | `system_event(d, v, p, g, sy, ft, cmd, fx)` | `send(cmd)` → `session::dispatch` | 门 + Outcome |
| W4 | `system_text(tc, sy, s)` | `sy.text(tc, s)` | 路由 |
| W5 | `system_wheel(v, p, dx, dy, shift)` | `intent::wheel(v, ix, p, dx, dy, shift)` | 拆 view |
| W6 | `hit_test(d, g, v, x, y)` | `detail::hit_target(d, g, v, x, y)` / `session::hit` | 门 |
| W7 | `table_ctx tc{...}` | `command::Context tc{...}`（多 `ix`） | 拆 view |
| W8 | `v.reset(d)` / 局部变量声明 | facade 的 `reset` + 绑定行 | 拆 view |

> `v.ui.*` **不在白名单**：那 18 处靠 facade 原样保留，脚本按"表达式文本"直接比对。

**涉及**：`table12/test/view_facade.hpp`（新）、`table12/test/selftest.hpp`、`table12/session.hpp`（dispatch/apply）、
demo cpp 的 Host `apply_effects`、`HANDOFF_ability12.md`（一句话）、`table12/CONTRACT.md`。
**验收**：
- `171/32/71/150`；
- `check_oracle.py` 三项 0 差异（表达式 / label / 计数）；
- `grep -c "v\.ui\." selftest` = 18；`grep -c "alpha.datX"` = 0；
- selftest 里 36 处 `command::Context` 与 2 处 `intent::wheel` **编译原样通过**（facade 转换）；
- selftest 里 `version`/`revision` 引用 = 0（blanket touch 不影响断言，已实测，P0 复跑确认）。
**回滚**：三个文件级。

---

### P1：app 层搬出 demo cpp（一次性改名 + DAG）

**目标**：demo cpp 里 ability12 的代码 2,395 行 → **< 400 行**；`table12_app/*.hpp` 是真实现（不是别名）。

#### P1-1　改名表（一次到位，P2 不再改名）

| 现在 | 最终 | 说明 |
|---|---|---|
| `Session::data` | `Session::model` | 注意：成员名遮蔽 `table::model` 命名空间 |
| `Session::view` | `Session::state` | |
| `Session::ix` | `Session::interaction` | |
| `Session::prefs` | `Session::theme` | |
| `Session::geom` | `Session::layout` | |
| `Session::frame` | `Session::draw` | |
| `Session::sys` | `Session::router` | P2 会被藏进 pimpl；此处先起好名 |
| `Session::pipe` | 不变 | |

同时**删除** 5 个访问器 `model()/state()/interaction()/layout()/draw_list()`（否则与字段同名冲突，见 C2）。
**遮蔽坑写进 session.hpp 头注释**：声明 `model::Table model;` 之后，类内不得再写 `model::X`
（成员名遮蔽命名空间），需要时写全限定 `table::model::X` 或放到类外的门实现里。

改名影响面：`session.hpp`、`table12_app/*`（`s.session.model` 等）、`selftest.hpp`（facade 一行 + 3 处绑定）、
demo cpp（`s.session.*`）。统一在 P1 的搬移 diff 里完成。

#### P1-2　文件与 include DAG（C5 + D1 的 PickingResult 时序）

**PickingResult 在 P1 就位**（不等 P4）：`table12_app/shader_abi.hpp` 在 P1 创建，`host.hpp` 直接依赖它，
不存在"host.hpp 依赖一个 P4 才有的类型"。

| 文件 | 内容 | 依赖 |
|---|---|---|
| `table12_app/shader_abi.hpp` | `ObjectKey` / `RenderVersion` / `PickingResult`（从 demo cpp 375–690 段搬入，含 static_assert） | 无（std + Vulkan 基础类型） |
| `table12_app/glue.hpp` | `init_font` / `init_model`(JSON) / 剪贴板 / IME / cursor | 无 Host 依赖 |
| `table12_app/platform.hpp` | 仅 `resolve`（P4 加）；P1 可先留空壳或直接建好 | `shader_abi` + model/frame |
| `table12_app/encode.hpp` | `encode` + `clip_glyph` | `ui_dao`（app 类型） |
| `table12_app/host.hpp` | `Binding`（组合 `Session`）+ `Host` 定义；用 `platform::PickingResult` | `session` + `shader_abi` + `ui_new`/GLFW |
| `table12_app/window_draw.hpp` | `draw_sync`/`draw_build`/`draw_encode`/`draw` + `pointer_target` + `on_pick` | `host.hpp` |
| `table12_app/window_input.hpp` | `arena_input::Trait_*` + `Binding::on_*` + `click_dispatch` + `map_key` + `input_dispatch` + `apply_effects` + `notify_change` + 模型 API + `cancel_modal` | `host.hpp` |

**include 规则（写进每个头文件顶部）**

```text
framework 定义（ui_new / intent / gesture / ui_dao / input_frame）
  → shader_abi.hpp / glue.hpp / platform.hpp / encode.hpp
  → host.hpp
  → window_draw.hpp
  → window_input.hpp
  → demo 场景（make_table / make_table_screen / make_screen / main）
```

- `window_draw.hpp` / `window_input.hpp` **只能被 demo cpp 在 `host.hpp` 之后 include**；
  它们不得 include demo 的 main/场景代码，也不得被 host.hpp 反向 include（否则循环）；
- `host.hpp` 只声明/定义 `Binding` + `Host`，不 include 上面两个 window_* 头；
- 真实 `host.cpp` 仍不在本次 DoD（等 demo 框架抽头）。

#### P1-3　Binding 改组合（D1 的条件）

```cpp
struct Binding                    // 窗口绑定，不是 Session
{
    table::session::Session session;   // 真值/意图/派生缓存
    ui_new::ScreenWidget *screen{}; ui_new::Widget *owner{}; GLFWwindow *win{};
    float mx{}, my{}, wx{}, wy{}, sx{1}, sy{1}, vw{1}, vh{1};
    // ... pointer / pick / arbiter / input_state / callbacks / stats（沿用）
};
```
- 所有 `s.data` → `s.session.model` 等；`Binding` 不再继承 Session；
- Host 只保留窗口级职责（`route_event`、focus、`draw_order`、`frame_serial`、`hover_fn`、
  `allocate_table_id`、`accept_pick`/`on_pick`、`apply_cursor`）；
- **interim 标注**：`host.hpp` 顶部写明"P1 interim：先 header-only；`host.cpp` 要等 demo 框架抽头，
  不列入本次 DoD"（D1 条件满足）。

**验收**：`171/32/71/150`；cpp ≤ 13,000 且 ability12 段落 < 400；7 个新头各 < 1500；DAG 与 include 顺序一致（人工 review）。
**回滚**：整体一个 commit。

---

### P2：Session pimpl + test accessor

#### P2-1　两步走（C3：先冒烟构建系统）

1. **冒烟**：新建 `table12/detail/runtime.hpp`（`struct Runtime { AppRouter router; RowPipeline pipe; };`）
   与最小 `table12/session.cpp`（只定义 `Session` 的 ctor/dtor/移动 + `Runtime` 实例化），
   在 `vma.cmake` 的 ability12 行之后加：
   ```cmake
   target_sources(${TARGET_NAME} PRIVATE
       "${CMAKE_SOURCE_DIR}/test/mcsvulkan/vma/table12/session.cpp")
   ```
   **先构建一次**。若 `TARGET_NAME` 在宏外已失效，立刻改用显式目标名
   `mcsvulkan-vma-test_ui_ability12_table`（不要在 P2 后半才发现）；两种写法都失败就先停，
   退回"`detail/gates.hpp` 只被 app include"，并在 CONTRACT 记偏差。
2. **搬门**：把 dispatch/compose/hit/窄门的实现从 `session.hpp` 移到 `session.cpp`。

#### P2-2　test-only accessor（B2：38 处 `sy.get` 不能动）

`table12/test/access.hpp`：
```cpp
#pragma once
// test-only：oracle 需要直呼 feature/pass（ability11 的 table_ctx 直调），
// 这里把 detail 拉进来并提供 Session 内部访问器。公共头永远不 include 它。
#include "../detail/gestures.hpp"
#include "../detail/layout_passes.hpp"
#include "../detail/paint_passes.hpp"

namespace table::test
{
struct SessionAccess { detail::AppRouter &router; frame::RowPipeline &pipe; };
inline SessionAccess access(session::Session &s) { return {s.rt->router, s.rt->pipe}; }
} // namespace table::test
```
- `session.hpp` 顶部先 `namespace table::test { struct SessionAccess; }` 前向声明，类内一行
  `friend struct test::SessionAccess;`（顺序不能反，否则 friend 声明找不到名字）；
- selftest 只改两行绑定：`auto &sy = test::access(S).router; auto &pipe = test::access(S).pipe;`
- **38 处 `sy.get<>`、11 处 `compose_draw`、2 处 `compose_layout`、5 处 `hit_target`、36 处 `command::Context` 全不动**——
  `access.hpp` 已经 include 了 detail，这些限定名照样可见。

#### P2-3　命名与访问器（C2）
- 字段用最终名（P1 已改），**删除**同名访问器；
- `Session` 禁用复制、保留移动；`rt` 在 `reset()` 里不清空（或重建，二选一并写明）。

**验收**：`grep -n "detail/" table12/session.hpp` = 0；`session.hpp` ≤ 220；`171/32/71/150`（含 session.cpp 链接）。
**回滚**：P2 独立 commit；若 `target_sources` 有坑，退回"`detail/gates.hpp` 只被 app include"的中间态并在 CONTRACT 记偏差。

---

### P3：`Outcome` 唯一结果（真零分配）

#### P3-1　EffectList（C6）
```cpp
// command.hpp
using EffectList = std::inplace_vector<Effect, 8>;   // C++26；GCC 16 已实测可用
struct Outcome
{
    bool handled{false};
    bool model_changed{false};
    bool state_changed{false};
    EffectList effects{};
    void merge(const Outcome &o);   // handled |= …
};
```
- `inplace_vector` 只在用到时构造元素（`array<Effect,4>` 会构造 4 个 string，不算零分配但也不算零构造）；
- **容量与溢出（不允许静默丢 effect）**：容量取 **8**；`push` 超过容量时 `assert(size < 8)`，
  在 debug 直接炸；release 下留一条 `std::abort()`（宁可崩也不丢副作用）；
- `Outcome::merge` 语义：`effects` 依次 append（同样受容量断言约束），`handled/model_changed/state_changed` 取或；
- 可移植性：GCC 16 已验证 `std::inplace_vector`；若 CI 用 Clang/libc++ 没有，退化为 40 行
  `SmallVector<Effect,8>`（对齐存储 + placement new + 同样的溢出断言），接口保持一致（`this->push_back/size/begin/end`）；
- selftest 里统计"单条命令产生的最大 effect 数"，确认 ≤ 8（当前最多是 undo/redo 的 1 条 + clipboard 0–1 条）。

#### P3-2　feature 返回 Outcome
- `Context` 删除 `fx`；`Router`/`AppRouter` 用真折叠（短路 + merge）；
- 15 个 feature：`bool handle` → `Outcome handle`；中间 helper 也返回 `Outcome`；
- 删除重复 `using Effect`（保留一处）；
- selftest 的 `send()`：从 `Outcome.effects` 读（断言体不动）。

**验收**：`171/32/71/150`；`grep -c "using Effect"` = 1；`grep -rn "c\.fx\|Context::fx"` = 0；
P6 的 0 分配计数器 = 0。

---

### P4：五门命名 + shader ABI + `platform::resolve`

#### P4-1　`platform::resolve`（shader ABI 已在 P1 就位）
- `table12_app/shader_abi.hpp` 在 **P1** 已建（`ObjectKey` / `RenderVersion` / `PickingResult` + static_assert）；
- `table12_app/platform.hpp` = `resolve`（include shader_abi + model/frame），**自包含**；
- `encode` 在 P1 已挪到 `table12_app/encode.hpp`（依赖 `ui_dao`，文件头写明必须在 ui_dao 之后 include）。

#### P4-2　命名与差异记录
- `Host::input_event` → `Host::route_event`（37 处调用）；
- `platform::resolve(const model::Table&, const PickingResult&)`（D4 批准），内部只调 `model::resolve`；
- **PickingResult 搬迁后的引用清理（D4 补充）**：demo cpp 里主循环、PICKCHECK 段、Host 里所有
  `picking_result` / `object_key` / `render_version` 引用统一改成 `table::platform::*`；
- `CONTRACT.md` 记一行：
  ```text
  platform::resolve(const model::Table&, const PickingResult&)
  // 与 HANDOFF §6.4 草案的差异：Target 需要 row_id/column_id，必须查 model
  ```
- `CONTRACT.md` / `MAINLINE.md` 五门列表统一成 route_event / dispatch / compose / encode / resolve，
  另注 `session::hit` 是 CPU 命中门（不是 ABI 门）。

**验收**：`171/32/71/150`；`grep -rn "input_event" table12_app/` = 0；`grep -rn "model::resolve" table12_app/` 仅 platform.hpp；
`grep -rn "picking_result" table12_app table12 --glob '!shader_abi.hpp'` = 0（D4 补充条件）。

---

### P5：尺寸收尾 + HANDOFF 测量口径

#### P5-1　拆分（纯切分）
| 文件 | 现状 | 目标 | 做法 |
|---|---|---|---|
| `contract.hpp` | 717 | ≤ 300 | 拆 `text.hpp`（Font/GlyphMetrics/Arena/utf8/fit_text） |
| `intent.hpp` | 525 | ≤ 300 | 拆 `intent_types.hpp`（Theme + 11 组件） |
| `frame.hpp` | 360 | ≤ 300 | 拆 `draw_list.hpp`（Stream/ShapeOp/TextOp/DrawList） |
| `detail/features.hpp` | 2,075 | 5–6 个文件各 ≤ 800 | filter_find / layout_ops / selection / edit / overlay / router |
| `session.hpp` | 304 | ≤ 220 | P2 已达成 |
| `test/selftest.hpp` | 1,204 | ≤ 1,250 | 达标 |
| `test/view_facade.hpp` / `test/access.hpp` | 新增 | ≤ 150 / ≤ 60 | — |

#### P5-2　HANDOFF 测量口径附录（D2 条件，替代"只写 CONTRACT"）
在 `HANDOFF_ability12.md` 追加 **§16 测量口径（验收附件）**：

```text
1. "单文件 < 1500 行" 只统计 ability12 拥有的代码：
   table12/**、table12_app/**、demo cpp 中 ability12-owned 段落（接线 < 400 行）。
2. demo 框架（ui_new / intent / gesture / renderer / demo main / shader ABI 之外）
   不计入；依据见 PLAN_ability12_fix.md 附录 A 的行数解剖。
3. 如需拆分 demo 框架，作为独立项目立项，不混入 ability12 验收。
4. CONTRACT.md 与本文互为引用：CONTRACT 记录契约，本文附录记录口径。
```
`table12/CONTRACT.md` 加一行指向 §16。

**验收**：行数表全绿 + `171/32/71/150`。

---

### P6：零开销 + 基准

1. **pass 表改 tuple 折叠**（核心热路径）：
   ```cpp
   using LayoutPasses = std::tuple<ReconcilePass, FramePass, ColumnsPass, MetricsPass,
                                   RowsPass, FrozenPass, VisiblePass, BarsPass, EdgesPass>;
   template <class... P> void run(LayoutContext &c) { (P{}(c), ...); }
   ```
   paint 同理；函数指针表只留给诊断。
2. **0 分配计数**：给 `Outcome`/`EffectList` 加编译期开关计数（或重载 `operator new` 计数），
   在 selftest 末尾断言"100 次 press/drag/key 的 dispatch 分配次数 = 0"。
3. **基准**（scratch TU，不进仓库）：同一 TU include ability11 头 + table12，跑 A/B 两表：
   compose ×10k、dispatch ×100k、DrawList op/zone 逐条相等；结果填附录 C。
4. 劣化 > 5% 回到对应阶段定位。

---

## 5. 每阶段的统一验收门

```powershell
# 1) 构建
ninja -C build mcsvulkan-vma-test_ui_ability12_table

# 2) 四基线
$exe = "output\test_program\mcsvulkan\vma\test_ui_ability12_table.exe"
$env:TABLE_SELFTEST="1";  & $exe                       # 171 ok / 0 failed
$env:TABLE_SELFTEST=$null
$env:TABLE_EVTEST="1";    & $exe                       #  32 ok / 0 failed
$env:TABLE_EVTEST=$null
$env:TABLE_MULTITEST="1"; & $exe                       #  71 ok / 0 failed
$env:TABLE_MULTITEST=$null
$env:TABLE_PICKCHECK="1"; $env:DSH_AUTOEXIT="1"; & $exe  # 150 ok / 0 mismatch
$env:TABLE_PICKCHECK=$null; $env:DSH_AUTOEXIT=$null

# 3) oracle 三项（P0 起每阶段）
python test/mcsvulkan/vma/table12/test/check_oracle.py `
    --ref test/mcsvulkan/vma/test_ui_ability11_table.hpp `
    --cur test/mcsvulkan/vma/table12/test/selftest.hpp --run $exe
# 表达式 / label / 调用点数 三者 0 差异 + ok: 计数 171；退出码非 0 即失败

# 4) 结构（按阶段启用）
rg -n "detail/" test/mcsvulkan/vma/table12/session.hpp        # P2 后 = 0
rg -n "c\.fx|Context::fx" test/mcsvulkan/vma/table12          # P3 后 = 0
rg -n "input_event" test/mcsvulkan/vma/table12_app            # P4 后 = 0
rg -n "picking_result" test/mcsvulkan/vma/table12_app test/mcsvulkan/vma/table12 `
   --glob '!shader_abi.hpp'                                   # P4 后 = 0
rg -n "void\s*\*|getenv|hover_fn_ref|next_table_id|make_aggregate|invoke<|std::function" \
   test/mcsvulkan/vma/table12 --glob '!test/**'               # 全程 = 0
```

`check_oracle.py`（**进仓库**：`test/mcsvulkan/vma/table12/test/check_oracle.py`）：

```text
抽取：两边的全部 check(<expr>, "<label>");（140 个）
归一化：仅类型名映射（ui_table::X → table::Y，cmd_press→Press …），**成员路径不动**
比较：① 首个参数表达式串  ② label 串  ③ 调用点数（140）
运行：可选 --run <exe> 时用 TABLE_SELFTEST_VERBOSE=1 跑一次，ok: 计数必须 = 171
输出：表达式差异表 + label 差异表 + 计数；任何一项非空 → 退出码 1
```

- **不挂成 CTest 硬依赖**（CI 不一定有 Python）；提供一个可选的
  `check_oracle.ps1` 包装（Windows 本地用）与 CMake 可选目标 `add_test(NAME ...)`，默认不 enable；
- 脚本只依赖标准库（`re`/`io`/`argparse`），无第三方包。

---

## 6. 风险与回滚

| 风险 | 触发 | 处置 |
|---|---|---|
| facade 引用悬空 | Session 被移动/拷贝 | selftest 里 Session 栈上不动；facade 头注释写死；Session 复制已 delete |
| facade 转换被误用（按值切 State） | 编译/隐性拷贝 | 只提供 `State&`/`const State&` 转换；核心签名不接受 facade；出现按值重载就改调用点 |
| 成员 `model` 遮蔽 `table::model` | 编译错 | Session 内禁止 `model::X`；需要时全限定或移出门实现 |
| P1 搬移漏改 `s.data` | 编译错 / 行为差异 | 纯搬移 + 171 全绿再提交；`git diff` 只应看到移动与统一改名 |
| `target_sources` 与 `-fmodules-ts` 冲突 / `TARGET_NAME` 失效 | 构建错 | P2 第一步最小 session.cpp 就验证；先试 `${TARGET_NAME}`，再试显式目标名；都失败退回中间态并记偏差 |
| P3 `inplace_vector` 容量不够 | 运行期断言 | 容量 8 + 溢出 abort（不静默丢）；selftest 统计最大 effect 数 |
| `std::inplace_vector` 在别的工具链缺失 | 编译错 | 40 行 `SmallVector<Effect,8>` 退化实现，接口一致 |
| 性能劣化 > 5% | 基准 | 回退 P6 对应项（tuple/pass 表） |

---

## 7. 裁定落地映射（你的条件 → 检查点）

| 裁定 | 条件 | 落在哪 | 怎么验 |
|---|---|---|---|
| D1 | 真 Host/Binding（组合）、DAG、interim 标注、**PickingResult 在 P1 就位** | P1-2/P1-3 | `host.hpp` 含 `struct Binding`/`struct Host` 且 > 400 行；`shader_abi.hpp` 在 P1 存在；DAG 与 include 顺序一致 |
| D2 | 口径写入 HANDOFF 验收附件 | P5-2 | HANDOFF §16 在正文；CONTRACT 引用 |
| D3 | 扩展 18 处表达式 + 41 label + 1 值 + **36 Context / 2 wheel 兼容** | P0-1/P0-3 | `check_oracle.py` 三项 0 差异；36+2 处原样编译 |
| D4 | resolve 带 model + 差异记录 + 类型可见 + **引用清理** | P4-1/P4-2 | `shader_abi.hpp` 存在；CONTRACT 有差异行；`grep picking_result` = 0 |
| B1 | test facade（方案 A 隐式转换） | P0-1 | `test/view_facade.hpp`；`grep -c "v\.ui\."` = 18；36 处 Context 未改 |
| B2 | test accessor | P2-2 | `test/access.hpp`；38 处 `sy.get` 未改 |
| B3 | 写入口 + **每命令多一次 blanket touch（不是总共一次）** | P0-2 | dispatch 无 Model 写；feature/gesture 65 处 touch 未动；selftest 的 version 引用 = 0 |
| C1 | 一次改名 | P1-1 | 改名表；P2 无重命名 |
| C2 | 字段/访问器冲突 | P1-1 + P2-3 | 访问器已删；`session.hpp` 无 `model()` 方法 |
| C3 | session.cpp 冒烟 + 显式目标名兜底 | P2-1 | 第一步就能构建 |
| C4 | PickingResult 可见（P1）/ resolve（P4） | P1-2 + P4-1 | platform.hpp 自包含；host.hpp 不含 P4 才有的类型 |
| C5 | DAG + window_* 只被 demo cpp 在 host.hpp 之后 include | P1-2 | 头文件顶部依赖说明 |
| C6 | 真零分配 + 容量 8 + 溢出不静默 | P3-1 | P6 计数器 = 0；selftest 最大 effect 数 ≤ 8 |

---

## 8. 批准后的执行与检查点

| 检查点 | 交付 | 你看什么 |
|---|---|---|
| C0 | P0：facade + oracle 恢复 + 写入口 | `check_oracle.py` 三项 0 差异；`alpha.dat` 回来了；18 处 `v.ui.*` 原样；36 处 `Context` + 2 处 `wheel` 编译原样；65 处 feature/gesture touch 未动 |
| C1 | P1：7 个 app 头 + 一次性改名 + shader_abi | cpp 里 ability12 < 400 行；`host.hpp` 真 Host 且依赖 `shader_abi`；DAG |
| C2 | P2：pimpl + accessor + session.cpp | `session.hpp` 无 detail；38 处 `sy.get` 未动；构建早验通过 |
| C3 | P3：Outcome | `grep fx` = 0；0 分配计数 |
| C4 | P4：五门 | route_event / platform::resolve 就位；CONTRACT 差异行 |
| C5 | P5：尺寸 + HANDOFF §16 | 行数表全绿 |
| C6 | P6：基准 | 附录 C 差值 < 5%，0 分配 |

任一步方向不对，按回滚点退回上一个 commit。

---

## 附录 A：demo cpp 行数解剖（D2 的依据）

| 行段 | 内容 | 行数 | 归属 |
|---|---|---|---|
| 1–134 | includes / usings / 常量 | 134 | demo |
| 135–374 | camera / mesh / font / FrameClock / my_render | 240 | demo 框架 |
| 375–690 | Vertex / object_data / **shader ABI**（object_key、render_version、picking_result）/ record_info | 316 | demo 框架（P1 抽成 shader_abi） |
| 691–2275 | named_resource / command record / ui_layer / input init | 1,585 | demo 框架 |
| 2276–4222 | ui_new 初始化（hardware/descriptor/font/pipeline/texture/mesh） | 1,947 | demo 框架 |
| 4223–8026 | **ui_new widgets** | 3,804 | demo 框架 |
| 8027–8458 | type_erased_invoker / multicast | 432 | demo 框架 |
| 8459–8589 | glfw_input_translation | 131 | demo 框架 |
| 8590–10380 | **intent / gesture** | 1,791 | demo 框架 |
| 10381–10929 | mcs::app / render | 549 | demo 框架 |
| **10930–11156** | **table_glue** | **227** | **ability12** |
| **11157–13324** | **table_ui（Binding/Host/输入/绘制链）** | **2,168** | **ability12** |
| 13325–14785 | main + demo 场景 | 1,461 | demo |

P1 之后：ability12 在 cpp 里只剩 `make_table`/`make_table_screen`/`make_screen` 接线（< 400 行）。

## 附录 B：oracle 比对口径（P0 的门）

```text
输入：test_ui_ability11_table.hpp::selftest()    → 140 个 check(...)（运行 171 次）
      table12/test/selftest.hpp                  → 同上
比较：① 每个 check 的首个参数表达式（仅类型名归一化）
      ② 每个 check 的 label 串（0 容忍）
      ③ 调用点数 140 + verbose 运行 ok: 计数 171
禁止：改 label、改断言值、把测试体 v.ui.* 改写（facade 承载）
允许：附录 P0-3 的驱动白名单 W1–W8（换门必然产生的适配）
```

## 附录 C：基准记录（P6 填）

| 场景 | ability11 | ability12 | 差值 |
|---|---|---|---|
| compose A（8×60）×10k | — | — | — |
| compose B（3×24）×10k | — | — | — |
| dispatch（press/drag/key）×100k | — | — | — |
| DrawList op 数 / zone 数（A/B） | — | — | 逐条相等 |
| dispatch 堆分配次数（100 次常见命令） | 0（复用 fx） | 0（inplace_vector） | 0 |

## 附录 D：最终文件树（预算）

```text
table12/
  contract.hpp          ≤300   身份/坐标/TargetKey/Owner
  text.hpp              ≤480   Font/GlyphMetrics/Arena/utf8/fit_text
  model.hpp              204   Cell/Column/Table
  intent.hpp            ≤300   State/Interaction/drag/hover/wheel
  intent_types.hpp      ≤240   Theme + 11 组件
  frame.hpp             ≤300   Layout + hit
  draw_list.hpp         ≤130   Stream/ShapeOp/TextOp/DrawList
  command.hpp           ≤300   Key/Command/Effect/Outcome/Context/Router
  session.hpp           ≤220   Session + 门声明（无 detail；friend test::SessionAccess）
  session.cpp           ≤350   门实现（唯一 include detail 的地方）
  detail/runtime.hpp     ≤40   Runtime{router, pipe}（仅 session.cpp / test 可见）
  detail/common.hpp     ≤120
  detail/features/*.hpp ≤800 各  filter_find / layout_ops / selection / edit / overlay / router
  detail/layout_passes.hpp ≤500   9 pass
  detail/paint_passes.hpp  ≤550  10 pass
  detail/gestures.hpp   ≤450
  test/selftest.hpp    ≤1250  171 次断言（140 个 check 调用点，label/表达式与 ability11 原文一致）
  test/view_facade.hpp  ≤150  v.ui / v.sel 的引用代理 + State& 转换（P0）
  test/access.hpp        ≤60  router/pipe + detail 可见性（P2）
  test/check_oracle.py   ≤160  三项比对脚本（进仓库；不挂 CTest 硬依赖）
  test/check_oracle.ps1  ≤40  Windows 本地包装（可选）
table12_app/
  shader_abi.hpp        ≤200  ObjectKey / RenderVersion / PickingResult（P1 就建；static_assert）
  platform.hpp          ≤100  resolve（自包含；P4 完成）
  encode.hpp            ≤160  encode + clip_glyph（ui_dao 之后）
  glue.hpp              ≤350  字体/模型初始化、剪贴板、IME、cursor
  host.hpp              ≤700  Binding(组合 Session) + Host（依赖 shader_abi，不依赖 P4 类型）
  window_draw.hpp       ≤300  draw 链 + on_pick（只能被 demo cpp 在 host.hpp 之后 include）
  window_input.hpp      ≤900  on_* / 输入路由 / effect 应用 / 模型 API（同上）
  (host.cpp            后续)  框架抽头后再做，不计入本次 DoD
```

## 附录 E：C0 实测记录（P0 完成）

执行日期：2026-11-26。命令与结果（本机实跑，非估计）：

```text
构建     ninja mcsvulkan-vma-test_ui_ability12_table          → 成功（无 error）
oracle   check_oracle.py --ref ...11... --cur ...table12/test/selftest.hpp --run <exe>
         ref checks 142 / cur checks 142
         expr diffs 0 / label diffs 0
         run: ok=171 failed=0                                   → OK
回归     TABLE_SELFTEST=1   → 171 ok / 0 failed
         TABLE_EVTEST=1     →  32 ok / 0 failed
         TABLE_MULTITEST=1  →  71 ok / 0 failed
         TABLE_PICKCHECK=1  → 150 ok / 0 mismatch
oracle 恢复项
         alpha.datX = 0；alpha.dat 断言回来了
         grep -c "v\.ui\." table12/test/selftest.hpp = 18
         command::Context 原样 36 处；intent::wheel(v, ix, p, ...) 原样 2 处
         test::ViewFacade 生效（operator intent::State&）；facade 只在 table12/test/
写入口
         session.hpp 无 apply_model_effects；set_cell 只出现在 session::apply_effects
         dispatch 末尾 1 次 blanket touch；apply_effects 无 touch；Host apply_effects 无 touch
         features touch 58 + gestures touch 7 = 65（未动）
         Outcome.model_changed 在 dispatch 阶段恒 false
其它     ability10/11 git diff 为空
```

产物清单：`table12/test/view_facade.hpp`（新）、`table12/test/check_oracle.py`（新）、
`table12/test/selftest.hpp`（重生成，label 逐字节同 ability11）、`table12/session.hpp`（写入口拆分）、
`table12/command.hpp`（Effect 注释）、demo cpp（Host apply_effects）、HANDOFF §5.7 补充、CONTRACT §1/§2。

未做（按计划留给后续阶段）：P1 app 层搬移、P3 Outcome、P4 五门命名、P5 尺寸、P6 零开销。

## 附录 F：明确不做

- 不改 ability10/11；不改 `TargetKey`/picking ABI/命令顺序/坐标空间；
- 不引入 MVVM/observable/ECS/虚接口；
- 不重构 demo 框架（ui_new/intent/gesture/renderer/main）——只把 shader ABI 抽成 `shader_abi.hpp`；
- 不为达标改测试（除 P0 把 oracle 恢复原样）；
- 不做严格 DOD（SoA/arena 全量），HANDOFF §2.2 已列为另一个项目。
