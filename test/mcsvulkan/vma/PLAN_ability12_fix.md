# ability12 修改计划（v5）：G0–G7 全局顺序

> 状态：**C0 已通过**（tag `ability12-c0`，见附录 E）。v5 只做两件事：
> ① 把接口/文件/验收冻在 **G0**；② 用 **G0–G7 全局顺序**替代 v4 的 P0–P6，消掉"搬完再改名 / 大文件里改 feature"的返工。
> 批准后**不再等全量复核**，直接进 G2（app 层搬移），做到 **C1** 停下报告。

---

## 0. 结论

- **P0（= G1）已完成并验收**：oracle 三项 0 差异、写入口拆分、171/32/71/150、ability10/11 零改动。
- **v5 的核心是"先冻结、再搬、再改"**：接口只冻结一次、文件只搬一次、大文件先拆后改。
- 之前 v4 的三处局部最优已在 v5 消掉（见 §0.1）。

### 0.1 v5 相对 v4 的 4 项（你的批准条件）

| # | 要求 | v5 落点 |
|---|---|---|
| 1 | **接口冻结（G0）**：五门最终名 / Outcome 公共签名 / PickingResult / Session 字段最终名 / app DAG | §4 冻结清单（可直接照抄成代码） |
| 2 | **不返工矩阵**：阶段 × 文件，标出 P1 搬过的文件会不会被 P2/P3/P4 再大改 | §4.1 矩阵 + 三条"已消掉的返工" |
| 3 | **features 切分前移**：G3（P2 之后、P3 之前） | §5 G3；P3 的 Outcome 签名改动落到 5–6 个小文件 |
| 4 | **C0 先提交/打 tag** | 已执行：commit `b6abadc c0` + tag **`ability12-c0`**（回滚点） |

三条已消掉的返工：

| 原返工 | v5 做法 |
|---|---|
| P4 才把 `input_event` 改 `route_event`（P1 刚搬完就再改 37 处） | **G0 就改名**（37 处机械替换，零行为变化）；G2 搬移时代码已是最终名 |
| P5 才拆 `features.hpp`，P3 要在 2075 行里改 15 个 feature | **G3 先切分**（纯移动），G4 的 Outcome 改动落进 5–6 个小文件 |
| P1 用中间 `Context::fx`，P3 再改 | 复核结论：**app 层 0 处接触 `Context`/`fx`**（36 处只在 selftest），所以 G2 搬移的代码对 G4 天然免疫；selftest 的 36 处 `Context` 在 G4 一次改（白名单 W7） |

---

## 1. 现状与复核（实测）

| 项 | 值 | 备注 |
|---|---|---|
| demo cpp | 14,789 行 | ability12-owned 2,395（table_glue 227 + table_ui 2,168）；其余是 demo 框架（附录 A） |
| `table12_app/host.hpp` | 25 行 | 只有 `using` 别名 |
| `table12/session.hpp` | 313 行 | 仍 include detail；有 5 个访问器 |
| `detail/features.hpp` | 2,075 行 | 15 个 feature |
| selftest | 204 处断言相关：142 check 调用点 / 171 次运行 / 41 label 已恢复 / 18 处 `v.ui.*` | 附录 B |
| `touch()` | 84 次（82 处代码 + 2 行注释）：feature 58 + gesture 7 = 65；其余 19 处是 layout/common/intent/session | 口径见 §5 G1 |
| `sy.get<>` / `command::Context` / `detail::*` | 38 / 36 / 35（19 名字） | G5 前需要 test accessor |

---

## 2. 目标（可测，不变）

| 维度 | 指标 |
|---|---|
| 行为 | 171/32/71/150；`check_oracle.py` 三项 0 差异（表达式 / label / 调用点，自适应 142） |
| 结构 | 五门各一处实现且名字 = 冻结清单；`session.hpp` 无 detail（G5 后）；`Context` 无 `fx`（G4 后） |
| 可读 | ability12-owned 文件 < 1500；公共 schema 头 < 300；demo cpp 里 ability12 段落 < 400（G2 后） |
| 性能 | dispatch 常见路径 0 堆分配（G4/G7）；compose A/B 与 ability11 差 < 5%（G7） |

---

## 3. 全局顺序（v5）

