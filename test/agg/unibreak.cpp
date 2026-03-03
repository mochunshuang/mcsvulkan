// compile: g++ -std=c++23 -o linebreak_demo linebreak_demo.cpp \
//     -lfreetype -lharfbuzz -lraqm -lSheenBidi -lagg -lunibreak
// run: ./linebreak_demo
// outputs: linebreak_200.ppm, linebreak_400.ppm, ... up to 1200

#include <cstdio>
#include <cstring>
#include <vector>
#include <print>
#include <cmath>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <hb.h>
#include <hb-ft.h>
#include <raqm.h>
#include <linebreak.h>   // libunibreak 核心头文件
#include <unibreakdef.h> // 基础定义

#include <agg_basics.h>
#include <agg_pixfmt_rgb.h>
#include <agg_rendering_buffer.h>
#include <agg_rasterizer_scanline_aa.h>
#include <agg_scanline_u.h>
#include <agg_renderer_scanline.h>
#include <agg_path_storage.h>
#include <agg_conv_curve.h>
#include <agg_conv_stroke.h>

const int HEIGHT = 800;
const int BYTES_PER_PIXEL = 3;
const double LINE_HEIGHT_FACTOR = 1.2;

// ---------- AGG 辅助函数（与之前相同，此处保留占位）----------
// ... (请保留您原有的 draw_line_with_stroke, draw_arrow, OutlineToAgg 等函数)

struct OutlineToAgg
{
    agg::path_storage &path;
    double offset_x, offset_y;
    double scale;

    OutlineToAgg(agg::path_storage &p, double ox, double oy, double s)
        : path(p), offset_x(ox), offset_y(oy), scale(s)
    {
    }

