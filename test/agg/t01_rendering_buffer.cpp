#include <cstdio>
#include <cstring>
#include <print>
#include <agg_rendering_buffer.h>

using std::fclose;
using std::fopen;
using std::fwrite;

enum
{
    frame_width = 320,
    frame_height = 200
};

// Writing the buffer to a .PPM file, assuming it has
// RGB-structure, one byte per color component
//--------------------------------------------------
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

/*
第一行: [黑][黑][黑][黑][黑]  ← 上边框（整行全黑）
第二行: [黑][原][原][原][黑]  ← 左右边框（第一列和最后一列黑）
第三行: [黑][原][原][原][黑]  ← 左右边框（第一列和最后一列黑）
第四行: [黑][黑][黑][黑][黑]  ← 下边框（整行全黑）
*/
// Draw a black frame around the rendering buffer, assuming it has
// RGB-structure, one byte per color component
//--------------------------------------------------
void draw_black_frame(agg::rendering_buffer &rbuf)
{
    constexpr std::size_t BYTES_PER_PIXEL = 3; // RGB格式，每像素3字节
    unsigned i;
    // 1. 处理左右边框
    for (i = 0; i < rbuf.height(); ++i) // 遍历每一行
    {
        unsigned char *p = rbuf.row_ptr(i); // 获取第i行的指针

        // 设置左边框第一个像素为黑色
        *p++ = 0; // R = 0
        *p++ = 0; // G = 0
        *p++ = 0; // B = 0

        // 跳过中间像素，定位到最后一个像素
        p += (rbuf.width() - 2) * BYTES_PER_PIXEL; //-2是因为前面+1了

        // 设置右边框最后一个像素为黑色
        *p++ = 0; // R = 0
        *p++ = 0; // G = 0
        *p++ = 0; // B = 0
    }

    // 2. 处理上边框
    memset(rbuf.row_ptr(0), 0, rbuf.width() * BYTES_PER_PIXEL); // 第一行全黑

    // 3. 处理下边框
    memset(rbuf.row_ptr(rbuf.height() - 1), 0,
           rbuf.width() * BYTES_PER_PIXEL); // 最后一行全黑
}

