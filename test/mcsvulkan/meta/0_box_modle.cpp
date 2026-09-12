#include <cassert>
#include <cmath>
#include <iostream>
#include <exception>
#include "layout_engine.hpp"

// NOLINTBEGIN
// 辅助函数：比较 double 是否相等（处理有限值和无穷大）
bool nearlyEqual(double a, double b)
{
    if (std::isinf(a) || std::isinf(b))
        return a == b;
    return std::abs(a - b) < 1e-9;
}
void expectSize(const Widget *w, double width, double height)
{
    assert(w);
    assert(nearlyEqual(w->size.width, width));
    assert(nearlyEqual(w->size.height, height));
}

void expectOffset(const Widget *child, const Widget *parent, double dx, double dy)
{
    assert(child && parent);
    assert(nearlyEqual(child->offset.x - parent->offset.x, dx));
    assert(nearlyEqual(child->offset.y - parent->offset.y, dy));
}

void expectTopLeft(const Widget *w, double x, double y)
{
    assert(w);
    assert(nearlyEqual(w->offset.x, x));
    assert(nearlyEqual(w->offset.y, y));
}

// 一组前后的打印
template <typename Fn>
void runStage(const char *name, Fn &&fn)
{
    std::cout << "[ RUN  ] " << name << std::endl;
    fn();
    std::cout << "[ PASS ] " << name << std::endl;
}

namespace test_BoxConstraints
{
    // 测试 BoxConstraints 构造函数默认参数
    void testDefaultConstructor()
    {
        BoxConstraints bc;
        assert(nearlyEqual(bc.minWidth(), 0.0));
        assert(nearlyEqual(bc.maxWidth(), BoxConstraints::inf));
        assert(nearlyEqual(bc.minHeight(), 0.0));
        assert(nearlyEqual(bc.maxHeight(), BoxConstraints::inf));
        {
            BoxConstraints bc{};
            assert(nearlyEqual(bc.minWidth(), 0.0));
            assert(nearlyEqual(bc.maxWidth(), BoxConstraints::inf));
            assert(nearlyEqual(bc.minHeight(), 0.0));
            assert(nearlyEqual(bc.maxHeight(), BoxConstraints::inf));
        }
    }

    // 测试 tight: 固定尺寸
    void testTight()
    {
        auto bc = BoxConstraints::tight({100.0, 200.0});
        assert(nearlyEqual(bc.minWidth(), 100.0));
        assert(nearlyEqual(bc.maxWidth(), 100.0));
        assert(nearlyEqual(bc.minHeight(), 200.0));
        assert(nearlyEqual(bc.maxHeight(), 200.0));
        assert(bc.isTight());
    }

    // 测试 tightFor
    void testTightFor()
    {
        // 无值 -> 宽松
        auto bc1 = BoxConstraints::tightFor({});
        assert(nearlyEqual(bc1.minWidth(), 0.0));
        assert(nearlyEqual(bc1.maxWidth(), BoxConstraints::inf));
        assert(nearlyEqual(bc1.minHeight(), 0.0));
        assert(nearlyEqual(bc1.maxHeight(), BoxConstraints::inf));

        // 宽度有值，高度无值 -> 宽度紧，高度宽松
        auto bc2 = BoxConstraints::tightFor({.width = 50.0});
        assert(nearlyEqual(bc2.minWidth(), 50.0));
        assert(nearlyEqual(bc2.maxWidth(), 50.0));
        assert(nearlyEqual(bc2.minHeight(), 0.0));
        assert(nearlyEqual(bc2.maxHeight(), BoxConstraints::inf));

        // 两个维度都有值 -> 两个维度都紧
        auto bc3 = BoxConstraints::tightFor({.width = 30.0, .height = 40.0});
        assert(nearlyEqual(bc3.minWidth(), 30.0));
        assert(nearlyEqual(bc3.maxWidth(), 30.0));
        assert(nearlyEqual(bc3.minHeight(), 40.0));
        assert(nearlyEqual(bc3.maxHeight(), 40.0));
    }

    // 测试 tightForFinite
    void testTightForFinite()
    {
        // 默认 inf -> 宽松
        auto bc1 = BoxConstraints::tightForFinite({});
        assert(nearlyEqual(bc1.minWidth(), 0.0));
        assert(nearlyEqual(bc1.maxWidth(), BoxConstraints::inf));
        assert(nearlyEqual(bc1.minHeight(), 0.0));
        assert(nearlyEqual(bc1.maxHeight(), BoxConstraints::inf));

        // 宽度有限，高度 inf -> 宽度紧，高度宽松
        auto bc2 = BoxConstraints::tightForFinite({100.0, BoxConstraints::inf});
        assert(nearlyEqual(bc2.minWidth(), 100.0));
        assert(nearlyEqual(bc2.maxWidth(), 100.0));
        assert(nearlyEqual(bc2.minHeight(), 0.0));
        assert(nearlyEqual(bc2.maxHeight(), BoxConstraints::inf));

        // 两个维度都有限 -> 两个维度都紧
        auto bc3 = BoxConstraints::tightForFinite({60.0, 70.0});
        assert(nearlyEqual(bc3.minWidth(), 60.0));
        assert(nearlyEqual(bc3.maxWidth(), 60.0));
        assert(nearlyEqual(bc3.minHeight(), 70.0));
        assert(nearlyEqual(bc3.maxHeight(), 70.0));
    }

    // 测试 loose
    void testLoose()
    {
        auto bc = BoxConstraints::loose({120.0, 240.0});
        assert(nearlyEqual(bc.minWidth(), 0.0));
        assert(nearlyEqual(bc.maxWidth(), 120.0));
        assert(nearlyEqual(bc.minHeight(), 0.0));
        assert(nearlyEqual(bc.maxHeight(), 240.0));
        assert(!bc.isTight());
    }

    // 测试 expand
    void testExpand()
    {
        // 无参数 -> 所有维度无限大紧约束
        auto bc1 = BoxConstraints::expand();
        assert(nearlyEqual(bc1.minWidth(), BoxConstraints::inf));
        assert(nearlyEqual(bc1.maxWidth(), BoxConstraints::inf));
        assert(nearlyEqual(bc1.minHeight(), BoxConstraints::inf));
        assert(nearlyEqual(bc1.maxHeight(), BoxConstraints::inf));
        assert(bc1.isTight());

        // 指定宽高 -> 对应维度紧且为指定值
        auto bc2 = BoxConstraints::expand({80.0, 90.0});
        assert(nearlyEqual(bc2.minWidth(), 80.0));
        assert(nearlyEqual(bc2.maxWidth(), 80.0));
        assert(nearlyEqual(bc2.minHeight(), 90.0));
        assert(nearlyEqual(bc2.maxHeight(), 90.0));
    }

    // 测试 copyWith
    void testCopyWith()
    {
        BoxConstraints original(10.0, 100.0, 20.0, 200.0);
        // 只覆盖部分字段
        auto bc = original.copyWith({.minWidth = 50.0, .maxHeight = 150.0});
        assert(nearlyEqual(bc.minWidth(), 50.0));
        assert(nearlyEqual(bc.maxWidth(), 100.0)); // 未覆盖，保持原值
        assert(nearlyEqual(bc.minHeight(), 20.0));
        assert(nearlyEqual(bc.maxHeight(), 150.0));
    }

    // 测试 deflate
    void testDeflate()
    {
        BoxConstraints bc(10.0, 100.0, 20.0, 200.0);
        // 使用 fromLTRB 构造 EdgeInsetsGeometry，start/end 为 0
        auto edges = EdgeInsetsGeometry::fromLTRB(
            5.0, 10.0, 15.0, 20.0); // left=5, top=10, right=15, bottom=20
        auto deflated = bc.deflate(edges);
        // horizontal = left + right = 20, vertical = top + bottom = 30
        assert(nearlyEqual(deflated.minWidth(), std::max(0.0, 10.0 - 20.0)));   // 0
        assert(nearlyEqual(deflated.maxWidth(), std::max(0.0, 100.0 - 20.0)));  // 80
        assert(nearlyEqual(deflated.minHeight(), std::max(0.0, 20.0 - 30.0)));  // 0
        assert(nearlyEqual(deflated.maxHeight(), std::max(0.0, 200.0 - 30.0))); // 170

        // 测试 max 小于 min 的情况：确保 max 不会被减到小于 min
        BoxConstraints bc2(50.0, 60.0, 50.0, 60.0);
        auto edges2 = EdgeInsetsGeometry::fromLTRB(20.0, 0.0, 20.0, 0.0); // horizontal=40
        auto deflated2 = bc2.deflate(edges2);
        assert(nearlyEqual(deflated2.minWidth(), 10.0));
        assert(nearlyEqual(deflated2.maxWidth(), 20.0)); // 60-40=20，大于 min 10
        // 进一步减少：horizontal=60
        auto edges3 = EdgeInsetsGeometry::fromLTRB(30.0, 0.0, 30.0, 0.0); // horizontal=60
        auto deflated3 = bc2.deflate(edges3);
        assert(nearlyEqual(deflated3.minWidth(), 0.0));
        assert(nearlyEqual(deflated3.maxWidth(),
                           0.0)); // 60-60=0，但 min 为 max(0,50-60)=0，所以 max 也为 0
    }

