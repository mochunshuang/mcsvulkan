#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <hb.h>
#include <hb-ft.h>
#include <hb-subset.h>

#include <cstdio>
#include <cstdlib>
#include <print>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <optional>
#include <cuchar>
#include <clocale>

// ---------- 直接复用 readCharset 和 utf8_to_codepoint ----------
std::optional<uint32_t> utf8_to_codepoint(const char *&str)
{
    if (!str || !*str)
        return std::nullopt;
    std::mbstate_t state{};
    char32_t cp = 0;
    std::size_t result = std::mbrtoc32(&cp, str, 5, &state);
    if (result == (std::size_t)-1 || result == (std::size_t)-3)
        return std::nullopt;
    if (result == (std::size_t)-2)
        return std::nullopt;
    str += (result > 0 ? result : 1);
    return static_cast<uint32_t>(cp);
}

std::vector<uint32_t> readCharset(const std::string &filename)
{
    std::vector<uint32_t> result;
    std::ifstream file(filename);
    if (!file.is_open())
        throw std::runtime_error("Cannot open charset file: " + filename);
    std::string line;
    while (std::getline(file, line))
    {
        // 去除首尾空白
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
            continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);
        if (line.empty() || line[0] == '#')
            continue;

        if (line.size() >= 2 && line[0] == '\'' && line.back() == '\'')
        {
            std::string inside = line.substr(1, line.size() - 2);
            const char *str = inside.c_str();
            if (auto cp = utf8_to_codepoint(str))
                result.push_back(*cp);
            else
                std::cerr << "Warning: invalid UTF-8 inside quotes: " << line << "\n";
            continue;
        }

        char *endptr;
        long val = strtol(line.c_str(), &endptr, 0);
        if (endptr != line.c_str() && *endptr == '\0')
        {
            result.push_back(static_cast<uint32_t>(val));
        }
        else
        {
            const char *str = line.c_str();
            if (auto cp = utf8_to_codepoint(str))
                result.push_back(*cp);
            else
                std::cerr << "Warning: invalid UTF-8: " << line << "\n";
        }
    }
    return result;
}

// ---------- 主程序：字体子集化 ----------
int main(int argc, char *argv[])
{
    std::ignore = std::setlocale(LC_ALL, "en_US.utf8");

    if (argc != 4)
    {
        std::println(stderr,
                     "Usage: {} <input_font> <output_font> <charset.txt>\n  input_font  "
                     ": path to .ttf or .ttc (will use face index 0)\n  output_font : "
                     "path to output subset font (same format as input)\n  charset.txt : "
                     "file containing Unicode characters",
                     argv[0]);
        return 1;
    }

    const char *input_path = argv[1];
    const char *output_path = argv[2];
    const char *charset_path = argv[3];

    // 读取字符集
    std::vector<uint32_t> unicodes;
    try
    {
        unicodes = readCharset(charset_path);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error reading charset: " << e.what() << "\n";
        return 1;
    }
    if (unicodes.empty())
    {
        std::cerr << "No valid Unicode characters found in charset.\n";
        return 1;
    }

    // 打开字体（默认取索引 0，如果是 TTC 也只用第一个字体）
    hb_blob_t *blob = hb_blob_create_from_file(input_path);
    if (!blob || !hb_blob_get_length(blob))
    {
        std::cerr << "Failed to open font file: " << input_path << "\n";
        return 1;
    }
    hb_face_t *face = hb_face_create(blob, 0); // 始终使用索引 0
    hb_blob_destroy(blob);

    // 创建 subset 输入
    hb_subset_input_t *input = hb_subset_input_create_or_fail();
    if (!input)
    {
        std::cerr << "Failed to create subset input.\n";
        return 1;
    }

    // 添加需要保留的 Unicode 码点
    hb_set_t *unicode_set = hb_subset_input_unicode_set(input);
    for (uint32_t cp : unicodes)
    {
        hb_set_add(unicode_set, cp);
    }

    // 执行子集化
    hb_face_t *subset_face = hb_subset_or_fail(face, input);
    hb_subset_input_destroy(input);
    hb_face_destroy(face);

    if (!subset_face)
    {
        std::cerr << "Subset failed!\n";
        return 1;
    }

    // 保存到文件
    hb_blob_t *subset_blob = hb_face_reference_blob(subset_face);
    std::ofstream out(output_path, std::ios::binary);
    if (!out)
    {
        std::cerr << "Cannot open output file: " << output_path << "\n";
        // 清理资源
        hb_blob_destroy(subset_blob);
        hb_face_destroy(subset_face);
        return 1;
    }
    unsigned int length;
    const char *data = hb_blob_get_data(subset_blob, &length);
    out.write(data, length);
    out.close();

    hb_blob_destroy(subset_blob);
    hb_face_destroy(subset_face);

    std::cout << "Subset font saved to " << output_path << "\n";
    return 0;
}