// g++ -o arrow_label arrow_label.cpp -lagg -lfreetype
// 运行后生成 arrow_label.ppm
#include <fstream>
#include <vector>
#include <cmath>
#include <string>
#include <iostream>
#include <agg_basics.h>
#include <agg_pixfmt_rgb.h>
#include <agg_rendering_buffer.h>
#include <agg_rasterizer_scanline_aa.h>
#include <agg_scanline_u.h>
#include <agg_renderer_scanline.h>
#include <agg_path_storage.h>
#include <agg_conv_stroke.h>
#include <agg_conv_transform.h>
#include <agg_conv_curve.h>
#include <agg_font_freetype.h>
#include <agg_math.h>
#include <agg_trans_affine.h>

const unsigned WIDTH = 800;
const unsigned HEIGHT = 600;

// 计算字符串总宽度（用于居中）
double get_text_width(agg::font_cache_manager<agg::font_engine_freetype_int32> &fman,
                      const std::string &text)
{
    double w = 0;
    for (char c : text)
    {
        const agg::glyph_cache *glyph = fman.glyph(c);
        if (glyph)
            w += glyph->advance_x;
    }
    return w;
}

// ------------------------------------------------------------------
// 封装类：ArrowLabel
// 负责绘制带箭头的线段和可定位的平行文字
// ------------------------------------------------------------------
class ArrowLabel
{
  public:
    ArrowLabel(agg::font_engine_freetype_int32 &feng,
               agg::font_cache_manager<agg::font_engine_freetype_int32> &fman, double x1,
               double y1, double x2, double y2, agg::rgba8 line_color,
               agg::rgba8 text_color, double line_width, double arrow_size,
               const std::string &text)
        : m_feng(feng), m_fman(fman), m_x1(x1), m_y1(y1), m_x2(x2), m_y2(y2),
          m_line_color(line_color), m_text_color(text_color), m_line_width(line_width),
          m_arrow_size(arrow_size), m_text(text)
    {
    }

    // 设置文字在线段上的位置比例 t (0~1)
    void set_position(double t)
    {
        m_t = t;
    }

    // 设置垂直于线段的偏移量（正值左侧，负值右侧）
    void set_offset(double offset)
    {
        m_offset = offset;
    }

    // 绘制到指定的渲染器
    void draw(agg::rasterizer_scanline_aa<> &ras, agg::scanline_u8 &sl,
              agg::renderer_scanline_aa_solid<agg::renderer_base<agg::pixfmt_rgb24>>
                  &ren_solid) const
    {
        // 计算线段相关量
        double dx = m_x2 - m_x1;
        double dy = m_y2 - m_y1;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6)
            return;

        double angle = std::atan2(dy, dx);
        double nx = -std::sin(angle); // 法线方向（逆时针旋转90度）
        double ny = std::cos(angle);

        // ---------- 绘制线段 ----------
        agg::path_storage line_path;
        line_path.move_to(m_x1, m_y1);
        line_path.line_to(m_x2, m_y2);
        agg::conv_stroke<agg::path_storage> stroke(line_path);
        stroke.width(m_line_width);
        ras.reset();
        ras.add_path(stroke);
        ren_solid.color(m_line_color);
        agg::render_scanlines(ras, sl, ren_solid);

        // ---------- 绘制箭头（填充三角形） ----------
        // 箭头始终在终点
        double xa1 = m_x2 + m_arrow_size * std::cos(angle + agg::pi * 5.0 / 6.0);
        double ya1 = m_y2 + m_arrow_size * std::sin(angle + agg::pi * 5.0 / 6.0);
        double xa2 = m_x2 + m_arrow_size * std::cos(angle - agg::pi * 5.0 / 6.0);
        double ya2 = m_y2 + m_arrow_size * std::sin(angle - agg::pi * 5.0 / 6.0);

        agg::path_storage arrow_path;
        arrow_path.move_to(m_x2, m_y2);
        arrow_path.line_to(xa1, ya1);
        arrow_path.line_to(xa2, ya2);
        arrow_path.close_polygon();

        ras.reset();
        ras.add_path(arrow_path);
        ren_solid.color(m_line_color);
        agg::render_scanlines(ras, sl, ren_solid);

        // ---------- 绘制平行文字 ----------
        if (m_text.empty())
            return;

        m_feng.height(16.0);
        m_feng.width(16.0);
        m_feng.flip_y(true); // 屏幕坐标系

        // 文字中心位置：线段上的点 (x1 + t*dx, y1 + t*dy) 加上法向偏移
        double cx = m_x1 + m_t * dx + nx * m_offset;
        double cy = m_y1 + m_t * dy + ny * m_offset;

        // 文字总宽度
        double text_width = get_text_width(m_fman, m_text);

        // 构建总变换矩阵：将原点文字先平移 (-width/2, 0)
        // 使中心对齐原点，然后旋转，然后平移到 (cx, cy)
        agg::trans_affine mtx_total;
        mtx_total.reset();
        mtx_total *= agg::trans_affine_translation(-text_width / 2.0, 0.0);
        mtx_total *= agg::trans_affine_rotation(angle);
        mtx_total *= agg::trans_affine_translation(cx, cy);

