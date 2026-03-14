// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for display mode auto-selection and alpha detection.
// Build & run: bazelisk test //:display_mode_test

#include "display_mode.h"
#include "image.h"

#include <cassert>
#include <cstdio>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                                                                     \
    static void test_##name();                                                                                         \
    struct Register_##name                                                                                             \
    {                                                                                                                  \
        Register_##name() { test_##name(); }                                                                           \
    } reg_##name;                                                                                                      \
    static void test_##name()

#define EXPECT(expr)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        tests_run++;                                                                                                   \
        if (expr)                                                                                                      \
        {                                                                                                              \
            tests_passed++;                                                                                            \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr);                                         \
        }                                                                                                              \
    } while (0)

// --- Helper: build a small synthetic ImageData ---

static ImageData MakeImage(int w, int h, float alphaValue)
{
    ImageData img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < img.pixels.size(); i += 4)
    {
        img.pixels[i + 0] = 0.5f; // R
        img.pixels[i + 1] = 0.3f; // G
        img.pixels[i + 2] = 0.1f; // B
        img.pixels[i + 3] = alphaValue;
    }
    return img;
}

// --- AutoSelectDisplayMode ---

TEST(ZeroAlpha_FromRGB_SelectsIgnoreAlpha)
{
    EXPECT(AutoSelectDisplayMode(kDisplayModeRGB, true) == kDisplayModeRGBNoAlpha);
}

TEST(ZeroAlpha_FromSoloR_Unchanged) { EXPECT(AutoSelectDisplayMode(kDisplayModeR, true) == kDisplayModeR); }

TEST(ZeroAlpha_FromSoloG_Unchanged) { EXPECT(AutoSelectDisplayMode(kDisplayModeG, true) == kDisplayModeG); }

TEST(ZeroAlpha_FromSoloB_Unchanged) { EXPECT(AutoSelectDisplayMode(kDisplayModeB, true) == kDisplayModeB); }

TEST(ZeroAlpha_FromSoloA_Unchanged) { EXPECT(AutoSelectDisplayMode(kDisplayModeA, true) == kDisplayModeA); }

TEST(ZeroAlpha_FromIgnoreAlpha_Unchanged)
{
    EXPECT(AutoSelectDisplayMode(kDisplayModeRGBNoAlpha, true) == kDisplayModeRGBNoAlpha);
}

TEST(MeaningfulAlpha_FromIgnoreAlpha_RevertsToRGB)
{
    EXPECT(AutoSelectDisplayMode(kDisplayModeRGBNoAlpha, false) == kDisplayModeRGB);
}

TEST(MeaningfulAlpha_FromRGB_Unchanged) { EXPECT(AutoSelectDisplayMode(kDisplayModeRGB, false) == kDisplayModeRGB); }

TEST(MeaningfulAlpha_FromSoloR_Unchanged) { EXPECT(AutoSelectDisplayMode(kDisplayModeR, false) == kDisplayModeR); }

// --- NewFile pattern: pass kDisplayModeRGB to force auto-select ---

TEST(NewFile_ZeroAlpha_SelectsIgnoreAlpha)
{
    int mode = AutoSelectDisplayMode(kDisplayModeRGB, true);
    EXPECT(mode == kDisplayModeRGBNoAlpha);
}

TEST(NewFile_MeaningfulAlpha_SelectsRGB)
{
    int mode = AutoSelectDisplayMode(kDisplayModeRGB, false);
    EXPECT(mode == kDisplayModeRGB);
}

// --- ImageData.alphaAllZero detection (via LoadEXR internals) ---
// We test the flag by simulating what LoadEXR does: build pixel data,
// then check the flag after the same scan loop used in image.cpp.

static bool DetectAlphaAllZero(const ImageData& img)
{
    bool allZero = true;
    for (size_t i = 3; i < img.pixels.size() && allZero; i += 4)
        allZero = (img.pixels[i] == 0.0f);
    return allZero;
}

TEST(AlphaDetect_AllZero)
{
    ImageData img = MakeImage(4, 4, 0.0f);
    EXPECT(DetectAlphaAllZero(img) == true);
}

TEST(AlphaDetect_AllOpaque)
{
    ImageData img = MakeImage(4, 4, 1.0f);
    EXPECT(DetectAlphaAllZero(img) == false);
}

TEST(AlphaDetect_OneNonZeroPixel)
{
    ImageData img = MakeImage(4, 4, 0.0f);
    img.pixels[7] = 0.5f; // second pixel's alpha
    EXPECT(DetectAlphaAllZero(img) == false);
}

TEST(AlphaDetect_EmptyImage)
{
    ImageData img;
    EXPECT(DetectAlphaAllZero(img) == true); // vacuously true
}

TEST(AlphaDetect_SinglePixel_Zero)
{
    ImageData img = MakeImage(1, 1, 0.0f);
    EXPECT(DetectAlphaAllZero(img) == true);
}

TEST(AlphaDetect_SinglePixel_NonZero)
{
    ImageData img = MakeImage(1, 1, 0.001f);
    EXPECT(DetectAlphaAllZero(img) == false);
}

// --- End-to-end: synthetic image → detect → auto-select ---

TEST(EndToEnd_ZeroAlphaImage_AutoSelectsIgnoreAlpha)
{
    ImageData img = MakeImage(8, 8, 0.0f);
    bool allZero = DetectAlphaAllZero(img);
    int mode = AutoSelectDisplayMode(kDisplayModeRGB, allZero);
    EXPECT(mode == kDisplayModeRGBNoAlpha);
}

TEST(EndToEnd_OpaqueImage_StaysRGB)
{
    ImageData img = MakeImage(8, 8, 1.0f);
    bool allZero = DetectAlphaAllZero(img);
    int mode = AutoSelectDisplayMode(kDisplayModeRGB, allZero);
    EXPECT(mode == kDisplayModeRGB);
}

int main()
{
    printf("display_mode_test: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
