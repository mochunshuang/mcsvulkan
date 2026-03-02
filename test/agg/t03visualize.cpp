//============================================================================
// Visualizing AGG Core Concepts (lion.cpp style)
//   - scanline rasterizer
//   - affine transformer
//   - basic renderers
// 生成三个 PPM 文件，每个 400x400，RGB24 格式。
//============================================================================

#include <cstdio>
#include <cstring>
#include <cmath>
#include <print>

// AGG 核心头文件（注意：pixel_formats.h 已经包含了必要的颜色和格式定义）
#include <agg_basics.h>
#include <agg_rendering_buffer.h>
#include <agg_rasterizer_scanline_aa.h>
#include <agg_scanline_p.h>
#include <agg_renderer_scanline.h>
#include <agg_path_storage.h>
#include <agg_conv_transform.h>
#include <agg_bounding_rect.h>
#include <ctrl/agg_slider_ctrl.h>
#include <platform/agg_platform_support.h>

// #define AGG_GRAY16
// #define AGG_GRAY32
#define AGG_BGR24
// #define AGG_BGR48
// #define AGG_RGB_AAA
// #define AGG_BGRA32
// #define AGG_RGBA32
// #define AGG_ARGB32
// #define AGG_ABGR32
// #define AGG_BGR96
// #define AGG_BGRA128
// #define AGG_RGB565
// #define AGG_RGB555
#include <pixel_formats.h>

// 根据 pixel_formats.h 的定义，我们获得以下类型：
// - pixfmt : 像素格式操作类（例如 agg::pixfmt_bgr24）
// - color_type : 像素格式对应的颜色类型（例如 agg::rgba8 或 agg::rgba16，但这里使用
// agg::rgba8 作为渲染颜色） 为简化，我们统一使用 agg::rgba8
// 设置颜色，渲染器会自动转换到像素格式。

// 定义常用的类型别名
using pixfmt = agg::pixfmt_bgr24; // 由 AGG_BGR24 定义
using renderer_base = agg::renderer_base<pixfmt>;
using renderer_solid = agg::renderer_scanline_aa_solid<renderer_base>;

constexpr unsigned WIDTH = 400;
constexpr unsigned HEIGHT = 400;
constexpr unsigned BYTES_PER_PIXEL = 3; // BGR24 也是 3 字节

//------------------------------------------------------------------------------
// 写入 PPM 文件（二进制 P6 格式）
// 注意：PPM 标准是 RGB 顺序，而我们缓冲区是 BGR 顺序，需要转换？
// 实际上 agg::pixfmt_bgr24 存储为 BGR，但写入 PPM 时应为 RGB。
// 为了正确显示，我们可以在写入时交换 R 和 B 分量。
// 或者直接写入，然后用支持 BGR 的查看器（如 ImageMagick 的 -swap 选项）。
// 这里我们选择写入时交换，使生成的文件能被普通看图软件正确识别。
//------------------------------------------------------------------------------
bool write_ppm(const unsigned char *buf, unsigned width, unsigned height,
               const char *file_name)
{
    FILE *fd = std::fopen(file_name, "wb");
    if (!fd)
        return false;
    std::fprintf(fd, "P6\n%u %u\n255\n", width, height);

    // 逐行写入，并交换 R 和 B
    for (unsigned y = 0; y < height; ++y)
    {
        const unsigned char *row = buf + y * width * BYTES_PER_PIXEL;
        for (unsigned x = 0; x < width; ++x)
        {
            // BGR -> RGB
            unsigned char b = row[x * 3 + 0];
            unsigned char g = row[x * 3 + 1];
            unsigned char r = row[x * 3 + 2];
            std::fputc(r, fd);
            std::fputc(g, fd);
            std::fputc(b, fd);
        }
    }
    std::fclose(fd);
    return true;
}

