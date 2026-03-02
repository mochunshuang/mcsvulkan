// g++ -o render_g render_g.cpp -lfreetype -lagg
// 运行生成 render_g.ppm

#include <fstream>
#include <vector>
#include <cstdio>
#include <cstring>
#include <print>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include <agg_basics.h>
#include <agg_pixfmt_rgb.h>
#include <agg_rendering_buffer.h>
#include <agg_rasterizer_scanline_aa.h>
#include <agg_scanline_u.h>
#include <agg_renderer_scanline.h>
#include <agg_path_storage.h>

const int WIDTH = 400;
const int HEIGHT = 300;
const int BYTES_PER_PIXEL = 3; // RGB

// 将FreeType轮廓点转换为AGG路径的回调
struct OutlineToAgg
{
    agg::path_storage &path;
    double offset_x, offset_y;
    double scale; // 1.0 表示像素坐标

    OutlineToAgg(agg::path_storage &p, double ox, double oy, double s)
        : path(p), offset_x(ox), offset_y(oy), scale(s)
    {
    }

    static int move_to(const FT_Vector *to, void *user)
    {
        auto self = static_cast<OutlineToAgg *>(user);
        double x = self->offset_x + to->x * self->scale / 64.0;
        double y = self->offset_y -
                   to->y * self->scale / 64.0; // 翻转Y：FreeType向上为正，AGG向下为正
        self->path.move_to(x, y);
        return 0;
    }

    static int line_to(const FT_Vector *to, void *user)
    {
        auto self = static_cast<OutlineToAgg *>(user);
        double x = self->offset_x + to->x * self->scale / 64.0;
        double y = self->offset_y - to->y * self->scale / 64.0;
        self->path.line_to(x, y);
        return 0;
    }

    static int conic_to(const FT_Vector *ctrl, const FT_Vector *to, void *user)
    {
        auto self = static_cast<OutlineToAgg *>(user);
        double cx = self->offset_x + ctrl->x * self->scale / 64.0;
        double cy = self->offset_y - ctrl->y * self->scale / 64.0;
        double tx = self->offset_x + to->x * self->scale / 64.0;
        double ty = self->offset_y - to->y * self->scale / 64.0;
        self->path.curve3(cx, cy, tx, ty);
        return 0;
    }

    static int cubic_to(const FT_Vector *c1, const FT_Vector *c2, const FT_Vector *to,
                        void *user)
    {
        auto self = static_cast<OutlineToAgg *>(user);
        double c1x = self->offset_x + c1->x * self->scale / 64.0;
        double c1y = self->offset_y - c1->y * self->scale / 64.0;
        double c2x = self->offset_x + c2->x * self->scale / 64.0;
        double c2y = self->offset_y - c2->y * self->scale / 64.0;
        double tx = self->offset_x + to->x * self->scale / 64.0;
        double ty = self->offset_y - to->y * self->scale / 64.0;
        self->path.curve4(c1x, c1y, c2x, c2y, tx, ty);
        return 0;
    }
};

int main()
{
    // 1. 分配画布内存并填充白色
    std::vector<unsigned char> buffer(WIDTH * HEIGHT * BYTES_PER_PIXEL, 255);

    // 2. 创建AGG渲染缓冲区 (步幅为正，Y向下)
    agg::rendering_buffer rbuf(buffer.data(), WIDTH, HEIGHT, WIDTH * BYTES_PER_PIXEL);
    agg::pixfmt_rgb24 pixf(rbuf);
    agg::renderer_base<agg::pixfmt_rgb24> ren_base(pixf);
    agg::renderer_scanline_aa_solid<agg::renderer_base<agg::pixfmt_rgb24>> ren_solid(
        ren_base);
    ren_base.clear(agg::rgba8(255, 255, 255)); // 白色背景

    // 3. 初始化FreeType并加载字体
    const char *font_path = "TiroBangla-Regular.ttf"; // 请替换为实际存在的字体文件
    FT_Library ft_library;
    if (FT_Init_FreeType(&ft_library))
    {
        std::println(stderr, "FT_Init_FreeType failed");
        return 1;
    }
    FT_Face ft_face;
    if (FT_New_Face(ft_library, font_path, 0, &ft_face))
    {
        std::println(stderr, "FT_New_Face failed");
        FT_Done_FreeType(ft_library);
        return 1;
    }

    // 设置像素大小（例如 100 像素高）
    double font_size = 100.0;
    FT_Set_Pixel_Sizes(ft_face, 0, font_size);

    // 4. 加载字形 'g'
    FT_UInt glyph_index = FT_Get_Char_Index(ft_face, 'g');
    if (glyph_index == 0)
    {
        std::println(stderr, "字符 'g' 不存在");
        return 1;
    }
    if (FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING))
    {
        std::println(stderr, "FT_Load_Glyph failed");
        return 1;
    }

    FT_GlyphSlot slot = ft_face->glyph;
    FT_Outline *outline = &slot->outline;

    // 5. 计算字形边界，用于居中
    FT_Glyph_Metrics &metrics = slot->metrics;
    double bbox_x_min = metrics.horiBearingX / 64.0;
    double bbox_x_max = (metrics.horiBearingX + metrics.width) / 64.0;
    double bbox_y_min = (metrics.horiBearingY - metrics.height) / 64.0; // 下边界（负值）
    double bbox_y_max = metrics.horiBearingY / 64.0;                    // 上边界（正值）

    double bbox_center_x = (bbox_x_min + bbox_x_max) / 2.0;
    double bbox_center_y = (bbox_y_min + bbox_y_max) / 2.0;

    // 6. 计算偏移量，使字形居中于画布
    double origin_x = WIDTH / 2.0 - bbox_center_x;
    double origin_y =
        HEIGHT / 2.0 +
        bbox_center_y; // 注意：AGG Y向下为正，所以用加法（因为bbox_center_y可能为负）

    // 7. 将FreeType轮廓转换为AGG路径
    agg::path_storage path;
    OutlineToAgg converter(path, origin_x, origin_y,
                           1.0); // scale = 1.0 表示直接使用像素坐标
    FT_Outline_Funcs funcs;
    funcs.move_to = OutlineToAgg::move_to;
    funcs.line_to = OutlineToAgg::line_to;
    funcs.conic_to = OutlineToAgg::conic_to;
    funcs.cubic_to = OutlineToAgg::cubic_to;
    funcs.shift = 0;
    funcs.delta = 0;

    FT_Outline_Decompose(outline, &funcs, &converter);

    // 8. 使用AGG渲染路径
    agg::rasterizer_scanline_aa<> ras;
    agg::scanline_u8 sl;
    ras.add_path(path);
    ren_solid.color(agg::rgba8(0, 0, 0)); // 黑色
    agg::render_scanlines(ras, sl, ren_solid);

    // 9. 输出PPM文件
    FILE *fd = fopen("freeType.ppm", "wb");
    if (fd)
    {
        std::fprintf(fd, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
        fwrite(buffer.data(), 1, buffer.size(), fd);
        fclose(fd);
        std::println("已生成 render_g.ppm");
    }
    else
    {
        std::println(stderr, "无法写入文件");
    }

    // 10. 清理
    FT_Done_Face(ft_face);
    FT_Done_FreeType(ft_library);

    return 0;
}