```text
G0  契约冻结（编译骨架，零行为变化）            ← 本文件 §4；批准后立即执行
G1  P0：oracle + facade + 写入口               ← 已完成（C0 通过，tag ability12-c0）
G2  P1：app 层搬移（直接用 G0 最终接口）        ← 下一步，做到 C1 停
G3  P2：核心文件切分（features / passes，纯移动）
G4  P3：Outcome 实现（在切好的小文件里改）
G5  P4：pimpl + session.cpp + 五门收尾
G6  P5：尺寸收尾 + HANDOFF §16 测量口径
G7  P6：tuple 折叠 + 基准 + 0 分配
```

与 v4 的映射：G1=P0、G2=P1、G3=原 P5 的 features 切分（前移）、G4=P3（Outcome）、G5=P2（pimpl）+ P4（五门收尾）、G6=P5（尺寸+§16）、G7=P6。

单向门（先定死，后面不许改）：
`TargetKey/picking ABI` · 五门名字 · `Session` 字段名 · `Outcome` 公共签名 · 文件/namespace DAG · oracle 与 D2 口径。
可逆项（可以后面调）：pimpl 的形态、pass 表实现、EffectList 容量细节、性能数字。

---

## 4. G0：接口冻结清单

> 这一节是"照抄级"的契约；G0 只落地**能零行为变化落地**的部分（ABI 类型搬家、`resolve` 定名、
> `route_event` 定名、删访问器），其余冻结为声明，由后续阶段实现。

### 4.1 五门（最终名与签名）

```cpp
// ── table12_app/host.hpp（窗口层）─────────────────────────────────────
namespace table::window {
struct Host {
    void route_event(const event::Event &e);   // 唯一输入入口：平台事件 → Command / 窄门
    void begin_frame() noexcept;
    void mark_drawn(Binding &) noexcept;
    bool accept_pick(const platform::PickingResult &) noexcept;
    void on_pick(const platform::PickingResult &);
    void apply_cursor(GLFWwindow *) noexcept;
    std::uint32_t allocate_table_id() noexcept;
    std::uint32_t hover_fn{~0U};
};
} // namespace table::window

// ── table12/session.hpp（组合根）──────────────────────────────────────
namespace table::session {
Outcome dispatch(Session &, const command::Command &);   // 只写 State/Interaction
bool    apply_effects(Session &, const Outcome &);       // 唯一 Model 写点
Frame   compose(Session &, const Rect &viewport);        // 只写 layout/draw 缓存
Target  hit(const Session &, float x, float y);          // CPU 命中门（只读）
} // namespace table::session

// ── table12_app/platform.hpp（ABI 端口）───────────────────────────────
namespace table::platform {
template <class View, class Sink>
void encode(const frame::DrawList &, const Font &, const View &, Sink &&,
            std::uint32_t hover_fn);                     // 唯一 GPU 出口
[[nodiscard]] Target resolve(const model::Table &, const PickingResult &); // 唯一 GPU 入口
} // namespace table::platform
```

口径：`session::hit` 是 **CPU 命中门**（draw/hit 共用几何），不占五门位置；五门 = route_event / dispatch /
compose / encode / resolve。`dispatch + apply_effects` 合起来是唯一写路径（HANDOFF §5.7 补充）。

### 4.2 `Outcome` / `Context` 公共签名（G4 实现）

```cpp
namespace table::command {
using Effect = std::variant<SetCell, WriteClipboard>;     // 保持
using EffectList = std::inplace_vector<Effect, 8>;        // 容量 8；溢出 assert + abort（不静默丢）
struct Outcome {
    bool handled{false};
    bool model_changed{false};   // dispatch 阶段恒 false；apply_effects 返回值补
    bool state_changed{false};
    EffectList effects{};
    static Outcome handled_now() noexcept;
    void merge(const Outcome &) noexcept;
};
struct Context {                 // 无 fx
    const model::Table &d;
    intent::State &v;
    intent::Interaction &ix;
    const intent::Theme &p;
    const frame::Layout &g;
    const Font &ft;
    bool trace{false};
    TableId id{};
};
template <class... Fs> struct Router { /* 短路折叠 + merge，见 §5 G4 */ };
}
```

### 4.3 ABI 类型（`table12_app/shader_abi.hpp`，G0 落地）

