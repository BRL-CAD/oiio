// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO

#pragma once

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/strutil.h>

#ifdef USE_STRUETYPE
#    define STT_STATIC
#    define STRUETYPE_IMPLEMENTATION
#    include <struetype.h>
#elif defined(USE_FREETYPE)
#    include <ft2build.h>
#    include FT_FREETYPE_H
#endif

OIIO_NAMESPACE_BEGIN
namespace {

struct FontGlyph {
    int width = 0, height = 0;
    int left = 0, top = 0, advance = 0;
    std::vector<unsigned char> pixels;
};



class FontFace {
public:
    FontFace()                           = default;
    FontFace(const FontFace&)            = delete;
    FontFace& operator=(const FontFace&) = delete;
    ~FontFace()
    {
#if defined(USE_FREETYPE) && !defined(USE_STRUETYPE)
        if (m_face)
            FT_Done_Face(m_face);
#endif
    }

    bool load(const std::string& filename)
    {
        m_error.clear();
#ifdef USE_STRUETYPE
        const uint64_t size = Filesystem::file_size(filename);
        if (!size || size > uint64_t(std::numeric_limits<int>::max()))
            return fail("Font buffer is empty, unreadable, or too large");
        m_data.resize(size);
        if (Filesystem::read_bytes(filename, m_data.data(), size) != size)
            return fail("Cannot read font buffer");
        const int offset = stt_GetFontOffsetForIndex(m_data.data(), int(size),
                                                     0);
        if (offset < 0
            || !stt_InitFont(&m_font, m_data.data(), int(size), offset))
            return fail("Invalid or unsupported TrueType/OpenType font");
        m_family = name(FamilyName);
        m_style  = name(StyleName);
        if (m_family.empty())
            m_family
                = Filesystem::replace_extension(Filesystem::filename(filename),
                                                "");
        return true;
#else
        if (m_face) {
            FT_Done_Face(m_face);
            m_face = nullptr;
        }
        static Library library;
        if (!library.handle)
            return fail("Cannot initialize FreeType");
        if (FT_New_Face(library.handle, filename.c_str(), 0, &m_face))
            return fail("Cannot open font face");
        m_family = m_face->family_name ? m_face->family_name : "";
        m_style  = m_face->style_name ? m_face->style_name : "";
        return true;
#endif
    }

    bool set_size(int size)
    {
        if (size <= 0)
            return fail("Font size must be positive");
#ifdef USE_STRUETYPE
        m_scale = stt_ScaleForMappingEmToPixels(&m_font, float(size));
        return (std::isfinite(m_scale) && m_scale > 0)
               || fail("Invalid font scale");
#else
        return !FT_Set_Pixel_Sizes(m_face, 0, size)
               || fail("Cannot set font size");
#endif
    }

