#include <cstdio>
#include <cstring>
#include <print>

#include <agg_basics.h>
#include <agg_rendering_buffer.h>
#include <agg_pixfmt_rgb.h>
#include <agg_renderer_base.h>
#include <agg_renderer_mclip.h>
#include <agg_renderer_scanline.h>
#include <agg_rasterizer_scanline_aa.h>
#include <agg_scanline_u.h>
#include <agg_ellipse.h>

enum
{
    frame_width = 400,
    frame_height = 300
};
const unsigned BPP = 3; // 每像素字节数

bool write_ppm(const unsigned char *buf, unsigned w, unsigned h, const char *fname)
{
    FILE *f = fopen(fname, "wb");
    if (!f)
        return false;
    fprintf(f, "P6 %u %u 255 ", w, h);
    fwrite(buf, 1, w * h * BPP, f);
    fclose(f);
    return true;
}

// 辅助函数：绘制椭圆（使用给定渲染器和颜色）
template <class Ren>
void draw_ellipse(Ren &ren, double cx, double cy, double rx, double ry, agg::rgba8 color,
                  agg::rasterizer_scanline_aa<> &ras, agg::scanline_u8 &sl)
{
    ras.reset();
    agg::ellipse ell(cx, cy, rx, ry);
    ras.add_path(ell);
    agg::render_scanlines_aa_solid(ras, sl, ren, color);
}

// 辅助函数：绘制矩形边框（使用无裁剪渲染器）
void draw_rect_border(agg::renderer_base<agg::pixfmt_rgb24> &ren, int x1, int y1, int x2,
                      int y2, agg::rgba8 color)
{
    // 绘制四条边，确保长度正确（包含端点）
    ren.copy_hline(x1, y1, x2 - x1 + 1, color); // 上边
    ren.copy_hline(x1, y2, x2 - x1 + 1, color); // 下边
    ren.copy_vline(x1, y1, y2 - y1 + 1, color); // 左边
    ren.copy_vline(x2, y1, y2 - y1 + 1, color); // 右边
}