```cpp
namespace table::platform {
struct ObjectKey {                       // 8B
    std::uint32_t object_type{};
    std::uint32_t entity_index{};
    auto operator<=>(const ObjectKey &) const = default;
};
template <std::size_t Bits> class RenderVersion { /* 位宽掩码、回绕、to_u32 */ };
using RenderVersion16 = RenderVersion<16>;   // RENDER_VERSION_BIT_WIDTH = 16
struct PickingResult {                   // 16B
    using version_t = RenderVersion16;
    static constexpr std::size_t version_bits = 16;
    static constexpr std::size_t primitive_bits = 16;
    ObjectKey key;            // outPicking.xy
    std::uint32_t packed;     // z: version<<16 | primitive
    std::uint32_t hover_fn;   // w
    [[nodiscard]] std::uint16_t render_version() const noexcept;
    [[nodiscard]] std::uint16_t primitive_id() const noexcept;
    static constexpr std::uint32_t pack(version_t, std::uint16_t) noexcept;
};
static_assert(sizeof(ObjectKey) == 8);
static_assert(sizeof(PickingResult) == 16);
}
```
demo cpp 里保留过渡别名（G0，避免 200 处无谓改名）：
`using object_key = table::platform::ObjectKey;` / `using render_version_t = table::platform::RenderVersion16;` /
`using picking_result = table::platform::PickingResult;`（这三个别名不进入 table12_app 头，G6 清理）。

### 4.4 `Session` 最终字段名（G2 一次改名）

**字段归属表（G2a 的可审计矩阵：先按域切开，再替换）**

| 归属 | 字段 | 在 app/Binding 里的写法 |
|---|---|---|
| **Session**（要 `s.session.` 前缀） | `data→model`、`view→state`、`ix→interaction`、`prefs→theme`、`font→font`、`geom→layout`、`frame→draw`、`sys→router`、`pipe→pipe`、`id→id` | `s.session.model` / `s.session.state` / … |
| **Binding 自己**（保持 `s.`） | `screen`、`owner`、`win`、`mx/my/wx/wy/sx/sy/vw/vh`、`pointer`、`pick`、`pick_version`、`arbiter`、`input_state`、`now`、`esc_cancelled`、`cb`、`cb_r0…cb_valid`、`changes`、`inputs`、`info` | `s.win` / `s.arbiter` / … 不变 |
| **Host 自己** | `tables`、`draw_order`、`focused`、`pressed`、`hovered`、`binding`、`input_attached`、`pointer_x/y`、`pointer_inside`、`frame_serial`、`hover_fn`、`next_table_id`、`last_pick` | `h.focused` / … 不变 |

**三个替换域（顺序固定，不交叉）**

| 域 | 文件 | 规则 |
|---|---|---|
| ① Session 内部 | `table12/session.hpp` | `s.data→s.model`、`s.view→s.state`、`s.ix→s.interaction`、`s.prefs→s.theme`、`s.geom→s.layout`、`s.frame→s.draw`、`s.sys→s.router`；**不加** `session.` |
| ② Binding / app | `test_ui_ability12_table.cpp` | 同上字段映射，但前缀是 `s.session.*`；`s.screen/s.win/s.arbiter/s.pointer/s.cb/...` 不动 |
| ③ selftest | `table12/test/selftest.hpp` | 绑定行 `S.data→S.model`、`S.view→S.state`、`S.ix→S.interaction`、`S.prefs→S.theme`、`S.geom→S.layout`、`S.sys→S.router`；facade 用 `{S.state, S.interaction}` |

审计顺序（用编译器当审计器）：
1. 先改 `Session` 字段声明 + 域①；编译 → 域②的 `s.data` 全部报错；
2. 按接收者（`s.` / `st.` / `a->` / `hovered->` / `focused->` / `pressed->` / `arena.`）补 `s.session.`；
3. `grep -nE '\bs\.(data|view|ix|prefs|geom|frame)\b'` 应为 0；
4. `grep -n 's\.session\.' table12/session.hpp` 应为 0（域不交叉）；
5. 四门 + oracle 全绿再提交。

```cpp
struct Session {
    model::Table model;            // 注意：成员 model 遮蔽 table::model，类内禁止写 model::X
    intent::State state;
    intent::Interaction interaction;
    intent::Theme theme;
    Font font;
    detail::AppRouter router;      // G5 后进 pimpl，字段名不变（访问走 test::access）
    frame::RowPipeline pipe;       // 同上
    frame::Layout layout;
    frame::DrawList draw;
    TableId id{};
    bool trace{false};
    // 删除 5 个访问器：model()/state()/interaction()/layout()/draw_list()（G0 先删）
};
```

### 4.5 `table12_app` 文件 DAG（G2 建立）