    // 测试 enforce
    void testEnforce()
    {
        BoxConstraints original(0.0, 100.0, 0.0, 100.0);
        BoxConstraints limits(20.0, 80.0, 30.0, 70.0);
        auto enforced = original.enforce(limits);
        assert(nearlyEqual(enforced.minWidth(), 20.0));
        assert(nearlyEqual(enforced.maxWidth(), 80.0));
        assert(nearlyEqual(enforced.minHeight(), 30.0));
        assert(nearlyEqual(enforced.maxHeight(), 70.0));

        // 测试原约束超出范围时被钳制
        BoxConstraints original2(0.0, 200.0, 0.0, 50.0);
        auto enforced2 = original2.enforce(limits);
        assert(nearlyEqual(enforced2.maxWidth(), 80.0));  // 200 -> 80
        assert(nearlyEqual(enforced2.minHeight(), 30.0)); // 0 -> 30
    }

    // 测试 tighten
    void testTighten()
    {
        BoxConstraints bc(0.0, 100.0, 10.0, 200.0);
        // 指定宽度，不指定高度
        auto tightened1 = bc.tighten({50.0, std::nullopt});
        assert(nearlyEqual(tightened1.minWidth(), 50.0));
        assert(nearlyEqual(tightened1.maxWidth(), 50.0));
        assert(nearlyEqual(tightened1.minHeight(), 10.0));
        assert(nearlyEqual(tightened1.maxHeight(), 200.0));

        // 指定高度，不指定宽度
        auto tightened2 = bc.tighten({std::nullopt, 150.0});
        assert(nearlyEqual(tightened2.minWidth(), 0.0));
        assert(nearlyEqual(tightened2.maxWidth(), 100.0));
        assert(nearlyEqual(tightened2.minHeight(), 150.0));
        assert(nearlyEqual(tightened2.maxHeight(), 150.0));

        // 指定值超出范围时被钳制
        auto tightened3 = bc.tighten({-10.0, 300.0});
        assert(nearlyEqual(tightened3.minWidth(), 0.0));
        assert(nearlyEqual(tightened3.maxWidth(), 0.0));
        assert(nearlyEqual(tightened3.minHeight(), 200.0));
        assert(nearlyEqual(tightened3.maxHeight(), 200.0));
    }

    // 测试 normalize
    void testNormalize()
    {
        // 正常约束保持不变
        BoxConstraints normal(0.0, 100.0, 10.0, 200.0);
        auto normalized1 = normal.normalize();
        assert(nearlyEqual(normalized1.minWidth(), 0.0));
        assert(nearlyEqual(normalized1.maxWidth(), 100.0));
        assert(nearlyEqual(normalized1.minHeight(), 10.0));
        assert(nearlyEqual(normalized1.maxHeight(), 200.0));

        // 负 min 变为 0，max 不变
        BoxConstraints negativeMin(-10.0, 50.0, -20.0, 30.0);
        auto normalized2 = negativeMin.normalize();
        assert(nearlyEqual(normalized2.minWidth(), 0.0));
        assert(nearlyEqual(normalized2.maxWidth(), 50.0));
        assert(nearlyEqual(normalized2.minHeight(), 0.0));
        assert(nearlyEqual(normalized2.maxHeight(), 30.0));

        // min > max 时，max 被调整为 min（归一化后 min 为 0）
        BoxConstraints invalid(-10.0, -5.0, 5.0, 3.0);
        auto normalized3 = invalid.normalize();
        assert(nearlyEqual(normalized3.minWidth(), 0.0));
        assert(nearlyEqual(normalized3.maxWidth(),
                           0.0)); // 原 max=-5，但 min 变为 0 后 max 取较大值 0
        assert(nearlyEqual(normalized3.minHeight(), 5.0));
        assert(nearlyEqual(normalized3.maxHeight(),
                           5.0)); // 原 min=5 > max=3，所以 max 变为 5
    }

    void testEnforceIntersection()
    {
        // 父约束：宽 [0, 200]，高 [0, 100]
        BoxConstraints parent(0, 200, 0, 100);

        // 子自身约束：固定宽度 150，高度不限 [0, inf]
        BoxConstraints child = BoxConstraints::tightFor({150.0, std::nullopt});

        // 取交集：父.enforce(子)
        BoxConstraints merged = parent.enforce(child);
        // 宽度：max(0,150)=150, min(200,150)=150 → 紧 150
        assert(merged.minWidth() == 150.0);
        assert(merged.maxWidth() == 150.0);
        // 高度：max(0,0)=0, min(100,inf)=100 → [0, 100]
        assert(merged.minHeight() == 0.0);
        assert(merged.maxHeight() == 100.0);

        // 反向 enforce
        BoxConstraints merged2 = child.enforce(parent);
        assert(merged2.minWidth() == 150.0);
        assert(merged2.maxWidth() == 150.0);
        assert(merged2.minHeight() == 0.0);
        assert(merged2.maxHeight() == 100.0);
    }

    void testTightenWithParent()
    {
        // 父约束：宽 [10, 100]，高 [20, 200]
        BoxConstraints parent(10, 100, 20, 200);

        // 子要求宽度固定 50，高度不指定
        auto tightened = parent.tighten({50.0, std::nullopt});
        // 宽度被收紧到 50（在父范围内），高度保持 [20,200]
        assert(tightened.minWidth() == 50.0);
        assert(tightened.maxWidth() == 50.0);
        assert(tightened.minHeight() == 20.0);
        assert(tightened.maxHeight() == 200.0);

        // 子要求宽度固定 150（超出父最大），会被钳制到父最大 100
        auto clamped = parent.tighten({150.0, std::nullopt});
        assert(clamped.minWidth() == 100.0);
        assert(clamped.maxWidth() == 100.0);
    }

    void testLoosen()
    {
        BoxConstraints bc(50, 100, 20, 200);
        auto loose = bc.loosen();
        assert(nearlyEqual(loose.minWidth(), 0.0));
        assert(nearlyEqual(loose.maxWidth(), 100.0));
        assert(nearlyEqual(loose.minHeight(), 0.0));
        assert(nearlyEqual(loose.maxHeight(), 200.0));

        // 本来就是 loose 的，结果不变
        BoxConstraints already(0, 100, 0, 200);
        assert(already.loosen() == already);
    }

    void testFlipped()
    {
        BoxConstraints bc(50, 100, 20, 200);
        auto f = bc.flipped();
        assert(nearlyEqual(f.minWidth(), 20.0));
        assert(nearlyEqual(f.maxWidth(), 200.0));
        assert(nearlyEqual(f.minHeight(), 50.0));
        assert(nearlyEqual(f.maxHeight(), 100.0));
    }

    void testEquality()
    {
        BoxConstraints a(10, 20, 30, 40);
        BoxConstraints b(10, 20, 30, 40);
        BoxConstraints c(10, 20, 30, 41);
        assert(a == b);
        assert(a != c);
    }

}; // namespace test_BoxConstraints

// =========================================================================
// 2. Container 与子节点：padding/border 压缩与定位
// =========================================================================
namespace test_Container_Child
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    // 2a. Container padding and border compress child
    void testPaddingBorderCompressChild()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Container("outer")
                                     .width(200)
                                     .height(150)
                                     .padding(EdgeInsets::all(10))
                                     .border(EdgeInsets::all(5))
                                     .child(Container("inner"))))
            .layout();

        auto *outer = screen.findByKey("outer");
        auto *inner = screen.findByKey("inner");
        assert(outer && inner);

        expectSize(outer, 200, 150);
        expectSize(inner, 170, 120);
        expectOffset(inner, outer, 15, 15);
        expectTopLeft(outer, 300, 225);
    }

    // 2b. Container border only
    void testBorderOnly()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Container("outer")
                                     .width(100)
                                     .height(100)
                                     .border(EdgeInsets::all(5))
                                     .child(Container("inner"))))
            .layout();

        auto *outer = screen.findByKey("outer");
        auto *inner = screen.findByKey("inner");
        assert(outer && inner);

        expectSize(outer, 100, 100);
        expectSize(inner, 90, 90);
        expectOffset(inner, outer, 5, 5);
        expectTopLeft(outer, 350, 250);
    }

    // 2c. Container shrink to child with padding and border
    void testShrinkToChild()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Container("c")
                                     .padding(EdgeInsets::all(10))
                                     .border(EdgeInsets::all(5))
                                     .child(SizedBox("s").width(60).height(40))))
            .layout();

        auto *c = screen.findByKey("c");
        auto *s = screen.findByKey("s");
        assert(c && s);

        expectSize(c, 90, 70);
        expectSize(s, 60, 40);
        expectOffset(s, c, 15, 15);
        expectTopLeft(c, 355, 265);
    }
} // namespace test_Container_Child

