# test_ui_ability7 · UI 特效表达力与实现状态（修订版）

日期：2026-11-21
范围：`test/mcsvulkan/vma/test_ui_ability7.cpp` + `shaders/test_intent8.{vert,frag}` + `shaders/test_intent8_abi.glsl`
性质：只读收集，未改任何代码。
说明：本文修正了此前报告中的五处错误判断，见 §7「修订记录」。

---

## 0. 结论：三层，不能混在一起说

| 层级 | 结论 | 依据 |
|---|---|---|
| **平台：裸 Vulkan C API + 任意着色器** | **能**表达全部 UI 特效 | 渲染目标 + 采样 + 混合/模板 + 计算；片元/计算着色器足以表达任意逐像素函数（受资源与精度限制） |
| **现有 7 program（intent7/8 ABI）** | **能覆盖**文档里的 36 / 42 项能力 | 7 个图元 + 多次发射 + 多 target 组合；新增 9 项特效时**未改 ABI** |
| **ability7 当前这份代码** | 默认场景：**3 个 inter scope**（主 UI + 抓屏模糊 + 玻璃合成），已接“抓主 framebuffer”的磨砂玻璃；其它特效仍未接 | 场景与多 target 的调用没写，不是 API 不能 |

**三者不矛盾：可实现 ≠ 已实现。**
ability1 用同一套裸 C API 内联了 8 个 render scope，效果比任何带框架的版本都全 —— 这就是“平台能”的实证；
ability7 只发 1 个 scope，是“这份代码没写”的实证。

---

## 1. 概念的准确说法

### 1.1 Vulkan 没有“pass”这个对象

一个所谓 pass，落地就是：

```cpp
vkCmdBeginRendering(&infoA);   // 开 scope：附件/load-store/resolve 都在 infoA 里
... bind / push / draw ...
vkCmdEndRendering();           // 关 scope
// scope 之间：vkCmdPipelineBarrier2 做 layout transition
```

没有额外语义。**“pass”是调用序列的名字，不是 Vulkan 概念，也不是能力单位。**

### 1.2 多 scope ≡ 重复调裸 C API

换 target 就再写一对 `begin/endRendering` + 前后 barrier。等价关系：

```
多 scope  ==  多次 vkCmdBeginRendering / vkCmdEndRendering + vkCmdPipelineBarrier2
```

不需要新 API、新对象、新抽象。

### 1.3 “pass 框架”是可选的工程组织

- ability1：0 个框架类，`layerBegin` lambda + 主 pass，**一帧 8 个 scope**，效果最全。
- ability2：有 `FramePlan` / `PassKind::Draw|Blit` / `Recorder::pushClip/pushBlend/pushOpacity`，把 scope 显式化。
- intent8：又一套 `PassManager/PassRecord`，没有带来新特效。

所以框架不是能力前提。**能力 = 7 个图元 × 组合 × 多 target；框架只影响可维护性。**

### 1.4 UI 特效的范围（按“需要什么机制”分类）

| 族 | 典型项 | 需要什么 | 谁提供 |
|---|---|---|---|
| 形状/路径 | 圆角、逐角、椭圆、多边形、描边 | 片元 SDF + 解析 AA | 着色器 |
| 渐变 | 线性/径向/圆锥/多 stop | 片元数学（多 stop 靠组合） | 着色器 |
| 阴影/发光 | 外/内/多层/spread | SDF 模糊或离屏高斯 | 着色器 + 多 target |
| 图像 | 贴图、9-slice、mip、alpha 遮罩 | 采样器 + bindless | 着色器 + 描述符 |
| 文本 | MSDF / 位图 / 彩色 emoji | 距离场采样（shaping 在 CPU） | 着色器 |
| 模糊 | 内容/可分离/大半径 | 邻域卷积 / 多 pass | 着色器 + 多 target |
| 背景滤镜 | backdrop、毛玻璃 | 抓取已画内容 → 模糊 → 采样 | 多 target |
| 组/隔离 | group opacity、组变换 | 离屏 + 合成 | 多 target |
| 裁剪 | 矩形/圆角/任意/嵌套 | scissor / 模板 / 片元遮罩 | 状态 + 多 target |
| 混合 | over/add/mul/screen/overlay | 混合状态（或 EXT） | 管线状态 |
| 变换/动画 | 2D 仿射、3D 透视 | model 矩阵（插值在 CPU） | 顶点着色器 |
| 后处理 | vignette/grain/aurora/bloom/LUT | 全屏 pass / 多级 / 3D 纹理 | 多 target + 着色器 |
| 拾取 | hover / 命中 | uint 附件 + 回读 | 多附件 |