//------------------------------------------------------------------------------
// 清除缓冲区为指定颜色（这里用白色）
//------------------------------------------------------------------------------
void clear_buffer(unsigned char *buf, unsigned width, unsigned height)
{
    // BGR24 白色：B=255, G=255, R=255
    for (unsigned y = 0; y < height; ++y)
    {
        unsigned char *row = buf + y * width * BYTES_PER_PIXEL;
        for (unsigned x = 0; x < width; ++x)
        {
            row[x * 3 + 0] = 255; // B
            row[x * 3 + 1] = 255; // G
            row[x * 3 + 2] = 255; // R
        }
    }
}

//------------------------------------------------------------------------------
// 示例1：扫描线光栅化
// 绘制一个带有抗锯齿边缘的三角形，并在左侧绘制灰度条表示覆盖权重，
// 背景添加网格线表示扫描线行。
//------------------------------------------------------------------------------
void visualize_scanline_rasterizer()
{
    unsigned char *buffer = new unsigned char[WIDTH * HEIGHT * BYTES_PER_PIXEL];
    clear_buffer(buffer, WIDTH, HEIGHT);

    agg::rendering_buffer rbuf(buffer, WIDTH, HEIGHT, WIDTH * BYTES_PER_PIXEL);
    pixfmt pf(rbuf);
    renderer_base rb(pf);
    renderer_solid r(rb);

    agg::rasterizer_scanline_aa<> ras;
    agg::scanline_p8 sl;

    // 定义一个三角形
    agg::path_storage path;
    path.move_to(100, 100);
    path.line_to(300, 150);
    path.line_to(200, 350);
    path.close_polygon();

    // 设置颜色为半透明蓝色（alpha=200）
    agg::rgba8 color(0, 0, 255, 200); // R=0,G=0,B=255,Alpha=200

    // 添加路径并渲染
    ras.add_path(path);
    r.color(color);
    agg::render_scanlines(ras, sl, r);
    ras.reset();

    // 绘制扫描线辅助元素：水平网格线（浅灰色，每隔10像素）
    agg::rgba8 grid_color(220, 220, 220); // 浅灰色
    for (unsigned y = 0; y < HEIGHT; y += 10)
    {
        for (unsigned x = 0; x < WIDTH; ++x)
        {
            rb.copy_pixel(x, y, grid_color);
        }
    }

    // 在左侧绘制灰度渐变条，表示扫描线上像素的覆盖权重（模拟抗锯齿）
    for (unsigned y = 0; y < HEIGHT; ++y)
    {
        double cover = (double)y / HEIGHT; // 从0到1
        unsigned char gray = (unsigned char)(cover * 255);
        agg::rgba8 grad_color(gray, gray, gray); // 灰度
        for (unsigned x = 0; x < 20; ++x)
        { // 宽度20像素
            rb.copy_pixel(x, y, grad_color);
        }
    }

    write_ppm(buffer, WIDTH, HEIGHT, "scanline_rasterizer.ppm");
    delete[] buffer;
}

