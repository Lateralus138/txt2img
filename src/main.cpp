#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <optional>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>

#ifndef _WIN32
#include <unistd.h>
#else
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#endif

#include "ArgumentParser.h"

// FreeType
#include <ft2build.h>
#include FT_FREETYPE_H

// STB Image Write
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace fs = std::filesystem;

#include "version.h"

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

static constexpr std::string_view DEFAULT_BG = "#f6efdf";
static constexpr std::string_view DEFAULT_FG = "#222222";

// Stored as packed uint32 (0x00RRGGBB) for O(1) lookup without string alloc.
// Key is the raw ANSI code string; value is the RGB packed int.
static const std::unordered_map<std::string_view, uint32_t> ANSI_COLORS = {
    {"30", 0x000000}, {"31", 0xcc0000}, {"32", 0x4e9a06}, {"33", 0xc4a000},
    {"34", 0x3465a4}, {"35", 0x75507b}, {"36", 0x06989a}, {"37", 0xd3d7cf},
    {"90", 0x555753}, {"91", 0xef2929}, {"92", 0x8ae234}, {"93", 0xfce94f},
    {"94", 0x729fcf}, {"95", 0xad7fa8}, {"96", 0x34e2e2}, {"97", 0xeeeeec},
};

// ─────────────────────────────────────────────────────────────────────────────
// Color
// ─────────────────────────────────────────────────────────────────────────────

struct Color {
    uint8_t r, g, b;
};

// Returns 0-15 for a valid hex digit, -1 otherwise.
static constexpr int hexdigit(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Parse a single channel from either one nibble (#rgb) or two (#rrggbb).
// Returns -1 if any digit is invalid.
static constexpr int parse_channel(char hi, char lo) noexcept {
    int h = hexdigit(hi), l = hexdigit(lo);
    if (h < 0 || l < 0) return -1;
    return (h << 4) | l;
}

// Parse "#rrggbb" / "#rgb" hex strings.  Returns {0,0,0} on error.
// Both formats share one code path: #rgb expands each nibble to xx (n -> nn).
static Color hexToRgb(std::string_view hex) noexcept {
    if (hex.empty()) return {};
    if (hex[0] == '#') hex.remove_prefix(1);

    int r, g, b;
    if (hex.size() == 3) {
        r = parse_channel(hex[0], hex[0]);
        g = parse_channel(hex[1], hex[1]);
        b = parse_channel(hex[2], hex[2]);
    } else if (hex.size() == 6) {
        r = parse_channel(hex[0], hex[1]);
        g = parse_channel(hex[2], hex[3]);
        b = parse_channel(hex[4], hex[5]);
    } else {
        return {};
    }

    if (r < 0 || g < 0 || b < 0) return {};
    return { static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b) };
}

// Pack r,g,b into a uint32 for storage in Style (avoids a 7-byte string).
static constexpr uint32_t packRgb(uint8_t r, uint8_t g, uint8_t b) noexcept {
    return (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) <<  8) |
            static_cast<uint32_t>(b);
}

static constexpr Color unpackRgb(uint32_t packed) noexcept {
    return { static_cast<uint8_t>((packed >> 16) & 0xFF),
             static_cast<uint8_t>((packed >>  8) & 0xFF),
             static_cast<uint8_t>( packed        & 0xFF) };
}

// ─────────────────────────────────────────────────────────────────────────────
// Style — stores color as packed uint32 to avoid per-char string allocation
// ─────────────────────────────────────────────────────────────────────────────

struct Style {
    uint32_t fg_packed = 0;   // packed RGB
    bool bold   = false;
    bool italic = false;

    bool operator==(const Style&) const noexcept = default;
};

struct StyledChar {
    char32_t ch;
    Style    style;
};

struct Run {
    std::u32string text;
    Style          style;
};

// ─────────────────────────────────────────────────────────────────────────────
// UTF-8 → UTF-32
// ─────────────────────────────────────────────────────────────────────────────

