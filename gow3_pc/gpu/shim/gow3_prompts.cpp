// Keyboard, mouse and controller button prompts. Naughty Dog's engine draws the button icons in
// its text ("Press |X| to jump", the menus' legends) from one symbol font: symbol3.fnt with a
// 512x512 atlas (or its colour and alpha in two textures). While the player uses the keyboard and
// mouse, or a controller that is not a PlayStation one, the texture cache uploads a redrawn atlas
// instead: key caps and mice for the bound keys and mouse buttons, or Xbox-style buttons (A B X Y,
// LB RB, LT RT...) for the controller's, drawn here in the same cells from the labels runtime_pad.c
// passes; the guest data stays as it is. The prompts drawn in the world ("Triangle -> chest") are
// separate BC1 pictures of the buttons, stored upside down: the round face buttons, the shoulder
// buttons and the d-pad are recognised by the hash of their first mip and redrawn the same way,
// the bound key in place of the PlayStation symbol (re-encoded to BC1 here). A device switch
// invalidates them, and they upload again at their next use. runtime_pad.c picks the device
// (GOW3_PROMPTS); GOW3_PROMPTS_LOG=1 logs the hash of each candidate upload. The Last of Us
// Remastered's 256x512 atlas is redrawn the same way. Its world prompts and menu tabs come from one
// HUD sprite shader drawing fixed pictures of hud.pak: those draws are recognised by the hash of
// their two textures (known_prompts) and get the redrawn atlas cell bound instead; an unknown
// picture keeps the game's icon.

#include "gow3_prompts.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <xxhash.h>

#include "../gow3gpu.h"
#ifndef GOW3_PROMPTS_TEST
#include "core/memory.h"
#include "video_core/amdgpu/resource.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/image.h"
#include <filesystem>
#include <miniz.h>
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "imstb_truetype.h"

extern "C" const unsigned char gow3_font_ttf[]; // gow3_overlay.cpp: DejaVu Sans

namespace Gow3Prompts {
namespace {

constexpr int Size = 512;
constexpr u32 TlouAtlasWidth = 256, TlouAtlasHeight = 512;
constexpr u64 TlouAtlasHash = 0xcc7e9c9744cf4e67ULL;

// The textures by the XXH3 hash of their first mip's guest data (1D-tiled): the symbol font atlases
// (512x512 RGBA8, or colour and alpha apart) and the button pictures (BC1 with mips, found by
// decoding the paks: 256x256 face and shoulder buttons and d-pad, 512x512 flat "HOLD" buttons in
// two press positions). Found with GOW3_PROMPTS_LOG and a dump of the loaded pictures.
enum class Kind { Rgba, Color, Alpha, Face, Shoulder, Dpad };
struct KnownTexture {
    u64 hash;
    Kind kind;
    int button; // pictures: GOW3_PROMPT_*
    const char* name;
};
constexpr KnownTexture known_textures[] = {
    {0x5db884aa1bae54f9ULL, Kind::Face, GOW3_PROMPT_TRIANGLE, "Triangle picture"},
    {0x7af1abf688d8c824ULL, Kind::Face, GOW3_PROMPT_CIRCLE, "Circle picture"},
    {0x9edd4f2f0f64f6e5ULL, Kind::Face, GOW3_PROMPT_SQUARE, "Square picture"},
};

// symbol3.fnt: glyph id and its cell in the atlas.
struct Cell {
    int id, x, y, w, h;
};
constexpr Cell cells[] = {
    {1, 0, 0, 75, 75},      {2, 0, 75, 64, 64},     {3, 64, 75, 64, 64},    {4, 128, 75, 64, 64},
    {5, 192, 75, 64, 64},   {6, 84, 252, 77, 46},   {7, 0, 139, 64, 60},    {8, 161, 243, 77, 46},
    {9, 64, 139, 64, 60},   {10, 128, 139, 74, 53}, {11, 148, 192, 64, 51}, {12, 75, 0, 75, 75},
    {13, 150, 0, 75, 75},   {14, 0, 252, 84, 50},   {15, 0, 199, 74, 53},   {16, 74, 199, 74, 53},
    {18, 238, 216, 86, 86}, {21, 0, 355, 75, 75},   {24, 0, 302, 74, 53},   {25, 74, 302, 74, 53},
    {29, 148, 298, 64, 51}, {30, 0, 252, 77, 51},   {40, 340, 113, 77, 46}, {41, 256, 0, 64, 60},
    {42, 417, 104, 77, 46}, {43, 320, 0, 64, 60},   {44, 404, 53, 64, 51},  {45, 256, 60, 74, 53},
    {46, 330, 60, 74, 53},  {47, 256, 163, 74, 53}, {48, 330, 163, 74, 53}, {49, 404, 159, 64, 51},
};

// What each glyph shows. 1-30 and 40-49 are two drawings of the same buttons; 10 is the plain
// stick (moving). Share (29, 49) keeps its PlayStation icon: the PC has no such button. The
// touchpad has two cells (14, 30) on the same pixels: drawn once in their common part.
enum class Draw { Key, Pair, Dpad, Stick };
struct Plan {
    int id;
    Draw draw;
    int a, b; // GOW3_PROMPT_*: the key (Key), the pair (Pair), the stick's first direction (Stick)
};
constexpr Plan plans[] = {
    {5, Draw::Key, GOW3_PROMPT_CROSS},       {2, Draw::Key, GOW3_PROMPT_CIRCLE},
    {3, Draw::Key, GOW3_PROMPT_SQUARE},      {4, Draw::Key, GOW3_PROMPT_TRIANGLE},
    {8, Draw::Key, GOW3_PROMPT_L1},          {42, Draw::Key, GOW3_PROMPT_L1},
    {6, Draw::Key, GOW3_PROMPT_R1},          {40, Draw::Key, GOW3_PROMPT_R1},
    {9, Draw::Key, GOW3_PROMPT_L2},          {43, Draw::Key, GOW3_PROMPT_L2},
    {7, Draw::Key, GOW3_PROMPT_R2},          {41, Draw::Key, GOW3_PROMPT_R2},
    {24, Draw::Key, GOW3_PROMPT_L3},         {47, Draw::Key, GOW3_PROMPT_L3},
    {25, Draw::Key, GOW3_PROMPT_R3},         {48, Draw::Key, GOW3_PROMPT_R3},
    {11, Draw::Key, GOW3_PROMPT_OPTIONS},    {44, Draw::Key, GOW3_PROMPT_OPTIONS},
    {30, Draw::Key, GOW3_PROMPT_TOUCHPAD},   {18, Draw::Key, GOW3_PROMPT_UP},
    {21, Draw::Key, GOW3_PROMPT_DOWN},       {12, Draw::Pair, GOW3_PROMPT_LEFT, GOW3_PROMPT_RIGHT},
    {13, Draw::Pair, GOW3_PROMPT_UP, GOW3_PROMPT_DOWN}, {1, Draw::Dpad},
    {10, Draw::Stick, GOW3_PROMPT_LS_UP},    {15, Draw::Stick, GOW3_PROMPT_LS_UP},
    {45, Draw::Stick, GOW3_PROMPT_LS_UP},    {16, Draw::Stick, GOW3_PROMPT_RS_UP},
    {46, Draw::Stick, GOW3_PROMPT_RS_UP},
};

// The Last of Us Remastered's symbol atlas (256x512): each glyph's cell, from symbol3.fnt or its pixels plus 2.
constexpr Cell tlou_cells[] = {
    {101, 3, 3, 70, 70},    {102, 78, 2, 71, 70},   {103, 153, 2, 71, 71},  {104, 3, 77, 71, 70},
    {105, 78, 77, 71, 71},  {106, 157, 81, 53, 52}, {107, 192, 144, 64, 54}, {108, 6, 156, 53, 52},
    {109, 70, 156, 53, 52}, {110, 135, 157, 52, 52}, {111, 1, 216, 71, 55},  {112, 76, 216, 71, 56},
    {113, 150, 217, 55, 54}, {114, 2, 275, 55, 54}, {117, 170, 272, 56, 55}, {118, 0, 330, 56, 55},
    {122, 140, 383, 74, 53}, {124, 61, 389, 76, 47}, {125, 0, 440, 55, 43},  {126, 55, 440, 55, 43},
};

// Its d-pad drawings: none, left and right, up and down, up, down lit.
constexpr Plan tlou_plans[] = {
    {101, Draw::Dpad},
    {102, Draw::Pair, GOW3_PROMPT_LEFT, GOW3_PROMPT_RIGHT},
    {103, Draw::Pair, GOW3_PROMPT_UP, GOW3_PROMPT_DOWN},
    {104, Draw::Key, GOW3_PROMPT_UP},
    {105, Draw::Key, GOW3_PROMPT_DOWN},
    {106, Draw::Key, GOW3_PROMPT_CIRCLE},
    {107, Draw::Key, GOW3_PROMPT_OPTIONS},
    {108, Draw::Key, GOW3_PROMPT_SQUARE},
    {109, Draw::Key, GOW3_PROMPT_TRIANGLE},
    {110, Draw::Key, GOW3_PROMPT_CROSS},
    {111, Draw::Stick, GOW3_PROMPT_LS_UP},
    {112, Draw::Stick, GOW3_PROMPT_RS_UP},
    {113, Draw::Key, GOW3_PROMPT_L3},
    {114, Draw::Key, GOW3_PROMPT_R3},
    {117, Draw::Key, GOW3_PROMPT_R2},
    {118, Draw::Key, GOW3_PROMPT_L2},
    {122, Draw::Stick, GOW3_PROMPT_LS_UP},
    {124, Draw::Key, GOW3_PROMPT_TOUCHPAD},
    {125, Draw::Key, GOW3_PROMPT_R1},
    {126, Draw::Key, GOW3_PROMPT_L1},
};

struct Rgb {
    float r, g, b;
};
Rgb Mix(Rgb a, Rgb b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
// The PlayStation icons' palette: warm light rims and glow, dark bodies, orange highlights.
constexpr Rgb glow{0.95f, 0.92f, 0.85f};
constexpr Rgb rim{0.84f, 0.81f, 0.75f};
constexpr Rgb body_top{0.34f, 0.33f, 0.31f};
constexpr Rgb body_bottom{0.12f, 0.12f, 0.11f};
constexpr Rgb ink{0.98f, 0.97f, 0.93f};
constexpr Rgb accent_top{1.0f, 0.68f, 0.30f};
constexpr Rgb accent_bottom{0.85f, 0.44f, 0.12f};
constexpr Rgb groove{0.06f, 0.06f, 0.06f};
constexpr float Margin = 4.0f; // the font's 2-pixel padding and room for the glow

// Signed distances in pixels (negative inside).
float RoundBox(float px, float py, float cx, float cy, float hw, float hh, float r) {
    r = std::min(r, std::min(hw, hh));
    const float qx = std::fabs(px - cx) - hw + r, qy = std::fabs(py - cy) - hh + r;
    return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
}
float Segment(float px, float py, float ax, float ay, float bx, float by) {
    const float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
    const float h = std::clamp((pax * bax + pay * bay) / (bax * bax + bay * bay), 0.0f, 1.0f);
    return std::hypot(pax - bax * h, pay - bay * h);
}
// Exact triangle distance (corners v: x0 y0 x1 y1 x2 y2), after Inigo Quilez.
float Triangle(float px, float py, const float (&v)[6]) {
    const float e0x = v[2] - v[0], e0y = v[3] - v[1], e1x = v[4] - v[2], e1y = v[5] - v[3];
    const float e2x = v[0] - v[4], e2y = v[1] - v[5];
    const float w0x = px - v[0], w0y = py - v[1], w1x = px - v[2], w1y = py - v[3];
    const float w2x = px - v[4], w2y = py - v[5];
    const auto edge = [](float wx, float wy, float ex, float ey) {
        const float h = std::clamp((wx * ex + wy * ey) / (ex * ex + ey * ey), 0.0f, 1.0f);
        const float dx = wx - ex * h, dy = wy - ey * h;
        return dx * dx + dy * dy;
    };
    const float s = e0x * e2y - e0y * e2x > 0 ? 1.0f : -1.0f;
    const float d = std::min({edge(w0x, w0y, e0x, e0y), edge(w1x, w1y, e1x, e1y), edge(w2x, w2y, e2x, e2y)});
    const float side = std::min({s * (w0x * e0y - w0y * e0x), s * (w1x * e1y - w1y * e1x),
                                 s * (w2x * e2y - w2y * e2x)});
    return side > 0 ? -std::sqrt(d) : std::sqrt(d);
}

// Premultiplied colour and coverage per texel, and which texels belong to redrawn cells.
struct Canvas {
    int w = Size, h = Size;
    std::vector<float> texels = std::vector<float>(size_t(w) * h * 4, 0.0f);
    std::vector<u8> drawn = std::vector<u8>(size_t(w) * h, 0);
};

class Painter {
public:
    // clear: the area starts transparent (atlas cells); otherwise drawing goes over the canvas.
    Painter(Canvas& canvas_, int x0_, int y0_, int x1_, int y1_, bool clear = true)
        : canvas{canvas_}, x0{x0_}, y0{y0_}, x1{x1_}, y1{y1_} {
        for (int y = y0; clear && y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                std::fill_n(&canvas.texels[(y * canvas.w + x) * 4], 4, 0.0f);
                canvas.drawn[y * canvas.w + x] = 1;
            }
        }
    }
    // Colour per point (gradients).
    template <typename Sdf, typename Color>
    void FillShaded(Sdf&& sdf, Color&& color, float opacity = 1.0f) {
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                const float cover = std::clamp(0.5f - sdf(px, py), 0.0f, 1.0f) * opacity;
                if (cover > 0.0f) {
                    Over(x, y, color(px, py), cover);
                }
            }
        }
    }
    void Fill(auto&& sdf, Rgb color, float opacity = 1.0f) {
        FillShaded(sdf, [color](float, float) { return color; }, opacity);
    }
    // A soft glow around a shape (as the PlayStation icons have), fading over `radius` pixels.
    void Halo(auto&& sdf, float radius, float opacity, Rgb color = glow) {
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const float d = sdf(x + 0.5f, y + 0.5f);
                const float t = d <= 0.0f ? 1.0f : std::max(0.0f, 1.0f - d / radius);
                if (t > 0.0f) {
                    Over(x, y, color, t * t * opacity);
                }
            }
        }
    }
    // A picture (premultiplied RGBA floats, sw x sh) fitted into a bw x bh box centred on (cx, cy),
    // its aspect kept; smaller than the picture: several filtered samples per pixel.
    void Picture(const float* src, int sw, int sh, float cx, float cy, float bw, float bh) {
        const float scale = std::min(bw / sw, bh / sh);
        if (!(scale > 0.0f)) {
            return;
        }
        const float left = cx - sw * scale / 2, top = cy - sh * scale / 2;
        const int n = std::clamp(int(std::ceil(1.0f / scale)), 1, 8);
        const auto at = [&](int x, int y) {
            return &src[(std::clamp(y, 0, sh - 1) * sw + std::clamp(x, 0, sw - 1)) * 4];
        };
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                float sum[4]{};
                for (int j = 0; j < n; ++j) {
                    for (int i = 0; i < n; ++i) {
                        const float sx = (x + (i + 0.5f) / n - left) / scale - 0.5f;
                        const float sy = (y + (j + 0.5f) / n - top) / scale - 0.5f;
                        if (sx < -0.5f || sy < -0.5f || sx > sw - 0.5f || sy > sh - 0.5f) {
                            continue;
                        }
                        const int ix = int(std::floor(sx)), iy = int(std::floor(sy));
                        const float fx = sx - ix, fy = sy - iy;
                        const float *a = at(ix, iy), *b = at(ix + 1, iy), *c = at(ix, iy + 1), *d = at(ix + 1, iy + 1);
                        for (int k = 0; k < 4; ++k) {
                            sum[k] += (a[k] * (1 - fx) + b[k] * fx) * (1 - fy) + (c[k] * (1 - fx) + d[k] * fx) * fy;
                        }
                    }
                }
                const float inv = 1.0f / float(n * n);
                if (sum[3] * inv > 0.0f) {
                    float* p = &canvas.texels[(y * canvas.w + x) * 4];
                    const float a = std::min(sum[3] * inv, 1.0f);
                    for (int k = 0; k < 3; ++k) {
                        p[k] = sum[k] * inv + p[k] * (1.0f - a);
                    }
                    p[3] = a + p[3] * (1.0f - a);
                }
            }
        }
    }