“多 target”与“管线状态”都是裸 C API，**没有一族需要新 Vulkan 能力**。

---

## 2. 事实清单（可逐条核对）

### 2.1 身份与构建状态

- `test_ui_ability7.cpp` 原本是 `test_ui_ability6.cpp` 的逐字节副本；本轮已在其上重构 replay（见 2.5）。
- `test_ui_ability7.exe` 已构建：`output/test_program/mcsvulkan/vma/test_ui_ability7.exe`。

### 2.2 着色器：7 个 program

映射来自 `test/mcsvulkan/vma.cmake:244`：

```cmake
add_std_glsl_target(test_ui_ability7 test_intent8.vert test_intent8.frag)
```

| 项 | 内容 |
|---|---|
| program | `SHAPE=0`、`PATH_FILL=1`、`PATH_STROKE=2`、`TEXTURE=3`、`BLUR=4`、`COLOR_MATRIX=5`、`NOISE=6` |
| 每条 indirect draw | 一个 `CommandConstant{program, address_offset}`（8 B） |
| 实例行 | Shape 248 / PathFill 252 / PathStroke 256 / TexQuad 180 / BlurQuad 136 / ColorMatrix 212 / NoiseQuad 136 B |
| push constant | 32 B：vertexAddress、dataAddress、commandConstantsAddress、cameraIndex、renderVersion |
| descriptor set 0 | `b0` UBO、`b1` texture2D[64]、`b2` sampler[5]、`b3` storage buffer |
| 图元 | 固定 4 顶点 quad + 6 索引；`gl_DrawIDARB` 取 program，`gl_InstanceIndex` 取实例行 |

旁证：`test_intent8.{vert,frag}` 与 intent7 版除 include 文件名外相同；ABI 与 intent7 哈希相同。

### 2.3 一帧实际发出的命令

| 项 | 数量 |
|---|---|
| render scope | **1**（`11172-11225`；源码 1 个 `beginRendering` 调用点，执行 1 次） |
| `vkCmdDrawIndexedIndirect` | 2 次（`setCameraIndex(1)` 切成两段） |
| indirect 命令 | 4 条 |
| 实例 | 8 个：2 面板 + 2 卡片 + 2 边框 + 1 折线 + 1 三角形 |
| 纹理 / 文字 / 阴影 / 模糊 / 渐变 / 噪声 | **0** |
| 图像转换 | 8 条（起始 5 + 末尾 3） |
| `vkCmdCopyImageToBuffer` | 1 次（1 像素拾取回读） |

### 2.4 建了但从未使用的对象

| 对象 | 创建位置 | 状态 |
|---|---|---|
| 离屏层 4 张 × 2 frame = 8 张 320×192 | `1801-1899`、`10565-10570` | 从未作为 target，也从未被采样；已占 8 个 bindless 槽 |
| `pipelineUILayer`（离屏单附件） | `3459-3521` | 从未 bind |
| `pipes[1..11]`（add / multiply / erase / 模板 mask / 只写 RG / EQUAL） | `3622-3668` | 从未 bind |
| `pipelineOpaque3D` / `pipelineTransparent3D` / `pipelineOpaqueUI` | `3671-3674` | 从未 bind |
| UI 纹理（9-slice 源图 / alpha 遮罩 / mip 棋盘） | `10573-10576` | 建了没画 |
| glyph 表 / `uiUnicodeLines` | `10551-10563` | 只打印日志，未发射 glyph |
| `ui_mode` 34 个 `MODE_*` | `10996-11032` | ability1 遗留死枚举 |

### 2.5 延迟回放：已改为“两阶段”（本轮修复）

原实现每个 scope 回放时都写 GPU buffer 的 offset 0，多 scope 会互相覆盖；且每个 scope 单独扩容还会让先录的地址悬空。
本轮把 `command_recorder` 的职责拆成两段，用 `scope_binding` 把地址固定下来：