void test_Container_Child_api()
{
    using namespace test_Container_Child;
    runStage("2a Container padding and border compress child",
             testPaddingBorderCompressChild);
    runStage("2b Container border only", testBorderOnly);
    runStage("2c Container shrink to child with padding and border", testShrinkToChild);
}

// =========================================================================
// 3. Container alignment 对子节点定位和约束的影响
// =========================================================================
namespace test_Container_Alignment
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    // 3a. Container alignment topLeft (default)
    void testTopLeft()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Container("outer")
                                     .width(200)
                                     .height(100)
                                     .padding(EdgeInsets::all(10))
                                     .child(Container("inner").width(50).height(30))))
            .layout();

        auto *outer = screen.findByKey("outer");
        auto *inner = screen.findByKey("inner");
        assert(outer && inner);

        expectOffset(inner, outer, 10, 10);
        expectTopLeft(outer, 300, 250);
    }

    // 3b. Container alignment center
    void testCenter()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Container("outer")
                                     .width(200)
                                     .height(100)
                                     .alignment(Alignment::center)
                                     .child(Container("inner").width(50).height(30))))
            .layout();

        auto *outer = screen.findByKey("outer");
        auto *inner = screen.findByKey("inner");
        assert(outer && inner);

        expectOffset(inner, outer, 75, 35);
        expectTopLeft(outer, 300, 250);
    }

    // 3c. Container alignment bottomRight with padding
    void testBottomRightWithPadding()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Container("outer")
                                     .width(200)
                                     .height(150)
                                     .padding(EdgeInsets::all(20))
                                     .alignment(Alignment::bottomRight)
                                     .child(Container("inner").width(60).height(40))))
            .layout();

        auto *outer = screen.findByKey("outer");
        auto *inner = screen.findByKey("inner");
        assert(outer && inner);

        expectOffset(inner, outer, 120, 90);
        expectTopLeft(outer, 300, 225);
    }

    // 3d. Container alignment changes constraints to loose
    void testAlignmentLoose()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Container("outer")
                                     .width(200)
                                     .height(150)
                                     .alignment(Alignment::center)
                                     .child(Container("inner"))))
            .layout();

        auto *outer = screen.findByKey("outer");
        auto *inner = screen.findByKey("inner");
        assert(outer && inner);

        expectSize(inner, 200, 150);
        expectOffset(inner, outer, 0, 0);
        expectTopLeft(outer, 300, 225);
    }
} // namespace test_Container_Alignment

void test_Container_Alignment_api()
{
    using namespace test_Container_Alignment;
    runStage("3a Container alignment topLeft", testTopLeft);
    runStage("3b Container alignment center", testCenter);
    runStage("3c Container alignment bottomRight with padding",
             testBottomRightWithPadding);
    runStage("3d Container alignment changes constraints to loose", testAlignmentLoose);
}

// =========================================================================
// 4. SizedBox 行为
// =========================================================================
namespace test_SizedBox
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    // 4a. SizedBox fixed size forces child tight
    void testFixedForcesTight()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(
                Center().child(SizedBox("s").width(100).height(80).child(Container("c"))))
            .layout();

        auto *s = screen.findByKey("s");
        auto *c = screen.findByKey("c");
        assert(s && c);

        expectSize(s, 100, 80);
        expectSize(c, 100, 80);
        expectOffset(c, s, 0, 0);
        expectTopLeft(s, 350, 260);
    }

    // 4b. SizedBox no child and no size is zero
    void testNoChildNoSize()
    {
        ScreenWidget screen{};
        screen.size(Width, Height).root(Center().child(SizedBox("s"))).layout();

        auto *s = screen.findByKey("s");
        assert(s);
        expectSize(s, 0, 0);
    }

    // 4c. SizedBox with only width and child height from child
    void testWidthFromSelfHeightFromChild()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(
                Center().child(SizedBox("s").width(100).child(Container("c").height(50))))
            .layout();

        auto *s = screen.findByKey("s");
        auto *c = screen.findByKey("c");
        assert(s && c);

        expectSize(s, 100, 50);
        expectSize(c, 100, 50);
        expectOffset(c, s, 0, 0);
        expectTopLeft(s, 350, 275);
    }
} // namespace test_SizedBox

void test_SizedBox_api()
{
    using namespace test_SizedBox;
    runStage("4a SizedBox fixed size forces child tight", testFixedForcesTight);
    runStage("4b SizedBox no child and no size is zero", testNoChildNoSize);
    runStage("4c SizedBox with only width and child height from child",
             testWidthFromSelfHeightFromChild);
}

// =========================================================================
// 5. Padding 行为
// =========================================================================
namespace test_Padding
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    // 5a. Padding adds insets around child
    void testInsets()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Padding("p")
                                     .padding(EdgeInsets::all(10))
                                     .child(SizedBox("s").width(50).height(30))))
            .layout();

        auto *p = screen.findByKey("p");
        auto *s = screen.findByKey("s");
        assert(p && s);

        expectSize(p, 70, 50);
        expectSize(s, 50, 30);
        expectOffset(s, p, 10, 10);
        expectTopLeft(p, 365, 275);
    }

    // 5b. Padding with asymmetric insets
    void testAsymmetric()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Padding("p")
                                     .padding(EdgeInsetsGeometry::fromLTRB(8, 6, 4, 2))
                                     .child(SizedBox("s").width(40).height(20))))
            .layout();

        auto *p = screen.findByKey("p");
        auto *s = screen.findByKey("s");
        assert(p && s);

        expectSize(p, 52, 28);
        expectOffset(s, p, 8, 6);
        expectTopLeft(p, 374, 286);
    }
} // namespace test_Padding

void test_Padding_api()
{
    using namespace test_Padding;
    runStage("5a Padding adds insets around child", testInsets);
    runStage("5b Padding with asymmetric insets", testAsymmetric);
}

// =========================================================================
// 6. 嵌套结构与尺寸传播
// =========================================================================
namespace test_Nesting
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    // 6a. Nested Container with margin
    void testNestedContainerWithMargin()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Container("outer")
                                     .margin(EdgeInsets::all(5))
                                     .padding(EdgeInsets::all(10))
                                     .child(Container("inner").width(60).height(40))))
            .layout();

        auto *outer = screen.findByKey("outer");
        auto *inner = screen.findByKey("inner");
        assert(outer && inner);

        expectSize(outer, 90, 70);
        expectSize(inner, 60, 40);
        expectOffset(inner, outer, 15, 15);
        expectTopLeft(outer, 355, 265);
    }

    // 6b. Deep nesting: Container > Padding > SizedBox
    void testDeepNesting()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(
                Container("container")
                    .width(300)
                    .height(200)
                    .padding(EdgeInsets::all(20))
                    .border(EdgeInsets::all(5))
                    .alignment(Alignment::topLeft)
                    .child(Padding("padding")
                               .padding(EdgeInsetsGeometry::fromLTRB(10, 8, 6, 4))
                               .child(SizedBox("sizedbox").width(100).height(80)))))
            .layout();

        auto *c = screen.findByKey("container");
        auto *p = screen.findByKey("padding");
        auto *s = screen.findByKey("sizedbox");
        assert(c && p && s);

        expectSize(c, 300, 200);
        expectSize(p, 116, 92);
        expectSize(s, 100, 80);
        expectOffset(s, p, 10, 8);
        expectOffset(p, c, 25, 25);
        expectTopLeft(c, 250, 200);
    }
} // namespace test_Nesting

void test_Nesting_api()
{
    using namespace test_Nesting;
    runStage("6a Nested Container with margin", testNestedContainerWithMargin);
    runStage("6b Deep nesting: Container > Padding > SizedBox", testDeepNesting);
}

// =========================================================================
// 7. 边界与默认行为
// =========================================================================
namespace test_Boundary
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    // 7a. Zero size child with padding
    void testZeroSizeChildWithPadding()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Container("c")
                                     .padding(EdgeInsets::all(10))
                                     .child(SizedBox("s").width(0).height(0))))
            .layout();

        auto *c = screen.findByKey("c");
        auto *s = screen.findByKey("s");
        assert(c && s);

        expectSize(c, 20, 20);
        expectSize(s, 0, 0);
        expectOffset(s, c, 10, 10);
        expectTopLeft(c, 390, 290);
    }

    // 7b. Default margin/padding/border are zero
    void testDefaultsZero()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(
                Container("outer").width(100).height(80).child(Container("inner"))))
            .layout();

        auto *outer = screen.findByKey("outer");
        auto *inner = screen.findByKey("inner");
        assert(outer && inner);

        expectSize(outer, 100, 80);
        expectSize(inner, 100, 80);
        expectOffset(inner, outer, 0, 0);
        expectTopLeft(outer, 350, 260);
    }

    // 7c. Container fixed width, height from child
    void testWidthFromSelfHeightFromChild()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(
                Container("outer").width(150).child(Container("inner").height(40))))
            .layout();

        auto *outer = screen.findByKey("outer");
        auto *inner = screen.findByKey("inner");
        assert(outer && inner);

        expectSize(outer, 150, 40);
        expectSize(inner, 150, 40);
        expectOffset(inner, outer, 0, 0);
        expectTopLeft(outer, 325, 280);
    }
} // namespace test_Boundary

