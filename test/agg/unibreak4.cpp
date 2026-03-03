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
#include FT_COLOR_H // 彩色字体支持

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

// ---------- AGG 辅助函数（保持不变）----------
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

// ---------- 获取 UTF-8 字符串中下一个字符的字节偏移 ----------
size_t next_utf8_char_offset(const char *text, size_t current_offset, size_t text_len)
{
    if (current_offset >= text_len)
        return text_len;
    unsigned char c = static_cast<unsigned char>(text[current_offset]);
    size_t char_len = 1;
    if (c >= 0xF0)
        char_len = 4;
    else if (c >= 0xE0)
        char_len = 3;
    else if (c >= 0xC0)
        char_len = 2;
    size_t next = current_offset + char_len;
    return (next > text_len) ? text_len : next;
}

// ---------- 从 UTF-8 字符串的指定位置解码一个 Unicode 码点 ----------
uint32_t decode_utf8_char(const char *text, size_t offset, size_t *char_len = nullptr)
{
    uint32_t ch = 0;
    unsigned char c = static_cast<unsigned char>(text[offset]);
    if (c < 0x80)
    {
        ch = c;
        if (char_len)
            *char_len = 1;
    }
    else if (c < 0xE0)
    {
        ch = (c & 0x1F) << 6;
        ch |= (text[offset + 1] & 0x3F);
        if (char_len)
            *char_len = 2;
    }
    else if (c < 0xF0)
    {
        ch = (c & 0x0F) << 12;
        ch |= (text[offset + 1] & 0x3F) << 6;
        ch |= (text[offset + 2] & 0x3F);
        if (char_len)
            *char_len = 3;
    }
    else
    {
        ch = (c & 0x07) << 18;
        ch |= (text[offset + 1] & 0x3F) << 12;
        ch |= (text[offset + 2] & 0x3F) << 6;
        ch |= (text[offset + 3] & 0x3F);
        if (char_len)
            *char_len = 4;
    }
    return ch;
}

// ---------- 检查字体是否有某个 Unicode 字符 ----------
bool has_glyph(FT_Face face, uint32_t codepoint)
{
    return FT_Get_Char_Index(face, codepoint) != 0;
}

// ---------- 字体段结构 ----------
struct FontRun
{
    size_t byte_start; // 相对于当前子串的起始字节偏移
    size_t byte_len;
    FT_Face face;
};

// ---------- 收集指定子串内的字体段 ----------
std::vector<FontRun> collect_font_runs_for_range(
    const char *text, size_t byte_len, FT_Face primary_face,
    const std::vector<FT_Face> &fallback_faces)
{
    std::vector<FontRun> runs;
    size_t pos = 0;
    while (pos < byte_len)
    {
        size_t char_len;
        uint32_t ch = decode_utf8_char(text, pos, &char_len);
        // 选择字体
        FT_Face current_face = primary_face;
        if (!has_glyph(primary_face, ch))
        {
            for (FT_Face face : fallback_faces)
            {
                if (has_glyph(face, ch))
                {
                    current_face = face;
                    break;
                }
            }
        }
        // 如果 runs 为空或上一个 face 不同，则开始新段
        if (runs.empty() || runs.back().face != current_face)
        {
            runs.push_back({pos, char_len, current_face});
        }
        else
        {
            runs.back().byte_len += char_len;
        }
        pos += char_len;
    }
    return runs;
}

// ---------- 带字体回退的宽度测量 ----------
double measure_text_width_with_fallback(FT_Library ft_library, const char *text,
                                        size_t byte_start, size_t byte_end,
                                        FT_Face primary_face,
                                        const std::vector<FT_Face> &fallback_faces)
{
    // 创建临时 raqm 对象
    raqm_t *rq = raqm_create();
    if (!rq)
        return 0.0;

    size_t seg_len = byte_end - byte_start;
    const char *seg_text = text + byte_start;

    // 设置文本
    if (!raqm_set_text_utf8(rq, seg_text, seg_len))
    {
        raqm_destroy(rq);
        return 0.0;
    }

    // 收集字体段
    auto runs =
        collect_font_runs_for_range(seg_text, seg_len, primary_face, fallback_faces);
    for (const auto &run : runs)
    {
        if (!raqm_set_freetype_face_range(rq, run.face, run.byte_start, run.byte_len))
        {
            std::println(stderr, "raqm_set_freetype_face_range failed");
        }
    }

    raqm_set_par_direction(rq, RAQM_DIRECTION_LTR);
    raqm_set_language(rq, "en", 0, seg_len);

    if (!raqm_layout(rq))
    {
        raqm_destroy(rq);
        return 0.0;
    }

    size_t glyph_cnt;
    raqm_glyph_t *glyphs = raqm_get_glyphs(rq, &glyph_cnt);
    double width = 0.0;
    for (size_t i = 0; i < glyph_cnt; ++i)
    {
        width += glyphs[i].x_advance / 64.0;
    }

    raqm_destroy(rq);
    return width;
}

