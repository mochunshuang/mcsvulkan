#pragma once
// ============================================================================
// table12_app/glue.hpp — 平台胶水（不是 Host，也不碰 Session 真值）
//
//   init_font / init_model(JSON) / read_clipboard / write_clipboard /
//   ime_preedit / apply_cursor / trace
//
//   这些都是"宿主对平台做 I/O、把结果交给核心"的函数：不计算几何、不写
//   Model/State（write_clipboard 是平台副作用；init_* 只在创建时喂数据）。
//
//   ⚠ 本头是 app 层，依赖 demo 框架里已有的类型与句柄（nlohmann::json、
//   uiAsciiGlyphs、GLFWwindow、Win32 imm32），所以必须在这些定义之后 include：
//     test_ui_ability12_table.cpp 的 "framework 定义" 之后、host.hpp 之前。
// ============================================================================

namespace table_glue
{
    // ---- platform helpers (GLFW / clipboard / IME / cursor)
    inline std::string read_clipboard(GLFWwindow *win)
    {
        if (win == nullptr)
            return {};
        const char *s = glfwGetClipboardString(win);
        return s == nullptr ? std::string{} : std::string{s};
    }
    inline void write_clipboard(GLFWwindow *win, std::string_view text)
    {
        if (win == nullptr || text.empty())
            return;
        glfwSetClipboardString(win, std::string{text}.c_str());
    }
    inline std::string ime_preedit(GLFWwindow *win) noexcept
    {
        if (win == nullptr)
            return {};
        struct api_t
        {
            void *(*get_ctx)(void *){};
            long (*get_str)(void *, unsigned long, void *, long){};
            int (*rel_ctx)(void *, void *){};
        };
        static const api_t api = [] {
            api_t a{};
            HMODULE imm = ::LoadLibraryW(L"imm32.dll");
            if (imm == nullptr)
                return a;
            a.get_ctx = reinterpret_cast<void *(*)(void *)>(
                ::GetProcAddress(imm, "ImmGetContext"));
            a.get_str = reinterpret_cast<long (*)(void *, unsigned long, void *, long)>(
                ::GetProcAddress(imm, "ImmGetCompositionStringW"));
            a.rel_ctx = reinterpret_cast<int (*)(void *, void *)>(
                ::GetProcAddress(imm, "ImmReleaseContext"));
            return a;
        }();
        if (api.get_ctx == nullptr || api.get_str == nullptr || api.rel_ctx == nullptr)
            return {};
        HWND hwnd = glfwGetWin32Window(win);
        if (hwnd == nullptr)
            return {};
        void *himc = api.get_ctx(hwnd);
        if (himc == nullptr)
            return {};
        std::string out;
        constexpr unsigned long kGcsCompStr = 0x0008UL;
        const long bytes = api.get_str(himc, kGcsCompStr, nullptr, 0);
        if (bytes > 0)
        {
            std::wstring w(static_cast<std::size_t>(bytes) / sizeof(wchar_t), L'\0');
            api.get_str(himc, kGcsCompStr, w.data(), bytes);
            const int need =
                ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
            if (need > 0)
            {
                out.resize(static_cast<std::size_t>(need));
                ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                      out.data(), need, nullptr, nullptr);
            }
        }
        api.rel_ctx(hwnd, himc);
        return out;
    }
    inline void apply_cursor(GLFWwindow *win, table::Cursor c) noexcept
    {
        if (win == nullptr)
            return;
        static GLFWcursor *pool[7] = {};
        static int current = -1;
        const int idx = static_cast<int>(c);
        if (idx == current)
            return;
        if (idx >= 0 && pool[idx] == nullptr)
        {
            static constexpr int kShape[7] = {
                GLFW_ARROW_CURSOR,         GLFW_IBEAM_CURSOR,
                GLFW_RESIZE_EW_CURSOR,     GLFW_RESIZE_NS_CURSOR,
                GLFW_RESIZE_NWSE_CURSOR,   GLFW_RESIZE_NESW_CURSOR,
                GLFW_POINTING_HAND_CURSOR,
            };
            pool[idx] = glfwCreateStandardCursor(kShape[idx]);
        }
        current = idx;
        glfwSetCursor(win, pool[idx]);
    }

    // ---- font / model (one-time init, ownership moved into Binding)
    inline void init_font(table::Font &F)
    {
        F.px = 15.0F;
        for (const auto &[cp, g] : uiAsciiGlyphs())
        {
            table::GlyphMetrics m{};
            m.uv = g.uv;
            m.plane = g.plane;
            m.advance = g.advance;
            m.tex = g.tex;
            m.samp = g.samp;
            m.font_id = g.shaderFont;
            m.px_range = g.pxRange;
            m.y_down = g.yDown;
            F.put(cp, m);
        }
        std::println("[table] font glyphs: {}", F.ascii.size());
    }

    inline std::string cell_text(const nlohmann::json &v)
    {
        if (v.is_string())
            return v.get<std::string>();
        if (v.is_null())
            return {};
        return v.dump();
    }

    inline constexpr const char *kTableJson = R"json(
{
  "style": { "header_h": 40, "row_h": 30, "row_gap": 2, "bar": 8,
             "radius": 10, "cell_pad": 10, "frozen": 1 },
  "columns": [
    { "key": "name",     "title": "Name",     "width": 220, "min": 120, "max": 320, "group": "Identity" },
    { "key": "status",   "title": "Status",   "width": 116, "min": 90,  "max": 180, "align": "left", "group": "Identity",
      "render": "tag",
      "tags": { "ready": "#4fbf7a", "busy": "#e0a83c", "error": "#e0574f" } },
    { "key": "size",     "title": "Size",     "width": 100, "min": 60,  "max": 160, "align": "right", "group": "Metrics" },
    { "key": "modified", "title": "Modified", "width": 160, "min": 100, "max": 220, "group": "Metrics" },
    { "key": "kind",     "title": "Kind",     "width": 140, "min": 80,  "max": 220,
      "group": "Source", "render": "dot",
      "tags": { "gltf": "#5aa9e6", "png": "#b07ad8", "bin": "#6ec2a0", "json": "#e0a83c" } },
    { "key": "path",     "title": "Path",     "width": 420, "min": 200, "max": 900, "group": "Source" },
    { "key": "sha",      "title": "SHA",      "width": 300, "min": 120, "max": 600, "group": "Source" },
    { "key": "note",     "title": "Note",     "width": 200, "min": 80,  "max": 300, "group": "Source" }
  ],
  "rows": []
}
)json";

    inline void init_model(table::model::Table &d, table::intent::Theme &p)
    {
        auto j = nlohmann::json::parse(kTableJson);
        static constexpr const char *kStatus[3] = {"ready", "busy", "error"};
        static constexpr const char *kKind[4] = {"gltf", "png", "bin", "json"};
        static constexpr const char *kNote[4] = {"cached", "3 blocks", "hot", "cold"};
        for (int i = 0; i < 60; ++i)
        {
            j["rows"].push_back(nlohmann::json::array(
                {std::format("asset_{:02d}.gltf", i), kStatus[i % 3],
                 std::format("{}.{} KB", 12 + i * 7, i % 10),
                 std::format("2026-{:02d}-{:02d}", 1 + (i % 12), 1 + (i % 28)),
                 kKind[i % 4],
                 std::format("assets/scene/sub{:02d}/asset_{:02d}.gltf", i / 12, i),
                 std::format("sha256:{:012x}", 0x9f2a1c3d4e5fULL + i * 7919ULL),
                 kNote[i % 4]}));
        }

        d = {};
        for (const auto &c : j.at("columns"))
        {
            table::model::Column col;
            col.key = c.value("key", std::string{});
            col.title = c.value("title", col.key);
            col.w = c.value("width", 140.0F);
            col.min_w = c.value("min", 48.0F);
            col.max_w = c.value("max", 640.0F);
            col.weight = c.value("weight", 0.0F);
            const auto a = c.value("align", std::string{"left"});
            col.a = a == "center"  ? table::Align::center
                    : a == "right" ? table::Align::right
                                   : table::Align::left;
            col.filterable = c.value("filterable", true);
            col.wrap = c.value("wrap", false);
            col.group = c.value("group", std::string{});
            const auto rd = c.value("render", std::string{"text"});
            col.render = rd == "tag"   ? table::RenderKind::tag
                         : rd == "bar" ? table::RenderKind::bar
                         : rd == "dot" ? table::RenderKind::dot
                                       : table::RenderKind::text;
            if (c.contains("tags") && c["tags"].is_object())
                for (const auto &[k, v] : c["tags"].items())
                    if (v.is_string())
                        col.tags.emplace_back(
                            k, table::hex_color(v.get<std::string>(),
                                                   {0.5F, 0.5F, 0.5F, 1.0F}));
            d.cols.push_back(std::move(col));
        }
        d.cells.resize(d.cols.size());
        std::uint64_t next_id = 1;
        for (const auto &r : j.at("rows"))
        {
            for (std::size_t c = 0; c < d.cols.size(); ++c)
            {
                std::string text;
                if (r.is_array())
                {
                    if (c < r.size())
                        text = cell_text(r[c]);
                }
                else if (r.is_object() && r.contains(d.cols[c].key))
                    text = cell_text(r.at(d.cols[c].key));
                d.cells[c].push_back({std::move(text), {}});
            }
            d.ids.push_back(table::RowId{next_id++});
        }

        if (j.contains("style"))
        {
            const auto &s = j.at("style");
            p.header_h = s.value("header_h", p.header_h);
            p.row_h = s.value("row_h", p.row_h);
            p.row_gap = s.value("row_gap", p.row_gap);
            p.bar = s.value("bar", p.bar);
            p.radius = s.value("radius", p.radius);
            p.cell_pad = s.value("cell_pad", p.cell_pad);
            p.frozen = s.value("frozen", p.frozen);
            p.row_select = s.value("row_select", p.row_select);
            p.wrap = s.value("wrap", p.wrap);
            p.group_h = s.value("group_h", p.group_h);
        }
        std::println("[table] json: {} cols x {} rows, frozen={}", d.cols.size(),
                     d.ids.size(), p.frozen);
    }
} // namespace table_glue