/*

NOTE: 解释和接触 渲染缓冲区 这个概念。说白了就是一块内存
这里几乎所有内容都是“手动”编码的。我们唯一使用的类是rendering_buffer。
这个类对内存中的像素格式一无所知，它只是保留一个指针数组指向每一行。
分配和释放缓冲区的实际内存是你的责任
*/
int main()
{
    // In the first example we do the following:
    //--------------------
    // Allocate the buffer.
    // Clear the buffer, for now "manually"
    // Create the rendering buffer object
    // Do something simple, draw a diagonal line
    // Write the buffer to agg_test.ppm
    // Free memory
    constexpr std::size_t BYTES_PER_PIXEL = 3; // RGB格式，每像素3字节
    unsigned char *buffer =
        new unsigned char[frame_width * frame_height * BYTES_PER_PIXEL];

    const auto reset_buffer = [&] {
        memset(buffer, 255, frame_width * frame_height * BYTES_PER_PIXEL);
    };
    reset_buffer();

    // NOTE: 渲染缓冲区 [comments/t01_rendering_buffer_2.png]
    agg::rendering_buffer rbuf(buffer, frame_width, frame_height,
                               frame_width * BYTES_PER_PIXEL);

    unsigned i;
    for (i = 0; i < rbuf.height() / 2; ++i)
    {
        // NOTE: 填像素而已。找到位置，填值就这么简单
        // [comments/t01_rendering_buffer_1.png]
        //  Get the pointer to the beginning of the i-th row (Y-coordinate)
        //  and shift it to the i-th position, that is, X-coordinate.
        //---------------
        unsigned char *ptr = rbuf.row_ptr(i) + i * BYTES_PER_PIXEL;

        // 像素可视化 [comments/t01_rendering_buffer_0.png]
        // PutPixel, very sophisticated, huh? :)
        //-------------
        *ptr++ = 127; // R
        *ptr++ = 200; // G
        *ptr++ = 98;  // B
    }

    draw_black_frame(rbuf);
    write_ppm(buffer, frame_width, frame_height, "agg_test.ppm");

    // rendering_buffer info: width: 320,height: 200,stride: 960,stride_abs: 960
    // NOTE: 960 /320 == 3.也就是一个像素3字节. 长和宽，都说单位都是像素。stride系内存对齐
    std::println("rendering_buffer info: width: {},height: {},stride: {},stride_abs: {}",
                 rbuf.width(), rbuf.height(), rbuf.stride(), rbuf.stride_abs());

    /*
步幅[stride]——在类型中测量的行的“步幅”（大步幅）。 类rendering_buffer为“，因此，
该值以字节为单位。步幅决定了 的物理宽度 记忆中只有一行。如果该值为负，则方向为
Y轴是倒置的，也就是说，指向最后一个 缓冲区的一排
*/

    const auto draw_diagonal_line = [](agg::rendering_buffer &rbuf) noexcept {
        for (unsigned i = 0; i < rbuf.height() / 2; ++i)
        {
            unsigned char *ptr = rbuf.row_ptr(i) + i * BYTES_PER_PIXEL;
            *ptr++ = 127; // R
            *ptr++ = 200; // G
            *ptr++ = 98;  // B
        }
    };
    {
        // -y 下效果 [comments/t01_rendering_buffer_3.png]
        // 原理： [comments/t01_rendering_buffer_4.png]
        reset_buffer();
        agg::rendering_buffer rbuf(buffer, frame_width, frame_height,
                                   -frame_width * BYTES_PER_PIXEL);
        draw_diagonal_line(rbuf);
        write_ppm(buffer, frame_width, frame_height, "agg_test2.ppm");
    }

    // diff: attach . 本质是view，以某种方式访问 缓冲区
    {
        reset_buffer();
        agg::rendering_buffer rbuf(buffer, frame_width, frame_height,
                                   frame_width * BYTES_PER_PIXEL);

        // Draw the outer black frame
        //------------------------
        draw_black_frame(rbuf);

        // Attach to the part of the buffer,
        // with 20 pixel margins at each side.
        // 定义四周留出的边距（像素）
        constexpr int MARGIN = 20;

        // 基于原图尺寸和边距计算子区域的新尺寸
        constexpr int SUB_WIDTH = frame_width - (2 * MARGIN);   // 320 - 40 = 280
        constexpr int SUB_HEIGHT = frame_height - (2 * MARGIN); // 200 - 40 = 160

        // NOTE: 原理，重新解释内存范围：[comments/t01_rendering_buffer_5.png]
        //  使用常量计算内存偏移量
        rbuf.attach(buffer +
                        (frame_width * BYTES_PER_PIXEL * MARGIN) // 跳过顶部的 MARGIN 行
                        + (BYTES_PER_PIXEL * MARGIN),            // 跳过左侧的 MARGIN 列
                    SUB_WIDTH, SUB_HEIGHT,
                    frame_width * BYTES_PER_PIXEL // 步长保持不变
        );

        // Draw a diagonal line
        //------------------------
        draw_diagonal_line(rbuf);

        // Draw the inner black frame
        //------------------------
        draw_black_frame(rbuf);

        // Write to a file [comments/t01_rendering_buffer_6.png]
        //------------------------
        write_ppm(buffer, frame_width, frame_height, "agg_attach.ppm");
    }
    // -y
    {
        reset_buffer();
        agg::rendering_buffer rbuf(buffer, frame_width, frame_height, frame_width * 3);

        // Draw the outer black frame
        //------------------------
        draw_black_frame(rbuf);

        // Attach to the part of the buffer,
        // with 20 pixel margins at each side and negative 'stride'
        rbuf.attach(buffer + frame_width * BYTES_PER_PIXEL * 20 + // initial Y-offset
                        BYTES_PER_PIXEL * 20,                     // initial X-offset
                    frame_width - 40, frame_height - 40,
                    -frame_width * BYTES_PER_PIXEL // Negate the stride
        );

        // Draw a diagonal line
        //------------------------
        draw_diagonal_line(rbuf);

        // Draw the inner black frame
        //------------------------
        draw_black_frame(rbuf);

        // Write to a file [comments/t01_rendering_buffer_7.png]
        //------------------------
        write_ppm(buffer, frame_width, frame_height, "agg_attach-y.ppm");
    }

    delete[] buffer;
    return 0;
}