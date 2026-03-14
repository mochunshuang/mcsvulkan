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
#include <cctype>
#include <set>
#include <stack>
#include <filesystem>
namespace fs = std::filesystem;

// 去除 UTF-8 BOM
std::string removeBOM(const std::string &line)
{
    if (line.size() >= 3 && (unsigned char)line[0] == 0xEF &&
        (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
        return line.substr(3);
    return line;
}

// 将 UTF-8 字符转换为 Unicode 码点
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

// 解析转义序列（返回转义后的字符，并移动指针）
char parseEscape(const char *&p)
{
    if (*p != '\\')
        return 0;
    ++p;
    char c = *p++;
    switch (c)
    {
    case '\\':
        return '\\';
    case '\'':
        return '\'';
    case '"':
        return '"';
    case 'n':
        return '\n';
    case 't':
        return '\t';
    case 'r':
        return '\r';
    default:
        return c;
    }
}

// 解析单个字符字面量（例如 'A' 或 '\''），返回码点，p 指向结束后的位置
std::optional<uint32_t> parseCharLiteral(const char *&p)
{
    if (*p != '\'')
        return std::nullopt;
    ++p; // 跳过 '
    std::string literal;
    while (*p && *p != '\'')
    {
        if (*p == '\\')
            literal.push_back(parseEscape(p));
        else
            literal.push_back(*p++);
    }
    if (*p != '\'')
        return std::nullopt;
    ++p; // 跳过结束的 '
    const char *tmp = literal.c_str();
    if (auto cp = utf8_to_codepoint(tmp))
        return *cp;
    return std::nullopt;
}

// 解析字符串字面量（例如 "ABC"），返回所有码点，p 指向结束后的位置
std::vector<uint32_t> parseStringLiteral(const char *&p)
{
    std::vector<uint32_t> result;
    if (*p != '"')
        return result;
    ++p; // 跳过 "
    std::string literal;
    while (*p && *p != '"')
    {
        if (*p == '\\')
            literal.push_back(parseEscape(p));
        else
            literal.push_back(*p++);
    }
    if (*p != '"')
    {
        result.clear();
        return result;
    }
    ++p; // 跳过 "
    const char *tmp = literal.c_str();
    while (*tmp)
    {
        if (auto cp = utf8_to_codepoint(tmp))
            result.push_back(*cp);
        else
            break;
    }
    return result;
}

// 解析数字（十进制或十六进制）
std::optional<uint32_t> parseNumber(const char *&p)
{
    char *end;
    long val = strtol(p, &end, 0);
    if (end == p)
        return std::nullopt;
    p = end;
    return static_cast<uint32_t>(val);
}

// 解析范围 [start, end]（返回起始和结束码点），p 指向结束后的位置
std::optional<std::pair<uint32_t, uint32_t>> parseRange(const char *&p)
{
    if (*p != '[')
        return std::nullopt;
    ++p; // 跳过 [
    // 跳过空白
    while (std::isspace(*p))
        ++p;

    std::optional<uint32_t> start;
    if (*p == '\'')
        start = parseCharLiteral(p);
    else
        start = parseNumber(p);
    if (!start)
        return std::nullopt;

    while (std::isspace(*p))
        ++p;
    if (*p != ',')
        return std::nullopt;
    ++p; // 跳过 ,
    while (std::isspace(*p))
        ++p;

    std::optional<uint32_t> end;
    if (*p == '\'')
        end = parseCharLiteral(p);
    else
        end = parseNumber(p);
    if (!end)
        return std::nullopt;

    while (std::isspace(*p))
        ++p;
    if (*p != ']')
        return std::nullopt;
    ++p; // 跳过 ]
    return std::make_pair(*start, *end);
}

// 解析一行字符集描述（来自文件或直接字符串），line_num 用于打印信息（-1 时不打印）
void parseLine(const std::string &line, std::vector<uint32_t> &out, int line_num = -1)
{
    const char *p = line.c_str();
    size_t before = out.size();

    while (*p)
    {
        // 跳过空白和逗号
        while (*p && (std::isspace(*p) || *p == ','))
            ++p;
        if (!*p)
            break;

        if (*p == '\'')
        {
            if (auto cp = parseCharLiteral(p))
                out.push_back(*cp);
            else
                std::cerr << "Warning: invalid character literal"
                          << (line_num > 0 ? " at line " + std::to_string(line_num) : "")
                          << "\n";
        }
        else if (*p == '"')
        {
            auto chars = parseStringLiteral(p);
            out.insert(out.end(), chars.begin(), chars.end());
        }
        else if (*p == '[')
        {
            auto range = parseRange(p);
            if (range)
            {
                uint32_t start = range->first, end = range->second;
                if (start <= end)
                {
                    for (uint32_t cp = start; cp <= end; ++cp)
                        out.push_back(cp);
                }
                else
                {
                    std::cerr << "Warning: invalid range start > end"
                              << (line_num > 0 ? " at line " + std::to_string(line_num)
                                               : "")
                              << "\n";
                }
            }
            else
            {
                std::cerr << "Warning: invalid range"
                          << (line_num > 0 ? " at line " + std::to_string(line_num) : "")
                          << "\n";
            }
        }
        else
        {
            const char *numStart = p;
            if (auto num = parseNumber(p))
            {
                out.push_back(*num);
            }
            else
            {
                // 无法识别，跳过当前 token
                while (*p && !std::isspace(*p) && *p != ',')
                    ++p;
                std::cerr << "Warning: unrecognized token"
                          << (line_num > 0 ? " at line " + std::to_string(line_num) : "")
                          << "\n";
            }
        }
    }

    if (line_num > 0 && out.size() > before)
    {
        std::println("Line {}: parsed {} characters", line_num, out.size() - before);
    }
}

// 递归解析字符集文件（支持 @include）
void parseCharsetFile(const std::string &filename, std::vector<uint32_t> &out,
                      std::set<std::string> &included, int &lineCounter)
{
    fs::path canonical = fs::weakly_canonical(filename);
    std::string canonStr = canonical.string();
    if (included.count(canonStr))
    {
        std::cerr << "Warning: recursive @include detected: " << filename << "\n";
        return;
    }
    included.insert(canonStr);

    std::ifstream file(filename);
    if (!file.is_open())
        throw std::runtime_error("Cannot open charset file: " + filename);

    std::string line;
    while (std::getline(file, line))
    {
        ++lineCounter;
        line = removeBOM(line);
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
            continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);
        if (line.empty() || line[0] == '#')
            continue;

        // 处理 @include
        if (line.compare(0, 8, "@include") == 0 && line.size() > 8 &&
            std::isspace(line[8]))
        {
            size_t quoteStart = line.find('"', 8);
            if (quoteStart == std::string::npos)
            {
                std::cerr << "Warning: invalid @include syntax at line " << lineCounter
                          << "\n";
                continue;
            }
            size_t quoteEnd = line.find('"', quoteStart + 1);
            if (quoteEnd == std::string::npos)
            {
                std::cerr << "Warning: invalid @include syntax at line " << lineCounter
                          << "\n";
                continue;
            }
            std::string includeFile =
                line.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
            fs::path baseDir = fs::path(filename).parent_path();
            fs::path includePath = baseDir / includeFile;
            parseCharsetFile(includePath.string(), out, included, lineCounter);
            continue;
        }

        parseLine(line, out, lineCounter);
    }
}

// 从文件读取字符集
std::vector<uint32_t> readCharsetFromFile(const std::string &filename)
{
    std::vector<uint32_t> result;
    std::set<std::string> included;
    int lineCounter = 0;
    parseCharsetFile(filename, result, included, lineCounter);
    return result;
}

// 从字符串解析字符集
std::vector<uint32_t> readCharsetFromString(const std::string &str)
{
    std::vector<uint32_t> result;
    parseLine(str, result, 1); // 视为第一行
    return result;
}

int main(int argc, char *argv[])
{
    std::ignore = std::setlocale(LC_ALL, "en_US.utf8");

    std::string input_path;
    std::string output_path;
    std::string charset_file;
    std::string charset_string;

    // 解析命令行
    if (argc == 4 && argv[1][0] != '-')
    {
        // 位置参数模式：<input> <output> <charset_file>
        input_path = argv[1];
        output_path = argv[2];
        charset_file = argv[3];
    }
    else
    {
        // 命名参数模式
        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (arg == "-font" && i + 1 < argc)
            {
                input_path = argv[++i];
            }
            else if (arg == "-out" && i + 1 < argc)
            {
                output_path = argv[++i];
            }
            else if (arg == "-charset" && i + 1 < argc)
            {
                charset_file = argv[++i];
            }
            else if (arg == "-chars" && i + 1 < argc)
            {
                charset_string = argv[++i];
            }
            else if (arg == "-help" || arg == "--help")
            {
                std::println(
                    stderr,
                    "Usage: {} [options]\n"
                    "  -font <input_font>       Input font file\n"
                    "  -out <output_font>       Output subset font\n"
                    "  -charset <charset.txt>   Character set file\n"
                    "  -chars <string>          Character set specification in-line\n"
                    "  Legacy: <input_font> <output_font> <charset.txt>\n",
                    argv[0]);
                return 0;
            }
            else
            {
                std::cerr << "Unknown argument: " << arg << "\n";
                return 1;
            }
        }
    }

    if (input_path.empty() || output_path.empty())
    {
        std::cerr << "Missing required arguments: -font and -out (or legacy mode)\n";
        return 1;
    }

    std::vector<uint32_t> unicodes;
    if (!charset_string.empty())
    {
        unicodes = readCharsetFromString(charset_string);
    }
    else if (!charset_file.empty())
    {
        try
        {
            unicodes = readCharsetFromFile(charset_file);
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error reading charset: " << e.what() << "\n";
            return 1;
        }
    }
    else
    {
        std::cerr << "No character set specified. Use -charset or -chars.\n";
        return 1;
    }

    if (unicodes.empty())
    {
        std::cerr << "No valid Unicode characters found in charset.\n";
        return 1;
    }

    // 去重
    std::set<uint32_t> unique_set(unicodes.begin(), unicodes.end());
    unicodes.assign(unique_set.begin(), unique_set.end());

    std::println("Loaded {} unique Unicode characters ({} total entries)",
                 unique_set.size(), unicodes.size());

    // 打印示例
    if (unicodes.size() <= 10)
    {
        std::print("Characters: ");
        for (auto cp : unicodes)
            std::print("U+{:04X} ", cp);
        std::println("");
    }
    else
    {
        std::print("First 5 characters: ");
        for (int i = 0; i < 5; ++i)
            std::print("U+{:04X} ", unicodes[i]);
        std::println("");
    }

    // 打开字体
    hb_blob_t *blob = hb_blob_create_from_file(input_path.c_str());
    if (!blob || !hb_blob_get_length(blob))
    {
        std::cerr << "Failed to open font file: " << input_path << "\n";
        return 1;
    }
    hb_face_t *face = hb_face_create(blob, 0);
    hb_blob_destroy(blob);

    // 创建 subset 输入
    hb_subset_input_t *input = hb_subset_input_create_or_fail();
    if (!input)
    {
        std::cerr << "Failed to create subset input.\n";
        return 1;
    }

    hb_set_t *unicode_set = hb_subset_input_unicode_set(input);
    for (uint32_t cp : unicodes)
        hb_set_add(unicode_set, cp);

    hb_face_t *subset_face = hb_subset_or_fail(face, input);
    hb_subset_input_destroy(input);
    hb_face_destroy(face);

    if (!subset_face)
    {
        std::cerr << "Subset failed! Possibly font does not contain some characters.\n";
        return 1;
    }

    hb_blob_t *subset_blob = hb_face_reference_blob(subset_face);
    std::ofstream out(output_path, std::ios::binary);
    if (!out)
    {
        std::cerr << "Cannot open output file: " << output_path << "\n";
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

    std::println("Subset font saved to {} ({} bytes, {} characters)", output_path, length,
                 unicodes.size());
    return 0;
}