void test_Boundary_api()
{
    using namespace test_Boundary;
    runStage("7a Zero size child with padding", testZeroSizeChildWithPadding);
    runStage("7b Default margin/padding/border are zero", testDefaultsZero);
    runStage("7c Container fixed width, height from child",
             testWidthFromSelfHeightFromChild);
}

// =========================================================================
// 8. 综合复杂测试
// =========================================================================
namespace test_Comprehensive
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    // 8. Comprehensive deep nesting box model
    void testDeepNestingBoxModel()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(
                Container("outer")
                    .width(400)
                    .height(300)
                    .margin(EdgeInsets::all(20))
                    .padding(EdgeInsets::all(10))
                    .border(EdgeInsets::all(5))
                    .alignment(Alignment::center)
                    .child(Padding("padding")
                               .padding(EdgeInsetsGeometry::fromLTRB(8, 6, 4, 2))
                               .child(SizedBox("sizedbox").width(150).height(80)))))
            .layout();

        auto *outer = screen.findByKey("outer");
        auto *p = screen.findByKey("padding");
        auto *s = screen.findByKey("sizedbox");
        assert(outer && p && s);

        expectSize(outer, 440, 340);
        expectSize(p, 162, 88);
        expectSize(s, 150, 80);
        expectOffset(s, p, 8, 6);
        expectOffset(p, outer, 139, 126);
        expectTopLeft(outer, 180, 130);
    }
} // namespace test_Comprehensive

void test_Comprehensive_api()
{
    using namespace test_Comprehensive;
    runStage("8 Comprehensive deep nesting box model", testDeepNestingBoxModel);
}

// =========================================================================
// 9. 约束传递与 alignment 的交互
// =========================================================================
namespace test_ConstraintPassing
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    // 9a. Container without alignment passes tight constraints to child
    void testTightToChild()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("s").width(200).height(100).child(
                Container("outer")
                    .padding(EdgeInsets::all(10))
                    .child(Container("inner").width(30).height(20)))))
            .layout();

        auto *outer = screen.findByKey("outer");
        auto *inner = screen.findByKey("inner");
        assert(outer && inner);

        expectSize(inner, 180, 80);
        expectOffset(inner, outer, 10, 10);
        expectTopLeft(outer, 300, 250);
    }

    // 9b. Container with alignment passes loose constraints to child
    void testLooseToChild()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("s").width(200).height(100).child(
                Container("outer")
                    .padding(EdgeInsets::all(10))
                    .alignment(Alignment::topLeft)
                    .child(Container("inner").width(30).height(20)))))
            .layout();

        auto *outer = screen.findByKey("outer");
        auto *inner = screen.findByKey("inner");
        assert(outer && inner);

        expectSize(inner, 30, 20);
        expectOffset(inner, outer, 10, 10);
        expectTopLeft(outer, 300, 250);
    }

    // 9c. Container no child loose constraints fills
    void testNoChildLooseFills()
    {
        ScreenWidget screen{};
        screen.size(Width, Height).root(Center().child(Container("c"))).layout();

        auto *c = screen.findByKey("c");
        assert(c);

        expectSize(c, 800, 600);
        expectTopLeft(c, 0, 0);
    }

    // 9d. Container no child loose constraints with alignment fills
    void testNoChildLooseWithAlignmentFills()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Container("c").alignment(Alignment::center)))
            .layout();

        auto *c = screen.findByKey("c");
        assert(c);

        expectSize(c, 800, 600);
        expectTopLeft(c, 0, 0);
    }
} // namespace test_ConstraintPassing

void test_ConstraintPassing_api()
{
    using namespace test_ConstraintPassing;
    runStage("9a Container without alignment passes tight constraints to child",
             testTightToChild);
    runStage("9b Container with alignment passes loose constraints to child",
             testLooseToChild);
    runStage("9c Container no child loose constraints fills", testNoChildLooseFills);
    runStage("9d Container no child loose constraints with alignment fills",
             testNoChildLooseWithAlignmentFills);
}

// =========================================================================
// 10. Padding 与 tight/loose 约束
// =========================================================================
namespace test_Padding_Constraints
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    // 10a. Padding under tight constraints stretches child
    void testTightStretchesChild()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("s").width(200).height(150).child(
                Padding("p").padding(EdgeInsets::all(20)).child(Container("c")))))
            .layout();

        auto *p = screen.findByKey("p");
        auto *c = screen.findByKey("c");
        assert(p && c);

        expectSize(p, 200, 150);
        expectSize(c, 160, 110);
        expectOffset(c, p, 20, 20);
        expectTopLeft(p, 300, 225);
    }

    // 10b. Padding under loose constraints shrinks to child
    void testLooseShrinksToChild()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Padding("p")
                                     .padding(EdgeInsets::all(20))
                                     .child(SizedBox("s").width(50).height(30))))
            .layout();

        auto *p = screen.findByKey("p");
        auto *s = screen.findByKey("s");
        assert(p && s);

        expectSize(p, 90, 70);
        expectSize(s, 50, 30);
        expectOffset(s, p, 20, 20);
        expectTopLeft(p, 355, 265);
    }
} // namespace test_Padding_Constraints

void test_Padding_Constraints_api()
{
    using namespace test_Padding_Constraints;
    runStage("10a Padding under tight constraints stretches child",
             testTightStretchesChild);
    runStage("10b Padding under loose constraints shrinks to child",
             testLooseShrinksToChild);
}

// =========================================================================
// 11. Container 固定尺寸与 alignment 的交互
// =========================================================================
namespace test_Container_FixedAlignment
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    void testFixedSizeWithAlignment()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Container("c")
                                     .width(200)
                                     .height(100)
                                     .alignment(Alignment::center)
                                     .child(Container("inner").width(40).height(20))))
            .layout();

        auto *c = screen.findByKey("c");
        auto *inner = screen.findByKey("inner");
        assert(c && inner);

        expectSize(c, 200, 100);
        expectSize(inner, 40, 20);
        expectOffset(inner, c, 80, 40);
        expectTopLeft(c, 300, 250);
    }
} // namespace test_Container_FixedAlignment

void test_Container_FixedAlignment_api()
{
    using namespace test_Container_FixedAlignment;
    runStage("11 Container fixed size with alignment still keeps size",
             testFixedSizeWithAlignment);
}

// =========================================================================
// 12. SizedBox 的 tight 约束传递
// =========================================================================
namespace test_SizedBox_Tight
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    // 12a. SizedBox forces tight constraints even if child has fixed size
    void testForceTight()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("s").width(120).height(80).child(
                Container("c").width(30).height(20))))
            .layout();

        auto *s = screen.findByKey("s");
        auto *c = screen.findByKey("c");
        assert(s && c);

        expectSize(s, 120, 80);
        expectSize(c, 120, 80);
        expectOffset(c, s, 0, 0);
        expectTopLeft(s, 340, 260);
    }

    // 12b. SizedBox with loose child constraints?
    void testFillByTight()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(
                Center().child(SizedBox("s").width(100).height(50).child(Container("c"))))
            .layout();

        auto *s = screen.findByKey("s");
        auto *c = screen.findByKey("c");
        assert(s && c);

        expectSize(c, 100, 50);
        expectOffset(c, s, 0, 0);
        expectTopLeft(s, 350, 275);
    }
} // namespace test_SizedBox_Tight

void test_SizedBox_Tight_api()
{
    using namespace test_SizedBox_Tight;
    runStage("12a SizedBox forces tight constraints even if child has fixed size",
             testForceTight);
    runStage("12b SizedBox with loose child constraints?", testFillByTight);
}

// =========================================================================
// 13. 多个 margin 的累积与定位
// =========================================================================
namespace test_NestedMargins
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    void testNestedMargins()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(
                Container("outer")
                    .margin(EdgeInsets::all(10))
                    .child(Container("middle")
                               .margin(EdgeInsets::all(5))
                               .child(Container("inner").width(50).height(30)))))
            .layout();

        auto *outer = screen.findByKey("outer");
        auto *middle = screen.findByKey("middle");
        auto *inner = screen.findByKey("inner");
        assert(outer && middle && inner);

        expectSize(outer, 80, 60);
        expectSize(middle, 60, 40);
        expectSize(inner, 50, 30);
        expectOffset(inner, middle, 5, 5);
        expectOffset(middle, outer, 10, 10);
        expectTopLeft(outer, 360, 270);
    }
} // namespace test_NestedMargins

void test_NestedMargins_api()
{
    using namespace test_NestedMargins;
    runStage("13 Nested margins accumulate and affect offset", testNestedMargins);
}