```text
shader_abi.hpp   (无依赖；std + 基础整数)
glue.hpp         (无 Host 依赖：font/model 初始化、剪贴板、IME、cursor)
platform.hpp     (shader_abi + model/frame: resolve)
encode.hpp       (ui_dao: encode + clip_glyph；必须在 ui_dao 之后 include)
host.hpp         (session + shader_abi + ui_new/GLFW: Binding 组合 Session + Host)
window_draw.hpp  (host.hpp: draw 链 + on_pick)
window_input.hpp (host.hpp: on_* / 输入路由 / effect 应用 / 模型 API)

include 顺序（demo cpp）：
framework(ui_new/intent/gesture/ui_dao/input_frame) → shader_abi/glue/platform/encode
  → host.hpp → window_draw.hpp → window_input.hpp → demo 场景(main)
```

### 4.6 不返工矩阵（阶段 × 文件）
`●` 新建 · `◐` 会再改 · `○` 不再改 · `—` 不涉及

| 文件 | G0 | G2 | G3 | G4 | G5 | G6 | G7 |
|---|---|---|---|---|---|---|---|
| shader_abi.hpp | ● | ○ | ○ | ○ | ○ | ○ | ○ |
| platform.hpp (resolve) | ◐ | ○ | ○ | ○ | ● | ○ | ○ |
| encode.hpp | — | ● | ○ | ○ | ○ | ○ | ○ |
| glue.hpp | — | ● | ○ | ○ | ○ | ○ | ○ |
| host.hpp | — | ● | ○ | ○ | ◐(pimpl 访存) | ○ | ○ |
| window_input.hpp | — | ● | ○ | ○ | ◐(apply/route 收尾) | ○ | ○ |
| window_draw.hpp | — | ● | ○ | ○ | ○ | ○ | ○ |
| session.hpp | ◐(删访问器) | ◐(字段改名) | ○ | ◐(声明) | ●(pimpl/删 detail) | ◐(注释) | ○ |
| session.cpp | — | — | — | — | ● | ○ | ○ |
| detail/features.hpp | ○ | ○ | ●(切 5–6 文件) | ◐(Outcome 签名) | ○ | ○ | ◐(pass 折叠?) |
| detail/layout_passes.hpp | ○ | ○ | ○ | ○ | ○ | ○ | ◐(tuple) |
| detail/paint_passes.hpp | ○ | ○ | ○ | ○ | ○ | ○ | ◐(tuple) |
| test/selftest.hpp | ○ | ◐(绑定 5 行) | ○ | ◐(Context 36 处去 fx) | ◐(sy/pipe 走 access) | ○ | ◐(0 分配断言) |
| test/view_facade.hpp | ○ | ○ | ○ | ○ | ○ | ○ | ○ |
| test/access.hpp | — | — | — | — | ● | ○ | ○ |
| demo cpp | ◐(route_event/别名) | ●(搬出 2.4k 行) | ○ | ○ | ○ | ○ | ○ |

结论：**G2 搬过的 6 个头，G3/G4 完全不碰**；只有 G5 的 pimpl 会让 `session.hpp`/`host.hpp`/`window_input.hpp`
各改一处（访问器从继承字段改成 `s.session.*`/`test::access`）。没有"搬完再大改"的文件。

---

## 5. 各阶段步骤

### G0：契约冻结（零行为变化）

1. **删访问器**（5 个，未使用）：`Session::model()/state()/interaction()/layout()/draw_list()`；
2. **`shader_abi.hpp`**：从 demo cpp 375–690 段搬 `object_key`/`render_version`/`picking_result`，
   命名成 `table::platform::{ObjectKey, RenderVersion, RenderVersion16, PickingResult}` + `std::hash<ObjectKey>`；
   demo 里留 3 个过渡别名（§4.3）；
3. **`platform::resolve`**：`table12_app/platform.hpp` 加 `resolve(model, PickingResult)`（内部调 `model::resolve`），
   demo 的 2 处 picking 调用改走它；
4. **`Host::input_event` → `Host::route_event`**（37 处，机械）；
5. **CONTRACT.md** 增 §7「G0 冻结清单」（照抄 §4.1–4.5）；
6. **验收**：`171/32/71/150` + oracle 三项 0 差异 + `grep route_event` 生效 + ability10/11 零改动；
   提交 `ability12 G0: freeze gate names / ABI / session fields`。