int main()
{
    unsigned char *buffer = new unsigned char[frame_width * frame_height * BPP];
    agg::rendering_buffer rbuf(buffer, frame_width, frame_height, frame_width * BPP);
    agg::pixfmt_rgb24 pixf(rbuf);
    agg::rasterizer_scanline_aa<> ras;
    agg::scanline_u8 sl;

    // --------------------------------------------
    // 第一部分：renderer_base 单矩形裁剪（三步曲）
    // --------------------------------------------
    {
        // 1. 开头：只绘制背景椭圆（灰色），无裁剪，无边框
        memset(buffer, 255, frame_width * frame_height * BPP); // 白色背景
        agg::renderer_base<agg::pixfmt_rgb24> ren(pixf);       // 无裁剪的渲染器
        draw_ellipse(ren, 200.0, 150.0, 150.0, 100.0, agg::rgba8(200, 200, 200), ras, sl);
        write_ppm(buffer, frame_width, frame_height, "clip_base_00_original.ppm");
        std::print("生成 clip_base_00_original.ppm（原始背景椭圆）\n");

        // 2. 过程：在背景上绘制裁剪框（黑色边框），但不应用裁剪
        // 注意：这里仍然使用无裁剪的渲染器，只添加边框
        draw_rect_border(ren, 50, 50, 200, 150, agg::rgba8(0, 0, 0));
        write_ppm(buffer, frame_width, frame_height, "clip_base_01_clipbox.ppm");
        std::print("生成 clip_base_01_clipbox.ppm（背景 + 裁剪框）\n");

        // 3. 结果：应用裁剪，绘制前景蓝色椭圆（只出现在框内），并保留边框
        // 先重置缓冲区到白色背景（如果要保留背景，可不清除，但为了清晰对比，我们重新开始）
        memset(buffer, 255, frame_width * frame_height * BPP);
        agg::renderer_base<agg::pixfmt_rgb24> ren_result(pixf); // 新渲染器
        // 先画背景椭圆（灰色）
        draw_ellipse(ren_result, 200.0, 150.0, 150.0, 100.0, agg::rgba8(200, 200, 200),
                     ras, sl);
        // 设置裁剪
        ren_result.clip_box(50, 50, 200, 150);
        // 画前景蓝色椭圆（受裁剪）
        draw_ellipse(ren_result, 200.0, 150.0, 150.0, 100.0, agg::rgba8(0, 0, 255), ras,
                     sl);
        // 关闭裁剪，画边框
        ren_result.reset_clipping(false); // 禁用裁剪
        draw_rect_border(ren_result, 50, 50, 200, 150, agg::rgba8(0, 0, 0));
        write_ppm(buffer, frame_width, frame_height, "clip_base_02_result.ppm");
        std::print("生成 clip_base_02_result.ppm（最终裁剪结果）\n");
    }

    // --------------------------------------------
    // 第二部分：renderer_mclip 多矩形裁剪（三步曲）
    // --------------------------------------------
    {
        // 1. 开头：背景椭圆（灰色）
        memset(buffer, 255, frame_width * frame_height * BPP);
        agg::renderer_base<agg::pixfmt_rgb24> ren_bg(pixf);
        draw_ellipse(ren_bg, 200.0, 150.0, 150.0, 100.0, agg::rgba8(200, 200, 200), ras,
                     sl);
        write_ppm(buffer, frame_width, frame_height, "clip_mclip_00_original.ppm");
        std::print("生成 clip_mclip_00_original.ppm（原始背景椭圆）\n");

        // 2. 过程：在背景上绘制两个裁剪框（黑色边框）
        draw_rect_border(ren_bg, 30, 30, 100, 100, agg::rgba8(0, 0, 0));
        draw_rect_border(ren_bg, 150, 80, 250, 170, agg::rgba8(0, 0, 0));
        write_ppm(buffer, frame_width, frame_height, "clip_mclip_01_clipbox.ppm");
        std::print("生成 clip_mclip_01_clipbox.ppm（背景 + 两个裁剪框）\n");

        // 3. 结果：应用多矩形裁剪，绘制前景红色椭圆（只出现在两个框内），并保留边框
        memset(buffer, 255, frame_width * frame_height * BPP);
        agg::renderer_base<agg::pixfmt_rgb24> ren_result(pixf);
        // 画背景椭圆
        draw_ellipse(ren_result, 200.0, 150.0, 150.0, 100.0, agg::rgba8(200, 200, 200),
                     ras, sl);
        // 创建多矩形裁剪渲染器，并添加裁剪框
        agg::renderer_mclip<agg::pixfmt_rgb24> ren_mclip(pixf);
        ren_mclip.add_clip_box(30, 30, 100, 100);
        ren_mclip.add_clip_box(150, 80, 250, 170);
        // 画前景红色椭圆（受多矩形裁剪）
        draw_ellipse(ren_mclip, 200.0, 150.0, 150.0, 100.0, agg::rgba8(255, 0, 0), ras,
                     sl);
        // 边框绘制必须使用无裁剪渲染器
        draw_rect_border(ren_result, 30, 30, 100, 100, agg::rgba8(0, 0, 0));
        draw_rect_border(ren_result, 150, 80, 250, 170, agg::rgba8(0, 0, 0));
        write_ppm(buffer, frame_width, frame_height, "clip_mclip_02_result.ppm");
        std::print("生成 clip_mclip_02_result.ppm（最终裁剪结果）\n");
    }
    /*
在AGG中，扫描线容器有三种类型：

scanline_u- 未包装的鳞状容器
scanline_p- 填充扫描线集装箱
scanline_bin- 用于二进制“混叠”扫描线的容器
前两个容器可以保存抗锯齿信息，第三个容器不能。
*/

    delete[] buffer;
    return 0;
}