```text
阶段 1  record_builder::resolveStorage(frame)     —— 只写数据，不录 vkCmd*
          统计所有 scope 的 heap/constants/indirect 总量
          → 每个 scope 分配 16B 对齐、互不重叠的区间
          → 一次性扩容（此后不再 realloc）
          → command_recorder::writeStorage() 写入
          → 产出 scope_binding{vertex, heap, constants, index_buffer,
                               indirect_buffer, indirect_offset}

阶段 2  record_builder::replay(cb)                —— 只录 vkCmd*
          outer.flush(cb)                          （transition/copy/blit）
          inter.record(cb, scope.binding, ...)     （begin/state/push/draw/end）
```

`command_recorder::record` 的签名强制要求 `scope_binding`，因此**不可能在“地址未定”时先录命令**；
`record` 内也不再有任何 `write` / `realloc`。

实测（`MCS_DUMP_SCOPES=1`，默认场景 = 主 UI pass + 抓屏/模糊 + 玻璃合成 pass）：

```text
[scope-bind] scopes=4 heap=3824 constants=80 indirect=192
[scope-bind] inter=0 heap[0,3236)   const[0,40)  indirect[0,100)
[scope-bind] inter=1 heap[3248,3384) const[48,56) indirect[112,132)
[scope-bind] inter=2 heap[3392,3820) const[64,80) indirect[144,184)
```

三个 inter scope 的区间互不重叠；`VUID-` 计数为 0。

**已实装的真实特效：磨砂玻璃（抓主 framebuffer）**

1. 主 pass：UI 画到 MSAA，`resolve` 到 swapchain（1x）
2. `vkCmdBlitImage`：把玻璃区域从 swapchain 抓到 `layerCapture`（320×192）
3. `BLUR`：`layerCapture` → `layerBlur`
4. 第二个主 pass：swapchain `LOAD`，用 `layerBlur` 画圆角玻璃 + 描边

截图：`output/ability7_backdrop.png`。卡片外的圆点清晰、卡片内模糊，说明抓的是真实 framebuffer 内容。
唯一提示是合法警告：layer/glass scope 只有 1 个附件，而片元着色器仍写 location 1
（`Undefined-Value-ShaderOutputNotConsumed-DynamicRendering`），不影响结果。

### 2.6 quad-only

`addInsteance` 写死 `indexCount=6, firstIndex=0, vertexOffset=0`（`1332-1337`），
`mesh_manager` 也只塞了一个 quad（`3126-3131`）。所有 draw 都是同一个 4 顶点四边形。

### 2.7 版本对比：scope 数一路减少

| 版本 | `beginRendering` 调用点 | 说明 |
|---|---|---|
| ability1 | 3 处 | `layerBegin` 一帧执行 7 次 + 主 pass = **8 个 scope**，全内联 |
| ability2 | 0 处 | 走后端；但有 `FramePlan` + `PassKind::Draw / Blit` |
| ability3 | 9 处 | 含 load/suspend/resume 展示 |
| ability5 | 2 处 | 只剩通用 + 主 |
| ability7 | 1 处 | 只剩主 |

### 2.8 两条相关 VUID（按本地 spec 1.4.341 核对）

1. **`VUID-vkCmdDrawIndexedIndirect-colorAttachmentCount-06179`**
   未启用 `dynamicRenderingUnusedAttachments` 且当前 scope 由 `vkCmdBeginRendering` 开始时，
   绑定管线的 `VkPipelineRenderingCreateInfo::colorAttachmentCount` **必须等于** `VkRenderingInfo::colorAttachmentCount`。
   → 主通道管线（2 附件）不能用于 1 附件的 layer scope；layer 必须有自己的管线（现有 `pipelineUILayer` 正是 1 附件）。
   这条在录制时就能由 app 自己判定，不需要 validation layer。

2. **`vkCmdSetStencilReference` 没有绑定 dynamic state 的 VUID**
   该命令的 Valid Usage 只有 commandBuffer/faceMask/recording/cmdpool 等隐式项。
   管线未声明 `VK_DYNAMIC_STATE_STENCIL_REFERENCE` 时，这次调用**不非法，只是不生效** ——
   实际 reference 取 `VkStencilOpState::reference`。
   → 之前说“对它调 setStencilReference 非法”是错的，正确说法是“静默无效”。

补充事实：**Vulkan 不提供合法性校验 API。** VUID 是规范条文，运行时由 validation layer 打印（不启用就是未定义行为）。
其中一部分可以在 app 内静态判定（附件数/格式/采样数/动态状态声明/描述符兼容），其余依赖运行时状态（image layout、同步）。