static std::u32string utf8_to_utf32(std::string_view utf8) {
    std::u32string utf32;
    utf32.reserve(utf8.size()); // upper bound; shrinks implicitly

    for (size_t i = 0; i < utf8.size(); ) {
        auto byte = static_cast<unsigned char>(utf8[i]);
        uint32_t cp;
        size_t extra;

        if (byte <= 0x7F) {
            cp = byte; extra = 0;
        } else if (byte <= 0xDF) {
            cp = byte & 0x1F; extra = 1;
        } else if (byte <= 0xEF) {
            cp = byte & 0x0F; extra = 2;
        } else {
            cp = byte & 0x07; extra = 3;
        }

        ++i;
        for (size_t j = 0; j < extra && i < utf8.size(); ++j, ++i) {
            cp = (cp << 6) | (static_cast<unsigned char>(utf8[i]) & 0x3F);
        }
        utf32 += static_cast<char32_t>(cp);
    }
    return utf32;
}

// ─────────────────────────────────────────────────────────────────────────────
// ANSI parsing  (manual scanner — no regex, no string copies in the hot path)
// ─────────────────────────────────────────────────────────────────────────────

// Split a semicolon-separated codes string (e.g. "1;38;2;255;0;128") into
// a flat span represented as a small_vector of string_views into `src`.
static void split_codes(std::string_view src, std::vector<std::string_view>& out) {
    out.clear();
    size_t start = 0;
    for (size_t i = 0; i <= src.size(); ++i) {
        if (i == src.size() || src[i] == ';') {
            out.push_back(src.substr(start, i - start));
            start = i + 1;
        }
    }
    if (out.empty()) out.push_back("0");
}

