// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/unittest.h>

using namespace OIIO;

namespace {
constexpr int Width = 256, Height = 128, FontSize = 32;
constexpr int OriginX = 20, OriginY = 60;
const float TextColor[] = { 1.0f, 0.5f, 0.25f, 1.0f };

ImageBuf
canvas()
{
    ImageBuf image(ImageSpec(Width, Height, 4, TypeDesc::FLOAT));
    ImageBufAlgo::zero(image);
    return image;
}

void
check_equal(const ImageBuf& a, const ImageBuf& b)
{
    const auto result = ImageBufAlgo::compare(a, b, 0, 0);
    OIIO_CHECK_ASSERT(!result.error);
    OIIO_CHECK_EQUAL(result.maxerror, 0.0);
}

void
check_text(string_view font)
{
    // The accented glyph exercises UTF-8 decoding as well as ASCII rendering.
    const std::string text = "art \xc3\x85";
    const ROI bounds       = ImageBufAlgo::text_size(text, FontSize, font);
    OIIO_CHECK_ASSERT(bounds.defined());
    OIIO_CHECK_GT(bounds.width(), 0);
    OIIO_CHECK_GT(bounds.height(), 0);
    ImageBuf image = canvas();
    OIIO_CHECK_ASSERT(ImageBufAlgo::render_text(image, OriginX, OriginY, text,
                                                FontSize, font, TextColor));
    int covered   = 0;
    int rightmost = OriginX;
    for (ImageBuf::ConstIterator<float> p(image); !p.done(); ++p) {
        for (int c = 0; c < image.nchannels(); ++c) {
            OIIO_CHECK_ASSERT(std::isfinite(p[c]));
            OIIO_CHECK_GE(p[c], 0.0f);
            OIIO_CHECK_LE(p[c], 1.0f);
        }
        if (p[3] == 0)
            continue;
        ++covered;
        rightmost = std::max(rightmost, p.x());
        OIIO_CHECK_GE(p.x(), OriginX + bounds.xbegin);
        OIIO_CHECK_LT(p.x(), OriginX + bounds.xend);
        OIIO_CHECK_GE(p.y(), OriginY + bounds.ybegin);
        OIIO_CHECK_LT(p.y(), OriginY + bounds.yend);
        OIIO_CHECK_EQUAL(p[0], p[3]);
        OIIO_CHECK_EQUAL(p[1], 0.5f * p[3]);
        OIIO_CHECK_EQUAL(p[2], 0.25f * p[3]);
    }
    OIIO_CHECK_GT(covered, 0);
    OIIO_CHECK_GT(rightmost, OriginX + bounds.xbegin + bounds.width() / 2);

    ImageBuf unix_lines = canvas(), windows_lines = canvas();
    OIIO_CHECK_ASSERT(ImageBufAlgo::render_text(unix_lines, OriginX, OriginY,
                                                "art\nOSL", FontSize, font));
    OIIO_CHECK_ASSERT(ImageBufAlgo::render_text(windows_lines, OriginX, OriginY,
                                                "art\r\nOSL", FontSize, font));
    check_equal(unix_lines, windows_lines);

    ImageBuf aligned = canvas(), positioned = canvas();
    OIIO_CHECK_ASSERT(ImageBufAlgo::render_text(
        aligned, Width / 2, Height / 2, text, FontSize, font, TextColor,
        ImageBufAlgo::TextAlignX::Center, ImageBufAlgo::TextAlignY::Center));
    OIIO_CHECK_ASSERT(ImageBufAlgo::render_text(
        positioned, Width / 2 - (bounds.width() / 2 + bounds.xbegin),
        Height / 2 - (bounds.height() / 2 + bounds.ybegin), text, FontSize,
        font, TextColor));
    check_equal(aligned, positioned);

    ImageBuf shadowed = canvas();
    ImageBufAlgo::fill(shadowed, 1.0f);
    OIIO_CHECK_ASSERT(ImageBufAlgo::render_text(
        shadowed, OriginX, OriginY, text, FontSize, font, TextColor,
        ImageBufAlgo::TextAlignX::Left, ImageBufAlgo::TextAlignY::Baseline, 2));
    bool has_shadow = false;
    for (ImageBuf::ConstIterator<float> p(shadowed); !p.done(); ++p)
        has_shadow |= p[0] < 1.0f;
    OIIO_CHECK_ASSERT(has_shadow);

    ImageBuf empty = canvas(), blank = canvas();
    OIIO_CHECK_ASSERT(
        ImageBufAlgo::render_text(empty, 0, 0, "\r\n", FontSize, font));
    check_equal(empty, blank);
    OIIO_CHECK_ASSERT(!ImageBufAlgo::text_size(text, 0, font).defined());
    OIIO_CHECK_ASSERT(!ImageBufAlgo::render_text(empty, 0, 0, text, 0, font));
    OIIO_CHECK_ASSERT(empty.has_error());
    empty.geterror();
    OIIO_CHECK_ASSERT(
        !ImageBufAlgo::render_text(empty, std::numeric_limits<int>::max(),
                                   std::numeric_limits<int>::max(), text,
                                   FontSize, font));
    OIIO_CHECK_ASSERT(empty.has_error());
    empty.geterror();
    OIIO_CHECK_ASSERT(
        !ImageBufAlgo::render_text(empty, OriginX, OriginY, text, FontSize,
                                   font, TextColor,
                                   ImageBufAlgo::TextAlignX::Left,
                                   ImageBufAlgo::TextAlignY::Baseline, -1));
    OIIO_CHECK_ASSERT(empty.has_error());
    empty.geterror();
}
}  // namespace

int
main(int argc, char** argv)
{
    OIIO_CHECK_EQUAL(argc, 2);
    if (argc != 2)
        return 1;
#if defined(USE_FREETYPE) || defined(USE_STRUETYPE)
    const std::string font = argv[1];
    OIIO::attribute("font_searchpath", Filesystem::parent_path(font));
    check_text(font);
    check_text("DroidSans");
    check_text("Droid Sans Regular");
    check_text("");
    OIIO_CHECK_ASSERT(
        OIIO::get_string_attribute("font_family_list").find("Droid Sans")
        != std::string::npos);
    OIIO_CHECK_ASSERT(
        OIIO::get_string_attribute("font_style_list:Droid Sans").find("Regular")
        != std::string::npos);

    // Existing files with truncated headers must fail, rather than fall back.
    const std::string invalid = Filesystem::temp_directory_path() + "/"
                                + Filesystem::unique_path(
                                    "oiio-font-%%%%-%%%%.ttf");
    OIIO_CHECK_ASSERT(
        Filesystem::write_text_file(invalid,
                                    string_view("\0\1broken font", 13)));
    ImageBuf image = canvas();
    OIIO_CHECK_ASSERT(
        !ImageBufAlgo::text_size("art", FontSize, invalid).defined());
    OIIO_CHECK_ASSERT(!ImageBufAlgo::render_text(image, 0, OriginY, "art",
                                                 FontSize, invalid));
    OIIO_CHECK_ASSERT(image.has_error());
    image.geterror();
    OIIO_CHECK_ASSERT(Filesystem::remove(invalid));
#else
    ImageBuf image = canvas();
    OIIO_CHECK_ASSERT(
        !ImageBufAlgo::text_size("art", FontSize, argv[1]).defined());
    OIIO_CHECK_ASSERT(!ImageBufAlgo::render_text(image, 0, OriginY, "art",
                                                 FontSize, argv[1]));
    OIIO_CHECK_ASSERT(image.has_error());
    image.geterror();
#endif
    return unit_test_failures;
}