private:
    void Over(int x, int y, Rgb c, float a) {
        float* p = &canvas.texels[(y * canvas.w + x) * 4];
        p[0] = c.r * a + p[0] * (1.0f - a);
        p[1] = c.g * a + p[1] * (1.0f - a);
        p[2] = c.b * a + p[2] * (1.0f - a);
        p[3] = a + p[3] * (1.0f - a);
    }
    Canvas& canvas;
    int x0, y0, x1, y1;
};

const stbtt_fontinfo* Font() {
    static stbtt_fontinfo info;
    static const bool ok =
        stbtt_InitFont(&info, gow3_font_ttf, stbtt_GetFontOffsetForIndex(gow3_font_ttf, 0)) != 0;
    return ok ? &info : nullptr;
}

std::vector<int> Codepoints(const std::string& text) {
    std::vector<int> out;
    for (size_t i = 0; i < text.size();) {
        const u8 c = u8(text[i]);
        const int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
        int cp = extra ? c & (0x3f >> extra) : c;
        for (int k = 1; k <= extra && i + k < text.size(); ++k) {
            cp = (cp << 6) | (u8(text[i + k]) & 0x3f);
        }
        out.push_back(cp);
        i += extra + 1;
    }
    return out;
}

// A label as signed distance fields (stb_truetype), centred by its ink on (cx, cy) with
// capitals `cap` pixels tall, condensed horizontally by `condense` (long key names stay
// legible in a square key) and made smaller to fit `max_width`.
class Text {
public:
    static constexpr int Pad = 8; // pixels of distance kept around each glyph (glows)
    static constexpr float DistScale = 128.0f / Pad;

    Text(const std::string& text, float cx, float cy, float cap, float max_width, float condense_ = 1.0f)
        : condense{condense_}, centre{cx} {
        const stbtt_fontinfo* font = Font();
        if (!font || text.empty()) {
            return;
        }
        const auto cps = Codepoints(text);
        float scale = Scale(cap);
        float x_min, x_max, y_min, y_max;
        Extent(cps, scale, x_min, x_max, y_min, y_max);
        if ((x_max - x_min) * condense > max_width && x_max > x_min) {
            scale *= max_width / ((x_max - x_min) * condense);
            Extent(cps, scale, x_min, x_max, y_min, y_max);
        }
        const float dx = cx - (x_min + x_max) / 2, dy = cy - (y_min + y_max) / 2;
        float pen = 0.0f;
        for (size_t i = 0; i < cps.size(); ++i) {
            int w = 0, h = 0, xoff = 0, yoff = 0;
            u8* sdf = stbtt_GetCodepointSDF(font, scale, cps[i], Pad, 128, DistScale, &w, &h, &xoff, &yoff);
            if (sdf) {
                glyphs.push_back({std::vector<u8>(sdf, sdf + w * h), w, h, dx + std::floor(pen) + xoff,
                                  dy + yoff});
                stbtt_FreeSDF(sdf, nullptr);
            }
            pen += Advance(cps, i, scale);
        }
    }

    // Capitals `cap` pixels tall: the label's ink width.
    static float Width(const std::string& text, float cap, float condense = 1.0f) {
        if (!Font() || text.empty()) {
            return 0.0f;
        }
        float x_min, x_max, y_min, y_max;
        Extent(Codepoints(text), Scale(cap), x_min, x_max, y_min, y_max);
        return (x_max - x_min) * condense;
    }

    float Distance(float px, float py) const {
        float best = 1e6f; // outside every glyph's field: far away
        const float qx = centre + (px - centre) / condense;
        for (const auto& g : glyphs) {
            const float u = qx - g.x - 0.5f, v = py - g.y - 0.5f;
            if (u < 0.0f || v < 0.0f || u > g.w - 1.0f || v > g.h - 1.0f) {
                continue;
            }
            const int iu = std::min(int(u), g.w - 2 < 0 ? 0 : g.w - 2);
            const int iv = std::min(int(v), g.h - 2 < 0 ? 0 : g.h - 2);
            const float fu = u - iu, fv = v - iv;
            auto at = [&](int x, int y) {
                return float(g.sdf[std::min(y, g.h - 1) * g.w + std::min(x, g.w - 1)]);
            };
            const float value = (at(iu, iv) * (1 - fu) + at(iu + 1, iv) * fu) * (1 - fv) +
                                (at(iu, iv + 1) * (1 - fu) + at(iu + 1, iv + 1) * fu) * fv;
            best = std::min(best, (128.0f - value) / DistScale);
        }
        if (best >= 1e6f) {
            return best;
        }
        // Condensed: horizontal distances shrink, vertical ones do not; in between.
        return best * (1.0f + condense) / 2;
    }

private:
    struct Glyph {
        std::vector<u8> sdf;
        int w, h;
        float x, y;
    };
    std::vector<Glyph> glyphs;
    float condense, centre;

    static float Scale(float cap) {
        int x0, y0, x1, y1;
        stbtt_GetCodepointBox(Font(), 'H', &x0, &y0, &x1, &y1);
        return cap / float(std::max(y1, 1));
    }
    // Pen advance after glyph i (kerning; tightened a little: key labels are short and bold).
    static float Advance(const std::vector<int>& cps, size_t i, float scale) {
        int advance = 0, bearing = 0;
        stbtt_GetCodepointHMetrics(Font(), cps[i], &advance, &bearing);
        float x = advance * scale;
        if (i + 1 < cps.size()) {
            x += stbtt_GetCodepointKernAdvance(Font(), cps[i], cps[i + 1]) * scale;
            x -= 0.04f * advance * scale;
        }
        return x;
    }
    // The ink box relative to the first pen position on the baseline (y down).
    static void Extent(const std::vector<int>& cps, float scale, float& x_min, float& x_max,
                       float& y_min, float& y_max) {
        x_min = y_min = 1e9f;
        x_max = y_max = -1e9f;
        float pen = 0.0f;
        for (size_t i = 0; i < cps.size(); ++i) {
            int x0, y0, x1, y1;
            if (stbtt_GetCodepointBox(Font(), cps[i], &x0, &y0, &x1, &y1)) {
                x_min = std::min(x_min, std::floor(pen) + x0 * scale);
                x_max = std::max(x_max, std::floor(pen) + x1 * scale);
                y_min = std::min(y_min, -y1 * scale);
                y_max = std::max(y_max, -y0 * scale);
            }
            pen += Advance(cps, i, scale);
        }
        if (x_min > x_max) {
            x_min = x_max = y_min = y_max = 0.0f;
        }
    }
};

float LabelCondense(size_t chars) {
    return chars <= 1 ? 1.0f : chars == 2 ? 0.9f : 0.78f;
}

// Keys and the mouse are drawn in two passes (glows first) so a cluster's glows stay behind its
// keys.
enum class Pass { Glow, Body };

void DrawKey(Painter& p, Pass pass, float cx, float cy, float w, float h, const std::string& label) {
    const float side = std::min(w, h);
    const float r = 0.22f * side, edge = std::max(1.4f, 0.06f * side);
    const auto outer = [=](float x, float y) { return RoundBox(x, y, cx, cy, w / 2, h / 2, r); };
    if (pass == Pass::Glow) {
        p.Halo(outer, std::max(2.0f, 0.06f * side), 0.6f);
        return;
    }
    const auto inner = [=](float x, float y) {
        return RoundBox(x, y, cx, cy, w / 2 - edge, h / 2 - edge, r - edge);
    };
    p.Fill(outer, rim);
    const float top = cy - h / 2;
    p.FillShaded(inner, [=](float, float y) { return Mix(body_top, body_bottom, (y - top) / h); });
    const size_t chars = Codepoints(label).size();
    const float cap = chars == 1 ? 0.5f * h : 0.42f * h;
    const Text text{label, cx, cy, cap, w - 2 * edge - 0.12f * h, LabelCondense(chars)};
    const float bold = (chars == 1 ? 0.06f : 0.05f) * cap;
    p.Fill([&](float x, float y) { return text.Distance(x, y) - bold; }, ink);
}

// An arrow head pointing up/down/left/right (dx, dy), light with a dark outline.
void DrawArrow(Painter& p, float cx, float cy, float size, int dx, int dy) {
    const float s = size / 2;
    float v[6];
    if (dy) {
        v[0] = cx, v[1] = cy + dy * s, v[2] = cx - s, v[3] = cy - dy * s * 0.6f, v[4] = cx + s,
        v[5] = cy - dy * s * 0.6f;
    } else {
        v[0] = cx + dx * s, v[1] = cy, v[2] = cx - dx * s * 0.6f, v[3] = cy - s,
        v[4] = cx - dx * s * 0.6f, v[5] = cy + s;
    }
    const float line = std::max(1.0f, 0.12f * size);
    p.Fill([&](float x, float y) { return Triangle(x, y, v) - line; }, groove);
    p.Fill([&](float x, float y) { return Triangle(x, y, v); }, ink);
}

enum class MousePart { None, Left, Right, Middle, WheelUp, WheelDown, Back, Forward, Move };

MousePart MousePartOf(const std::string& label) {
    static const std::pair<const char*, MousePart> names[] = {
        {"@LMB", MousePart::Left},         {"@RMB", MousePart::Right},
        {"@MMB", MousePart::Middle},       {"@WHEELUP", MousePart::WheelUp},
        {"@WHEELDOWN", MousePart::WheelDown}, {"@X1", MousePart::Back},
        {"@X2", MousePart::Forward},       {"@MOUSE", MousePart::Move},
    };
    for (const auto& [name, part] : names) {
        if (label == name) {
            return part;
        }
    }
    return MousePart::None;
}

