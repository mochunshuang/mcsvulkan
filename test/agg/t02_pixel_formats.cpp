#include <assert.h>
#include <cstdio>
#include <cstring>

#include <print>
#include <agg_rendering_buffer.h>
#include <agg_pixfmt_rgb.h>

// Alpha-Mask
#include <agg_pixfmt_amask_adaptor.h>
#include <agg_alpha_mask_u8.h>

enum
{
    frame_width = 320,
    frame_height = 200
};

// [...write_ppm is skipped...]
bool write_ppm(const unsigned char *buf, unsigned width, unsigned height,
               const char *file_name)
{
    constexpr std::size_t BYTES_PER_PIXEL = 3; // RGB格式，每像素3字节
    FILE *fd = fopen(file_name, "wb");
    if (fd)
    {
        // diff:所有AGG控制台示例均使用P6 256格式，即RGB，每个通道一个字节
        std::print(fd, "P6 {} {} 255 ", width, height);
        fwrite(buf, 1, width * height * BYTES_PER_PIXEL, fd);
        fclose(fd);
        return true;
    }
    return false;
}

// Draw a black frame around the rendering buffer
//--------------------------------------------------
template <class Ren>
void draw_black_frame(Ren &ren)
{
    unsigned i;
    agg::rgba8 c(0, 0, 0);
    for (i = 0; i < ren.height(); ++i)
    {
        ren.copy_pixel(0, i, c);
        ren.copy_pixel(ren.width() - 1, i, c);
    }
    for (i = 0; i < ren.width(); ++i)
    {
        ren.copy_pixel(i, 0, c);
        ren.copy_pixel(i, ren.height() - 1, c);
    }
}