// =========================================================================
// 14. 边界：padding 导致内容区域为负或零
// =========================================================================
namespace test_Padding_Clamp
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    void testPaddingLargerThanSpace()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("s").width(50).height(50).child(
                Padding("p").padding(EdgeInsets::all(30)).child(Container("c")))))
            .layout();

        auto *p = screen.findByKey("p");
        auto *c = screen.findByKey("c");
        assert(p && c);

        expectSize(p, 50, 50);
        expectSize(c, 0, 0);
        expectTopLeft(p, 375, 275);
    }
} // namespace test_Padding_Clamp

void test_Padding_Clamp_api()
{
    using namespace test_Padding_Clamp;
    runStage("14 Padding larger than available space clamps child to zero",
             testPaddingLargerThanSpace);
}

// =========================================================================
// 15. 综合：多级嵌套与边界
// =========================================================================
namespace test_Extreme
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    void testExtreme()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(
                Container("c1")
                    .width(300)
                    .height(200)
                    .margin(EdgeInsets::all(5))
                    .padding(EdgeInsets::all(8))
                    .border(EdgeInsets::all(2))
                    .alignment(Alignment::center)
                    .child(Container("c2")
                               .margin(EdgeInsets::all(3))
                               .padding(EdgeInsets::all(4))
                               .child(SizedBox("s").width(100).height(60)))))
            .layout();

        auto *c1 = screen.findByKey("c1");
        auto *c2 = screen.findByKey("c2");
        auto *s = screen.findByKey("s");
        assert(c1 && c2 && s);

        expectSize(c1, 310, 210);
        expectSize(c2, 114, 74);
        expectSize(s, 100, 60);
        expectOffset(s, c2, 7, 7);
        expectOffset(c2, c1, 98, 68);
        expectTopLeft(c1, 245, 195);
    }
} // namespace test_Extreme

void test_Extreme_api()
{
    using namespace test_Extreme;
    runStage("15 Extreme nesting with margins, padding, border, alignment", testExtreme);
}

// =========================================================================
// 16. Container alignment 全 9 种
// =========================================================================
namespace test_Container_Alignment_All
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    void testAllAlignments()
    {
        struct AlignCase
        {
            const char *name;
            Alignment a;
        };
        const AlignCase cases[] = {
            {"topLeft", Alignment::topLeft},
            {"topCenter", Alignment::topCenter},
            {"topRight", Alignment::topRight},
            {"centerLeft", Alignment::centerLeft},
            {"center", Alignment::center},
            {"centerRight", Alignment::centerRight},
            {"bottomLeft", Alignment::bottomLeft},
            {"bottomCenter", Alignment::bottomCenter},
            {"bottomRight", Alignment::bottomRight},
        };

        for (const auto &c : cases)
        {
            ScreenWidget screen{};
            screen.size(Width, Height)
                .root(Center().child(
                    Container("outer").width(200).height(100).alignment(c.a).child(
                        Container("inner").width(50).height(30))))
                .layout();

            auto *outer = screen.findByKey("outer");
            auto *inner = screen.findByKey("inner");
            assert(outer && inner);

            const double freeW = 200 - 50; // 150
            const double freeH = 100 - 30; // 70
            const double dx = freeW * (c.a.x + 1.0) * 0.5;
            const double dy = freeH * (c.a.y + 1.0) * 0.5;

            expectSize(outer, 200, 100);
            expectSize(inner, 50, 30);
            expectOffset(inner, outer, dx, dy);
        }
    }
} // namespace test_Container_Alignment_All

void test_Container_Alignment_All_api()
{
    using namespace test_Container_Alignment_All;
    runStage("16 Container alignment 9 values", testAllAlignments);
}

// =========================================================================
// 17. 单维度指定时的对称场景
// =========================================================================
namespace test_Single_Dimension
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    // 17a. SizedBox 只指定 height，宽由 child 决定
    void testSizedBoxHeightOnly()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(
                Center().child(SizedBox("s").height(80).child(Container("c").width(100))))
            .layout();

        auto *s = screen.findByKey("s");
        auto *c = screen.findByKey("c");
        assert(s && c);

        expectSize(s, 100, 80);
        expectSize(c, 100, 80);
        expectOffset(c, s, 0, 0);
        // (800-100)/2 = 350, (600-80)/2 = 260
        expectTopLeft(s, 350, 260);
    }

    // 17b. Container 只指定 height，宽由 child 决定
    void testContainerHeightOnly()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(
                Container("outer").height(40).child(Container("inner").width(150))))
            .layout();

        auto *outer = screen.findByKey("outer");
        auto *inner = screen.findByKey("inner");
        assert(outer && inner);

        expectSize(outer, 150, 40);
        expectSize(inner, 150, 40);
        expectOffset(inner, outer, 0, 0);
        // (800-150)/2 = 325, (600-40)/2 = 280
        expectTopLeft(outer, 325, 280);
    }
} // namespace test_Single_Dimension

void test_Single_Dimension_api()
{
    using namespace test_Single_Dimension;
    runStage("17a SizedBox with only height and child width from child",
             testSizedBoxHeightOnly);
    runStage("17b Container fixed height, width from child", testContainerHeightOnly);
}

// =========================================================================
// 18. SizedBox 无 child 但指定宽高
// =========================================================================
namespace test_SizedBox_NoChild_WithSize
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    void testSizedBoxNoChildWithSize()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("s").width(100).height(80)))
            .layout();

        auto *s = screen.findByKey("s");
        assert(s);

        // effective = tightFor(100,80).enforce((0,800,0,600)) = tight(100,80)
        // size = effective.smallest() = (100,80)
        expectSize(s, 100, 80);
        // (800-100)/2 = 350, (600-80)/2 = 260
        expectTopLeft(s, 350, 260);
    }
} // namespace test_SizedBox_NoChild_WithSize

void test_SizedBox_NoChild_WithSize_api()
{
    using namespace test_SizedBox_NoChild_WithSize;
    runStage("18 SizedBox with size but no child", testSizedBoxNoChildWithSize);
}

// =========================================================================
// 19. Padding 无 child
// =========================================================================
namespace test_Padding_NoChild
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    void testPaddingNoChild()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Padding("p").padding(EdgeInsets::all(10))))
            .layout();

        auto *p = screen.findByKey("p");
        assert(p);

        // Flutter: size = c.constrain(Size(padding.horizontal, padding.vertical))
        // = (0,800,0,600).constrain(20,20) = (20,20)
        expectSize(p, 20, 20);
        // (800-20)/2 = 390, (600-20)/2 = 290
        expectTopLeft(p, 390, 290);
    }
} // namespace test_Padding_NoChild

void test_Padding_NoChild_api()
{
    using namespace test_Padding_NoChild;
    runStage("19 Padding with no child", testPaddingNoChild);
}

// =========================================================================
// 20. Container 同时有 margin + padding/border 且无 child
// =========================================================================
namespace test_Container_NoChild_MarginAndInsets
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    void testMarginWithPaddingBorderNoChild()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Container("c")
                                     .width(100)
                                     .height(80)
                                     .margin(EdgeInsets::all(15))
                                     .padding(EdgeInsets::all(10))
                                     .border(EdgeInsets::all(5))))
            .layout();

        auto *c = screen.findByKey("c");
        assert(c);

        // 无 child 时：padding/border 不计入自身尺寸，只有 margin 计入
        // 100 + 2*15 = 130, 80 + 2*15 = 110
        expectSize(c, 130, 110);
        // (800-130)/2 = 335, (600-110)/2 = 245
        expectTopLeft(c, 335, 245);
    }
} // namespace test_Container_NoChild_MarginAndInsets

void test_Container_NoChild_MarginAndInsets_api()
{
    using namespace test_Container_NoChild_MarginAndInsets;
    runStage("20 Container with margin + padding/border, no child",
             testMarginWithPaddingBorderNoChild);
}

auto test_BoxConstraints_api()
{
    using namespace test_BoxConstraints;
    testDefaultConstructor();
    testTight();
    testTightFor();
    testTightForFinite();
    testLoose();
    testExpand();
    testCopyWith();
    testDeflate();
    testEnforce();
    testTighten();
    testNormalize();
    testEnforceIntersection();
    testTightenWithParent();
    testLoosen();
    testFlipped();
    testEquality();
};

namespace test_Container_NoChild
{