// A mouse (height h, centred) with the used button in orange; arrows beside it for the wheel
// and for movement.
void DrawMouse(Painter& p, Pass pass, float cx, float cy, float h, float room, MousePart part) {
    const float w = 0.62f * h, edge = std::max(1.4f, 0.06f * w);
    const auto outer = [=](float x, float y) { return RoundBox(x, y, cx, cy, w / 2, h / 2, w / 2); };
    if (pass == Pass::Glow) {
        p.Halo(outer, std::max(2.0f, 0.06f * h), 0.6f);
        return;
    }
    const auto inner = [=](float x, float y) {
        return RoundBox(x, y, cx, cy, w / 2 - edge, h / 2 - edge, w / 2 - edge);
    };
    p.Fill(outer, rim);
    const float top = cy - h / 2;
    p.FillShaded(inner, [=](float, float y) { return Mix(body_top, body_bottom, (y - top) / h); });
    const float split = cy - 0.08f * h, line = std::max(1.0f, 0.05f * w);
    const auto accent = [=](float, float y) {
        return Mix(accent_top, accent_bottom, (y - top) / (split - top));
    };
    if (part == MousePart::Left || part == MousePart::Right) {
        const float sign = part == MousePart::Left ? 1.0f : -1.0f;
        p.FillShaded([=](float x, float y) {
            return std::max(inner(x, y), std::max(y - split, sign * (x - cx)));
        }, accent);
    }
    p.Fill([=](float x, float y) {
        const float d = std::min(Segment(x, y, cx, top, cx, split),
                                 Segment(x, y, cx - w, split, cx + w, split));
        return std::max(d - line / 2, inner(x, y));
    }, groove);
    const float wy = cy - 0.27f * h, whw = std::max(1.2f, 0.09f * w), whh = 0.1f * h;
    const bool wheel = part == MousePart::Middle || part == MousePart::WheelUp ||
                       part == MousePart::WheelDown;
    p.Fill([=](float x, float y) { return RoundBox(x, y, cx, wy, whw + line, whh + line, whw + line); },
           groove);
    p.Fill([=](float x, float y) { return RoundBox(x, y, cx, wy, whw, whh, whw); },
           wheel ? accent_top : rim);
    if (part == MousePart::Back || part == MousePart::Forward) {
        const float sy = cy + (part == MousePart::Back ? 0.16f : 0.02f) * h;
        const float sx = cx - w / 2 + edge + 0.07f * w;
        p.Fill([=](float x, float y) { return RoundBox(x, y, sx, sy, 0.07f * w, 0.065f * h, 0.05f * w); },
               accent_top);
    }
    const float arrow = std::min(0.26f * h, room * 0.8f);
    const float beside = cx + w / 2 + room / 2;
    if (part == MousePart::WheelUp || part == MousePart::WheelDown) {
        DrawArrow(p, beside, wy, arrow, 0, part == MousePart::WheelUp ? -1 : 1);
    }
    if (part == MousePart::Move) {
        DrawArrow(p, cx - w / 2 - room / 2, cy, arrow, -1, 0);
        DrawArrow(p, beside, cy, arrow, 1, 0);
    }
}

// The controller icons sheet (xbox-icons.png beside the settings file, GOW3_CONFIG): the player's
// own pictures for the Xbox-style buttons, 4 columns x 3 rows of square cells in SheetNames' order
// (any cell size; 128 in the default). A transparent cell keeps the drawn button. The game writes
// xbox-icons-default.png there at each start, the drawn buttons, to start from.
constexpr const char* SheetNames[] = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "MENU", "VIEW"};
constexpr int SheetColumns = 4, SheetRows = 3, SheetCellSize = 128;
constexpr int SheetCount = SheetColumns * SheetRows;

struct Sheet {
    int cell_w = 0, cell_h = 0;
    std::array<std::vector<float>, SheetCount> cells; // premultiplied RGBA; empty: drawn button
};

// The sheet used while the labels render (gow3gpu_prompts_set_labels); null: the drawn buttons.
thread_local const Sheet* t_sheet = nullptr;

const std::vector<float>* SheetCell(const std::string& name) {
    if (!t_sheet) {
        return nullptr;
    }
    for (int i = 0; i < SheetCount; ++i) {
        if (name == SheetNames[i]) {
            return t_sheet->cells[i].empty() ? nullptr : &t_sheet->cells[i];
        }
    }
    return nullptr;
}

// Controller buttons in the Xbox style ("#A" "#B" "#X" "#Y" "#LB" "#RB" "#LT" "#RT" "#LS" "#RS"
// "#MENU" "#VIEW"): dark buttons with light rims; the face buttons' letters and glows in their
// colours.
Rgb FaceColor(char letter) {
    switch (letter) {
    case 'A':
        return {0.45f, 0.82f, 0.30f};
    case 'B':
        return {0.95f, 0.32f, 0.26f};
    case 'X':
        return {0.30f, 0.58f, 0.98f};
    default:
        return {0.99f, 0.80f, 0.22f};
    }
}

// A shape with a light rim, a dark body (lighter at the top) and a label; `outer` is the shape's
// distance, the body is `edge` pixels inside it.
template <typename Sdf>
void DrawShape(Painter& p, Pass pass, Sdf&& outer, float edge, float top, float height, Rgb halo,
               const std::string& label, float cx, float cy, float cap, float max_width, Rgb ink_color,
               float condense = 1.0f) {
    if (pass == Pass::Glow) {
        p.Halo(outer, std::max(2.0f, 0.06f * height), 0.6f, halo);
        return;
    }
    p.Fill(outer, rim);
    p.FillShaded([&](float x, float y) { return outer(x, y) + edge; },
                 [=](float, float y) { return Mix(body_top, body_bottom, (y - top) / height); });
    if (label.empty()) {
        return;
    }
    const Text text{label, cx, cy, cap, max_width, condense};
    const float bold = 0.06f * cap;
    p.Fill([&](float x, float y) { return text.Distance(x, y) - bold; }, ink_color);
}

void DrawPadButton(Painter& p, Pass pass, float cx, float cy, float w, float h, const std::string& token) {
    const std::string name = token.substr(1);
    if (const auto* picture = SheetCell(name)) { // the player's picture: no drawn glow around it
        if (pass == Pass::Body) {
            p.Picture(picture->data(), t_sheet->cell_w, t_sheet->cell_h, cx, cy, w, h);
        }
        return;
    }
    const float side = std::min(w, h);
    const float edge = std::max(1.4f, 0.06f * side);
    if (name.size() == 1) { // A B X Y
        const float r = 0.47f * side;
        const Rgb color = FaceColor(name[0]);
        DrawShape(p, pass, [=](float x, float y) { return std::hypot(x - cx, y - cy) - r; }, edge, cy - r,
                  2 * r, Mix(glow, color, 0.6f), name, cx, cy, 0.95f * r, 1.3f * r, color);
    } else if (name == "LB" || name == "RB") { // bumper: wide, short, rounder on top
        const float hw = std::min(0.5f * w, 0.85f * h), hh = 0.3f * h;
        DrawShape(p, pass, [=](float x, float y) {
            return RoundBox(x, y, cx, cy, hw, hh, (y < cy ? 0.9f : 0.45f) * hh);
        }, edge, cy - hh, 2 * hh, glow, name, cx, cy, 0.95f * hh, 1.5f * hw, ink, 0.9f);
    } else if (name == "LT" || name == "RT") { // trigger: tall, round top
        const float hw = 0.36f * h, hh = 0.47f * h;
        DrawShape(p, pass, [=](float x, float y) {
            return RoundBox(x, y, cx, cy, hw, hh, (y < cy ? 0.95f : 0.35f) * hw);
        }, edge, cy - hh, 2 * hh, glow, name, cx, cy + 0.08f * hh, 0.62f * hw, 1.6f * hw, ink, 0.9f);
    } else if (name == "LS" || name == "RS") { // stick click
        const float r = 0.47f * side;
        DrawShape(p, pass, [=](float x, float y) { return std::hypot(x - cx, y - cy) - r; }, edge, cy - r,
                  2 * r, glow, name, cx, cy, 0.62f * r, 1.4f * r, ink, 0.9f);
    } else if (name == "MENU" || name == "VIEW") { // small round buttons with their symbols
        const float r = 0.36f * side;
        DrawShape(p, pass, [=](float x, float y) { return std::hypot(x - cx, y - cy) - r; }, edge, cy - r,
                  2 * r, glow, "", cx, cy, 0, 0, ink);
        if (pass == Pass::Glow) {
            return;
        }
        const float line = std::max(1.0f, 0.1f * r);
        if (name == "MENU") { // three lines
            for (int i = -1; i <= 1; ++i) {
                const float y0 = cy + i * 0.32f * r;
                p.Fill([=](float x, float y) { return Segment(x, y, cx - 0.45f * r, y0, cx + 0.45f * r, y0) - line; },
                       ink);
            }
        } else { // two overlapping windows
            const float s = 0.26f * r;
            p.Fill([=](float x, float y) {
                return std::fabs(RoundBox(x, y, cx - 0.14f * r, cy - 0.14f * r, s, s, 0.2f * s)) - line * 0.8f;
            }, ink);
            p.Fill([=](float x, float y) { return RoundBox(x, y, cx + 0.14f * r, cy + 0.14f * r, s, s, 0.2f * s); },
                   ink);
        }
    }
}

using Labels = std::vector<std::string>;

// One binding in a w x h area: a key cap fitted to its label, the mouse, or a controller button.
void DrawBinding(Painter& p, Pass pass, float cx, float cy, float w, float h, const std::string& label) {
    if (!label.empty() && label[0] == '#') {
        DrawPadButton(p, pass, cx, cy, w, h, label);
        return;
    }
    const MousePart part = MousePartOf(label);
    if (part != MousePart::None) {
        const float mh = h, mw = 0.62f * mh;
        DrawMouse(p, pass, cx, cy, mh, std::max(0.0f, (w - mw) / 2), part);
        return;
    }
    float kw = h;
    if (Codepoints(label).size() > 1) {
        const size_t chars = Codepoints(label).size();
        kw = std::clamp(Text::Width(label, 0.42f * h, LabelCondense(chars)) + 0.4f * h, h, w);
    }
    DrawKey(p, pass, cx, cy, std::min(kw, w), h, label);
}

// Four keys as an inverted T (WASD, the d-pad's keys).
void DrawCluster(Painter& p, const Cell& c, const std::string& up, const std::string& left,
                 const std::string& down, const std::string& right) {
    const float gap = 1.5f;
    const float s = std::min((c.w - 2 * Margin - 2 * gap) / 3, (c.h - 2 * Margin - gap) / 2);
    const float cx = c.x + c.w / 2.0f, cy = c.y + c.h / 2.0f;
    const float row0 = cy - (s + gap) / 2, row1 = cy + (s + gap) / 2;
    for (const Pass pass : {Pass::Glow, Pass::Body}) {
        DrawKey(p, pass, cx, row0, s, s, up);
        DrawKey(p, pass, cx - (s + gap), row1, s, s, left);
        DrawKey(p, pass, cx, row1, s, s, down);
        DrawKey(p, pass, cx + (s + gap), row1, s, s, right);
    }
}

void DrawPlan(Canvas& canvas, const Plan& plan, const Cell& c, const Labels& labels) {
    const auto label = [&](int i) -> const std::string& { return labels[i]; };
    switch (plan.draw) {
    case Draw::Key:
        if (label(plan.a).empty()) {
            return;
        }
        break;
    case Draw::Pair:
        if (label(plan.a).empty() || label(plan.b).empty()) {
            return;
        }
        break;
    case Draw::Dpad:
        if (label(GOW3_PROMPT_UP).empty() || label(GOW3_PROMPT_LEFT).empty() ||
            label(GOW3_PROMPT_DOWN).empty() || label(GOW3_PROMPT_RIGHT).empty()) {
            return;
        }
        break;
    case Draw::Stick:
        if (label(plan.a).empty() || (MousePartOf(label(plan.a)) == MousePart::None &&
                                      (label(plan.a + 1).empty() || label(plan.a + 2).empty() ||
                                       label(plan.a + 3).empty()))) {
            return;
        }
        break;
    }
    Cell cell = c;
    if (plan.id == 30) {
        cell.w = 77, cell.h = 50; // the part shared with glyph 14
        Painter{canvas, 0, 252, 84, 303};
    }
    Painter p{canvas, cell.x, cell.y, cell.x + cell.w, cell.y + cell.h};
    const float cx = cell.x + cell.w / 2.0f, cy = cell.y + cell.h / 2.0f;
    const float w = cell.w - 2 * Margin, h = cell.h - 2 * Margin;
    switch (plan.draw) {
    case Draw::Key:
        for (const Pass pass : {Pass::Glow, Pass::Body}) {
            DrawBinding(p, pass, cx, cy, w, h, label(plan.a));
        }
        break;
    case Draw::Pair: {
        const float gap = 2.0f, s = std::min((w - gap) / 2, h);
        for (const Pass pass : {Pass::Glow, Pass::Body}) {
            DrawBinding(p, pass, cx - (s + gap) / 2, cy, s, s, label(plan.a));
            DrawBinding(p, pass, cx + (s + gap) / 2, cy, s, s, label(plan.b));
        }
        break;
    }
    case Draw::Dpad:
        DrawCluster(p, cell, label(GOW3_PROMPT_UP), label(GOW3_PROMPT_LEFT), label(GOW3_PROMPT_DOWN),
                    label(GOW3_PROMPT_RIGHT));
        break;
    case Draw::Stick:
        if (MousePartOf(label(plan.a)) != MousePart::None) {
            for (const Pass pass : {Pass::Glow, Pass::Body}) {
                DrawBinding(p, pass, cx, cy, w, h, label(plan.a));
            }
        } else if (!label(plan.a + 1).empty() && !label(plan.a + 2).empty() &&
                   !label(plan.a + 3).empty()) {
            DrawCluster(p, cell, label(plan.a), label(plan.a + 1), label(plan.a + 2), label(plan.a + 3));
        }
        break;
    }
}

template <size_t P, size_t C>
std::shared_ptr<const Canvas> RenderCells(const Labels& labels, std::shared_ptr<Canvas> canvas,
                                          const Plan (&plan_list)[P], const Cell (&cell_list)[C]) {
    for (const Plan& plan : plan_list) {
        for (const Cell& cell : cell_list) {
            if (cell.id == plan.id) {
                DrawPlan(*canvas, plan, cell, labels);
            }
        }
    }
    return canvas;
}

std::shared_ptr<const Canvas> Render(const Labels& labels) {
    return RenderCells(labels, std::make_shared<Canvas>(), plans, cells);
}

