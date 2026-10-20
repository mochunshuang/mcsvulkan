# 03 · 角色与上下游

工厂视角。**编译期是开工前的工艺准备，运行期每一帧走一次产线。**

---

## 开工前（编译期，一次性）

```
你写的树：  all_of<one_of<refines<click,dbl>, drag>, key_A>
                │
                │ ① 工艺科 plan()        每个组合子把自己的 footprint 与孩子的相加
                ▼
        kinds[] + relations[]            纯数据，只在 consteval 里存在
                │
                │ ② 质检科 validate()     跨源？重名？既细化又互斥？成环？
                ▼
        合法的图（不合法就是编译错误，不是断言）
                │
                │ ③ 布局科 cells_of()     同源内求连通分量 + 拓扑出“最具体先”的序
                ▼
        cells[]                          每个 cell 要么一张裁决表，要么一个直呼
                │
                │ ④ 开模
                ▼
        stage<cell> × N                  运行期状态，一个 kind 一个槽
```

## 生产中（每一帧）

```
frame(t) ──► ① leaf.step ×N ──► ② stage.on ×N ──► ③ stage.settle ×cell ──► ④ 播报 + 复位
              各写各的 FSM         登记“我想赢”        按“最具体先”裁决        唯一出口
```

## 工位表

| # | 工位 | 输入 | 输出 | 谁实现 | 代码 |
|---|---|---|---|---|---|
| ① | 工艺科 `plan()` | 组合树 | kinds + relations | 每个组合子一份 | [uhi.hpp:120](v1/src/uhi.hpp#L120-L123) 等 5 处 |
| ② | 质检科 `validate()` | 图 | 通过 / 编译错误 | 库 | [uhi.hpp:263](v1/src/uhi.hpp#L263-L313) |
| ③ | 布局科 `cells_of()` | 图 | cell 划分 + 裁决顺序 | 库 | [uhi.hpp:318](v1/src/uhi.hpp#L318-L373) |
| ④ | 开模 `stage<Tree,Cell>` | cells | 每 cell 一张存活表 | 库（模板实例化） | [uhi.hpp:473](v1/src/uhi.hpp#L473-L563) |
| ⑤ | 车间 `leaf.step` | frame + now | `step_t` 四选一 | **你**（唯一必须写的） | [uhi.hpp:126](v1/src/uhi.hpp#L126-L129) |
| ⑥ | 登记处 `stage.on<G>` | 一个 `step_t` | 该 kind 的 alive/want | 库 | [uhi.hpp:489](v1/src/uhi.hpp#L489-L519) |
| ⑦ | 裁决台 `stage.settle` | 本 cell 的 want | verdict | 库 | [uhi.hpp:521](v1/src/uhi.hpp#L521-L567) |
| ⑧ | 播报台 `record` | (kind, verdict) | 回调 sink + 记录 + 复位叶子 | 库 | [uhi.hpp:664](v1/src/uhi.hpp#L664-L683) |
| ⑨ | 总装 `feed` | frame（自带时间） | 走完 ⑤⑥⑦⑧ | 库 | [uhi.hpp:663](v1/src/uhi.hpp#L663-L683) |
| ⑩ | 接缝 `ui::bind` | arena | 两字函数指针 | 库 | [uhi.hpp:653](v1/src/uhi.hpp#L653-L659) |

**⑨ 就是主线本身**，二十行。其余全是开工前的准备，或你写的车间。
⑩ 是接缝：`bind` 把 `{&arena, thunk}` 存进 sink（延迟绑定）。
它**不是**输入入口——库里唯一的输入动词是 `feed`。

**⑨ 就是主线本身**，二十行。其余全是开工前的准备，或你写的车间。

## 传送带纪律（谁能碰谁）

| 上游 | 下游 | 允许的动作 |
|---|---|---|
| frame | leaf.step | 只读 |
| leaf.step | stage.on | **只写自己的 FSM**，只交出 `step_t` |
| stage.on | 本 cell 的槽 | 只写本 kind 的 alive / want |
| stage.settle | plan（编译期常量） | 只读；只产出 verdict |
| verdict | 播报台 | **唯一能回头复位 leaf 的地方** |
| cell A | cell B | **互相看不见** |

一句话：**下游不反向读上游，同级从不相邻。**

## 一帧的时间线（S4 那场戏，逐工位）

```
t=0    press
  ⑤ click=active  dbl=active  drag=active  key=idle
  ⑥ mouse 三个槽 alive；keyboard 空

t=10   key
  ⑤ key=done（其余 leaf 看到 key 帧，各自维持 active）
  ⑥ keyboard: want=1
  ⑦ keyboard cell：无人挡 → accepted
  ⑧ 播报 key_A:acc → 复位 key 叶子
  ★ mouse cell 全程没被惊动（L2）

t=20   release
  ⑤ click=done  dbl=active  drag=aborted
  ⑦ 顺序 [dbl, drag, click]：dbl want=0；drag want=2 → 静默关闭；click want=1 → 被 dbl 挡 → deferred
  ⑧ 零播报

t=320  none（时间推进）
  ⑤ dbl=aborted（窗口到期）
  ⑦ dbl 静默关闭 → 再扫 deferred：click 不再被挡 → accepted
  ⑧ 播报 click:acc → 复位 click 叶子
```