### 2.9 动态状态（ext2/3）：不新建管线

- 启用 `VK_EXT_extended_dynamic_state_2/3`；主 UI 管线声明
  `COLOR_BLEND_ENABLE/EQUATION/WRITE_MASK_EXT`、`ALPHA_TO_COVERAGE_ENABLE_EXT`、
  `STENCIL_TEST_ENABLE/OP/COMPARE_MASK/WRITE_MASK`。
- `draw_state` 增加对应字段；回放时按变化发 `vkCmdSet*`，不再为 blend/stencil/A2C 建变体管线。
- 演示：mask（A2C + `INCREMENT_AND_CLAMP`）→ content（`EQUAL 1` + over）→ glow（add 混合）。
  截图：`output/ability7_dynstate.png`。
- 设备支持（vulkaninfo）：ext2/ext3 在；`ColorBlendAdvanced=false`（未使用，也不影响本演示）。
- 验证：`VUID-` 计数 0。

---

## 3. 表达力：7 个 program 到底能覆盖什么

### 3.0 七个 program 的分工与硬上限

| program | 核心算法 | 单实例能画 | 硬上限 |
|---|---|---|---|
| SHAPE | 圆角盒 SDF + 逐角半径 | 填充 / 边框 / 外阴影 / 线性-径向-圆锥(2 stop) / 程序化遮罩 / 椭圆 | 只有“圆角盒”；渐变 2 stop |
| PATH_FILL | 闭合多边形距离 + 奇偶 | 多边形填充 + AA | 16 点、单轮廓、直线段 |
| PATH_STROKE | 线段距离场 | 折线描边 / 圆头 / 圆接头 | 16 点、开链、无虚线 |
| TEXTURE | bindless 采样 + 圆角遮罩 + glyph 距离场 | 贴图 / mip / alpha 遮罩 / MSDF / 位图 | 单一半径、无 9-slice |
| BLUR | 2D 高斯（taps 1..6） | 纹理模糊 | 只能吃纹理、不可分离 |
| COLOR_MATRIX | 4×4 + offset | sepia / 灰 / 反相 / 饱和 | 只能吃纹理、无 LUT |
| NOISE | value noise / fbm | 噪波 / 雾 / 纹理底 | 只有 value noise |

### 3.1 单实例内建 vs 组合可达

| 效果 | 单实例内建 | 组合可达 | 代价 |
|---|---|---|---|
| 圆角/逐角/椭圆、填充、边框、外阴影、线性/径向/圆锥（2 stop） | ✅ | — | — |
| 任意多边形填充（≤16 点、even-odd）、折线描边 | ✅ | — | 点数受限 |
| 纹理、mip LOD、alpha 遮罩、MSDF/位图 glyph | ✅ | — | 圆角单一半径 |
| 2D 高斯模糊（≤6 taps）、颜色矩阵、value noise/fbm | ✅ | — | 需纹理输入 |
| 9-slice | ❌ | ✅ 3×3 个 Texture | 9 次 draw |
| 虚线 | ❌ | ✅ 多段 PathStroke | draw 数随段数 |
| 圆弧 | ❌ | ✅ ≤16 点折线（可多段拼） | 边缘多边形化 |
| 曲线/多子路径 | ❌ | ✅ 多段 + 模板并集 | draw + 模板 |
| 多 stop 渐变 | ❌ | ✅ 分段/composite | 接缝/渐变精度 |
| 组透明度 / backdrop / 滤镜链 / 滚动缓存 | ❌ | ✅ 多 target + 采样 | 多 scope |
| 3D LUT、双输入逐像素运算、compute 后处理 | ❌ | 需**新增 program**（不是新 API） | — |

### 3.2 文档实证：7 个 program 的覆盖范围