// 在 main 函数开头（可选）包含随机数种子
#include <random>
#include <chrono>

// 定义颜色数组
agg::rgba8 colors[] = {
    agg::rgba8(255, 0, 0),   // 红
    agg::rgba8(0, 255, 0),   // 绿
    agg::rgba8(0, 0, 255),   // 蓝
    agg::rgba8(255, 255, 0), // 黄
    agg::rgba8(255, 0, 255), // 紫
    agg::rgba8(0, 255, 255), // 青
};
const int num_colors = sizeof(colors) / sizeof(colors[0]);

// ---------- 渲染一行（已设置好 raqm 对象）,支持彩色----------
void render_line(
    raqm_t *rq, double pen_x, double pen_y, agg::rasterizer_scanline_aa<> &ras,
    agg::scanline_u8 &sl,
    agg::renderer_scanline_aa_solid<agg::renderer_base<agg::pixfmt_rgb24>> &ren_solid,
    FT_Face primary_face,
    agg::rgba8 default_color) // 新增：默认颜色参数
{
    size_t glyph_cnt;
    raqm_glyph_t *glyphs = raqm_get_glyphs(rq, &glyph_cnt);

    double cur_x = pen_x;
    double cur_y = pen_y;
    for (size_t i = 0; i < glyph_cnt; ++i)
    {
        raqm_glyph_t &g = glyphs[i];
        FT_Face current_face = (FT_Face)g.ftface;
        if (!current_face)
            current_face = primary_face;

        // 加载字形（启用颜色）
        if (FT_Load_Glyph(current_face, g.index, FT_LOAD_COLOR | FT_LOAD_NO_BITMAP))
        {
            if (FT_Load_Glyph(current_face, g.index, FT_LOAD_NO_BITMAP))
            {
                cur_x += g.x_advance / 64.0;
                cur_y -= g.y_advance / 64.0;
                continue;
            }
        }

        if (FT_HAS_COLOR(current_face))
        {
            // 彩色字体：使用调色板颜色
            FT_Color *palette = nullptr;
            if (FT_Palette_Select(current_face, 0, &palette) == 0)
            {
                FT_LayerIterator iterator;
                FT_UInt layer_glyph;
                FT_UInt layer_color_index;
                iterator.p = nullptr;

                while (FT_Get_Color_Glyph_Layer(current_face, g.index, &layer_glyph,
                                                &layer_color_index, &iterator))
                {
                    if (FT_Load_Glyph(current_face, layer_glyph, FT_LOAD_NO_BITMAP))
                        continue;

                    FT_Color color = palette[layer_color_index];
                    agg::rgba8 render_color(color.red, color.green, color.blue, 255);

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
                    ren_solid.color(render_color);
                    agg::render_scanlines(ras, sl, ren_solid);
                }
            }
            else
            {
                // 调色板获取失败，回退到默认颜色
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
                ren_solid.color(default_color);
                agg::render_scanlines(ras, sl, ren_solid);
            }
        }
        else
        {
            // 非彩色字体：使用默认颜色
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
            ren_solid.color(default_color);
            agg::render_scanlines(ras, sl, ren_solid);
        }

        cur_x += g.x_advance / 64.0;
        cur_y -= g.y_advance / 64.0;
    }
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
    if (FT_Init_FreeType(&ft_library))
    {
        std::println(stderr, "FT_Init_FreeType failed");
        return 1;
    }

    // 主字体（微软雅黑）
    const char *font_path = "C:/Windows/Fonts/msyh.ttc";
    FT_Face primary_face;
    if (FT_New_Face(ft_library, font_path, 0, &primary_face))
    {
        std::println(stderr, "FT_New_Face failed: {}", font_path);
        FT_Done_FreeType(ft_library);
        return 1;
    }
    double font_size = 20.0;
    FT_Set_Pixel_Sizes(primary_face, 0, font_size);

    // ---------- 加载备选字体（Windows 常用）----------
    std::vector<FT_Face> fallback_faces;
    const char *fallback_paths[] = {
        "C:/Windows/Fonts/seguiemj.ttf",  // Segoe UI Emoji
        "C:/Windows/Fonts/seguiisym.ttf", // Segoe UI Symbol
        "C:/Windows/Fonts/arial.ttf",     // Arial (包含希伯来语等)
        "C:/Windows/Fonts/times.ttf",     // Times New Roman
        "C:/Windows/Fonts/cour.ttf",      // Courier New
    };
    for (const char *path : fallback_paths)
    {
        FT_Face face;
        if (FT_New_Face(ft_library, path, 0, &face) == 0)
        {
            FT_Set_Pixel_Sizes(face, 0, font_size);
            fallback_faces.push_back(face);
        }
        else
        {
            std::println(stderr, "Failed to load fallback font: {}", path);
        }
    }

    // ---------- 使用 libunibreak 获取换行属性 ----------
    char *brks = new char[text_len]; // breaks 数组长度 = 字节数
    set_linebreaks_utf8(reinterpret_cast<const utf8_t *>(text), text_len, nullptr, brks);

    // ---------- 主 raqm 对象（用于渲染）---------
    raqm_t *rq = raqm_create();

    // ---------- 对不同宽度生成图片 ----------
    std::vector<int> widths = {200, 400, 600, 800, 1000, 1200};

    // 初始化随机数生成器
    auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);

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
        {
            total_lines++;

            size_t byte_end = byte_start;
            size_t last_break_byte = byte_start;
            size_t forced_next_start = text_len;

            // 贪心扩展行，使用带回退的宽度测量
            while (byte_end < text_len)
            {
                size_t next_char_end = next_utf8_char_offset(text, byte_end, text_len);
                double line_width = measure_text_width_with_fallback(
                    ft_library, text, byte_start, next_char_end, primary_face,
                    fallback_faces);

                if (line_width > max_width - 20)
                {
                    if (last_break_byte > byte_start)
                    {
                        byte_end = last_break_byte;
                        forced_next_start = byte_end;
                    }
                    else
                    {
                        byte_end = next_char_end;
                        forced_next_start = byte_end;
                    }
                    break;
                }
                else
                {
                    char break_type = brks[next_char_end - 1];
                    if (break_type == LINEBREAK_ALLOWBREAK)
                    {
                        last_break_byte = next_char_end;
                    }
                    if (break_type == LINEBREAK_MUSTBREAK)
                    {
                        forced_next_start = next_char_end;
                        // 当前行结束于此字符之前，所以 byte_end 保持当前值
                        break;
                    }
                    byte_end = next_char_end;
                }
            }

            if (byte_end >= text_len && forced_next_start == text_len)
            {
                forced_next_start = text_len;
            }

            // ---------- 渲染当前行 ----------
            // 生成随机颜色（用于当前行的非彩色文字）
            agg::rgba8 line_color(static_cast<uint8_t>(dist(rng)),
                                  static_cast<uint8_t>(dist(rng)),
                                  static_cast<uint8_t>(dist(rng)), 255);
            if (byte_end > byte_start)
            {
                size_t seg_len = byte_end - byte_start;
                const char *seg_text = text + byte_start;

                // 清空 raqm 对象并设置当前行的文本
                raqm_clear_contents(rq);
                if (!raqm_set_text_utf8(rq, seg_text, seg_len))
                {
                    std::println(stderr, "raqm_set_text_utf8 failed");
                    break;
                }

                // 收集当前行的字体段
                auto runs = collect_font_runs_for_range(seg_text, seg_len, primary_face,
                                                        fallback_faces);
                for (const auto &run : runs)
                {
                    if (!raqm_set_freetype_face_range(rq, run.face, run.byte_start,
                                                      run.byte_len))
                    {
                        std::println(stderr, "raqm_set_freetype_face_range failed");
                    }
                }

                raqm_set_par_direction(rq, RAQM_DIRECTION_LTR);
                raqm_set_language(rq, "en", 0, seg_len);

                if (!raqm_layout(rq))
                {
                    std::println(stderr, "raqm_layout failed");
                }
                else
                {
                    render_line(rq, 10.0, pen_y, ras, sl, ren_solid, primary_face,
                                line_color);
                }
            }

            // 移动到下一行
            byte_start = forced_next_start;
            pen_y += line_height;
            if (pen_y > HEIGHT - line_height)
            {
                std::println(stderr, "警告：文本超出画布高度，剩余 {} 字节",
                             text_len - byte_start);
                break;
            }
        }

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
    FT_Done_Face(primary_face);
    for (FT_Face f : fallback_faces)
        FT_Done_Face(f);
    FT_Done_FreeType(ft_library);
    return 0;
}