//------------------------------------------------------------------------------
// 示例2：仿射变换
// 绘制四个相同的小房子，分别应用原始、旋转、缩放、倾斜变换，
// 并在底部用色块标注。
//------------------------------------------------------------------------------
void visualize_affine_transformer()
{
    unsigned char *buffer = new unsigned char[WIDTH * HEIGHT * BYTES_PER_PIXEL];
    clear_buffer(buffer, WIDTH, HEIGHT);

    agg::rendering_buffer rbuf(buffer, WIDTH, HEIGHT, WIDTH * BYTES_PER_PIXEL);
    pixfmt pf(rbuf);
    renderer_base rb(pf);
    renderer_solid r(rb);
    agg::rasterizer_scanline_aa<> ras;
    agg::scanline_p8 sl;

    // 定义一个简单的房子（原点为中心，坐标范围[-30,30] x [-20,40]）
    agg::path_storage house;
    house.move_to(-30, -20); // 左下
    house.line_to(30, -20);  // 右下
    house.line_to(30, 20);   // 右上
    house.line_to(0, 40);    // 屋顶
    house.line_to(-30, 20);  // 左上
    house.close_polygon();

    // 四个变换及颜色（顺序：原始、旋转、缩放、倾斜）
    struct
    {
        agg::trans_affine mtx;
        agg::rgba8 color;
    } transforms[] = {
        // 原始 (红色) - 位于 (250,120)
        {agg::trans_affine_translation(250, 120), agg::rgba8(255, 0, 0)},
        // 旋转45度 (绿色) - 位于 (120,120)
        {agg::trans_affine_rotation(agg::pi / 4) *
             agg::trans_affine_translation(120, 120),
         agg::rgba8(0, 255, 0)},
        // 缩放 (1.5,0.8) (蓝色) - 位于 (120,280)
        {agg::trans_affine_scaling(1.5, 0.8) * agg::trans_affine_translation(120, 280),
         agg::rgba8(0, 0, 255)},
        // 倾斜 X (0.3) (黄色) - 位于 (280,280)
        {agg::trans_affine_skewing(0.3, 0.0) * agg::trans_affine_translation(280, 280),
         agg::rgba8(255, 255, 0)}};

    for (auto &t : transforms)
    {
        agg::conv_transform<agg::path_storage, agg::trans_affine> trans(house, t.mtx);
        ras.add_path(trans);
        r.color(t.color);
        agg::render_scanlines(ras, sl, r);
        ras.reset();
    }

    // 在底部绘制色块图例（每个色块10x10，间隔10像素）
    for (int i = 0; i < 4; ++i)
    {
        int x = 30 + i * 70;
        int y = 360;
        for (int dy = 0; dy < 10; ++dy)
        {
            for (int dx = 0; dx < 10; ++dx)
            {
                rb.copy_pixel(x + dx, y + dy, transforms[i].color);
            }
        }
    }

    write_ppm(buffer, WIDTH, HEIGHT, "affine_transformer.ppm");
    delete[] buffer;
}

//------------------------------------------------------------------------------
// 示例3：基础渲染器与透明度混合
// 绘制三个半透明的圆（红、绿、蓝），展示混合效果。
//------------------------------------------------------------------------------
void visualize_basic_renderers()
{
    unsigned char *buffer = new unsigned char[WIDTH * HEIGHT * BYTES_PER_PIXEL];
    clear_buffer(buffer, WIDTH, HEIGHT);

    agg::rendering_buffer rbuf(buffer, WIDTH, HEIGHT, WIDTH * BYTES_PER_PIXEL);
    pixfmt pf(rbuf);
    renderer_base rb(pf);
    renderer_solid r(rb);
    agg::rasterizer_scanline_aa<> ras;
    agg::scanline_p8 sl;

    // 辅助函数：添加一个圆（通过多边形近似）
    auto add_circle = [&](double cx, double cy, double radius) {
        const unsigned steps = 60;
        double ang_step = 2.0 * agg::pi / steps;
        ras.move_to_d(cx + radius, cy);
        for (unsigned i = 1; i <= steps; ++i)
        {
            double ang = i * ang_step;
            ras.line_to_d(cx + radius * cos(ang), cy + radius * sin(ang));
        }
    };

    // 红色圆 (alpha=128)
    add_circle(150, 150, 90);
    r.color(agg::rgba8(255, 0, 0, 128));
    agg::render_scanlines(ras, sl, r);
    ras.reset();

    // 绿色圆 (alpha=128)
    add_circle(250, 150, 90);
    r.color(agg::rgba8(0, 255, 0, 128));
    agg::render_scanlines(ras, sl, r);
    ras.reset();

    // 蓝色圆 (alpha=128)
    add_circle(200, 250, 90);
    r.color(agg::rgba8(0, 0, 255, 128));
    agg::render_scanlines(ras, sl, r);
    ras.reset();

    write_ppm(buffer, WIDTH, HEIGHT, "basic_renderers.ppm");
    delete[] buffer;
}

//------------------------------------------------------------------------------
// 主函数
//------------------------------------------------------------------------------
int agg_main(int argc, char *argv[])
{
    visualize_scanline_rasterizer();
    visualize_affine_transformer();
    visualize_basic_renderers();

    std::print("生成完成：\n"
               "  scanline_rasterizer.ppm\n"
               "  affine_transformer.ppm\n"
               "  basic_renderers.ppm\n");
    return 0;
}