// repro.cpp —— 复现"同一帧流有两个 cell 时，只结算第一个"
// 完全照抄 uhi.cpp 里 do_feed 的递归结构，只是把 FSM 换成计数器。
#include <cstdio>
#include <string_view>

// 两个 cell，**同一个帧流类型**（mouse_frame），互不相连（all_of 的典型情形）
constexpr int NC = 2;
constexpr const char *FRAME[NC] = {"mouse_frame", "mouse_frame"};
// 把"帧名字"做成一个带静态成员的标签类型，绕开字符串字面量不能当 NTTP 的限制
struct tag_mouse
{
    static constexpr const char *name = "mouse_frame";
};
struct tag_kb
{
    static constexpr const char *name = "kb_frame";
};
inline int settle_count[NC] = {0, 0}; // 每格被 settle 了几次

// ── 你现在的写法 ──────────────────────────────────────────
template <class Tag, std::size_t First, std::size_t... Rest>
void do_feed_buggy(int &result)
{
    if constexpr (std::string_view{Tag::name} == FRAME[First])   // ← 命中就不再往下走
    {
        ++settle_count[First];
        result = 1;                       // ← 赋值，不是累加
    }
    else if constexpr (sizeof...(Rest) > 0)
    {
        do_feed_buggy<Tag, Rest...>(result);
    }
}

// ── 修好之后 ──────────────────────────────────────────────
inline int settle_count2[NC] = {0, 0};
template <class Tag, std::size_t First, std::size_t... Rest>
void do_feed_fixed(int &result)
{
    if constexpr (std::string_view{Tag::name} == FRAME[First])
    {
        ++settle_count2[First];
        result += 1; // ← 累加
    }
    if constexpr (sizeof...(Rest) > 0) // ← 无条件继续，不是 else if
    {
        do_feed_fixed<Tag, Rest...>(result);
    }
}

int main()
{
    int r1 = 0, r2 = 0;
    do_feed_buggy<tag_mouse, 0, 1>(r1);
    do_feed_fixed<tag_mouse, 0, 1>(r2);

    std::printf("树：all_of< hover(mouse), wheel(mouse) >   —— 两个 cell，同一帧流\n\n");
    std::printf("  你的写法  : cell0 settled %d 次, cell1 settled %d 次, 返回 %d 个判决\n",
                settle_count[0], settle_count[1], r1);
    std::printf("  修好之后  : cell0 settled %d 次, cell1 settled %d 次, 返回 %d 个判决\n",
                settle_count2[0], settle_count2[1], r2);
    std::printf("\n  结论：cell1 的 FSM 永远没人调 step，它的 on_wheel 永远不会触发。\n");
    return 0;
}
