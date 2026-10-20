# 04 · 设计空间：可能、不可能、已否决

这一章是全书最重要的一页。它记录**为什么现在是这个样子**，
以及哪些路走不通——这样不会有人（包括我）再走一遍。

---

## 一、已做的十个决定

| # | 决策点 | 走过的（已否决） | 现在 | 否决的证据 |
|---|---|---|---|---|
| 1 | 关系从哪来 | 人写 `await`/`cancels` 数组 | 组合子生成 | 手写漏了 `(dbl, drag)` 一条边，4000 步随机输入到第 107 步才发现 |
| 2 | 冲突单元 | 全局一个裁决器 | cell（同源×分量） | 键鼠互相排队；一个 domain 只能一个 `serial` 开关，表达不了“同源但无关” |
| 3 | 判决时机 | 当场回调 | 本帧只登记，`settle` 统一结算 | 被判决者会在同一帧复位后重新开张（同一帧出现 `superseded` + `accepted`） |
| 4 | 判决种类 | accepted / rejected / superseded | accepted / superseded | 每次按下不拖都冒出 `drag:rejected`——自己放弃不是判决 |
| 5 | 状态表示 | handle + 槽位 + 代际 | 一 kind 一槽 | L6 成立后 handle 全是冗余；v1 的 `stage` 比 v0 的 `referee` 短一半 |
| 6 | 判定顺序 | 扇出顺序（谁先 publish） | 编译期拓扑（最具体先） | 边界帧结果依赖注册顺序：`[click]` vs `[click,click]` |
| 7 | 识别器归属 | 外部对象 + 类型擦除 join | 树里的节点（值） | 擦除丢了 `self`（handler 摸不到 arena 数据）和 footprint（无法再组合） |
| 8 | 组合子 | 只有 `seq` / `within`（时序） | 加 `all_of`/`one_of`/`refines`（结构） | 时序组合子表达不了“谁和谁竞争”，只能退回手写策略 |
| 9 | 图检查 | 运行期计数 + fuzz | `consteval` + `static_assert` | 漏声明只能事后发现，不能禁止 |
| 10 | 环检测 | 当作核心防线 | 保留，但知道它是死代码 | 树里每个 leaf 只有一个父亲，有向边天然无环——**构造不出环** |
| 11 | 胜利回调 | 回调进库里的 trace 数组 | 两字函数指针 `{&arena, thunk}`，延迟绑定到 `make_aggregate` 的 `self` | 回调停在库里 = 与 arena 脱节；handler 改不了 `self` 的字段 |
| 12 | `ui` 的归属 | 库外的独立对象，`ui.bind(arena)` 从外面绑 | **`ui` 是 arena 的字段**，`arena.bind()` 里 `self.ui.bind(self)` | 外部对象要用户自己管两份生命周期；作为字段时 `self` 天然就是 arena，且 arena 可以整体拷贝/移动（`ui` 不含自引用） |
| 13 | 入口的类型集合 | 想接多种事件类型 | 只接 `frame_like` 的帧；主模板空壳 ⇒ 不认识的类型被 `requires` 裁剪 | 只声明不定义会让探测变硬错误，上层无法"丢弃不能投递的类型" |
| 14 | 投递链要不要库配合 | 让 uhi 自己订阅分发器 | **不**：arena 里三行（build_change / feed / apply_change），uhi 不认识 dispatcher 也不认识 proxy | v2 的库与 v1 逐字节相同——投递链通不通过，跟库无关 |
| 15 | 一帧内的结算顺序 | 按 cell 顺序混着处理"想赢"和"放弃" | **先处理放弃，再处理想赢** | release 那一帧 drag 已经自己放弃，却被随后判赢的 dbl 记成 `superseded` ⇒ 上层收到一次假回滚。v2 的语义表测试抓到的，改完 v1 的 S3a 也少了一条假事件 |
| 16 | 未映射的鼠标按钮 | `map_button` 返回 `eUNDEFINED`(0xFF)，`apply_change` 直接拿它当下标 | **丢弃**：`if (button == eUNDEFINED) break;` | GLFW 真会送 4..8 号侧键。原来是 `ctx.buttons[0xFF] = …` 越界写，去掉保护立刻 `0xC0000409` 崩溃。翻译矩阵（122 keys × 4 + 13 buttons × 4）抓到 |
| 17 | 兜底时限 | 没有：卡死的假设永久占槽 | `leaf<…, HoldMs>` + 超时静默判负，并且**复位识别器** | 只清槽位不清 FSM，识别器还以为按着，下一次用的是旧载荷（`last_x=5` 而不是 70）。`test_multitouch` t3 抓到的 |
| 19 | 组合子拼接子计划 | 直接把子计划的 relations 并进来 | **平移下标**（`offset_rels`） | 子计划的关系是 0 基的，右操作数并进来后会指到别人的 kind：`one_of<A, one_of<long,drag>>` 把 `long~drag` 变成了 `click~dbl`，而 `long~drag` 消失。只有把事件目录做宽才会暴露——之前所有树都只有左子树带关系 |
| 20 | 三级细化链 | click 只等 dbl（直接关系） | **awaits 的传递闭包**：click 一起等 triple | 否则 dbl 一超时就放 click 落地，紧接着来的第三次点击就永远解释不了。`test_catalogue` 的 click→dbl→triple 逼出来的 |
| 18 | 多指怎么放 | 把指针 id 塞进 stage/leaf，一张表按 id 分裂 | **每指一台独立机器** `pool<Tree,N>`；帧用 `frame_traits::who` 报 id | 两根手指不是"谁赢"而是"各算各的"：各自的状态/槽位/记录。`ui` 本来就是一台机器，池化反而不需要动 stage 一行 |


