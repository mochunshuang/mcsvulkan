# v3 —— liveness + 多指

## 一、liveness：两个东西

### ① `ui.tick(now)`：只有时间，没有帧

```cpp
ui.feed(frame);     // 有帧：事件来了
ui.tick(now);       // 只有时间：让所有识别器看一眼时钟
```

`tick` 让每片叶子走一步，但它给的是 `step_t::hold`（"状态不变"）——
识别器想处理超时就自己实现 `tick(now)`：

```cpp
struct dbl_fsm {
    step_t step(const frame &f, time_point now) noexcept { ... }
    step_t tick(time_point now) noexcept {          // 可选
        if (waiting && now >= deadline) return step_t::aborted;
        return step_t::hold;
    }
};
```

于是**不再需要喂 `none` 帧**来让窗口过期。v1 的 S2 用 `none` 帧推进时间，
在 v3 里等价于一次 `tick`。

### ② `hold`：兜底时限

```cpp
leaf<kind_t<"dbl">, mouse_src, dbl_fsm, 600>   // 600ms 还没结论 ⇒ 静默判负
```

规则：**兜底只判负，不发明赢家。**"谁赢"永远是识别器自己说的。
超时的假设会被静默清掉，同时**复位它的识别器**（否则槽位没了、FSM 还以为自己按着）。

## 二、多指：每指一台机器

```cpp
uhi::pool<touch_tree, 4> pool{};      // 4 根手指
pool.bind(arena);                     // 4 台机器绑同一个 arena
pool.feed(frame);                     // 帧自己报 id：frame_traits<F>::who(f)
pool.tick(now);                       // 时间对**所有**通道一起走
pool[2].events()                      // 第 3 根手指的记录
```

```
template <> struct uhi::frame_traits<touch_frame> {
    static uhi::time_point when(const touch_frame &f) noexcept;
    static std::size_t     who(const touch_frame &f) noexcept;   // 可选，缺省 = 0
};
```

**为什么是 N 台机器而不是一张表按 id 分裂？**
两根手指不是"谁赢"，是"各算各的"：它们各自有识别器状态、各自有槽位表、
各自有记录。互斥只发生在**同一根手指内部**。所以正确做法不是把 id 塞进
`stage`，而是让每一指拥有一整套机器——`ui` 本来就是"一台机器"。

## 三、测试

| 用例 | 序列 | 结果 |
|---|---|---|
| t1 三指独立 | 三指同时按住、交错抬起 | `[0] tap:acc@80 [1] tap:acc@60 [2] tap:acc@70`，各自 payload 带自己的 id |
| t2 互斥按指划分 | 0 号划走 + 1 号点 | `[0] tap:sup@50, swipe:acc@50` / `[1] tap:acc@60` |
| t3 卡住不牵连 | 0 号按住不放（hold=500），1 号正常点 | 1 号照常；`tick(600)` 只清 0 号；0 号随后恢复正常 |

liveness（`test_liveness.cpp`）：

| 用例 | 结果 |
|---|---|
| t1 无兜底 | click 永远 deferred（卡死堵死邻居） |
| t2 600ms 兜底 | `click:acc@700`，跨度仍是它自己的 `[10,60]` |
| t3 纯 tick 推进 | `click:acc@360`——60ms 之后**没有任何帧** |

t1/t2 是同一份输入，只差一个 `hold` 参数：没有它永远等，有它 700ms 时落地。

## 四、这一轮抓到的 bug

**兜底清掉槽位，却没清识别器。**

`stage` 静默判负时只把 `alive = false`，没调 `fsm.reset()`。于是识别器还以为
自己按着（`s == true`），下一次按下不会重新开假设，用的是**上一次的载荷**。

修法：`settle` 多收一个 `kill(g)` 回调，所有静默判负（超时、静默放弃）都调用它：

```cpp
auto kill = [&](std::size_t g) { reset_kind(tree, g); };
(std::get<Cs>(stages).settle(record, kill, now), ...);
```

这条是被 `test_multitouch` 的 t3 逼出来的（`last_x[0]` 拿到的是 5 而不是 70）。

> 排查过程也说明一件事：`static_assert(plan_kinds_v<tree>[0].hold_ns == 500'000'000)`
> 这种"探针断言"比打印快得多。它当场指出我测试里**忘了给 tap 设 hold**。

## 六、综合试跑 `test_tour.cpp`（摸底）

一个程序走完全部能力，每一步都 drain 记录 + 审计：