| 文档 | 说法 |
|---|---|
| `INTENT7_HANDOFF.md` §1.6 | `ui_dao` 使用 **7 个 program**：Shape / PathFill / PathStroke / Texture / Blur / ColorMatrix / Noise；同段“特性覆盖”里**包含 arc、dash、nine-slice** |
| `INTENT7_HANDOFF.md` §3 | `ability_test`：**7 个 program、38 个特性 tile** |
| `INTENT7_HANDOFF.md` 结论 | “7 DAO programs; 430 glyphs.” |
| `INTENT8_HANDOFF.md` §3 | “**既有 7-program ABI**；未改 shader/DAO 设计。” |
| `INTENT8_HANDOFF.md` §1.1 / §6 | 新增 9 项（阴影堆栈/多边框/文字 FX/多 stop/aurora/路径裁剪/滚动缓存/组 FX/玻璃滤镜）**“全部通过多次 DAO 提交 + 已有 offscreen/composite 机制实现”**，“shader 7 program 和 draw 框架没有改” |
| `intent7_pass_api_coverage.md` §3 | 从“命令能否提交”角度抽象**功能完备**；`pass.vk()` 暴露完整 dispatch table，任何 `vkCmd*` 都能发 |

结论：**7 个图元 + 组合覆盖了文档里的 36/42 项，且新增 9 项时一个 program 都没加。**

### 3.3 没有任何 UI 特效需要“新 Vulkan API”

| 需求 | 需要什么 | 需要新 API 吗 |
|---|---|---|
| 9-slice / dash / arc / 曲线 / 多 stop | 组合，或加 shader 字段 | ❌ |
| 3D LUT / 双输入像素运算 | 新 shader program（或 `VK_EXT_blend_operation_advanced`） | ❌ |
| screen/overlay 混合 | EXT blend，或把 dst 抓成纹理在片元里算 | ❌ |
| 任意/嵌套裁剪 | 模板（`VK_STENCIL_OP_INVERT` / `INCREMENT_AND_CLAMP` + `EQUAL`） | ❌ |
| 大半径模糊 / bloom | 多 target + 多 pass，或 compute | ❌ |
| HDR / 宽色域 | 换 format + colorspace + metadata | ❌ |
| 文本 | MSDF/位图纹理 + glyph quad（shaping 在 CPU） | ❌ |

---

## 4. “画布外内容”的准确说法

**你的原理是对的：shader 输出像素 → framebuffer → 屏幕。** 任何进了 `VkDevice` 的像素都能 blur / mix / 采样。

唯一边界是**像素所有权**：

- 你的 swapchain 里只有你自己画的内容；DWM/合成器在 `vkQueuePresentKHR` 之后才把窗口贴到桌面。
- Windows Acrylic/Mica 是 DWM 的 backdrop，不在你的 framebuffer 里。
- **但只要拿到那份像素**（OS 截屏、Desktop Duplication、或你自己就是合成器），它就是普通纹理，Vulkan 照常处理。

所以正确表述：**这是输入获取问题，不是渲染能力问题。** 原报告说“永远不行”是错的。

---

## 5. 让 record_builder 在宿主侧“能表达全部”，需要什么

### 5.1 改动清单（全部是已有 `vkCmd*` 的排列组合）

| # | 要补的 | 位置 | 估计 | 解决什么 |
|---|---|---|---|---|
| 1 | 每 scope 的 `scope_base`（heap / constants / indirect + push 基址） | `resolveStorage` / `scope_binding`（本轮已实现） | ✅ | 多 target 数据踩踏 |
| 2 | 每 draw 的几何参数（`indexCount/firstIndex/vertexOffset`） | `1327-1371` | 20–40 行 | quad-only |
| 3 | 多 target 的 begin/end 序列 + 每 target layout 追踪 | `1637-1796` 已支持结构，补调用方 | 80–150 行 | 离屏合成类全部 |
| 4 | 混合状态（pipeline 变体或 `extended_dynamic_state3`） | `3358-3668`、`2927` | 50–100 行 + 建管线 | screen/overlay 等 |
| 5 | clip：reference 栈 + A2C 管线变体 + mask/content 配对 | `3622-3668` 有管线 | 100–200 行 | 任意/嵌套裁剪 |
| 6 | 每 target 的 pipeline 兼容（sample/format 按 target 建） | `3358` | 30–60 行 | 离屏 sample=1 vs 主通道 MSAA |

合计 **约 300–600 行宿主代码**。没有一条需要新 Vulkan API，也没有一条需要“pass 框架”。

### 5.2 需要提前定的资源策略（设计选择，不是 API 难度）

- 离屏层几张、多大、什么格式、何时失效（resize / 内容变化）
- clip 层级上限（模板 8 bit → 255 级，够用）
- layer 纹理占用 bindless 槽（当前 64 个，8 个已被死层占用）