第 10 条特别值得记：我原以为环检测是核心，写反例时才证明它不可达。
真正提供保证的是“树”这个结构，不是检查。

---

## 二、不可能的分支（别再试）

| 不可能 | 为什么 |
|---|---|
| 类型擦除之后还能算 footprint | 擦除丢掉的就是结构；没有结构就没有组合 |
| 跨 source 的关系 | 两个通道的假设永不相遇，关系没有可观测含义 |
| 同帧内“让被判决者看见判决”且“顺序无关” | 二选一：延迟到帧末（改语义）或接受顺序依赖（L4 的边界） |
| 一个 leaf 产出多个 kind，同时保留“一 kind 一槽” | 两条申请共用一个槽 = 丢身份 |
| 用一棵树同时表达时序组合和冲突关系 | 两种语义压在同一组边上，图不再是树，`plan()` 无法定义 |

---

## 三、尚未决定的分支（要拍板或做实验）

| # | 分支 | 选项 | 结果 |
|---|---|---|---|
| A | 载荷怎么带 | ① `kind<Name, Payload>`（类型参数）<br>② claim 带 `void*` + tag（擦除）<br>③ `claim<P>` 泛型 | **已定为 ①**（v2）：载荷在类型里，`on_<kind>` 的签名自动跟着变 |
| B | `on_<kind>` 怎么来 | ① `define_aggregate` 生成 method<br>② 手写 + 编译期检查 | **已定为 ②**：`invoke<"on_<kind>">` 找不到就编译失败，等于免费检查；生成留到需要时 |
| C | liveness | ① 不做，靠 FSM 自己的窗口<br>② `stage` 带 `hold` 兜底 | 未定（v3） |
| D | 多指 | ① source 带运行期 id<br>② 编译期展开 pointer 0..N | 未定（v3） |

我倾向：A①、B①、C②、D①。理由是它们都保持“结构在类型层”，
不把任何身份信息推迟到运行期。但这一轮不动手，留到你确认。

---

## 四、历史（免得忘了走过的路）

| 版本 | 做了什么 | 为什么被取代 |
|---|---|---|
| **v0.1** | claim 作为值 + 全局 referee + 帧末 mailbox + 类型擦除通道 + 4000 步 fuzz | 图靠手写（漏边）、handler 丢 `self`、`commit` 靠人记得调 |
| **v0.2** | 想过 `make_gestures` + `define_aggregate` 直接生成整个 arena | 只到草图，没落地；当时还没有 cell 的概念 |
| **v1** | 树 + footprint + 同态 + cell + stage；删掉擦除/join/mailbox | 现在这版 |

从 v0 到 v1 真正被删掉的东西：`sink_erased`、`submitter`、`bind`、`join`、
`registry`、`hub`、`void*`、`handle`、`gen`、`commit` 的人为调用点。
**删掉它们不是因为不好看，是因为它们让 footprint 无法计算。**
