#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// test_intent6_ui_controller.hpp —— controller 层（后端接口）
//
//   前端（Widget）只看到 ui_controller::client 和它的特性方法：
//     client ui{ctx.drawRecorder, w, h};
//     ui.panel(...); ui.button(...); ui.cell(...); ui.glyphs(...);
//
//   client 内部持有 service::canvas，service 调用 dao 编码；前端不碰
//   canvas / ABI / NDC。
// ═══════════════════════════════════════════════════════════════════════════

#include "test_intent6_ui_service.hpp"

#include <span>

namespace ui_controller
{

struct palette
{
    glm::vec4 surface{0.10F, 0.11F, 0.14F, 0.97F};
    glm::vec4 surface_alt{0.15F, 0.16F, 0.21F, 0.95F};
    glm::vec4 text{0.86F, 0.88F, 0.93F, 1.0F};
    glm::vec4 accent{0.45F, 0.72F, 1.0F, 1.0F};
    glm::vec4 border{1.0F, 1.0F, 1.0F, 0.14F};
    glm::vec4 shadow{0.0F, 0.0F, 0.0F, 0.35F};
};

class client
{
  public:
    client(shader_data::CommandBuilder &builder, float width, float height)
        : canvas_{builder, width, height}
    {
    }

    [[nodiscard]] const palette &colors() const noexcept { return p_; }
    [[nodiscard]] std::uint32_t count() const noexcept { return canvas_.count(); }

    // 面板：填充 + 1px 边框 + 外阴影（同一个 Shape 程序）
    void panel(const ui_dao::rect_px &r, float radius)
    {
        canvas_.shape(ui_dao::sdf_shape(r, canvas_.view(), radius, p_.surface,
                                        p_.shadow, 4.0F, 12.0F));
        canvas_.shape(ui_dao::shape_border(r, canvas_.view(), radius, p_.border, 1.0F));
    }

    // 按钮：阴影 + 填充 + 边框
    void button(const ui_dao::rect_px &r, float radius, bool pressed, bool hovered)
    {
        const glm::vec4 fill = hovered ? p_.accent : p_.surface_alt;
        canvas_.shape(ui_dao::sdf_shape(r, canvas_.view(), radius, fill, p_.shadow,
                                        pressed ? 1.0F : 3.0F,
                                        pressed ? 2.0F : 6.0F));
        canvas_.shape(ui_dao::shape_border(r, canvas_.view(), radius, p_.border, 1.0F));
    }

    // 单元格：选中/悬停改填充
    void cell(const ui_dao::rect_px &r, float radius, bool selected, bool hovered)
    {
        glm::vec4 fill = p_.surface_alt;
        if (selected)
            fill = glm::vec4{0.19F, 0.31F, 0.44F, 0.98F};
        else if (hovered)
            fill = glm::vec4{0.19F, 0.22F, 0.30F, 0.98F};
        canvas_.shape(ui_dao::sdf_shape(r, canvas_.view(), radius, fill));
        canvas_.shape(ui_dao::shape_border(r, canvas_.view(), radius, p_.border, 1.0F));
    }

    void divider(const ui_dao::rect_px &r)
    {
        canvas_.shape(ui_dao::sdf_shape(r, canvas_.view(), 0.0F, p_.border));
    }

    void focus_ring(const ui_dao::rect_px &r)
    {
        canvas_.shape(ui_dao::shape_border(r, canvas_.view(), 8.0F, p_.accent, 2.0F));
    }

    // 文本：字形由前端整形后送来（controller 不依赖字体系统）
    void glyphs(std::span<const shader_data::TexQuad> g)
    {
        for (const auto &one : g)
            canvas_.glyph(one);
    }

    // 参考场景：一个 Widget 组合多个特性
    void reference()
    {
        panel({40.0F, 80.0F, 640.0F, 420.0F}, 12.0F);
        for (int i = 0; i < 4; ++i)
            cell({60.0F, 110.0F + static_cast<float>(i) * 44.0F, 140.0F, 40.0F}, 6.0F,
                 i == 1, i == 2);
        button({240.0F, 110.0F, 120.0F, 36.0F}, 8.0F, false, true);
        button({240.0F, 160.0F, 120.0F, 36.0F}, 8.0F, true, false);
        focus_ring({240.0F, 160.0F, 120.0F, 36.0F});
        for (int i = 0; i < 3; ++i)
            divider({60.0F, 320.0F + static_cast<float>(i) * 30.0F, 560.0F, 1.0F});
    }

  private:
    ui_service::canvas<shader_data::CommandBuilder> canvas_;
    palette p_{};
};

} // namespace ui_controller
