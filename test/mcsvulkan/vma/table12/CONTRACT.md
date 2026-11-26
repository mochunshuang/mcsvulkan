# ability12 contract（一页）

> 只描述**不变的契约**：七个 schema + 五个门 + 两条链 + 依赖方向。
> 实现细节见各头文件顶部说明；主线见 `MAINLINE.md`。

## 1. 七个 schema（谁拥有、活多久）

| schema | 类型 | 生命周期 | 谁写 |
|---|---|---|---|
| Model | `table::model::Table` | 表的一生，可序列化 | 只有 `session::apply_effects`（Host 在 dispatch 之后调用；effect 值来自 dispatch） |
| State | `table::intent::State` | 表的一生，可 `reset/reconcile` | `dispatch` 与窄门（`feed_text`/`open_menu`/`cancel_modal`/`wheel`） |
| Interaction | `table::intent::Interaction` | 窗口/帧级，掉帧可丢 | 窄门 `observe_pointer`/`update_hover`/`clear_hover` |
| Layout | `table::frame::Layout` | 一帧（`compose` 重建） | `detail::compose_layout` 的 9 个 pass |
| DrawList | `table::frame::DrawList` | 一帧（`compose` 重建） | `detail::compose_draw` 的 10 个 pass |
| Command / Outcome | `table::command::Command` / `Outcome` | 一次命令 | Host 构造命令；`dispatch` 返回结果 |
| Effect | `table::command::Effect` | 一次命令 | feature 产生；`SetCell` 在 dispatch 内应用，`WriteClipboard` 交 Host |

`Theme`（样式）与 `Font`（字形）是宿主给的只读配置，不算真值。

## 2. 五个门（每层只有一个入口）

```text
window::Host::route_event   ← 唯一输入入口（平台事件 → Command / 窄门）
session::dispatch           ← 唯一命令入口（只写 State / Interaction）
session::apply_effects      ← 唯一 Model 写点（Host 在 dispatch 之后调用）
session::compose            ← 唯一派生入口（layout passes → paint passes）
platform::encode            ← 唯一 GPU 出口（DrawList → instances，entity_index = TargetKey）
platform::resolve           ← 唯一 GPU 入口（TargetKey → Target；实现在 model::resolve）
session::hit                ← CPU 命中门（几何 + 语义覆盖，只读；不是 ABI 门）
```

`dispatch` + `apply_effects` 合起来是**唯一写路径**：Host 按
`dispatch → apply_effects → 剪贴板 → 回调 → 重绘` 的顺序调用，自己不写 Session 数据。
`Outcome.model_changed` 在 dispatch 阶段恒 false，由 `apply_effects` 的返回值补上。
touch 口径：feature/gesture 自身的 `touch()` 原样 + `dispatch` 末尾 1 次 blanket touch
（等价 ability11 `apply_effects` 末尾那次）；`apply_effects` 与 Host 都不 touch。

两个窄门只写 Interaction：`observe_pointer` / `update_hover` / `clear_hover` / `wheel`。
四个窄门只写 State：`feed_text` / `open_menu` / `paste_text` / `cancel_modal` / `ime_compose`。

## 3. 两条链

```text
draw : compose → Layout passes → DrawList passes → platform::encode → GPU
input: Host → Command → dispatch → CommandRouter(feature 短路链) → gesture fallback
                → Outcome{effects} → Host 应用平台 side（clipboard/回调/重绘）
```

两条链只在 `TargetKey` 上合并：
- draw 侧：`ShapeOp.key` / `TextOp.key` 写进 `entity_index`；
- input 侧：`session::hit` 得到 `Target`；
- picking 侧：`platform::resolve`（=`model::resolve`）把 `TargetKey` 还原成 `Target`。

## 4. 依赖方向（单向，无环）

```text
contract   ← 无（std + glm）
model      ← contract
intent     ← contract, model
frame      ← contract, model, intent
command    ← contract, model, intent, frame
detail/*   ← command（features → layout_passes → paint_passes → gestures）
session    ← detail/*
table12_app ← session, frame（host/platform）
cpp        ← 全部
```

禁止：`model/intent/frame/command/session` include `window/platform/ui_new/GLFW/Vulkan`；
禁止：核心出现 `void*`、`std::function` 回调、`getenv`、进程级全局、字符串反射。

## 5. 不动点（不许改）

- `TargetKey` 位布局：part 5 / table 5 / row 13 / col 9，`kRowNone`/`kColNone` 哨兵；
- `Part` 枚举的顺序（5 bit）与 `kTableMask = 0x1F`；
- 命令语义与 feature 优先级（`AppRouter` 里的顺序）；
- paint pass 顺序与 zone/hit 几何一致；
- 坐标空间：window logical → screen（`mx = wx * sx`），`Layout.box` 已含 owner offset。

## 6. 加东西改哪里

| 想加 | 只改 |
|---|---|
| Model 字段 | `model::Table` + 读它的 layout/paint pass |
| State 字段 | `intent::State` + `reset/reconcile` + 对应 feature + pass |
| Interaction 字段 | `intent::Interaction` + `Host` 的窄门调用 |
| Draw 字段 | `frame::DrawList` + 对应 `pass_*` + `platform::encode` |
| 命令 | `command::Command` + 一个 feature（不碰 Host） |
| 视觉 | 新增一个 `pass_*` + 插进 `kPaintPasses`（不碰其他 pass） |

## 7. G0 冻结清单（2026-11，单向门）

> 这一节是**不可回退**的名字与签名；后续阶段只实现，不再改名。
> 完整清单与代码骨架见 `PLAN_ability12_fix.md` §4。

### 7.1 五门

```cpp
// table12_app/host.hpp
namespace table::window { struct Host { void route_event(const event::Event &); ... }; }
// table12/session.hpp
namespace table::session {
Outcome dispatch(Session &, const command::Command &);   // 只写 State/Interaction
bool    apply_effects(Session &, const Outcome &);       // 唯一 Model 写点
Frame   compose(Session &, const Rect &viewport);        // 只写 layout/draw 缓存
Target  hit(const Session &, float x, float y);          // CPU 命中门（只读）
}
// table12_app/platform.hpp
namespace table::platform {
template <class View, class Sink>
void encode(const frame::DrawList &, const Font &, const View &, Sink &&, std::uint32_t hover_fn);
[[nodiscard]] Target resolve(const model::Table &, const PickingResult &);
}
```

### 7.2 ABI（`table12_app/shader_abi.hpp`）

`ObjectKey` 8B · `RenderVersion16`（16 bit）· `PickingResult` 16B（version 16 / primitive 16）。
静态断言写死在头里；demo 里只保留 `object_key/render_version_t/picking_result` 三个过渡别名。

### 7.3 Session 字段

`model / state / interaction / theme / font / router / pipe / layout / draw / id / trace`。
不再有 `data/view/ix/prefs/geom/frame` 短名，也不再有 `model()/state()/…` 访问器。
（成员 `model` 遮蔽 `table::model` 命名空间：Session 内禁止 `model::X`。）

### 7.4 Outcome（G4 实现，签名先冻结）

```cpp
using EffectList = std::inplace_vector<Effect, 8>;
struct Outcome { bool handled, model_changed, state_changed; EffectList effects; ... };
struct Context { /* 无 fx */ const model::Table &d; intent::State &v; intent::Interaction &ix; ... };
```

### 7.5 测量口径（HANDOFF §16）

"单文件 < 1500"只统计 ability12-owned 代码；demo 框架不计入；拆框架另立项。