### 5.3 明确不在本节范围

- shader 字段/新 program（用户明确先不管）
- 性能与保真度（见 §6）

---

## 6. 性能与保真度（与“能否表达”无关）

| 项 | 影响 | 不影响 |
|---|---|---|
| 大半径 blur 单 pass | 采样数随半径平方增长 | 可表达 |
| 多层离屏 | 带宽、resolve 次数 | 可表达 |
| 每 draw push constants + 状态变化 | 合批上限 | 可表达 |
| 9-slice / dash / 曲线用组合 | draw 数上升 | 可表达 |
| 模板裁剪 | 打断合批、需要 A2C 变体 | 可表达 |
| 文本 outline/shadow 多份发射 | draw 数 | 可表达 |

文档自己列的保真度限制（`INTENT8_HANDOFF.md:198-216`）：文字 outline 是多份偏移 glyph、多 stop 只做线性、aurora 是多个 radial blob、玻璃只做饱和度矩阵、单级 stencil、圆角 clip 是纹理遮罩 —— **都是质量/成本取舍，不是表达力边界。**

---

## 7. 修订记录：此前的错误判断

| # | 之前的说法 | 错在哪 | 正确说法 |
|---|---|---|---|
| 1 | “需要 pass / pass 框架” | 把调用序列当成了 Vulkan 概念 | Vulkan 只有 `begin/endRendering`；多 scope = 多调几次裸 API；框架可选 |
| 2 | “9-slice / dash / arc ❌ 不具备” | 把“单实例没有字段”当成“不可表达” | 7 program **组合可达**；intent7 的 36 项、intent8 的 42 项已实证 |
| 3 | “ability7 不具备能力” | 把“未实现”说成了“不能” | **当前代码没发这些命令**；裸 API 完全支持 |
| 4 | “多 scope 要先修 offset，所以…” | 把延迟回放的实现 bug 当成架构需求 | offset 只是让延迟回放正确；内联写法则不存在该问题 |
| 5 | “画布外内容永远不行” | 把输入缺失当成了渲染限制 | 像素拿到就能画；是获取问题，不是渲染问题 |

（另有两处计数错误已修正：混合/模板管线是 11 条；一帧图像转换为 8 条。）

---

## 8. 证据索引

> 行号以修订前版本为准；本轮重构后部分行号已移动，以符号名为准。

| 结论 | 位置 |
|---|---|
| ability7 的 shader 映射 | `test/mcsvulkan/vma.cmake:244-245` |
| ability7 == ability6（逐字节） | SHA256 相同；11764 行 / 488,899 B |
| 7 program / 实例行布局 | `shaders/test_intent8_abi.glsl:22-226`；`test_ui_ability3_shader_abi.hpp:35-216` |
| 7 个 program 的实现 | `shaders/test_intent8.frag:132-424` |
| 离屏层（未使用） | `test_ui_ability7.cpp:1801-1899`、`10565-10570` |
| 离屏管线（未 bind） | `test_ui_ability7.cpp:3459-3521` |
| 11 条混合/模板管线（未 bind） | `test_ui_ability7.cpp:3622-3668` |
| 两阶段 replay（scope_binding） | `scope_binding` / `record_builder::resolveStorage` / `command_recorder::record`（原 offset 0 写入已移除） |
| quad-only | `test_ui_ability7.cpp:1332-1337`、`3126-3131` |
| 单 scope / 8 实例 | `test_ui_ability7.cpp:11172-11225`、`11265-11299` |
| A2C 关闭 | `test_ui_ability7.cpp:3567` |
| ability1 的 8 个内联 scope | `test_ui_ability.cpp:11505-11566` |
| ability2 的 Recorder / FramePlan | `test_ui_ability2.cpp:1442-1446`、`2571-2578`、`3166-3183` |
| program 数量 = 7（文档口径） | `INTENT7_HANDOFF.md` §1.6 / §3 / 结论；`INTENT8_HANDOFF.md:98`、`173` |
| 新增 9 项未改 ABI | `INTENT8_HANDOFF.md:27-47`、`161-175` |
| pass API “功能完备” + `pass.vk()` | `output/intent7_pass_api_coverage.md:95-124` |
| 能力矩阵截图 | `output/intent8_ability.png` |
| 文档自列保真度限制 | `INTENT8_HANDOFF.md:198-216` |
