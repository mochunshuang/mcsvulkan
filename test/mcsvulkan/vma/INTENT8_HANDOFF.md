# INTENT8_HANDOFF — DrawPipelineData 绘制管线

日期：2026-11-09
目标：按 `INTENT8_NEXT_BLUEPRINT.md` 从 intent7 独立出 intent8；只重做绘制层，
主流程初始化与资源初始化保持原样；命令录制和提交显式可查。

## 1. 结果

- 构建：`mcsvulkan-vma-test_intent8` PASS。
- 运行：默认 1280x1040，42 个能力 tile（7 行），7 个能力 pass
  （缓存帧 5 个）+ 主 pass。
- 验证层：无 VUID、无 validation error、无 `[intent8-validator]`、无 assert。
- C API 列表：

```text
[intent8-secondary-c-api] calls=28 draws=4
[intent8-c-api] opcodes=16 calls=406 passes=8 draws=53
```

- 截图：`output/intent8_ability.png`（1280x1040，INTENT8 标题 + 42 tile + composed panel）。
- 运行时 resize 验证：`[resize] new extent=1857x1055 images=2`，无 VUID。
- 上限帧/截图环境变量：
  - `INTENT8_FRAMES=N`：渲染 N 帧后退出（测试用）。
  - `INTENT8_CAPTURE=<path.ppm>`：默认第 3 帧把 swapchain 读回并写 PPM，然后退出。
  - `INTENT8_CAPTURE_FRAME=N`：指定读回帧号（用于验证 scroll cache 在缓存帧上持续滚动）。

## 1.1 本轮新增 UI 特性

本轮没有改 shader ABI，也没有新增 render target；全部通过“多次 DAO 提交 +
已有 offscreen/composite 机制”实现：

| 能力 | 实现 | 位置 |
|---|---|---|
| 多层阴影 + 图片投影/发光 | 多个 `shape_blurred` 实例 + `blur(texture)` 黑/彩色 tint + 加法混合 | SHADOW STACK tile |
| 多边框 + 焦点环 + 虚线 | 多个 `shape_border` + `path_stroke` dash 段 | MULTI-BORDER tile |
| 文字硬阴影 / 描边 / 逐字渐变 | 同一 glyph 发射 shadow 1 份 + outline 8 份 + fill 1 份，fill 按 glyph 位置取渐变色 | TEXT FX tile、`emit_text_fx()` |
| 多 stop 线性渐变 | 3 段相邻 `sdf_linear`，1 px 重叠消除 AA 缝 | MULTI-STOP tile |
| Aurora / vignette / noise | 多个 additive `sdf_radial` blob + vignette radial + noise overlay，并用 scissor 裁到内容区 | AURORA/VIGNETTE tile |
| 任意路径 clip | stencil mask 用 `path_fill` 星形；mask pipeline 开 alpha-to-coverage，得到 4x MSAA 边缘 | PATH-CLIP tile |
| 圆角 clip + 滚动缓存 | 滚动内容先画到 offscreen 的未使用区域；composite 用 `texture()` 的 rounded mask 采样一个会移动的 UV 子矩形 | CLIP/SCROLL tile |
| 组模糊 / 组变换 / 组透明度 | 同一份 offscreen group 通过 `blur()`、旋转 mini quad、alpha 0.55 各采样一次 | GROUP FX tile |
| 玻璃 / backdrop filter 链 | offscreen pass 先把 `blur(backdrop)` 写入中间区域；composite 再对该已模糊纹理做 saturation color matrix，最后叠 tint/grain/双边框 | GLASS/FILTER tile |

滚动缓存的“缓存”是真实的：offscreen 层只在 `layerDirty_` 时重画；composite
每帧只改 UV offset。用 `INTENT8_CAPTURE_FRAME=3` 与 `=90` 两张截图可以看到
viewport 里的行滚动，但 `[ability-test] passes=5 layer=cached` 不变。

## 2. 新模型落点

```text
D = ConfigStore + ResourceTable

Draw = DrawPipelineData(config/resources) + DrawCall(operation)
Pass = RenderTarget + sequence<DrawCall ops>
Frame = sequence<Pass>
```

数据链路：

```text
UIWidget
  -> ui_dao / shader_data::CommandBuilder     (UI 意图)
  -> CommandStream / DrawSegment              (每个 segment 一个绘制)
  -> shader_data::emit                        (DrawSegment -> DrawPipelineData + DrawCall)
  -> intent8::FrameRecorder                   (DrawCall -> OpStream)
  -> intent8::VkEmitter                       (Op -> 显式 vkCmd*)
  -> intent8::c_api_list                      (同一 OpStream 的投影)
```

