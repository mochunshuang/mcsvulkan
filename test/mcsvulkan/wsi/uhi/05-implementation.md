# 05 · 实现映射

概念落到哪个符号、哪一行。**读这一章是为了能自己改。**

---

## 用户会碰到的名字

| 名字 | 干什么 | 行 |
|---|---|---|
| `kind_t<"click">` | 语义事件身份 | 55 |
| `source_t<"mouse.left">` | 物理通道身份 | 60 |
| `leaf<K,S,Fsm>` | 状态机 + 它的 kind/source | 113 |
| `all_of<…>` | 并行：不加关系 | 155 |
| `one_of<…>` | 互斥：加对称边 | 162 |
| `refines<a,b>` | 细化：加有向边 | 185 |
| `times<d,r>` | 时窗：只改时序 | 201 |
| `uhi::ui<Tree>` | 装配（≈ connect） | 703 |
| `ui.bind(arena)` | **延迟绑定**：每个 leaf 填一个 `{&arena, thunk}` | 675 |
| `ui.feed(frame)` | **唯一输入动词**：推进一帧（时间读自 `frame_traits`） | 689 |
| `uhi::frame_traits<Frame>::when` | 你告诉库“这一帧是什么时候” | 587 |
| `ui.events()` / `uhi::to_string(…)` | 观测：走过的事件 / 一行字 | 715 / 738 |
| `kind_t<Name, Payload>` | 语义事件身份 + 载荷 | 67 |
| `no_payload` | 无载荷 kind 的默认参数 | 62 |

## 绑定：uhi 与 make_aggregate 的接缝

```cpp
// 载荷存在 FSM 里（约定：FSM 有 payload 成员），leaf 只交出地址
[[nodiscard]] const void *payload() const noexcept;                         // 126 附近

// 两字：{arena, thunk}。无捕获，所以可以延迟绑定、可以拷贝
struct sink { void *self; void (*fn)(void *, const void *) noexcept; };     // 615

template <class Arena, class Leaf>
void on_leaf(void *self, const void *payload) noexcept                      // 639
{
    using K = typename Leaf::kind;
    using P = typename K::payload_type;
    static constexpr auto name = cat("on_", K::name);     // 编译期拼方法名
    if constexpr (std::is_same_v<P, no_payload>)
        static_cast<Arena *>(self)->template invoke<name>();               // on_key_A(self)
    else
        static_cast<Arena *>(self)->template invoke<name>(                 // on_click(self, e)
            *static_cast<const P *>(payload));
}
```

`bind` 只做一件事：给每个 leaf 填 `{&arena, &on_leaf<Arena, Leaf>}`。
所以 uhi 完全不认识 arena 的类型，也不需要 arena 在构造时就存在。

**代价**：`on_<kind>` 这个名字必须存在，且签名必须和载荷对得上。
加一个 kind 就多一个必须实现的 `method<"on_<kind>">`，漏了或签名不对都编译失败。

## 内部件索引

| 概念 | 符号 | 行 |
|---|---|---|
| footprint 数据形态 | `plan_data` / `kind_desc` / `relation` | 80 / 71 / 74 |
| footprint 合成 | 5 个 `plan()` | 120,158,166,189,206 |
| 检查 | `detail::validate` | 269 |
| cell 划分 + 裁决顺序 | `detail::cells_of` | 324 |
| 裁决单元状态 | `stage` | 457 |
| 登记申请 | `stage::on<G>` | 495 |
| 结算 | `stage::settle` → `stage::win` | 527 / 559 |
| 一帧 | `ui_impl::feed` | 653 |
| 复位 | `ui_impl::reset_kind` | 681 |

## 运行期全部代码（`feed`，约 20 行）

```cpp
template <class Frame>
void feed(const Frame &f)
{
    const time_point now = frame_traits<std::remove_cvref_t<Frame>>::when(f);
    // ① 所有叶子各走一步：此刻没有任何判决可见
    walk_leaves<0>(tree, [&](auto idx, auto &lf) {
        constexpr std::size_t g = decltype(idx)::value;
        constexpr int         c = cell_of_kind<Tree>(g);
        std::get<c>(stages).template on<g>(lf.step(f, now), now);
    });
    // ② 各 cell 结算；③ 回调 + 记录 + 复位
    auto record = [&](std::size_t g, verdict v, time_point b, time_point e) {
        const event ev{plan_kinds_v<Tree>[g].name, v, now, b, e};
        if (v == verdict::accepted) sinks[g](ev);    // ← 两字函数指针
        trace[n_events++] = ev;
        reset_kind(tree, g);
    };
    (std::get<Cs>(stages).settle(record), ...);
}
```

三件事值得注意：
- `g` 与 `c` 都是 `constexpr`，**没有运行期查表**；
- `lf.step` 是模板调用，**没有类型擦除**；唯一的擦除是 `sink` 那两字，
  而且它只在“回调用户”这一处，不在组合里；
- `settle` 用折叠表达式展开，**没有虚函数、没有容器**。

## 现在还没有的

| # | 缺什么 | 影响 | 属于 |
|---|---|---|---|
| 1 | 每 kind 专属载荷 | 现在所有 handler 都收 `const event&` | v2 |
| 2 | `on_<kind>` 参数类型由 kind 决定 | 需要 `kind_t<Name, Payload>` | v2 |
| 3 | 运行期通道（多指） | source 全是编译期常量 | v3 |
| 4 | liveness | 靠各 FSM 自己的窗口 | v3 |
