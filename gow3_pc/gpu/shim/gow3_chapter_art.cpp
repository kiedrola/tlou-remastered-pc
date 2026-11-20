// SPDX-License-Identifier: GPL-2.0-or-later
// gow3 (The Last of Us Remastered): the Chapters page's area pictures, read from the game's own
// files for the save list in the pause menu, where the game has them unloaded
// (src/runtime_menu_saves.c). They are one texture, art/general/menu/shot-chapters.tga, streamed
// from texturedict1/common-tstrm.pak in build/ps4/main/common.psarc: BC1, 2048x4096, its blocks
// in 8x8 Morton-ordered tiles, upside down; the 12 areas are the right column's 512x256 cells
// (area i at row i, the torn frame included). Only the zlib blocks of the PSARC that hold the
// pack's header and that texture's top mip are read. Each area becomes area-<i>.png.

#include "../gow3gpu.h"

#include <miniz.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

uint32_t Be32(const unsigned char* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
uint64_t Be40(const unsigned char* p) {
    return uint64_t(p[0]) << 32 | Be32(p + 1);
}
uint32_t Le32(const unsigned char* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

/// One entry of a PSARC (zlib, block size up to 64 KiB): reads a byte range of the stored file
/// by inflating only the blocks that cover it.
struct Psarc {
    FILE* file = nullptr;
    uint32_t block = 0, toc_entries = 0, entry_size = 0;
    std::vector<unsigned char> toc;
    std::vector<uint32_t> sizes;

    ~Psarc() {
        if (file) std::fclose(file);
    }
    bool Open(const char* path) {
        file = std::fopen(path, "rb");
        if (!file) return false;
        unsigned char header[32];
        if (std::fread(header, 1, 32, file) != 32 || std::memcmp(header, "PSAR", 4) || std::memcmp(header + 8, "zlib", 4))
            return false;
        const uint32_t toc_length = Be32(header + 12);
        entry_size = Be32(header + 16);
        toc_entries = Be32(header + 20);
        block = Be32(header + 24);
        if (toc_length < 32 || entry_size < 30 || block == 0 || block > 0x10000) return false;
        toc.resize(toc_length - 32);
        if (std::fread(toc.data(), 1, toc.size(), file) != toc.size()) return false;
        for (size_t at = size_t(toc_entries) * entry_size; at + 2 <= toc.size(); at += 2)
            sizes.push_back(uint32_t(toc[at]) << 8 | toc[at + 1]);
        return true;
    }
    struct Entry {
        uint32_t first = 0;
        uint64_t length = 0, offset = 0;
    };
    Entry EntryAt(uint32_t index) const {
        const unsigned char* e = toc.data() + size_t(index) * entry_size;
        return {Be32(e + 16), Be40(e + 20), Be40(e + 25)};
    }
    /// Bytes [from, from + count) of an entry.
    bool Read(const Entry& entry, uint64_t from, uint64_t count, std::vector<unsigned char>& out) {
        out.clear();
        if (from + count > entry.length) return false;
        uint64_t position = entry.offset;
        const uint64_t first_block = from / block, last_block = (from + count - 1) / block;
        for (uint64_t b = 0; b < first_block; ++b) {
            const uint32_t stored = sizes.at(entry.first + b);
            position += stored ? stored : block;
        }
        std::vector<unsigned char> packed, plain(block);
        for (uint64_t b = first_block; b <= last_block; ++b) {
            const uint32_t stored = sizes.at(entry.first + b);
            const uint64_t plain_size = std::min<uint64_t>(block, entry.length - b * block);
            packed.resize(stored ? stored : block);
#ifdef _WIN32
            const int seek = _fseeki64(file, int64_t(position), SEEK_SET);
#else
            const int seek = fseeko(file, off_t(position), SEEK_SET);
#endif
            if (seek || std::fread(packed.data(), 1, packed.size(), file) != packed.size())
                return false;
            position += packed.size();
            if (packed.size() == plain_size) {
                plain.assign(packed.begin(), packed.end());
            } else {
                mz_ulong length = mz_ulong(plain_size);
                plain.resize(plain_size);
                if (mz_uncompress(plain.data(), &length, packed.data(), mz_ulong(packed.size())) != MZ_OK || length != plain_size)
                    return false;
            }
            const uint64_t start = b == first_block ? from % block : 0;
            const uint64_t end = b == last_block ? (from + count - 1) % block + 1 : plain_size;
            out.insert(out.end(), plain.begin() + start, plain.begin() + end);
        }
        return true;
    }
    /// The entry of a name: the manifest (entry 0) lists the others in order.
    bool Find(const char* name, Entry& found) {
        const Entry manifest = EntryAt(0);
        std::vector<unsigned char> text;
        if (!Read(manifest, 0, manifest.length, text)) return false;
        uint32_t index = 1;
        size_t at = 0;
        while (at < text.size() && index < toc_entries) {
            size_t end = at;
            while (end < text.size() && text[end] != '\n' && text[end] != 0) ++end;
            std::string line(text.begin() + at, text.begin() + end);
            if (!line.empty() && line[0] == '/') line.erase(0, 1);
            if (line == name) {
                found = EntryAt(index);
                return true;
            }
            ++index;
            at = end + 1;
        }
        return false;
    }
};

void Rgb565(uint16_t c, int out[3]) {
    out[0] = ((c >> 11) & 31) * 255 / 31;
    out[1] = ((c >> 5) & 63) * 255 / 63;
    out[2] = (c & 31) * 255 / 31;
}

/// One BC1 block into 4x4 RGBA pixels at (x, y) of an image `width` wide.
void DecodeBc1(const unsigned char* block, unsigned char* image, int width, int x, int y) {
    const uint16_t c0 = uint16_t(block[0] | block[1] << 8), c1 = uint16_t(block[2] | block[3] << 8);
    int palette[4][4];
    Rgb565(c0, palette[0]);
    Rgb565(c1, palette[1]);
    for (int k = 0; k < 3; ++k) {
        palette[2][k] = c0 > c1 ? (2 * palette[0][k] + palette[1][k]) / 3 : (palette[0][k] + palette[1][k]) / 2;
        palette[3][k] = c0 > c1 ? (palette[0][k] + 2 * palette[1][k]) / 3 : 0;
    }
    for (int k = 0; k < 4; ++k) palette[k][3] = 255;
    if (c0 <= c1) palette[3][3] = 0;
    const uint32_t bits = Le32(block + 4);
    for (int i = 0; i < 16; ++i) {
        const int* c = palette[(bits >> (2 * i)) & 3];
        unsigned char* p = image + (size_t(y + i / 4) * width + x + i % 4) * 4;
        p[0] = uint8_t(c[0]);
        p[1] = uint8_t(c[1]);
        p[2] = uint8_t(c[2]);
        p[3] = uint8_t(c[3]);
    }
}

/// One BC4 block (one channel) into 4x4 values at (x, y) of a `width` wide plane.
void DecodeBc4(const unsigned char* block, unsigned char* plane, int width, int x, int y) {
    const int r0 = block[0], r1 = block[1];
    int palette[8] = {r0, r1};
    for (int i = 2; i < 8; ++i) palette[i] = r0 > r1 ? ((8 - i) * r0 + (i - 1) * r1) / 7 : 0;
    if (r0 <= r1) {
        for (int i = 2; i < 6; ++i) palette[i] = ((6 - i) * r0 + (i - 1) * r1) / 5;
        palette[6] = 0;
        palette[7] = 255;
    }
    uint64_t bits = 0;
    for (int k = 0; k < 6; ++k) bits |= uint64_t(block[2 + k]) << (8 * k);
    for (int i = 0; i < 16; ++i) plane[size_t(y + i / 4) * width + x + i % 4] = uint8_t(palette[(bits >> (3 * i)) & 7]);
}

int Morton8(int x, int y) {
    int r = 0;
    for (int i = 0; i < 3; ++i) r |= ((x >> i) & 1) << (2 * i) | ((y >> i) & 1) << (2 * i + 1);
    return r;
}

} // namespace

extern "C" int gow3gpu_chapter_art(const char* common_psarc, const char* out_dir) {
    constexpr const char* kPack = "texturedict1/common-tstrm.pak";
    constexpr const char* kName = "z:/t1ps4/build/main/common/texture1/art/general/menu/shot-chapters.tga";
    // The torn-paper cut of every picture (BC4, 512x256: a cell's size), the game's alpha for it.
    constexpr const char* kMask = "z:/t1ps4/build/main/common/texture1/art/general/menu/shot-mask.tga";
    constexpr int kAreas = 12, kCellWidth = 512, kCellHeight = 256, kColumn = 2;
    Psarc psarc;
    Psarc::Entry pack;
    if (!common_psarc || !psarc.Open(common_psarc) || !psarc.Find(kPack, pack)) return 0;
    // The pack: data size at +0x20 (the data ends the pack); its texture records before it.
    std::vector<unsigned char> header;
    if (!psarc.Read(pack, 0, 0x40, header)) return 0;
    const uint64_t data_size = Le32(header.data() + 0x20);
    if (data_size == 0 || data_size >= pack.length) return 0;
    const uint64_t data_base = pack.length - data_size;
    std::vector<unsigned char> records;
    if (!psarc.Read(pack, 0, data_base, records)) return 0;
    // The record before a name: data offset, size, ..., format, mips, width, height.
    auto record = [&](const char* texture) -> const unsigned char* {
        const auto hit = std::search(records.begin(), records.end(), texture, texture + std::strlen(texture));
        return hit == records.end() || hit - records.begin() < 0x40 ? nullptr : &*hit;
    };
    const unsigned char* name = record(kName);
    const unsigned char* mask_name = record(kMask);
    if (!name) return 0;
    const uint32_t offset = Le32(name - 0x38), format = Le32(name - 0x24);
    const int width = int(Le32(name - 0x18)), height = int(Le32(name - 0x14));
    if (format != 0x47 || width != 2048 || height != 4096) {
        std::printf("Chapter art: unexpected texture (format %#x, %dx%d)\n", format, width, height);
        return 0;
    }
    const int bw = width / 4, bh = height / 4;
    std::vector<unsigned char> blocks;
    if (!psarc.Read(pack, data_base + offset, uint64_t(bw) * bh * 8, blocks)) return 0;
    // Only the right column's cells: decode the tiles that cover it.
    std::vector<unsigned char> image(size_t(width) * height * 4, 0);
    const int tiles_wide = bw / 8, x0 = kColumn * kCellWidth / 32, x1 = (kColumn + 1) * kCellWidth / 32;
    for (int ty = 0; ty < bh / 8; ++ty) {
        for (int tx = x0; tx < x1 && tx < tiles_wide; ++tx) {
            const unsigned char* tile = blocks.data() + (size_t(ty) * tiles_wide + tx) * 64 * 8;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    DecodeBc1(tile + Morton8(x, y) * 8, image.data(), width, (tx * 8 + x) * 4, (ty * 8 + y) * 4);
        }
    }
    // The mask (upside down too), and the part of the cell it keeps.
    std::vector<unsigned char> mask(size_t(kCellWidth) * kCellHeight, 255);
    int left = 0, top = 0, right = kCellWidth, bottom = kCellHeight;
    if (mask_name && Le32(mask_name - 0x24) == 0x50 && Le32(mask_name - 0x18) == uint32_t(kCellWidth) &&
        Le32(mask_name - 0x14) == uint32_t(kCellHeight)) {
        std::vector<unsigned char> mask_blocks, plane(mask.size());
        const int mw = kCellWidth / 4, mh = kCellHeight / 4;
        if (psarc.Read(pack, data_base + Le32(mask_name - 0x38), uint64_t(mw) * mh * 8, mask_blocks)) {
            for (int ty = 0; ty < mh / 8; ++ty)
                for (int tx = 0; tx < mw / 8; ++tx) {
                    const unsigned char* tile = mask_blocks.data() + (size_t(ty) * (mw / 8) + tx) * 64 * 8;
                    for (int y = 0; y < 8; ++y)
                        for (int x = 0; x < 8; ++x)
                            DecodeBc4(tile + Morton8(x, y) * 8, plane.data(), kCellWidth, (tx * 8 + x) * 4, (ty * 8 + y) * 4);
                }
            for (int y = 0; y < kCellHeight; ++y)
                std::memcpy(mask.data() + size_t(y) * kCellWidth, plane.data() + size_t(kCellHeight - 1 - y) * kCellWidth,
                            kCellWidth);
            left = kCellWidth, top = kCellHeight, right = 0, bottom = 0;
            for (int y = 0; y < kCellHeight; ++y)
                for (int x = 0; x < kCellWidth; ++x)
                    if (mask[size_t(y) * kCellWidth + x] > 16) {
                        left = std::min(left, x), right = std::max(right, x + 1);
                        top = std::min(top, y), bottom = std::max(bottom, y + 1);
                    }
            if (right <= left || bottom <= top) left = 0, top = 0, right = kCellWidth, bottom = kCellHeight;
        }
    } else {
        std::puts("Chapter art: no picture mask, the pictures keep their black corners");
    }
    const int out_width = right - left, out_height = bottom - top;
    int written = 0;
    std::vector<unsigned char> cell(size_t(kCellWidth) * kCellHeight * 4), cut(size_t(out_width) * out_height * 4);
    for (int area = 0; area < kAreas; ++area) {
        // Upside down: area 0 is the top row once flipped.
        for (int y = 0; y < kCellHeight; ++y) {
            const int source = height - 1 - (area * kCellHeight + y);
            std::memcpy(cell.data() + size_t(y) * kCellWidth * 4,
                        image.data() + (size_t(source) * width + kColumn * kCellWidth) * 4, size_t(kCellWidth) * 4);
        }
        // The mask's alpha, cut to the part it keeps.
        for (int y = 0; y < out_height; ++y)
            for (int x = 0; x < out_width; ++x) {
                const size_t from = size_t(top + y) * kCellWidth + left + x;
                unsigned char* p = cut.data() + (size_t(y) * out_width + x) * 4;
                std::memcpy(p, cell.data() + from * 4, 3);
                p[3] = uint8_t(std::min<int>(cell[from * 4 + 3], mask[from]));
            }
        size_t png_size = 0;
        void* png = tdefl_write_image_to_png_file_in_memory(cut.data(), out_width, out_height, 4, &png_size);
        if (!png) continue;
        char path[1024];
        std::snprintf(path, sizeof(path), "%s/area-%d.png", out_dir, area);
        if (FILE* f = std::fopen(path, "wb")) {
            written += std::fwrite(png, 1, png_size, f) == png_size;
            std::fclose(f);
        }
        mz_free(png);
    }
    std::printf("Chapter art: %d area pictures from %s\n", written, kPack);
    return written;
}
