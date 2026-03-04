#include <msdfgen.h>
#include <msdfgen-ext.h>

#include <ext/save-png.h>

using namespace msdfgen;

#include <fstream>
#include <algorithm>

static void savePPM(const Bitmap<float, 3> &bitmap, const char *filename)
{
    std::ofstream out(filename, std::ios::binary);
    if (!out)
    {
        // 可以在此处添加错误处理（如抛出异常或打印消息）
        return;
    }

    int w = bitmap.width();
    int h = bitmap.height();

    // 写入 PPM 头部
    out << "P6\n" << w << " " << h << "\n255\n";

    // 逐像素写入 RGB 数据
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const float *pixel = bitmap(x, y);
            // 钳位到 [0,1] 并转换为字节
            unsigned char r =
                static_cast<unsigned char>(std::clamp(pixel[0], 0.0f, 1.0f) * 255.0f);
            unsigned char g =
                static_cast<unsigned char>(std::clamp(pixel[1], 0.0f, 1.0f) * 255.0f);
            unsigned char b =
                static_cast<unsigned char>(std::clamp(pixel[2], 0.0f, 1.0f) * 255.0f);

            out.write(reinterpret_cast<const char *>(&r), 1);
            out.write(reinterpret_cast<const char *>(&g), 1);
            out.write(reinterpret_cast<const char *>(&b), 1);
        }
    }
}

int main()
{
    if (FreetypeHandle *ft = initializeFreetype())
    {
        if (FontHandle *font = loadFont(ft, R"(C:\Windows\Fonts\arialbd.ttf)"))
        {
            Shape shape;
            if (loadGlyph(shape, font, 'A', FONT_SCALING_EM_NORMALIZED))
            {
                shape.normalize();
                //                      max. angle
                edgeColoringSimple(shape, 3.0);
                //          output width, height
                Bitmap<float, 3> msdf(32, 32);
                //                            scale, translation (in em's)
                SDFTransformation t(Projection(32.0, Vector2(0.125, 0.125)),
                                    Range(0.125));
                generateMSDF(msdf, shape, t);
                msdfgen::savePng(msdf, "msdfgen.png");
                savePPM(msdf, "msdfgen.ppm");
            }
            destroyFont(font);
        }
        deinitializeFreetype(ft);
    }
    return 0;
}