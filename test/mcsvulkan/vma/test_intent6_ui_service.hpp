#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// test_intent6_ui_service.hpp —— service 层：把 DAO 编好的实例交给 builder
//
//   这一层只做三件事：
//     1. 持有 builder 和视口（像素 → NDC 的换算在 DAO 里用 view 完成）；
//     2. 类型化 add（glyph / shape）；
//     3. 计数（给上层/自测用）。
//   合批/间接绘制在 CommandBuilder 里，这里不重复实现。
// ═══════════════════════════════════════════════════════════════════════════

#include "test_intent6_ui_dao.hpp"

#include <cstdint>

namespace ui_service
{

template <class Builder>
class canvas
{
  public:
    canvas(Builder &builder, float width, float height)
        : builder_(builder), view_{width, height}
    {
    }

    [[nodiscard]] const ui_dao::view &view() const noexcept { return view_; }
    [[nodiscard]] std::uint32_t count() const noexcept { return count_; }

    void glyph(const shader_data::TexQuad &g)
    {
        builder_.addInstance(g);
        ++count_;
    }
    void shape(const shader_data::Shape &s)
    {
        builder_.addInstance(s);
        ++count_;
    }

  private:
    Builder &builder_;
    ui_dao::view view_;
    std::uint32_t count_{0};
};

} // namespace ui_service