`DrawCall` 的展开顺序固定为：

```text
bind pipeline
bind descriptor set
set viewport / set scissor   (dynamic state)
push constants
bind index buffer / vertex buffers
draw
```

`c_api_list()` 直接遍历 recorder 的 `OpStream`；emitter 也遍历同一份
`OpStream`，并断言 `emittedCalls() == c_api_list().size()`。

## 3. 文件职责

| 文件 | 职责 |
|---|---|
| `test_intent8_draw_pipeline.hpp` | D 的核心：`PipelineConfig` / `PipelineResource` / `ShaderProgram` / `PipelineRenderingConfig` / `DynamicStateConfig` / `DescriptorLayout` / `RenderTargetConfig`+`RenderTargetResources` / `RenderTarget` / `DrawResources` / `DrawPipelineData` / `DrawCall`；`PipelineTable` / `DescriptorLayoutTable` / `ShaderLibrary` / `GlobalConfig` / `GlobalResources` / `ResourceTable` / `DrawContext`。 |
| `test_intent8_draw_ops.hpp` | O：`OpCode`、payload、`OpStream`、`c_api_name`、`c_api_list`、`dump`。每个 opcode 对应一个官方 `vkCmd*`。 |
| `test_intent8_draw_emitter.hpp` | `Op -> vkCmd*` 的 1:1 switch；不重排、不插入隐藏命令。 |
| `test_intent8_draw_validator.hpp` | 绘制层契约校验：target 活跃、pipeline/DrawResources 完整、pipeline-target formats/samples/viewMask 匹配、descriptor set layout 匹配、push range 匹配、dynamic state 已声明、index/indirect buffer 有效。 |
| `test_intent8_draw_recorder.hpp` | `FrameRecorder`：`beginPass/endPass`、`draw(DrawCall)` 展开、显式 barrier/transfer/executeCommands、`emitAll`、`dump`、`c_api_list`。 |
| `test_intent8_draw_pass.hpp` | `PipelineState` / `PassBarriers` / `DrawPass` / `PassManager` / `PassRecord` / `recordPass` / `recordPasses`；pass 只做分组。 |
| `test_intent8_frame_context.hpp` | 每帧 `ShaderBuffers + CommandBuilder`；`emit` 把 `DrawSegment` 变成 `DrawPipelineData + DrawCall`。 |
| `test_intent8_command_stream.hpp` | `DrawSegment`：引用 `DrawPipeline`，携带 viewport/scissor、indirect 范围、PushData。 |
| `test_intent8_command_builder.hpp` | UI 意图 -> DrawSegment；`setPipeline` 接收真实 `DrawPipeline*`。 |
| `test_intent8_shader_buffers.hpp` | 每帧 GPU buffer：vertex/index/heap/indirect/command constants。 |
| `test_intent8_shader_abi.hpp` | 既有 7-program ABI；未改 shader/DAO 设计。 |
| `test_intent8_ui_dao.hpp` | UI 数据编码；从 intent7 复制，独立文件。 |
| `test_intent8_ability_test.hpp` | 42 个能力 tile + composed panel；新增多层阴影/多边框/文字 FX/多 stop/aurora/路径裁剪/滚动缓存/组 FX/玻璃 filter；pass 参数是 `RenderTarget` + `DrawPipeline*`。 |
| `test_intent8_frame_scheduler.hpp` | acquire / submit / present 的显式队列层。 |
| `test_intent8_resource_state.hpp` | 显式 image/buffer 状态跟踪 + barrier 规划。 |
| `test_intent8.cpp` | 初始化（保留）+ 收集 config/resource 视图 + 命令录制/提交主循环；默认窗口 1280x1040。 |
| `shaders/test_intent8.vert/.frag` | 与 intent7 相同算法的独立 shader 副本。 |
| `shaders/test_intent8_abi.glsl` | shader ABI 的 intent8 独立副本（不再 include intent7 的 ABI 文件）。 |
| `test/mcsvulkan/vma.cmake` | 新增 `add_std_glsl_target(test_intent8 ...)`。 |

旧的 intent8 pass/registry/DataRef 路线没有沿用；intent7 的 pass 头文件也没有
被 intent8 引用。复制出来但未使用的 pass 头、scene、compose 已删除。

## 4. 配置与资源视图（不拥有所有权）

初始化没有重写：device/shader module/descriptor layout/pipeline layout/pipeline/
image/buffer/view/sampler/descriptor set 仍由原有代码创建。intent8 在初始化之后
只收集视图：