| 段 | 内容 | 观察到的记录 |
|---|---|---|
| P1 | 直呼：键盘那一格没有纠纷 | `key_A:acc@100` |
| P2 | 细化 + tick + 载荷：click 等 dbl，窗口靠 tick 过期 | `click:acc@360`（60ms 之后没有帧），`click_x=10` |
| P3 | 双击：click 让位 | `click:sup@750, dbl:acc@750` |
| P4 | 拖拽：一个赢家同时压掉两个 | `dbl:sup@860, click:sup@860, drag:acc@860` |
| P5 | 类型裁剪 | `window_size` 不掉进记录，`dropped=1` |
| P6 | 多指：两根手指各算各的 | `[0] tap:acc@1050`、`[1] tap:sup@1060, swipe:acc@1060` |
| P7 | 兜底：dbl 卡死 400ms 后被静默判负 | `click:acc@500` |
| P8 | 审计汇总 | 投递 14 / 裁剪 1 / 记录 10 / accepted 6 / 回调 6 / 跨度重叠 0 |

三条不变量每次核对：**同 cell 的 accepted 跨度不重叠**、
**回调次数 == accepted 条数**、**投递次数 == posted**。

```
=== 综合试跑通过：14 项能力全部覆盖 ===
```

## 七、全套回归（8 个程序 × debug/release）

| 程序 | 覆盖 |
|---|---|
| `v3/test_tour` | 综合：14 项能力 |
| `v3/test_liveness` | tick / hold |
| `v3/test_multitouch` | pool：每指一台机器 |
| `v2/test_translation` | GLFW 全部取值 ⇒ ChangeType（2237 项） |
| `v2/test_handwritten` | 4 个手写 arena（无工厂） |
| `v2/test_delivery` | 5 场景 + 语义表 + 穷举 5460 序列 + 随机 10 万 |
| `v1/test_uhi` | 玩具帧四场 |
| `v1/test_uhi_arena` | 真 input_frame 四场 |
| `v1/test_uhi_reject` | 3 个坏结构编译失败 |

## 八、跑

```powershell
cd E:\0_github_project\mcsvulkan\test\mcsvulkan\wsi
g++ -std=c++26 -freflection -O2 uhi\v3\src\test_tour.cpp -o output/test_tour.exe -lstdc++exp
.\output\test_tour.exe
```
## 九、事件目录 `test_catalogue.cpp`

从 arena_event 的输入生成一整本 UI 事件（桌面侧 8 种，全部一次跑出）：

| # | 事件 | 结构里的位置 | 记录 |
|---|---|---|---|
| 1 | `click` | 三级链最抽象 | `click:acc@350`（等 dbl+triple 一起过期） |
| 2 | `dbl` | 链中 | `click:sup@840, dbl:acc@840` |
| 3 | `triple` | 链最具体 | `dbl:sup@1140, click:sup@1140, triple:acc@1140` |
| 4 | `long_press` | 对手 | `long_press:acc@1900`，抬起后不补 click |
| 5 | `drag` | 对手 | `drag:acc@2100`（dist=300） |
| 6 | `right_click` | 独立通道 | `right_click:acc@2350`，左键同时按着也不受影响 |
| 7 | `key_A` | 键盘直呼 | `key_A:acc@2800` |
| 8 | `combo_ctrl_s` | 键盘直呼（读 context 的按键状态） | `combo_ctrl_s:acc@2950` |

结构就三行：

```cpp
using mouse_cell = uhi::one_of<refines<refines<g_click, g_dbl>, g_triple>,
                               uhi::one_of<g_long, g_drag>>;
using key_cell   = uhi::all_of<g_key_a, g_combo>;
using desktop_tree = uhi::all_of<mouse_cell, uhi::all_of<g_rclick, key_cell>>;
```

## 十、事件目录（15 种，全部一次跑出）

```
1  click          click:acc@350
2  dbl            click:sup@840, dbl:acc@840
3  triple         dbl:sup@1140, click:sup@1140, triple:acc@1140
4  long_press     long_press:acc@1900        抬起后不补 click
5  drag           drag:acc@2100              dist=300
6  right_click    right_click:acc@2350       左键同时按着也互不干扰
7  key_A          key_A:acc@2800
8  combo_ctrl_s   combo_ctrl_s:acc@2950      读 context 的按键状态
9  tap            [0] tap:acc@3400           等 dbl_tap 窗口
10 dbl_tap        [0] tap:sup@3640, dbl_tap:acc@3640
11~14 swipe_*     [0] dbl_tap:sup, 其余三个方向 sup, <方向>:acc
15 long_tap       [0] 其余六个 sup, long_tap:acc@5300
```