        // 逐个字符渲染
        double cur_x = 0.0; // 当前字符在水平方向上的累积偏移（相对于文字起点）
        for (char c : m_text)
        {
            const agg::glyph_cache *glyph = m_fman.glyph(c);
            if (!glyph || glyph->data_type != agg::glyph_data_outline)
            {
                if (glyph)
                    cur_x += glyph->advance_x;
                continue;
            }

            // 获取字形路径，并应用曲线光滑
            m_fman.init_embedded_adaptors(glyph, 0, 0);
            typedef agg::conv_curve<agg::font_cache_manager<
                agg::font_engine_freetype_int32>::path_adaptor_type>
                curve_t;
            curve_t curve(m_fman.path_adaptor());

            // 构建当前字符的变换矩阵：先平移到 (cur_x, 0)，再应用总变换
            agg::trans_affine mtx_char;
            mtx_char.reset();
            mtx_char *= agg::trans_affine_translation(cur_x, 0.0);
            mtx_char *= mtx_total;

            typedef agg::conv_transform<curve_t, agg::trans_affine> transform_t;
            transform_t trans(curve, mtx_char);

            ras.reset();
            ras.add_path(trans);
            ren_solid.color(m_text_color);
            agg::render_scanlines(ras, sl, ren_solid);

            cur_x += glyph->advance_x;
        }
    }

  private:
    agg::font_engine_freetype_int32 &m_feng;
    agg::font_cache_manager<agg::font_engine_freetype_int32> &m_fman;
    double m_x1, m_y1, m_x2, m_y2;
    agg::rgba8 m_line_color;
    agg::rgba8 m_text_color;
    double m_line_width;
    double m_arrow_size;
    std::string m_text;
    double m_t = 0.5;    // 默认中点
    double m_offset = 0; // 默认无偏移
};

int main()
{
    // 创建图像缓冲区（白色背景）
    std::vector<unsigned char> buffer(WIDTH * HEIGHT * 3, 255);
    agg::rendering_buffer rbuf(&buffer[0], WIDTH, HEIGHT, WIDTH * 3);
    agg::pixfmt_rgb24 pixf(rbuf);
    agg::renderer_base<agg::pixfmt_rgb24> ren_base(pixf);
    agg::renderer_scanline_aa_solid<agg::renderer_base<agg::pixfmt_rgb24>> ren_solid(
        ren_base);
    ren_base.clear(agg::rgba8(255, 255, 255));

    // 初始化字体引擎
    agg::font_engine_freetype_int32 feng;
    agg::font_cache_manager<agg::font_engine_freetype_int32> fman(feng);

    // 尝试加载字体（根据系统调整）
    const char *font_paths[] = {
        "C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/times.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf", "times.ttf"};
    bool font_loaded = false;
    for (const char *path : font_paths)
    {
        if (feng.load_font(path, 0, agg::glyph_ren_outline))
        {
            std::cout << "成功加载字体: " << path << std::endl;
            font_loaded = true;
            break;
        }
    }
    if (!font_loaded)
    {
        std::cerr << "警告：无法加载字体，文字将不会显示。\n";
    }

    agg::rasterizer_scanline_aa<> ras;
    agg::scanline_u8 sl;

    // 创建多个 ArrowLabel 对象并设置不同位置
    // 箭头1：水平，文字在起点附近 (t=0.2)，上方偏移20
    ArrowLabel arrow1(feng, fman, 100, 200, 300, 200, agg::rgba8(255, 0, 0), // 红色线段
                      agg::rgba8(0, 0, 255),                                 // 蓝色文字
                      2.0, 15.0, "Start");
    arrow1.set_position(0.2);
    arrow1.set_offset(20.0);
    arrow1.draw(ras, sl, ren_solid);

    // 箭头2：水平，文字在终点附近 (t=0.8)，下方偏移 -20
    ArrowLabel arrow2(feng, fman, 100, 300, 300, 300, agg::rgba8(0, 255, 0), // 绿色线段
                      agg::rgba8(255, 0, 255),                               // 紫色文字
                      2.0, 15.0, "End");
    arrow2.set_position(0.8);
    arrow2.set_offset(-20.0);
    arrow2.draw(ras, sl, ren_solid);

    // 箭头3：斜线，文字在中点 (t=0.5)，上方偏移20
    ArrowLabel arrow3(feng, fman, 400, 100, 600, 250, agg::rgba8(0, 0, 255), // 蓝色线段
                      agg::rgba8(255, 165, 0),                               // 橙色文字
                      2.0, 15.0, "Mid");
    arrow3.set_position(0.5);
    arrow3.set_offset(20.0);
    arrow3.draw(ras, sl, ren_solid);

    // 箭头4：垂直，文字在 0.3 处，右侧偏移20
    ArrowLabel arrow4(feng, fman, 500, 400, 500, 200, agg::rgba8(255, 255, 0), // 黄色线段
                      agg::rgba8(128, 0, 128),                                 // 紫色文字
                      2.0, 15.0, "Vertical");
    arrow4.set_position(0.3);
    arrow4.set_offset(20.0);
    arrow4.draw(ras, sl, ren_solid);

    // 输出 PPM
    std::ofstream out("arrow_demo2.ppm", std::ios::binary);
    out << "P6\n" << WIDTH << " " << HEIGHT << "\n255\n";
    out.write(reinterpret_cast<char *>(&buffer[0]), buffer.size());
    out.close();

    std::cout << "已生成 arrow_label.ppm，可用 ImageMagick 查看。\n";
    return 0;
}