- `GlobalConfig.capabilities`：API version / properties / 已启用 feature 的布尔镜像。
- `GlobalConfig.shaders`：按 `VERT_SHADER_PATH` / `FRAG_SHADER_PATH` 建立的真实
  `VkShaderModule`（用于 `PipelineConfig.shaders`，由 draw 层在退出时销毁）。
- `GlobalConfig.descriptorLayouts`：复制 init 的 4 个 binding（UBO / sampled image
  array / sampler array / storage buffer），引用真实 `VkDescriptorSetLayout`。
- `GlobalConfig.pipelines`：8 个 `PipelineConfig + PipelineResource`：
  opaque/transparent 3D、opaque/transparent/additive/multiply UI、
  stencil mask/content。`PipelineConfig` 与 `PipelineResource` 分开存放。
- `GlobalResources.device/queue/queueFamily/frameSets`：真实句柄；每个 in-flight
  frame 的 descriptor set 以 `DescriptorSetResources{set, layout}` 进入 view。
- `GlobalResources.resources`：稳定 ID -> 真实对象 + 元数据 + 图像状态：
  swapchain、swapchain images/views、main MSAA/depth/pick/pick-resolve、
  offscreen MSAA/resolve、stencil、每帧 vertex/index/heap/indirect/constants、
  descriptor sets、texture views、samplers、8 个 pipeline。
- `DrawContext`：`config + resources + extent + frameIndex/imageIndex +
  descriptorSet + mainTarget`。场景只读它；resize 由所有者刷新稳定 ID 背后的真实
  handle（`ResourceTable::upsertImage/upsertImageView`）。

`RenderTarget` 把配置（format/samples/load-store/clear/renderArea）和资源
（color/resolve/depth/stencil views、renderPass、framebuffer）分字段保存；
`VkRenderingAttachmentInfo` 是二者解析后的录制数据。

## 5. 录制与提交

- 主 command buffer：`FrameRecorder` 逐 pass 录制，pass 之间显式
  `pass_boundary_barrier()`，pass 前后使用 `PassRecord.preBarriers/postBarriers`。
- 主 pass 与 extra pass 都是 `PassRecord`（target + stream + buffers + descriptorSet），
  `recordPasses` 只做顺序遍历；没有 PassCompiler/registry。
- secondary：composite pass 用 `FrameRecorder::beginInheritedPass` 录到 secondary
  command buffer，primary 用 `vkCmdExecuteCommands`；secondary 的 c_api 投影同样是
  `OpStream`（28 calls / 4 draws）。
- transfer：`FillBuffer -> barrier -> CopyBuffer`、`FillBuffer`、`CopyImageToBuffer`
  都在同一 `OpStream` 中，顺序与 `c_api_list` 一致。
- 提交：`intent8::FrameScheduler` 做 acquire / `vkQueueSubmit` / `vkQueuePresentKHR`；
  fence/semaphore 仍由 main 的 frame context 管理。
- 验证截图路径：渲染 -> `COLOR_ATTACHMENT -> TRANSFER_SRC` -> `CopyImageToBuffer`
  -> `TRANSFER_SRC -> PRESENT_SRC`，全部是显式 op；swapchain 因此增加了
  `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`（只服务读回验证，不改变绘制结果）。
- offscreen 层除了 group/backdrop/path-clip，还包含两份“中间纹理”：
  scroll cache（20 行列表）和 glass blur cache；它们不被原样 composite，
  只作为 composite 的采样源。因此没有新增 `VkImage`/`VkRenderPass`/pipeline。
- path clip 使用 `alphaToCoverageEnable = VK_TRUE` 的 stencil mask pipeline；
  alpha-to-coverage 是核心 MSAA 行为，不需要额外 feature 开关。

## 6. 与 intent7 的差异

1. 绘制层全部新写：`DrawPipelineData` + `DrawCall` + `FrameRecorder` + `VkEmitter`
   + `c_api_list`；不再有 pass_data/opcode/validator/builder 那套复制。
2. 场景构造参数从 `VkPipeline` 变成 `const DrawPipeline*`；pass target 从
   `PassDesc` 变成 `RenderTarget`（config + resources 分离）。
3. UI 的每段 draw 现在按蓝图展开为 bind/push/index/draw 的完整 DrawCall；
   每段都会重新 bind descriptor set / viewport / scissor / index buffer（显式、
   可验证），因此 c_api 数量高于 intent7 的 per-pass 去重版本。
4. 初始化保留；只增加 config/resource 视图收集和验证截图路径。
5. 标题改为 `INTENT8 UI ABILITY MATRIX`；默认窗口高度从 900 提到 1040
   （7 行 tile）。
6. 新增 UI 特性全部是 DAO/pass 组合；shader 7 program 和 draw 框架没有改。
7. 旧 intent8 的 pass/registry/DataRef 路线（此前生成物的日志仍留在 output/）
   没有被沿用。