    static int move_to(const FT_Vector *to, void *user)
    {
        auto self = static_cast<OutlineToAgg *>(user);
        double x = self->offset_x + to->x * self->scale / 64.0;
        double y = self->offset_y - to->y * self->scale / 64.0;
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

// ---------- 测量宽度 ----------
double measure_text_width(FT_Face face, raqm_t *rq, const char *utf8_text,
                          size_t byte_len)
{
    raqm_clear_contents(rq);
    raqm_set_text_utf8(rq, utf8_text, byte_len);
    raqm_set_freetype_face(rq, face);
    raqm_set_par_direction(rq, RAQM_DIRECTION_LTR);
    raqm_set_language(rq, "en", 0, byte_len);
    if (!raqm_layout(rq))
        return 0.0;

    size_t glyph_cnt;
    raqm_glyph_t *glyphs = raqm_get_glyphs(rq, &glyph_cnt);
    double width = 0.0;
    for (size_t i = 0; i < glyph_cnt; ++i)
    {
        width += glyphs[i].x_advance / 64.0;
    }
    return width;
}

// ---------- 渲染一行 ----------
void render_line(
    FT_Face face, raqm_t *rq, const char *utf8_text, size_t byte_len, double pen_x,
    double pen_y, agg::rasterizer_scanline_aa<> &ras, agg::scanline_u8 &sl,
    agg::renderer_scanline_aa_solid<agg::renderer_base<agg::pixfmt_rgb24>> &ren_solid)
{
    raqm_clear_contents(rq);
    raqm_set_text_utf8(rq, utf8_text, byte_len);
    raqm_set_freetype_face(rq, face);
    raqm_set_par_direction(rq, RAQM_DIRECTION_LTR);
    raqm_set_language(rq, "en", 0, byte_len);
    if (!raqm_layout(rq))
        return;

    size_t glyph_cnt;
    raqm_glyph_t *glyphs = raqm_get_glyphs(rq, &glyph_cnt);

    double cur_x = pen_x;
    double cur_y = pen_y;
    for (size_t i = 0; i < glyph_cnt; ++i)
    {
        raqm_glyph_t &g = glyphs[i];
        FT_Face current_face = (FT_Face)g.ftface;
        if (!current_face)
            current_face = face;

        if (FT_Load_Glyph(current_face, g.index, FT_LOAD_NO_BITMAP))
        {
            cur_x += g.x_advance / 64.0;
            cur_y -= g.y_advance / 64.0;
            continue;
        }

        FT_Outline *outline = &current_face->glyph->outline;

        double render_x = cur_x + g.x_offset / 64.0;
        double render_y = cur_y - g.y_offset / 64.0;

        agg::path_storage path;
        OutlineToAgg converter(path, render_x, render_y, 1.0);
        FT_Outline_Funcs funcs = {OutlineToAgg::move_to,
                                  OutlineToAgg::line_to,
                                  OutlineToAgg::conic_to,
                                  OutlineToAgg::cubic_to,
                                  0,
                                  0};
        FT_Outline_Decompose(outline, &funcs, &converter);

        agg::conv_curve<agg::path_storage> curved_path(path);
        curved_path.approximation_scale(4.0);

        ras.reset();
        ras.add_path(curved_path);
        ren_solid.color(agg::rgba8(0, 0, 0));
        agg::render_scanlines(ras, sl, ren_solid);

        cur_x += g.x_advance / 64.0;
        cur_y -= g.y_advance / 64.0;
    }
}

// ---------- 获取 UTF-8 字符串中下一个字符的字节偏移 ----------
size_t next_utf8_char_offset(const char *text, size_t current_offset, size_t text_len)
{
    if (current_offset >= text_len)
        return text_len;
    unsigned char c = static_cast<unsigned char>(text[current_offset]);
    size_t char_len = 1;
    if (c >= 0xF0)
        char_len = 4; // 11110xxx
    else if (c >= 0xE0)
        char_len = 3; // 1110xxxx
    else if (c >= 0xC0)
        char_len = 2; // 110xxxxx
    // ASCII (0xxxxxxx) 长度为 1
    size_t next = current_offset + char_len;
    return (next > text_len) ? text_len : next;
}

int main()
{
    // ---------- 测试文本 ----------
    const char *text =
        "Combining marks and accented letters behave as expected: Your naïveté amuses "
        "me!\n"
        "Unbreakable: $(12.35) 2,1234 (12)¢ 12.54¢ (s)he file(s); breakable: ()()\n"
        "Unbreakable pairs: ‘$9’, ‘$[’, ‘$-’, ‘-9’, ‘/9’, ‘99’, ‘,9’, ‘9%’, ‘]%’\n"
        "Hyphenated words are breakable: server-side\n"
        "Em dashes can be broken on either side, but not in the middle: A——B\n"
        "NOT each space is breakable, like the last one in the following sentence (in "
        "English): He did say, “He said, ‘Good morning.’ ”\n"
        "Regretfully it does not work for the alternative quoting style due to the "
        "ambiguity of “’” (quote and apostrophe): one should not insert normal spaces "
        "between the last two quotation marks in ‘He said, “Good morning.”’ — Of course, "
        "non-breaking spaces are OK: ‘He said, “Good morning.” ’\n"
        "Still, the space rule makes my favourite ellipsis style work well, like ‘once "
        "upon a time . . .’.\n"
        "Here's an apostrophe; and there’s a right single quotation mark.\n"
        "Spanish inverted punctuation marks are always opening (e.g.)¡Foo bar!\n"
        "There should be no break after Hebrew + Hyphen like א-ת, or between solidus and "
        "Hebrew like /ת.\n"
        "Lines like this should not have their punctuation broken!… \n"
        "Emojis joined by ZWJs should not be broken: 👩‍❤️‍👩.\n"
        "Breaks can occur between Chinese characters, except before or after certain "
        "punctuation marks: 「你看過《三國演義》嗎？」他問我。";

    size_t text_len = strlen(text);

    // ---------- 初始化 FreeType ----------
    FT_Library ft_library;
    FT_Init_FreeType(&ft_library);
    const char *font_path = "C:/Windows/Fonts/msyh.ttc";
    FT_Face ft_face;
    FT_New_Face(ft_library, font_path, 0, &ft_face);
    double font_size = 20.0;
    FT_Set_Pixel_Sizes(ft_face, 0, font_size);

    // ---------- 【正确用法】直接对 UTF-8 字符串调用 libunibreak ----------
    char *brks = new char[text_len]; // breaks 数组长度 = 字节数
    set_linebreaks_utf8(reinterpret_cast<const utf8_t *>(text), text_len, nullptr, brks);

    // ---------- 创建 raqm 对象 ----------
    raqm_t *rq = raqm_create();

    // ---------- 对不同宽度生成图片 ----------
    std::vector<int> widths = {200, 400, 600, 800, 1000, 1200};

    for (int max_width : widths)
    {
        std::vector<unsigned char> buffer(max_width * HEIGHT * BYTES_PER_PIXEL, 255);
        agg::rendering_buffer rbuf(buffer.data(), max_width, HEIGHT,
                                   max_width * BYTES_PER_PIXEL);
        agg::pixfmt_rgb24 pixf(rbuf);
        agg::renderer_base<agg::pixfmt_rgb24> ren_base(pixf);
        agg::renderer_scanline_aa_solid<agg::renderer_base<agg::pixfmt_rgb24>> ren_solid(
            ren_base);
        ren_base.clear(agg::rgba8(255, 255, 255));

        agg::rasterizer_scanline_aa<> ras;
        agg::scanline_u8 sl;

        double line_height = font_size * LINE_HEIGHT_FACTOR;
        double pen_y = line_height;
        size_t total_lines = 0;
        size_t byte_start = 0; // 当前行的起始字节偏移
        while (byte_start < text_len)
        { // ... 渲染一行 ...
            total_lines++;

            size_t byte_end = byte_start;
            size_t last_break_byte = byte_start; // 上一个可换行点的字节位置

            // 贪心扩展行，直到宽度超出
            while (byte_end < text_len)
            {
                // 临时包含下一个字符进行测量
                size_t next_char_end = next_utf8_char_offset(text, byte_end, text_len);
                double line_width = measure_text_width(ft_face, rq, text + byte_start,
                                                       next_char_end - byte_start);

                if (line_width > max_width - 20)
                {
                    // 超宽：如果存在有效的上一个可换行点且不是起始点，就回退到那里
                    if (last_break_byte > byte_start)
                    {
                        byte_end = last_break_byte;
                    }
                    // 否则就在当前字符处强制断行（单词内断行）
                    break;
                }
                else
                {
                    // 未超宽：检查当前字符的换行属性（注意 breaks 索引是字节位置）
                    if (brks[next_char_end - 1] == LINEBREAK_ALLOWBREAK ||
                        brks[next_char_end - 1] == LINEBREAK_MUSTBREAK)
                    {
                        last_break_byte = next_char_end; // 记录可换行点
                    }
                    if (brks[next_char_end - 1] == LINEBREAK_MUSTBREAK)
                    {
                        // 强制换行：行结束于当前字符的末尾
                        byte_end = next_char_end;
                        break;
                    }
                    // 否则继续扩展
                    byte_end = next_char_end;
                }
            }

            // 确保 byte_end 至少推进了一个字符
            if (byte_end == byte_start)
            {
                byte_end = next_utf8_char_offset(text, byte_start, text_len);
            }

            // 渲染当前行 [byte_start, byte_end)
            if (byte_end > byte_start)
            {
                render_line(ft_face, rq, text + byte_start, byte_end - byte_start, 10.0,
                            pen_y, ras, sl, ren_solid);
            }

            // 移动到下一行
            byte_start = byte_end;
            pen_y += line_height;
            if (pen_y > HEIGHT - line_height)
                break;
        }
        // 循环结束后
        if (byte_start < text_len)
        {
            std::println(stderr, "警告：文本未完全渲染，剩余 {} 字节",
                         text_len - byte_start);
        }
        else
        {
            std::println("文本渲染完成，共 {} 行", total_lines);
        }

        char filename[64];
        std::snprintf(filename, sizeof(filename), "linebreak_%d.ppm", max_width);
        FILE *fd = std::fopen(filename, "wb");
        if (fd)
        {
            std::fprintf(fd, "P6\n%d %d\n255\n", max_width, HEIGHT);
            std::fwrite(buffer.data(), 1, buffer.size(), fd);
            std::fclose(fd);
            std::println("已生成 {}", filename);
        }
    }

    // 清理
    raqm_destroy(rq);
    delete[] brks;
    FT_Done_Face(ft_face);
    FT_Done_FreeType(ft_library);
    return 0;
}