### G1：oracle + facade + 写入口（已完成，C0）

见附录 E。tag `ability12-c0`。

### G2：app 层搬移（P1；C1 检查点）

**目标**：demo cpp 里 ability12 段 2,395 → < 400 行；`host.hpp` 是真 Host（组合 Session）；文件 DAG 落地。

1. **改名一次到位**（§4.4）：`data→model`、`view→state`、`ix→interaction`、`prefs→theme`、`geom→layout`、
   `frame→draw`、`sys→router`；影响面 = `session.hpp` + demo cpp 的 `s.<field>` + selftest 5 行绑定；
2. **`Binding` 改组合**：
   ```cpp
   struct Binding : ...  →  struct Binding { table::session::Session session; ... };
   ```
   搬移时同步把 `s.data` → `s.session.model` 等（约 260 处，全在搬移的 2.4k 行里）；
3. **建 6 个头**（§4.5 DAG），把 `table_glue` / `table_ui` 的代码搬进去；
4. **demo cpp 只留**：`make_table`（`make_aggregate` 接线）、`make_table_screen`（`ui_new` 布局）、
   `make_screen`、`main`；`#include "table12_app/*.hpp"` 按 DAG 顺序；
5. **验收（C1）**：`171/32/71/150` + oracle 三项 0 差异；cpp ≤ 13,000 且 ability12 段 < 400；
   6 个新头各 < 1500；`grep -n "struct Binding" host.hpp` 命中且是组合；DAG 顺序与 include 一致；
   `shader_abi.hpp` 在 G0 已可用（host.hpp 直接依赖它）；ability10/11 零改动。

### G3：核心文件切分（P2，纯移动）

把 `detail/features.hpp`（2,075）切成：
`features/filter_find.hpp`（~400）· `features/layout_ops.hpp`（sort/resize/reorder/freeze ~450）·
`features/selection.hpp`（checks/scroll/table_resize/selection ~500）· `features/edit.hpp`（edit/clipboard/undo ~450）·
`features/overlay.hpp`（menu/tooltip ~250）· `features/router.hpp`（AppRouter + includes ~120）。
同时把 `contract.hpp`（717）拆出 `text.hpp`、`intent.hpp`（525）拆出 `intent_types.hpp`、`frame.hpp`（360）拆出 `draw_list.hpp`？
——**不**，尺寸拆分留到 G6；G3 只切 features（因为 G4 要改它）。
**验收**：纯移动（`git diff -M` 应显示 rename/移动），`171/32/71/150` + oracle 0 差异。

### G4：`Outcome` 实现（P3）

1. `command.hpp`：`EffectList`/`Outcome` 按 §4.2 落地；`Context` 删 `fx`；`Router` 短路折叠 + merge；
2. 5–6 个 feature 文件：`bool handle` → `Outcome handle`，helper 返回 Outcome，`c.fx->push_back` → `o.effects.push_back`；
3. selftest 36 处 `Context tc{...}` 去掉 `&fx,`（白名单 W7 覆盖）；`send()` 从 `Outcome.effects` 读；
4. `session::dispatch/feed_text/paste_text` 改用返回的 `Outcome`（不再有局部 fx vector）；
5. **验收**：`171/32/71/150`；`grep -c "using Effect"` = 1；`grep -rn "c\.fx\|Context::fx"` = 0；0 分配计数器 = 0（G7 正式断言）。

### G5：pimpl + `session.cpp` + 五门收尾（P2+P4）

1. `detail/runtime.hpp`（`Runtime{router, pipe}`）+ 最小 `session.cpp` + `vma.cmake` 的 `target_sources`
   **先冒烟构建**（`${TARGET_NAME}` 失效就换显式目标名）；
2. `session.hpp` 改 pimpl（unique_ptr<detail::Runtime>、out-of-line 特殊成员、`friend test::SessionAccess`），
   **不含 detail**；门实现搬到 `session.cpp`；
3. `test/access.hpp`（router/pipe + detail 可见性），selftest 只改 2 行绑定；
4. `platform.hpp` 收尾：`encode` 移到 `encode.hpp`，`CONTRACT.md` 记 `resolve` 与草案差异；
5. **验收**：`grep -n "detail/" session.hpp` = 0；`session.hpp` ≤ 220；`grep -rn "input_event"` = 0；
   `grep -rn "picking_result" table12* --glob '!shader_abi.hpp'` = 0；五门各一处；`171/32/71/150`。