static std::vector<StyledChar> parse_ansi(std::string_view text, uint32_t default_fg_packed) {
    std::vector<StyledChar> result;
    result.reserve(text.size()); // conservative; many chars are ASCII

    uint32_t fg = default_fg_packed;
    bool bold   = false;
    bool italic = false;

    std::vector<std::string_view> codes; // reused across escapes
    codes.reserve(8);

    size_t i = 0;
    while (i < text.size()) {
        // Fast path: accumulate plain ASCII run
        size_t run_start = i;
        while (i < text.size() && text[i] != '\x1b' && static_cast<unsigned char>(text[i]) < 0x80) {
            ++i;
        }
        // Emit ASCII run
        for (size_t j = run_start; j < i; ++j) {
            result.push_back({ static_cast<char32_t>(text[j]), { fg, bold, italic } });
        }

        if (i >= text.size()) break;

        // Non-ASCII multi-byte codepoint
        if (text[i] != '\x1b') {
            auto byte = static_cast<unsigned char>(text[i]);
            uint32_t cp; size_t extra;
            if      (byte <= 0xDF) { cp = byte & 0x1F; extra = 1; }
            else if (byte <= 0xEF) { cp = byte & 0x0F; extra = 2; }
            else                   { cp = byte & 0x07; extra = 3; }
            ++i;
            for (size_t j = 0; j < extra && i < text.size(); ++j, ++i)
                cp = (cp << 6) | (static_cast<unsigned char>(text[i]) & 0x3F);
            result.push_back({ static_cast<char32_t>(cp), { fg, bold, italic } });
            continue;
        }

        // ESC [ … m  sequence
        if (i + 1 >= text.size() || text[i + 1] != '[') {
            result.push_back({ static_cast<char32_t>('\x1b'), { fg, bold, italic } });
            ++i;
            continue;
        }

        size_t seq_start = i + 2; // skip ESC [
        size_t seq_end   = seq_start;
        while (seq_end < text.size() && text[seq_end] != 'm') ++seq_end;

        if (seq_end >= text.size()) { // unterminated sequence — emit raw
            for (; i <= seq_end && i < text.size(); ++i)
                result.push_back({ static_cast<char32_t>(text[i]), { fg, bold, italic } });
            continue;
        }

        split_codes(text.substr(seq_start, seq_end - seq_start), codes);
        i = seq_end + 1; // advance past 'm'

        for (size_t ci = 0; ci < codes.size(); ++ci) {
            const auto code = codes[ci];
            if (code.empty() || code == "0") {
                fg = default_fg_packed; bold = false; italic = false;
            } else if (code == "1")  { bold   = true;  }
            else if (code == "3")    { italic = true;  }
            else if (code == "22")   { bold   = false; }
            else if (code == "23")   { italic = false; }
            else if (code == "39")   { fg = default_fg_packed; }
            else if (code == "38" && ci + 2 < codes.size()) {
                if (codes[ci + 1] == "5") {
                    ci += 2; // 256-colour index — skip (unimplemented)
                } else if (codes[ci + 1] == "2" && ci + 4 < codes.size()) {
                    auto toi = [](std::string_view sv) -> int {
                        int v = 0;
                        for (char c : sv) { if (c >= '0' && c <= '9') v = v * 10 + (c - '0'); }
                        return v;
                    };
                    int r = toi(codes[ci + 2]);
                    int g = toi(codes[ci + 3]);
                    int b = toi(codes[ci + 4]);
                    fg = packRgb(static_cast<uint8_t>(r),
                                 static_cast<uint8_t>(g),
                                 static_cast<uint8_t>(b));
                    ci += 4;
                }
            } else {
                // Single lookup — one hash probe via find()
                auto it = ANSI_COLORS.find(code);
                if (it != ANSI_COLORS.end()) fg = it->second;
            }
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tab expansion
// ─────────────────────────────────────────────────────────────────────────────

static std::vector<StyledChar> expand_tabs_styled(
    const std::vector<StyledChar>& in, int tabsize)
{
    std::vector<StyledChar> result;
    result.reserve(in.size());
    int col = 0;
    for (const auto& sc : in) {
        if (sc.ch == U'\t') {
            int spaces = tabsize - (col % tabsize);
            for (int s = 0; s < spaces; ++s)
                result.push_back({ U' ', sc.style });
            col += spaces;
        } else {
            result.push_back(sc);
            col = (sc.ch == U'\n') ? 0 : col + 1;
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Word-wrap
// ─────────────────────────────────────────────────────────────────────────────

static std::vector<std::vector<StyledChar>> split_logical_lines(const std::vector<StyledChar>& in) {
    std::vector<std::vector<StyledChar>> logical;
    std::vector<StyledChar> cur;
    for (const auto& sc : in) {
        if (sc.ch == U'\n') { logical.push_back(std::move(cur)); cur.clear(); }
        else cur.push_back(sc);
    }
    logical.push_back(std::move(cur));
    return logical;
}

static void wrap_line(std::vector<std::vector<StyledChar>>& out, const std::vector<StyledChar>& line, size_t w) {
    if (line.empty()) { out.emplace_back(); return; }
    for (size_t pos = 0; pos < line.size(); ) {
        size_t len = std::min(w, line.size() - pos);
        if (pos + len < line.size()) {
            size_t bp = len;
            while (bp > 0 && line[pos + bp - 1].ch != U' ') --bp;
            if (bp > 0) len = bp;
        }

        std::vector<StyledChar> chunk(line.begin() + pos, line.begin() + pos + len);
        if (pos + len < line.size() && len == w && line[pos + len - 1].ch != U' ' && chunk.size() > 1) {
            chunk.back() = { U'-', chunk.back().style };
            --len;
        }
        out.push_back(std::move(chunk));
        pos += len;
        if (pos < line.size() && line[pos].ch == U' ') ++pos;
    }
}

static std::vector<std::vector<StyledChar>> wrap_styled_text(const std::vector<StyledChar>& in, int width) {
    auto logical = split_logical_lines(in);
    std::vector<std::vector<StyledChar>> wrapped;
    wrapped.reserve(logical.size());
    for (const auto& line : logical) wrap_line(wrapped, line, static_cast<size_t>(width));
    return wrapped;
}

// ─────────────────────────────────────────────────────────────────────────────
// Compress StyledChar lines into runs of same-style characters
// ─────────────────────────────────────────────────────────────────────────────

static std::vector<Run> convert_to_runs(const std::vector<StyledChar>& line) {
    if (line.empty()) return {};
    std::vector<Run> runs;
    runs.reserve(8);
    std::u32string text;
    text.reserve(line.size());
    text += line[0].ch;
    Style style = line[0].style;

    for (size_t i = 1; i < line.size(); ++i) {
        if (line[i].style == style) {
            text += line[i].ch;
        } else {
            runs.push_back({ std::move(text), style });
            text.clear();
            text += line[i].ch;
            style = line[i].style;
        }
    }
    runs.push_back({ std::move(text), style });
    return runs;
}

static std::vector<std::vector<Run>> lines_to_runs(const std::vector<std::vector<StyledChar>>& lines) {
    std::vector<std::vector<Run>> run_lines;
    run_lines.reserve(lines.size());
    for (const auto& line : lines) run_lines.push_back(convert_to_runs(line));
    return run_lines;
}

// ─────────────────────────────────────────────────────────────────────────────
// Font discovery
// ─────────────────────────────────────────────────────────────────────────────

struct FontPaths {
    std::string normal, bold, italic, bold_italic;
};

static std::vector<FontPaths> get_font_families() {
    std::vector<FontPaths> families;

#ifdef _WIN32
    const fs::path win = "C:/Windows/Fonts";
    auto w = [&](const char* name) { return (win / name).string(); };
    families.push_back({ w("consola.ttf"),       w("consolab.ttf"),        w("consolai.ttf"),        w("consolaz.ttf")       });
    families.push_back({ w("CascadiaMono.ttf"),  w("CascadiaMonoBold.ttf"), "",                       ""                      });
    families.push_back({ w("lucon.ttf"),         "",                        "",                       ""                      });
    families.push_back({ w("cour.ttf"),          w("courbd.ttf"),           w("couri.ttf"),           w("courbi.ttf")         });

#elif defined(__APPLE__)
    const std::vector<fs::path> mac_dirs = {
        "/Library/Fonts", "/System/Library/Fonts",
        fs::path(getenv("HOME") ? getenv("HOME") : "") / "Library/Fonts"
    };
    auto find_mac = [&](std::initializer_list<const char*> names) -> std::string {
        for (const auto& d : mac_dirs)
            for (const char* n : names)
                if (fs::exists(d / n)) return (d / n).string();
        return {};
    };
    families.push_back({ find_mac({"Menlo.ttc","Menlo-Regular.ttf"}), find_mac({"Menlo-Bold.ttf"}),      find_mac({"Menlo-Italic.ttf"}),      find_mac({"Menlo-BoldItalic.ttf"})      });
    families.push_back({ find_mac({"SF-Mono-Regular.otf"}),          find_mac({"SF-Mono-Bold.otf"}),     find_mac({"SF-Mono-RegularItalic.otf"}), find_mac({"SF-Mono-BoldItalic.otf"}) });
    families.push_back({ find_mac({"Monaco.ttf"}),                   {},                                 {},                                  {}                                       });
    families.push_back({ find_mac({"Courier New.ttf","CourierNew.ttf"}), find_mac({"Courier New Bold.ttf","CourierNewBold.ttf"}),
                         find_mac({"Courier New Italic.ttf","CourierNewItalic.ttf"}), find_mac({"Courier New Bold Italic.ttf","CourierNewBoldItalic.ttf"}) });

#else // Linux / BSD
    std::vector<fs::path> linux_dirs = {
        "/usr/share/fonts/truetype/dejavu",
        "/usr/share/fonts/truetype/liberation",
        "/usr/share/fonts/truetype/freefont",
        "/usr/share/fonts/TTF",
        "/usr/local/share/fonts"
    };
    if (const char* home = getenv("HOME")) {
        linux_dirs.push_back(fs::path(home) / ".local/share/fonts");
        linux_dirs.push_back(fs::path(home) / ".fonts");
    }
    auto find_lx = [&](std::initializer_list<const char*> names) -> std::string {
        for (const auto& d : linux_dirs)
            for (const char* n : names)
                if (fs::exists(d / n)) return (d / n).string();
        return {};
    };
    families.push_back({ find_lx({"DejaVuSansMono.ttf"}),      find_lx({"DejaVuSansMono-Bold.ttf"}),    find_lx({"DejaVuSansMono-Oblique.ttf"}),    find_lx({"DejaVuSansMono-BoldOblique.ttf"})  });
    families.push_back({ find_lx({"LiberationMono-Regular.ttf"}), find_lx({"LiberationMono-Bold.ttf"}), find_lx({"LiberationMono-Italic.ttf"}),     find_lx({"LiberationMono-BoldItalic.ttf"})  });
    families.push_back({ find_lx({"FreeMono.ttf"}),             find_lx({"FreeMonoBold.ttf"}),           find_lx({"FreeMonoOblique.ttf"}),           find_lx({"FreeMonoBoldOblique.ttf"})         });
    families.push_back({ find_lx({"UbuntuMono-R.ttf"}),         find_lx({"UbuntuMono-B.ttf"}),          find_lx({"UbuntuMono-RI.ttf"}),             find_lx({"UbuntuMono-BI.ttf"})               });
#endif
    return families;
}

// ─────────────────────────────────────────────────────────────────────────────
// FontVariants — RAII wrapper so we never leak FT_Faces
// ─────────────────────────────────────────────────────────────────────────────

struct FontVariants {
    FT_Face normal      = nullptr;
    FT_Face bold        = nullptr;
    FT_Face italic      = nullptr;
    FT_Face bold_italic = nullptr;

    // Unique faces for cleanup (variant pointers may alias normal)
    ~FontVariants() {
        // Collect distinct faces without a heap allocation
        FT_Face seen[4] = { nullptr, nullptr, nullptr, nullptr };
        int n = 0;
        auto release = [&](FT_Face f) {
            if (!f) return;
            for (int i = 0; i < n; ++i) if (seen[i] == f) return;
            seen[n++] = f;
            FT_Done_Face(f);
        };
        release(normal); release(bold); release(italic); release(bold_italic);
    }

    // Non-copyable; movable
    FontVariants() = default;
    FontVariants(const FontVariants&) = delete;
    FontVariants& operator=(const FontVariants&) = delete;
    FontVariants(FontVariants&& o) noexcept
        : normal(o.normal), bold(o.bold), italic(o.italic), bold_italic(o.bold_italic)
    { o.normal = o.bold = o.italic = o.bold_italic = nullptr; }

    [[nodiscard]] FT_Face face_for(const Style& s) const noexcept {
        if (s.bold && s.italic) return bold_italic;
        if (s.bold)             return bold;
        if (s.italic)           return italic;
        return normal;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Image
// ─────────────────────────────────────────────────────────────────────────────

struct Image {
    int width, height;
    std::vector<uint8_t> data;

    Image(int w, int h, Color bg) : width(w), height(h), data(static_cast<size_t>(w * h) * 3) {
        // Fill with background color using a single pass
        uint8_t* p   = data.data();
        uint8_t* end = p + data.size();
        while (p < end) { p[0] = bg.r; p[1] = bg.g; p[2] = bg.b; p += 3; }
    }

    // Integer alpha blend — avoids float math per pixel
    void setPixel(int x, int y, Color fg, uint8_t alpha) noexcept {
        if (static_cast<unsigned>(x) >= static_cast<unsigned>(width) ||
            static_cast<unsigned>(y) >= static_cast<unsigned>(height)) return;
        uint8_t* p = data.data() + (static_cast<size_t>(y) * width + x) * 3;
        if (alpha == 255) {
            p[0] = fg.r; p[1] = fg.g; p[2] = fg.b;
        } else if (alpha > 0) {
            // Fixed-point blend: out = bg*(256-a) + fg*a  >>  8
            const uint32_t a  = alpha;
            const uint32_t ia = 256 - a;
            p[0] = static_cast<uint8_t>((p[0] * ia + fg.r * a) >> 8);
            p[1] = static_cast<uint8_t>((p[1] * ia + fg.g * a) >> 8);
            p[2] = static_cast<uint8_t>((p[2] * ia + fg.b * a) >> 8);
        }
    }

    bool save(const std::string& path) const {
        return stbi_write_png(path.c_str(), width, height, 3, data.data(), width * 3) != 0;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Glyph rendering
// ─────────────────────────────────────────────────────────────────────────────

static void render_run(Image& img, int& x, int y, const Run& run, FT_Face face, Color fg) {
    for (char32_t ch : run.text) {
        if (FT_Load_Char(face, ch, FT_LOAD_RENDER)) continue;
        const FT_Bitmap& bm = face->glyph->bitmap;
        const int gx = x + face->glyph->bitmap_left;
        const int gy = y - face->glyph->bitmap_top;

        for (unsigned r = 0; r < bm.rows; ++r)
            for (unsigned c = 0; c < bm.width; ++c)
                img.setPixel(gx + c, gy + r, fg, bm.buffer[r * bm.pitch + c]);

        x += face->glyph->advance.x >> 6;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Named config — parsed once from ArgumentParser, passed around cleanly
// ─────────────────────────────────────────────────────────────────────────────

struct Config {
    int         width;
    std::optional<int> height;
    bool        trim_height;
    bool        raw;
    std::string out_name;
    std::string out_dir;
    uint32_t    fg_packed;   // pre-parsed color
    uint32_t    bg_packed;
    std::string font_path;
    int         font_size;
    int         padding;
    int         tabsize;
};

// ─────────────────────────────────────────────────────────────────────────────
// Helpers extracted from main()
// ─────────────────────────────────────────────────────────────────────────────

static void setup_args(argparser::ArgumentParser& p) {
    using namespace argparser;
    p.add_switch_pair("v", "version",          "\tShow version information",    SwitchType::FLAG,      Requirement::OPTIONAL);
    p.add_switch_pair("h", "help",             "\tShow help message",           SwitchType::FLAG,      Requirement::OPTIONAL);
    p.add_switch_pair("t", "text",             "\tDirect text input",           SwitchType::PARAMETER, Requirement::OPTIONAL);
    p.add_switch_pair("W", "width",            "\tImage width",                 SwitchType::PARAMETER, Requirement::REQUIRED);
    p.add_switch_pair("H", "height",           "\tImage height",                SwitchType::PARAMETER, Requirement::OPTIONAL);
    p.add_switch_pair("T", "trim-height",      "\tTrim height to content",      SwitchType::FLAG,      Requirement::OPTIONAL);
    p.add_switch_pair("r", "raw",              "\tNo ANSI parsing",             SwitchType::FLAG,      Requirement::OPTIONAL);
    p.add_switch_pair("n", "file-name",        "\tOutput filename",             SwitchType::PARAMETER, Requirement::OPTIONAL);
    p.add_switch_pair("o", "output-directory", "\tOutput directory",            SwitchType::PARAMETER, Requirement::OPTIONAL);
    p.add_switch("fg",        "\tForeground colour",   SwitchType::PARAMETER, Requirement::OPTIONAL);
    p.add_switch("bg",        "\tBackground colour",   SwitchType::PARAMETER, Requirement::OPTIONAL);
    p.add_switch("font",      "\tPath to TTF/OTF",     SwitchType::PARAMETER, Requirement::OPTIONAL);
    p.add_switch("font-size", "\tFont size",           SwitchType::PARAMETER, Requirement::OPTIONAL);
    p.add_switch("padding",   "\tImage padding",       SwitchType::PARAMETER, Requirement::OPTIONAL);
    p.add_switch("tabsize",   "\tTab stop width",      SwitchType::PARAMETER, Requirement::OPTIONAL);
}

static Config parse_config(const argparser::ArgumentParser& parser) {
    auto get_val = [&](std::string_view k, std::string_view def) { return parser.get_switch_value(k).value_or(std::string(def)); };
    auto get_int = [&](std::string_view k, int def) { return std::stoi(get_val(k, std::to_string(def))); };

    Config cfg;
    cfg.width       = std::stoi(parser.get_switch_value("width").value());
    cfg.trim_height = parser.is_switch_set("trim-height");
    cfg.raw         = parser.is_switch_set("raw");
    cfg.out_name    = get_val("file-name", "output.png");
    cfg.out_dir     = get_val("output-directory", ".");
    cfg.font_path   = get_val("font", "");
    cfg.font_size   = get_int("font-size", 20);
    cfg.padding     = get_int("padding", 20);
    cfg.tabsize     = get_int("tabsize", 4);

    if (auto h = parser.get_switch_value("height")) cfg.height = std::stoi(*h);

    auto parse_color = [](std::optional<std::string> hex, std::string_view def) {
        Color c = hexToRgb(hex && !hex->empty() ? *hex : def);
        return packRgb(c.r, c.g, c.b);
    };
    cfg.fg_packed = parse_color(parser.get_switch_value("fg"), DEFAULT_FG);
    cfg.bg_packed = parse_color(parser.get_switch_value("bg"), DEFAULT_BG);
    return cfg;
}

static std::string read_input(const argparser::ArgumentParser& parser) {
    if (auto text_opt = parser.get_switch_value("text")) {
        std::string t = *text_opt;
        for (size_t p = 0; (p = t.find("\\n", p)) != std::string::npos; p++) t.replace(p, 2, "\n");
        return t;
    }

    const std::string path = parser.get_arguments().empty() ? "-" : parser.get_arguments()[0];
    if (path != "-") {
        std::ifstream ifs(path);
        if (!ifs) throw std::runtime_error("Cannot open: " + path);
        return {std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
    }

    if (isatty(fileno(stdin))) std::cerr << "Enter text (Ctrl-D/Z to finish):\n";
    std::string text, line;
    while (std::getline(std::cin, line)) { text += line; text += '\n'; }
    return text;
}

static FontVariants load_fonts(const Config& cfg, FT_Library lib) {
    FontVariants fonts;

    auto load_face = [&](const std::string& path) -> FT_Face {
        if (path.empty()) return nullptr;
        FT_Face face;
        if (FT_New_Face(lib, path.c_str(), 0, &face)) return nullptr;
        FT_Set_Pixel_Sizes(face, 0, cfg.font_size);
        return face;
    };

    // Try user-supplied font first
    if (!cfg.font_path.empty()) {
        fonts.normal = load_face(cfg.font_path);
        if (!fonts.normal)
            std::cerr << "Warning: could not load font '" << cfg.font_path << "'\n";
        else {
            const fs::path p(cfg.font_path);
            const std::string stem = p.stem().string();
            const std::string ext  = p.extension().string();
            const fs::path   par  = p.parent_path();
            fonts.bold        = load_face((par / (stem + "-Bold"       + ext)).string());
            fonts.italic      = load_face((par / (stem + "-Italic"     + ext)).string());
            fonts.bold_italic = load_face((par / (stem + "-BoldItalic" + ext)).string());
        }
    }

    // System font fallback
    if (!fonts.normal) {
        for (const auto& fam : get_font_families()) {
            fonts.normal = load_face(fam.normal);
            if (fonts.normal) {
                fonts.bold        = load_face(fam.bold);
                fonts.italic      = load_face(fam.italic);
                fonts.bold_italic = load_face(fam.bold_italic);
                break;
            }
        }
    }

    if (!fonts.normal) throw std::runtime_error("No usable font found");
    if (!fonts.bold)        fonts.bold        = fonts.normal;
    if (!fonts.italic)      fonts.italic      = fonts.normal;
    if (!fonts.bold_italic) fonts.bold_italic = fonts.normal;
    return fonts;
}

static void render_pages(
    const std::vector<std::vector<Run>>& run_lines,
    const FontVariants& fonts,
    const Config&       cfg,
    int line_total_height, int ascent)
{
    const Color bg = unpackRgb(cfg.bg_packed);

    const int lines_per_page = cfg.height
        ? std::max(1, (*cfg.height - cfg.padding * 2) / line_total_height)
        : static_cast<int>(run_lines.size());

    const int total_pages = std::max(1,
        static_cast<int>(std::ceil(
            static_cast<double>(run_lines.size()) / lines_per_page)));

    fs::create_directories(cfg.out_dir);
    const fs::path base_path(cfg.out_name);
    const fs::path out_dir(cfg.out_dir);

    for (int page = 0; page < total_pages; ++page) {
        const int start_line = page * lines_per_page;
        const int end_line   = std::min(start_line + lines_per_page,
                                        static_cast<int>(run_lines.size()));
        const int n_lines    = end_line - start_line;

        const int img_h = (cfg.height && !cfg.trim_height)
            ? *cfg.height
            : n_lines * line_total_height + cfg.padding * 2;

        Image img(cfg.width, img_h, bg);

        int y = cfg.padding + ascent;
        for (int li = start_line; li < end_line; ++li) {
            int x = cfg.padding;
            for (const auto& run : run_lines[li]) {
                render_run(img, x, y, run, fonts.face_for(run.style),
                           unpackRgb(run.style.fg_packed));
            }
            y += line_total_height;
        }

        // Build output path
        const std::string name = (total_pages > 1)
            ? base_path.stem().string() + "_" + std::to_string(page + 1) + base_path.extension().string()
            : cfg.out_name;
        const std::string out_path = (out_dir / name).string();

        if (!img.save(out_path))
            std::cerr << "Warning: failed to write " << out_path << "\n";
        else
            std::cout << "Saved image to: " << out_path << "\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

static constexpr std::string_view HELP_HEADER =
    "Text 2 Image:"
    "\n\tRender wrapped text into an image."
    "\n\nExamples:"
    "\n\ttxt2img -W <WIDTH> [SWITCHES] [FILE|INPUT]"
    "\n\tcat [FILE] | txt2img -W <WIDTH> [SWITCHES]";

int main(int argc, char* argv[]) {
    try {
        argparser::ArgumentParser parser(argc, argv);
        setup_args(parser);

        try {
            parser.parse();
        } catch (const std::exception&) {
            // If the user passed help or version flags, parse() may have thrown (e.g. --width required).
            // Check for these flags manually to exit cleanly.
            for (int i = 1; i < argc; ++i) {
                std::string_view arg(argv[i]);
                if (arg == "-h" || arg == "--help") {
                    parser.print_help(HELP_HEADER, true);
                    return 0;
                }
                if (arg == "-v" || arg == "--version") {
                    std::cout << "txt2img version " << PROJECT_VERSION << "\n";
                    return 0;
                }
            }
            throw;
        }

        if (parser.is_switch_set("version")) {
            std::cout << "txt2img version " << PROJECT_VERSION << "\n";
            return 0;
        }

        if (parser.is_switch_set("help")) {
            parser.print_help(HELP_HEADER, true);
            return 0;
        }

        const Config cfg        = parse_config(parser);
        const std::string input = read_input(parser);

        // ── FreeType ─────────────────────────────────────────────────────────
        FT_Library library;
        if (FT_Init_FreeType(&library))
            throw std::runtime_error("Could not initialise FreeType");

        // Scope so FontVariants destructor runs before FT_Done_FreeType
        {
            FontVariants fonts = load_fonts(cfg, library);

            // ── Metrics ──────────────────────────────────────────────────────
            const int ascent    =  fonts.normal->size->metrics.ascender   >> 6;
            const int descent   = -fonts.normal->size->metrics.descender  >> 6;  // positive
            const int char_height      = ascent + descent;
            const int line_spacing     = std::max(1, char_height / 5);   // ~20%
            const int line_total_height= char_height + line_spacing;

            FT_Load_Char(fonts.normal, 'M', FT_LOAD_DEFAULT);
            const int char_width = fonts.normal->glyph->advance.x >> 6;
            const int char_wrap_width = std::max(1, (cfg.width - cfg.padding * 2) / char_width);

            // ── Layout pipeline ──────────────────────────────────────────────
            std::vector<StyledChar> styled;
            if (cfg.raw) {
                std::u32string u32 = utf8_to_utf32(input);
                styled.reserve(u32.size());
                for (char32_t c : u32)
                    styled.push_back({ c, { cfg.fg_packed } });
            } else {
                styled = parse_ansi(input, cfg.fg_packed);
            }

            styled = expand_tabs_styled(styled, cfg.tabsize);
            const auto wrapped  = wrap_styled_text(styled, char_wrap_width);
            const auto run_lines= lines_to_runs(wrapped);

            // ── Render ───────────────────────────────────────────────────────
            render_pages(run_lines, fonts, cfg, line_total_height, ascent);
        }

        FT_Done_FreeType(library);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
