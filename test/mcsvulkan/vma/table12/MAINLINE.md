# ability12 主线（一页）

> 目标：新读者 3 跳内解释**一次点击**和**一帧绘制**。

## 1. 文件地图

```text
table12/
  contract.hpp    Rect/Point/Viewport、TableId/RowId/ColumnId、Part/Zone/Target、
                  TargetKey、Font/GlyphMetrics/Arena
  model.hpp       Cell / Column / Table（真值）
  intent.hpp      Theme、State（持久意图）、Interaction（临时交互）、drag 变体
  frame.hpp       Layout、DrawList/ShapeOp/TextOp、Frame、hit（唯一几何命中）
  command.hpp     Key、Command、Effect、Outcome、Context、Router 模板
  detail/
    common.hpp        detail 的 using 表 + ensure_visible
    features.hpp      15 个 feature 孤岛 + AppRouter（顺序 = 行为）
    layout_passes.hpp LayoutContext + 9 个 pass + compose_layout
    paint_passes.hpp  DrawContext + 10 个 pass + compose_draw
    gestures.hpp      hit_zone / hit_target / gesture（fallback）
  session.hpp     Session（组合根）+ 门：dispatch / compose / hit + 窄门
  test/selftest.hpp   171 项 oracle（只走公共门）
table12_app/
  host.hpp        window::Binding / window::Host（定义在宿主 cpp）
  platform.hpp    platform::encode（唯一 GPU 出口）
test_ui_ability12_table.cpp   窗口/GPU/demo 宿主
```

## 2. 一次点击（3 跳）

```text
1. window::Host::input_event(mousebutton)
      → Binding::handle_event → click_dispatch
      → press.target = session::hit(binding, mx, my)      // 几何 + 语义覆盖
2. table::command::Press{...} → session::dispatch
      → note_click（连击计数）
      → detail::AppRouter::handle：filter → find → sort → … → tooltip（短路）
      → 没人认领 → detail::gesture（选择 / 拖拽 / 滚动）
3. Outcome{handled, model_changed, state_changed, effects}
      → apply_effects：SetCell 已在 dispatch 内写进 Model；WriteClipboard 交宿主
      → 宿主按 changes 触发回调 / 重绘
```

要点：命中（`session::hit`）与绘制（`ShapeOp.key`）用同一套 `Zone` 几何；
feature 的顺序写在 `AppRouter` 一处，短路即“第一个认领的生效”。

## 3. 一帧绘制（3 跳）

```text
1. session::compose(binding, viewport)
      → detail::compose_layout：9 个 layout pass 写 Session::geom
        reconcile → frame → columns → metrics → rows → frozen → visible → bars → edges
      → detail::compose_draw：10 个 paint pass 写 Session::frame
        visual → frame → group_bands → header → rows → cells → separators
               → zones → features → owners
2. platform::encode(DrawList, Font, view, sink, hover_fn)
      → shader instances（entity_index = TargetKey）
3. GPU：颜色附件 + picking 附件
      → platform::resolve（= model::resolve）→ Target → Host::on_pick
```

要点：layout/paint 都是纯函数组（只读 Model/State/Interaction，只写 Layout/DrawList）；
`zones` 与 `hit` 同一几何，`Proxy` stream 是拾取代理，最后画 = 拾取赢。

## 4. Session 里放什么

```cpp
struct Session {
    model::Table data;          // Model（真值）
    intent::State view;         // State（持久意图）
    intent::Interaction ix;     // Interaction（临时交互）
    intent::Theme prefs;        // 样式（宿主给）
    Font font;                  // 字形（宿主给）
    detail::AppRouter sys;      // CommandRouter：15 个 feature 的有序组合
    frame::RowPipeline pipe;    // 行变换缓冲
    frame::Layout geom;         // 派生缓存：Layout
    frame::DrawList frame;      // 派生缓存：DrawList
    TableId id; bool trace;     // 身份 + 诊断开关
};
```

窗口层的 `window::Binding` **继承** Session：`data/view/ix/prefs/font/sys/pipe/geom/frame`
都是同一份数据（不是第二份），窗口只额外持有 owner/win/缩放/指针/GPU 回读。

## 5. 验证

```powershell
$env:TABLE_SELFTEST=1  ; .\test_ui_ability12_table.exe   # 171 ok / 0 failed
$env:TABLE_EVTEST=1    ; .\test_ui_ability12_table.exe   #  32 ok / 0 failed
$env:TABLE_MULTITEST=1 ; .\test_ui_ability12_table.exe   #  71 ok / 0 failed
$env:TABLE_PICKCHECK=1 ; .\test_ui_ability12_table.exe   # 150 ok / 0 mismatch
```

oracle 只走公共门（Session + dispatch/compose/hit），所以它就是“重构没改行为”的证明。