结构就三行：

```cpp
using mouse_cell = one_of<refines<refines<g_click, g_dbl>, g_triple>,
                          one_of<g_long, g_drag>>;
using key_cell   = all_of<g_key_a, g_combo>;
using touch_cell = one_of<refines<g_tap, g_dtap>, g_sl, g_sr, g_su, g_sd, g_ltap>;
```

## 十一、结构性质测试 `test_plan_props.cpp`（组合爆炸）

不是手挑形状，而是对**所有合法组合**检查性质。

8 条性质：

| # | 内容 | 抓过什么 |
|---|---|---|
| P1 | 关系不出界、不自指 | — |
| **P2** | 同一对不能既 refines 又互斥 | **下标错位 bug**（回退修复后立刻在这里失败） |
| P3 | 互斥对只记一次 | — |
| **P4** | cell 是 kind 集合的**划分**：每个 kind 恰好一格、同格同源 | — |
| P5 | Σ\|members\| == \|kinds\| | — |
| P6 | 细化链传递性 a->b、b->c ⇒ a->c | — |
| P7 | all_of 子树之间无关系 | — |
| **P8** | kind 名字必须唯一 | 组合测试自己暴露的：`refines<X,X>` 造出重名 kind |

形状规模：**74 棵合法树** = 8 棵具名 + 66 棵来自 6 个节点的两两组合
（每对跑 `one_of` / `refines` / `all_of`）。14 组 kind 重叠的组合被跳过——它们不是"遗漏"，而是**编译期就该拒绝**的形状（两棵子树声明同一个 kind，违反"一个事件一个生产者"）。这条由 `test_uhi_reject.cpp` CASE 3 以编译失败的方式覆盖。

**它验证过自己能失败**：把 `offset_rels` 去掉重编译 →

```
FAIL [pair:one_of]: P2 同一对既是细化又互斥
```

## 十二、反例（必要性）

"能触发"不等于"该触发时才对"。目录末尾加了 4 条反例：

| 反例 | 断言 |
|---|---|
| 松开 Ctrl 后只按 S | `combo_ctrl_s` 不增加 |
| 按住移动 10px（< 阈值 30） | `drag` 不增加；抬手后是 `click` |
| 只点一次 | `dbl` 不增加 |
| 触摸只点一次 | `dbl_tap` 不增加 |

第一条真踩到过：P8 里 Ctrl 一直按着没松，所以"只按 S"其实是 Ctrl+S，
`context` 忠实反映了这一点——反例先失败，才说明 context 是对的。

## 十三、P9/P10：两条独立算法必须给出同一个数

`leaf_count_v` 走的是**类型树**（`walk_leaves` 的算法），`plan_kinds_v` 走的是
**plan()**（组合子拼出来的）。两者算的是同一个数，但代码路径完全独立：

```cpp
static_assert(leaf_count_v<Tree> == plan_kinds_v<Tree>.size(), "P9");
static_assert(cell_count_v<Tree> <= leaf_count_v<Tree>, "P10");
```

74 棵树全部成立。任何一边漏算/多算一个 kind，这里立刻断。

## 十四、设计目的判定

| 目的 | 证据 | 判定 |
|---|---|---|
| 从 arena_event 的输入生成高层 UI 事件 | 15 种，一次跑通，含三级链/组合键/四方向/多指 | ✅ |
| 结构由声明决定，裁决由库统一 | 3 行组合子 ⇒ 完整语义；74 棵树性质成立 | ✅ |
| 编译期确定 | 蓝图/关系/cell/sink 全 consteval；3 类坏结构编译失败 | ✅ |
| 与 make_aggregate 的 self 绑定 | 两字函数指针，延迟绑定；手写 arena 4 例 | ✅ |
| 投递链每一跳 | 真 dispatcher + 你们的 proxy；2237 项翻译矩阵 | ✅ |
| 时间与兜底 | `tick` / `hold`；卡死不再永久占槽 | ✅ |
| 多指 | `pool`，每指一台机器，互不牵连 | ✅ |

**还差**（不影响上述判定，但应当记录）：
1. 跨指针手势（双指缩放）——需要跨通道状态，是另一层概念，没做。
2. 三份 `uhi.hpp` 靠 hash 相等维持——迟早要收成一份。
3. 目录里 15 种之后没有继续加（长按拖拽、双击后拖拽、边缘手势…），但**加新 kind 的成本已经降到只写一个 FSM**，这是设计目的达到的直接证据。