    static constexpr auto Width = 800;
    static constexpr auto Height = 600;
    // 1. Container fixed size, no child, no margin/padding/border
    void testFixedSizeNoChild()
    {

        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Container("c").width(100).height(80)))
            .layout();

        auto *c = screen.findByKey("c");
        assert(c);
        assert(nearlyEqual(c->size.width, 100.0));
        assert(nearlyEqual(c->size.height, 80.0));
        assert(nearlyEqual(c->offset.x, 350.0));
        assert(nearlyEqual(c->offset.y, 260.0));
    }
    // 2. Container with margin includes margin in size
    void testWithMargin()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(
                Container("c").width(100).height(80).margin(EdgeInsets::all(15))))
            .layout();

        auto *c = screen.findByKey("c");
        assert(c);
        // 130 = 100 + 2*15, 110 = 80 + 2*15
        assert(nearlyEqual(c->size.width, 130.0));
        assert(nearlyEqual(c->size.height, 110.0));
        // (800-130)/2 = 335, (600-110)/2 = 245
        assert(nearlyEqual(c->offset.x, 335.0));
        assert(nearlyEqual(c->offset.y, 245.0));
    }

    // 3. Container with padding and border, no child, fixed size
    void testWithPaddingBorderNoChild()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Container("c")
                                     .width(100)
                                     .height(80)
                                     .padding(EdgeInsets::all(10))
                                     .border(EdgeInsets::all(5))))
            .layout();

        auto *c = screen.findByKey("c");
        assert(c);
        // 无 child 时 padding/border 不增加自身尺寸
        assert(nearlyEqual(c->size.width, 100.0));
        assert(nearlyEqual(c->size.height, 80.0));
        assert(nearlyEqual(c->offset.x, 350.0));
        assert(nearlyEqual(c->offset.y, 260.0));
    }

    // 4. Container no child, no size, in tight constraints fills
    //    用 Container(tight 200x150) 模拟 Dart 的 SizedBox(200, 150)
    void testNoSizeTightConstraintsFills()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(
                Container("s").width(200).height(150).child(Container("c"))))
            .layout();

        auto *c = screen.findByKey("c");
        assert(c);
        assert(nearlyEqual(c->size.width, 200.0));
        assert(nearlyEqual(c->size.height, 150.0));
        // 与外层 SizedBox 同位置：(800-200)/2 = 300, (600-150)/2 = 225
        assert(nearlyEqual(c->offset.x, 300.0));
        assert(nearlyEqual(c->offset.y, 225.0));
    }

}; // namespace test_Container_NoChild
void test_Container_NoChild_api()
{
    using namespace test_Container_NoChild;
    testFixedSizeNoChild();
    testWithMargin();
    testWithPaddingBorderNoChild();
    testNoSizeTightConstraintsFills();
}

// =========================================================================
// 21. ConstrainedBox：约束修改类
//    算法：合并父约束与自身 additionalConstraints（enforce），
//          布局 child，size = child.size（无 child 时为 constrain(Size.zero)）。
//          只能收紧约束，不能放松父约束。
// =========================================================================
namespace test_ConstrainedBox
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    // 21a. min constraints enlarge child in loose parent
    void testMinConstraintsEnlargeChildInLooseParent()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(
                ConstrainedBox("cb")
                    .constraints(BoxConstraints(200.0, BoxConstraints::inf, 100.0,
                                                BoxConstraints::inf))
                    .child(SizedBox("s").width(50).height(30))))
            .layout();

        auto *cb = screen.findByKey("cb");
        auto *s = screen.findByKey("s");
        assert(cb && s);

        // 父约束 0..800 / 0..600 与 min 200x100 合并，child 被抬到 200x100
        expectSize(cb, 200, 100);
        expectSize(s, 200, 100);
        expectOffset(s, cb, 0, 0);
        // 居中：(800-200)/2=300, (600-100)/2=250
        expectTopLeft(cb, 300, 250);
    }

    // 21b. cannot relax tight parent constraints
    void testCannotRelaxTightParentConstraints()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("outer").width(100).height(80).child(
                ConstrainedBox("cb")
                    .constraints(BoxConstraints(200.0, BoxConstraints::inf, 100.0,
                                                BoxConstraints::inf))
                    .child(Container("c")))))
            .layout();

        auto *cb = screen.findByKey("cb");
        auto *c = screen.findByKey("c");
        assert(cb && c);

        // 试图放宽到 200x100，但父约束 tight 100x80，enforce 后仍为 100x80
        expectSize(cb, 100, 80);
        expectSize(c, 100, 80);
        expectTopLeft(cb, 350, 260);
    }

    // 21c. no child, size = constrain(Size.zero)
    void testNoChildSizeEqualsConstrainZero()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(ConstrainedBox("cb").constraints(
                BoxConstraints(100.0, BoxConstraints::inf, 50.0, BoxConstraints::inf))))
            .layout();

        auto *cb = screen.findByKey("cb");
        assert(cb);

        // 无 child 时取合并约束对 Size.zero 的 constrain 结果
        expectSize(cb, 100, 50);
        expectTopLeft(cb, 350, 275);
    }
} // namespace test_ConstrainedBox
void test_ConstrainedBox_api()
{
    using namespace test_ConstrainedBox;
    runStage("21a ConstrainedBox min constraints enlarge child in loose parent",
             testMinConstraintsEnlargeChildInLooseParent);
    runStage("21b ConstrainedBox cannot relax tight parent constraints",
             testCannotRelaxTightParentConstraints);
    runStage("21c ConstrainedBox no child, size = constrain(Size.zero)",
             testNoChildSizeEqualsConstrainZero);
}

// =========================================================================
// 22. OverflowBox：给 child 不同的约束
//    算法：自身 size 由父约束决定（max → c.biggest()）；
//          child 使用 OverflowBox 指定的约束，按 alignment 定位，可超出自身范围。
//    注意：minWidth/minHeight 未显式指定时会从父约束继承，
//          若要给 child 宽松/无界约束，必须显式写出 min 值。
// =========================================================================
namespace test_OverflowBox
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;
    static constexpr auto inf = std::numeric_limits<double>::infinity();

    // 22a. child constrained to fixed size, centered
    void testChildFixedSizeCentered()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(
                SizedBox("s").width(200).height(200).child(OverflowBox("ob")
                                                               .minWidth(100)
                                                               .maxWidth(100)
                                                               .minHeight(100)
                                                               .maxHeight(100)
                                                               .child(Container("c")))))
            .layout();

        auto *ob = screen.findByKey("ob");
        auto *c = screen.findByKey("c");
        assert(ob && c);

        // 自身尺寸来自父约束 tight 200x200
        expectSize(ob, 200, 200);
        // child 被强制为 100x100
        expectSize(c, 100, 100);
        // 默认 alignment.center -> (200-100)/2 = 50
        expectOffset(c, ob, 50, 50);
        // SizedBox 在 (300,200)，child 在 (350,250)
        expectTopLeft(c, 350, 250);
    }

    // 22b. child larger than self via unbounded constraints
    void testChildLargerThanSelf()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("s").width(100).height(100).child(
                OverflowBox("ob")
                    // 显式 min: 0，避免继承父约束的 tight 100
                    .minWidth(0)
                    .maxWidth(inf)
                    .minHeight(0)
                    .maxHeight(inf)
                    .child(Container("c").width(200).height(200)))))
            .layout();

        auto *ob = screen.findByKey("ob");
        auto *c = screen.findByKey("c");
        assert(ob && c);

        expectSize(ob, 100, 100);
        expectSize(c, 200, 200);
        // 居中 -> 负偏移 (100-200)/2 = -50
        expectOffset(c, ob, -50, -50);
        // SizedBox 在 (350,250)，child 在 (300,200)
        expectTopLeft(c, 300, 200);
    }
} // namespace test_OverflowBox

void test_OverflowBox_api()
{
    using namespace test_OverflowBox;
    runStage("22a OverflowBox child constrained to fixed size, centered",
             testChildFixedSizeCentered);
    runStage("22b OverflowBox child larger than self via unbounded constraints",
             testChildLargerThanSelf);
}

// =========================================================================
// 23. UnconstrainedBox：移除父约束
//    算法：给 child 无界约束，让 child 按自然尺寸布局；
//          自身 size 仍受父约束限制（c.constrain(child.size)）；
//          child 在自身范围内按 alignment 定位。
//    注意：为避免溢出，child 尺寸都 <= 自身尺寸。
// =========================================================================
namespace test_UnconstrainedBox
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    // 23a. child keeps its natural size, not stretched
    void testChildKeepsNaturalSize()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("s").width(200).height(200).child(
                UnconstrainedBox("ub").child(Container("c").width(100).height(100)))))
            .layout();

        auto *ub = screen.findByKey("ub");
        auto *c = screen.findByKey("c");
        assert(ub && c);

        // 自身 size 被父 tight 约束限制为 200x200
        expectSize(ub, 200, 200);
        // 关键：child 保持 100x100，没有被拉伸到 200x200
        expectSize(c, 100, 100);
        // 默认居中：(200-100)/2 = 50
        expectOffset(c, ub, 50, 50);
        expectTopLeft(ub, 300, 200);
        expectTopLeft(c, 350, 250);
    }

    // 23b. alignment topLeft places child at origin
    void testAlignmentTopLeft()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("s").width(200).height(200).child(
                UnconstrainedBox("ub")
                    .alignment(Alignment::topLeft)
                    .child(Container("c").width(100).height(100)))))
            .layout();

        auto *ub = screen.findByKey("ub");
        auto *c = screen.findByKey("c");
        assert(ub && c);

        expectSize(ub, 200, 200);
        // child 自然尺寸仍为 100x100
        expectSize(c, 100, 100);
        // topLeft -> 偏移 (0, 0)
        expectOffset(c, ub, 0, 0);
        expectTopLeft(c, 300, 200);
    }

    // 23c. alignment bottomRight with padding-like inset
    void testAlignmentBottomRight()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("s").width(200).height(200).child(
                UnconstrainedBox("ub")
                    .alignment(Alignment::bottomRight)
                    .child(Container("c").width(100).height(100)))))
            .layout();

        auto *ub = screen.findByKey("ub");
        auto *c = screen.findByKey("c");
        assert(ub && c);

        expectSize(c, 100, 100);
        // bottomRight -> (200-100, 200-100) = (100, 100)
        expectOffset(c, ub, 100, 100);
        expectTopLeft(c, 400, 300);
    }
} // namespace test_UnconstrainedBox