## 7. 验收对照（蓝图第 11 节）

- [x] `DrawPipelineData` 能表示实际使用的 pipeline（8 个 pipeline config/resource）。
- [x] DrawCall 按正确顺序发出 bind/push/draw。
- [x] `c_api_list` 与 emitter 读同一 `OpStream`，并断言调用数一致。
- [x] 测试路径无 VUID。
- [x] pipeline config 与 pipeline resource 分离。
- [x] descriptor layout/config 与 descriptor set resource 分离。
- [x] target config 与 attachment resources 分离。
- [x] resource 有稳定 ID 和真实 handle。
- [x] config 全局、初始化后只读（resize 只更新 resource handle）。
- [x] pipeline layout / descriptor / push constant 匹配由 validator + 真实 init 保证。
- [x] target formats/samples 与 pipeline rendering 匹配由 validator 检查。
- [x] dynamic state 在 pipeline config 中声明并由 validator 检查。
- [x] barrier / layout transition 是显式 op。
- [x] 增加第二 pipeline/target 不需要改 draw core（8 pipeline / 主 target +
  offscreen + stencil target）。
- [x] classic render pass：`TargetKind::ClassicRenderPass` + `vkCmdBeginRenderPass2/
  vkCmdNextSubpass2/vkCmdEndRenderPass2` 已在 `RenderTarget`/emitter 中支持；
  当前 app 只走 dynamic rendering（未用 classic 路径）。

## 8. 已知限制

1. 当前 app 只使用一个 descriptor set / 一个 descriptor set layout；多 set、
   dynamic offset、descriptor buffer 未接入（intent8 数据结构可扩展）。
2. `PipelineConfig` 是手写/捕获的配置，没有做 SPIR-V reflection；binding 与
   push range 与 shader 的匹配由 init 时的真实创建和 validator 共同保证。
3. `ResourceTable` 记录的是稳定生命周期资源；pass 内的 per-frame GPU buffer
   通过 `ShaderBuffers` 直接引用，没有逐帧重复注册。
4. classic render pass 只有类型与 emitter 支持，没有运行时用例。
5. 没有 raw escape op；所有记录操作都必须有对应 `vkCmd*`。这是有意的。
6. 截图环境变量和 `TRANSFER_SRC` usage 只用于验证，不属于绘制模型。
7. 本轮新增效果的能力边界：
   - 文字 outline/shadow 是 glyph 多份偏移副本，不是 MSDF outline 参数；
   - 多 stop 只支持线性（相邻 2-stop 带拼接）；径向/圆锥仍是 2 stop；
   - aurora 是多个 radial blob 的近似 mesh gradient；
   - 玻璃 filter 链是“blur 到中间纹理 -> color matrix 采样”，只做饱和度矩阵；
   - 滚动缓存使用 offscreen 的固定未用区域，内容宽 160 px，适合演示；
   - path clip 是单级 stencil（reference 1），没有嵌套 stencil 栈；
   - 圆角 clip 是 composite 的 TexQuad mask，不是几何裁剪。

## 9. 构建 / 运行

```powershell
cd E:\0_github_project\mcsvulkan\build
ninja mcsvulkan-vma-test_intent8
```

```powershell
E:\0_github_project\mcsvulkan\output\test_program\mcsvulkan\vma\test_intent8.exe
```

验证截图（PowerShell）：

```powershell
$env:INTENT8_CAPTURE = "E:\0_github_project\mcsvulkan\test\mcsvulkan\vma\output\intent8_capture.ppm"
$env:INTENT8_CAPTURE_FRAME = "3"   # 可选，默认 3
E:\0_github_project\mcsvulkan\output\test_program\mcsvulkan\vma\test_intent8.exe
```

scroll cache 验证：分别用 `INTENT8_CAPTURE_FRAME=3` 和 `=90` 截图，
CLIP/SCROLL viewport 里的行位置会变化，但日志仍是 `layer=cached`（内容没有重画）。

PPM -> PNG 使用仓库环境里的 Pillow 即可；当前截图已保存为
`output/intent8_ability.png`。

## 10. 关键日志

```text
[ability-test] passes=7 glyphs=897 layer=rebuilt
[intent8-secondary-c-api] calls=28 draws=4
[intent8-c-api] opcodes=16 calls=406 passes=8 draws=53
[ability-test] passes=5 glyphs=897 layer=cached
[intent8-capture] wrote ...\output\intent8_capture.ppm 1280x900
```

无 `[intent8-validator]`、无 `VUID-`、无 `Validation Layer: Error`。