std::shared_ptr<const Canvas> RenderTlou(const Labels& labels) {
    const size_t texels = size_t(TlouAtlasWidth) * TlouAtlasHeight;
    auto canvas = std::make_shared<Canvas>(Canvas{int(TlouAtlasWidth), int(TlouAtlasHeight),
                                                  std::vector<float>(texels * 4, 0.0f), std::vector<u8>(texels, 0)});
    return RenderCells(labels, std::move(canvas), tlou_plans, tlou_cells);
}

// The redrawn cells over the guest's atlas (1D-tiled: 8x8 tiles in rows, Z order inside).
std::vector<u8> Compose(const Canvas& canvas, const u8* guest, Kind kind) {
    const int width = canvas.w, height = canvas.h;
    std::vector<u8> out(size_t(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            u32 element = 0;
            for (int b = 0; b < 3; ++b) {
                element |= ((x >> b) & 1) << (2 * b);
                element |= ((y >> b) & 1) << (2 * b + 1);
            }
            const u32 tile = (y / 8) * (width / 8) + x / 8;
            u8* o = &out[(y * width + x) * 4];
            std::memcpy(o, guest + (tile * 64 + element) * 4, 4);
            if (!canvas.drawn[y * width + x]) {
                continue;
            }
            const float* t = &canvas.texels[(y * width + x) * 4];
            const float a = t[3];
            // Transparent texels take the glow colour: filtering at the edges blends towards it.
            const Rgb c = a > 1e-4f ? Rgb{t[0] / a, t[1] / a, t[2] / a} : glow;
            const auto byte = [](float v) { return u8(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f)); };
            switch (kind) {
            case Kind::Rgba:
                o[0] = byte(c.r), o[1] = byte(c.g), o[2] = byte(c.b), o[3] = byte(a);
                break;
            case Kind::Color:
                o[0] = byte(c.r), o[1] = byte(c.g), o[2] = byte(c.b);
                break;
            case Kind::Alpha:
                o[0] = o[1] = o[2] = byte(a);
                break;
            default:
                break;
            }
        }
    }
    return out;
}

// ---- Button pictures (BC1)
struct Picture {
    int w = 0, h = 0;
    std::vector<u8> rgba; // alpha 0: BC1 punch-through texels
};

Rgb Decode565(u32 c) {
    return {float((c >> 11) & 31) / 31.0f, float((c >> 5) & 63) / 63.0f, float(c & 31) / 31.0f};
}

// Mip 0 of a 1D-tiled BC1 image (8x8 blocks per tile, Z order inside, tiles in rows).
Picture DecodeBc1(const u8* guest, int width, int height) {
    Picture pic{width, height, std::vector<u8>(size_t(width) * height * 4)};
    const int nbx = width / 4, nby = height / 4;
    for (int by = 0; by < nby; ++by) {
        for (int bx = 0; bx < nbx; ++bx) {
            u32 element = 0;
            for (int b = 0; b < 3; ++b) {
                element |= ((bx >> b) & 1) << (2 * b);
                element |= ((by >> b) & 1) << (2 * b + 1);
            }
            const u8* blk = guest + ((by / 8) * (nbx / 8) + bx / 8) * 512 + element * 8;
            const u32 c0 = blk[0] | blk[1] << 8, c1 = blk[2] | blk[3] << 8;
            const Rgb a = Decode565(c0), b = Decode565(c1);
            Rgb pal[4] = {a, b, Mix(a, b, 1.0f / 3), Mix(a, b, 2.0f / 3)};
            if (c0 <= c1) {
                pal[2] = Mix(a, b, 0.5f);
            }
            const u32 bits = blk[4] | blk[5] << 8 | blk[6] << 16 | u32(blk[7]) << 24;
            for (int k = 0; k < 16; ++k) {
                const u32 i = (bits >> (2 * k)) & 3;
                u8* o = &pic.rgba[((by * 4 + k / 4) * width + bx * 4 + k % 4) * 4];
                o[0] = u8(pal[i].r * 255 + 0.5f), o[1] = u8(pal[i].g * 255 + 0.5f), o[2] = u8(pal[i].b * 255 + 0.5f);
                o[3] = c0 <= c1 && i == 3 ? 0 : 255;
            }
        }
    }
    return pic;
}

u32 To565(const float (&c)[3]) {
    const auto q = [](float v, int bits) {
        const int max = (1 << bits) - 1;
        return u32(std::clamp(int(v / 255.0f * max + 0.5f), 0, max));
    };
    return q(c[0], 5) << 11 | q(c[1], 6) << 5 | q(c[2], 5);
}

// One 4x4 block: endpoints along the colours' main axis, punch-through for transparent texels.
void EncodeBlock(const u8 (&px)[16][4], u8* out) {
    float mean[3] = {}, n = 0;
    bool transparent = false;
    for (const auto& c : px) {
        if (c[3] < 128) {
            transparent = true;
            continue;
        }
        for (int i = 0; i < 3; ++i) {
            mean[i] += c[i];
        }
        ++n;
    }
    if (n == 0) {
        std::memcpy(out, "\0\0\0\0\xff\xff\xff\xff", 8);
        return;
    }
    for (float& m : mean) {
        m /= n;
    }
    float cov[3][3] = {};
    for (const auto& c : px) {
        if (c[3] < 128) {
            continue;
        }
        const float d[3] = {c[0] - mean[0], c[1] - mean[1], c[2] - mean[2]};
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                cov[i][j] += d[i] * d[j];
            }
        }
    }
    float axis[3] = {0.577f, 0.577f, 0.577f};
    for (int it = 0; it < 8; ++it) {
        float next[3];
        for (int i = 0; i < 3; ++i) {
            next[i] = cov[i][0] * axis[0] + cov[i][1] * axis[1] + cov[i][2] * axis[2];
        }
        const float len = std::sqrt(next[0] * next[0] + next[1] * next[1] + next[2] * next[2]);
        if (len < 1e-6f) {
            break;
        }
        for (int i = 0; i < 3; ++i) {
            axis[i] = next[i] / len;
        }
    }
    float lo = 1e9f, hi = -1e9f;
    for (const auto& c : px) {
        if (c[3] < 128) {
            continue;
        }
        const float t = (c[0] - mean[0]) * axis[0] + (c[1] - mean[1]) * axis[1] + (c[2] - mean[2]) * axis[2];
        lo = std::min(lo, t), hi = std::max(hi, t);
    }
    const float inset = (hi - lo) / 16;
    float e0[3], e1[3];
    for (int i = 0; i < 3; ++i) {
        e0[i] = mean[i] + axis[i] * (hi - inset);
        e1[i] = mean[i] + axis[i] * (lo + inset);
    }
    u32 c0 = To565(e0), c1 = To565(e1);
    if (transparent ? c0 > c1 : c0 < c1) {
        std::swap(c0, c1);
    }
    const Rgb a = Decode565(c0), b = Decode565(c1);
    Rgb pal[4];
    int count;
    if (c0 > c1) {
        pal[0] = a, pal[1] = b, pal[2] = Mix(a, b, 1.0f / 3), pal[3] = Mix(a, b, 2.0f / 3), count = 4;
    } else {
        pal[0] = a, pal[1] = b, pal[2] = Mix(a, b, 0.5f), count = 3;
    }
    u32 bits = 0;
    for (int k = 0; k < 16; ++k) {
        u32 best = 3;
        if (px[k][3] >= 128) {
            float best_d = 1e9f;
            for (int i = 0; i < count; ++i) {
                const float dr = pal[i].r * 255 - px[k][0], dg = pal[i].g * 255 - px[k][1], db = pal[i].b * 255 - px[k][2];
                const float d = dr * dr + dg * dg + db * db;
                if (d < best_d) {
                    best_d = d, best = u32(i);
                }
            }
        }
        bits |= best << (2 * k);
    }
    out[0] = u8(c0), out[1] = u8(c0 >> 8), out[2] = u8(c1), out[3] = u8(c1 >> 8);
    out[4] = u8(bits), out[5] = u8(bits >> 8), out[6] = u8(bits >> 16), out[7] = u8(bits >> 24);
}

// Linear BC1 blocks of a picture, appended to out.
void EncodeBc1(const Picture& pic, std::vector<u8>& out) {
    const int bw = std::max(1, (pic.w + 3) / 4), bh = std::max(1, (pic.h + 3) / 4);
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            u8 px[16][4];
            for (int k = 0; k < 16; ++k) {
                const int x = std::min(bx * 4 + k % 4, pic.w - 1), y = std::min(by * 4 + k / 4, pic.h - 1);
                std::memcpy(px[k], &pic.rgba[(y * pic.w + x) * 4], 4);
            }
            const size_t at = out.size();
            out.resize(at + 8);
            EncodeBlock(px, &out[at]);
        }
    }
}

Picture Half(const Picture& pic) {
    Picture half{std::max(1, pic.w / 2), std::max(1, pic.h / 2), {}};
    half.rgba.resize(size_t(half.w) * half.h * 4);
    for (int y = 0; y < half.h; ++y) {
        for (int x = 0; x < half.w; ++x) {
            int sum[3] = {}, opaque = 0;
            for (int k = 0; k < 4; ++k) {
                const int sx = std::min(x * 2 + k % 2, pic.w - 1), sy = std::min(y * 2 + k / 2, pic.h - 1);
                const u8* s = &pic.rgba[(sy * pic.w + sx) * 4];
                for (int i = 0; i < 3; ++i) {
                    sum[i] += s[i];
                }
                opaque += s[3] >= 128;
            }
            u8* o = &half.rgba[(y * half.w + x) * 4];
            o[0] = u8(sum[0] / 4), o[1] = u8(sum[1] / 4), o[2] = u8(sum[2] / 4), o[3] = opaque >= 2 ? 255 : 0;
        }
    }
    return half;
}

void FlipRows(Picture& pic) {
    const size_t row = size_t(pic.w) * 4;
    for (int y = 0; y < pic.h / 2; ++y) {
        std::swap_ranges(pic.rgba.begin() + y * row, pic.rgba.begin() + (y + 1) * row,
                         pic.rgba.begin() + (pic.h - 1 - y) * row);
    }
}

float Luma(const u8* c) {
    return 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2];
}

// The picture on a canvas (opaque) and back.
Canvas ToCanvas(const Picture& pic) {
    Canvas c{pic.w, pic.h, std::vector<float>(size_t(pic.w) * pic.h * 4), std::vector<u8>(size_t(pic.w) * pic.h)};
    for (size_t i = 0; i < size_t(pic.w) * pic.h; ++i) {
        for (int k = 0; k < 3; ++k) {
            c.texels[i * 4 + k] = pic.rgba[i * 4 + k] / 255.0f;
        }
        c.texels[i * 4 + 3] = 1.0f;
    }
    return c;
}
void FromCanvas(const Canvas& c, Picture& pic) {
    for (size_t i = 0; i < size_t(pic.w) * pic.h; ++i) {
        for (int k = 0; k < 3; ++k) {
            pic.rgba[i * 4 + k] = u8(std::clamp(c.texels[i * 4 + k] * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }
}

// What a binding label draws inside a picture's button: text (a key, or a controller's letters)
// or a mouse.
void DrawPictureLabel(Painter& p, const std::string& label, float cx, float cy, float height, float max_width,
                      Rgb color) {
    if (label.empty()) {
        return;
    }
    const MousePart part = MousePartOf(label);
    if (part != MousePart::None) {
        for (const Pass pass : {Pass::Glow, Pass::Body}) {
            DrawMouse(p, pass, cx, cy, height, 0.0f, part);
        }
        return;
    }
    const std::string text_label = label[0] == '#' ? label.substr(1) : label;
    const size_t chars = Codepoints(text_label).size();
    const float cap = chars == 1 ? 0.62f * height : 0.42f * height;
    const Text text{text_label, cx, cy, cap, max_width, LabelCondense(chars)};
    const float bold = 0.07f * cap;
    // The glow ends inside the glyphs' distance fields (they end Text::Pad pixels out).
    const float glow_radius = std::min(0.12f * height, Text::Pad - bold - 1.0f);
    p.Halo([&](float x, float y) { return text.Distance(x, y) - bold; }, glow_radius, 0.45f, color);
    p.Fill([&](float x, float y) { return text.Distance(x, y) - bold; }, color);
}

// A round face button (upright): its symbol replaced by the binding; for the controller set the
// glow colour becomes the face button's (Xbox A green, B red, X blue, Y yellow).
void RedrawFace(Picture& pic, const std::string& label, bool controller) {
    const int w = pic.w, h = pic.h;
    float bg[3] = {}, nb = 0;
    float cx = 0, cy = 0;
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const u8* c = &pic.rgba[(y * w + x) * 4];
            if (c[3] < 128) {
                continue;
            }
            if (x < w / 16 || y < h / 16 || x >= w - w / 16 || y >= h - h / 16) {
                for (int i = 0; i < 3; ++i) {
                    bg[i] += c[i];
                }
                ++nb;
            } else if (Luma(c) < 80) {
                x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
            }
        }
    }
    if (nb == 0 || x1 < x0) {
        return;
    }
    for (float& v : bg) {
        v /= nb;
    }
    cx = (x0 + x1 + 1) / 2.0f, cy = (y0 + y1 + 1) / 2.0f;
    // The dark button: round, or an ellipse (pictures of the button pressed are flat).
    const float rx = (x1 - x0 + 1) / 2.0f, ry = (y1 - y0 + 1) / 2.0f, r = std::min(rx, ry);
    const auto unit = [=](float x, float y) { return std::hypot((x - cx) / rx, (y - cy) / ry); };
    float body[3] = {}, symbol[3] = {}, n_body = 0, n_symbol = 0, symbol_reach = 0.0f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const u8* c = &pic.rgba[(y * w + x) * 4];
            const float d = unit(x + 0.5f, y + 0.5f) * r;
            if (c[3] < 128 || d > 0.95f * r) {
                continue;
            }
            if (Luma(c) < 80 && d > 0.6f * r) {
                for (int i = 0; i < 3; ++i) {
                    body[i] += c[i];
                }
                ++n_body;
            } else if (Luma(c) >= 80 && d < 0.75f * r) {
                for (int i = 0; i < 3; ++i) {
                    symbol[i] += c[i];
                }
                ++n_symbol;
            }
            if (std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]}) > 60 && unit(x + 0.5f, y + 0.5f) < 0.92f) {
                symbol_reach = std::max(symbol_reach, unit(x + 0.5f, y + 0.5f));
            }
        }
    }
    const Rgb body_color = n_body ? Rgb{body[0] / n_body / 255, body[1] / n_body / 255, body[2] / n_body / 255}
                                  : Rgb{0.18f, 0.18f, 0.18f};
    Rgb symbol_color = n_symbol ? Rgb{symbol[0] / n_symbol / 255, symbol[1] / n_symbol / 255, symbol[2] / n_symbol / 255}
                                : Rgb{bg[0] / 255, bg[1] / 255, bg[2] / 255};
    // The symbol's bright lines: towards the glow colour (the average includes their dim edges).
    symbol_color = Mix(symbol_color, Rgb{bg[0] / 255, bg[1] / 255, bg[2] / 255}, 0.6f);
    if (controller && label.size() == 2 && label[0] == '#') {
        // Recolour: each texel's part of the glow colour (against the dark body) takes the new one.
        const Rgb face = FaceColor(label[1]);
        const float nf[3] = {face.r * 255, face.g * 255, face.b * 255};
        const float bd[3] = {body_color.r * 255, body_color.g * 255, body_color.b * 255};
        const float axis[3] = {bg[0] - bd[0], bg[1] - bd[1], bg[2] - bd[2]};
        const float len2 = std::max(1.0f, axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
        for (size_t i = 0; i < size_t(w) * h; ++i) {
            u8* c = &pic.rgba[i * 4];
            const float s = std::clamp(((c[0] - bd[0]) * axis[0] + (c[1] - bd[1]) * axis[1] + (c[2] - bd[2]) * axis[2]) / len2,
                                       0.0f, 1.0f);
            for (int k = 0; k < 3; ++k) {
                c[k] = u8(std::clamp(c[k] + s * (nf[k] - bg[k]), 0.0f, 255.0f));
            }
        }
        symbol_color = Mix(face, Rgb{1, 1, 1}, 0.15f);
    }
    Canvas canvas = ToCanvas(pic);
    Painter p{canvas, 0, 0, w, h, false};
    const float inner = 0.8f * r;
    const Rgb top = Mix(body_color, Rgb{1, 1, 1}, 0.08f), bottom = Mix(body_color, Rgb{0, 0, 0}, 0.25f);
    const float cover = std::clamp(symbol_reach + 0.04f, 0.8f, 0.92f);
    p.FillShaded([=](float x, float y) { return (unit(x, y) - cover) * r; },
                 [=](float, float y) { return Mix(top, bottom, (y - (cy - cover * ry)) / (2 * cover * ry)); });
    DrawPictureLabel(p, label, cx, cy, 1.25f * inner, 1.5f * 0.8f * rx, symbol_color);
    FromCanvas(canvas, pic);
}