void test_UnconstrainedBox_api()
{
    using namespace test_UnconstrainedBox;
    runStage("23a UnconstrainedBox child keeps its natural size, not stretched",
             testChildKeepsNaturalSize);
    runStage("23b UnconstrainedBox alignment topLeft places child at origin",
             testAlignmentTopLeft);
    runStage("23c UnconstrainedBox alignment bottomRight with padding-like inset",
             testAlignmentBottomRight);
}

// =========================================================================
// 24. LimitedBox：无界约束下限制尺寸
//    算法：仅当父约束 maxWidth/maxHeight == infinity 时，才用自身的
//          maxWidth/maxHeight 替换该方向；有界的方向完全不动。逐维独立生效。
//    前置依赖：OverflowBox（Group 22 已测试），用于构造真正的无界约束。
//    注意：OverflowBox 必须显式设置 min = 0，否则 min 会从父约束继承，
//          LimitedBox 看到的就不是真正无界的约束。
// =========================================================================
namespace test_LimitedBox
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;
    static constexpr auto inf = std::numeric_limits<double>::infinity();

    // 24a. ignored when parent constraints are bounded
    void testIgnoredWhenParentBounded()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(LimitedBox("lb").maxWidth(200).maxHeight(100).child(
                Container("c").width(500).height(500))))
            .layout();

        auto *lb = screen.findByKey("lb");
        auto *c = screen.findByKey("c");
        assert(lb && c);

        // Center 提供有界松约束 0..800 / 0..600，max 都不是 infinity
        // LimitedBox 被完全忽略，child 保持 500x500
        expectSize(lb, 500, 500);
        expectSize(c, 500, 500);
        expectOffset(c, lb, 0, 0);
        // 绝对坐标：(800-500)/2 = 150, (600-500)/2 = 50
        expectTopLeft(lb, 150, 50);
    }

    // 24b. applies when parent gives truly unbounded constraints
    void testAppliesWhenTrulyUnbounded()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("s").width(300).height(300).child(
                OverflowBox("ob")
                    // 必须显式 min: 0，否则 min 会从 SizedBox 继承为 300
                    .minWidth(0)
                    .maxWidth(inf)
                    .minHeight(0)
                    .maxHeight(inf)
                    .child(LimitedBox("lb").maxWidth(200).maxHeight(100).child(
                        Container("c").width(500).height(500))))))
            .layout();

        auto *ob = screen.findByKey("ob");
        auto *lb = screen.findByKey("lb");
        auto *c = screen.findByKey("c");
        assert(ob && lb && c);

        // OverflowBox 自身 300x300
        expectSize(ob, 300, 300);
        // LimitedBox 看到 maxWidth/maxHeight == infinity，替换为 200x100
        expectSize(lb, 200, 100);
        // child 在 0..200 x 0..100 约束下收缩到 200x100
        expectSize(c, 200, 100);
        expectOffset(c, lb, 0, 0);
        // OverflowBox 默认居中，200x100 在 300x300 中 -> 偏移 (50, 100)
        expectOffset(lb, ob, 50, 100);
        // 绝对坐标：SizedBox 在 (250,150)，ob 同位置，lb 在 (300,250)
        expectTopLeft(ob, 250, 150);
        expectTopLeft(lb, 300, 250);
    }

    // 24c. only applies to the unbounded dimension
    void testOnlyUnboundedDimension()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("s").width(200).height(200).child(
                OverflowBox("ob")
                    // 宽度无界，高度有界 200
                    .minWidth(0)
                    .maxWidth(inf)
                    .minHeight(0)
                    .maxHeight(200)
                    .child(LimitedBox("lb").maxWidth(100).maxHeight(50).child(
                        Container("c").width(300).height(300))))))
            .layout();

        auto *ob = screen.findByKey("ob");
        auto *lb = screen.findByKey("lb");
        auto *c = screen.findByKey("c");
        assert(ob && lb && c);

        expectSize(ob, 200, 200);
        // 宽度无界 -> maxWidth 被替换为 100
        // 高度有界 (maxHeight = 200) -> maxHeight: 50 被忽略
        // 子级约束变成 0..100 x 0..200，child 收缩到 100x200
        expectSize(lb, 100, 200);
        expectSize(c, 100, 200);
        // OverflowBox 默认居中，100x200 在 200x200 中 -> 偏移 (50, 0)
        expectOffset(lb, ob, 50, 0);
        // 绝对坐标：SizedBox 在 (300,200)，ob 同位置，lb 在 (350,200)
        expectTopLeft(lb, 350, 200);
    }
} // namespace test_LimitedBox

void test_LimitedBox_api()
{
    using namespace test_LimitedBox;
    runStage("24a LimitedBox ignored when parent constraints are bounded",
             testIgnoredWhenParentBounded);
    runStage("24b LimitedBox applies when parent gives truly unbounded constraints",
             testAppliesWhenTrulyUnbounded);
    runStage("24c LimitedBox only applies to the unbounded dimension",
             testOnlyUnboundedDimension);
}

// =========================================================================
// 25. AspectRatio：按宽高比计算尺寸
//    算法：先按 maxWidth 试算高度；若超 maxHeight，改用 maxHeight 反推宽度；
//          最后用父约束 constrain。child 收到 tight 约束。
//    注意：AspectRatio 直接收到 tight 约束时会直接使用 tight 尺寸，
//          不按比例计算。因此测试里用 Center 给松约束。
// =========================================================================
namespace test_AspectRatio
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    // 25a. width drives height when width fits
    void testWidthDrivesHeight()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("s").width(300).height(300).child(
                Center().child(AspectRatio("ar", 2.0).child(Container("c"))))))
            .layout();

        auto *ar = screen.findByKey("ar");
        auto *c = screen.findByKey("c");
        assert(ar && c);

        // 松约束 0..300 x 0..300，宽 300 -> 高 150（2:1），未超 maxHeight
        expectSize(ar, 300, 150);
        // child 被 tight 约束拉伸到 300x150
        expectSize(c, 300, 150);
        expectOffset(c, ar, 0, 0);
        // SizedBox 在 (250,150)，AspectRatio 在 300x300 中纵向居中：(300-150)/2 = 75
        expectTopLeft(ar, 250, 225);
    }

    // 25b. height constraint limits width
    void testHeightLimitsWidth()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("s").width(300).height(100).child(
                Center().child(AspectRatio("ar", 2.0).child(Container("c"))))))
            .layout();

        auto *ar = screen.findByKey("ar");
        assert(ar);

        // 松约束 0..300 x 0..100。宽 300 -> 高 150 超过 maxHeight 100，
        // 改用高 100 -> 宽 200
        expectSize(ar, 200, 100);
        // SizedBox 在 (250,250)，AspectRatio 在 300 宽中居中：(300-200)/2 = 50
        expectTopLeft(ar, 300, 250);
    }

    // 25c. passes tight constraints to child
    void testPassesTightConstraintsToChild()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(
                Center().child(SizedBox("s").width(200).height(200).child(Center().child(
                    AspectRatio("ar", 1.0).child(Container("c").width(50).height(50))))))
            .layout();

        auto *ar = screen.findByKey("ar");
        auto *c = screen.findByKey("c");
        assert(ar && c);

        // 1:1，父约束 200x200 松 -> AspectRatio 200x200
        expectSize(ar, 200, 200);
        // child 被 tight 约束拉伸到 200x200（自己声明的 50x50 被忽略）
        expectSize(c, 200, 200);
        expectOffset(c, ar, 0, 0);
        // SizedBox 在 (300,200)，AspectRatio 填满 SizedBox
        expectTopLeft(ar, 300, 200);
    }
} // namespace test_AspectRatio