### G6：尺寸收尾 + HANDOFF §16（P5）

- 拆 `contract.hpp → +text.hpp`、`intent.hpp → +intent_types.hpp`、`frame.hpp → +draw_list.hpp`；
- HANDOFF 追加 **§16 测量口径**（正文，见 §5 G6 附录）；CONTRACT 互引；
- 行数表全绿 + `171/32/71/150`。

### G7：零开销 + 基准（P6）

- pass 表改 `std::tuple` 折叠；paint/layout 的 `std::array<fn*>` 只留诊断；
- 0 分配计数器断言（selftest 末尾 100 次 dispatch = 0）；
- 基准（scratch TU）：compose A/B ×10k、dispatch ×100k、DrawList op/zone 逐条相等；结果填附录 C；
- 劣化 > 5% 回退对应项。

---

## 6. 每阶段验收门

```powershell
# 构建
ninja -C build mcsvulkan-vma-test_ui_ability12_table

# 四基线
$exe = "output\test_program\mcsvulkan\vma\test_ui_ability12_table.exe"
$env:TABLE_SELFTEST="1";  & $exe    # 171 ok
$env:TABLE_SELFTEST=$null
$env:TABLE_EVTEST="1";    & $exe    #  32 ok
$env:TABLE_EVTEST=$null
$env:TABLE_MULTITEST="1"; & $exe    #  71 ok
$env:TABLE_MULTITEST=$null
$env:TABLE_PICKCHECK="1"; $env:DSH_AUTOEXIT="1"; & $exe   # 150 ok / 0 mismatch
$env:TABLE_PICKCHECK=$null; $env:DSH_AUTOEXIT=$null

# oracle 三项（自适应调用点数，当前 142）
python test/mcsvulkan/vma/table12/test/check_oracle.py `
    --ref test/mcsvulkan/vma/test_ui_ability11_table.hpp `
    --cur test/mcsvulkan/vma/table12/test/selftest.hpp --run $exe

# 结构（按阶段启用）
rg -n "detail/" test/mcsvulkan/vma/table12/session.hpp          # G5 后 = 0
rg -n "c\.fx|Context::fx" test/mcsvulkan/vma/table12            # G4 后 = 0
rg -n "input_event" test/mcsvulkan/vma/table12_app              # G0 后 = 0
rg -n "picking_result" test/mcsvulkan/vma/table12_app test/mcsvulkan/vma/table12 `
   --glob '!shader_abi.hpp'                                     # G5 后 = 0