// Shoulder button pictures (upright): the "L1" etc. on the dark button replaced.
void RedrawShoulder(Picture& pic, const std::string& label) {
    const int w = pic.w, h = pic.h;
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    float body[3] = {}, n_body = 0;
    for (int y = h / 16; y < h - h / 16; ++y) {
        for (int x = w / 16; x < w - w / 16; ++x) {
            const u8* c = &pic.rgba[(y * w + x) * 4];
            if (c[3] >= 128 && Luma(c) < 90) {
                x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
                for (int i = 0; i < 3; ++i) {
                    body[i] += c[i];
                }
                ++n_body;
            }
        }
    }
    if (x1 < x0 || n_body == 0) {
        return;
    }
    // The text: light texels well inside the button.
    const int ix0 = x0 + (x1 - x0) / 8, ix1 = x1 - (x1 - x0) / 8, iy0 = y0 + (y1 - y0) / 6, iy1 = y1 - (y1 - y0) / 6;
    int tx0 = w, ty0 = h, tx1 = -1, ty1 = -1;
    float ink_sum[3] = {}, n_ink = 0;
    for (int y = iy0; y <= iy1; ++y) {
        for (int x = ix0; x <= ix1; ++x) {
            const u8* c = &pic.rgba[(y * w + x) * 4];
            if (c[3] >= 128 && Luma(c) >= 120) {
                tx0 = std::min(tx0, x), ty0 = std::min(ty0, y), tx1 = std::max(tx1, x), ty1 = std::max(ty1, y);
                for (int i = 0; i < 3; ++i) {
                    ink_sum[i] += c[i];
                }
                ++n_ink;
            }
        }
    }
    if (tx1 < tx0) {
        tx0 = ix0, tx1 = ix1, ty0 = iy0, ty1 = iy1;
    }
    const Rgb body_color{body[0] / n_body / 255, body[1] / n_body / 255, body[2] / n_body / 255};
    const Rgb text_color = n_ink ? Rgb{ink_sum[0] / n_ink / 255, ink_sum[1] / n_ink / 255, ink_sum[2] / n_ink / 255}
                                 : Rgb{0.93f, 0.88f, 0.78f};
    // The button: between its dark texels along both the row and the column (its text included).
    std::vector<int> row0(h, w), row1(h, -1), col0(w, h), col1(w, -1);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const u8* c = &pic.rgba[(y * w + x) * 4];
            if (c[3] >= 128 && Luma(c) < 90) {
                row0[y] = std::min(row0[y], x), row1[y] = std::max(row1[y], x);
                col0[x] = std::min(col0[x], y), col1[x] = std::max(col1[x], y);
            }
        }
    }
    std::vector<u8> on_button(size_t(w) * h);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            on_button[y * w + x] = x >= row0[y] && x <= row1[y] && y >= col0[x] && y <= col1[x];
        }
    }
    const auto button = [&](float x, float y) {
        const int ix = std::clamp(int(x), 0, w - 1), iy = std::clamp(int(y), 0, h - 1);
        return on_button[iy * w + ix] ? -1.0f : 1.0f;
    };
    Canvas canvas = ToCanvas(pic);
    Painter p{canvas, 0, 0, w, h, false};
    const float pad = 0.04f * w;
    const float cx = (tx0 + tx1 + 1) / 2.0f, cy = (ty0 + ty1 + 1) / 2.0f;
    const float hw = (tx1 - tx0 + 1) / 2.0f + pad, hh = (ty1 - ty0 + 1) / 2.0f + pad;
    const Rgb top = Mix(body_color, Rgb{1, 1, 1}, 0.05f), bottom = Mix(body_color, Rgb{0, 0, 0}, 0.15f);
    p.FillShaded([&](float x, float y) { return std::max(RoundBox(x, y, cx, cy, hw, hh, 0.4f * hh), button(x, y)); },
                 [=](float, float y) { return Mix(top, bottom, (y - (cy - hh)) / (2 * hh)); });
    const float bw = (x1 - x0) * 0.8f, bh = (y1 - y0) * 0.62f;
    DrawPictureLabel(p, label, (x0 + x1 + 1) / 2.0f, cy, std::min(bh, 2.6f * hh), bw, text_color);
    FromCanvas(canvas, pic);
}

// A d-pad picture with one arm lit (orange): the bound key is written on that arm, in
// the dark of the other arms, so the picture still reads as "this direction" (keyboard and mouse
// only: other controllers' d-pads look the same).
void RedrawDpad(Picture& pic, const std::string& label) {
    const int w = pic.w, h = pic.h;
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    float dark[3] = {}, n_dark = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const u8* c = &pic.rgba[(y * w + x) * 4];
            if (c[3] < 128) {
                continue;
            }
            // The lit arm: saturated orange (red well above blue).
            if (c[0] > 150 && c[0] - c[2] > 90 && c[0] >= c[1]) {
                x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
            } else if (Luma(c) < 90) {
                for (int i = 0; i < 3; ++i) {
                    dark[i] += c[i];
                }
                ++n_dark;
            }
        }
    }
    if (x1 < x0 || (x1 - x0) < w / 16 || (y1 - y0) < h / 16) {
        return;
    }
    const Rgb ink_color = n_dark ? Rgb{dark[0] / n_dark / 255, dark[1] / n_dark / 255, dark[2] / n_dark / 255}
                                 : Rgb{0.12f, 0.12f, 0.11f};
    Canvas canvas = ToCanvas(pic);
    Painter p{canvas, 0, 0, w, h, false};
    const float cx = (x0 + x1 + 1) / 2.0f, cy = (y0 + y1 + 1) / 2.0f;
    const float aw = float(x1 - x0 + 1), ah = float(y1 - y0 + 1);
    DrawPictureLabel(p, label, cx, cy, 0.62f * std::min(aw, ah), 0.86f * aw, ink_color);
    FromCanvas(canvas, pic);
}

// Label sets: keyboard and mouse, other controllers. Device: which icons show (GOW3_PROMPT_*).
constexpr int Sets = 2;
int SetOf(int device) {
    return device == 1 ? 0 : device == 2 ? 1 : -1;
}

struct State {
    std::mutex mutex;
    u64 generation[Sets]{};
    std::shared_ptr<const Canvas> canvas[Sets];
    std::shared_ptr<const Canvas> tlou_canvas[Sets];
    Labels labels[Sets];
    struct Output {
        u64 generation;
        std::shared_ptr<const Texels> texels;
    };
    std::unordered_map<u64, Output> outputs[Sets];     // by atlas hash
    std::vector<std::pair<VAddr, u32>> seen;           // atlas uploads (invalidated on a switch)
    std::unordered_set<u64> logged;                    // GOW3_PROMPTS_LOG: address ^ hash
};
State& GetState() {
    static State state;
    return state;
}
std::atomic<int> device{0}; // 0 the game's PlayStation icons, 1 keyboard and mouse, 2 other controller
void InvalidateSeen() {
    std::vector<std::pair<VAddr, u32>> seen;
    {
        auto& state = GetState();
        std::scoped_lock lock{state.mutex};
        seen = state.seen;
    }
#ifndef GOW3_PROMPTS_TEST
    auto* rasterizer = Core::Memory::Instance()->GetRasterizer();
    if (!rasterizer) {
        return;
    }
    for (const auto& [address, size] : seen) {
        rasterizer->GetTextureCache().InvalidateMemory(address, size);
    }
#endif
}

} // namespace

namespace {

std::shared_ptr<const Texels> MakePicture(const KnownTexture& known, const u8* guest, int width, int height,
                                          u32 levels, const std::string& label, bool controller) {
    Picture pic = DecodeBc1(guest, width, height);
    FlipRows(pic); // stored upside down
    if (known.kind == Kind::Face) {
        RedrawFace(pic, label, controller);
    } else if (known.kind == Kind::Dpad) {
        RedrawDpad(pic, label);
    } else {
        RedrawShoulder(pic, label);
    }
    FlipRows(pic);
    auto texels = std::make_shared<Texels>();
    for (u32 m = 0; m < levels; ++m) {
        if (m) {
            pic = Half(pic);
        }
        const u32 offset = u32(texels->data.size());
        EncodeBc1(pic, texels->data);
        texels->mips.push_back({offset, u32(std::max(1, (pic.w + 3) / 4) * 4), u32(std::max(1, (pic.h + 3) / 4) * 4)});
    }
    return texels;
}

#ifndef GOW3_PROMPTS_TEST
void ExportPlayStationSheet(const u8* guest); // below, with the other sheet files
#endif

} // namespace