void test_AspectRatio_api()
{
    using namespace test_AspectRatio;
    runStage("25a AspectRatio width drives height when width fits",
             testWidthDrivesHeight);
    runStage("25b AspectRatio height constraint limits width", testHeightLimitsWidth);
    runStage("25c AspectRatio passes tight constraints to child",
             testPassesTightConstraintsToChild);
}
// =========================================================================
// 26. FractionallySizedBox：按父约束比例计算尺寸
//    算法：把父约束的 min/max 分别乘以 factor 作为 child 的约束；
//          child 在该约束下布局；
//          自身 size = 父约束.constrain(child.size)；
//          child 在自身范围内按 alignment 定位。
//    注意：父级 tight 时，factor 后 child 仍 tight；
//          父级 loose 时，factor 后 child 仍 loose。
// =========================================================================
namespace test_FractionallySizedBox
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    // 26a. tight parent, both factors shrink child
    void testBothFactorsShrinkChild()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("s").width(400).height(300).child(
                FractionallySizedBox("fsb").widthFactor(0.5).heightFactor(0.5).child(
                    Container("c")))))
            .layout();

        auto *fsb = screen.findByKey("fsb");
        auto *c = screen.findByKey("c");
        assert(fsb && c);

        // 父 tight 400x300。inner: minWidth=400*0.5=200, maxWidth=200
        //               inner: minHeight=300*0.5=150, maxHeight=150
        // child 被 tight 约束到 200x150
        expectSize(c, 200, 150);
        // 自身仍被父 tight 约束为 400x300
        expectSize(fsb, 400, 300);
        // 默认 alignment.center：(400-200)/2 = 100, (300-150)/2 = 75
        expectOffset(c, fsb, 100, 75);
        // 绝对坐标：SizedBox 在 (200,150)，child 在 (300,225)
        expectTopLeft(fsb, 200, 150);
        expectTopLeft(c, 300, 225);
    }

    // 26b. tight parent, only widthFactor
    void testOnlyWidthFactor()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("s").width(400).height(300).child(
                FractionallySizedBox("fsb")
                    .widthFactor(0.5)
                    // heightFactor 为 null -> 高度约束原样传递（仍是 tight 300）
                    .child(Container("c")))))
            .layout();

        auto *fsb = screen.findByKey("fsb");
        auto *c = screen.findByKey("c");
        assert(fsb && c);

        // inner 约束：width 400*0.5=200 的 tight；height 仍是 tight 300
        expectSize(c, 200, 300);
        expectSize(fsb, 400, 300);
        // 只有宽度需要居中：(400-200)/2 = 100，高度差为 0
        expectOffset(c, fsb, 100, 0);
        expectTopLeft(c, 300, 150);
    }

    // 26c. tight parent, only heightFactor
    void testOnlyHeightFactor()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("s").width(400).height(300).child(
                FractionallySizedBox("fsb")
                    // widthFactor 为 null -> 宽度约束原样传递（仍是 tight 400）
                    .heightFactor(0.5)
                    .child(Container("c")))))
            .layout();

        auto *fsb = screen.findByKey("fsb");
        auto *c = screen.findByKey("c");
        assert(fsb && c);

        // inner 约束：height 300*0.5=150 的 tight；width 仍是 tight 400
        expectSize(c, 400, 150);
        expectSize(fsb, 400, 300);
        // 只有高度需要居中：(300-150)/2 = 75，宽度差为 0
        expectOffset(c, fsb, 0, 75);
        expectTopLeft(c, 200, 225);
    }
} // namespace test_FractionallySizedBox

void test_FractionallySizedBox_api()
{
    using namespace test_FractionallySizedBox;
    runStage("26a FractionallySizedBox tight parent, both factors shrink child",
             testBothFactorsShrinkChild);
    runStage("26b FractionallySizedBox tight parent, only widthFactor",
             testOnlyWidthFactor);
    runStage("26c FractionallySizedBox tight parent, only heightFactor",
             testOnlyHeightFactor);
}
/*
覆盖的主要模块：

Container 基础盒模型

    固定宽高、无 child；

    margin 是否计入自身尺寸；

    padding/border 在无 child 时是否影响自身尺寸；

    无 child、无尺寸时在 loose/tight 约束下如何填充。

Container + child

    padding/border 如何压缩 child；

    child 相对 Container 的偏移；

    Container 收缩到 child 尺寸时，padding/border 如何累加。

alignment

    九种 Alignment 的偏移；

    alignment 会把 child 约束从 tight 变成 loose；

    无 alignment 时 child 容易被拉伸，有 alignment 时 child 可保持自然尺寸。

SizedBox / Padding

    SizedBox 固定宽高时给 child tight 约束，强制拉伸；

    只指定宽或高时，另一维由 child 决定；

    Padding 增加 insets，tight 下外框不变，loose 下收缩到 child + padding。

嵌套与边界

    margin/padding/border 多层累加；

    padding 大于可用空间时 child 被压到零；

    多层 Container、Padding、SizedBox 综合计算。

约束修改类组件

    ConstrainedBox：合并父约束，只能收紧不能放松；无 child 时 size = constrain(Size.zero)。

    OverflowBox：自身受父约束，child 使用指定约束，可超出自身范围，按 alignment 定位。

    UnconstrainedBox：给 child 无界约束，让 child 保持自然尺寸，自身仍受父约束。

    LimitedBox：只在父约束对应方向为 infinity 时生效，逐维独立。

    AspectRatio：按宽高比计算尺寸，child 收到 tight 约束。

    FractionallySizedBox：把父约束的 min/max 按 factor 缩放后给 child，自身仍受父约束，child 按 alignment 定位。

整体规律可以概括为：

    父约束决定自身 size；

    padding/border/margin 决定 child 可用空间和偏移；

    alignment 决定 child 在剩余空间中的位置，并影响传给 child 的约束松紧；

    SizedBox / AspectRatio / FractionallySizedBox 等会主动给 child 施加 tight 或缩放后的约束；

    OverflowBox / UnconstrainedBox / LimitedBox 用于特殊约束场景
*/
int main()
try
{
    test_BoxConstraints_api();

    std::cout << "\n========== BoxConstraints all pass ==========\n\n";

    test_Container_NoChild_api();
    std::cout << "\n========== Group 1 all pass ==========\n\n";

    test_Container_Child_api();
    std::cout << "\n========== Group 2 all pass ==========\n\n";

    test_Container_Alignment_api();
    std::cout << "\n========== Group 3 all pass ==========\n\n";

    test_SizedBox_api();
    std::cout << "\n========== Group 4 all pass ==========\n\n";

    test_Padding_api();
    std::cout << "\n========== Group 5 all pass ==========\n\n";

    test_Nesting_api();
    std::cout << "\n========== Group 6 all pass ==========\n\n";

    test_Boundary_api();
    std::cout << "\n========== Group 7 all pass ==========\n\n";

    test_Comprehensive_api();
    std::cout << "\n========== Group 8 all pass ==========\n\n";

    test_ConstraintPassing_api();
    std::cout << "\n========== Group 9 all pass ==========\n\n";

    test_Padding_Constraints_api();
    std::cout << "\n========== Group 10 all pass ==========\n\n";

    test_Container_FixedAlignment_api();
    std::cout << "\n========== Group 11 all pass ==========\n\n";

    test_SizedBox_Tight_api();
    std::cout << "\n========== Group 12 all pass ==========\n\n";

    test_NestedMargins_api();
    std::cout << "\n========== Group 13 all pass ==========\n\n";

    test_Padding_Clamp_api();
    std::cout << "\n========== Group 14 all pass ==========\n\n";

    test_Extreme_api();
    std::cout << "\n========== Group 15 all pass ==========\n\n";

    test_Container_Alignment_All_api();
    std::cout << "\n========== Group 16 all pass ==========\n\n";

    test_Single_Dimension_api();
    std::cout << "\n========== Group 17 all pass ==========\n\n";

    test_SizedBox_NoChild_WithSize_api();
    std::cout << "\n========== Group 18 all pass ==========\n\n";

    test_Padding_NoChild_api();
    std::cout << "\n========== Group 19 all pass ==========\n\n";

    test_Container_NoChild_MarginAndInsets_api();
    std::cout << "\n========== Group 20 all pass ==========\n\n";

    test_ConstrainedBox_api();
    std::cout << "\n========== Group 21 all pass ==========\n\n";

    test_OverflowBox_api();
    std::cout << "\n========== Group 22 (OverflowBox) all pass ==========\n\n";

    test_UnconstrainedBox_api();
    std::cout << "\n========== Group 23 (UnconstrainedBox) all pass ==========\n\n";

    test_LimitedBox_api();
    std::cout << "\n========== Group 24 (LimitedBox) all pass ==========\n\n";

    test_AspectRatio_api();
    std::cout << "\n========== Group 25 (AspectRatio) all pass ==========\n\n";

    test_FractionallySizedBox_api();
    std::cout << "\n========== Group 26 (FractionallySizedBox) all pass ==========\n\n";

    std::cout << "main done\n";
    return 0;
}
catch (const std::exception &e)
{
    std::cerr << "Exception: " << e.what() << '\n';
    return 1;
}
catch (...)
{
    std::cerr << "Unknown exception\n";
    return 1;
} // NOLINTEND