bool operator==(const agg::rgba8 &lhs, const agg::rgba8 &rhs)
{
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

int main()
{
    //--------------------
    // Allocate the buffer.
    // Clear the buffer, for now "manually"
    // Create the rendering buffer object
    // Create the Pixel Format renderer
    // Do something simple, draw a diagonal line
    // Write the buffer to agg_test.ppm
    // Free memory

    constexpr std::size_t BYTES_PER_PIXEL = 3; // RGB格式，每像素3字节
    unsigned char *buffer =
        new unsigned char[frame_width * frame_height * BYTES_PER_PIXEL];

    const auto reset_buffer = [&](int value = 255) {
        memset(buffer, value, frame_width * frame_height * BYTES_PER_PIXEL);
    };
    reset_buffer();
    constexpr auto stride = frame_width * BYTES_PER_PIXEL;

    agg::rendering_buffer rbuf{buffer, frame_width, frame_height, stride};

    // NOTE: 思维的转变：将像素复制到 渲染缓冲区中
    agg::pixfmt_rgb24 pixf(rbuf); // NOTE: 引用传递
    const auto color = agg::rgba8(127, 200, 98);
    for (unsigned i = 0; i < pixf.height() / 2; ++i)
    {
        pixf.copy_pixel(i, i, color);
    }
    draw_black_frame(pixf); // 覆盖边框
    // [comments/t02_pixel_formats_0.png]
    write_ppm(buffer, frame_width, frame_height, "t02_pixel_formats.ppm");

    // NOTE: 一些成员
    agg::rgba8 a = pixf.pixel(1, 1);
    assert(a.r == color.r);
    assert(a.g == color.g);
    assert(a.b == color.b);
    assert(a.a == color.a);

    assert(color == pixf.pixel(1, 1)); // NOTE: 手动添加功能

    // NOTE: 混合实现抗锯齿
    // 混合水平或垂直的纯色跨度
    {
        // 重置整个图像缓冲区为白色（每个像素都是255,255,255）
        reset_buffer();

        // 创建一个像素格式渲染器对象 pixf。
        // agg::pixfmt_rgb24 知道如何将颜色数据写入缓冲区，
        // 它封装了具体的像素格式（这里是 24位 RGB，每通道8位），
        // 并提供了各种绘制和混合像素的方法。
        agg::pixfmt_rgb24 pixf(rbuf);

        // 定义一个颜色数组 span，长度等于图像宽度。
        // 这个数组将存储一行的所有像素颜色，供后续批量绘制使用。
        agg::rgba8 span[frame_width];

        // 循环填充颜色数组，生成一个渐变效果。
        for (unsigned i = 0; i < frame_width; ++i)
        {
            // 注意：agg::rgba 通常用红、绿、蓝、透明度四个分量构造，
            // 每个分量的取值范围是 0.0 到 1.0。
            // 这里使用了两个参数的构造函数，可能是将第一个参数解释为某种特殊值（比如波长或灰度），
            // 第二个参数 0.8 作为透明度。实际应用中应确保颜色值在有效范围内，
            // 此处仅为演示渐变效果，具体数值可能产生意料之外的色彩。
            agg::rgba c(380.0 + 400.0 * i / frame_width, 0.8);
            // 将高精度的 rgba 颜色转换为 8 位每通道的 rgba8 格式，
            // 以便与 pixfmt_rgb24 的像素格式匹配。
            span[i] = agg::rgba8(c);
        }

        // 对图像的每一行进行混合绘制。
        for (unsigned i = 0; i < frame_height; ++i)
        {
            // blend_color_hspan 是“混合颜色水平跨度”的缩写。
            // 它的作用是将一行（从 (0,i) 开始，长度为 frame_width）的像素颜色
            // 通过 alpha 混合的方式与当前背景融合。
            // 参数说明：
            //   x, y    : 起始坐标
            //   len     : 要绘制的像素数量
            //   colors  : 颜色数组，每个元素对应一个像素的颜色
            //   covers  : 可选，每个像素独立的覆盖因子（抗锯齿权重），这里为 0 表示不使用
            //   cover   : 统一的覆盖因子，agg::cover_full 表示完全不透明（值 255）
            // 混合后的结果会直接写入渲染缓冲区。
            pixf.blend_color_hspan(0, i, frame_width, span, 0, agg::cover_full);
        }

        // [comments/t02_pixel_formats_1.png]
        write_ppm(buffer, frame_width, frame_height, "t02_pixel_formats_blend.ppm");
    }
    // diff: Alpha-Mask（阿尔法遮罩）用于实现任意形状的裁剪或透明度控制。
    {
        reset_buffer();
        agg::pixfmt_rgb24 pixf(rbuf);

        // ---------- 分配并初始化 Alpha-Mask 缓冲区 ----------
        // Alpha-mask 是一个单独的灰度缓冲区，每个像素1字节，值范围0~255。
        // 它决定了主缓冲区每个像素的“额外覆盖因子”（即透明度权重）。
        // 这里动态分配一个大小为 frame_width * frame_height 的字节数组。
        auto *amask_buf = new agg::int8u[frame_width * frame_height * 1];

        // 为 mask 缓冲区创建 rendering_buffer 对象。
        // 由于 mask 是灰度图，每像素1字节，因此步长 = 宽度（字节数）。
        agg::rendering_buffer amask_rbuf(amask_buf, frame_width, frame_height,
                                         frame_width);

        // 注意这里我们使用的amask_no_clip_gray8不执行裁剪。
        // 这是因为我们使用了大小完全相同的主alpha掩罩缓冲区
        // NOTE:如果你的 alpha-mask 缓冲区小于主缓冲区，你就必须用 alpha_mask_gray8 代替
        agg::amask_no_clip_gray8 amask(amask_rbuf);

        // ---------- 创建 Alpha-Mask 适配器 ----------
        // 适配器 pixfmt_amask_adaptor 将像素格式渲染器（pixf）和 mask
        // 对象（amask）结合起来。 它的作用是：拦截所有绘图调用，在底层自动从 mask
        // 中读取对应位置的灰度值， 并将该值作为额外的覆盖因子（alpha
        // 权重）应用到混合操作中。 这样，我们只需正常调用绘图函数，就能实现基于 mask
        // 的裁剪或透明度渐变。
        agg::pixfmt_amask_adaptor<agg::pixfmt_rgb24, agg::amask_no_clip_gray8> pixf_amask(
            pixf, amask);

        // ---------- 填充 Alpha-Mask 缓冲区 ----------
        // 此处我们简单地填充一个垂直渐变：从上到下，灰度值从0（完全透明）逐渐变为255（完全不透明）。
        // 这将导致最终图像从上到下逐渐显现（上方区域几乎不绘制，下方区域完全绘制）。
        for (unsigned i = 0; i < frame_height; ++i)
        {
            // 计算当前行的灰度值：值随行号增加而增大，实现从上到下渐亮。
            unsigned val = 255 * i / frame_height;
            // 将整行设置为相同的灰度值。
            memset(amask_rbuf.row_ptr(i), val, frame_width);
        }

        const auto draw_pixel = [&] { // ---------- 准备颜色跨度（光谱渐变） ----------
            agg::rgba8 span[frame_width];
            for (unsigned i = 0; i < frame_width; ++i)
            {
                // 构造一个颜色，这里使用两个参数的 agg::rgba 构造函数：
                // 第一个参数可能是某种特殊值（如波长），第二个参数 0.8 为 alpha 值。
                // 注意：实际使用时需要确保颜色分量在 [0,1]
                // 范围内，否则可能产生不可预知结果。
                agg::rgba c(380.0 + (400.0 * i / frame_width), 0.8);
                span[i] = agg::rgba8(c); // 转换为8位每通道的颜色
            }

            // ---------- 通过适配器绘制水平颜色跨度 ----------
            for (unsigned i = 0; i < frame_height; ++i)
            {
                // 注意：我们现在调用的是适配器 pixf_amask 的 blend_color_hspan 方法，
                // 而不是原始 pixf 的方法。
                // 适配器内部会自动处理 mask 的覆盖值：它会从 mask 中读取当前行 (第 i 行)
                // 所有像素的灰度值，并将这些灰度值作为额外的覆盖因子，与颜色混合。
                // 因此，实际绘制效果是：每个像素的颜色会乘以 mask 对应位置的灰度值/255，
                // 从而实现垂直渐变的透明度效果。
                // NOTE: pixf_amask 适配了 pixf。 代理了 pixf。在 pixf 增加了透明的能力
                pixf_amask.blend_color_hspan(0, i, frame_width, span, 0, agg::cover_full);
            }
        };
        draw_pixel();

        /*
       Alpha-Mask：一个与主画布尺寸相同的灰度图像，每个像素的值（0~255）表示该位置被绘制的“允许程度”。0
       表示完全不允许绘制（完全透明），255
       表示完全允许（不透明），中间值产生半透明效果。

       适配器（Adapter）：在 AGG
       中，适配器是一种包装类，它拦截对原始对象的调用，并添加额外功能。这里的
       pixfmt_amask_adaptor 包装了像素格式渲染器，使得所有绘图操作都会自动考虑 mask
       的覆盖值。

       工作原理：当通过适配器调用 blend_color_hspan 时，适配器会从 mask
       中读取当前行每个像素的灰度值，然后将这些值作为覆盖因子（covers
       数组）传递给底层的混合函数。这样，每个像素的最终颜色 = 背景颜色 × (1 - α) +
       前景颜色 × α，其中 α 不仅包含颜色本身的透明度，还乘以 mask 的灰度值/255。

       效果：由于 mask
       填充了从上到下的垂直渐变，最终图像也会呈现从上到下逐渐显现的透明度渐变（上方区域颜色很淡，下方区域颜色正常）。
       */
        // 将最终图像写入 PPM 文件（主缓冲区中的数据已经包含了 mask 的影响）。
        // [comments/t02_pixel_formats_2.png]
        write_ppm(buffer, frame_width, frame_height, "t02_pixel_formats_alpha_mask.ppm");

        // NOTE: 主缓冲区改成 黑色背景
        reset_buffer(0);
        draw_pixel();
        // diff: 阿尔法掩膜作为分离的 α 通道工作 混合了渲染的原件。
        // diff: 它包含8位值，这让你可以 剪裁 所有这些都绘制成任意形状，且具有完美的抗锯齿
        // [comments/t02_pixel_formats_3.png]
        write_ppm(buffer, frame_width, frame_height, "t02_pixel_formats_alpha_mask2.ppm");

        // 释放动态分配的 alpha-mask 缓冲区。
        delete[] amask_buf;
    }

    delete[] buffer;
    return 0;
}