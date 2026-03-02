// g++ -o arrow_demo arrow_demo.cpp -lagg -lfreetype
// 运行后生成 arrow_demo.ppm
#include <fstream>
#include <vector>
#include <cmath>
#include <iostream>
#include <agg_basics.h>
#include <agg_pixfmt_rgb.h>
#include <agg_rendering_buffer.h>
#include <agg_rasterizer_scanline_aa.h>
#include <agg_scanline_u.h>
#include <agg_renderer_scanline.h>
#include <agg_path_storage.h>
#include <agg_conv_stroke.h>

const unsigned WIDTH = 800;
const unsigned HEIGHT = 600;

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

    // 设置光栅化器和扫描线
    agg::rasterizer_scanline_aa<> ras;
    agg::scanline_u8 sl;

    // 定义线段端点（逻辑坐标，原点在左上角，y向下为正，与AGG默认一致）
    double x1 = 100, y1 = 100;
    double x2 = 500, y2 = 400;

    // 1. 绘制线段（使用 path_storage + conv_stroke）
    agg::path_storage line_path;
    line_path.move_to(x1, y1);
    line_path.line_to(x2, y2);
    agg::conv_stroke<agg::path_storage> stroke(line_path);
    stroke.width(2.0); // 线宽2像素
    ras.reset();
    ras.add_path(stroke);
    ren_solid.color(agg::rgba8(255, 0, 0)); // 红色线段
    agg::render_scanlines(ras, sl, ren_solid);

    // 2. 绘制箭头三角形（填充）
    double arrow_size = 20.0;
    double dx = x2 - x1;
    double dy = y2 - y1;
    double len = std::sqrt(dx * dx + dy * dy);
    if (len > 1e-6)
    {
        double angle = std::atan2(dy, dx);
        double xa1 = x2 + arrow_size * std::cos(angle + agg::pi * 5.0 / 6.0);
        double ya1 = y2 + arrow_size * std::sin(angle + agg::pi * 5.0 / 6.0);
        double xa2 = x2 + arrow_size * std::cos(angle - agg::pi * 5.0 / 6.0);
        double ya2 = y2 + arrow_size * std::sin(angle - agg::pi * 5.0 / 6.0);

        agg::path_storage arrow_path;
        arrow_path.move_to(x2, y2);
        arrow_path.line_to(xa1, ya1);
        arrow_path.line_to(xa2, ya2);
        arrow_path.close_polygon();

        ras.reset();
        ras.add_path(arrow_path);
        ren_solid.color(agg::rgba8(0, 255, 0)); // 绿色箭头填充
        agg::render_scanlines(ras, sl, ren_solid);
    }

    // 3. 输出 PPM
    std::ofstream out("arrow_demo.ppm", std::ios::binary);
    out << "P6\n" << WIDTH << " " << HEIGHT << "\n255\n";
    out.write(reinterpret_cast<char *>(&buffer[0]), buffer.size());
    out.close();

    std::cout << "已生成 arrow_demo.ppm，可用 ImageMagick 查看。\n";
    return 0;
}