    bool glyph(uint32_t codepoint, FontGlyph& glyph)
    {
        glyph.pixels.clear();
        glyph.width = glyph.height = glyph.left = glyph.top = glyph.advance = 0;
#ifdef USE_STRUETYPE
        if (codepoint > uint32_t(std::numeric_limits<int>::max()))
            return fail("Invalid Unicode codepoint");
        const int index = stt_FindGlyphIndex(&m_font, int(codepoint));
        int advance;
        stt_GetGlyphHMetrics(&m_font, index, &advance, nullptr);
        const double scaled_advance = std::round(double(advance) * m_scale);
        if (!std::isfinite(scaled_advance)
            || scaled_advance < std::numeric_limits<int>::min()
            || scaled_advance > std::numeric_limits<int>::max())
            return fail("Glyph advance is out of range");
        glyph.advance = int(scaled_advance);
        int yoff      = 0;
        const std::unique_ptr<unsigned char, BitmapDeleter> bitmap(
            stt_GetGlyphBitmap(&m_font, m_scale, m_scale, index, &glyph.width,
                               &glyph.height, &glyph.left, &yoff));
        if (yoff == std::numeric_limits<int>::min())
            return fail("Glyph offset is out of range");
        glyph.top = -yoff;
        if (glyph.width < 0 || glyph.height < 0)
            return fail("Invalid glyph dimensions");
        if (!glyph.width || !glyph.height)
            return true;
        if (!bitmap)
            return fail("Glyph rasterization failed");
        const size_t width = size_t(glyph.width), height = size_t(glyph.height);
        if (width > glyph.pixels.max_size() / height)
            return fail("Glyph bitmap is too large");
        glyph.pixels.assign(bitmap.get(), bitmap.get() + width * height);
#else
        if (FT_Load_Char(m_face, codepoint, FT_LOAD_RENDER))
            return fail("Glyph rasterization failed");
        const auto& slot   = *m_face->glyph;
        const auto& bitmap = slot.bitmap;
        if (bitmap.width > unsigned(std::numeric_limits<int>::max())
            || bitmap.rows > unsigned(std::numeric_limits<int>::max()))
            return fail("Glyph bitmap is too large");
        glyph.width   = int(bitmap.width);
        glyph.height  = int(bitmap.rows);
        glyph.left    = slot.bitmap_left;
        glyph.top     = slot.bitmap_top;
        glyph.advance = int(slot.advance.x >> 6);
        if (!glyph.width || !glyph.height)
            return true;
        if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY
            && bitmap.pixel_mode != FT_PIXEL_MODE_MONO)
            return fail("Unsupported glyph bitmap format");
        const size_t width = size_t(glyph.width), height = size_t(glyph.height);
        if (width > glyph.pixels.max_size() / height)
            return fail("Glyph bitmap is too large");
        glyph.pixels.resize(width * height);
        for (int y = 0; y < glyph.height; ++y) {
            const auto* row = bitmap.buffer + ptrdiff_t(y) * bitmap.pitch;
            for (int x = 0; x < glyph.width; ++x) {
                glyph.pixels[size_t(y) * width + x]
                    = bitmap.pixel_mode == FT_PIXEL_MODE_MONO
                          ? ((row[x / 8] & (0x80 >> (x % 8))) ? 255 : 0)
                          : row[x];
            }
        }
#endif
        return true;
    }

    const std::string& family() const { return m_family; }
    const std::string& style() const { return m_style; }
    const std::string& error() const { return m_error; }

private:
    bool fail(const char* message)
    {
        m_error = message;
        return false;
    }

#ifdef USE_STRUETYPE
    static constexpr int FamilyName = 1;
    static constexpr int StyleName  = 2;

    struct BitmapDeleter {
        void operator()(unsigned char* bitmap) const
        {
            stt_FreeBitmap(bitmap, nullptr);
        }
    };

    std::string name(int id) const
    {
        int length        = 0;
        const char* value = stt_GetFontNameString(&m_font, &length,
                                                  STT_PLATFORM_ID_MICROSOFT,
                                                  STT_MS_EID_UNICODE_BMP,
                                                  STT_MS_LANG_ENGLISH, id);
        if (!value)
            value = stt_GetFontNameString(&m_font, &length,
                                          STT_PLATFORM_ID_UNICODE,
                                          STT_UNICODE_EID_UNICODE_2_0_BMP, 0,
                                          id);
        if (!value || length <= 0 || length % 2)
            return {};
        std::u16string decoded;
        decoded.reserve(size_t(length) / 2);
        for (int i = 0; i < length; i += 2)
            decoded.push_back(char16_t((uint16_t(uint8_t(value[i])) << 8)
                                       | uint8_t(value[i + 1])));
        return Strutil::utf16_to_utf8(decoded);
    }

    std::vector<unsigned char> m_data;
    stt_fontinfo m_font {};
    float m_scale = 0;
#else
    struct Library {
        FT_Library handle = nullptr;
        Library()
        {
            if (FT_Init_FreeType(&handle))
                handle = nullptr;
        }
        ~Library()
        {
            if (handle)
                FT_Done_FreeType(handle);
        }
    };
    FT_Face m_face = nullptr;
#endif
    std::string m_family, m_style, m_error;
};

}  // namespace
OIIO_NAMESPACE_END