std::shared_ptr<const Texels> Replacement(VAddr address, u32 guest_size, u32 mip0_size, u32 width, u32 height,
                                          u32 levels, bool bc1, u32 tile_mode) {
    static const bool log = [] {
        const char* env = std::getenv("GOW3_PROMPTS_LOG");
        return env && env[0] == '1';
    }();
    const bool atlas = !bc1 && width == Size && height == Size && levels == 1 && mip0_size == Size * Size * 4;
    const bool tlou_atlas = !bc1 && width == TlouAtlasWidth && height == TlouAtlasHeight && levels == 1 &&
                            mip0_size == TlouAtlasWidth * TlouAtlasHeight * 4;
    const auto side = [](u32 v) { return v >= 64 && v <= 512 && (v & (v - 1)) == 0; };
    // Redrawn pictures are 1D-tiled (tile mode 13: their mip 0 is width * height / 2 bytes).
    const bool picture = bc1 && side(width) && side(height) && mip0_size == width * height / 2;
    if (!atlas && !picture && !tlou_atlas) {
        return nullptr;
    }
    // Every 256x512 RGBA8 upload is hashed, PlayStation icons too: only the atlas (by its hash) is
    // remembered for a later switch to other icons, not every texture of that size.
    const u8* guest = reinterpret_cast<const u8*>(address);
    const u64 hash = XXH3_64bits(guest, mip0_size);
    const KnownTexture* known = nullptr;
    for (const auto& k : known_textures) {
        if (k.hash == hash &&
            (k.kind == Kind::Face || k.kind == Kind::Shoulder || k.kind == Kind::Dpad) == picture) {
            known = &k;
        }
    }
    // GOW3_PROMPTS_TEST_HASH=<hash hex>:face|shoulder:<GOW3_PROMPT_* index> (tests of the picture
    // upload path): that BC1 image counts as a button picture.
    static const KnownTexture test_texture = [] {
        KnownTexture k{0, Kind::Face, GOW3_PROMPT_TRIANGLE, "test picture"};
        if (const char* env = std::getenv("GOW3_PROMPTS_TEST_HASH")) {
            char kind[16] = {};
            unsigned long long h = 0;
            int button = GOW3_PROMPT_TRIANGLE;
            if (std::sscanf(env, "%llx:%15[a-z]:%d", &h, kind, &button) >= 1) {
                k.hash = h;
                k.kind = std::strcmp(kind, "shoulder") == 0 ? Kind::Shoulder : Kind::Face;
                k.button = std::clamp(button, 0, int(GOW3_PROMPT_COUNT) - 1);
            }
        }
        return k;
    }();
    if (!known && picture && test_texture.hash && test_texture.hash == hash) {
        known = &test_texture;
    }
    auto& state = GetState();
    std::scoped_lock lock{state.mutex};
    if (log && state.logged.insert(address ^ hash).second) {
        std::printf("Prompts: %ux%u %s image (tile mode %u) at 0x%llx, hash %016llx%s%s\n", width, height,
                    bc1 ? "BC1" : "RGBA8", tile_mode, static_cast<unsigned long long>(address),
                    static_cast<unsigned long long>(hash), known ? ": " : "", known ? known->name : "");
        std::fflush(stdout);
    }
    if (tlou_atlas) {
        if (hash != TlouAtlasHash) {
            return nullptr;
        }
#ifndef GOW3_PROMPTS_TEST
        static std::once_flag reference;
        std::call_once(reference, ExportPlayStationSheet, guest);
#endif
        if (std::find(state.seen.begin(), state.seen.end(), std::pair{address, guest_size}) == state.seen.end()) {
            state.seen.emplace_back(address, guest_size);
        }
        const int set = SetOf(device.load(std::memory_order_relaxed));
        if (set < 0 || !state.tlou_canvas[set]) {
            return nullptr;
        }
        auto& output = state.outputs[set][hash];
        if (!output.texels || output.generation != state.generation[set]) {
            auto atlas_texels = std::make_shared<Texels>();
            atlas_texels->data = Compose(*state.tlou_canvas[set], guest, Kind::Rgba);
            atlas_texels->mips.push_back({0, TlouAtlasWidth, TlouAtlasHeight});
            output = {state.generation[set], std::move(atlas_texels)};
            std::printf("Prompts: %s icons in the Last of Us atlas\n", set == 0 ? "keyboard and mouse" : "controller");
            std::fflush(stdout);
        }
        return output.texels;
    }
    if (!known || (picture && tile_mode != 13)) {
        return nullptr;
    }
    if (std::find(state.seen.begin(), state.seen.end(), std::pair{address, guest_size}) == state.seen.end()) {
        state.seen.emplace_back(address, guest_size);
    }
    const int set = SetOf(device.load(std::memory_order_relaxed));
    if (set < 0 || !state.canvas[set]) {
        return nullptr;
    }
    if (known->kind == Kind::Dpad && set != 0) {
        return nullptr; // other controllers' d-pads look like the PlayStation one
    }
    std::string label;
    if (picture) {
        label = state.labels[set][known->button];
        if (label.empty()) {
            return nullptr;
        }
    }
    auto& output = state.outputs[set][hash];
    if (!output.texels || output.generation != state.generation[set]) {
        std::shared_ptr<const Texels> texels;
        if (picture) {
            texels = MakePicture(*known, guest, int(width), int(height), levels, label, set == 1);
        } else {
            auto atlas_texels = std::make_shared<Texels>();
            atlas_texels->data = Compose(*state.canvas[set], guest, known->kind);
            atlas_texels->mips.push_back({0, u32(Size), u32(Size)});
            texels = std::move(atlas_texels);
        }
        output = {state.generation[set], std::move(texels)};
        std::printf("Prompts: %s icons in the %s\n", set == 0 ? "keyboard and mouse" : "controller", known->name);
        std::fflush(stdout);
    }
    return output.texels;
}

// A quad of the HUD sprite shader: centre and size in pixels of a W x H viewport, and its colour factor.
struct PromptQuad {
    float cx, cy, w, h, W, H;
    float r, g, b, a;
};

// The button's TLOU atlas cell, redrawn for the set and scaled into a 128x128 RGBA8 image (stored
// upside down). Empty when the set has no drawing for it: the game's own icon stays.
static std::vector<u8> GenerateFloatingPrompt(int button, int set) {
    constexpr int OutSize = 128;
    constexpr int TargetSize = 68;
    const Cell* cell = nullptr;
    for (const auto& plan : tlou_plans) {
        if (plan.a == button && plan.draw == Draw::Key) {
            for (const auto& c : tlou_cells) {
                if (c.id == plan.id) {
                    cell = &c;
                }
            }
            break;
        }
    }
    std::shared_ptr<const Canvas> canvas;
    {
        auto& state = GetState();
        std::scoped_lock lock{state.mutex};
        if (state.labels[set].size() > size_t(button) && !state.labels[set][button].empty()) {
            canvas = state.tlou_canvas[set];
        }
    }
    if (!cell || !canvas) {
        return {};
    }
    const int atlas_w = int(TlouAtlasWidth);
    const auto texel = [&](int x, int y) -> std::array<float, 4> {
        const float* t = &canvas->texels[(y * atlas_w + x) * 4];
        if (!canvas->drawn[y * atlas_w + x] || t[3] <= 1e-4f) {
            return {0.f, 0.f, 0.f, 0.f};
        }
        return {t[0] / t[3] * 255.0f, t[1] / t[3] * 255.0f, t[2] / t[3] * 255.0f, t[3] * 255.0f};
    };
    const float scale = float(TargetSize) / float(std::max(cell->w, cell->h));
    std::vector<u8> out(OutSize * OutSize * 4, 0);
    bool any = false;
    for (int dy = 0; dy < OutSize; ++dy) {
        for (int dx = 0; dx < OutSize; ++dx) {
            const float sx = (dx - OutSize * 0.5f + 0.5f) / scale + (cell->x + cell->w * 0.5f);
            const float sy = (dy - OutSize * 0.5f + 0.5f) / scale + (cell->y + cell->h * 0.5f);
            if (sx < cell->x || sx >= cell->x + cell->w || sy < cell->y || sy >= cell->y + cell->h) {
                continue;
            }
            const int x0 = int(sx), y0 = int(sy);
            const int x1 = std::min(x0 + 1, cell->x + cell->w - 1);
            const int y1 = std::min(y0 + 1, cell->y + cell->h - 1);
            const float fx = sx - x0, fy = sy - y0;
            const auto p00 = texel(x0, y0), p10 = texel(x1, y0), p01 = texel(x0, y1), p11 = texel(x1, y1);
            u8* dst = &out[((OutSize - 1 - dy) * OutSize + dx) * 4];
            for (int c = 0; c < 4; ++c) {
                const float v = (p00[c] * (1 - fx) + p10[c] * fx) * (1 - fy) + (p01[c] * (1 - fx) + p11[c] * fx) * fy;
                dst[c] = u8(std::clamp(v + 0.5f, 0.0f, 255.0f));
            }
            any = any || dst[3];
        }
    }
    if (!any) {
        return {};
    }
    return out;
}

#ifndef GOW3_PROMPTS_TEST
struct ButtonGpu {
    VideoCore::UniqueImage image;
    vk::UniqueImageView color_view;
    vk::UniqueImageView alpha_view;
    int device = -1;
    u64 generation = ~0ull;
    bool none = false; // no drawing for this device and generation: the game's icon stays
};
static std::array<ButtonGpu, GOW3_PROMPT_COUNT> s_prompt_buttons;