```

---

## 7. 风险与回滚

| 风险 | 处置 |
|---|---|
| G2 改名漏点（`s.data` → `s.session.model`） | 纯搬移 + 统一改名；`git diff` 只应是移动/改名；171+oracle 全绿再提交 |
| `Binding` 组合后反射 `^^Binding` 失效 | `make_aggregate<"Table">` 用 `field<"arena">`，类型名不变；反射 `find_function(^^Binding)` 同步改 |
| G3 切分导致 include 顺序问题 | 每个 feature 头自包含（显式 include `../command.hpp`），不靠传递包含 |
| G5 `target_sources` / modules 冲突 | 先最小 session.cpp 冒烟；`${TARGET_NAME}` 失败换显式目标名；再失败退回中间态并记偏差 |
| `inplace_vector` 工具链缺失 | 40 行 `SmallVector<Effect,8>` 退化，接口一致 |
| 性能劣化 | G7 基准 > 5% 回退对应项 |

---

## 8. 检查点

| 检查点 | 交付 | 你看什么 |
|---|---|---|
| C0 ✅ | G1 | 已通过（附录 E） |
| C0.5 | G0 | `route_event`/`platform::resolve` 生效；`shader_abi.hpp` 有 static_assert；访问器已删；四基线 + oracle 全绿 |
| C1 | G2 | cpp ability12 段 < 400；`host.hpp` 真 Host（组合）；DAG 与 include 顺序一致；shader_abi 可用；六头 < 1500 |
| C2 | G3 | features 切成 5–6 文件；纯移动；四基线 + oracle 全绿 |
| C3 | G4 | `grep fx` = 0；`Outcome` 唯一结果；0 分配计数 |
| C4 | G5 | `session.hpp` 无 detail；五门各一处；`session.cpp` 链接；W7 白名单外无差异 |
| C5 | G6 | 行数表全绿；HANDOFF §16 |
| C6 | G7 | 基准表；0 分配；tuple 折叠 |

---

## 附录 A：demo cpp 行数解剖（D2 依据）

| 行段 | 内容 | 行数 | 归属 |
|---|---|---|---|
| 1–134 | includes / usings / 常量 | 134 | demo |
| 135–374 | camera / mesh / font / FrameClock / my_render | 240 | demo 框架 |
| 375–690 | Vertex / object_data / **shader ABI** / record_info | 316 | demo 框架（G0 抽 shader_abi） |
| 691–2275 | named_resource / command record / ui_layer / input init | 1,585 | demo 框架 |
| 2276–4222 | ui_new 初始化 | 1,947 | demo 框架 |
| 4223–8026 | **ui_new widgets** | 3,804 | demo 框架 |
| 8027–8458 | type_erased_invoker / multicast | 432 | demo 框架 |
| 8459–8589 | glfw_input_translation | 131 | demo 框架 |
| 8590–10380 | **intent / gesture** | 1,791 | demo 框架 |
| 10381–10929 | mcs::app / render | 549 | demo 框架 |
| **10930–11156** | **table_glue** | **227** | **ability12** |
| **11157–13324** | **table_ui** | **2,168** | **ability12** |
| 13325–14789 | main + demo 场景 | 1,465 | demo |

## 附录 B：oracle 比对口径

```text
抽取：两边全部 check(<expr>, "<label>");（自适应，当前 142 个调用点；运行 171 次）
归一化：仅类型名映射（ui_table::X → table::Y、cmd_press→Press、filter_feature→FilterFeature …）
比较：① 首参表达式 ② label 逐字节 ③ 调用点数 ref==cur
禁止：改 label / 改断言值 / 把测试体 v.ui.* 改写（facade 承载）
允许：驱动白名单 W1–W8（换门必然适配）
```

## 附录 C：基准记录（G7 填）

| 场景 | ability11 | ability12 | 差值 |
|---|---|---|---|
| compose A（8×60）×10k | — | — | — |
| compose B（3×24）×10k | — | — | — |
| dispatch ×100k | — | — | — |
| DrawList op/zone（A/B） | — | — | 逐条相等 |
| dispatch 堆分配（100 次） | 0 | 0 | 0 |

## 附录 D：最终文件树（预算）

```text
table12/
  contract.hpp ≤300   text.hpp ≤480   model.hpp 203
  intent.hpp ≤300     intent_types.hpp ≤240
  frame.hpp ≤300      draw_list.hpp ≤130
  command.hpp ≤300    session.hpp ≤220   session.cpp ≤350
  detail/runtime.hpp ≤40   detail/common.hpp ≤120
  detail/features/{filter_find,layout_ops,selection,edit,overlay}.hpp ≤800 各 + router.hpp ≤120
  detail/{layout_passes,paint_passes,gestures}.hpp ≤550 各
  test/{selftest,view_facade,access}.hpp + check_oracle.py
table12_app/
  shader_abi.hpp ≤200   platform.hpp ≤100   encode.hpp ≤160   glue.hpp ≤350
  host.hpp ≤700   window_draw.hpp ≤300   window_input.hpp ≤900
  (host.cpp 后续)
```

## 附录 E：C0 实测记录（G1 完成）

```text
构建成功；TABLE_SELFTEST 171/0；EVTEST 32/0；MULTITEST 71/0；PICKCHECK 150/0
check_oracle.py：ref 142 / cur 142（含 2 处跨行 check；自适应，不硬编码）
                 expr 0 差异；label 0 差异；run ok=171 failed=0
alpha.datX = 0；v.ui.* = 18；Context 36 / wheel 2 文本未改；facade 只在 test/
apply_model_effects = 0；set_cell 只在 session::apply_effects；Host apply_effects 无 touch
features touch 58 + gestures 7 = 65 未动；touch 总计 84 次（82 处代码 + 2 行注释）
ability10/11 git diff 为空；commit b6abadc「c0」+ tag ability12-c0
```

## 附录 F：明确不做

- 不改 ability10/11；不改 `TargetKey`/picking ABI/命令顺序/坐标空间；
- 不引入 MVVM/observable/ECS/虚接口；
- 不重构 demo 框架（ui_new/intent/gesture/renderer/main）——只把 shader ABI 抽成 `shader_abi.hpp`；
- 不为达标改 oracle；
- 不做严格 DOD（SoA/arena 全量），HANDOFF §2.2 已列为另一个项目。