bool BindFloatingPrompt(const Vulkan::Instance& instance, Vulkan::Scheduler& scheduler,
                        Vulkan::Runtime& runtime, void* p_img0_info, void* p_img1_info,
                        int button) {
    const int cur_dev = device.load(std::memory_order_relaxed);
    const int set = SetOf(cur_dev);
    if (set < 0 || button < 0 || button >= GOW3_PROMPT_COUNT) {
        return false;
    }

    u64 gen;
    {
        auto& state = GetState();
        std::scoped_lock lock{state.mutex};
        gen = state.generation[set];
    }
    auto& b_gpu = s_prompt_buttons[button];
    const bool current = b_gpu.device == cur_dev && b_gpu.generation == gen;
    if (current && b_gpu.none) {
        return false;
    }
    if (!b_gpu.image || !current) {
        auto pixels = GenerateFloatingPrompt(button, set);
        b_gpu.none = pixels.empty();
        if (b_gpu.none) {
            b_gpu.device = cur_dev;
            b_gpu.generation = gen;
            return false;
        }

        vk::Device dev = instance.GetDevice();
        const bool is_new = !b_gpu.image;
        if (is_new) {
            b_gpu.image = VideoCore::UniqueImage(dev, instance.GetAllocator());
            b_gpu.image.Create(vk::ImageCreateInfo{
                .imageType = vk::ImageType::e2D,
                .format = vk::Format::eR8G8B8A8Unorm,
                .extent = {128, 128, 1},
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = vk::SampleCountFlagBits::e1,
                .tiling = vk::ImageTiling::eOptimal,
                .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
                .initialLayout = vk::ImageLayout::eUndefined,
            });
            b_gpu.color_view = Vulkan::Check(dev.createImageViewUnique({
                .image = vk::Image(b_gpu.image),
                .viewType = vk::ImageViewType::e2D,
                .format = vk::Format::eR8G8B8A8Unorm,
                .components = {
                    vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eIdentity,
                },
                .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            }));
            b_gpu.alpha_view = Vulkan::Check(dev.createImageViewUnique({
                .image = vk::Image(b_gpu.image),
                .viewType = vk::ImageViewType::e2D,
                .format = vk::Format::eR8G8B8A8Unorm,
                .components = {
                    vk::ComponentSwizzle::eA,
                    vk::ComponentSwizzle::eA,
                    vk::ComponentSwizzle::eA,
                    vk::ComponentSwizzle::eA,
                },
                .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            }));
        }

        const auto staging = runtime.GetStagingPool().Request(128 * 128 * 4, VideoCore::MemoryType::HostUncached, 16);
        std::memcpy(staging.mapped, pixels.data(), 128 * 128 * 4);
        staging.Flush();

        const vk::BufferImageCopy copy_region{
            .bufferOffset = staging.offset,
            .bufferRowLength = 128,
            .bufferImageHeight = 128,
            .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = {128, 128, 1},
        };

        scheduler.EndRendering();
        scheduler.Record([src_handle = staging.buffer->Handle(), dst_image = vk::Image(b_gpu.image), copy_region, is_new](vk::CommandBuffer cmdbuf) {
            const vk::ImageMemoryBarrier2 pre_barrier{
                .srcStageMask = is_new ? vk::PipelineStageFlagBits2::eAllCommands : vk::PipelineStageFlagBits2::eFragmentShader,
                .srcAccessMask = is_new ? vk::AccessFlagBits2::eNone : vk::AccessFlagBits2::eShaderRead,
                .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
                .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
                .oldLayout = is_new ? vk::ImageLayout::eUndefined : vk::ImageLayout::eShaderReadOnlyOptimal,
                .newLayout = vk::ImageLayout::eTransferDstOptimal,
                .image = dst_image,
                .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
            cmdbuf.pipelineBarrier2(vk::DependencyInfo{
                .imageMemoryBarrierCount = 1u,
                .pImageMemoryBarriers = &pre_barrier,
            });
            cmdbuf.copyBufferToImage(src_handle, dst_image, vk::ImageLayout::eTransferDstOptimal, copy_region);
            const vk::ImageMemoryBarrier2 post_barrier{
                .srcStageMask = vk::PipelineStageFlagBits2::eCopy,
                .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
                .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
                .oldLayout = vk::ImageLayout::eTransferDstOptimal,
                .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                .image = dst_image,
                .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
            cmdbuf.pipelineBarrier2(vk::DependencyInfo{
                .imageMemoryBarrierCount = 1u,
                .pImageMemoryBarriers = &post_barrier,
            });
        });

        b_gpu.device = cur_dev;
        b_gpu.generation = gen;
    }

    auto* img0 = static_cast<vk::DescriptorImageInfo*>(p_img0_info);
    auto* img1 = static_cast<vk::DescriptorImageInfo*>(p_img1_info);
    img0->imageView = *b_gpu.color_view;
    img0->imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    img1->imageView = *b_gpu.alpha_view;
    img1->imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    return true;
}

// The draw's first quad from guest memory: u16 or u32 indices, PromptVertexStride-byte vertices
// with the clip-space position in floats 0-1 and the colour factor in floats 8-11.
static bool ReadPromptQuad(const PromptGeometry& geom, Core::MemoryManager* memory, PromptQuad& q) {
    if (!memory || geom.num_indices < 6 || !geom.ib_addr || !geom.vb_addr) {
        return false;
    }
    u32 idx[6];
    if (geom.is_index16) {
        u16 raw[6];
        if (!memory->TryReadBacking(geom.ib_addr, raw, sizeof(raw))) {
            return false;
        }
        std::copy(raw, raw + 6, idx);
    } else if (!memory->TryReadBacking(geom.ib_addr, idx, sizeof(idx))) {
        return false;
    }
    u32 vi[4];
    int unique = 0;
    for (const u32 i : idx) {
        if (std::find(vi, vi + unique, i) != vi + unique) {
            continue;
        }
        if (unique == 4) {
            return false;
        }
        vi[unique++] = i;
    }
    if (unique != 4) {
        return false;
    }
    float v[4][PromptVertexStride / 4];
    for (int i = 0; i < 4; ++i) {
        if (!memory->TryReadBacking(geom.vb_addr + u64(vi[i]) * PromptVertexStride, v[i], sizeof(v[i]))) {
            return false;
        }
    }
    // In the interface's 16:9 canvas, whatever the render target's width (the render scale changes
    // it). On a wider display (GOW3_DISPLAY_ASPECT, runtime_hud.c) the canvas is the middle 16:9.
    static const float ratio = [] {
        int w = 0, h = 0;
        const char* env = std::getenv("GOW3_DISPLAY_ASPECT");
        return env && std::sscanf(env, "%dx%d", &w, &h) == 2 && w > 0 && h > 0 ? float(w) / float(h) * 9.0f / 16.0f
                                                                                : 1.0f;
    }();
    const float H = geom.viewport_h, W = H * 16.0f / 9.0f;
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (const auto& vertex : v) {
        const float x = (vertex[0] * ratio * 0.5f + 0.5f) * W, y = (0.5f - vertex[1] * 0.5f) * H;
        x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
    }
    q = {(x0 + x1) / 2, (y0 + y1) / 2, x1 - x0, y1 - y0, W, H, v[0][8], v[0][9], v[0][10], v[0][11]};
    return true;
}

bool FloatingPromptsActive() {
    // GOW3_PROMPTS_WORLD=0: world prompts and menu tabs keep the game's icons (a fallback should a
    // picture ever show the wrong button); the symbol atlas in the text is still redrawn.
    static const bool world = [] {
        const char* env = std::getenv("GOW3_PROMPTS_WORLD");
        return !(env && env[0] == '0');
    }();
    return world && SetOf(device.load(std::memory_order_relaxed)) >= 0;
}

#ifndef _WIN32
// gow3 (Linux): _wfopen is the Windows CRT's wide-char fopen. Here std::filesystem::path::c_str()
// already returns the narrow UTF-8 string (on Windows it is wchar_t), and the mode literals are
// pure ASCII, so the same call sites narrow both and use fopen (io_file.cpp guards the same way).
static FILE* _wfopen(const char* path, const wchar_t* mode) {
    char narrow_mode[8] = {};
    for (size_t i = 0; i + 1 < sizeof(narrow_mode) && mode[i] != L'\0'; ++i) {
        narrow_mode[i] = static_cast<char>(mode[i]);
    }
    return std::fopen(path, narrow_mode);
}
#endif

namespace {
std::filesystem::path ConfigFolder();

// GOW3_PROMPTS_DUMP=1 (study of the prompt pictures): each new colour/alpha texture pair of the HUD
// sprite shader, with its descriptors, quad and known_prompts' answer, into prompt-dump/ beside the
// settings file (<hash>.bin; one line per pair in index.txt). Render thread only; at most 2000.
void DumpPrompt(const AmdGpu::Image& s0, const AmdGpu::Image& s1, Core::MemoryManager* memory,
                const PromptQuad& q, int rule, u64 picture_hash) {
    static const bool on = [] {
        const char* env = std::getenv("GOW3_PROMPTS_DUMP");
        return env && env[0] == '1';
    }();
    static std::unordered_set<u64> dumped;
    if (!on || dumped.size() >= 2000) {
        return;
    }
    std::vector<u8> data[2];
    for (int i = 0; i < 2; ++i) {
        const VAddr address = (i ? s1 : s0).Address();
        for (const size_t size : {size_t(16384), size_t(8192)}) {
            data[i].resize(size);
            if (memory->TryReadBacking(address, data[i].data(), size)) {
                break;
            }
            data[i].clear();
        }
        if (data[i].empty()) {
            return;
        }
    }
    const u64 hash = XXH3_64bits(data[0].data(), data[0].size()) ^ XXH3_64bits(data[1].data(), data[1].size()) * 3;
    if (!dumped.insert(hash).second) {
        return;
    }
    const auto folder = ConfigFolder() / "prompt-dump";
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.bin", static_cast<unsigned long long>(hash));
    if (FILE* f = _wfopen((folder / name).c_str(), L"wb")) {
        const u32 header[4] = {0x44504c54u /* TLPD */, 1, u32(data[0].size()), u32(data[1].size())};
        const float quad[10] = {q.cx, q.cy, q.w, q.h, q.W, q.H, q.r, q.g, q.b, q.a};
        std::fwrite(header, sizeof(header), 1, f);
        std::fwrite(&s0, sizeof(s0), 1, f);
        std::fwrite(&s1, sizeof(s1), 1, f);
        std::fwrite(quad, sizeof(quad), 1, f);
        std::fwrite(&rule, sizeof(rule), 1, f);
        std::fwrite(data[0].data(), 1, data[0].size(), f);
        std::fwrite(data[1].data(), 1, data[1].size(), f);
        std::fclose(f);
    }
    if (FILE* f = _wfopen((folder / "index.txt").c_str(), L"a")) {
        std::fprintf(f,
                     "%s colour 0x%llx alpha 0x%llx quad %.0f,%.0f %.0fx%.0f in %.0fx%.0f rgba %.2f %.2f %.2f %.2f "
                     "rule %d picture %016llx\n",
                     name, static_cast<unsigned long long>(s0.Address()),
                     static_cast<unsigned long long>(s1.Address()), q.cx, q.cy, q.w, q.h, q.W, q.H, q.r, q.g, q.b,
                     q.a, rule, static_cast<unsigned long long>(picture_hash));
        std::fclose(f);
    }
}
} // namespace

// The HUD sprite shader's pictures by the XXH3 of their colour and alpha textures' first mips
// (8192 bytes each), as GOW3_PROMPTS_DUMP=1 and tools/prompt_dump_view.py showed them. They are
// fixed pictures of hud.pak (art/general/hud/buttons/*-color.tga and *-trans.tga). button -1:
// one of that shader's pictures that shows no button. An unknown picture keeps the game's icon
// (GOW3_PROMPTS_LOG=1 logs its hash).
struct KnownPrompt {
    u64 hash;
    int button;
    const char* name;
};
constexpr KnownPrompt known_prompts[] = {
    {0xd37b1eed61743d34ULL, GOW3_PROMPT_L1, "backpack tab L1"},
    {0x05cb6648e035aef2ULL, GOW3_PROMPT_R1, "backpack tab R1"},
    {0x16d823638b6b66d4ULL, GOW3_PROMPT_TRIANGLE, "world prompt triangle"},
    {0x6898d5ea5dfeef79ULL, GOW3_PROMPT_SQUARE, "world prompt square"},
    {0x3fc57b3149c75febULL, -1, "noise marker"},
    {0x8b945ba80e57f325ULL, -1, "soft round glow"},
    {0x67e8badcc6bc9d02ULL, -1, "square frame"},
};

int IdentifyFloatingPrompt(const AmdGpu::Image& s0, const AmdGpu::Image& s1, Core::MemoryManager* memory,
                           const PromptGeometry& geom) {
    if (s0.width != 127 || s0.height != 127 || s0.data_format != 35 || s1.width != 127 || s1.height != 127 ||
        s1.data_format != 38) {
        return -1;
    }
    // The picture names the button: a known one its button (or none), whatever its size and place.
    // An unknown one keeps the game's own icon (better than a guessed, maybe wrong, button).
    u8 texels[2][8192]; // the colour and the alpha texture's first mip (BC1 and BC4, 128x128)
    if (!memory->TryReadBacking(s0.Address(), texels[0], sizeof(texels[0])) ||
        !memory->TryReadBacking(s1.Address(), texels[1], sizeof(texels[1]))) {
        return -1;
    }
    const u64 hash = XXH3_64bits(texels, sizeof(texels));
    int button = -1;
    bool known = false;
    for (const KnownPrompt& k : known_prompts) {
        if (k.hash == hash) {
            button = k.button, known = true;
            break;
        }
    }
    // GOW3_PROMPTS_LOG=1: each unknown picture once, to add it to known_prompts (GOW3_PROMPTS_DUMP=1
    // saves the picture itself). The quad is read only for these.
    static const bool log = [] {
        const char* env = std::getenv("GOW3_PROMPTS_LOG");
        return env && env[0] == '1';
    }();
    static const bool dump = [] {
        const char* env = std::getenv("GOW3_PROMPTS_DUMP");
        return env && env[0] == '1';
    }();
    static std::unordered_set<u64> logged; // render thread only
    if ((dump || (log && !known)) && logged.size() < 2000) {
        PromptQuad q{};
        ReadPromptQuad(geom, memory, q);
        DumpPrompt(s0, s1, memory, q, button, hash);
        if (log && !known && logged.insert(hash).second) {
            std::printf("Prompts: unknown HUD picture %016llx, %.0fx%.0f at %.0f,%.0f: the game's icon stays\n",
                        static_cast<unsigned long long>(hash), q.w, q.h, q.cx, q.cy);
            std::fflush(stdout);
        }
    }
    return button;
}

void ClearFloatingPrompts() {
    for (auto& b : s_prompt_buttons) {
        b.alpha_view.reset();
        b.color_view.reset();
        b.image.Destroy();
        b.device = -1;
        b.generation = ~0ull;
        b.none = false;
    }
}
#endif

#ifndef GOW3_PROMPTS_TEST
namespace {

u32 Be32(const u8* p) {
    return u32(p[0]) << 24 | u32(p[1]) << 16 | u32(p[2]) << 8 | p[3];
}

// An 8-bit, non-interlaced PNG (grey, grey+alpha, RGB, RGBA or palette) as RGBA8; false otherwise.
bool DecodePng(const std::vector<u8>& file, int& w, int& h, std::vector<u8>& rgba, std::string& why) {
    static const u8 signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (file.size() < 8 || std::memcmp(file.data(), signature, 8) != 0) {
        why = "not a PNG";
        return false;
    }
    int depth = 0, type = -1, interlace = 0;
    w = h = 0;
    std::vector<u8> idat, palette, alpha;
    for (size_t pos = 8; pos + 8 <= file.size();) {
        const u32 len = Be32(&file[pos]);
        const char* tag = reinterpret_cast<const char*>(&file[pos + 4]);
        pos += 8;
        if (len > file.size() - pos || file.size() - pos - len < 4) {
            why = "truncated";
            return false;
        }
        const u8* d = &file[pos];
        if (std::memcmp(tag, "IHDR", 4) == 0 && len >= 13) {
            w = int(Be32(d)), h = int(Be32(d + 4)), depth = d[8], type = d[9], interlace = d[12];
        } else if (std::memcmp(tag, "IDAT", 4) == 0) {
            idat.insert(idat.end(), d, d + len);
        } else if (std::memcmp(tag, "PLTE", 4) == 0) {
            palette.assign(d, d + len);
        } else if (std::memcmp(tag, "tRNS", 4) == 0) {
            alpha.assign(d, d + len);
        } else if (std::memcmp(tag, "IEND", 4) == 0) {
            break;
        }
        pos += len + 4; // the chunk and its CRC
    }
    const int channels = type == 0 ? 1 : type == 2 ? 3 : type == 3 ? 1 : type == 4 ? 2 : type == 6 ? 4 : 0;
    if (depth != 8 || interlace != 0 || channels == 0) {
        why = "only 8-bit, non-interlaced PNGs are read (save it as 32-bit RGBA)";
        return false;
    }
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        why = "size out of range";
        return false;
    }
    size_t raw_len = 0;
    u8* raw = static_cast<u8*>(
        tinfl_decompress_mem_to_heap(idat.data(), idat.size(), &raw_len, TINFL_FLAG_PARSE_ZLIB_HEADER));
    const size_t stride = size_t(w) * channels;
    if (!raw || raw_len < (stride + 1) * h) {
        mz_free(raw);
        why = "damaged image data";
        return false;
    }
    std::vector<u8> pixels(stride * h);
    for (int y = 0; y < h; ++y) {
        const u8 filter = raw[y * (stride + 1)];
        const u8* in = &raw[y * (stride + 1) + 1];
        u8* out = &pixels[y * stride];
        const u8* prev = y ? &pixels[(y - 1) * stride] : nullptr;
        for (size_t i = 0; i < stride; ++i) {
            const int a = i >= size_t(channels) ? out[i - channels] : 0;
            const int b = prev ? prev[i] : 0;
            const int c = prev && i >= size_t(channels) ? prev[i - channels] : 0;
            int v = in[i];
            switch (filter) {
            case 0:
                break;
            case 1:
                v += a;
                break;
            case 2:
                v += b;
                break;
            case 3:
                v += (a + b) / 2;
                break;
            case 4: {
                const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
                break;
            }
            default:
                mz_free(raw);
                why = "damaged image data";
                return false;
            }
            out[i] = u8(v);
        }
    }
    mz_free(raw);
    rgba.resize(size_t(w) * h * 4);
    for (size_t i = 0; i < size_t(w) * h; ++i) {
        const u8* s = &pixels[i * channels];
        u8* o = &rgba[i * 4];
        switch (type) {
        case 0:
        case 4:
            o[0] = o[1] = o[2] = s[0], o[3] = type == 4 ? s[1] : 255;
            break;
        case 2:
            o[0] = s[0], o[1] = s[1], o[2] = s[2], o[3] = 255;
            break;
        case 3: {
            const size_t k = s[0];
            o[0] = k * 3 + 2 < palette.size() ? palette[k * 3] : 0;
            o[1] = k * 3 + 2 < palette.size() ? palette[k * 3 + 1] : 0;
            o[2] = k * 3 + 2 < palette.size() ? palette[k * 3 + 2] : 0;
            o[3] = k < alpha.size() ? alpha[k] : 255;
            break;
        }
        default:
            std::memcpy(o, s, 4);
        }
    }
    return true;
}

std::filesystem::path ConfigFolder() {
    const char* env = std::getenv("GOW3_CONFIG");
    return env && env[0] ? std::filesystem::path(env).parent_path() : std::filesystem::path(".");
}

// The player's sheet (xbox-icons.png), or null when there is none or it cannot be read (logged).
std::shared_ptr<const Sheet> LoadSheet() {
    const auto path = ConfigFolder() / "xbox-icons.png";
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return nullptr;
    }
    // Read into the heap: this runs on the game's pad fiber, whose stack is only 64 KiB.
    std::vector<u8> file(std::min<uintmax_t>(std::filesystem::file_size(path, error), 64u << 20));
    if (FILE* f = _wfopen(path.c_str(), L"rb")) {
        file.resize(std::fread(file.data(), 1, file.size(), f));
        std::fclose(f);
    } else {
        file.clear();
    }
    int w = 0, h = 0;
    std::vector<u8> rgba;
    std::string why;
    if (!DecodePng(file, w, h, rgba, why) ||
        (w % SheetColumns != 0 || h % SheetRows != 0 ? (why = "its size is not 4 columns by 3 rows of cells", true)
                                                     : false)) {
        std::printf("Prompts: xbox-icons.png not used: %s\n", why.c_str());
        std::fflush(stdout);
        return nullptr;
    }
    auto sheet = std::make_shared<Sheet>();
    sheet->cell_w = w / SheetColumns, sheet->cell_h = h / SheetRows;
    int used = 0;
    for (int i = 0; i < SheetCount; ++i) {
        const int ox = (i % SheetColumns) * sheet->cell_w, oy = (i / SheetColumns) * sheet->cell_h;
        std::vector<float> cell(size_t(sheet->cell_w) * sheet->cell_h * 4);
        bool any = false;
        for (int y = 0; y < sheet->cell_h; ++y) {
            for (int x = 0; x < sheet->cell_w; ++x) {
                const u8* s = &rgba[(size_t(oy + y) * w + ox + x) * 4];
                float* o = &cell[(size_t(y) * sheet->cell_w + x) * 4];
                const float a = s[3] / 255.0f;
                o[0] = s[0] / 255.0f * a, o[1] = s[1] / 255.0f * a, o[2] = s[2] / 255.0f * a, o[3] = a;
                any = any || s[3] != 0;
            }
        }
        if (any) {
            sheet->cells[i] = std::move(cell);
            ++used;
        }
    }
    std::printf("Prompts: xbox-icons.png: %d of %d controller buttons from the sheet (%dx%d cells)\n", used,
                SheetCount, sheet->cell_w, sheet->cell_h);
    std::fflush(stdout);
    return sheet;
}

// xbox-icons-default.png: the drawn buttons in the sheet's layout, once per start.
void ExportDefaultSheet() {
    Canvas canvas{SheetColumns * SheetCellSize, SheetRows * SheetCellSize,
                  std::vector<float>(size_t(SheetColumns) * SheetRows * SheetCellSize * SheetCellSize * 4, 0.0f),
                  std::vector<u8>(size_t(SheetColumns) * SheetRows * SheetCellSize * SheetCellSize, 0)};
    for (int i = 0; i < SheetCount; ++i) {
        const int x0 = (i % SheetColumns) * SheetCellSize, y0 = (i / SheetColumns) * SheetCellSize;
        Painter p{canvas, x0, y0, x0 + SheetCellSize, y0 + SheetCellSize};
        const float side = SheetCellSize - 2 * Margin;
        for (const Pass pass : {Pass::Glow, Pass::Body}) {
            DrawPadButton(p, pass, x0 + SheetCellSize / 2.0f, y0 + SheetCellSize / 2.0f, side, side,
                          std::string("#") + SheetNames[i]);
        }
    }
    std::vector<u8> rgba(size_t(canvas.w) * canvas.h * 4);
    for (size_t i = 0; i < size_t(canvas.w) * canvas.h; ++i) {
        const float* t = &canvas.texels[i * 4];
        const float a = std::clamp(t[3], 0.0f, 1.0f);
        for (int k = 0; k < 3; ++k) {
            rgba[i * 4 + k] = a > 1e-4f ? u8(std::clamp(t[k] / a, 0.0f, 1.0f) * 255.0f + 0.5f) : 0;
        }
        rgba[i * 4 + 3] = u8(a * 255.0f + 0.5f);
    }
    size_t len = 0;
    void* png = tdefl_write_image_to_png_file_in_memory_ex(rgba.data(), canvas.w, canvas.h, 4, &len, 6, MZ_FALSE);
    if (!png) {
        return;
    }
    const auto path = ConfigFolder() / "xbox-icons-default.png";
    if (FILE* f = _wfopen(path.c_str(), L"wb")) {
        std::fwrite(png, 1, len, f);
        std::fclose(f);
    }
    mz_free(png);
}

// playstation-icons-reference.png: the game's own PlayStation symbols, read from its atlas (guest
// data, 1D-tiled RGBA8), in the sheet's layout (Cross where A goes, Circle where B goes...): a
// reference for drawing xbox-icons.png. Made from the player's copy of the game at run time and
// written beside the settings file once per start; no game picture is in the repository.
void ExportPlayStationSheet(const u8* guest) {
    // The atlas glyph per sheet cell: Cross Circle Square Triangle, L1 R1 L2 R2, L3 R3 Options Touchpad.
    static constexpr int ids[SheetCount] = {110, 106, 108, 109, 126, 125, 118, 117, 113, 114, 107, 124};
    const int atlas_w = int(TlouAtlasWidth), atlas_h = int(TlouAtlasHeight);
    const auto texel = [&](int x, int y) {
        x = std::clamp(x, 0, atlas_w - 1), y = std::clamp(y, 0, atlas_h - 1);
        u32 element = 0;
        for (int b = 0; b < 3; ++b) {
            element |= ((x >> b) & 1) << (2 * b);
            element |= ((y >> b) & 1) << (2 * b + 1);
        }
        return guest + ((u32(y / 8) * (atlas_w / 8) + x / 8) * 64 + element) * 4;
    };
    const int out_w = SheetColumns * SheetCellSize, out_h = SheetRows * SheetCellSize;
    std::vector<u8> rgba(size_t(out_w) * out_h * 4, 0);
    for (int i = 0; i < SheetCount; ++i) {
        const Cell* cell = nullptr;
        for (const Cell& c : tlou_cells) {
            if (c.id == ids[i]) {
                cell = &c;
            }
        }
        if (!cell) {
            continue;
        }
        const float fit = SheetCellSize - 2 * Margin;
        const float scale = std::min(fit / cell->w, fit / cell->h);
        const int ox = (i % SheetColumns) * SheetCellSize, oy = (i / SheetColumns) * SheetCellSize;
        for (int dy = 0; dy < SheetCellSize; ++dy) {
            for (int dx = 0; dx < SheetCellSize; ++dx) {
                const float sx = (dx + 0.5f - SheetCellSize / 2.0f) / scale + cell->x + cell->w / 2.0f - 0.5f;
                const float sy = (dy + 0.5f - SheetCellSize / 2.0f) / scale + cell->y + cell->h / 2.0f - 0.5f;
                if (sx < cell->x - 0.5f || sy < cell->y - 0.5f || sx > cell->x + cell->w - 0.5f ||
                    sy > cell->y + cell->h - 0.5f) {
                    continue;
                }
                const int x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
                const float fx = sx - x0, fy = sy - y0;
                const u8* p[4] = {texel(x0, y0), texel(x0 + 1, y0), texel(x0, y0 + 1), texel(x0 + 1, y0 + 1)};
                const float wgt[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
                float sum[4]{}; // premultiplied, so transparent texels do not darken the edges
                for (int k = 0; k < 4; ++k) {
                    const float a = p[k][3] / 255.0f * wgt[k];
                    sum[0] += p[k][0] * a, sum[1] += p[k][1] * a, sum[2] += p[k][2] * a, sum[3] += a;
                }
                u8* o = &rgba[(size_t(oy + dy) * out_w + ox + dx) * 4];
                if (sum[3] > 1e-4f) {
                    for (int k = 0; k < 3; ++k) {
                        o[k] = u8(std::clamp(sum[k] / sum[3] + 0.5f, 0.0f, 255.0f));
                    }
                    o[3] = u8(std::clamp(sum[3] * 255.0f + 0.5f, 0.0f, 255.0f));
                }
            }
        }
    }
    size_t len = 0;
    void* png = tdefl_write_image_to_png_file_in_memory_ex(rgba.data(), out_w, out_h, 4, &len, 6, MZ_FALSE);
    if (!png) {
        return;
    }
    if (FILE* f = _wfopen((ConfigFolder() / "playstation-icons-reference.png").c_str(), L"wb")) {
        std::fwrite(png, 1, len, f);
        std::fclose(f);
    }
    mz_free(png);
}

} // namespace
#endif

} // namespace Gow3Prompts

extern "C" void gow3gpu_prompts_set_labels(int set, const char* const* labels, int count) {
    using namespace Gow3Prompts;
    if (set < 0 || set >= Sets) {
        return;
    }
    Labels list(GOW3_PROMPT_COUNT);
    for (int i = 0; i < std::min(count, int(GOW3_PROMPT_COUNT)); ++i) {
        list[i] = labels[i] ? labels[i] : "";
    }
#ifndef GOW3_PROMPTS_TEST
    // The controller set: the player's sheet, read again each time (an edited sheet shows at the
    // next controls change or start); the default sheet written once.
    std::shared_ptr<const Sheet> sheet;
    if (set == 1) {
        static std::once_flag exported;
        std::call_once(exported, ExportDefaultSheet);
        sheet = LoadSheet();
    }
    t_sheet = sheet.get();
#endif
    auto canvas = Render(list);
    auto tlou_canvas = RenderTlou(list);
#ifndef GOW3_PROMPTS_TEST
    t_sheet = nullptr;
#endif
    {
        auto& state = GetState();
        std::scoped_lock lock{state.mutex};
        state.canvas[set] = std::move(canvas);
        state.tlou_canvas[set] = std::move(tlou_canvas);
        state.labels[set] = list;
        ++state.generation[set];
    }
    if (SetOf(device.load()) == set) {
        InvalidateSeen();
    }
}

extern "C" void gow3gpu_prompts_set_device(int next) {
    using namespace Gow3Prompts;
    if (device.exchange(next) == next) {
        return;
    }
    static const char* const names[] = {"PlayStation", "keyboard and mouse", "controller"};
    std::printf("Prompts: %s icons\n", names[std::clamp(next, 0, 2)]);
    std::fflush(stdout);
    InvalidateSeen();
}

#ifdef GOW3_PROMPTS_TEST
// Picture check without the game: prompts-test face|shoulder LABEL CONTROLLER(0|1) IN OUT SIZE
// IN: the picture's first mip (1D-tiled BC1, as in the pak); OUT: the redrawn mip 0 as raw
// RGBA (stored upside down, as in the game).
asm(".section .rdata,\"dr\"\n"
    ".balign 16\n"
    ".global gow3_font_ttf\n"
    "gow3_font_ttf:\n"
    ".incbin \"" GOW3_FONT_PATH "\"\n"
    ".text\n");

int main(int argc, char** argv) {
    using namespace Gow3Prompts;
    if (argc < 7) {
        return 2;
    }
    const int size = std::atoi(argv[6]);
    FILE* in = std::fopen(argv[4], "rb");
    if (!in) {
        return 1;
    }
    std::vector<u8> guest(size_t(size) * size / 2);
    const size_t got = std::fread(guest.data(), 1, guest.size(), in);
    std::fclose(in);
    if (got != guest.size()) {
        return 1;
    }
    const std::string kind_name = argv[1];
    const KnownTexture known{0,
                             kind_name == "face"   ? Kind::Face
                             : kind_name == "dpad" ? Kind::Dpad
                                                   : Kind::Shoulder,
                             0, "test"};
    const auto texels = MakePicture(known, guest.data(), size, size, 9, argv[2], argv[3][0] == '1');
    // Mip 0 back from linear BC1 blocks.
    std::vector<u8> rgba(size_t(size) * size * 4);
    const int nb = size / 4;
    for (int by = 0; by < nb; ++by) {
        for (int bx = 0; bx < nb; ++bx) {
            const u8* blk = &texels->data[(by * nb + bx) * 8];
            const u32 c0 = blk[0] | blk[1] << 8, c1 = blk[2] | blk[3] << 8;
            const Rgb a = Decode565(c0), b = Decode565(c1);
            Rgb pal[4] = {a, b, c0 > c1 ? Mix(a, b, 1.0f / 3) : Mix(a, b, 0.5f), Mix(a, b, 2.0f / 3)};
            const u32 bits = blk[4] | blk[5] << 8 | blk[6] << 16 | u32(blk[7]) << 24;
            for (int k = 0; k < 16; ++k) {
                const u32 i = (bits >> (2 * k)) & 3;
                u8* o = &rgba[((by * 4 + k / 4) * size + bx * 4 + k % 4) * 4];
                o[0] = u8(pal[i].r * 255), o[1] = u8(pal[i].g * 255), o[2] = u8(pal[i].b * 255);
                o[3] = c0 <= c1 && i == 3 ? 0 : 255;
            }
        }
    }
    FILE* out = std::fopen(argv[5], "wb");
    std::fwrite(rgba.data(), 1, rgba.size(), out);
    std::fclose(out);
    std::printf("%zu bytes, %zu mips\n", texels->data.size(), texels->mips.size());
    return 0;
}
#endif
